param(
    [string]$Version = '0.26.3-pre.1',
    [string]$Root = (Split-Path $PSScriptRoot -Parent),
    [string]$Publisher = 'Worldbench Project'
)
$ErrorActionPreference = 'Stop'
$dist = Join-Path $Root ("dist\Worldbench-" + $Version)
$release = Join-Path $Root ("dist\release-" + $Version)
if (-not (Test-Path -LiteralPath (Join-Path $dist 'Worldbench.exe'))) { throw "Build package not found: $dist" }

# Inno Setup provides a normal Windows wizard, destination selection,
# shortcuts, file association and a registered uninstaller.
$iscc = @(
    (Get-Command ISCC.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -ErrorAction SilentlyContinue),
    (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
    (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
    (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe')
) | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
if (-not $iscc) { throw 'Inno Setup 6 ISCC.exe is required. Install it with: winget install --id JRSoftware.InnoSetup --exact' }
$icon = Join-Path $Root 'app\icons\worldbench.ico'
if (-not (Test-Path -LiteralPath $icon)) { throw "Application icon not found: $icon" }
Remove-Item -LiteralPath $release -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $release | Out-Null
$archive = Join-Path $release ("Worldbench-" + $Version + '-win-x64.zip')
Compress-Archive -Path (Join-Path $dist '*') -DestinationPath $archive -CompressionLevel Optimal

$iss = Join-Path $release 'Worldbench.iss'
$template = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'Worldbench.iss') -Raw -Encoding UTF8
$template = $template.Replace('@VERSION@', $Version).Replace('@SOURCE@', $dist).Replace('@OUTPUT@', $release).Replace('@ICON@', $icon)
[IO.File]::WriteAllText($iss, $template, (New-Object System.Text.UTF8Encoding($true)))

$setup = Join-Path $release ("Worldbench-" + $Version + '-setup.exe')
Remove-Item -LiteralPath $setup -Force -ErrorAction SilentlyContinue
& $iscc /Qp $iss
if ($LASTEXITCODE) { throw "Inno Setup failed with exit code $LASTEXITCODE" }
if (-not (Test-Path -LiteralPath $setup)) { throw "Inno Setup did not create installer: $setup" }

# A self-signed certificate makes the package tamper-evident.
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
Remove-Item -LiteralPath $iss -Force -ErrorAction SilentlyContinue
Write-Host "Release assets created in $release"
