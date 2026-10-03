; SPDX-License-Identifier: GPL-3.0-or-later
; The Windows web installer (spec 9). The installer itself carries no program
; files: it reads the list of versions from GitHub Releases, downloads the
; chosen version's package, checks its SHA-256 and unpacks it.
;
; Build: tools/package-windows.ps1 (or ISCC /DAppVersion=x.y.z leinwand.iss).

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef ReleasesUrl
  #define ReleasesUrl "https://api.github.com/repos/thesilver0913/leinwand/releases"
#endif

[Setup]
AppId={{A672F98B-EA8A-4967-A102-705BEAB5CC7F}
AppName=Leinwand
AppVersion={#AppVersion}
AppVerName=Leinwand
AppPublisher=The Leinwand authors
AppPublisherURL=https://github.com/thesilver0913/leinwand
AppSupportURL=https://github.com/thesilver0913/leinwand/issues
DefaultDirName={autopf}\Leinwand
DefaultGroupName=Leinwand
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputBaseFilename=LeinwandSetup
SetupIconFile=..\resources\leinwand.ico
UninstallDisplayIcon={app}\bin\leinwand.exe
UninstallDisplayName=Leinwand
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequiredOverridesAllowed=dialog
ChangesAssociations=yes
WizardStyle=modern
Compression=lzma2
SolidCompression=yes

[Languages]
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
japanese.VersionCaption=バージョンの選択
japanese.VersionDescription=インストールする Leinwand のバージョンを選びます。
japanese.VersionLatest=Leinwand %1(最新版)をインストールします。
japanese.VersionChosen=Leinwand %1 をインストールします。
japanese.OtherVersions=他のバージョン...
japanese.Fetching=バージョンの一覧を取得しています...
japanese.FetchFailed=GitHub からバージョンの一覧を取得できませんでした。インターネット接続を確かめて、もう一度実行してください。%n%n%1
japanese.NoPackage=インストールできるバージョンが見つかりませんでした。
japanese.Unpacking=ファイルを展開しています...
japanese.UnpackFailed=ダウンロードしたパッケージを展開できませんでした。
japanese.SmartScreen=このインストーラーにはまだコード署名がありません。
japanese.DocumentType=Leinwand ドキュメント
english.VersionCaption=Choose a Version
english.VersionDescription=Choose the version of Leinwand to install.
english.VersionLatest=Leinwand %1 (the latest version) will be installed.
english.VersionChosen=Leinwand %1 will be installed.
english.OtherVersions=Other Versions...
english.Fetching=Getting the list of versions...
english.FetchFailed=The list of versions could not be read from GitHub. Check the internet connection and run the installer again.%n%n%1
english.NoPackage=No version to install was found.
english.Unpacking=Unpacking the files...
english.UnpackFailed=The downloaded package could not be unpacked.
english.SmartScreen=This installer is not code-signed yet.
english.DocumentType=Leinwand Document

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Icons]
Name: "{autoprograms}\Leinwand"; Filename: "{app}\bin\leinwand.exe"
Name: "{autodesktop}\Leinwand"; Filename: "{app}\bin\leinwand.exe"; Tasks: desktopicon

[Registry]
; .lwd opens with Leinwand (spec 9). The program's second icon is the
; document icon.
Root: HKA; Subkey: "Software\Classes\.lwd"; ValueType: string; ValueName: ""; ValueData: "Leinwand.Document"; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKA; Subkey: "Software\Classes\.lwd\OpenWithProgids"; ValueType: string; ValueName: "Leinwand.Document"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKA; Subkey: "Software\Classes\Leinwand.Document"; ValueType: string; ValueName: ""; ValueData: "{cm:DocumentType}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Leinwand.Document\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\bin\leinwand.exe"",1"
Root: HKA; Subkey: "Software\Classes\Leinwand.Document\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\bin\leinwand.exe"" ""%1"""
; .svg, .ai and .pdf only get Leinwand in "Open with"; their default program
; stays as it is.
Root: HKA; Subkey: "Software\Classes\Leinwand.Import"; ValueType: string; ValueName: ""; ValueData: "Leinwand"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Leinwand.Import\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\bin\leinwand.exe"" ""%1"""
; The extension keys themselves go on uninstall only when nothing else uses them.
Root: HKA; Subkey: "Software\Classes\.svg"; Flags: uninsdeletekeyifempty
Root: HKA; Subkey: "Software\Classes\.ai"; Flags: uninsdeletekeyifempty
Root: HKA; Subkey: "Software\Classes\.pdf"; Flags: uninsdeletekeyifempty
Root: HKA; Subkey: "Software\Classes\.svg\OpenWithProgids"; ValueType: string; ValueName: "Leinwand.Import"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKA; Subkey: "Software\Classes\.ai\OpenWithProgids"; ValueType: string; ValueName: "Leinwand.Import"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKA; Subkey: "Software\Classes\.pdf\OpenWithProgids"; ValueType: string; ValueName: "Leinwand.Import"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKA; Subkey: "Software\Classes\Applications\leinwand.exe"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Leinwand"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Applications\leinwand.exe\SupportedTypes"; ValueType: string; ValueName: ".lwd"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\leinwand.exe\SupportedTypes"; ValueType: string; ValueName: ".svg"; ValueData: ""

[UninstallDelete]
; The program files came from the downloaded package, not from [Files].
Type: filesandordirs; Name: "{app}\bin"
Type: filesandordirs; Name: "{app}\plugins"
Type: filesandordirs; Name: "{app}\qml"
Type: filesandordirs; Name: "{app}\translations"
Type: filesandordirs; Name: "{app}\licenses"
Type: files; Name: "{app}\LICENSE"
Type: files; Name: "{app}\NOTICE.md"
Type: dirifempty; Name: "{app}"

[Run]
Filename: "{app}\bin\leinwand.exe"; Description: "{cm:LaunchProgram,Leinwand}"; Flags: nowait postinstall skipifsilent

[Code]
const
  PackageSuffix = '-windows-x64.zip';

var
  VersionPage: TWizardPage;
  VersionText: TNewStaticText;
  OtherButton: TNewButton;
  VersionList: TNewListBox;
  DownloadPage: TDownloadWizardPage;
  Tags, ZipUrls, HashUrls: TStringList;
  Fetched: Boolean;

// The string value after `Key` in `Json`, starting the search at the front;
// '' if there is none. Removes everything up to the value from `Json`.
function TakeValue(var Json: String; const Key: String): String;
var
  P: Integer;
begin
  Result := '';
  P := Pos('"' + Key + '"', Json);
  if P = 0 then
    exit;
  Delete(Json, 1, P + Length(Key) + 1);
  P := Pos('"', Json);
  if P = 0 then
    exit;
  Delete(Json, 1, P);
  P := Pos('"', Json);
  Result := Copy(Json, 1, P - 1);
  Delete(Json, 1, P);
end;

// Releases newest first, as GitHub lists them: each "tag_name" is followed by
// its assets' "browser_download_url"s before the next release starts.
procedure ParseReleases(Json: String);
var
  Release, Tag, Url, ZipUrl, HashUrl: String;
  Next: Integer;
begin
  while Pos('"tag_name"', Json) > 0 do
  begin
    Tag := TakeValue(Json, 'tag_name');
    Next := Pos('"tag_name"', Json);
    if Next = 0 then
      Release := Json
    else
      Release := Copy(Json, 1, Next - 1);
    ZipUrl := '';
    HashUrl := '';
    while Pos('"browser_download_url"', Release) > 0 do
    begin
      Url := TakeValue(Release, 'browser_download_url');
      if Copy(Url, Length(Url) - Length(PackageSuffix) + 1, Length(PackageSuffix)) = PackageSuffix then
        ZipUrl := Url
      else if Copy(Url, Length(Url) - Length(PackageSuffix + '.sha256') + 1, Length(PackageSuffix + '.sha256')) = PackageSuffix + '.sha256' then
        HashUrl := Url;
    end;
    if (ZipUrl <> '') and (HashUrl <> '') then
    begin
      Tags.Add(Tag);
      ZipUrls.Add(ZipUrl);
      HashUrls.Add(HashUrl);
    end;
  end;
end;

procedure ShowChoice;
begin
  if VersionList.ItemIndex <= 0 then
    VersionText.Caption := FmtMessage(CustomMessage('VersionLatest'), [Tags[0]])
  else
    VersionText.Caption := FmtMessage(CustomMessage('VersionChosen'), [Tags[VersionList.ItemIndex]]);
end;

procedure OtherClick(Sender: TObject);
begin
  VersionList.Visible := True;
  OtherButton.Visible := False;
end;

procedure ListClick(Sender: TObject);
begin
  ShowChoice;
end;

function OnDownloadProgress(const Url, FileName: String; const Progress, ProgressMax: Int64): Boolean;
begin
  Result := True;
end;

procedure FetchVersions;
var
  Json: AnsiString;
  I: Integer;
begin
  Fetched := False;
  VersionText.Caption := CustomMessage('Fetching');
  WizardForm.NextButton.Enabled := False;
  try
    DownloadTemporaryFile('{#ReleasesUrl}', 'releases.json', '', @OnDownloadProgress);
    LoadStringFromFile(ExpandConstant('{tmp}\releases.json'), Json);
    ParseReleases(String(Json));
  except
    VersionText.Caption := FmtMessage(CustomMessage('FetchFailed'), [GetExceptionMessage]);
    exit;
  end;
  if Tags.Count = 0 then
  begin
    VersionText.Caption := CustomMessage('NoPackage');
    exit;
  end;
  VersionList.Items.Clear;
  for I := 0 to Tags.Count - 1 do
    VersionList.Items.Add(Tags[I]);
  VersionList.ItemIndex := 0;
  ShowChoice;
  Fetched := True;
  WizardForm.NextButton.Enabled := True;
end;

procedure InitializeWizard;
begin
  Tags := TStringList.Create;
  ZipUrls := TStringList.Create;
  HashUrls := TStringList.Create;

  VersionPage := CreateCustomPage(wpSelectDir, CustomMessage('VersionCaption'),
    CustomMessage('VersionDescription'));
  VersionText := TNewStaticText.Create(VersionPage);
  VersionText.Parent := VersionPage.Surface;
  VersionText.AutoSize := False;
  VersionText.WordWrap := True;
  VersionText.Width := VersionPage.SurfaceWidth;
  VersionText.Height := ScaleY(48);
  OtherButton := TNewButton.Create(VersionPage);
  OtherButton.Parent := VersionPage.Surface;
  OtherButton.Top := VersionText.Top + VersionText.Height + ScaleY(8);
  OtherButton.Width := ScaleX(140);
  OtherButton.Height := ScaleY(24);
  OtherButton.Caption := CustomMessage('OtherVersions');
  OtherButton.OnClick := @OtherClick;
  VersionList := TNewListBox.Create(VersionPage);
  VersionList.Parent := VersionPage.Surface;
  VersionList.Top := OtherButton.Top;
  VersionList.Width := ScaleX(220);
  VersionList.Height := VersionPage.SurfaceHeight - VersionList.Top;
  VersionList.Visible := False;
  VersionList.OnClick := @ListClick;

  DownloadPage := CreateDownloadPage(SetupMessage(msgWizardPreparing),
    SetupMessage(msgPreparingDesc), @OnDownloadProgress);
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if (CurPageID = VersionPage.ID) and not Fetched then
    FetchVersions;
  if CurPageID = VersionPage.ID then
    WizardForm.NextButton.Enabled := Fetched;
end;

// The package is downloaded when the user confirms on the Ready page; its
// SHA-256 comes from the .sha256 file next to it in the release.
function NextButtonClick(CurPageID: Integer): Boolean;
var
  Index: Integer;
  HashText: AnsiString;
begin
  Result := True;
  if CurPageID <> wpReady then
    exit;
  // Silent installs skip the version page: the latest version.
  if not Fetched then
    FetchVersions;
  if not Fetched then
  begin
    SuppressibleMsgBox(VersionText.Caption, mbCriticalError, MB_OK, IDOK);
    Result := False;
    exit;
  end;
  Index := VersionList.ItemIndex;
  if Index < 0 then
    Index := 0;
  DownloadPage.Clear;
  DownloadPage.Show;
  try
    try
      DownloadTemporaryFile(HashUrls[Index], 'package.sha256', '', @OnDownloadProgress);
      LoadStringFromFile(ExpandConstant('{tmp}\package.sha256'), HashText);
      DownloadPage.Add(ZipUrls[Index], 'package.zip', Copy(Trim(String(HashText)), 1, 64));
      DownloadPage.Download;
    except
      SuppressibleMsgBox(AddPeriod(GetExceptionMessage), mbCriticalError, MB_OK, IDOK);
      Result := False;
    end;
  finally
    DownloadPage.Hide;
  end;
end;

// Unpacks the package into the program folder, replacing an earlier version
// (spec 9: one version at a time).
procedure CurStepChanged(CurStep: TSetupStep);
var
  App, Zip, Params: String;
  Code: Integer;
begin
  if CurStep <> ssInstall then
    exit;
  App := ExpandConstant('{app}');
  Zip := ExpandConstant('{tmp}\package.zip');
  WizardForm.StatusLabel.Caption := CustomMessage('Unpacking');
  ForceDirectories(App);
  DelTree(App + '\bin', True, True, True);
  DelTree(App + '\plugins', True, True, True);
  DelTree(App + '\qml', True, True, True);
  DelTree(App + '\translations', True, True, True);
  Params := '-NoProfile -ExecutionPolicy Bypass -Command "Expand-Archive -LiteralPath ''' + Zip +
    ''' -DestinationPath ''' + App + ''' -Force"';
  if not Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'), Params, '', SW_HIDE,
              ewWaitUntilTerminated, Code) or (Code <> 0) or not FileExists(App + '\bin\leinwand.exe') then
    RaiseException(CustomMessage('UnpackFailed'));
end;
