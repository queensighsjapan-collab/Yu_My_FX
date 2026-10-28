; Inno Setup script for the Windows VST3 installer. Built by CI: ISCC /DAppVersion=1.0.0 installer\ym2000.iss
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

[Setup]
AppId={{6E1B7C52-4A52-4F0E-9B5B-5C2B0A7A1D20}
AppName=YM2000
AppVersion={#AppVersion}
AppPublisher=Yu_My
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
Uninstallable=yes
UninstallFilesDir={commonappdata}\YM2000
OutputDir=..\dist
OutputBaseFilename=YM2000-Setup-{#AppVersion}
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
Source: "..\dist\YM2000.vst3\*"; DestDir: "{commoncf64}\VST3\YM2000.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "..\LICENSE.txt"; DestDir: "{commoncf64}\VST3\YM2000.vst3"; Flags: ignoreversion
