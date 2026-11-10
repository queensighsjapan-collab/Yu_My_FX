; Inno Setup script for the Windows VST3 installer. Built by CI: ISCC /DAppVersion=0.1.0 installer\yumy-bacteria.iss
#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif

[Setup]
AppId={{B3D5A8E1-7C4F-4D0B-9A63-2E1F5C8B7D41}
AppName=YuMy Bacteria
AppVersion={#AppVersion}
AppPublisher=Yu_My
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
Uninstallable=yes
UninstallFilesDir={commonappdata}\YuMy Bacteria
OutputDir=..\dist
OutputBaseFilename=YuMyBacteria-Setup-{#AppVersion}
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
Source: "..\dist\YuMy Bacteria.vst3\*"; DestDir: "{commoncf64}\VST3\YuMy Bacteria.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "..\LICENSE.txt"; DestDir: "{commoncf64}\VST3\YuMy Bacteria.vst3"; Flags: ignoreversion
