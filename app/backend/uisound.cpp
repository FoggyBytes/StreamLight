// XAudio2 2.9 ships with Windows 10, which is also StreamLight's floor; its header refuses to
// compile below Windows 8, and nothing on the command line sets the target, so it is set here,
// before the first Windows header.
#ifdef _WIN32
#  ifdef _WIN32_WINNT
#    undef _WIN32_WINNT
#  endif
#  define _WIN32_WINNT 0x0A00
#  include <windows.h>
#  include <xaudio2.h>
#  pragma comment(lib, "xaudio2.lib")
#endif

#include "uisound.h"

#include <QCoreApplication>
#include <QFile>
#include <QJSValue>
#include <QKeyEvent>
#include <QMetaMethod>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QTimer>
#include <QtEndian>

#include <atomic>

namespace
{
    const char* SER_ENABLED = "ui/navigationsounds";
    const char* SER_VOLUME = "ui/navigationsoundsvolume";

    // The fastest each sound may repeat. A held D-pad steps every ~100 ms and keyboard
    // auto-repeat about every 33: Move keeps up with the first and thins out the second.
    constexpr qint64 MinGapMs[3] = { 45, 90, 90 };

    // Voices per sound: Move overlaps itself when held, the other two hardly ever do.
    constexpr int VoiceCount[3] = { 4, 2, 2 };

    const char* const ClipPath[3] = {
        ":/sounds/move.wav", ":/sounds/confirm.wav", ":/sounds/back.wav"
    };

    // How far up from what took a click soundForClick() looks for the button it belongs to:
    // the MouseArea inside a prompt, the background inside a Button.
    constexpr int ClickDepth = 4;

    // Buttons that leave, close or decline, by their label. The dialogs' Cancel and Close and
    // UpdatePromptDialog's No all go back where the user came from, as B does.
    bool isBackLabel(const QString& text)
    {
        static const QStringList words = {
            QStringLiteral("Back"), QStringLiteral("Cancel"), QStringLiteral("Close"),
            QStringLiteral("Dismiss"), QStringLiteral("Not now"), QStringLiteral("No"),
            QStringLiteral("Exit")
        };
        return words.contains(text.trimmed(), Qt::CaseInsensitive);
    }

    // QObject::isSignalConnected() is protected. QML handlers (onClicked…) count as
    // connections there, which is the whole point: a MouseArea that only swallows clicks so
    // they do not fall through to what is behind it does nothing, and must stay silent.
    struct SignalPeek : public QObject
    {
        using QObject::isSignalConnected;
    };

    bool mouseAreaActs(QObject* area)
    {
        const QMetaObject* mo = area->metaObject();
        for (int i = 0; i < mo->methodCount(); i++) {
            const QMetaMethod m = mo->method(i);
            if (m.methodType() != QMetaMethod::Signal) continue;
            const QByteArray name = m.name();
            if ((name == "clicked" || name == "pressed" || name == "released" ||
                 name == "doubleClicked") &&
                    static_cast<SignalPeek*>(area)->isSignalConnected(m)) {
                return true;
            }
        }
        return false;
    }

    // A var property holding a JS object reads back as a QJSValue, a map as a QVariantMap.
    QString stringMember(const QVariant& v, const char* name)
    {
        if (v.metaType() == QMetaType::fromType<QJSValue>()) {
            const QJSValue js = v.value<QJSValue>();
            return js.isObject() ? js.property(QString::fromLatin1(name)).toString() : QString();
        }
        if (v.metaType() == QMetaType::fromType<QVariantMap>()) {
            return v.toMap().value(QString::fromLatin1(name)).toString();
        }
        return QString();
    }
}

#ifdef _WIN32
namespace
{
    // A device unplugged or an endpoint gone: the engine is dead and is rebuilt on the next
    // sound. The callback arrives on XAudio2's own thread, so it only raises a flag.
    struct EngineCallback : public IXAudio2EngineCallback
    {
        std::atomic<bool> broken { false };
        void STDMETHODCALLTYPE OnProcessingPassStart() override {}
        void STDMETHODCALLTYPE OnProcessingPassEnd() override {}
        void STDMETHODCALLTYPE OnCriticalError(HRESULT) override { broken = true; }
    };

