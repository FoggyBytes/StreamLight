#pragma once

#include <QByteArray>
#include <QString>

#include <windows.h>

namespace up {

/**
 * How StreamLight's update screen looked when it handed over (look.json, written by
 * AppUpdate::writeLook): where its window was, its colours, where each element sat, and how
 * far the waves had travelled. Physical pixels; positions relative to the window's top-left.
 *
 * ⚠️ It comes from a file the user can write, and this process is elevated: it is only ever
 * numbers and colours, each one checked, and the most a bad one can do is a strange picture.
 * No image, font or text is ever taken from it — those are embedded in this program.
 */
struct Look {
    RECT rect = {};
    double dpr = 1.0;
    double u = 1.0;             // Theme.uiScale
    double waveTime = 0;        // AmbientWaves._t, ms
    bool waves = true;
    // ARGB
    quint32 top = 0xff151515, mid = 0xff151515, foot = 0xff151515;
    quint32 ink = 0xff00d3f2, text = 0xfff3f7f8, text2 = 0xffa2b2ba, text3 = 0xff758790, line = 0x14ffffff;
    double icon[4] = {};        // x, y, w, h
    double wordPx = 0, wordBaseline = 0;
    double wordX[11] = {};
    double statusPx = 0, statusX = 0, statusBaseline = 0;
    double percentPx = 0, percentRight = 0, percentBaseline = 0;
    double bar[4] = {};
    double versionPx = 0, versionCx = 0, versionBaseline = 0;
};

/** look.json, validated; false (and `out` untouched) when anything in it is off. */
bool parseLook(const QByteArray& json, Look& out);
/** The same screen laid out from scratch, on the primary monitor, in the dark theme. */
Look defaultLook();

enum class ScreenPhase { Closing, Installing, Starting, Failed };

/**
 * The full-screen window shown while StreamLight is closed: the last frame of StreamLight's own
 * update screen, redrawn here, with Setup's progress on its bar. It runs on a thread of its own
 * so that Setup's progress (posted straight to it, k_MsgSetupProgress) is drawn while the main
 * thread waits on Setup. Topmost: it goes up over StreamLight before StreamLight closes, so
 * the desktop is never seen in between.
 */
class Screen
{
public:
    bool start(const Look& look, const QString& fromVersion, const QString& toVersion);
    void setPhase(ScreenPhase phase);
    /** Fades out over 0.3 s, then closes. Returns once the window is gone. */
    void close();
    bool shown() const { return m_Hwnd != nullptr; }
    ~Screen();

private:
    HANDLE m_Thread = nullptr;
    HWND m_Hwnd = nullptr;
};

} // namespace up
