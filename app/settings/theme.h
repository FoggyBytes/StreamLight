#pragma once

#include <QColor>
#include <QObject>
#include <QDate>
#include <QQmlEngine>
#include <QTimer>
#include <QVariantList>

/**
 * One colour generates the whole interface.
 *
 * <p>Every accent-bearing surface in the app — the focus ring, the primary button, the active
 * tab, the mark in the header, the wash on the background — reads from here rather than naming
 * a colour of its own. That is what makes the accent user-settable at all: 377 literals spread
 * across 22 QML files could never have been recoloured from a settings page.</p>
 *
 * <p><b>The rule that keeps this usable: semantic colours never follow the accent.</b> Online
 * stays green, a pending link change stays amber, Shut down stays red, offline stays grey. If
 * they tracked the accent, a user who picked red would read "online" in red and the colour would
 * stop meaning anything. The accent says <i>where you are</i>; the semantics say <i>how it is</i>.
 * They are exposed here together so that the distinction is visible in one file instead of being
 * a convention people have to remember.</p>
 */
class Theme : public QObject
{
    Q_OBJECT

    /** The one colour the user chooses. Persisted; everything below derives from it. */
    Q_PROPERTY(QColor accent READ accent WRITE setAccent NOTIFY changed)

    /**
     * The accent palette (7.0.0): twelve hues in two rows, vivid then pale, 24 entries in all,
     * each a map { name, color }. The pale row is the vivid one mixed 45% with white and has
     * no name of its own. Ion, Signal, Ember, Amber and Ultra — the five presets Settings had
     * up to 6.5 — are all in the vivid row, under the same names.
     *
     * ⚠️ Every entry is meant to hold at least 3:1 against `card`, so a focus ring drawn in it
     * stays visible. That is why there is no dark row: a navy accent would vanish on a card.
     */
    Q_PROPERTY(QVariantList accentPalette READ accentPalette CONSTANT)

    /** Black or white, whichever reads better on accentFill — the higher contrast wins. */
    Q_PROPERTY(QColor onAccent READ onAccent NOTIFY changed)

    /**
     * The accent as a FILL (08/10/2026): a selected pill, a lit button, a tag. In Dark it is
     * the accent itself. In Light it is the accent deepened to inkText, so a fill never glares
     * on the grey page and its white label clears 4.5:1 — the vivid Rose of 7.0.0's first light
     * carried white text at 3.3:1, and Dark carried it the same way (onAccent chose by a fixed
     * luminance threshold, not by contrast). Text on it is always onAccent.
     */
    Q_PROPERTY(QColor accentFill READ accentFill NOTIFY changed)

    /** Page background: near-black carrying a trace of the accent, never pure black. */
    Q_PROPERTY(QColor ground READ ground NOTIFY changed)

