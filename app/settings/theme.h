#pragma once

#include <QColor>
#include <QObject>
#include <QQmlEngine>

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

    /** Black or white, whichever the accent can actually carry as a foreground. */
    Q_PROPERTY(QColor onAccent READ onAccent NOTIFY changed)

    /** The accent at low alpha, for tinted fills and washes. */
    Q_PROPERTY(QColor accentSoft READ accentSoft NOTIFY changed)

    /** Page background: near-black carrying a trace of the accent, never pure black. */
    Q_PROPERTY(QColor ground READ ground NOTIFY changed)

    // Structure. Fixed, cool neutrals — deliberately NOT derived from the accent, or every
    // surface in the app would shift hue together and the accent would stop reading as an accent.
    Q_PROPERTY(QColor card     READ card     CONSTANT)
    Q_PROPERTY(QColor cardHigh READ cardHigh CONSTANT)
    Q_PROPERTY(QColor line     READ line     CONSTANT)
    Q_PROPERTY(QColor lineHigh READ lineHigh CONSTANT)
    Q_PROPERTY(QColor text     READ text     CONSTANT)
    Q_PROPERTY(QColor text2    READ text2    CONSTANT)
    Q_PROPERTY(QColor text3    READ text3    CONSTANT)

    // Semantics. Fixed on purpose — see the class note.
    Q_PROPERTY(QColor online  READ online  CONSTANT)
    Q_PROPERTY(QColor warning READ warning CONSTANT)
    Q_PROPERTY(QColor danger  READ danger  CONSTANT)
    Q_PROPERTY(QColor offline READ offline CONSTANT)

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
     * scale — <tt>_px(Theme.fontBody)</tt>, not <tt>Theme.fontBody</tt> — except the window
     * chrome, which is sized in fixed pixels on purpose. Handing out pre-scaled values here
     * would take that choice away from the call site.</p>
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

public:
    static Theme* get(QQmlEngine* qmlEngine = nullptr);

    explicit Theme(QObject* parent = nullptr);

    QColor accent()     const { return m_Accent; }
    QColor onAccent()   const;
    QColor accentSoft() const;
    QColor ground()     const;

    QColor card()     const { return QColor(0x0f, 0x15, 0x19); }
    QColor cardHigh() const { return QColor(0x16, 0x1f, 0x25); }
    QColor line()     const { return QColor(0xff, 0xff, 0xff, 0x14); }
    QColor lineHigh() const { return QColor(0xff, 0xff, 0xff, 0x30); }
    QColor text()     const { return QColor(0xf3, 0xf7, 0xf8); }
    QColor text2()    const { return QColor(0xa2, 0xb2, 0xba); }

    /**
     * Tertiary text — captions, section headers, the muted half of a two-tone line.
     *
     * ⚠️ It was #66767e, which measures 4.26:1 on the page and 3.90:1 on a card. WCAG AA wants
     * 4.5:1 for text this size, and this token's whole job is small text: the 11 px
     * "PLAYED"/"SESSIONS" captions, the uppercase section headers in Settings. #758790 is the
     * same hue lightened 15%, measuring 5.37:1 and 4.93:1, and it stays clearly below text2
     * (9.18:1) so the three-step hierarchy is intact. Do not darken it back without measuring.
     */
    QColor text3()    const { return QColor(0x75, 0x87, 0x90); }

    QColor online()  const { return QColor(0x4a, 0xde, 0x80); }
    QColor warning() const { return QColor(0xf5, 0xa6, 0x23); }
    QColor danger()  const { return QColor(0xf8, 0x71, 0x71); }
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
    QColor offline() const { return QColor(0x5b, 0x6c, 0x75); }

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

    qreal uiScale() const { return m_UiScale; }

    void setAccent(const QColor& c);
    void setReduceAnimations(bool on);
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

    /**
     * Two colours composited, so a caller can ask about what is actually on screen rather
     * than about one of the layers. The stage draws its scrim over its backdrop, and it is
     * the result of the two that the text has to be read against.
     */
    Q_INVOKABLE QColor blend(const QColor& under, const QColor& over) const;

    /** Back to the default accent — see DefaultAccent in theme.cpp for which, and why. */
    Q_INVOKABLE void resetAccent();

signals:
    void changed();
    void uiScaleChanged();

private:
    void save() const;

    QColor m_Accent;
    bool   m_ReduceAnimations = false;

    // 1.0 until AppShell has a width to measure. Not persisted: it describes the window the
    // app happens to be in, not anything the user chose.
    qreal  m_UiScale = 1.0;
};