    struct Clip
    {
        WAVEFORMATEX                fmt {};
        QByteArray                  pcm;      // kept alive: XAudio2 reads it while it plays
        QList<IXAudio2SourceVoice*> voices;
        int                         next = 0;
        bool                        valid = false;
    };

    // RIFF/WAVE, PCM only — the files are ours and are written by gen.mjs.
    bool loadClip(const char* path, Clip& clip)
    {
        QFile f(QString::fromLatin1(path));
        if (!f.open(QIODevice::ReadOnly)) return false;
        const QByteArray d = f.readAll();
        if (d.size() < 12 || !d.startsWith("RIFF") || d.mid(8, 4) != "WAVE") return false;

        bool haveFmt = false;
        int pos = 12;
        while (pos + 8 <= d.size()) {
            const QByteArray id = d.mid(pos, 4);
            const quint32 len = qFromLittleEndian<quint32>(d.constData() + pos + 4);
            const int body = pos + 8;
            if (body + qint64(len) > d.size()) break;
            if (id == "fmt " && len >= 16) {
                memcpy(&clip.fmt, d.constData() + body, 16);
                clip.fmt.cbSize = 0;
                haveFmt = clip.fmt.wFormatTag == WAVE_FORMAT_PCM;
            }
            else if (id == "data") {
                clip.pcm = d.mid(body, int(len));
            }
            pos = body + int(len) + (len & 1);
        }
        clip.valid = haveFmt && !clip.pcm.isEmpty();
        return clip.valid;
    }
}

struct UiSound::Impl
{
    IXAudio2*               engine = nullptr;
    IXAudio2MasteringVoice* master = nullptr;
    EngineCallback          callback;
    Clip                    clips[3];
    bool                    loaded = false;
};
#else
struct UiSound::Impl {};
#endif

UiSound* UiSound::get(QQmlEngine*)
{
    static UiSound* instance = new UiSound();
    // Created before the QML engine (main.cpp installs its filter early), and kept by C++:
    // SdlGamepadKeyNavigation calls it directly, and must never find it collected.
    QQmlEngine::setObjectOwnership(instance, QQmlEngine::CppOwnership);
    return instance;
}

UiSound::UiSound(QObject* parent)
    : QObject(parent),
      m_Impl(new Impl)
{
    QSettings settings;
    m_Enabled = settings.value(SER_ENABLED, true).toBool();
    m_Volume = qBound(0, settings.value(SER_VOLUME, 100).toInt(), 100);
    m_Clock.start();

    if (QCoreApplication::instance() != nullptr) {
        QCoreApplication::instance()->installEventFilter(this);
    }

    // The output is opened now, while the app starts, so that it is already awake when the
    // first sound comes — see the header.
    QTimer::singleShot(0, this, [this]() {
        if (m_Enabled && !m_Streaming) ensureEngine();
    });
}

UiSound::~UiSound()
{
    teardown();
    delete m_Impl;
}

void UiSound::setEnabled(bool on)
{
    if (on == m_Enabled) return;
    m_Enabled = on;
    QSettings().setValue(SER_ENABLED, m_Enabled);
    // Off lets the device go; On takes it again at once, for the same reason as at startup.
    if (!m_Enabled) teardown();
    else if (!m_Streaming) ensureEngine();
    emit enabledChanged();
}

void UiSound::setVolume(int volume)
{
    volume = qBound(0, volume, 100);
    if (volume == m_Volume) return;
    m_Volume = volume;
    QSettings().setValue(SER_VOLUME, m_Volume);
    applyVolume();
    emit volumeChanged();
}

void UiSound::applyVolume()
{
#ifdef _WIN32
    // Squared, so the slider moves evenly in loudness: linear amplitude would put half the
    // track within the top 6 dB, where the ear hears little difference.
    const float level = float(m_Volume) / 100.0f;
    if (m_Impl->master) m_Impl->master->SetVolume(level * level);
#endif
}

void UiSound::setStreaming(bool streaming)
{
    if (streaming == m_Streaming) return;
    m_Streaming = streaming;
    // The stream's audio gets the device to itself; the interface takes it back as soon as the
    // stream is over, so the first step after it is heard.
    if (m_Streaming) teardown();
    else if (m_Enabled) ensureEngine();
}

