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
    QColor text3()    const { return QColor(0x66, 0x76, 0x7e); }

    QColor online()  const { return QColor(0x4a, 0xde, 0x80); }
    QColor warning() const { return QColor(0xf5, 0xa6, 0x23); }
    QColor danger()  const { return QColor(0xf8, 0x71, 0x71); }
    QColor offline() const { return QColor(0x5b, 0x6c, 0x75); }

    QString family()     const { return QStringLiteral("DM Sans"); }
    QString monoFamily() const { return QStringLiteral("JetBrains Mono"); }

    bool reduceAnimations() const { return m_ReduceAnimations; }

    void setAccent(const QColor& c);
    void setReduceAnimations(bool on);

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

private:
    void save() const;

    QColor m_Accent;
    bool   m_ReduceAnimations = false;
};
