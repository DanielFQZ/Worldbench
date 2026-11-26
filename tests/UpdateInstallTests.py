"""Exercise the shipped PowerShell updater on disposable installations (Windows)."""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SHELL = str(Path(os.environ['SystemRoot']) / 'System32/WindowsPowerShell/v1.0/powershell.exe')
checks = 0


def check(value, message):
    global checks
    assert value, message
    checks += 1


def snapshot(directory):
    return {str(p.relative_to(directory)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in directory.rglob('*') if p.is_file()}


def scenario(kind):
    # Keep the failure artifact for diagnosis; all paths belong to this fixture.
    root = Path(tempfile.mkdtemp(prefix='Worldbench-updater-test-'))
    install = root / '安装目录 with spaces'
    install.mkdir()
    (install / 'Worldbench.exe').write_bytes(b'old executable')
    (install / 'b.bin').write_bytes(b'old asset')
    (install / 'user.wbench').write_bytes(b'user project, never overwrite')
    before = snapshot(install)
    archive = root / 'release.zip'
    with zipfile.ZipFile(archive, 'w') as z:
        z.writestr('Worldbench.exe', b'new executable')
        z.writestr('a-new/sub.bin', b'new file')
        z.writestr('b.bin', b'new asset')
    updater = root / 'Updater.ps1'
    shutil.copyfile(ROOT / 'app/updates/WorldbenchUpdate.ps1', updater)
    args = ['-ArchivePath', str(archive), '-InstallDir', str(install), '-ProcessId', '0',
            '-RestartPath', str(install / 'Worldbench.exe'), '-NoRestart', '-NonInteractive']
    command = [SHELL, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(updater)] + args
    handle = None
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.restype = ctypes.c_void_p
    kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_ulong, ctypes.c_ulong,
                                  ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_void_p]
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    if kind == 'readonly':
        os.chmod(install / 'b.bin', 0o444)
    elif kind == 'locked':
        handle = kernel.CreateFileW(str(install / 'b.bin'), 0x80000000, 1, None, 3, 0, None)
        check(handle not in (None, ctypes.c_void_p(-1).value), 'create exclusive write lock')
    elif kind == 'acl':
        identity = subprocess.check_output(['whoami', '/user', '/fo', 'csv', '/nh'], text=True)
        sid = identity.strip().split(',')[-1].strip('"')
        subprocess.run(['icacls', str(install / 'b.bin'), '/deny', f'*{sid}:(W)'], check=True, capture_output=True)
    elif kind == 'rollback':
        # Fail verification once, after a real copy, then allow the real rollback.
        wrapper = root / 'fail-verification.ps1'
        target = str(install / 'b.bin').replace("'", "''")
        original = updater.read_text(encoding='utf-8-sig')
        # Inject only a test-local hash function branch; the real updater remains unchanged.
        original = original.replace('function Get-UpdateHash([string]$LiteralPath) {',
            "function Get-UpdateHash([string]$LiteralPath) {\n"
            "    if (-not $script:injected -and $LiteralPath -eq '" + target + "' -and "
            "[IO.File]::ReadAllText($LiteralPath) -eq 'new asset') { "
            "$script:injected=$true; throw 'Injected verification failure after writing the asset' }")
        wrapper.write_text(original, encoding='utf-8-sig')
        command[5] = str(wrapper)
    try:
        result = subprocess.run(command, capture_output=True, timeout=60)
        output = result.stdout.decode('utf-8', errors='replace')
        (root / 'output.txt').write_bytes(result.stdout + result.stderr)
        if handle:
            kernel.CloseHandle(handle)
            handle = None
        if kind == 'acl':
            subprocess.run(['icacls', str(install / 'b.bin'), '/remove:d', f'*{sid}'], check=True, capture_output=True)
        if kind == 'success':
            check(result.returncode == 0, f'{kind}: {output}')
            check((install / 'Worldbench.exe').read_bytes() == b'new executable', 'updated executable')
            check((install / 'b.bin').read_bytes() == b'new asset', 'updated existing asset')
            check((install / 'a-new/sub.bin').read_bytes() == b'new file', 'new subdirectory installed')
            check((install / 'user.wbench').read_bytes() == b'user project, never overwrite', 'unrelated project preserved')
        else:
            check(result.returncode != 0, f'{kind} rejects installation')
            check(snapshot(install) == before, f'{kind}: all original files restored and new files removed')
            check(archive.exists(), f'{kind}: recovery ZIP retained')
            check(('All replaced files were restored.' if kind == 'rollback' else
                   'No application files were changed.') in output, f'{kind}: accurate failure report: {output}')
        print(kind, 'passed', root, flush=True)
    finally:
        if handle:
            kernel.CloseHandle(handle)
        if kind == 'readonly':
            os.chmod(install / 'b.bin', 0o666)
        elif kind == 'acl':
            subprocess.run(['icacls', str(install / 'b.bin'), '/remove:d', f'*{sid}'], check=True, capture_output=True)


for case in ('success', 'readonly', 'locked', 'acl', 'rollback'):
    scenario(case)
print(f'PASS {checks} updater installation checks')
