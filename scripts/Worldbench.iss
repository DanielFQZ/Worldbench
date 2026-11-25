#define AppVersion "@VERSION@"
#define SourceDir "@SOURCE@"
#define OutputDir "@OUTPUT@"
#define SetupIcon "@ICON@"
#define AppName "Worldbench"

[Setup]
AppId={{8A9D5A83-5C7B-4A7E-9A69-8A9BD7E7F6D4}}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Worldbench Project
AppPublisherURL=https://github.com/DanielFQZ/Worldbench
DefaultDirName={localappdata}\Programs\Worldbench
DefaultGroupName=Worldbench
DisableDirPage=no
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
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "快捷方式："; Flags: unchecked

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
    if not UninstallSilent then
      if MsgBox('是否删除 Worldbench 的配置和日志？' + #13#10 + DataDir + #13#10 + #13#10 +
                '选择“否”保留设置。工程、资源包和其他用户文件不会被删除。',
                mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then begin
        DeleteFile(DataDir + '\Worldbench\Worldbench.ini');
        DeleteFile(DataDir + '\Worldbench.log');
        DeleteFile(DataDir + '\Worldbench.log.previous');
        RemoveDir(DataDir + '\Worldbench');
        RemoveDir(DataDir);
        RegDeleteKeyIncludingSubkeys(HKCU, 'Software\Worldbench\Worldbench');
      end;
end;
