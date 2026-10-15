#pragma once

#include <QObject>
#include <QUrl>
#include <QVariant>

#include <functional>

class ComputerManager;
class NvComputer;
class Session;
class StreamingPreferences;

namespace CliStartStream
{

class Event;
class LauncherPrivate;

class Launcher : public QObject
{
    Q_OBJECT
    Q_DECLARE_PRIVATE_D(m_DPtr, Launcher)

public:
    // `preferences` is the global object, read and never written. Each session gets a clone
    // with the host's active profile and the game's overrides applied, as in the GUI, and then
    // `commandLineOptions` on top: what was typed on the command line always wins.
    explicit Launcher(QString computer, QString app,
                      StreamingPreferences* preferences,
                      std::function<void(StreamingPreferences*)> commandLineOptions,
                      QObject *parent = nullptr);
    ~Launcher();
    Q_INVOKABLE void execute(ComputerManager *manager);
    Q_INVOKABLE void quitRunningApp();
    Q_INVOKABLE bool isExecuted() const;

signals:
    void searchingComputer();
    void searchingApp();
    // boxArt: the cover already on disk for this app, or empty — the launch curtain draws it
    // and its blur as the background, as from the host page. isResume: the host was already
    // running this app, so the curtain says Resuming.
    void sessionCreated(QString appName, Session *session, QUrl boxArt, bool isResume);
    void failed(QString text);
    void appQuitRequired(QString appName);

private slots:
    void onComputerFound(NvComputer *computer);
    void onComputerUpdated(NvComputer *computer);
    void onTimeout();
    void onQuitAppCompleted(QVariant error);

private:
    QScopedPointer<LauncherPrivate> m_DPtr;
};

}