void UiSound::play(int kind)
{
    if (kind < Move || kind > Back) return;
    // The dedupe described in the header: a QML request that follows the key that caused it.
    if (m_Clock.elapsed() - m_LastMs[kind] < 200) return;
    playNow(kind);
}

int UiSound::kindForKey(int key, Qt::KeyboardModifiers modifiers)
{
    // Ctrl/Alt combinations are shortcuts, not navigation.
    if (modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) return -1;

    switch (key) {
    case Qt::Key_Up: case Qt::Key_Down: case Qt::Key_Left: case Qt::Key_Right:
    case Qt::Key_Tab: case Qt::Key_Backtab:
    case Qt::Key_PageUp: case Qt::Key_PageDown:
    case Qt::Key_F14: case Qt::Key_F15:   // LT / RT: the profile
    case Qt::Key_F16: case Qt::Key_F17:   // LB / RB: the tab, or the host on Home
        return Move;
    case Qt::Key_Return: case Qt::Key_Enter: case Qt::Key_Space:
    case Qt::Key_Menu:     // X
    case Qt::Key_Hangup:   // Y: Settings
    case Qt::Key_F13:      // Select: per-game settings
    case Qt::Key_F18:      // Start: pin
    case Qt::Key_F19:      // right stick: move to the other tab, theme on Home
        return Confirm;
    case Qt::Key_Escape: case Qt::Key_Back:
        return Back;
    default:
        return -1;
    }
}

/*
 * The sound of a click, from what took the press. Asked on the release, before it is
 * delivered: the grabber is still the item the press went to.
 *
 * Silent unless the click lands on something that acts — a Button (any AbstractButton), a
 * MouseArea with a handler, a TapHandler — and is released over it. Sliders, text fields, a
 * list being dragged and the dimmed area around a popup take presses too, and stay silent.
 *
 * Back for the buttons that leave: a `uiSound` property says so explicitly ("back", "confirm",
 * "none"); otherwise a status-bar prompt whose kind is "back", or a label such as Cancel or
 * Close (isBackLabel). Everything else that acts is Confirm.
 */
int UiSound::soundForClick(QObject* grabber, const QPointF& scenePos)
{
    if (grabber == nullptr) return -1;
    QQuickItem* item = qobject_cast<QQuickItem*>(grabber);
    const bool handler = item == nullptr;
    if (handler) item = grabber->property("parent").value<QQuickItem*>();   // a pointer handler
    if (item == nullptr) return -1;

    // Dragged off before letting go: the button was let off, nothing happens.
    if (!item->contains(item->mapFromScene(scenePos))) return -1;

    const bool acts = handler ? grabber->inherits("QQuickTapHandler")
                    : item->inherits("QQuickAbstractButton")
                      || (item->inherits("QQuickMouseArea") && mouseAreaActs(item));
    if (!acts) return -1;

    QQuickItem* it = item;
    for (int depth = 0; it != nullptr && depth < ClickDepth; depth++, it = it->parentItem()) {
        const QVariant explicitSound = it->property("uiSound");
        if (explicitSound.isValid()) {
            const QString s = explicitSound.toString();
            return s == QLatin1String("back") ? Back : s == QLatin1String("none") ? -1 : Confirm;
        }
        const QString kind = stringMember(it->property("model"), "kind");
        if (!kind.isEmpty()) {
            return kind == QLatin1String("back") || kind == QLatin1String("cancel") ? Back : Confirm;
        }
        const QVariant text = it->property("text");
        if (text.metaType() == QMetaType::fromType<QString>() && !text.toString().isEmpty()) {
            return isBackLabel(text.toString()) ? Back : Confirm;
        }
    }
    return Confirm;
}

bool UiSound::focusIsText(QWindow* window)
{
    auto* quick = qobject_cast<QQuickWindow*>(window);
    QQuickItem* item = quick ? quick->activeFocusItem() : nullptr;
    return item && (item->inherits("QQuickTextInput") || item->inherits("QQuickTextEdit"));
}

