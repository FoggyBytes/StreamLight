#include "videooptions.h"

#include <QChar>
#include <QLatin1Char>
#include <QStringList>

#include <algorithm>

namespace
{
    // The four each picker offered before 5.5.0, unchanged. On a plain 1080p60 client the
    // strips still come out exactly as they did, which is the point: the display only ever
    // ADDS to this list.
    const int PRESET_FPS[] = { 30, 60, 90, 120 };

    struct ResPreset { int w; int h; const char* label; };
    const ResPreset PRESET_RES[] = {
        { 1280,  720, "720p"  },
        { 1920, 1080, "1080p" },
        { 2560, 1440, "1440p" },
        { 3840, 2160, "4K"    },
    };

    QList<QSize> s_NativeResolutions;
    QList<int>   s_NativeRefreshRates;
    int          s_DisplayCount = 0;

    // U+00D7, built rather than written: this file has no other reason to be non-ASCII.
    QString times() { return QString(QChar(0x00D7)); }

    QString dimensions(const QSize& size)
    {
        return QString::number(size.width()) + times() + QString::number(size.height());
    }

    bool isPreset(const QSize& size)
    {
        for (const ResPreset& p : PRESET_RES) {
            if (p.w == size.width() && p.h == size.height()) {
                return true;
            }
        }
        return false;
    }
}

void VideoOptions::setNativeDisplays(const QList<QSize>& resolutions, const QList<int>& refreshRates)
{
    s_NativeResolutions = resolutions;
    s_NativeRefreshRates = refreshRates;

    // The count the Settings rows use to pick between "This display" and "Your displays".
    // The longer of the two lists, because a display can contribute one and not the other.
    s_DisplayCount = qMax(resolutions.count(), refreshRates.count());
}

int VideoOptions::displayCount()
{
    return s_DisplayCount;
}

QList<int> VideoOptions::frameRates()
{
    QList<int> rates;
    for (int fps : PRESET_FPS) {
        rates.append(fps);
    }

    for (int fps : s_NativeRefreshRates) {
        // A rate of 0 is what getRefreshRate() returns for a display SDL could not read.
        if (fps > 0 && !rates.contains(fps)) {
            rates.append(fps);
        }
    }

    std::sort(rates.begin(), rates.end());
    return rates;
}

QList<QSize> VideoOptions::resolutions()
{
    QList<QSize> sizes;
    for (const ResPreset& p : PRESET_RES) {
        sizes.append(QSize(p.w, p.h));
    }

    for (const QSize& native : s_NativeResolutions) {
        if (native.isValid() && !sizes.contains(native)) {
            sizes.append(native);
        }
    }

    // By pixel count, so a native lands where its size says it belongs rather than at the
    // end: 2560x1600 between 1440p and 4K. Width breaks a tie, which is what separates an
    // ultrawide from the panel it shares a height with.
    std::sort(sizes.begin(), sizes.end(), [](const QSize& a, const QSize& b) {
        const qint64 areaA = qint64(a.width()) * a.height();
        const qint64 areaB = qint64(b.width()) * b.height();
        return areaA != areaB ? areaA < areaB : a.width() < b.width();
    });
    return sizes;
}

bool VideoOptions::isNativeFrameRate(int fps)
{
    return fps > 0 && s_NativeRefreshRates.contains(fps);
}

bool VideoOptions::isNativeResolution(const QSize& resolution)
{
    return s_NativeResolutions.contains(resolution);
}

QString VideoOptions::resolutionLabel(const QSize& resolution)
{
    for (const ResPreset& p : PRESET_RES) {
        if (p.w == resolution.width() && p.h == resolution.height()) {
            return QString::fromLatin1(p.label);
        }
    }

    if (isNativeResolution(resolution)) {
        // Only when nothing else offered shares the height — see the header.
        const QList<QSize> offered = resolutions();
        for (const QSize& other : offered) {
            if (other.height() == resolution.height() && other.width() != resolution.width()) {
                return dimensions(resolution);
            }
        }
        return QString::number(resolution.height()) + QLatin1Char('p');
    }

    return dimensions(resolution);
}

QString VideoOptions::frameRateLabel(int fps)
{
    return QString::number(fps);
}

QString VideoOptions::nativeResolutionHint()
{
    QStringList parts;
    for (const QSize& size : s_NativeResolutions) {
        if (!size.isValid()) {
            continue;
        }
        const QString text = dimensions(size);
        if (!parts.contains(text)) {
            parts.append(text);
        }
    }
    return parts.join(QStringLiteral(", "));
}

QString VideoOptions::nativeRefreshHint()
{
    QList<int> rates;
    for (int fps : s_NativeRefreshRates) {
        if (fps > 0 && !rates.contains(fps)) {
            rates.append(fps);
        }
    }
    if (rates.isEmpty()) {
        return QString();
    }
    std::sort(rates.begin(), rates.end());

    QStringList parts;
    for (int fps : rates) {
        parts.append(QString::number(fps));
    }
    // The unit once at the end: "60, 120 Hz". Repeating it per value is the widest the row
    // ever gets for the least it ever says.
    return parts.join(QStringLiteral(", ")) + QStringLiteral(" Hz");
}
