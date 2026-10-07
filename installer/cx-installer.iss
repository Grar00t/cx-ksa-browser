#define MyAppName "CX Build"
#define MyAppVersion "0.9.0"
#define MyAppPublisher "CX Build"
#define MyAppExeName "cx.exe"
#ifndef BuildConfiguration
  #define BuildConfiguration "Release"
#endif

[Setup]
AppId={{E6B3C15D-0E94-4E4E-9A54-16A2C82DF909}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={localappdata}\Programs\CX Build
DefaultGroupName=CX Build
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=..\dist
OutputBaseFilename=CX-Build-Setup-{#MyAppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\{#MyAppExeName}
CloseApplications=yes
RestartApplications=no
ChangesEnvironment=yes
SetupLogging=yes
ArchitecturesAllowed=x64compatible

[Tasks]
Name: "addtopath"; Description: "Add CX Build to the current user's PATH"; GroupDescription: "Optional integrations:"; Flags: unchecked

[Files]
Source: "..\build\{#BuildConfiguration}\cx.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\NOTICE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\PRIVACY.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\SECURITY.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\docs\INSTALL.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "..\docs\PRIVACY_POLICY.md"; DestDir: "{app}\docs"; Flags: ignoreversion

[Icons]
Name: "{group}\CX Build"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"
Name: "{group}\Uninstall CX Build"; Filename: "{uninstallexe}"

[UninstallDelete]
Type: files; Name: "{app}\.cx-path-added"
Type: filesandordirs; Name: "{app}\cx.exe.WebView2"

[Code]
function NormalizePath(const Value: string): string;
begin
  Result := RemoveQuotes(Trim(Value));
  while (Length(Result) > 3) and
        (Result[Length(Result)] = '\') do
    Delete(Result, Length(Result), 1);
end;

function PathContains(const PathValue, Candidate: string): Boolean;
var
  Remaining, Part, Wanted: string;
  Separator: Integer;
begin
  Result := False;
  Remaining := PathValue;
  Wanted := NormalizePath(Candidate);

  while Remaining <> '' do
  begin
    Separator := Pos(';', Remaining);
    if Separator = 0 then
    begin
      Part := Remaining;
      Remaining := '';
    end
    else
    begin
      Part := Copy(Remaining, 1, Separator - 1);
      Delete(Remaining, 1, Separator);
    end;

    if CompareText(NormalizePath(Part), Wanted) = 0 then
    begin
      Result := True;
      Exit;
    end;
  end;
end;

function RemovePathEntry(const PathValue, Candidate: string): string;
var
  Remaining, Part, Wanted: string;
  Separator: Integer;
begin
  Result := '';
  Remaining := PathValue;
  Wanted := NormalizePath(Candidate);

  while Remaining <> '' do
  begin
    Separator := Pos(';', Remaining);
    if Separator = 0 then
    begin
      Part := Remaining;
      Remaining := '';
    end
    else
    begin
      Part := Copy(Remaining, 1, Separator - 1);
      Delete(Remaining, 1, Separator);
    end;

    Part := Trim(Part);
    if (Part <> '') and
       (CompareText(NormalizePath(Part), Wanted) <> 0) then
    begin
      if Result <> '' then
        Result := Result + ';';
      Result := Result + Part;
    end;
  end;
end;

procedure AddAppToPath;
var
  CurrentPath, NewPath, AppPath, Marker: string;
begin
  AppPath := ExpandConstant('{app}');
  Marker := ExpandConstant('{app}\.cx-path-added');

  if not RegQueryStringValue(
      HKEY_CURRENT_USER, 'Environment', 'Path', CurrentPath) then
    CurrentPath := '';

  if PathContains(CurrentPath, AppPath) then
    Exit;

  NewPath := CurrentPath;
  if (NewPath <> '') and (NewPath[Length(NewPath)] <> ';') then
    NewPath := NewPath + ';';
  NewPath := NewPath + AppPath;

  if RegWriteExpandStringValue(
      HKEY_CURRENT_USER, 'Environment', 'Path', NewPath) then
    SaveStringToFile(Marker, 'added-by-cx-installer', False);
end;

procedure RemoveAppFromPath;
var
  CurrentPath, NewPath, AppPath, Marker: string;
begin
  AppPath := ExpandConstant('{app}');
  Marker := ExpandConstant('{app}\.cx-path-added');

  if not FileExists(Marker) then
    Exit;

  if RegQueryStringValue(
      HKEY_CURRENT_USER, 'Environment', 'Path', CurrentPath) then
  begin
    NewPath := RemovePathEntry(CurrentPath, AppPath);
    if NewPath = '' then
      RegDeleteValue(HKEY_CURRENT_USER, 'Environment', 'Path')
    else
      RegWriteExpandStringValue(
        HKEY_CURRENT_USER, 'Environment', 'Path', NewPath);
  end;

  DeleteFile(Marker);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and
     WizardIsTaskSelected('addtopath') then
    AddAppToPath;
end;

procedure CurUninstallStepChanged(
  CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    RemoveAppFromPath;
end;
