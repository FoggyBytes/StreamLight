#include "windowchrome.h"

#include <QCoreApplication>
#include <QDebug>
#include <QTimer>

#ifdef Q_OS_WIN32
#include <windows.h>
#include <windowsx.h>

namespace {

// The frame Windows hangs past the window's visible edge, used both for the top resize
// border and for what a maximized window loses above the screen.
int frameThickness(HWND hwnd)
{
    const UINT dpi = GetDpiForWindow(hwnd);
    return GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
}

bool hasCaption(HWND hwnd)
{
    // Qt takes WS_CAPTION away in full screen; there the chrome steps aside.
    return (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CAPTION) == WS_CAPTION;
}

int buttonFor(WPARAM hit)
{
    switch (hit) {
    case HTMINBUTTON: return WindowChrome::Minimize;
    case HTMAXBUTTON: return WindowChrome::Maximize;
    case HTCLOSE:     return WindowChrome::Close;
    default:          return WindowChrome::None;
    }
}

void refreshFrame(HWND hwnd)
{
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                 SWP_NOOWNERZORDER | SWP_NOACTIVATE);
}

QRect toQRect(const RECT& r)
{
    return QRect(r.left, r.top, r.right - r.left, r.bottom - r.top);
}

// Within a couple of pixels: a maximized window's edges are Windows' arithmetic, not ours.
bool roughlyEqual(const QRect& a, const QRect& b)
{
    const int tol = 2;
    return qAbs(a.left() - b.left()) <= tol && qAbs(a.top() - b.top()) <= tol
        && qAbs(a.right() - b.right()) <= tol && qAbs(a.bottom() - b.bottom()) <= tol;
}

const char* visibilityName(int v)
{
    switch (v) {
    case QWindow::Windowed:   return "windowed";
    case QWindow::Maximized:  return "maximized";
    case QWindow::FullScreen: return "full screen";
    case QWindow::Minimized:  return "minimized";
    case QWindow::Hidden:     return "hidden";
    default:                  return "automatic";
    }
}

}
#endif

WindowChrome* WindowChrome::get(QQmlEngine*)
{
    static WindowChrome* instance = new WindowChrome();
    return instance;
}

WindowChrome::WindowChrome(QObject* parent)
    : QObject(parent)
{
}

void WindowChrome::attach(QWindow* window)
{
#ifdef Q_OS_WIN32
    if (window == nullptr || m_Window != nullptr) {
        return;
    }

    m_Window = window;
    QCoreApplication::instance()->installNativeEventFilter(this);

    // winId() creates the native window if it does not exist yet. The frame change makes
    // Windows ask for the client area again, and this time the answer comes from below.
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    m_Hwnd = hwnd;
    refreshFrame(hwnd);
    updateMetrics();
#else
    Q_UNUSED(window)
#endif
}

