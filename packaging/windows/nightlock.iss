; Inno Setup script for the Nightlock installer.
;
; Compile from an installed tree (windeployqt output included):
;   iscc /DStageDir=<stage> /DAppVersion=<x.y.z> packaging\windows\nightlock.iss
;
; The stage root holds Nightlock.exe + Qt DLLs + qt.conf + plugins\,
; icons\ + fonts\, and bin\nightlock.exe (the CLI). The optional "PATH"
; task exposes bin\
; so `nightlock` works in cmd/PowerShell after a fresh session starts. The
; official VC++ runtime is embedded and installed silently; a clean machine
; does not need Qt, libsodium, Visual Studio, or a separate prerequisite.

#ifndef StageDir
  #error Pass /DStageDir=<installed tree>
#endif
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
AppId={{7A0F87E6-2B45-4A2C-9A44-nightlock01}}
AppName=Nightlock
AppVersion={#AppVersion}
AppPublisher=Nightlock
AppPublisherURL=https://github.com/rodukov/nightlock
DefaultDirName={autopf}\Nightlock
DefaultGroupName=Nightlock
DisableProgramGroupPage=yes
OutputBaseFilename=Nightlock-{#AppVersion}-Windows-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; Qt 6.10's Windows baseline is Windows 10 1809 (build 17763).
MinVersion=10.0.17763
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
ChangesEnvironment=yes
SetupLogging=yes
UninstallDisplayIcon={app}\Nightlock.exe

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; Flags: unchecked
Name: "addtopath"; Description: "Add the &nightlock command to PATH"

[Files]
; Extracted from Setup on demand in PrepareToInstall. Keeping the official
; redistributable inside Setup avoids both network access and unsupported
; copying of compiler DLLs from a developer machine. It deliberately comes
; before the solid-compressed application payload so extraction stays fast.
Source: "{#StageDir}\vc_redist.x64.exe"; Flags: dontcopy noencryption
Source: "{#StageDir}\*"; DestDir: "{app}"; Excludes: "vc_redist.x64.exe"; \
    Flags: recursesubdirs ignoreversion

[Icons]
Name: "{autoprograms}\Nightlock"; Filename: "{app}\Nightlock.exe"
Name: "{autodesktop}\Nightlock"; Filename: "{app}\Nightlock.exe"; Tasks: desktopicon

[Registry]
; Setup is an all-users install, so PATH must use the real machine environment
; key. HKLM\Environment (the old HKA expansion) is not read by Windows.
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; \
    ValueType: expandsz; ValueName: "Path"; \
    ValueData: "{olddata};{app}\bin"; Tasks: addtopath; \
    Check: NeedsAddPath(ExpandConstant('{app}\bin')); \
    AfterInstall: RememberAddedPath

[Code]
const
  EnvironmentKey = 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment';
  InstallerStateKey = 'Software\Nightlock\Installer';
  PathMarkerValue = 'AddedToMachinePath';
  { HRESULT_FROM_WIN32(ERROR_PRODUCT_VERSION): a newer compatible VC runtime }
  { is already present. Microsoft requires app installers to accept it. }
  VCRedistNewerVersionInstalled = -2147023258; { 0x80070666 }

function NeedsAddPath(Param: string): boolean;
var
  OrigPath: string;
begin
  if not RegQueryStringValue(HKLM, EnvironmentKey, 'Path', OrigPath) then
  begin
    Result := True;
    exit;
  end;
  Result := Pos(';' + Uppercase(Param) + ';',
                ';' + Uppercase(OrigPath) + ';') = 0;
end;

procedure RememberAddedPath;
begin
  { AfterInstall runs only when the PATH entry above was actually written. }
  RegWriteStringValue(HKLM, InstallerStateKey, PathMarkerValue,
                      ExpandConstant('{app}\bin'));
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
begin
  Result := '';
  ExtractTemporaryFile('vc_redist.x64.exe');

  if not Exec(ExpandConstant('{tmp}\vc_redist.x64.exe'),
              '/install /quiet /norestart', '', SW_HIDE,
              ewWaitUntilTerminated, ResultCode) then
  begin
    Result := 'Microsoft Visual C++ Runtime could not be started: ' +
              SysErrorMessage(ResultCode);
    exit;
  end;

  { 1638/0x80070666 mean an equal/newer runtime is installed. }
  { 3010 and 1641 are successful installs that require a restart. }
  if (ResultCode = 3010) or (ResultCode = 1641) then
    NeedsRestart := True
  else if (ResultCode <> 0) and
          (ResultCode <> 1638) and
          (ResultCode <> VCRedistNewerVersionInstalled) then
    Result := Format(
      'Microsoft Visual C++ Runtime installation failed (exit code %d).', [ResultCode]);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Path, Entry, BoundedPath, Needle: string;
  P: Integer;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    { Never remove a pre-existing or manually added PATH entry. The marker is
      written only after this Setup instance appends the entry. }
    if RegQueryStringValue(HKLM, InstallerStateKey, PathMarkerValue, Entry) then
    begin
      if RegQueryStringValue(HKLM, EnvironmentKey, 'Path', Path) then
      begin
        { Delimit both strings so C:\Nightlock\bin never matches a longer
          prefix such as C:\Nightlock\bin-tools. Remove the exact entry and
          its trailing delimiter while preserving the surrounding entries. }
        BoundedPath := ';' + Path + ';';
        Needle := ';' + Entry + ';';
        P := Pos(Uppercase(Needle), Uppercase(BoundedPath));
        if P > 0 then
        begin
          Delete(BoundedPath, P + 1, Length(Entry) + 1);
          if Length(BoundedPath) <= 1 then
            Path := ''
          else
            Path := Copy(BoundedPath, 2, Length(BoundedPath) - 2);
          RegWriteExpandStringValue(HKLM, EnvironmentKey, 'Path', Path);
        end;
      end;
      RegDeleteValue(HKLM, InstallerStateKey, PathMarkerValue);
      RegDeleteKeyIfEmpty(HKLM, InstallerStateKey);
      RegDeleteKeyIfEmpty(HKLM, 'Software\Nightlock');
    end;
  end;
end;
