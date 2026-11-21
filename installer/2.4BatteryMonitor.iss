; Inno Setup script for 2.4BatteryMonitor.
; Per-user install by default: no administrator rights needed, installs to %LOCALAPPDATA%\Programs.
; Setup also offers "Install for all users" (Program Files, needs administrator rights).
; Build with installer\build-installer.ps1 (or: ISCC /DAppVersion=1.2.3 installer\2.4BatteryMonitor.iss).

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#define Root ".."

[Setup]
; Keep the existing installation identity so updates keep the same directory and uninstall entry.
AppId={{7E0B5C2A-3F4D-4C8E-9B61-2D5A8F1C4E93}
AppName=2.4BatteryMonitor
AppVersion={#AppVersion}
AppVerName=2.4BatteryMonitor {#AppVersion}
AppPublisher=SunsetSH
DefaultDirName={autopf}\2.4BatteryMonitor
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#Root}\dist
OutputBaseFilename=2.4BatteryMonitor-Setup-{#AppVersion}
SetupIconFile={#Root}\res\app.ico
UninstallDisplayIcon={app}\2.4BatteryMonitor.exe
UninstallDisplayName=2.4BatteryMonitor
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
CloseApplicationsFilter=*.exe
RestartApplications=no

[Tasks]
Name: "autostart"; Description: "Start 2.4BatteryMonitor when I sign in to Windows"; Flags: unchecked

[Files]
Source: "{#Root}\2.4BatteryMonitor.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\2.4BatteryMonitor"; Filename: "{app}\2.4BatteryMonitor.exe"; IconFilename: "{app}\2.4BatteryMonitor.exe"; IconIndex: 0; AppUserModelID: "SunsetSH.BatteryMonitor24"

[Run]
Filename: "{app}\2.4BatteryMonitor.exe"; Parameters: "{code:StartParameters}"; Description: "Start 2.4BatteryMonitor now"; Flags: nowait postinstall skipifsilent runasoriginaluser

[Code]
const
  RunKey = 'Software\Microsoft\Windows\CurrentVersion\Run';
  RunValue = '2.4BatteryMonitor';

function IsUpdate(): Boolean;
begin
  Result := ExpandConstant('{param:WBUPDATE|0}') = '1';
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := IsUpdate() and (PageID = wpSelectTasks);
end;

function StartParameters(Param: String): String;
begin
  Result := '';
  if IsUpdate() and (ExpandConstant('{param:WLMOUSE|0}') = '1') then
    Result := '--wlmouse';
end;

function InstalledRunValue(): String;
begin
  Result := '"' + ExpandConstant('{app}\2.4BatteryMonitor.exe') + '"';
end;

function BackgroundRunValue(): String;
begin
  Result := InstalledRunValue() + ' --background';
end;

{ Removes the startup entry only if it points at this installed copy, so a portable copy's
  "Start with Windows" setting is left alone. }
procedure RemoveOwnAutostart();
var
  Value: String;
begin
  if RegQueryStringValue(HKCU, RunKey, RunValue, Value) and
     ((CompareText(Value, InstalledRunValue()) = 0) or
      (CompareText(Value, InstalledRunValue() + ' --wlmouse') = 0) or
      (CompareText(Value, BackgroundRunValue()) = 0) or
      (CompareText(Value, BackgroundRunValue() + ' --wlmouse') = 0)) then
    RegDeleteValue(HKCU, RunKey, RunValue);
end;

procedure NormalizeAutostart();
var
  Value: String;
begin
  if not RegQueryStringValue(HKCU, RunKey, RunValue, Value) then
    exit;
  if CompareText(Value, InstalledRunValue()) = 0 then
    RegWriteStringValue(HKCU, RunKey, RunValue, BackgroundRunValue())
  else if CompareText(Value, InstalledRunValue() + ' --wlmouse') = 0 then
    RegWriteStringValue(HKCU, RunKey, RunValue, BackgroundRunValue() + ' --wlmouse');
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Value: String;
begin
  if CurStep = ssPostInstall then
    NormalizeAutostart();
  if (CurStep = ssPostInstall) and not IsUpdate() then
  begin
    { Same value the app's own "Start with Windows" menu item writes, so the two stay in sync. }
    if WizardIsTaskSelected('autostart') then
    begin
      if not RegQueryStringValue(HKCU, RunKey, RunValue, Value) or
         (CompareText(Value, BackgroundRunValue() + ' --wlmouse') <> 0) then
        RegWriteStringValue(HKCU, RunKey, RunValue, BackgroundRunValue());
    end
    else
      RemoveOwnAutostart();
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    RemoveOwnAutostart();
end;