bool WindowChrome::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
#ifdef Q_OS_WIN32
    if (eventType != "windows_generic_MSG" || m_Window == nullptr) {
        return false;
    }

    // ⚠️ Two ways in. Sent messages (WM_NCCALCSIZE, WM_NCHITTEST…) come from the window procedure,
    // with a result to write. Posted ones — the mouse — come first from Qt's event loop with NO
    // result (nullptr); measured on 30/10/2026, a button press never reached the window procedure
    // at all. So the mouse is acted on wherever it shows up first and eaten there (true: not
    // dispatched), and a result is written only when there is one — writing through the null one
    // crashed on the first click.
    const auto eat = [result](LRESULT value) {
        if (result != nullptr) {
            *result = value;
        }
        return true;
    };

    const MSG* msg = static_cast<const MSG*>(message);
    HWND hwnd = msg->hwnd;

    if (hwnd != m_Hwnd) {
        // handle() first: during the creation of a new native window winId() would create
        // another one.
        if (m_Window->handle() == nullptr || hwnd != reinterpret_cast<HWND>(m_Window->winId())) {
            return false;
        }

        // The window was rebuilt (main.qml, after a stream on a gaming handheld), and the new
        // HWND was created with Windows' caption. Ask for its frame again.
        m_Hwnd = hwnd;
        QTimer::singleShot(0, this, [this, hwnd] {
            if (m_Hwnd == hwnd) {
                refreshFrame(hwnd);
            }
        });
    }

    switch (msg->message) {
    case WM_NCCALCSIZE: {
        if (!msg->wParam || !hasCaption(hwnd) || result == nullptr) {
            return false;
        }

        // Windows' own left, right and bottom borders — they stay, and so does resizing by
        // them — then the caption is given back to the client area.
        auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam);
        const LONG top = params->rgrc[0].top;
        *result = DefWindowProcW(hwnd, WM_NCCALCSIZE, msg->wParam, msg->lParam);
        params->rgrc[0].top = top;

        // A maximized window hangs its frame off the screen: without this its top would too.
        if (IsZoomed(hwnd)) {
            params->rgrc[0].top += frameThickness(hwnd);
        }
        return true;
    }

    case WM_NCHITTEST: {
        if (!hasCaption(hwnd) || result == nullptr) {
            return false;
        }

        // The side and bottom borders are still Windows' own.
        const LRESULT hit = DefWindowProcW(hwnd, WM_NCHITTEST, msg->wParam, msg->lParam);
        if (hit != HTCLIENT) {
            return eat(hit);
        }

        POINT pt { GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam) };
        ScreenToClient(hwnd, &pt);
        RECT client;
        GetClientRect(hwnd, &client);

        const double scale = GetDpiForWindow(hwnd) / 96.0;
        const int caption = qRound(k_CaptionHeight * scale);
        const int button = qRound(k_ButtonWidth * scale);

        if (pt.y < 0 || pt.y >= caption) {
            return false;
        }

        // The top border went with the caption: its first pixels resize the window, as they
        // did (not when maximized, where there is nothing to resize).
        const int border = frameThickness(hwnd);
        if (!IsZoomed(hwnd) && pt.y < border) {
            return eat(pt.x < border                  ? HTTOPLEFT
                     : pt.x >= client.right - border ? HTTOPRIGHT
                     :                                 HTTOP);
        }

        // HTMAXBUTTON on hover is what opens Windows 11's snap layouts.
        const int fromRight = client.right - pt.x;
        return eat(fromRight < button     ? HTCLOSE
                 : fromRight < 2 * button ? HTMAXBUTTON
                 : fromRight < 3 * button ? HTMINBUTTON
                 :                          HTCAPTION);
    }

    // Over the buttons the mouse is non-client, so QML never sees it: hover and press are
    // followed here. These messages never reach DefWindowProc over a button, or Windows would
    // track — and on Windows 10 draw — caption buttons of its own.
    case WM_NCMOUSEMOVE: {
        const int button = buttonFor(msg->wParam);
        setHovered(button);
        if (button == None) {
            return false;
        }
        if (!m_TrackingLeave) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE | TME_NONCLIENT, hwnd, 0 };
            m_TrackingLeave = TrackMouseEvent(&tme);
        }
        return eat(0);
    }

    case WM_NCMOUSELEAVE:
        m_TrackingLeave = false;
        setHovered(None);
        setPressed(None);
        return false;

    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK: {
        const int button = buttonFor(msg->wParam);
        if (button == None) {
            return false;
        }
        setPressed(button);
        return eat(0);
    }

    case WM_NCLBUTTONUP: {
        const int button = buttonFor(msg->wParam);
        const int pressed = m_Pressed;
        setPressed(None);
        if (button == None) {
            return false;
        }

        // A click is a press and a release on the same button, as with Windows' own.
        if (button == pressed) {
            const WPARAM command = button == Minimize ? SC_MINIMIZE
                                 : button == Close    ? SC_CLOSE
                                 : IsZoomed(hwnd)     ? SC_RESTORE
                                 :                      SC_MAXIMIZE;
            PostMessageW(hwnd, WM_SYSCOMMAND, command, 0);
        }
        return eat(0);
    }

    case WM_SIZE:
    case WM_STYLECHANGED:
    case WM_DPICHANGED:
        updateMetrics();
        return false;
    }
#else
    Q_UNUSED(eventType)
    Q_UNUSED(message)
    Q_UNUSED(result)
#endif

    return false;
}

