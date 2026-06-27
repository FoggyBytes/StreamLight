; StreamLight 4.4.0 — Moonlight fork with StreamTweak integration.
; SourceDir is the self-contained runtime built by build-arch.bat +
; manual windeployqt (see CLAUDE.md §3).
#define AppName "StreamLight"
#define AppVersion "4.4.0"
#define AppPublisher "FoggyBytes"
#define AppURL "https://github.com/FoggyBytes/StreamLight"
#define AppExeName "StreamLight.exe"
#define SourceDir "build\deploy-x64-release"

[Setup]
AppId={{B7A2C3D4-E5F6-7890-ABCD-EF1234567890}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
InfoBeforeFile=changelog.txt
SetupIconFile=installer\resources\streamlight.ico
WizardSmallImageFile=installer\resources\streamlight.bmp
WizardImageFile=installer\resources\streamlightinstaller.bmp
UninstallDisplayIcon={app}\{#AppExeName}
AllowNoIcons=yes
DirExistsWarning=no
CloseApplications=yes
Compression=lzma2
SolidCompression=yes
OutputDir=build\installer
OutputBaseFilename=StreamLight_{#AppVersion}_Installer
WizardStyle=modern
DisableWelcomePage=no
MinVersion=10.0
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
WelcomeLabel1=Welcome to the StreamLight Setup Wizard
WelcomeLabel2=

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "xboxtile"; Description: "Add an icon to the Xbox app's 'My apps' section"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
; portable.dat excluded: it would force Qt to write settings/cache to {app}
; (= Program Files), where standard users can't write. Default Qt storage
; (HKCU + %LOCALAPPDATA%) is user-writable and used instead.
Source: "{#SourceDir}\*"; DestDir: "{app}"; \
    Excludes: "*.log,sl_*.txt,streamlight_pad.log,portable.dat"; \
    Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\gamecontrollerdb.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "installer\resources\streamlight.bmp"; Flags: dontcopy
Source: "changelog.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\{#AppPublisher}\{#AppName}"; Flags: uninsdeletekey

[Run]
; If the user opted in to "Add an icon to the Xbox app's My apps", seed the
; CustomLibraryManagement manifest with the StreamLight entry + branded tile
; PNG. Runs hidden, blocking, finishes in ~50 ms.
; runasoriginaluser is critical: PrivilegesRequired defaults to "admin" so
; the installer is elevated, and a vanilla [Run] would inherit the elevated
; token. The child's %LOCALAPPDATA% would then resolve to the elevating
; account's profile, NOT the interactive user's — registerEntry() would
; write the manifest in the wrong place where Xbox app never reads.
Filename: "{app}\{#AppExeName}"; Parameters: "--register-xbox-tile"; \
    Tasks: xboxtile; \
    Flags: runhidden waituntilterminated runasoriginaluser
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; \
    Flags: nowait postinstall skipifsilent

[Code]
var
  LogoImage: TBitmapImage;
  DevelopedByLabel: TNewStaticText;
  GitHubLinkLabel: TNewStaticText;
  StreamTweakPage: TWizardPage;
  StreamTweakIntroLabel: TNewStaticText;
  StreamTweakBulletsLabel: TNewStaticText;
  StreamTweakOutroLabel: TNewStaticText;
  StreamTweakLearnMoreLabel: TNewStaticText;
  StreamTweakLinkLabel: TNewStaticText;

procedure GitHubLinkClick(Sender: TObject);
var
  ErrorCode: Integer;
begin
  ShellExec('open', '{#AppURL}', '', '', SW_SHOWNORMAL, ewNoWait, ErrorCode);
end;

procedure StreamTweakLinkClick(Sender: TObject);
var
  ErrorCode: Integer;
begin
  ShellExec('open', 'https://github.com/FoggyBytes/StreamTweak', '', '', SW_SHOWNORMAL, ewNoWait, ErrorCode);
end;

procedure InitializeWizard;
var
  TmpFileName: String;
begin
  ExtractTemporaryFile('streamlight.bmp');
  TmpFileName := ExpandConstant('{tmp}\streamlight.bmp');

  LogoImage := TBitmapImage.Create(WizardForm);
  LogoImage.Parent := WizardForm.WelcomePage;
  LogoImage.Bitmap.LoadFromFile(TmpFileName);
  LogoImage.Left := WizardForm.WelcomeLabel1.Left;
  LogoImage.Top := WizardForm.WelcomeLabel1.Top + WizardForm.WelcomeLabel1.Height + ScaleY(25);
  LogoImage.AutoSize := True;

  DevelopedByLabel := TNewStaticText.Create(WizardForm);
  DevelopedByLabel.Parent := WizardForm.WelcomePage;
  DevelopedByLabel.Left := LogoImage.Left;
  DevelopedByLabel.Top := LogoImage.Top + LogoImage.Height + ScaleY(30);
  DevelopedByLabel.Caption := 'Developed by FoggyBytes © 2026';
  DevelopedByLabel.Font.Size := 10;
  DevelopedByLabel.AutoSize := True;

  GitHubLinkLabel := TNewStaticText.Create(WizardForm);
  GitHubLinkLabel.Parent := WizardForm.WelcomePage;
  GitHubLinkLabel.Left := DevelopedByLabel.Left;
  GitHubLinkLabel.Top := DevelopedByLabel.Top + DevelopedByLabel.Height + ScaleY(15);
  GitHubLinkLabel.Caption := '{#AppURL}';
  GitHubLinkLabel.Cursor := crHand;
  GitHubLinkLabel.Font.Color := clHighlight;
  GitHubLinkLabel.Font.Style := [fsUnderline];
  GitHubLinkLabel.OnClick := @GitHubLinkClick;

  // Dedicated wizard page for StreamTweak — full inner-page width gives the
  // bullet list room to breathe (the Welcome page's right panel is too narrow).
  StreamTweakPage := CreateCustomPage(wpWelcome,
    'StreamTweak — recommended companion app', #13#10 +
    'Install StreamTweak on the host PC to unlock StreamLight''s advanced features.');

  StreamTweakIntroLabel := TNewStaticText.Create(StreamTweakPage);
  StreamTweakIntroLabel.Parent := StreamTweakPage.Surface;
  StreamTweakIntroLabel.Left := 0;
  StreamTweakIntroLabel.Top := 0;
  StreamTweakIntroLabel.Width := StreamTweakPage.SurfaceWidth;
  StreamTweakIntroLabel.WordWrap := True;
  StreamTweakIntroLabel.AutoSize := True;
  StreamTweakIntroLabel.Caption :=
    'StreamLight works as a standalone Moonlight client. When paired with StreamTweak — ' +
    'a free open-source companion app for the host PC, also developed by FoggyBytes — ' +
    'it gains the following advanced features:';

  StreamTweakBulletsLabel := TNewStaticText.Create(StreamTweakPage);
  StreamTweakBulletsLabel.Parent := StreamTweakPage.Surface;
  StreamTweakBulletsLabel.Left := ScaleX(16);
  StreamTweakBulletsLabel.Top := StreamTweakIntroLabel.Top + StreamTweakIntroLabel.Height + ScaleY(14);
  StreamTweakBulletsLabel.AutoSize := True;
  StreamTweakBulletsLabel.Caption :=
    '•  Adaptive NIC throttling — eliminates UDP bufferbloat and latency spikes' + #13#10 +
    '•  Live host metrics overlay (GPU, encoder, VRAM, temperature, CPU, network)' + #13#10 +
    '•  Auto HDR toggle + spatial audio (Dolby Atmos / Windows Sonic)' + #13#10 +
    '•  Game library sync with store badges (Steam, Epic, GOG, Xbox, …)' + #13#10 +
    '•  Session quality grading and per-stream telemetry' + #13#10 +
    '•  Remote host power-off and Windows Update' + #13#10 +
    '•  Tailscale presence for remote streaming over the internet';

  StreamTweakOutroLabel := TNewStaticText.Create(StreamTweakPage);
  StreamTweakOutroLabel.Parent := StreamTweakPage.Surface;
  StreamTweakOutroLabel.Left := 0;
  StreamTweakOutroLabel.Top := StreamTweakBulletsLabel.Top + StreamTweakBulletsLabel.Height + ScaleY(18);
  StreamTweakOutroLabel.Width := StreamTweakPage.SurfaceWidth;
  StreamTweakOutroLabel.WordWrap := True;
  StreamTweakOutroLabel.AutoSize := True;
  StreamTweakOutroLabel.Caption :=
    'StreamTweak is optional — you can install it on the host PC at any time, ' +
    'no need to interrupt this setup. Click Next to continue installing StreamLight.';

  StreamTweakLearnMoreLabel := TNewStaticText.Create(StreamTweakPage);
  StreamTweakLearnMoreLabel.Parent := StreamTweakPage.Surface;
  StreamTweakLearnMoreLabel.Left := 0;
  StreamTweakLearnMoreLabel.Top := StreamTweakOutroLabel.Top + StreamTweakOutroLabel.Height + ScaleY(16);
  StreamTweakLearnMoreLabel.Caption := 'Learn more:';
  StreamTweakLearnMoreLabel.AutoSize := True;

  StreamTweakLinkLabel := TNewStaticText.Create(StreamTweakPage);
  StreamTweakLinkLabel.Parent := StreamTweakPage.Surface;
  StreamTweakLinkLabel.Left := StreamTweakLearnMoreLabel.Left + StreamTweakLearnMoreLabel.Width + ScaleX(4);
  StreamTweakLinkLabel.Top := StreamTweakLearnMoreLabel.Top;
  StreamTweakLinkLabel.Caption := 'https://github.com/FoggyBytes/StreamTweak';
  StreamTweakLinkLabel.Cursor := crHand;
  StreamTweakLinkLabel.Font.Color := clHighlight;
  StreamTweakLinkLabel.Font.Style := [fsUnderline];
  StreamTweakLinkLabel.OnClick := @StreamTweakLinkClick;
  StreamTweakLinkLabel.AutoSize := True;
end;
