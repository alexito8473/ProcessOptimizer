[Setup]
AppName=ProcessOptimizer
AppVersion=1.2.0
DefaultDirName={autopf}\ProcessOptimizer
OutputDir=Output
OutputBaseFilename=ProcessOptimizer-Setup
ArchitecturesInstallIn64BitMode=x64
Compression=lzma
SolidCompression=yes
CloseApplications=no
PrivilegesRequired=admin
; AppMutex desactivado: StopServiceForUpgrade mata los procesos antes de copiar.
SetupIconFile=..\ProcessOptimizer\Assets\logo.ico

[Files]
Source: "..\x64\Release\ProcessOptimizer.Service.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\x64\Release\ProcessOptimizer.UI.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\x64\Release\ProcessOptimizer.Tray.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\ProcessOptimizer\Assets\logo.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\ProcessOptimizer\Assets\logo.png"; DestDir: "{app}"; Flags: ignoreversion
Source: "VC_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Icons]
Name: "{autoprograms}\ProcessOptimizer"; Filename: "{app}\ProcessOptimizer.UI.exe"; IconFilename: "{app}\logo.ico"
Name: "{autoprograms}\ProcessOptimizer Tray"; Filename: "{app}\ProcessOptimizer.Tray.exe"; IconFilename: "{app}\logo.ico"
Name: "{autostartup}\ProcessOptimizer Tray"; Filename: "{app}\ProcessOptimizer.Tray.exe"; IconFilename: "{app}\logo.ico"

[Run]
Filename: "{tmp}\VC_redist.x64.exe"; Parameters: "/install /quiet /norestart"; Flags: waituntilterminated
Filename: "{app}\ProcessOptimizer.Service.exe"; Parameters: "--install --start"; Description: "Instalar y arrancar el servicio de ProcessOptimizer"; Flags: waituntilterminated runhidden
Filename: "{app}\ProcessOptimizer.UI.exe"; Description: "Iniciar ProcessOptimizer"; Flags: postinstall nowait skipifsilent
Filename: "{app}\ProcessOptimizer.Tray.exe"; Flags: postinstall nowait skipifsilent

[UninstallRun]
Filename: "{app}\ProcessOptimizer.Service.exe"; Parameters: "--stop --uninstall"; RunOnceId: ProcessOptimizerServiceUninstall; Flags: waituntilterminated runhidden skipifdoesntexist

[Code]
const
  ServiceName = 'ProcessOptimizerService';

function ServiceHasState(const State: string): Boolean;
var
  ResultCode: Integer;
  ScPath: string;
begin
  ScPath := ExpandConstant('{sys}\sc.exe');
  Result :=
    Exec(
      ExpandConstant('{sys}\cmd.exe'),
      '/c "' + ScPath + '" query "' + ServiceName +
        '" | findstr /I /C:"' + State + '"',
      '', SW_HIDE, ewWaitUntilTerminated, ResultCode) and
    (ResultCode = 0);
end;

function ServiceIsBusy(): Boolean;
begin
  Result :=
    ServiceHasState('RUNNING') or
    ServiceHasState('START_PENDING') or
    ServiceHasState('STOP_PENDING') or
    ServiceHasState('PAUSED') or
    ServiceHasState('PAUSE_PENDING') or
    ServiceHasState('CONTINUE_PENDING');
end;

// El servicio viejo mantiene bloqueado su EXE. Hay que detenerlo ANTES
// de copiar los nuevos binarios; [Run] ya llega tarde para eso.
procedure StopServiceForUpgrade();
var
  ResultCode: Integer;
  I: Integer;
begin
  // Primero pedir parada ordenada al SCM si el servicio esta activo.
  if ServiceHasState('RUNNING') or
     ServiceHasState('START_PENDING') then
  begin
    Exec(
      ExpandConstant('{sys}\sc.exe'),
      'stop "' + ServiceName + '"',
      '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  end;

  // El servicio puede haber quedado en STOPPED con el proceso aun vivo.
  // Matarlo por nombre libera el EXE antes de copiar los nuevos binarios.
  Exec(
    ExpandConstant('{sys}\taskkill.exe'),
    '/F /IM ProcessOptimizer.Service.exe',
    '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Exec(
    ExpandConstant('{sys}\taskkill.exe'),
    '/F /IM ProcessOptimizer.UI.exe',
    '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Exec(
    ExpandConstant('{sys}\taskkill.exe'),
    '/F /IM ProcessOptimizer.Tray.exe',
    '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Exec(
    ExpandConstant('{sys}\taskkill.exe'),
    '/F /IM ProcessOptimizer.exe',
    '', SW_HIDE, ewWaitUntilTerminated, ResultCode);

  // Esperar a que el SCM y los procesos terminen de soltar los archivos.
  for I := 0 to 39 do
  begin
    if not ServiceIsBusy() then
      Exit;
    Sleep(250);
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
    StopServiceForUpgrade();
end;
