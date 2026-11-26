#pragma once
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <userenv.h>
#include <vector>

namespace wb::startup {
struct Context { DWORD integrity = 0; bool elevated = false; bool valid = false; };
inline Context context(HANDLE process) {
    Context result;
    HANDLE token = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &token)) return result;
    TOKEN_ELEVATION elevation{}; DWORD bytes = 0;
    if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &bytes)) {
        GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &bytes);
        std::vector<BYTE> buffer(bytes);
        if (bytes && GetTokenInformation(token, TokenIntegrityLevel, buffer.data(), bytes, &bytes)) {
            auto *label = reinterpret_cast<TOKEN_MANDATORY_LABEL *>(buffer.data());
            const auto count = *GetSidSubAuthorityCount(label->Label.Sid);
            if (count) {
                result.integrity = *GetSidSubAuthority(label->Label.Sid, count - 1);
                result.elevated = elevation.TokenIsElevated != 0;
                result.valid = true;
            }
        }
    }
    CloseHandle(token);
    return result;
}
inline bool needsDesktopLaunch(const Context &current) {
    return current.valid && (current.elevated || current.integrity > SECURITY_MANDATORY_MEDIUM_RID);
}
inline bool acceptsDesktopToken(const Context &desktop) {
    return desktop.valid && !desktop.elevated && desktop.integrity == SECURITY_MANDATORY_MEDIUM_RID;
}
enum class Result { Continue, Relaunched, Failed };
inline Result ensureDesktopUser(std::wstring &error) {
    const auto current = context(GetCurrentProcess());
    if (!current.valid) { error = L"Cannot inspect editor startup permissions."; return Result::Failed; }
    if (!needsDesktopLaunch(current)) return Result::Continue;
    // An installer launched through runas cannot reliably recover its original
    // user. Use the desktop shell's actual primary token, not a guessed parent.
    DWORD pid = 0;
    GetWindowThreadProcessId(GetShellWindow(), &pid);
    HANDLE shell = pid ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid) : nullptr;
    if (!shell) { error = L"Cannot find the desktop user. Close setup and start Worldbench normally."; return Result::Failed; }
    const auto desktop = context(shell);
    HANDLE token = nullptr, primary = nullptr;
    const bool opened = acceptsDesktopToken(desktop) && OpenProcessToken(shell, TOKEN_QUERY | TOKEN_DUPLICATE, &token);
    CloseHandle(shell);
    if (!opened) { error = L"Desktop permissions are unavailable. Start Worldbench normally from Explorer."; return Result::Failed; }
    const bool duplicated = DuplicateTokenEx(token, MAXIMUM_ALLOWED, nullptr, SecurityImpersonation, TokenPrimary, &primary);
    CloseHandle(token);
    if (!duplicated) { error = L"Cannot prepare normal-user startup."; return Result::Failed; }
    std::vector<wchar_t> exe(32768), directory(32768);
    const auto exeLength = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
    const auto dirLength = GetCurrentDirectoryW(static_cast<DWORD>(directory.size()), directory.data());
    void *environment = nullptr;
    if (!exeLength || exeLength >= exe.size() || !dirLength || dirLength >= directory.size() ||
        !CreateEnvironmentBlock(&environment, primary, FALSE)) {
        CloseHandle(primary); error = L"Cannot prepare the desktop user's environment."; return Result::Failed;
    }
    std::wstring command = GetCommandLineW(); // Preserve Unicode, quoting, project path and all switches.
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const bool started = CreateProcessWithTokenW(primary, 0, exe.data(), command.data(),
        CREATE_UNICODE_ENVIRONMENT, environment, directory.data(), &startup, &process);
    const DWORD failure = started ? 0 : GetLastError();
    DestroyEnvironmentBlock(environment);
    CloseHandle(primary);
    if (!started) { error = L"Normal-user startup failed (Windows error " + std::to_wstring(failure) +
        L"). Close setup and start Worldbench normally from Explorer."; return Result::Failed; }
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    return Result::Relaunched;
}
} // namespace wb::startup
#endif
