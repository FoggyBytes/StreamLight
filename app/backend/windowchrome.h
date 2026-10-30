#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QWindow>

/**
 * The main window without Windows' title bar (8.3.0, issue #48).
 *
 * <p>The classic Win32 caption made a band of its own over the app — lower than the WinUI apps
 * around it once maximized (23 px at 125 %, against their 40), and in Windows' colours rather
 * than the app's theme. Qt 6.9's own answer, Qt::ExpandedClientAreaHint, was tried first and
 * dropped (30/10/2026): its bar still drew a strip of its own, and its hit test answers HTCLIENT
 * while the mouse merely hovers, so Windows 11's snap layouts never open on the maximize
 * button.</p>
 *
 * <p>This does what WinUI apps do. WM_NCCALCSIZE gives the caption to the client area and
 * keeps the resize borders, so the app's floor runs up to the window's top edge. The top
 * strip — 32 DIP, as in WinUI — is still the title bar as far as Windows knows: WM_NCHITTEST
 * answers HTCAPTION there (drag, double click, window menu) and HTMINBUTTON / HTMAXBUTTON /
 * HTCLOSE over the three buttons, which is what Windows 11 needs for snap layouts. The buttons
 * themselves are QML (CaptionButtons.qml); since the mouse over them is non-client, the hover
 * and the press are followed here from the WM_NC* messages and handed to QML.</p>
 *
 * <p>Only the window passed to attach(). In full screen the window has no WS_CAPTION and this
 * steps aside (inset 0). On other platforms nothing is attached and inset stays 0.</p>
 */
class WindowChrome : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT

    // The strip at the top and each button's width, in QML units: 32 and 46 DIP, as in WinUI,
    // through the window's device pixel ratio (1 in this app, which scales itself — so at 125 %
    // the strip is 40 units). inset is 0 without the chrome or in full screen.
    Q_PROPERTY(int inset READ inset NOTIFY insetChanged)
    Q_PROPERTY(int buttonWidth READ buttonWidth NOTIFY insetChanged)
    // The button under the mouse, and the one held down: 0 none, 1 minimize, 2 maximize, 3 close.
    Q_PROPERTY(int hovered READ hovered NOTIFY hoveredChanged)
    Q_PROPERTY(int pressed READ pressed NOTIFY pressedChanged)

public:
    enum Button { None = 0, Minimize = 1, Maximize = 2, Close = 3 };
    Q_ENUM(Button)

    static WindowChrome* get(QQmlEngine* engine = nullptr);

    Q_INVOKABLE void attach(QWindow* window);

    int inset() const { return m_Inset; }
    int buttonWidth() const { return m_ButtonWidth; }
    int hovered() const { return m_Hovered; }
    int pressed() const { return m_Pressed; }

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

signals:
    void insetChanged();
    void hoveredChanged();
    void pressedChanged();

private:
    explicit WindowChrome(QObject* parent = nullptr);

    static constexpr int k_CaptionHeight = 32;
    static constexpr int k_ButtonWidth = 46;

    void updateMetrics();
    void setHovered(int button);
    void setPressed(int button);

    QPointer<QWindow> m_Window;
    void* m_Hwnd = nullptr;
    bool m_TrackingLeave = false;
    int m_Inset = 0;
    int m_ButtonWidth = k_ButtonWidth;
    int m_Hovered = None;
    int m_Pressed = None;
};
