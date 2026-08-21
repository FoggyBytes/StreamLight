; StreamLight 5.1.3 — Moonlight fork with StreamTweak integration.
; SourceDir is the self-contained runtime built by build-arch.bat +
; manual windeployqt (see CLAUDE.md §3).
#define AppName "StreamLight"
#define AppVersion "5.1.3"
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
WizardSmallImageFile=installer\resources\streamlight.png
WizardImageFile=installer\resources\streamlightinstaller.png
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
; 64-bit Setup binary (Inno Setup 7+). StreamLight.exe is x64, so a 32-bit installer
; bought nothing; this also gets high-entropy ASLR by default. Note this drops
; Windows 10 on ARM64, which only emulates x86 — but the x64 app could never have
; run there anyway. Windows 11 on ARM64 emulates x64 and is unaffected.
SetupArchitecture=x64
; x64compatible (unlike StreamTweak's x64os): this is the CLIENT, and an ARM64 device
; running it under x64 emulation is a plausible scenario.
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
; cache\* excluded: the app writes an auto-updated gamecontrollerdb.txt there at
; runtime, so a dev machine that ran the deployed build before packaging would
; otherwise ship its own stale copy (the pristine one is installed from the root,
; two lines below). Runtime folders must never be swept into the installer —
; StreamTweak shipped a WebView2 cache this way for six releases.
; *.bat excluded: the deploy directory is where throwaway launchers get dropped while
; chasing a bug, and a build machine's scratch scripts must never reach a user.
Source: "{#SourceDir}\*"; DestDir: "{app}"; \
    Excludes: "*.log,*.bat,sl_*.txt,streamlight_pad.log,portable.dat,cache\*"; \
    Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\gamecontrollerdb.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "installer\resources\streamlight.png"; Flags: dontcopy
Source: "changelog.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

; No [Registry] section on purpose. There used to be an HKCU entry creating
; Software\FoggyBytes\StreamLight with uninsdeletekey, but nothing ever wrote to that
; key — the app's settings live under Software\Moonlight Game Streaming Project\Moonlight
; (see main.cpp; that path is load-bearing and must not change). The empty key also
; triggered Inno's UsedUserAreasWarning: this installer runs elevated, so HKCU resolves
; to the elevating account's hive, which may not be the interactive user's — the same
; trap the [Run] section below avoids with runasoriginaluser.
; Deliberately NOT re-pointed at the real settings key: deleting it on uninstall would
; make a reinstall lose paired hosts and preferences.

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
// ── Stop Windows from maximising the wizard ──────────────────────────────────
// On a handheld — the ROG Ally is where this shows up — the wizard opens filling
// the whole screen, with the layout still drawn for a small window: the artwork
// at its natural size in the top-left corner and a large empty area around it.
// That is the signature of a window MAXIMISED after its layout was computed, not
// of a wizard computed too large (which would stretch the artwork to full height).
// It is not the Inno Setup version: it did the same under Inno Setup 6.
//
// Windows only auto-maximises windows that can be maximised, so the fix is to say
// this one cannot: take the sizing frame and the maximise box off it. Setup's
// wizard is not meant to be resized anyway — Inno Setup 7 dropped WizardResizable
// for exactly that reason, which makes this a no-op there and a fix everywhere else.
//
// ⚠️ SetWindowLongW, not SetWindowLongPtrW. Both are exported by user32 on 64-bit,
// but the Ptr variant takes a LONG_PTR (8 bytes) and we build a 64-bit installer
// (SetupArchitecture=x64), so handing it a 32-bit value is the argument-size
// mismatch Inno Setup 7's release notes warn about. Window styles are 32-bit, so
// the plain variant is the correct one for GWL_STYLE on any architecture.
const
  GWL_STYLE        = -16;
  WS_MAXIMIZEBOX   = $00010000;
  WS_THICKFRAME    = $00040000;
  SWP_NOSIZE       = $0001;
  SWP_NOMOVE       = $0002;
  SWP_NOZORDER     = $0004;
  SWP_NOACTIVATE   = $0010;
  SWP_FRAMECHANGED = $0020;

function GetWindowLong(hWnd: HWND; nIndex: Integer): LongInt;
  external 'GetWindowLongW@user32.dll stdcall';
function SetWindowLong(hWnd: HWND; nIndex: Integer; dwNewLong: LongInt): LongInt;
  external 'SetWindowLongW@user32.dll stdcall';
function SetWindowPos(hWnd: HWND; hWndInsertAfter: HWND; X, Y, cx, cy: Integer; uFlags: Cardinal): LongInt;
  external 'SetWindowPos@user32.dll stdcall';

procedure MakeWizardFixedSize;
begin
  SetWindowLong(WizardForm.Handle, GWL_STYLE,
    GetWindowLong(WizardForm.Handle, GWL_STYLE) and not (WS_MAXIMIZEBOX or WS_THICKFRAME));

  // Required after any style change: SetWindowLong alters the style bits but leaves the
  // cached non-client frame alone, so the window keeps the client area computed for the old
  // styles until Windows is asked to recompute it. SetWindowLong's own documentation
  // prescribes this call.
  //
  // ⚠️ It is NOT the fix for the check boxes being clipped on their left edge — that was my
  // first theory and hardware disproved it. That symptom appears only on the Ally and is
  // unexplained; see §28. This call stays because it is correct on its own terms.
  SetWindowPos(WizardForm.Handle, 0, 0, 0, 0, 0,
    SWP_NOMOVE or SWP_NOSIZE or SWP_NOZORDER or SWP_NOACTIVATE or SWP_FRAMECHANGED);
end;

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
  // Before anything is laid out: the window exists by now, but has not been shown.
  MakeWizardFixedSize;

  ExtractTemporaryFile('streamlight.png');
  TmpFileName := ExpandConstant('{tmp}\streamlight.png');

  LogoImage := TBitmapImage.Create(WizardForm);
  LogoImage.Parent := WizardForm.WelcomePage;
  // PngImage (not Bitmap) is the loader for .png — see Inno Setup's CodeClasses example.
  LogoImage.PngImage.LoadFromFile(TmpFileName);
  LogoImage.Left := WizardForm.WelcomeLabel1.Left;
  LogoImage.Top := WizardForm.WelcomeLabel1.Top + WizardForm.WelcomeLabel1.Height + ScaleY(25);
  // Sized explicitly, NOT with AutoSize.
  //
  // AutoSize draws the PNG at its native pixel size, so the artwork's own resolution silently
  // becomes the layout. That held while the file happened to be 96x96; the moment it was
  // replaced with a 672x672 master the logo rendered seven times too big and bled across the
  // welcome page. Stretch scales whatever it is given into the box below, so the asset can be
  // any resolution — and a higher one is now the better choice, since it downscales cleanly.
  //
  // ScaleX/ScaleY where AutoSize gave raw pixels: the rest of this layout is already
  // DPI-scaled, so the logo was the one element that shrank on a high-DPI display.
  LogoImage.AutoSize := False;
  LogoImage.Stretch := True;
  LogoImage.Width := ScaleX(96);
  LogoImage.Height := ScaleY(96);

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
    // NB: this label has no WordWrap, so every bullet must stay on one line —
    // keep them at or under ~76 characters or they get clipped on the right.
    '•  Link-speed matching — the host follows this device, no bufferbloat' + #13#10 +
    '•  Seamless launch — optionally wait for the game to appear' + #13#10 +
    '•  Wake the host and sign in with its PIN, from the sofa, on the pad' + #13#10 +
    '•  Live host metrics overlay (GPU, encoder, VRAM, temperature, CPU, network)' + #13#10 +
    '•  NVIDIA Sentinel — protects your driver profile from NVIDIA App resets' + #13#10 +
    '•  Auto HDR toggle + spatial audio (Dolby Atmos / Windows Sonic)' + #13#10 +
    '•  Game library sync with store badges (Steam, Epic, GOG, Xbox, …)' + #13#10 +
    '•  Session quality grading, and the host''s last session on your Home' + #13#10 +
    '•  Live bitrate shown against your configured target on the host dashboard' + #13#10 +
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
