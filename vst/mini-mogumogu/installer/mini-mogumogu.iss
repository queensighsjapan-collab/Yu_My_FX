; Inno Setup script for the Windows VST3 installer. Built by CI: ISCC /DAppVersion=1.0.0 installer\mini-mogumogu.iss
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

[Setup]
AppId={{B3D7A9E4-61C2-4E58-8A0F-2F9D4C7E1B35}
AppName=mini mogumogu
AppVersion={#AppVersion}
AppPublisher=Yu_My
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
Uninstallable=yes
UninstallFilesDir={commonappdata}\mini-mogumogu
OutputDir=..\dist
OutputBaseFilename=mini-mogumogu-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
LicenseFile=..\LICENSE.txt
WizardStyle=modern

[Languages]
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "..\dist\mini mogumogu.vst3\*"; DestDir: "{commoncf64}\VST3\mini mogumogu.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "..\LICENSE.txt"; DestDir: "{commoncf64}\VST3\mini mogumogu.vst3"; Flags: ignoreversion
Source: "..\Fonts\OFL-*.txt"; DestDir: "{commoncf64}\VST3\mini mogumogu.vst3"; Flags: ignoreversion
