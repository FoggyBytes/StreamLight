; StreamLight 8.6.1 — Moonlight fork with StreamTweak integration.
; SourceDir is the self-contained runtime built by build-arch.bat +
; manual windeployqt (see CLAUDE.md §3).
#define AppName "StreamLight"
; ⚠️ The second place the version is written, and the only one kept by hand: the exe
; takes it from app\version.txt through app.pro (VERSION and VERSION_STR), the installer
; does not. Bump both or the setup ships as the previous version and names its own file
; StreamLight_<old>_Installer, with nothing to notice it by — 5.6.0 was built with this
; line still reading 5.5.0.
;
; Reading version.txt from here was considered and left alone: that file carries a UTF-8
; BOM and no trailing newline, so an ISPP FileRead would have to skip three bytes by
; position, which is a more fragile thing than one line to remember at release time.
#define AppVersion "8.6.1"
#define AppPublisher "FoggyBytes"
#define AppURL "https://github.com/FoggyBytes/StreamLight"
#define AppExeName "StreamLight.exe"
#define SourceDir "build\deploy-x64-release"

; ── Microsoft Visual C++ Runtime (msvcp140/vcruntime140) ─────────────────────────────────
; The app and Qt are built /MD and the deploy directory carries no runtime
; (build-arch.bat runs windeployqt --no-compiler-runtime), so StreamLight runs on whatever
; msvcp140.dll the machine has. That stopped being safe with 7.0.0: Qt 6.12 is built with
; MSVC 14.44 and uses the constexpr std::mutex constructor from 14.40 (its DLLs no longer
; import _Mtx_init_in_situ; Qt 6.8.3's did), and an older msvcp140.dll dereferences null in
; _Mtx_lock. The exe itself is built with 14.51. Microsoft's rule is that the runtime must be
; at least as new as the newest toolset in the process. Upstream's WiX bundle ships VC_redist
; for the same reason (wix/MoonlightSetup/Bundle.wxs); this installer never did.
;
; The redistributable is taken from the toolset that builds the exe, never from a copy kept in
; the repo: when Visual Studio moves to a new toolset this folder goes away and the compile
; stops here, instead of shipping a runtime older than the code. Update the path then — the
; minimum version follows by itself, read from the file below.
; The GitHub Actions build passes its own with /DVCRedist=…, the runner's toolset being another.
#ifndef VCRedist
  #define VCRedist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Redist\MSVC\14.51.36231\vc_redist.x64.exe"
#endif
#if !FileExists(VCRedist)
  #error VC_redist not found: point VCRedist at the Redist folder of the toolset that builds StreamLight.exe
#endif
#define VCRedistVersion GetVersionNumbersString(VCRedist)

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
; Product version "7.0.1.0", as StreamLight.exe and AntiHooking.dll carry it (qmake writes four
; parts): SignPath requires the same product name and version on every file it signs, and
; without these lines Setup said "7.0.1" and had no file version at all.
VersionInfoVersion={#AppVersion}.0
VersionInfoProductTextVersion={#AppVersion}.0
; Signed uninstaller, switched on only by the GitHub Actions build when it signs
; (/DSignUninstaller). The uninstaller is not only unins000.exe: it is the same binary as the
; Setup program the loader unpacks to %TEMP% and runs during the install, so an unsigned one
; leaves Smart App Control something unsigned to stop even with a signed Setup.exe. With no
; SignTool, Inno works in two passes: the first compile writes uninst-<version>-<hash>.e64 into
; SignedUninstallerDir and stops; once that file is signed, the second compile copies the
; signature in. Local builds never define SignUninstaller and compile as before.
#ifdef SignUninstaller
  #ifndef SignedUninstallerDir
    #define SignedUninstallerDir "build\signing\uninstaller"
  #endif
SignedUninstaller=yes
SignedUninstallerDir={#SignedUninstallerDir}
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
WelcomeLabel1=Welcome to the StreamLight Setup Wizard
WelcomeLabel2=

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "xboxtile"; Description: "Add an icon to the Xbox app's 'My apps' section"; GroupDescription: "{cm:AdditionalIcons}"

[InstallDelete]
; 7.0.1 (issue #40): the Qt plugin folders are emptied before [Files] copies them again.
; Without this a plugin a release no longer ships stays behind forever, and a stale one
; can break a Qt upgrade: a 2024 tls\qopensslbackend.dll, left by an old install, does not
; load under Qt 6.12 (it imports QTlsBackend::addTustedRoot, no longer exported), Qt stops
; loading TLS plugins at the first one that fails, so qschannelbackend.dll is never reached
; and StreamLight dies at startup on the "cert-only" backend. Every folder below is rebuilt
; in full from the deploy directory; settings and cache live outside {app}.
Type: filesandordirs; Name: "{app}\tls"
Type: filesandordirs; Name: "{app}\platforms"
Type: filesandordirs; Name: "{app}\imageformats"
Type: filesandordirs; Name: "{app}\iconengines"
Type: filesandordirs; Name: "{app}\networkinformation"
Type: filesandordirs; Name: "{app}\qml"
; 8.3.0: SDL2_ttf.dll is no longer shipped (the overlays are painted with QPainter). An upgrade
; would otherwise keep the 8.2.0 copy in {app} for good.
Type: files; Name: "{app}\SDL2_ttf.dll"

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
; Extracted and run by InstallVCRedist in [Code], only when the machine's runtime is older.
Source: "{#VCRedist}"; Flags: dontcopy
Source: "changelog.txt"; DestDir: "{app}"; Flags: ignoreversion
; 7.0.0 (issue #38): the four pictures for StreamLight's non-Steam shortcut, for anyone who
; assigns them by hand in Steam. Settings → Interface → Steam artwork writes the same ones
; (embedded in the exe from the same files) into Steam's grid folder by itself.
Source: "app\res\steam\*.png"; DestDir: "{app}\Steam artwork"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

; No [Registry] section on purpose. There used to be an HKCU entry creating
; Software\FoggyBytes\StreamLight with uninsdeletekey, but nothing ever wrote to that
; key. As of 5.4.0 the app's settings DO live under Software\FoggyBytes\StreamLight
; (main.cpp; before 5.4.0 they were under Software\Moonlight Game Streaming Project\
; Moonlight, shared with Moonlight itself) — and the section still must not come back:
;
;   · uninsdeletekey on the real settings key would make a reinstall lose paired hosts
;     and preferences;
;   · this installer runs elevated, so HKCU here resolves to the ELEVATING account's
;     hive, which may not be the interactive user's. Anything the setup wrote would land
;     in the wrong place — which is why the store change in 5.4.0 is done by the app at
;     startup and never by the installer. Same trap the [Run] section below avoids with
;     runasoriginaluser.

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
; The box on the last page of a manual install. ⚠️ Not how the in-app update reopens the app
; (5.8.0–8.5.x it was): since 8.6.0 StreamLightUpdater.exe runs this setup /VERYSILENT /APPUPDATE
; and reopens StreamLight itself, as the user — started by the updater, this setup is already
; elevated, and its runasoriginaluser would start StreamLight elevated too. skipifsilent keeps
; this entry out of it.
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; \
    Flags: nowait postinstall skipifsilent

[UninstallDelete]
; 8.4.0: the TV service's pairing (it holds the key the TV granted) and, since 8.5.0, its log
; folder. The service itself is stopped and deleted in CurUninstallStepChanged, before this runs.
Type: filesandordirs; Name: "{commonappdata}\FoggyBytes\StreamLight\service"
Type: filesandordirs; Name: "{commonappdata}\FoggyBytes\StreamLight\logs"
; 8.6.0: the updater's work folder and its status (RegisterUpdater in [Code]).
Type: filesandordirs; Name: "{commonappdata}\FoggyBytes\StreamLight\update"
Type: filesandordirs; Name: "{commonappdata}\FoggyBytes\StreamLight\update-status"

[UninstallRun]
; 8.6.0: the update task (RegisterUpdater in [Code], updater/task.cpp). Same name as k_TaskName.
Filename: "schtasks.exe"; Parameters: "/Delete /TN ""StreamLight Update"" /F"; \
    Flags: runhidden waituntilterminated; RunOnceId: "DeleteUpdateTask"
; 8.2.0: the sign-in task (Settings → Interface → Start at sign-in, app/backend/signinlaunch.cpp).
; Left behind, it would try to start an exe that is gone at every sign-in, forever. Same name
; as k_TaskName; harmless when the switch was never turned on.
Filename: "schtasks.exe"; Parameters: "/Delete /TN ""StreamLight Startup"" /F"; \
    Flags: runhidden waituntilterminated; RunOnceId: "DeleteSignInTask"

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
    '•  Seamless launch — waits for the game, flags it if Quit leaves it open' + #13#10 +
    '•  Wake the host and sign in with its PIN, from the sofa, on the pad' + #13#10 +
    '•  Live host metrics overlay (GPU, encoder, VRAM, temperature, CPU, network)' + #13#10 +
    '•  NVIDIA Sentinel — protects your driver profile from NVIDIA App resets' + #13#10 +
    '•  The host from the sofa — dashboard, sessions, library, apps, Sentinel' + #13#10 +
    '•  Game library sync with store badges (Steam, Epic, GOG, Xbox, …)' + #13#10 +
    '•  Session quality grading and charts, bitrate against your set target' + #13#10 +
    '•  Shared clipboard while you stream — encrypted, passwords cleared' + #13#10 +
    '•  Remote host sleep, restart, power-off and Windows Update' + #13#10 +
    '•  Hue Sync keeps the lights on this device when the host wakes' + #13#10 +
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

// ── Microsoft Visual C++ Runtime ─────────────────────────────────────────────
// Why and which version: VCRedist at the top. Installed only when the machine's runtime is
// older than the one bundled, and before any file is copied: the first thing [Run] does is
// start StreamLight.exe (--register-xbox-tile), which needs it. Never uninstalled — it is
// shared by every program on the machine.
const
  VCRedistKey = 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64';
  ERROR_SUCCESS_REBOOT_REQUIRED = 3010;
  ERROR_PRODUCT_VERSION = 1638;   // a newer runtime is already installed: fine

var
  VCRedistRestart: Boolean;

// The version VC_redist's own setup records. The x64 runtime registers itself in both
// registry views; the native one is read first.
function InstalledVCRedist(RootKey: Integer; var Version: Int64): Boolean;
var
  Installed, Major, Minor, Bld, Rbld: Cardinal;
begin
  Result := RegQueryDWordValue(RootKey, VCRedistKey, 'Installed', Installed) and (Installed = 1) and
            RegQueryDWordValue(RootKey, VCRedistKey, 'Major', Major) and
            RegQueryDWordValue(RootKey, VCRedistKey, 'Minor', Minor) and
            RegQueryDWordValue(RootKey, VCRedistKey, 'Bld', Bld);
  if Result then begin
    if not RegQueryDWordValue(RootKey, VCRedistKey, 'Rbld', Rbld) then
      Rbld := 0;
    Version := PackVersionComponents(Word(Major), Word(Minor), Word(Bld), Word(Rbld));
  end;
end;

function VCRedistNeeded: Boolean;
var
  Bundled, Present: Int64;
begin
  // Unreadable bundled version or no runtime recorded: run it. At worst VC_redist answers
  // 1638 and changes nothing.
  Result := True;
  if not StrToVersion('{#VCRedistVersion}', Bundled) then
    Exit;
  if InstalledVCRedist(HKLM64, Present) or InstalledVCRedist(HKLM32, Present) then begin
    Log('VC++ runtime: found ' + VersionToStr(Present) + ', bundled {#VCRedistVersion}');
    Result := ComparePackedVersion(Present, Bundled) < 0;
  end else
    Log('VC++ runtime: none recorded, bundled {#VCRedistVersion}');
end;

procedure InstallVCRedist;
var
  Started: Boolean;
  ResultCode: Integer;
begin
  if not VCRedistNeeded then
    Exit;

  Started := False;
  WizardForm.StatusLabel.Caption := 'Installing Microsoft Visual C++ Runtime...';
  WizardForm.ProgressGauge.Style := npbstMarquee;
  try
    ExtractTemporaryFile('vc_redist.x64.exe');
    Started := Exec(ExpandConstant('{tmp}\vc_redist.x64.exe'), '/install /quiet /norestart', '',
                    SW_HIDE, ewWaitUntilTerminated, ResultCode);
  finally
    WizardForm.ProgressGauge.Style := npbstNormal;
  end;

  if not Started then
    Log('VC++ runtime: VC_redist could not be started, error ' + IntToStr(ResultCode))
  else
    Log('VC++ runtime: VC_redist exit code ' + IntToStr(ResultCode));

  if Started and ((ResultCode = 0) or (ResultCode = ERROR_PRODUCT_VERSION)) then
    Exit;
  if Started and (ResultCode = ERROR_SUCCESS_REBOOT_REQUIRED) then begin
    // A runtime DLL was in use and is replaced at the next boot: until then a new StreamLight
    // would still load the old one, so the last page asks for the restart (Setup then hides
    // the Launch box by itself).
    VCRedistRestart := True;
    Exit;
  end;

  // Not fatal for the setup, but StreamLight may not start: say so instead of leaving a
  // program that closes on launch with nothing to explain it.
  SuppressibleMsgBox('The Microsoft Visual C++ Runtime could not be installed (code ' +
    IntToStr(ResultCode) + ').' + #13#10#13#10 +
    'StreamLight needs it to start. Install the latest Microsoft Visual C++ ' +
    'Redistributable (x64), then open StreamLight again.', mbError, MB_OK, IDOK);
end;

// ── The TV service (8.4.0) ───────────────────────────────────────────────────
// StreamLightService.exe keeps an LG TV in step with this PC, from boot on (StreamLight/service/).
// It runs as LOCAL SERVICE, not LocalSystem: talking to a TV on the LAN needs nothing more.
// Stopped before the files are replaced (it holds its exe and Qt6Core.dll open), registered or
// re-pointed after, removed at uninstall. With no TV paired it stops by itself after ten minutes,
// and StreamLight starts it again when Settings → TV needs it — which the signed-in user may do
// because of the service's own permissions below (RP = start, for INTERACTIVE).
const
  TvServiceName = 'StreamLight';
  // Windows' default service permissions, plus RP (start) for the signed-in user.
  TvServiceSddl = 'D:(A;;CCLCSWRPWPDTLOCRRC;;;SY)(A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;BA)' +
                  '(A;;CCLCSWRPLOCRRC;;;IU)(A;;CCLCSWLOCRRC;;;SU)S:(AU;FA;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;WD)';
  ERROR_SERVICE_DOES_NOT_EXIST = 1060;

function RunHidden(const Exe, Params: String): Integer;
begin
  if not Exec(Exe, Params, '', SW_HIDE, ewWaitUntilTerminated, Result) then
    Result := -1;
  Log('TV service: ' + ExtractFileName(Exe) + ' ' + Params + ' -> ' + IntToStr(Result));
end;

// net stop waits until the service has stopped (sc stop would only ask).
procedure StopTvService;
begin
  RunHidden(ExpandConstant('{sys}\net.exe'), 'stop ' + TvServiceName);
end;

procedure RegisterTvService;
var
  Exe, DataDir, LogDir, Common, AppDir, Icacls: String;
begin
  Exe := ExpandConstant('{app}\StreamLightService.exe');
  Icacls := ExpandConstant('{sys}\icacls.exe');

  // ⚠️ 8.5.0 (security review): the service runs StreamLightService.exe and Qt6Core.dll from
  // {app}, as LOCAL SERVICE, from boot. Under Program Files only administrators can change them;
  // a folder chosen elsewhere (D:\…) inherits write access for every signed-in user from its
  // drive, and whoever replaced a DLL there would run as the service. Such a folder gets
  // Program Files' rights: SYSTEM and administrators full, users and app packages read.
  AppDir := ExpandConstant('{app}');
  if Pos(Lowercase(ExpandConstant('{commonpf}')) + '\', Lowercase(AppDir) + '\') <> 1 then begin
    RunHidden(Icacls, '"' + AppDir + '" /setowner *S-1-5-32-544 /T /C /Q');
    RunHidden(Icacls, '"' + AppDir + '" /inheritance:r /grant:r *S-1-5-18:(OI)(CI)F *S-1-5-32-544:(OI)(CI)F' +
                      ' *S-1-5-32-545:(OI)(CI)RX *S-1-15-2-1:(OI)(CI)RX /C /Q');
    RunHidden(Icacls, '"' + AppDir + '\*" /reset /T /C /Q');
  end;
  Common := 'binPath= "\"' + Exe + '\"" obj= "NT AUTHORITY\LocalService" start= auto';
  if RunHidden(ExpandConstant('{sys}\sc.exe'), 'query ' + TvServiceName) = ERROR_SERVICE_DOES_NOT_EXIST then
    RunHidden(ExpandConstant('{sys}\sc.exe'), 'create ' + TvServiceName + ' ' + Common + ' DisplayName= "StreamLight"')
  else
    RunHidden(ExpandConstant('{sys}\sc.exe'), 'config ' + TvServiceName + ' ' + Common);
  RunHidden(ExpandConstant('{sys}\sc.exe'), 'description ' + TvServiceName + ' "Turns an LG webOS TV on and off with this PC."');
  RunHidden(ExpandConstant('{sys}\sc.exe'), 'failure ' + TvServiceName + ' reset= 86400 actions= restart/5000/restart/5000//0');
  RunHidden(ExpandConstant('{sys}\sc.exe'), 'sdset ' + TvServiceName + ' "' + TvServiceSddl + '"');

  // Its pairing (tv.json). LOCAL SERVICE (S-1-5-19) must write there.
  // ⚠️ 8.5.0 (security review): its own rights, not ProgramData's. Inherited, every user could
  // read tv.json — the TV's client key — and create files in the folder; and a folder made
  // beforehand by a user stayed theirs. Now owned by administrators, with SYSTEM and
  // administrators full and LOCAL SERVICE modify, and nothing for anyone else: StreamLight
  // itself never reads here, it asks the service through its pipe.
  DataDir := ExpandConstant('{commonappdata}\FoggyBytes\StreamLight\service');
  ForceDirectories(DataDir);
  RunHidden(Icacls, '"' + DataDir + '" /setowner *S-1-5-32-544 /T /C /Q');
  RunHidden(Icacls, '"' + DataDir + '" /inheritance:r /grant:r *S-1-5-18:(OI)(CI)F *S-1-5-32-544:(OI)(CI)F' +
                    ' *S-1-5-19:(OI)(CI)M /C /Q');
  RunHidden(Icacls, '"' + DataDir + '\*" /reset /T /C /Q');

  // ⚠️ 8.5.0: its log, in a folder of its own (service/tvstore.cpp, TvStore::logFolder). In the
  // closed folder above, users could not open it any more; here they may read but not write, so
  // the app copies it next to its own logs (LogsAndCache::copyServiceLog). It never holds the
  // key or the certificate. Owned by administrators for the same reason as the folder above.
  // The log the 8.4.0 service left beside tv.json goes.
  LogDir := ExpandConstant('{commonappdata}\FoggyBytes\StreamLight\logs');
  ForceDirectories(LogDir);
  RunHidden(Icacls, '"' + LogDir + '" /setowner *S-1-5-32-544 /T /C /Q');
  RunHidden(Icacls, '"' + LogDir + '" /inheritance:r /grant:r *S-1-5-18:(OI)(CI)F *S-1-5-32-544:(OI)(CI)F' +
                    ' *S-1-5-19:(OI)(CI)M *S-1-5-32-545:(OI)(CI)RX /C /Q');
  RunHidden(Icacls, '"' + LogDir + '\*" /reset /T /C /Q');
  DeleteFile(DataDir + '\service.log');
  DeleteFile(DataDir + '\service.log.1');

  // Started now so a TV already paired is followed from this moment, not from the next boot.
  RunHidden(ExpandConstant('{sys}\sc.exe'), 'start ' + TvServiceName);
end;

// ── The updater (8.6.0) ──────────────────────────────────────────────────────
// StreamLightUpdater.exe installs an update started from Settings → About without the Windows
// prompt, which a controller cannot answer (updater/, and SECURITY.md for what it checks). Here:
// its folders with their rights, and its task, "StreamLight Update", for the account running
// this setup. When the updater itself runs this setup it passes /APPUPDATE, and the progress
// goes to its screen instead of a wizard.
const
  UpdaterScreenClass = 'StreamLightUpdaterScreen';   // updater/common.cpp, k_ScreenClass
  WM_APP_SETUPPROGRESS = $8001;                      // WM_APP + 1, k_MsgSetupProgress
  // FILE_ATTRIBUTE_REPARSE_POINT ($400) is Inno Setup's own.
  NO_FILE_ATTRIBUTES = $FFFFFFFF;                    // INVALID_FILE_ATTRIBUTES

function GetFileAttributes(lpFileName: String): Cardinal;
  external 'GetFileAttributesW@kernel32.dll stdcall';

var
  AppUpdateRunChecked, AppUpdateRunValue: Boolean;

function IsAppUpdate: Boolean;
var
  I: Integer;
begin
  if not AppUpdateRunChecked then begin
    AppUpdateRunChecked := True;
    AppUpdateRunValue := False;
    for I := 1 to ParamCount do
      if CompareText(ParamStr(I), '/APPUPDATE') = 0 then
        AppUpdateRunValue := True;
  end;
  Result := AppUpdateRunValue;
end;

// The updater's screen draws the bar; nothing is drawn here (/VERYSILENT).
procedure CurInstallProgressChanged(CurProgress, MaxProgress: Integer);
var
  Wnd: HWND;
begin
  if not IsAppUpdate then
    Exit;
  Wnd := FindWindowByClassName(UpdaterScreenClass);
  if Wnd <> 0 then
    PostMessage(Wnd, WM_APP_SETUPPROGRESS, CurProgress, MaxProgress);
end;

// A junction or symbolic link where a folder of ours should be is removed, not followed: the
// updater writes in these folders with administrator rights. (RemoveDir on a junction removes
// the junction only.)
procedure DropReparsePoint(const Dir: String);
var
  Attrs: Cardinal;
begin
  Attrs := GetFileAttributes(Dir);
  if (Attrs <> NO_FILE_ATTRIBUTES) and ((Attrs and FILE_ATTRIBUTE_REPARSE_POINT) <> 0) then begin
    Log('Updater: removing the reparse point at ' + Dir);
    RemoveDir(Dir);
  end;
end;

procedure RegisterUpdater;
var
  Icacls, Parent, Base, WorkDir, StatusDir, User: String;
  ResultCode: Integer;
begin
  Icacls := ExpandConstant('{sys}\icacls.exe');
  Parent := ExpandConstant('{commonappdata}\FoggyBytes');
  Base := Parent + '\StreamLight';
  WorkDir := Base + '\update';
  StatusDir := Base + '\update-status';

  // ⚠️ The two parents belong to the administrators. Anyone may create folders in ProgramData,
  // and whoever owns a folder can give themselves the right to rename what is inside it — the
  // updater's folders included. Their rights go back to what ProgramData passes down.
  ForceDirectories(Base);
  RunHidden(Icacls, '"' + Parent + '" /setowner *S-1-5-32-544 /C /Q');
  RunHidden(Icacls, '"' + Parent + '" /reset /C /Q');
  RunHidden(Icacls, '"' + Base + '" /setowner *S-1-5-32-544 /C /Q');
  RunHidden(Icacls, '"' + Base + '" /reset /C /Q');

  // The work folder: SYSTEM and administrators only. The updater copies itself and the
  // installer here, and gives Setup a TEMP here (updater/common.cpp, foldersAreSafe).
  DropReparsePoint(WorkDir);
  ForceDirectories(WorkDir);
  RunHidden(Icacls, '"' + WorkDir + '" /setowner *S-1-5-32-544 /T /C /Q');
  RunHidden(Icacls, '"' + WorkDir + '" /inheritance:r /grant:r *S-1-5-18:(OI)(CI)F *S-1-5-32-544:(OI)(CI)F /C /Q');
  RunHidden(Icacls, '"' + WorkDir + '\*" /reset /T /C /Q');

  // Its status (status.json, updater.log, setup.log): users may read it — StreamLight follows
  // the update there — and nobody but SYSTEM and administrators may write.
  DropReparsePoint(StatusDir);
  ForceDirectories(StatusDir);
  RunHidden(Icacls, '"' + StatusDir + '" /setowner *S-1-5-32-544 /T /C /Q');
  RunHidden(Icacls, '"' + StatusDir + '" /inheritance:r /grant:r *S-1-5-18:(OI)(CI)F *S-1-5-32-544:(OI)(CI)F' +
                    ' *S-1-5-32-545:(OI)(CI)RX /C /Q');
  RunHidden(Icacls, '"' + StatusDir + '\*" /reset /T /C /Q');

  // The task, for the account running this setup: the signed-in user after a consent prompt —
  // and the same account when the updater runs it. Its principal, rights and action are set by
  // the updater itself (updater/task.cpp); this setup only names the account.
  User := GetEnv('USERDOMAIN') + '\' + GetUserNameString;
  if not Exec(ExpandConstant('{app}\StreamLightUpdater.exe'), '--register "' + User + '"', '',
              SW_HIDE, ewWaitUntilTerminated, ResultCode) then
    ResultCode := -1;
  Log('Updater: task for ' + User + ' -> ' + IntToStr(ResultCode));
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  // An update: the running service holds files about to be replaced.
  StopTvService;
  Result := '';
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
    InstallVCRedist;
  if CurStep = ssPostInstall then begin
    RegisterTvService;
    RegisterUpdater;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  // Before the files go: a running service would keep its exe.
  if CurUninstallStep = usUninstall then begin
    StopTvService;
    RunHidden(ExpandConstant('{sys}\sc.exe'), 'delete ' + TvServiceName);
  end;
end;

function NeedRestart: Boolean;
begin
  Result := VCRedistRestart;
end;