void UiSound::noteKey(QWindow* window, int key, Qt::KeyboardModifiers modifiers, bool autoRepeat,
                      bool accepted, bool wasText)
{
    if (!m_Enabled || !accepted || wasText) return;

    // Without a focused item nobody could have taken the key — and a key event starts out
    // accepted, so `accepted` would claim otherwise.
    auto* quick = qobject_cast<QQuickWindow*>(window);
    if (quick == nullptr || !quick->isVisible() || quick->activeFocusItem() == nullptr) return;

    const int kind = kindForKey(key, modifiers);
    if (kind < 0) return;
    // Held: only a move repeats. A held A does not confirm ten times.
    if (autoRepeat && kind != Move) return;
    playNow(kind);
}

bool UiSound::eventFilter(QObject* watched, QEvent* event)
{
    // Real input only — pad presses come in through noteKey() with a real answer.
    // Watched == the window: the delivery to the window, not each item it is passed on to.
    if (!m_Enabled || !event->spontaneous()) return QObject::eventFilter(watched, event);

    if (event->type() == QEvent::KeyPress) {
        if (auto* window = qobject_cast<QQuickWindow*>(watched)) {
            const auto* ke = static_cast<QKeyEvent*>(event);
            if (!focusIsText(window)) {
                noteKey(window, ke->key(), ke->modifiers(), ke->isAutoRepeat(), true, false);
            }
        }
    }
    else if (event->type() == QEvent::MouseButtonRelease) {
        if (qobject_cast<QQuickWindow*>(watched) != nullptr) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton && me->pointCount() > 0) {
                const QEventPoint& point = me->point(0);
                const int kind = soundForClick(me->exclusiveGrabber(point), point.scenePosition());
                if (kind >= 0) playNow(kind);
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

void UiSound::playNow(int kind)
{
    if (m_Streaming || m_Volume == 0) return;
    const qint64 now = m_Clock.elapsed();
    if (now - m_LastMs[kind] < MinGapMs[kind]) return;
    m_LastMs[kind] = now;

#ifdef _WIN32
    if (!ensureEngine()) return;
    Clip& clip = m_Impl->clips[kind];
    if (!clip.valid || clip.voices.isEmpty()) return;

    IXAudio2SourceVoice* voice = clip.voices[clip.next];
    clip.next = (clip.next + 1) % clip.voices.size();

    // The oldest voice of this sound is reused: stopped, emptied, given the clip again.
    voice->Stop(0);
    voice->FlushSourceBuffers();
    XAUDIO2_BUFFER buffer {};
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.AudioBytes = UINT32(clip.pcm.size());
    buffer.pAudioData = reinterpret_cast<const BYTE*>(clip.pcm.constData());
    if (SUCCEEDED(voice->SubmitSourceBuffer(&buffer))) {
        voice->Start(0);
    }
#endif
}

bool UiSound::ensureEngine()
{
#ifdef _WIN32
    Impl* d = m_Impl;
    if (d->callback.broken) teardown();

    if (!d->loaded) {
        for (int k = 0; k < 3; k++) {
            if (!loadClip(ClipPath[k], d->clips[k])) {
                qWarning() << "UiSound: could not read" << ClipPath[k];
            }
        }
        d->loaded = true;
    }

    if (d->engine == nullptr) {
        if (FAILED(XAudio2Create(&d->engine, 0, XAUDIO2_DEFAULT_PROCESSOR))) {
            d->engine = nullptr;
            return false;
        }
        d->engine->RegisterForCallbacks(&d->callback);
        if (FAILED(d->engine->CreateMasteringVoice(&d->master))) {
            teardown();
            return false;
        }
        applyVolume();
        for (int k = 0; k < 3; k++) {
            Clip& clip = d->clips[k];
            if (!clip.valid) continue;
            for (int v = 0; v < VoiceCount[k]; v++) {
                IXAudio2SourceVoice* voice = nullptr;
                if (SUCCEEDED(d->engine->CreateSourceVoice(&voice, &clip.fmt))) {
                    clip.voices.append(voice);
                }
            }
            clip.next = 0;
        }
    }
    return true;
#else
    return false;
#endif
}

void UiSound::teardown()
{
#ifdef _WIN32
    Impl* d = m_Impl;
    for (Clip& clip : d->clips) {
        for (IXAudio2SourceVoice* voice : clip.voices) voice->DestroyVoice();
        clip.voices.clear();
    }
    if (d->master) { d->master->DestroyVoice(); d->master = nullptr; }
    if (d->engine) { d->engine->Release(); d->engine = nullptr; }
    d->callback.broken = false;
#endif
}
