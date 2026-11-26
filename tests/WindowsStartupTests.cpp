#include "WindowsStartup.h"
#include <iostream>
#include <stdexcept>
int main() {
    using namespace wb::startup;
    int count = 0;
    auto check = [&](bool ok, const char *message) { ++count; if (!ok) throw std::runtime_error(message); };
    try {
        check(!needsDesktopLaunch({8192, false, true}), "normal desktop editor must not relaunch");
        check(needsDesktopLaunch({12288, true, true}), "installer-inherited admin must relaunch");
        check(needsDesktopLaunch({12288, false, true}), "high-integrity token must relaunch even without elevation flag");
        check(acceptsDesktopToken({8192, false, true}), "medium desktop token accepted");
        check(!acceptsDesktopToken({12288, true, true}), "elevated shell cannot produce normal editor");
        check(!acceptsDesktopToken({8192, false, false}), "unknown token rejected");
        check(!acceptsDesktopToken({4096, false, true}), "low integrity shell rejected");
        const auto own = context(GetCurrentProcess());
        check(own.valid, "actual current token readable");
        DWORD pid = 0; GetWindowThreadProcessId(GetShellWindow(), &pid);
        HANDLE shell = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        check(shell != nullptr, "desktop shell discoverable");
        const auto desktop = context(shell); CloseHandle(shell);
        check(acceptsDesktopToken(desktop), "actual Explorer token is medium and non elevated");
        std::wstring error;
        if (!needsDesktopLaunch(own)) check(ensureDesktopUser(error) == Result::Continue, "normal startup remains in same process");
        std::cout << "PASS " << count << " startup checks; integrity=" << own.integrity << " elevated=" << own.elevated << "\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
