param(
    [Parameter(Mandatory=$true)][int]$InstallerProcessId,
    [Parameter(Mandatory=$true)][string]$InstallDir,
    [Parameter(Mandatory=$true)][string]$ExpectedVersion,
    [Parameter(Mandatory=$true)][string]$LogPath,
    [switch]$NoRestart,
    [switch]$NonInteractive
)
$ErrorActionPreference = 'Stop'
try {
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class WorldbenchInstallWait {
 [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
 [DllImport("kernel32.dll")] public static extern uint WaitForSingleObject(IntPtr handle, uint timeout);
 [DllImport("kernel32.dll")] public static extern bool GetExitCodeProcess(IntPtr handle, out uint code);
 [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr handle);
}
"@
    $handle = [WorldbenchInstallWait]::OpenProcess(0x00101000, $false, $InstallerProcessId)
    if ($handle -eq [IntPtr]::Zero) { throw 'Cannot observe installer. Check installation.log before restarting.' }
    try {
        # Parent stays open until this process holds a waitable installer handle.
        'ready' | Set-Content -LiteralPath ($LogPath + '.ready') -Encoding ASCII
        if ([WorldbenchInstallWait]::WaitForSingleObject($handle, 900000) -ne 0) { throw 'Installer has not completed. Check installation.log; no automatic restart.' }
        [uint32]$code = 0
        if (-not [WorldbenchInstallWait]::GetExitCodeProcess($handle, [ref]$code)) { throw 'Cannot read installer result.' }
        if ($code -ne 0) { throw "Installer failed with exit code $code. Check installation.log." }
    } finally { [WorldbenchInstallWait]::CloseHandle($handle) | Out-Null }
    $info = Get-Content -LiteralPath (Join-Path $InstallDir 'build-info.json') -Raw | ConvertFrom-Json
    if ($info.version -ne $ExpectedVersion) { throw 'Installed version does not match downloaded update.' }
    $exe = Join-Path $InstallDir 'Worldbench.exe'
    $stream = [IO.File]::OpenRead($exe)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $digest = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','').ToLowerInvariant() }
    finally { $stream.Dispose(); $sha.Dispose() }
    if ($digest -ne $info.executable_sha256) { throw 'Installed executable failed verification.' }
    if (-not $NoRestart) { Start-Process -FilePath $exe -WorkingDirectory $InstallDir }
    Write-Output 'Installation verified; normal-user restart requested.'
    exit 0
} catch {
    $message = "Worldbench update could not complete automatically.`n$($_.Exception.Message)`nLog: $LogPath"
    $message | Set-Content -LiteralPath ($LogPath + '.result.txt') -Encoding UTF8
    if (-not $NonInteractive) {
        Add-Type -AssemblyName System.Windows.Forms
        [System.Windows.Forms.MessageBox]::Show($message, 'Worldbench update') | Out-Null
    }
    Write-Output $message
    exit 1
}
