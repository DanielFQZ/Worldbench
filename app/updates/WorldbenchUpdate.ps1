param(
    [Parameter(Mandatory=$true)][string]$ArchivePath,
    [Parameter(Mandatory=$true)][string]$InstallDir,
    [Parameter(Mandatory=$true)][int]$ProcessId,
    [Parameter(Mandatory=$true)][string]$RestartPath
)
$ErrorActionPreference = 'Stop'
$stage = Join-Path ([IO.Path]::GetTempPath()) ("Worldbench-update-" + [guid]::NewGuid().ToString('N'))
$backup = Join-Path ([IO.Path]::GetTempPath()) ("Worldbench-backup-" + [guid]::NewGuid().ToString('N'))
$replaced = $false
try {
    if ($ProcessId -gt 0 -and (Get-Process -Id $ProcessId -ErrorAction SilentlyContinue)) {
        Wait-Process -Id $ProcessId -Timeout 60 -ErrorAction Stop
    }
    New-Item -ItemType Directory -Force -Path $stage | Out-Null
    Expand-Archive -LiteralPath $ArchivePath -DestinationPath $stage -Force
    $entries = @(Get-ChildItem -LiteralPath $stage -Force)
    $source = $stage
    if ($entries.Count -eq 1 -and $entries[0].PSIsContainer) { $source = $entries[0].FullName }
    if (-not (Test-Path -LiteralPath (Join-Path $source 'Worldbench.exe'))) { throw 'Update archive has no Worldbench.exe.' }
    New-Item -ItemType Directory -Force -Path $backup | Out-Null
    if (Test-Path -LiteralPath $InstallDir) {
        Copy-Item -Path (Join-Path $InstallDir '*') -Destination $backup -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
    $replaced = $true
    Copy-Item -Path (Join-Path $source '*') -Destination $InstallDir -Recurse -Force
    Start-Process -FilePath $RestartPath -WorkingDirectory $InstallDir
} catch {
    $message = $_.Exception.Message
    if ($replaced) {
        try { Copy-Item -Path (Join-Path $backup '*') -Destination $InstallDir -Recurse -Force }
        catch { $message += "`nRollback failed: " + $_.Exception.Message }
    }
    Add-Content -LiteralPath (Join-Path ([IO.Path]::GetTempPath()) 'Worldbench-update-error.log') -Value $message
    Add-Type -AssemblyName System.Windows.Forms
    [System.Windows.Forms.MessageBox]::Show("Update failed. Please use the setup installer. Backup: $backup`n$message", 'Worldbench update') | Out-Null
    # Preserve the downloaded archive and backup for recovery. Do not restart
    # elevated: it prevents Explorer drag/drop and is not needed per-user.
    exit 1
}
Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $ArchivePath -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $backup -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $PSCommandPath -Force -ErrorAction SilentlyContinue
