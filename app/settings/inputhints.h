#pragma once

#include <QObject>
#include <QQmlEngine>

/**
 * Which input device the user is actually holding, so every on-screen prompt can name a
 * button they can press.
 *
 * Before this, every prompt drew a controller glyph unconditionally: starting StreamLight
 * with a keyboard and never touching a pad still showed "B Back" with an Xbox glyph. The
 * rule here is the one games use — the last device that produced real input wins, and the
 * whole interface follows it at once.
 *
 * ⚠️ The trap this class exists to avoid: SdlGamepadKeyNavigation *synthesises Qt key
 * events* for pad buttons (B becomes Key_Escape, the D-pad becomes arrows). A naive
 * keyboard filter counts those as typing and flips straight back on every pad press. Real
 * key events from the window system are spontaneous; posted ones are not, and that is the
 * only reliable difference — see the filter in the .cpp.
 */
class InputHints : public QObject
{
    Q_OBJECT

    /** True when the last real input came from a controller. */
    Q_PROPERTY(bool padActive READ padActive NOTIFY padActiveChanged)

public:
    static InputHints* get(QQmlEngine* engine = nullptr);

    bool padActive() const { return m_PadActive; }

    /**
     * A controller button or axis moved for real. Called from the SDL poll loop, never from
     * the synthetic-key path — that is the whole point of keeping the two apart.
     */
    void notePadInput();

    /**
     * Seeds the starting state before anyone has touched anything: a connected controller
     * means prompts start as controller prompts, which is what a handheld user expects to
     * see on the first frame rather than after nudging a stick.
     */
    void seedFromConnectedPads(bool anyConnected);

    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    void padActiveChanged();

private:
    explicit InputHints(QObject* parent = nullptr);

    void setPadActive(bool padActive);

    bool m_PadActive = false;
};
