#define MyAppName "DataScythe"
#define MyAppVersion "0.1.0"
#define MyAppExeName "DataScythe.exe"
[Setup]
AppId={{6C7E3564-9F68-4E49-A0A8-30D289E32001}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher=DataScythe
DefaultDirName={localappdata}\Programs\DataScythe
DefaultGroupName=DataScythe
OutputDir=..\release\windows
OutputBaseFilename=DataScythe-Setup-0.1.0
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
Uninstallable=yes

[Files]
Source: "..\releases\native\windows\DataScythe-0.1.0-win64\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "*.asc"

[Icons]
Name: "{group}\DataScythe"; Filename: "{app}\DataScythe.exe"