/*
 * ⚠️ 8.6.1, issue #52: the window found shifted after a stream in the Xbox full screen
 * experience — the content pushed up (the top strip off the screen), a band of what is behind
 * at the bottom, the rounded corners and border of a window in the NORMAL state. Only on gaming
 * handhelds, and only since 8.3.0.
 *
 * What is certain, read in Qt 6.12's qwindowswindow.cpp: a native window is created at the
 * requested client geometry plus Windows' STANDARD frame (QWindowCreationContext →
 * AdjustWindowRectEx), caption included, because Qt cannot know that this filter will take the
 * caption away. On a gaming handheld main.qml rebuilds the native window after every full-screen
 * stream (SystemProperties::recreateNativeWindow, the grey-screen fix), so the new window's
 * normal rect is the screen grown by a caption above it — and that is the rect Qt keeps as the
 * one to go back to from full screen (m_savedFrameGeometry), which restoreAfterStream's
 * showNormal() then applies. With Windows' own caption that rect hid the caption off the top of
 * the screen and the client still covered it. With this filter the client starts where the
 * caption was: a caption's height above the screen. Windows also caps a window's height at the
 * screen plus its borders (WM_GETMINMAXINFO), so the bottom edge stops short — the band.
 * Normally the deferred showFullScreen() follows at once and none of it is ever seen.
 *
 * What is not certain, because nobody here has the device: why, sometimes, the window stays in
 * that state (the shell of the full screen experience reacting to the window passing through
 * the normal state is the likeliest reason; the reporter's Win+F11, which makes the shell lay
 * the windows out again, puts it right). So this does not try to predict it: it looks. A few
 * times after a stream it compares the window with the state it had before the stream and, if
 * they differ, puts it back — with Qt's own call when Qt's state is wrong, with SetWindowPos
 * when only the rect is. The looks are logged as [window], so a log from the device says which
 * case it was (the first look always, the others only when they put something back). On a
 * desktop where nothing went wrong it changes nothing.
 */
void WindowChrome::settle(int visibility, const QString& reason)
{
#ifdef Q_OS_WIN32
    if (m_Window == nullptr) {
        return;
    }
    if (visibility != QWindow::Windowed && visibility != QWindow::Maximized
        && visibility != QWindow::FullScreen) {
        return;
    }

    // Several looks, because whatever moves the window may do it late: the full screen
    // experience's shell, or refitWindowToScreen() 700 ms after a screen change.
    const int run = ++m_SettleRun;
    static const int k_Delays[] = { 400, 1200, 3000, 6000 };
    for (int pass = 0; pass < int(sizeof(k_Delays) / sizeof(k_Delays[0])); pass++) {
        QTimer::singleShot(k_Delays[pass], this, [this, run, visibility, pass, reason] {
            if (run == m_SettleRun) {
                checkSettled(visibility, pass, reason);
            }
        });
    }
#else
    Q_UNUSED(visibility)
    Q_UNUSED(reason)
#endif
}

