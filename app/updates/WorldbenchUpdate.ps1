param(
    [Parameter(Mandatory=$true)][string]$ArchivePath,
    [Parameter(Mandatory=$true)][string]$InstallDir,
    [Parameter(Mandatory=$true)][int]$ProcessId,
    [Parameter(Mandatory=$true)][string]$RestartPath,
    [switch]$NoRestart,
    [switch]$NonInteractive
)
$ErrorActionPreference = 'Stop'
function Get-UpdateHash([string]$LiteralPath) {
    $stream = [IO.File]::OpenRead($LiteralPath)
    $hash = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($hash.ComputeHash($stream)) }
    finally { $hash.Dispose(); $stream.Dispose() }
}
$stage = Join-Path ([IO.Path]::GetTempPath()) ("Worldbench-update-" + [guid]::NewGuid().ToString('N'))
$backup = Join-Path ([IO.Path]::GetTempPath()) ("Worldbench-backup-" + [guid]::NewGuid().ToString('N'))
$replaced = $false
$journal = New-Object 'System.Collections.Generic.List[object]'
$createdDirectories = New-Object 'System.Collections.Generic.List[string]'
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
    $InstallDir = [IO.Path]::GetFullPath($InstallDir).TrimEnd('\')
    if (-not (Test-Path -LiteralPath $InstallDir -PathType Container)) { throw 'Installation directory is missing. Use the setup installer.' }
    $plan = @(Get-ChildItem -LiteralPath $source -File -Recurse -Force | Sort-Object FullName | ForEach-Object {
        $relative = $_.FullName.Substring($source.Length + 1)
        $target = Join-Path $InstallDir $relative
        if (-not ([IO.Path]::GetFullPath($target)).StartsWith($InstallDir + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid update target.' }
        [pscustomobject]@{Source=$_.FullName;Target=$target;Relative=$relative;Existed=(Test-Path -LiteralPath $target -PathType Leaf)}
    })
    # Preflight every file before modifying any installed application content.
    # ReadWrite with Open never truncates; it catches both ACL denial and locks.
    $probed = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($item in $plan) {
        $parent = [IO.Path]::GetDirectoryName($item.Target)
        while (-not (Test-Path -LiteralPath $parent)) { $parent = [IO.Path]::GetDirectoryName($parent) }
        $cursor = $parent
        while ($cursor) {
            if ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Linked destination not supported: $cursor" }
            $cursor = [IO.Path]::GetDirectoryName($cursor)
        }
        if ($probed.Add($parent)) {
            $probe = Join-Path $parent ('.worldbench-write-check-' + [guid]::NewGuid().ToString('N'))
            try {
                $stream = [IO.File]::Open($probe, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
                $stream.Dispose()
            } finally { if (Test-Path -LiteralPath $probe) { [IO.File]::Delete($probe) } }
        }
        if (Test-Path -LiteralPath $item.Target) {
            if (-not $item.Existed -or ((Get-Item -LiteralPath $item.Target -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Not a regular file: $($item.Target)" }
            $stream = [IO.File]::Open($item.Target, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::Read)
            $stream.Dispose()
        }
    }
    New-Item -ItemType Directory -Force -Path $backup | Out-Null
    foreach ($item in $plan) {
        if ($item.Existed) {
            $old = Join-Path $backup $item.Relative
            [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($old)) | Out-Null
            [IO.File]::Copy($item.Target, $old, $false)
            if ((Get-UpdateHash -LiteralPath $old) -ne (Get-UpdateHash -LiteralPath $item.Target)) { throw 'Backup verification failed.' }
        }
    }
    $replaced = $true
    foreach ($item in $plan) {
        $missing = New-Object 'System.Collections.Generic.List[string]'
        $parent = [IO.Path]::GetDirectoryName($item.Target)
        while (-not (Test-Path -LiteralPath $parent)) { $missing.Add($parent); $parent = [IO.Path]::GetDirectoryName($parent) }
        for ($i=$missing.Count-1; $i -ge 0; --$i) {
            [IO.Directory]::CreateDirectory($missing[$i]) | Out-Null
            $createdDirectories.Add($missing[$i])
        }
        $journal.Add($item)
        [IO.File]::Copy($item.Source, $item.Target, $true)
        if ((Get-UpdateHash -LiteralPath $item.Source) -ne (Get-UpdateHash -LiteralPath $item.Target)) { throw "Update verification failed: $($item.Target)" }
    }
} catch {
    $message = $_.Exception.Message
    $rollbackErrors = New-Object 'System.Collections.Generic.List[string]'
    for ($i=$journal.Count-1; $i -ge 0; --$i) {
        $item=$journal[$i]
        try {
            if ($item.Existed) {
                $old=Join-Path $backup $item.Relative
                [IO.File]::Copy($old, $item.Target, $true)
                if ((Get-UpdateHash -LiteralPath $old) -ne (Get-UpdateHash -LiteralPath $item.Target)) { throw "Restore verification failed: $($item.Target)" }
            } elseif (Test-Path -LiteralPath $item.Target) { [IO.File]::Delete($item.Target) }
        } catch { $rollbackErrors.Add($_.Exception.Message) }
    }
    for ($i=$createdDirectories.Count-1; $i -ge 0; --$i) {
        try { [IO.Directory]::Delete($createdDirectories[$i], $false) } catch { $rollbackErrors.Add($_.Exception.Message) }
    }
    if ($journal.Count -eq 0) { $message += "`nNo application files were changed." }
    elseif ($rollbackErrors.Count -eq 0) { $message += "`nAll replaced files were restored." }
    else { $message += "`nRollback incomplete: " + ($rollbackErrors -join '; ') }
    Add-Content -LiteralPath (Join-Path ([IO.Path]::GetTempPath()) 'Worldbench-update-error.log') -Value $message -Encoding UTF8
    if (-not $NonInteractive) {
        Add-Type -AssemblyName System.Windows.Forms
        [System.Windows.Forms.MessageBox]::Show("Update failed. Use the setup installer for protected directories. Backup: $backup`n$message", 'Worldbench update') | Out-Null
    }
    Write-Output $message
    # Preserve the downloaded archive and backup for recovery. Do not restart
    # elevated: it prevents Explorer drag/drop and is not needed per-user.
    exit 1
}
if (-not $NoRestart) {
    try { Start-Process -FilePath $RestartPath -WorkingDirectory $InstallDir }
    catch { Write-Output "Update completed; automatic restart failed. Start Worldbench manually." }
}
Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $ArchivePath -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $backup -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $PSCommandPath -Force -ErrorAction SilentlyContinue
