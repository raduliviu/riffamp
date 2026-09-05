; RiffAmp installer — per-user (no admin), Inno Setup 6.
; Build:  ISCC.exe riffamp.iss   (from this directory, after a Release build)

#define AppVersion "0.2.2"
#define BuildDir "..\helper\build\Release"

[Setup]
AppId={{7B7E2C5A-1F0A-4B9E-9C61-RIFFAMP00001}
AppName=RiffAmp
AppVersion={#AppVersion}
AppPublisher=RiffAmp project
DefaultDirName={localappdata}\Programs\RiffAmp
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=..\dist
; Stable (unversioned) name so the site links releases/latest/download/riffamp-setup.exe
; without a per-release edit; the version is carried by the release tag + AppVersion.
OutputBaseFilename=riffamp-setup
SetupIconFile=..\helper\resources\riffamp.ico
UninstallDisplayIcon={app}\riffamp-helper.exe
Compression=lzma2
SolidCompression=yes
CloseApplications=yes

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; Flags: unchecked
Name: "autostart"; Description: "Start the RiffAmp engine with &Windows"; Flags: unchecked

[Files]
Source: "{#BuildDir}\riffamp-helper.exe"; DestDir: "{app}"; Flags: ignoreversion
; Starter tone pack — only files RiffAmp owns the right to redistribute
; (helper/starter/: MIT Obsidian capture + CC0 IRs; see its LICENSES.md).
; The repo's top-level assets/ is local-dev-only and must NOT ship.
Source: "..\helper\starter\*"; DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs

[Icons]
Name: "{userprograms}\RiffAmp"; Filename: "{app}\riffamp-helper.exe"
Name: "{userdesktop}\RiffAmp"; Filename: "{app}\riffamp-helper.exe"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; \
  ValueName: "RiffAmp"; ValueData: """{app}\riffamp-helper.exe"""; Flags: uninsdeletevalue; Tasks: autostart

[Run]
Filename: "{app}\riffamp-helper.exe"; Description: "Start RiffAmp now"; Flags: postinstall nowait skipifsilent

[Code]
// Stop a running engine before installing or uninstalling.
procedure KillHelper();
var R: Integer;
begin
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/im riffamp-helper.exe /f', '',
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
