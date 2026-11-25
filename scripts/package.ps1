param(
    [string]$Version = '0.26.2-pre.1',
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
@"
#define AppVersion "$Version"
#define SourceDir "$($dist.Replace('"','""'))"
#define OutputDir "$($release.Replace('"','""'))"
#define SetupIcon "$($icon.Replace('"','""'))"
#define AppName "Worldbench"

[Setup]
AppId={{8A9D5A83-5C7B-4A7E-9A69-8A9BD7E7F6D4}}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Worldbench Project
AppPublisherURL=https://github.com/DanielFQZ/Worldbench
DefaultDirName={localappdata}\Programs\Worldbench
DefaultGroupName=Worldbench
DisableProgramGroupPage=no
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=Worldbench-{#AppVersion}-setup
SetupIconFile={#SetupIcon}
UninstallDisplayIcon={app}\Worldbench.exe
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
ChangesAssociations=yes
Uninstallable=yes
VersionInfoCompany=Worldbench Project
VersionInfoDescription=Worldbench Minecraft world editor
VersionInfoCopyright=GNU General Public License v3.0

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Worldbench"; Filename: "{app}\Worldbench.exe"; WorkingDir: "{app}"
Name: "{group}\卸载 Worldbench"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Worldbench"; Filename: "{app}\Worldbench.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Classes\.wbench"; ValueType: string; ValueName: ""; ValueData: "Worldbench.Project"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\Worldbench.Project"; ValueType: string; ValueName: ""; ValueData: "Worldbench 工程"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Worldbench.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\Worldbench.exe,0"
Root: HKCU; Subkey: "Software\Classes\Worldbench.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Worldbench.exe"" ""%1"""

[Run]
Filename: "{app}\Worldbench.exe"; Description: "启动 Worldbench"; WorkingDir: "{app}"; Flags: postinstall nowait skipifsilent unchecked

[Code]
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  DataDir: string;
begin
  if CurUninstallStep <> usUninstall then
    exit;
  DataDir := ExpandConstant('{userdocs}\Worldbench');
  if DirExists(DataDir) then
    if MsgBox('Delete Worldbench user data?' + #13#10 + DataDir + #13#10 + #13#10 +
              'Choose No to keep settings, logs and resource paths. Project files are never deleted automatically.',
              mbConfirmation, MB_YESNO) = IDYES then
      DelTree(DataDir, True, True, True);
end;
"@ | Set-Content -LiteralPath $iss -Encoding UTF8

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