    /**
     * Dark, Light, System or Auto (7.0.0, issue #33). Persisted. System follows Windows through
     * QStyleHints::colorScheme() and changes with it while the app is open. Auto (13/10/2026)
     * switches by the clock — between sunrise and sunset where Windows says the PC is, or two set
     * hours — see autoHow.
     *
     * Light is direction A of the mockup ("Paper"): a pale cool floor, white cards, near-black
     * text. ⚠️ Not everything follows it, on purpose: the host card on Home keeps its coloured
     * backdrop (it is content, like a cover — HostStage), and the launch curtain and the
     * opening animation stay dark because they lead into the stream.
     */
    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY changed)

    /**
     * The theme actually in force: `mode` with System and Auto resolved, and the Home toggle's
     * hold applied. What every colour reads. Cached and re-evaluated on a clock (see m_Clock),
     * because Auto changes with nothing else to notify it.
     */
    Q_PROPERTY(bool light READ light NOTIFY changed)

    /**
     * ── Automatic switch (13/10/2026) ──────────────────────────────────────────────
     * How Auto decides: 0 = Light between sunrise and sunset here, 1 = Light from `lightFrom`
     * to `darkFrom` (minutes after midnight, local time). Sunrise and sunset come from the
     * sunrise equation on the position Windows gives (WindowsLocation) — read, never stored.
     * Without a position (location off in Windows, desktop apps not allowed, none found) the
     * sun cannot be used: effectiveAutoHow is 1 and Settings greys Sunrise & sunset out.
     * Time zone and daylight saving are Windows' own: every time here is local.
     */
    Q_PROPERTY(int autoHow READ autoHow WRITE setAutoHow NOTIFY scheduleChanged)
    Q_PROPERTY(int effectiveAutoHow READ effectiveAutoHow NOTIFY scheduleChanged)
    // 0 = asking Windows, 1 = position available, 2 = not available.
    Q_PROPERTY(int locationState READ locationState NOTIFY scheduleChanged)
    Q_PROPERTY(int lightFrom READ lightFrom WRITE setLightFrom NOTIFY scheduleChanged)
    Q_PROPERTY(int darkFrom READ darkFrom WRITE setDarkFrom NOTIFY scheduleChanged)
    // Today's sunrise and sunset here, minutes after midnight local time; -1 without a
    // position, or when the sun does not rise or does not set here today.
    Q_PROPERTY(int sunriseMinutes READ sunriseMinutes NOTIFY scheduleChanged)
    Q_PROPERTY(int sunsetMinutes READ sunsetMinutes NOTIFY scheduleChanged)

    /**
     * The Home toggle holds the other theme over System or Auto (13/10/2026): `forced` is
     * true until the next change — Windows switching its own scheme, or the next sunrise,
     * sunset or set hour — and then the automatic choice takes over again. Under Dark or
     * Light the toggle simply sets the other mode, and nothing is held.
     * forcedUntilMinutes: when the hold ends, local minutes after midnight; -1 when it ends
     * with Windows (System) or nothing is held.
     */
    Q_PROPERTY(bool forced READ forced NOTIFY changed)
    Q_PROPERTY(int forcedUntilMinutes READ forcedUntilMinutes NOTIFY changed)

    /**
     * Keeps the theme as it is while true (a stream is up): the automatic switch waits for
     * the end rather than repainting the app behind the stream. The user's own choices —
     * setMode, toggleLight — still apply at once.
     */
    Q_PROPERTY(bool hold READ hold WRITE setHold NOTIFY holdChanged)

    /**
     * The accent as a FOREGROUND (7.0.0): focus rings, the underline of the current tab,
     * slider tracks, outlines — anything drawn as a line or as text on a card or the floor.
     *
     * In Dark it is the accent itself. In Light it is the accent with its lightness lowered
     * until it holds 3:1 against `card`, because Ion measures 1.8:1 on white and a pale accent
     * (Lemon, the pale row) would make a focus ring vanish. FILLS use accentFill.
     */
    Q_PROPERTY(QColor ink READ ink NOTIFY changed)

    /** The accent as TEXT (7.0.0): like ink, but held to 4.5:1, the bar for words this size. */
    Q_PROPERTY(QColor inkText READ inkText NOTIFY changed)

    /**
     * The colour to raise a surface with (7.0.0): white in Dark, the ink-black of the text in
     * Light. Hover washes and the faint fills that used to be written `Qt.rgba(1, 1, 1, a)` are
     * `Qt.rgba(Theme.tintBase.r, Theme.tintBase.g, Theme.tintBase.b, a)` now — white at 5%
     * is a lift on a dark card and nothing at all on a white one.
     */
    Q_PROPERTY(QColor tintBase READ tintBase NOTIFY changed)

    /** The app's floor under the waves (AmbientBackground): #151515 in Dark, pale in Light. */
    Q_PROPERTY(QColor floor READ floor NOTIFY changed)

    // Structure. Cool neutrals — deliberately NOT derived from the accent, or every surface in
    // the app would shift hue together and the accent would stop reading as an accent.
    // 7.0.0: one set for Dark and one for Light, chosen by `light`.
    Q_PROPERTY(QColor card     READ card     NOTIFY changed)
    Q_PROPERTY(QColor cardHigh READ cardHigh NOTIFY changed)
    Q_PROPERTY(QColor line     READ line     NOTIFY changed)
    Q_PROPERTY(QColor lineHigh READ lineHigh NOTIFY changed)
    Q_PROPERTY(QColor text     READ text     NOTIFY changed)
    Q_PROPERTY(QColor text2    READ text2    NOTIFY changed)
    Q_PROPERTY(QColor text3    READ text3    NOTIFY changed)
    // What a modal dialog lays over the page behind it (Overlay.modal). It was the same literal
    // written into eleven dialogs (6.3.1).
    Q_PROPERTY(QColor scrim    READ scrim    NOTIFY changed)

    // Semantics. Never follow the accent — see the class note. 7.0.0: a darker set in Light,
    // same meanings: the dark theme's green measured 1.7:1 on white.
    Q_PROPERTY(QColor online  READ online  NOTIFY changed)
    Q_PROPERTY(QColor warning READ warning NOTIFY changed)
    Q_PROPERTY(QColor danger  READ danger  NOTIFY changed)
    Q_PROPERTY(QColor offline READ offline NOTIFY changed)

    /**
     * The host card's own colours (7.0.0). The card on Home keeps its coloured backdrop in
     * both themes — it is content, like a cover — so what is drawn ON it must not follow the
     * theme: Light's dark greys and darker semantic colours would sink into a dark card. These
     * are the Dark values, constant. HostStage reads them inside the card, and so do the launch
     * and quit screens, which stay dark in both themes (AmbientBackground.dark).
     */
    Q_PROPERTY(QColor stageText     READ stageText     CONSTANT)
    Q_PROPERTY(QColor stageText2    READ stageText2    CONSTANT)
    Q_PROPERTY(QColor stageLine     READ stageLine     CONSTANT)
    Q_PROPERTY(QColor stageLineHigh READ stageLineHigh CONSTANT)
    Q_PROPERTY(QColor stageOnline   READ stageOnline   CONSTANT)
    Q_PROPERTY(QColor stageWarning  READ stageWarning  CONSTANT)
    Q_PROPERTY(QColor stageDanger   READ stageDanger   CONSTANT)
    Q_PROPERTY(QColor stageOffline  READ stageOffline  CONSTANT)

    /** Typeface. One family everywhere; see monoFamily for the single exception. */
    Q_PROPERTY(QString family READ family CONSTANT)

    /**
     * The type scale. Seven steps, and nothing outside them.
     *
     * <p>A modular scale: base 16 with a minor third (1.2), so every size is a fixed ratio of
     * every other rather than a number somebody liked at the time. The steps come out at
     * 11 · 13 · 16 · 19 · 23 · 28 · 34, which is not a coincidence — those seven were already
     * the most-used sizes in the app. What the scale removes is the <i>other</i> fourteen that
     * had accumulated beside them: 12, 14, 15, 17, 18, 20, 22, 26 and 30 each existed in one
     * or two files, a pixel or two from a neighbour, for no reason anybody could state.</p>
     *
     * <p><b>Why it belongs here rather than in a QML singleton.</b> Same argument as the
     * colours: these are read from 47 files, and the day the base or the ratio moves it has to
     * move once. Written as literals they could not be moved at all — which is exactly the
     * state they were in.</p>
     *
     * <p>⚠️ Design sizes, NOT final pixels. Every caller still multiplies by the interface
     * scale — <tt>_px(Theme.fontBody)</tt>, not <tt>Theme.fontBody</tt> — the window chrome
     * included: it used to be the exception, in fixed pixels, until the status bar and the
     * clock (5.7.0) and the Home header (6.3.1) were found drawn at two thirds the size of what
     * they sat beside on a handheld. Handing out pre-scaled values here would still take the
     * choice of scale away from the call site.</p>
     *
     * <p>The four sizes above the scale (40, 52, 64, 68, 100) are deliberately not steps: they
     * are one-off display figures — the PIN digits, the segue title, the big numbers on the
     * stage — each used exactly once, where the size <i>is</i> the design rather than a
     * position in a hierarchy.</p>
     */
    Q_PROPERTY(int fontCaption READ fontCaption CONSTANT)
    Q_PROPERTY(int fontSmall   READ fontSmall   CONSTANT)
    Q_PROPERTY(int fontBody    READ fontBody    CONSTANT)
    Q_PROPERTY(int fontTitle   READ fontTitle   CONSTANT)
    Q_PROPERTY(int fontH2      READ fontH2      CONSTANT)
    Q_PROPERTY(int fontH1      READ fontH1      CONSTANT)
    Q_PROPERTY(int fontDisplay READ fontDisplay CONSTANT)

    /**
     * Monospace, kept for one thing only: the pairing PIN. Those four digits exist to be read
     * off one screen and compared against another, and a 1 that looks like an l is precisely
     * the failure a monospaced face exists to prevent. Everywhere else the column alignment
     * comes from the layout, not from character width, so the proportional face is fine.
     */
    Q_PROPERTY(QString monoFamily READ monoFamily CONSTANT)

    /**
     * The user asked for less movement, or the system did. Animations check this instead of
     * each deciding for themselves; effects that cost GPU time check it too.
     */
    Q_PROPERTY(bool reduceAnimations READ reduceAnimations WRITE setReduceAnimations NOTIFY changed)

    /**
     * Play the opening animation — waves, logo, wordmark — before Home (6.0.0). On by default.
     * AppShell reads it once, at launch, and skips it anyway under reduceAnimations or when a
     * command line opened the app straight into a stream.
     */
    Q_PROPERTY(bool startupAnimation READ startupAnimation WRITE setStartupAnimation NOTIFY changed)

    /**
     * The host page's background (7.0.0, 13/10/2026): false = the app's floor and its waves, as
     * on Home; true = the focused game's cover, blurred, as in 6.5.1 (CoverAmbient — lifted
     * toward the floor in Light). Settings → Interface → Host page background. Persisted.
     */
    Q_PROPERTY(bool hostPageCovers READ hostPageCovers WRITE setHostPageCovers NOTIFY changed)

    /**
     * How much bigger than its design size everything should be drawn, for the window the app
     * is currently in. AppShell computes it from the window width and writes it here; the
     * pages have always had their own copy of the same number.
     *
     * It lives on the theme because of who could not reach it. A dialog is not inside a page —
     * it sits in the overlay above all of them — so it had no way to read a page's scale, and
     * every dialog in the app was therefore written in fixed pixels. On a large screen that
     * left the popups drawn at roughly half the size of the page behind them: a button written
     * as 44 on the host page comes out at 70, while the same button written as 38 in a dialog
     * comes out at 38, on any screen. Fixed numbers cannot be corrected by picking bigger ones,
     * because there is no value that is right on both a handheld and a television.
     *
     * ⚠️ Deliberately a property and not a `px(n)` invokable. A C++ invokable is opaque to the
     * QML engine, so a binding that called it would never be told the scale had changed and
     * would keep whatever value it happened to be built with. Callers multiply by it, or wrap
     * it in a QML-side helper — where the engine does see the property read.
     */
    Q_PROPERTY(qreal uiScale READ uiScale WRITE setUiScale NOTIFY uiScaleChanged)

    /**
     * The user's interface size, in percent (7.0.0): Settings → Interface → Interface → Interface
     * scale. It multiplies the automatic fit to the window — uiScale is the fit times this — so
     * a handheld can read bigger and a desk monitor can fit more. Persisted. 100 = as before.
     *
     * The pages that work out their own fit from their width (AppsScreen, HostStage, UnlockPad)
     * multiply by scaleFactor too, or a page and its dialogs would disagree on size.
     */
    Q_PROPERTY(int scalePercent READ scalePercent WRITE setScalePercent NOTIFY uiScaleChanged)
    Q_PROPERTY(qreal scaleFactor READ scaleFactor NOTIFY uiScaleChanged)

