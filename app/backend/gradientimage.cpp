#include "gradientimage.h"

#include <QColor>
#include <QStringList>
#include <QtGlobal>

namespace {

// Ordered 8x8 Bayer matrix. Ordered and not random on purpose: a regular sub-threshold
// pattern averages out to the exact value the eye is looking for, while random noise of the
// same amplitude reads as grain — the eye finds clumps in randomness that are not there in a
// lattice. This is also why the tile is small: at ±half a level it must not become a texture
// of its own.
const int kBayer8[64] = {
     0, 32,  8, 40,  2, 34, 10, 42,
    48, 16, 56, 24, 50, 18, 58, 26,
    12, 44,  4, 36, 14, 46,  6, 38,
    60, 28, 52, 20, 62, 30, 54, 22,
     3, 35, 11, 43,  1, 33,  9, 41,
    51, 19, 59, 27, 49, 17, 57, 25,
    15, 47,  7, 39, 13, 45,  5, 37,
    63, 31, 55, 23, 61, 29, 53, 21
};

struct Stop
{
    qreal  pos;
    QColor color;
};

// One channel, dithered and rounded. `v` is the exact value in 0..255 and `t` the Bayer
// threshold in [-0.5, 0.5).
inline int quantise(qreal v, qreal t)
{
    return qBound(0, qRound(v + t), 255);
}

} // namespace

DitheredGradientProvider::DitheredGradientProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage DitheredGradientProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    // <v|h>/<pos>:<aarrggbb>,...
    const int slash = id.indexOf(QLatin1Char('/'));
    if (slash <= 0) {
        return QImage();
    }

    const bool vertical = id.left(slash) == QLatin1String("v");

    QList<Stop> stops;
    const QStringList parts = id.mid(slash + 1).split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        const int colon = part.indexOf(QLatin1Char(':'));
        if (colon <= 0) {
            continue;
        }
        bool ok = false;
        const qreal pos = part.left(colon).toDouble(&ok);
        if (!ok) {
            continue;
        }
        const uint argb = part.mid(colon + 1).toUInt(&ok, 16);
        if (!ok) {
            continue;
        }
        stops.append({ pos, QColor::fromRgba(argb) });
    }

    if (stops.isEmpty()) {
        return QImage();
    }

    // Only the axis the gradient runs along needs real pixels; across it, one Bayer tile is
    // all there is to say, and the caller tiles it. That keeps this to a few thousand pixels
    // even for a full-height window backdrop.
    const int along  = qMax(1, vertical ? requestedSize.height() : requestedSize.width());
    const int across = 8;

    const int w = vertical ? across : along;
    const int h = vertical ? along  : across;

    QImage img(w, h, QImage::Format_ARGB32);

    for (int y = 0; y < h; y++) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));

        for (int x = 0; x < w; x++) {
            const qreal t = (vertical ? y : x) / (qreal)qMax(1, along - 1);

            // Locate the segment this position falls in and interpolate it.
            int i = 0;
            while (i < stops.size() - 1 && t > stops[i + 1].pos) {
                i++;
            }
            const Stop& a = stops[i];
            const Stop& b = stops[qMin(i + 1, stops.size() - 1)];
            const qreal span = b.pos - a.pos;
            const qreal f = (span > 0.0) ? qBound(0.0, (t - a.pos) / span, 1.0) : 0.0;

            const qreal rf = a.color.red()   + (b.color.red()   - a.color.red())   * f;
            const qreal gf = a.color.green() + (b.color.green() - a.color.green()) * f;
            const qreal bf = a.color.blue()  + (b.color.blue()  - a.color.blue())  * f;
            const qreal af = a.color.alpha() + (b.color.alpha() - a.color.alpha()) * f;

            // ⚠️ The threshold has to vary across the ramp as well as along it, or the
            // dither becomes a set of stripes parallel to the bands it is meant to break —
            // which looks worse than the banding.
            const qreal th = (kBayer8[(y % 8) * 8 + (x % 8)] + 0.5) / 64.0 - 0.5;

            line[x] = qRgba(quantise(rf, th), quantise(gf, th),
                            quantise(bf, th), quantise(af, th));
        }
    }

    if (size != nullptr) {
        *size = QSize(w, h);
    }
    return img;
}
