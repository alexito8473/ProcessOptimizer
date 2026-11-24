[Setup]
AppName=ProcessOptimizer
AppVersion=1.1.0
DefaultDirName={autopf}\ProcessOptimizer
OutputDir=Output
OutputBaseFilename=ProcessOptimizer-Setup
ArchitecturesInstallIn64BitMode=x64
Compression=lzma
SolidCompression=yes
PrivilegesRequired=admin
AppMutex=ProcessOptimizerSetup

[Files]
Source: "..\ProcessOptimizer\x64\Release\ProcessOptimizer.Service.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\ProcessOptimizer\x64\Release\ProcessOptimizer.UI.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\ProcessOptimizer\x64\Release\ProcessOptimizer.Tray.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Icons]
Name: "{autoprograms}\ProcessOptimizer"; Filename: "{app}\ProcessOptimizer.UI.exe"
Name: "{autoprograms}\ProcessOptimizer Tray"; Filename: "{app}\ProcessOptimizer.Tray.exe"
Name: "{autostartup}\ProcessOptimizer Tray"; Filename: "{app}\ProcessOptimizer.Tray.exe"

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; Flags: waituntilterminated
Filename: "{app}\ProcessOptimizer.Service.exe"; Parameters: "--install --start"; Flags: waituntilterminated runhidden
Filename: "{app}\ProcessOptimizer.UI.exe"; Description: "Iniciar ProcessOptimizer"; Flags: postinstall nowait skipifsilent
Filename: "{app}\ProcessOptimizer.Tray.exe"; Flags: postinstall nowait skipifsilent

[UninstallRun]
Filename: "{app}\ProcessOptimizer.Service.exe"; Parameters: "--stop --uninstall"; Flags: waituntilterminated runhidden
