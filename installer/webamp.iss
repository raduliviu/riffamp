; webamp installer — per-user (no admin), Inno Setup 6.
; Build:  ISCC.exe webamp.iss   (from this directory, after a Release build)

#define AppVersion "0.2.0"
#define BuildDir "..\helper\build\Release"

[Setup]
AppId={{7B7E2C5A-1F0A-4B9E-9C61-WEBAMP000001}
AppName=webamp
AppVersion={#AppVersion}
AppPublisher=webamp project
DefaultDirName={localappdata}\Programs\webamp
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=..\dist
OutputBaseFilename=webamp-setup-{#AppVersion}
SetupIconFile=..\helper\resources\webamp.ico
UninstallDisplayIcon={app}\webamp-helper.exe
Compression=lzma2
SolidCompression=yes
CloseApplications=yes

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; Flags: unchecked
Name: "autostart"; Description: "Start the webamp engine with &Windows"; Flags: unchecked

[Files]
Source: "{#BuildDir}\webamp-helper.exe"; DestDir: "{app}"; Flags: ignoreversion
; Starter tone pack (user's local assets; swap for a curated pack before public distribution)
Source: "..\assets\*"; DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs

[Icons]
Name: "{userprograms}\webamp"; Filename: "{app}\webamp-helper.exe"
Name: "{userdesktop}\webamp"; Filename: "{app}\webamp-helper.exe"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; \
  ValueName: "webamp"; ValueData: """{app}\webamp-helper.exe"""; Flags: uninsdeletevalue; Tasks: autostart

[Run]
Filename: "{app}\webamp-helper.exe"; Description: "Start webamp now"; Flags: postinstall nowait skipifsilent

[Code]
// Stop a running engine before installing or uninstalling.
procedure KillHelper();
var R: Integer;
begin
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/im webamp-helper.exe /f', '',
       SW_HIDE, ewWaitUntilTerminated, R);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  KillHelper();
  Result := '';
end;

function InitializeUninstall(): Boolean;
begin
  KillHelper();
  Result := True;
end;
