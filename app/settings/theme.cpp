#include "theme.h"

#include <QSettings>

namespace
{
    // "Ion" — the cyan, and the default since 5.0.0.
    //
    // It replaced StreamLight's own green (#00e676, still available as the "Signal" preset)
    // for one reason: that green was doing two jobs at once. It meant "you have the focus
    // here" AND it is within a shade of the green that means "this host is online" — and a
    // colour that says two things says neither. The cyan sits beside the online green without
    // being mistaken for it, which is exactly what an accent has to do, since the semantic
    // colours deliberately never follow it.
    const QColor DefaultAccent = QColor(0x00, 0xd3, 0xf2);

    const char* SER_ACCENT = "theme/accent";
    const char* SER_REDUCE = "theme/reduceanimations";

    /**
     * Perceived brightness, not the average of the channels: green reads far lighter than blue
     * at the same numeric value, and an average would put dark text on a blue accent nobody can
     * read against. Rec. 709 coefficients on linearised channels — the same maths the contrast
     * guidelines use.
     */
    qreal relativeLuminance(const QColor& c)
    {
        auto lin = [](qreal v) {
            return v <= 0.03928 ? v / 12.92 : qPow((v + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * lin(c.redF()) + 0.7152 * lin(c.greenF()) + 0.0722 * lin(c.blueF());
    }
}

Theme::Theme(QObject* parent)
    : QObject(parent)
{
    QSettings settings;
    m_Accent = QColor(settings.value(SER_ACCENT, DefaultAccent.name()).toString());
    if (!m_Accent.isValid()) {
        m_Accent = DefaultAccent;
    }
    m_ReduceAnimations = settings.value(SER_REDUCE, false).toBool();
}

Theme* Theme::get(QQmlEngine* qmlEngine)
{
    static Theme* s_Theme = nullptr;
    if (s_Theme == nullptr) {
        s_Theme = new Theme(qmlEngine);
    }
    return s_Theme;
}

QColor Theme::onAccent() const
{
    // Whichever of black or white the accent can carry. Amber and lime need dark text; a deep
    // blue or violet needs white. Deciding this from the colour rather than fixing it is what
    // stops a user-chosen accent from producing an unreadable button.
    return relativeLuminance(m_Accent) > 0.42 ? QColor(0x08, 0x12, 0x0c) : QColor(0xff, 0xff, 0xff);
}

QColor Theme::onColor(const QColor& background) const
{
    // Same threshold as onAccent, and deliberately the same function: two different answers
    // to "is this light or dark" in one interface is how a button ends up legible and the
    // label beside it doesn't.
    return relativeLuminance(background) > 0.42 ? QColor(0x08, 0x12, 0x0c) : QColor(0xff, 0xff, 0xff);
}

QColor Theme::blend(const QColor& under, const QColor& over) const
{
    const qreal a = over.alphaF();
    if (a <= 0.0) return under;
    if (a >= 1.0) return over;

    return QColor::fromRgbF(under.redF()   * (1 - a) + over.redF()   * a,
                            under.greenF() * (1 - a) + over.greenF() * a,
                            under.blueF()  * (1 - a) + over.blueF()  * a);
}

QColor Theme::accentSoft() const
{
    QColor c = m_Accent;
    c.setAlphaF(0.16f);   // float, not double: Qt 6 narrowed the QColor *F() API
    return c;
}

QColor Theme::ground() const
{
    // Near-black with a trace of the accent in it. This is the difference between an interface
    // that has been recoloured and one that looks designed around its colour: at 4% the hue is
    // never nameable, but the screen stops feeling like a neutral grey box.
    const QColor base(0x06, 0x08, 0x0a);
    const float k = 0.04f;
    return QColor::fromRgbF(base.redF()   * (1 - k) + m_Accent.redF()   * k,
                            base.greenF() * (1 - k) + m_Accent.greenF() * k,
                            base.blueF()  * (1 - k) + m_Accent.blueF()  * k);
}

void Theme::setAccent(const QColor& c)
{
    if (!c.isValid() || c == m_Accent) {
        return;
    }

    m_Accent = c;
    save();
    emit changed();
}

void Theme::setReduceAnimations(bool on)
{
    if (on == m_ReduceAnimations) {
        return;
    }

    m_ReduceAnimations = on;
    save();
    emit changed();
}

void Theme::resetAccent()
{
    setAccent(DefaultAccent);
}

void Theme::save() const
{
    // Written on every change rather than at teardown: a process that is killed never reaches
    // a teardown hook, which is exactly how the link-matching preference used to lose itself.
    QSettings settings;
    settings.setValue(SER_ACCENT, m_Accent.name());
    settings.setValue(SER_REDUCE, m_ReduceAnimations);
}
