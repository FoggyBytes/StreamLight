#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QQmlEngine>

/**
 * The user is dragging or resizing one of our windows right now (6.0.0).
 *
 * <p>Exists for one reader: the app's animated floor (AmbientWaves.qml), which must not draw
 * while a window moves. StreamLight runs Qt Quick on the "basic" render loop (main.cpp), so
 * every frame is drawn and presented on the GUI thread — and while a window is being dragged,
 * that thread is inside Windows' own modal move loop, where a frame waiting for the vblank
 * holds the move up. Reported by Marcello on 18/09/2026 as a drag that stuttered only with
 * the animation on.</p>
 *
 * <p>A QML-only version came first — pause on every change of the window's x/y, resume after
 * 300 ms of stillness — and was only half a cure: it cannot see the drag until the window has
 * already moved, and it resumes while the button is still held with the window standing
 * still. Windows says exactly when the move loop begins and ends (WM_ENTERSIZEMOVE and
 * WM_EXITSIZEMOVE, sent to the window being moved), and that is what this reads. On other
 * platforms `moving` simply stays false.</p>
 */
class WindowMove : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT

    Q_PROPERTY(bool moving READ moving NOTIFY movingChanged)

public:
    static WindowMove* get(QQmlEngine* engine = nullptr);

    bool moving() const { return m_Moving; }

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

signals:
    void movingChanged();

private:
    explicit WindowMove(QObject* parent = nullptr);

    void setMoving(bool moving);

    bool m_Moving = false;
};
