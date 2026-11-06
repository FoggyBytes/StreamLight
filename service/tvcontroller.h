#pragma once

#include "tvstore.h"
#include "webostv.h"

#include <QElapsedTimer>

#include <atomic>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

// Everything the service does to the TV, on one worker thread: one operation at a time, and the
// newest request wins — a shutdown that arrives while the TV is still waking up cancels the wake and
// turns it off. Between requests the worker keeps a registered connection open while the TV is on,
// subscribed to the app in the foreground, so a turn-off at shutdown needs no setup inside the
// time Windows gives and knows at once whether the TV is showing this PC.
class TvController
{
public:
    explicit TvController(const TvRecord& tv);
    ~TvController();
    TvController(const TvController&) = delete;
    TvController& operator=(const TvController&) = delete;

    void turnOn(const QString& why);
    void turnOff(const QString& why, bool onlyIfShowingPc);

    // Waits until the TV has taken the command or budgetMs has gone by; the work itself goes on
    // after a timeout. For shutdown, where Windows will not wait.
    bool turnOffNow(const QString& why, bool onlyIfShowingPc, int budgetMs);

    // A new input from StreamLight, taken by the next operation; nothing running is interrupted.
    void setInput(int input) { m_Input = input; }

    // For StreamLight's status: whether a registered connection is open (= the TV is on), and the
    // app it showed last. Safe from any thread.
    bool connected() const { return m_Session.isOpen(); }
    QString showing() const
    {
        std::lock_guard<std::mutex> lock(m_ShowingLock);
        return m_Showing;
    }
    // 8.5.0: the TV at this MAC presented a certificate other than the one kept at pairing, so
    // nothing was sent to it. Cleared by a connection that matches; pairing again replaces it.
    bool certChanged() const { return m_CertChanged; }

private:
    enum class Action { On, Off };
    struct Job
    {
        Action action;
        QString why;
        bool onlyIfShowingPc = false;
        int budgetMs = 0;
        std::shared_ptr<std::promise<bool>> done;
    };

    void post(Job job);
    void run();
    bool doTurnOn(const Job& job);
    bool doTurnOff(const Job& job);
    void idle();

    // A registered session with the TV. False when it is off (in standby the TV accepts the
    // connection but closes it on register) or not on the network.
    bool ensureSession(int timeoutMs, bool allowSsdp);
    bool pressPower();
    // Sleeps up to ms, waking early when a newer request arrives or the controller stops.
    bool pause(int ms);
    bool cancelled() const { return m_Cancel || m_Stop; }

    TvRecord m_Tv;          // worker thread only, after construction
    std::atomic<int> m_Input;
    WebOsTv m_Session;
    QString m_Foreground;   // worker thread only: kept by the subscription
    mutable std::mutex m_ShowingLock;
    QString m_Showing;      // m_Foreground's copy for other threads
    QElapsedTimer m_LastCheck;

    std::thread m_Worker;
    std::mutex m_Lock;
    std::condition_variable m_Wake;
    std::optional<Job> m_Pending;
    std::atomic<bool> m_CertChanged { false };
    std::atomic<bool> m_Cancel { false };
    std::atomic<bool> m_Stop { false };
};
