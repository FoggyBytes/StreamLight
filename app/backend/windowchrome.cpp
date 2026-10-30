#include "windowchrome.h"

#include <QCoreApplication>
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
