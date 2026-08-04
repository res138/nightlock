; Inno Setup script for the Nightlock installer.
;
; Compile from an installed tree (windeployqt output included):
;   iscc /DStageDir=<stage> /DAppVersion=<x.y.z> packaging\windows\nightlock.iss
;
; The stage root holds Nightlock.exe + Qt DLLs + icons\ + fonts\, and
; bin\nightlock.exe (the CLI). The optional "PATH" task exposes bin\
; so `nightlock` works in cmd/PowerShell after a fresh session starts.

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
ArchitecturesInstallIn64BitMode=x64compatible
ChangesEnvironment=yes
UninstallDisplayIcon={app}\Nightlock.exe

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; Flags: unchecked
Name: "addtopath"; Description: "Add the &nightlock command to PATH"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{autoprograms}\Nightlock"; Filename: "{app}\Nightlock.exe"
Name: "{autodesktop}\Nightlock"; Filename: "{app}\Nightlock.exe"; Tasks: desktopicon

[Registry]
; Appends {app}\bin to the user PATH when the task is picked; the
; uninstaller removes the entry again via [UninstallDelete]-time code.
Root: HKA; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; \
    ValueData: "{olddata};{app}\bin"; Tasks: addtopath; \
    Check: NeedsAddPath(ExpandConstant('{app}\bin'))

[Code]
function NeedsAddPath(Param: string): boolean;
var
  OrigPath: string;
begin
  if not RegQueryStringValue(HKA, 'Environment', 'Path', OrigPath) then
  begin
    Result := True;
    exit;
  end;
  Result := Pos(';' + Uppercase(Param) + ';',
                ';' + Uppercase(OrigPath) + ';') = 0;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Path, Entry: string;
  P: Integer;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    if RegQueryStringValue(HKA, 'Environment', 'Path', Path) then
    begin
      Entry := ';' + ExpandConstant('{app}\bin');
      P := Pos(Uppercase(Entry), Uppercase(Path));
      if P > 0 then
      begin
        Delete(Path, P, Length(Entry));
        RegWriteExpandStringValue(HKA, 'Environment', 'Path', Path);
      end;
    end;
  end;
end;
