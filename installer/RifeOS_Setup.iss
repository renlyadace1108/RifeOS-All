; =====================================================================
; RifeOS Windows Installer Script (Inno Setup 6)
; Developed by Renly
; =====================================================================

#define MyAppName "RifeOS"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "Renly"
#define MyAppContact "renly20061108@gmail.com"
#define MyAppExeName "RIFEOS.exe"

[Setup]
; App Metadata
AppId={{D37B4A1F-8C92-4A83-B715-9E5C1B627D40}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} v{#MyAppVersion} 专业版
AppPublisher={#MyAppPublisher}
AppContact={#MyAppContact}
AppSupportURL=https://github.com/renlyadace1108/RifeOS-All
AppUpdatesURL=https://github.com/renlyadace1108/RifeOS-All/releases
VersionInfoVersion={#MyAppVersion}.0
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription=RifeOS 桌面工作空间专业版安装向导
VersionInfoCopyright=Copyright (C) 2026 Renly. All rights reserved.

; Installation Path & Permissions
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesInstallIn64BitMode=x64compatible

; Visuals & Branding
SetupIconFile=..\assets\rifeos.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
WizardStyle=modern
WizardSizePercent=115,115
DisableProgramGroupPage=yes

; Output Configuration
OutputDir=..\dist
OutputBaseFilename=RifeOS_Setup_v{#MyAppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
CloseApplications=force

[Languages]
Name: "chinesesimplified"; MessagesFile: "ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "autostart"; Description: "随 Windows 开机自动启动 (Start with Windows)"; GroupDescription: "系统集成选项:"

[Files]
Source: "..\out\build\x64-release\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\assets\rifeos.ico"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
; Run with Windows on boot (Optional Task)
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "{#MyAppName}"; ValueData: """{app}\{#MyAppExeName}"""; Flags: uninsdeletevalue; Tasks: autostart

; Register in App Paths so user can type "rifeos" in Win+R
Root: HKA; Subkey: "Software\Microsoft\Windows\CurrentVersion\App Paths\{#MyAppExeName}"; ValueType: string; ValueData: "{app}\{#MyAppExeName}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Microsoft\Windows\CurrentVersion\App Paths\{#MyAppExeName}"; ValueType: string; ValueName: "Path"; ValueData: "{app}"; Flags: uninsdeletekey

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
