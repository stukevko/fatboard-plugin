; ---------------------------------------------------------------------------
;  Fatboard – Windows installer (Inno Setup 6)
;  Built in CI: iscc /DVersion=0.1.0 /DStageDir=..\..\out installers\windows\fatboard.iss
; ---------------------------------------------------------------------------

#ifndef Version
  #define Version "0.1.0"
#endif
#ifndef StageDir
  #define StageDir "..\..\out"
#endif

[Setup]
AppId={{6C2E6F3A-FA7B-4F1E-9E36-1F0B0A2D0001}
AppName=Fatboard Plugins
AppVersion={#Version}
AppVerName=Fatboard Plugins {#Version}
AppPublisher=Fatboard
AppPublisherURL=https://fatboard.com
AppSupportURL=https://fatboard.com
DefaultDirName={autopf}\Fatboard
DefaultGroupName=Fatboard
DisableProgramGroupPage=yes
OutputDir=.\out
OutputBaseFilename=Fatboard-Setup-{#Version}
Compression=lzma2
SolidCompression=no
VersionInfoVersion={#Version}.0
VersionInfoCompany=Fatboard
VersionInfoDescription=Fatboard Plugins Setup
VersionInfoProductName=Fatboard Plugins
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
PrivilegesRequired=admin
UninstallDisplayName=Fatboard Plugins
; branding (optional files next to this script)
#ifexist "wizard-large.bmp"
WizardImageFile=wizard-large.bmp
#endif
#ifexist "wizard-small.bmp"
WizardSmallImageFile=wizard-small.bmp
#endif
#ifexist "fatboard.ico"
SetupIconFile=fatboard.ico
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "german";  MessagesFile: "compiler:Languages\German.isl"

[Types]
Name: "full";   Description: "Everything (recommended)"
Name: "custom"; Description: "Pick plugins"; Flags: iscustom

[Components]
Name: "scrub";    Description: "Scrub – one-knob cleanup (free)";     Types: full custom; Flags: fixed
Name: "scrubpro"; Description: "Scrub Pro – cleanup + vocal modes";    Types: full custom
Name: "harun";    Description: "Harun – harmony / doubler";            Types: full custom
Name: "spit";     Description: "Spit – beatbox to drum MIDI";               Types: full custom
Name: "smash";    Description: "Smash – drum bus";                           Types: full custom
Name: "standalone"; Description: "Standalone apps (test without a DAW)"; Types: full custom

[Files]
; VST3 bundles → Common Files\VST3 (the folder every DAW scans)
Source: "{#StageDir}\Scrub.vst3\*";     DestDir: "{commoncf64}\VST3\Scrub.vst3";     Flags: ignoreversion recursesubdirs createallsubdirs; Components: scrub
Source: "{#StageDir}\Scrub Pro.vst3\*"; DestDir: "{commoncf64}\VST3\Scrub Pro.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: scrubpro
Source: "{#StageDir}\Harun.vst3\*";     DestDir: "{commoncf64}\VST3\Harun.vst3";     Flags: ignoreversion recursesubdirs createallsubdirs; Components: harun
Source: "{#StageDir}\Spit.vst3\*";      DestDir: "{commoncf64}\VST3\Spit.vst3";      Flags: ignoreversion recursesubdirs createallsubdirs; Components: spit
Source: "{#StageDir}\Smash.vst3\*";     DestDir: "{commoncf64}\VST3\Smash.vst3";     Flags: ignoreversion recursesubdirs createallsubdirs; Components: smash
; Standalone
Source: "{#StageDir}\Scrub.exe";        DestDir: "{app}"; Flags: ignoreversion; Components: standalone and scrub
Source: "{#StageDir}\Scrub Pro.exe";    DestDir: "{app}"; Flags: ignoreversion; Components: standalone and scrubpro
Source: "{#StageDir}\Harun.exe";        DestDir: "{app}"; Flags: ignoreversion; Components: standalone and harun
Source: "{#StageDir}\Spit.exe";         DestDir: "{app}"; Flags: ignoreversion; Components: standalone and spit
Source: "{#StageDir}\Smash.exe";        DestDir: "{app}"; Flags: ignoreversion; Components: standalone and smash

[Icons]
Name: "{group}\Scrub";     Filename: "{app}\Scrub.exe";     Components: standalone and scrub
Name: "{group}\Scrub Pro"; Filename: "{app}\Scrub Pro.exe"; Components: standalone and scrubpro
Name: "{group}\Harun";     Filename: "{app}\Harun.exe";     Components: standalone and harun
Name: "{group}\Spit";      Filename: "{app}\Spit.exe";      Components: standalone and spit
Name: "{group}\Smash";     Filename: "{app}\Smash.exe";     Components: standalone and smash
Name: "{group}\fatboard.com"; Filename: "https://fatboard.com"

[Messages]
WelcomeLabel1=Thin in. Fat out.
WelcomeLabel2=This installs the Fatboard plugins (VST3) for every DAW on this computer.%n%nClose your DAW first, then click Next.
FinishedHeadingLabel=Fett. Brett.
FinishedLabel=Done. Rescan plugins in your DAW (FL Studio: Options → Manage Plugins → Find plugins).
