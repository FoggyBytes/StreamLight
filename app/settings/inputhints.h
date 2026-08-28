#pragma once

#include <QObject>
#include <QPoint>
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

    /**
     * True while the mouse pointer is hidden because the pad is driving.
     *
     * ⚠️ Deliberately NOT the same predicate as padActive, and the difference is the reason
     * this is a second property rather than a binding on the first. `padActive` flips back on
     * a key press or a mouse CLICK and pointedly ignores mouse movement, because a mouse
     * knocked on a desk must not repaint every prompt in the interface. A hidden pointer
     * cannot afford that rule: if only a click brought it back, the way to find an invisible
     * cursor would be to click blindly with it. So movement counts here and nowhere else.
     */
    Q_PROPERTY(bool pointerHidden READ pointerHidden NOTIFY pointerHiddenChanged)

public:
    static InputHints* get(QQmlEngine* engine = nullptr);

    bool padActive() const { return m_PadActive; }
    bool pointerHidden() const { return m_PointerHidden; }

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
    void pointerHiddenChanged();

private:
    explicit InputHints(QObject* parent = nullptr);

    void setPadActive(bool padActive);
    void setPointerHidden(bool hidden);

    bool m_PadActive = false;
    bool m_PointerHidden = false;

    // Where the pointer was when it was hidden. Qt Quick manufactures hover events whenever
    // items move under a stationary cursor — a list scrolling under the pad would otherwise
    // count as "the user moved the mouse" and bring the pointer straight back.
    QPoint m_HiddenAt;
};
