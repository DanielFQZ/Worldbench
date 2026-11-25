param(
    [string]$Version = '0.26.2-pre.1',
    [string]$Root = (Split-Path $PSScriptRoot -Parent),
    [string]$Publisher = 'Worldbench Project'
)
$ErrorActionPreference = 'Stop'
$dist = Join-Path $Root ("dist\Worldbench-" + $Version)
$release = Join-Path $Root ("dist\release-" + $Version)
if (-not (Test-Path -LiteralPath (Join-Path $dist 'Worldbench.exe'))) { throw "Build package not found: $dist" }
Remove-Item -LiteralPath $release -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $release | Out-Null
$archive = Join-Path $release ("Worldbench-" + $Version + '-win-x64.zip')
Compress-Archive -Path (Join-Path $dist '*') -DestinationPath $archive -CompressionLevel Optimal

$installScript = Join-Path $release 'install.ps1'
@'
param([string]$PayloadRoot)
$ErrorActionPreference = 'Stop'
$target = Join-Path $env:LOCALAPPDATA 'Programs\Worldbench'
New-Item -ItemType Directory -Force -Path $target | Out-Null
Copy-Item -Path (Join-Path $PayloadRoot '*') -Destination $target -Recurse -Force
$shortcut = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Worldbench.lnk'
$shell = New-Object -ComObject WScript.Shell
$link = $shell.CreateShortcut($shortcut)
$link.TargetPath = Join-Path $target 'Worldbench.exe'
$link.WorkingDirectory = $target
$link.IconLocation = Join-Path $target 'Worldbench.exe'
$link.Save()
Start-Process -FilePath (Join-Path $target 'Worldbench.exe') -WorkingDirectory $target
'@ | Set-Content -LiteralPath $installScript -Encoding UTF8

$cmd = Join-Path $release 'install.cmd'
'@echo off' + "`r`n" + 'set "STAGE=%TEMP%\Worldbench-setup-%RANDOM%"' + "`r`n" + 'mkdir "%STAGE%"' + "`r`n" + 'powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "Expand-Archive -LiteralPath ''%~dp0Worldbench-' + $Version + '-win-x64.zip'' -DestinationPath ''%STAGE%'' -Force"' + "`r`n" + 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" -PayloadRoot "%STAGE%"' + "`r`n" | Set-Content -LiteralPath $cmd -Encoding ASCII

$sed = Join-Path $release 'Worldbench.sed'
@"
[Version]
Class=IEXPRESS
SEDVersion=3
[Options]
PackagePurpose=InstallApp
ShowInstallProgramWindow=1
HideExtractAnimation=1
UseLongFileName=1
InsideCompressed=1
TargetName=$release\Worldbench-$Version-setup.exe
FriendlyName=Worldbench $Version
AppLaunched=install.cmd
PostInstallCmd=<None>
SourceFiles=SourceFiles
[SourceFiles]
SourceFiles0=$release
[SourceFiles0]
install.cmd=
install.ps1=
Worldbench-$Version-win-x64.zip=
"@ | Set-Content -LiteralPath $sed -Encoding ASCII
$iexpress = Join-Path $env:WINDIR 'System32\iexpress.exe'
if (-not (Test-Path -LiteralPath $iexpress)) { throw 'Windows IExpress was not found.' }
$setup = Join-Path $release ("Worldbench-" + $Version + '-setup.exe')
Remove-Item -LiteralPath $setup -Force -ErrorAction SilentlyContinue
& $iexpress /N /Q $sed
if ($LASTEXITCODE) { throw "IExpress failed with exit code $LASTEXITCODE" }
for ($attempt = 0; $attempt -lt 60 -and -not (Test-Path -LiteralPath $setup); ++$attempt) { Start-Sleep -Milliseconds 500 }
if (-not (Test-Path -LiteralPath $setup)) { throw "IExpress did not create installer: $setup" }

# A self-signed certificate makes the package tamper-evident. Windows will show
# the publisher as untrusted until this certificate is installed by the user.
$certPath = Join-Path $release 'Worldbench-self-signed.cer'
$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq "CN=$Publisher" } | Select-Object -First 1
if (-not $cert) {
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject "CN=$Publisher" -CertStoreLocation Cert:\CurrentUser\My -HashAlgorithm SHA256
}
Export-Certificate -Cert $cert -FilePath $certPath -Force | Out-Null
$signed = Set-AuthenticodeSignature -FilePath $setup -Certificate $cert -HashAlgorithm SHA256
if ($signed.Status -notin @('Valid','UnknownError')) { throw "Installer signing failed: $($signed.Status)" }
$hashLines = @($setup, $archive) | ForEach-Object {
    $hash = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $([IO.Path]::GetFileName($_))"
}
$hashLines | Set-Content -LiteralPath (Join-Path $release 'SHA256SUMS.txt') -Encoding ASCII
Remove-Item -LiteralPath $installScript,$cmd,$sed -Recurse -Force -ErrorAction SilentlyContinue
Write-Host "Release assets created in $release"