void WindowChrome::checkSettled(int visibility, int pass, const QString& reason)
{
#ifdef Q_OS_WIN32
    if (m_Window == nullptr || m_Window->handle() == nullptr || !m_Window->isVisible()) {
        return;
    }
    HWND hwnd = reinterpret_cast<HWND>(m_Window->winId());
    if (IsIconic(hwnd)) {
        return;     // minimized by the user: nothing to put back
    }

    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
        return;
    }
    const QRect monitor = toQRect(mi.rcMonitor);
    const QRect work = toQRect(mi.rcWork);

    RECT wr, cr;
    GetWindowRect(hwnd, &wr);
    GetClientRect(hwnd, &cr);
    POINT origin = { 0, 0 };
    ClientToScreen(hwnd, &origin);
    const QRect windowRect = toQRect(wr);
    const QRect client(origin.x, origin.y, cr.right - cr.left, cr.bottom - cr.top);
    // Window around client, as they stand now: in the same state they stay the same.
    const QMargins frame(client.left() - windowRect.left(), client.top() - windowRect.top(),
                         windowRect.right() - client.right(), windowRect.bottom() - client.bottom());

    const bool zoomed = IsZoomed(hwnd) != FALSE;
    const bool caption = hasCaption(hwnd);
    const int now = m_Window->visibility();

    // What the state means on this screen, and whether the window is it.
    bool ok = false;
    QRect expected;
    switch (visibility) {
    case QWindow::FullScreen:
        expected = monitor;
        ok = now == QWindow::FullScreen && !caption && roughlyEqual(client, expected);
        break;
    case QWindow::Maximized:
        expected = work;
        ok = now == QWindow::Maximized && zoomed && roughlyEqual(client, expected);
        break;
    default: {  // Windowed: only that the top strip — drag, the three buttons — is on a screen
        // ANY screen: with two screens one above the other a window may rightly straddle them,
        // and the nearest one's work area would call its top "off the screen".
        POINT strip = { client.center().x(), client.top() + 4 };
        const bool stripOnScreen = MonitorFromPoint(strip, MONITOR_DEFAULTTONULL) != nullptr;
        // Moved down onto the nearest screen, never resized: the minimum size is not ours to fight.
        expected = client;
        if (!stripOnScreen && expected.top() < work.top()) {
            expected.moveTop(work.top());
        }
        // A window the user maximized or made full screen since is the user's.
        ok = now != QWindow::Windowed || zoomed || stripOnScreen || roughlyEqual(client, expected);
        break;
    }
    }

    if (ok) {
        if (pass == 0) {
            qInfo().noquote() << "[window]" << reason + QLatin1Char(':') << visibilityName(visibility)
                              << "as before — client" << client << "on" << monitor;
        }
        return;
    }

    qWarning().noquote() << "[window]" << reason << "(look" << pass + 1 << "): should be" << visibilityName(visibility)
               << "— Qt says" << visibilityName(now) << ", zoomed" << zoomed << ", caption" << caption
               << ", window" << windowRect << ", client" << client << ", expected" << expected
               << ", screen" << monitor << ", work area" << work << "— put back";

    switch (visibility) {
    case QWindow::FullScreen:
        if (now != QWindow::FullScreen) {
            m_Window->showFullScreen();
        }
        else if (caption) {
            // Qt believes it is full screen, the window has a caption again: leave and re-enter,
            // a tick apart, or Qt folds the two into one.
            m_Window->setWindowStates(Qt::WindowNoState);
            QTimer::singleShot(0, m_Window, [w = m_Window] {
                if (w) {
                    w->showFullScreen();
                }
            });
        }
        else {
            SetWindowPos(hwnd, HWND_TOP, monitor.left(), monitor.top(), monitor.width(), monitor.height(),
                         SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        }
        break;

    case QWindow::Maximized:
        if (now != QWindow::Maximized) {
            m_Window->showMaximized();
        }
        else if (!zoomed) {
            // Qt still thinks maximized, Windows does not: Qt follows WM_SIZE (SIZE_MAXIMIZED).
            ShowWindow(hwnd, SW_MAXIMIZE);
        }
        else {
            const QRect target = expected.marginsAdded(frame);
            SetWindowPos(hwnd, nullptr, target.left(), target.top(), target.width(), target.height(),
                         SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        }
        break;

    default: {
        const QRect target = expected.marginsAdded(frame);
        SetWindowPos(hwnd, nullptr, target.left(), target.top(), target.width(), target.height(),
                     SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        break;
    }
    }
#else
    Q_UNUSED(visibility)
    Q_UNUSED(pass)
    Q_UNUSED(reason)
#endif
}

// The same sizes the hit test uses, in QML units: what the buttons draw is exactly where
// Windows sends the clicks.
void WindowChrome::updateMetrics()
{
#ifdef Q_OS_WIN32
    HWND hwnd = static_cast<HWND>(m_Hwnd);
    if (hwnd == nullptr || m_Window == nullptr) {
        return;
    }

    const double scale = GetDpiForWindow(hwnd) / 96.0;
    const qreal dpr = m_Window->devicePixelRatio();
    const int inset = hasCaption(hwnd) ? qRound(qRound(k_CaptionHeight * scale) / dpr) : 0;
    const int buttonWidth = qRound(qRound(k_ButtonWidth * scale) / dpr);

    if (m_Inset != inset || m_ButtonWidth != buttonWidth) {
        m_Inset = inset;
        m_ButtonWidth = buttonWidth;
        emit insetChanged();
    }
#endif
}

void WindowChrome::setHovered(int button)
{
    if (m_Hovered != button) {
        m_Hovered = button;
        emit hoveredChanged();
    }
}

void WindowChrome::setPressed(int button)
{
    if (m_Pressed != button) {
        m_Pressed = button;
        emit pressedChanged();
    }
}
