param(
    [Parameter(Mandatory=$true)][string]$ArchivePath,
    [Parameter(Mandatory=$true)][string]$InstallDir,
    [Parameter(Mandatory=$true)][int]$ProcessId,
    [Parameter(Mandatory=$true)][string]$RestartPath
)
$ErrorActionPreference = 'Stop'
$stage = Join-Path ([IO.Path]::GetTempPath()) ("Worldbench-update-" + [guid]::NewGuid().ToString('N'))
$backup = Join-Path ([IO.Path]::GetTempPath()) ("Worldbench-backup-" + [guid]::NewGuid().ToString('N'))
try {
    if (Get-Process -Id $ProcessId -ErrorAction SilentlyContinue) {
        Wait-Process -Id $ProcessId -Timeout 60 -ErrorAction SilentlyContinue
    }
    New-Item -ItemType Directory -Force -Path $stage | Out-Null
    Expand-Archive -LiteralPath $ArchivePath -DestinationPath $stage -Force
    $entries = @(Get-ChildItem -LiteralPath $stage -Force)
    $source = $stage
    if ($entries.Count -eq 1 -and $entries[0].PSIsContainer) { $source = $entries[0].FullName }
    New-Item -ItemType Directory -Force -Path $backup | Out-Null
    if (Test-Path -LiteralPath $InstallDir) {
        Copy-Item -Path (Join-Path $InstallDir '*') -Destination $backup -Recurse -Force -ErrorAction SilentlyContinue
    }
    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
    Copy-Item -Path (Join-Path $source '*') -Destination $InstallDir -Recurse -Force
    Start-Process -FilePath $RestartPath -WorkingDirectory $InstallDir
} catch {
    $message = $_.Exception.Message
    try {
        $args = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$PSCommandPath,'-ArchivePath',$ArchivePath,'-InstallDir',$InstallDir,'-ProcessId','0','-RestartPath',$RestartPath)
        Start-Process powershell.exe -Verb RunAs -ArgumentList $args -Wait
    } catch {
        Add-Content -LiteralPath (Join-Path ([IO.Path]::GetTempPath()) 'Worldbench-update-error.log') -Value $message
    }
}
Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $ArchivePath -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $backup -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $PSCommandPath -Force -ErrorAction SilentlyContinue