public:
    static Theme* get(QQmlEngine* qmlEngine = nullptr);

    explicit Theme(QObject* parent = nullptr);

    QColor accent()     const { return m_Accent; }
    QVariantList accentPalette() const;
    QColor onAccent()   const;
    QColor ground()     const;

    enum Mode { ModeDark = 0, ModeLight = 1, ModeSystem = 2, ModeAuto = 3 };
    Q_ENUM(Mode)

    int    mode()     const { return m_Mode; }
    bool   light()    const { return m_Light; }

    int     autoHow()   const { return m_AutoHow; }
    int     effectiveAutoHow() const;
    int     locationState() const { return m_LocState; }
    int     lightFrom() const { return m_LightFrom; }
    int     darkFrom()  const { return m_DarkFrom; }
    int     sunriseMinutes() const { return m_SunriseMin; }
    int     sunsetMinutes()  const { return m_SunsetMin; }
    bool    forced()    const { return m_Forced >= 0; }
    int     forcedUntilMinutes() const;
    bool    hold()      const { return m_Hold; }

    void setAutoHow(int how);
    void setLightFrom(int minutes);
    void setDarkFrom(int minutes);
    void setHold(bool on);

    /**
     * Asks Windows for the position again (at most every ten minutes), under Auto only.
     * Settings calls it when the Interface tab is shown, so switching location on in Windows
     * is picked up without a restart.
     */
    Q_INVOKABLE void refreshLocation();

    /**
     * The Home toggle (right stick click, T): the other theme. Under Dark or Light it sets the
     * other mode; under System or Auto it holds the other theme until the next change, or
     * drops the hold if the other theme is what the automatic choice says anyway.
     */
    Q_INVOKABLE void toggleLight();
    QColor ink()      const;
    QColor inkText()  const;
    QColor accentFill() const;
    QColor tintBase() const { return light() ? QColor(0x10, 0x1a, 0x20) : QColor(0xff, 0xff, 0xff); }
    /*
     * ── Light, rebuilt from Dark (08/10/2026) ───────────────────────────────────────────
     *
     * Dark is 6.5.1's, untouched: it had good contrast and Marcello asked to start from it.
     * Light is its mirror rather than a page of white: the first 7.0.0 light (white cards on
     * #eef1f4) was blinding on a TV and still failed — text3, online, warning and danger all
     * dropped under 4.5:1 on the floor. Here the floor sits at luminance 0.68 and the cards at
     * 0.81 (white is 1.0), cards lighter than the floor as Dark's are darker, and every text
     * token clears 4.5:1 on the card, the raised card, the floor and the floor's foot tinted by
     * any of the 24 accents, with the host card at its lowest opacity: text 10.7, text2 6.0,
     * text3 4.6, online 4.6, warning 5.0, danger 4.8 — the floor is always the worst case.
     * line / lineHigh sit at the same contrast steps as Dark's (1.22 / 1.81 against 1.22 /
     * 1.78). Measured with the script in §89.9 — change one value, rerun all of them.
     */
    QColor floor()    const { return light() ? QColor(0xcf, 0xd8, 0xde) : QColor(0x15, 0x15, 0x15); }

    QColor card()     const { return light() ? QColor(0xe3, 0xe9, 0xed) : QColor(0x0f, 0x15, 0x19); }
    QColor cardHigh() const { return light() ? QColor(0xd8, 0xe0, 0xe5) : QColor(0x16, 0x1f, 0x25); }
    QColor line()     const { return light() ? QColor(0x10, 0x1a, 0x20, 0x1a) : QColor(0xff, 0xff, 0xff, 0x14); }
    QColor lineHigh() const { return light() ? QColor(0x10, 0x1a, 0x20, 0x47) : QColor(0xff, 0xff, 0xff, 0x30); }
    QColor text()     const { return light() ? QColor(0x0f, 0x17, 0x1c) : QColor(0xf3, 0xf7, 0xf8); }
    QColor text2()    const { return light() ? QColor(0x36, 0x43, 0x4b) : QColor(0xa2, 0xb2, 0xba); }
    // Light: the text colour at 60% — enough to push the status bar under a dialog out of the
    // way (at 45% its prompts still competed with the dialog's own), not a black-out.
    QColor scrim()    const { return light() ? QColor(0x10, 0x1a, 0x20, 0x99) : QColor(0x00, 0x00, 0x00, 0xcc); }

    /**
     * Tertiary text — captions, section headers, the muted half of a two-tone line.
     *
     * ⚠️ It was #66767e, which measures 4.26:1 on the page and 3.90:1 on a card. WCAG AA wants
     * 4.5:1 for text this size, and this token's whole job is small text: the 11 px
     * "PLAYED"/"SESSIONS" captions, the uppercase section headers in Settings. #758790 is the
     * same hue lightened 15%, measuring 5.37:1 and 4.93:1, and it stays clearly below text2
     * (9.18:1) so the three-step hierarchy is intact. Do not darken it back without measuring.
     */
    QColor text3()    const { return light() ? QColor(0x46, 0x54, 0x5c) : QColor(0x75, 0x87, 0x90); }

    // Light: 4.6, 5.0 and 4.8:1 at worst (the floor), over 5.8:1 on a card.
    QColor online()  const { return light() ? QColor(0x0d, 0x5f, 0x2c) : QColor(0x4a, 0xde, 0x80); }
    QColor warning() const { return light() ? QColor(0x7f, 0x3a, 0x05) : QColor(0xf5, 0xa6, 0x23); }
    QColor danger()  const { return light() ? QColor(0x9b, 0x1c, 0x1c) : QColor(0xf8, 0x71, 0x71); }
    /**
     * "This host is off."
     *
     * ⚠️ It measures 3.67:1 on the page, which is BELOW the 4.5:1 that text needs — and that is
     * fine, because it is never text. Every use is either an 8 px dot (a graphical object,
     * where 3:1 is the bar, and it clears it) or the label of a disabled pill (which WCAG
     * exempts). Checked, all four sites, on 09/09/2026. If it ever does become the colour of a
     * live word, it has to be lightened first — and it cannot simply be lightened to match
     * text3, because the two would then be the same colour and "off" would stop meaning
     * anything next to "quiet".
     */
    // Light: 3.3:1 at worst — the same dots-and-disabled-only rule, so 3:1 is its bar.
    QColor offline() const { return light() ? QColor(0x5c, 0x6b, 0x74) : QColor(0x5b, 0x6c, 0x75); }

    QColor stageText()     const { return QColor(0xf3, 0xf7, 0xf8); }
    QColor stageText2()    const { return QColor(0xa2, 0xb2, 0xba); }
    QColor stageLine()     const { return QColor(0xff, 0xff, 0xff, 0x14); }
    QColor stageLineHigh() const { return QColor(0xff, 0xff, 0xff, 0x30); }
    QColor stageOnline()   const { return QColor(0x4a, 0xde, 0x80); }
    QColor stageWarning()  const { return QColor(0xf5, 0xa6, 0x23); }
    QColor stageDanger()   const { return QColor(0xf8, 0x71, 0x71); }
    QColor stageOffline()  const { return QColor(0x5b, 0x6c, 0x75); }

    QString family()     const { return QStringLiteral("DM Sans"); }
    QString monoFamily() const { return QStringLiteral("JetBrains Mono"); }

    // The type scale — see the note on the properties. Base 16, minor third (1.2).
    int fontCaption() const { return 11; }   // ms(-2)
    int fontSmall()   const { return 13; }   // ms(-1)
    int fontBody()    const { return 16; }   // ms( 0)  — base
    int fontTitle()   const { return 19; }   // ms( 1)
    int fontH2()      const { return 23; }   // ms( 2)
    int fontH1()      const { return 28; }   // ms( 3)
    int fontDisplay() const { return 34; }   // ms( 4)

    bool reduceAnimations() const { return m_ReduceAnimations; }
    bool startupAnimation() const { return m_StartupAnimation; }
    bool hostPageCovers() const { return m_HostPageCovers; }

    qreal uiScale() const { return m_UiScale; }
    int   scalePercent() const { return m_ScalePercent; }
    qreal scaleFactor() const { return m_ScalePercent / 100.0; }
    void  setScalePercent(int pct);

    void setAccent(const QColor& c);
    void setMode(int m);
    void setReduceAnimations(bool on);
    void setStartupAnimation(bool on);
    void setHostPageCovers(bool on);
    void setUiScale(qreal s);

    /**
     * Black or white, whichever can be read on top of @p background.
     *
     * The same decision `onAccent` makes for the accent, exposed for anything drawn over a
     * colour the app does not choose — chiefly the host stage, whose backdrop is a hue
     * derived from a picture the user picked. White text is right on nearly all of them and
     * wrong on the bright ones, and "nearly all" is not a thing a UI can rely on.
     */
    Q_INVOKABLE QColor onColor(const QColor& background) const;

    /** WCAG contrast ratio of two opaque colours, 1..21 — what onColor() decides by. */
    qreal contrast(const QColor& a, const QColor& b) const;

    /**
     * The palette name of @p c ("Ion", "Ember"…), or an empty string for a colour the palette
     * does not name — the pale row, or anything typed in the hex field. Callers show the hex
     * code then.
     */
    Q_INVOKABLE QString accentName(const QColor& c) const;

    /**
     * Shows @p c as the accent WITHOUT saving it (7.0.0). The palette popup calls this as its
     * cursor moves, so the whole interface previews the colour; it then either sets `accent`
     * (which saves) or calls this again with the colour it started from. Without it every
     * D-pad step through the palette would be a registry write.
     */
    Q_INVOKABLE void previewAccent(const QColor& c);

signals:
    void changed();
    void uiScaleChanged();
    void scheduleChanged();
    void holdChanged();

private:
    void save() const;
    QColor darkenedAccent(qreal ratio) const;

    // ── The automatic switch ──
    enum { LocAsking = 0, LocAvailable = 1, LocUnavailable = 2 };
    void askLocation(bool force);
    void onLocation(bool ok, double lat, double lon);

    bool systemLight() const;
    bool scheduleLight(qint64 nowMs) const;
    qint64 nextTransition(qint64 nowMs) const;
    bool computeLight() const;
    // Re-reads the clock and the hold; emits changed() when the theme in force moves, or
    // always with @p notify.
    void reevaluate(bool notify = false);
    void refreshSun();
    void clearForce();

    // The accent on screen: the saved one of the theme in force, or the palette's preview.
    QColor m_Accent;
    // The accent each theme keeps (13/10/2026). save() writes these, never m_Accent: during a
    // palette preview m_Accent is the colour under the cursor, and a save for any other reason
    // (mode, scale) would otherwise make the preview permanent.
    QColor m_AccentDark;
    QColor m_AccentLight;
    int    m_Mode = ModeDark;
    bool   m_Light = false;

    int     m_AutoHow = 0;
    // The position, in memory only (WindowsLocation); m_LastLight is the theme Auto last showed.
    int     m_LocState = LocAsking;
    double  m_Lat = 0;
    double  m_Lon = 0;
    bool    m_LocBusy = false;
    qint64  m_LocAskedAt = 0;
    bool    m_LastLight = false;
    int     m_LightFrom = 7 * 60;
    int     m_DarkFrom = 19 * 60;
    int     m_SunriseMin = -1;
    int     m_SunsetMin = -1;
    QDate   m_SunDate;
    // The hold: -1 none, 0 Dark, 1 Light. Under Auto it ends at m_ForcedUntil (ms since the
    // epoch); under System, when Windows changes scheme.
    int     m_Forced = -1;
    qint64  m_ForcedUntil = 0;
    bool    m_Hold = false;
    QTimer  m_Clock;
    bool   m_ReduceAnimations = false;
    bool   m_StartupAnimation = true;
    bool   m_HostPageCovers = false;

    // 1.0 until AppShell has a width to measure. Not persisted: it describes the window the
    // app happens to be in, not anything the user chose.
    qreal  m_UiScale = 1.0;
    // The fit to the window before the user's scale, as AppShell last wrote it.
    qreal  m_FitScale = 1.0;
    int    m_ScalePercent = 100;
};
