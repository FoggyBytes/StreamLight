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

/*
 * Half a level, added to the COLOUR channels before they are dithered — and it is the
 * difference between this ramp dithering itself and this ramp dithering what is under it.
 *
 * Dither can only act on a value that falls between two integers: qRound(v + t) with t in
 * [-0.5, 0.5) returns v unchanged for every t when v is already a whole number. That is
 * correct for a ramp judged on its own — an exactly representable colour has no error to
 * spread — and it is why a ramp whose colour never changes, only its alpha, came out
 * perfectly flat: every pixel of it identical, no dither anywhere.
 *
 * A scrim is exactly that ramp, and it is not judged on its own. Laid over a picture the
 * result is pic*(1-a) + scrim*a, and near the dense end a is around 0.9 — so the picture
 * arrives with its 256 levels squeezed into about 26, and a smooth dark region turns into
 * wide plateaus a full level apart. Dithering the ALPHA cannot repair that: at this contrast
 * half a level of alpha moves the output by about 0.014 of a level, three orders of
 * magnitude short. Dithering the scrim's COLOUR can, because the output follows it almost
 * one for one — half a level in becomes 0.45 of a level out.
 *
 * Hence the bias: it puts the colour half a step off the lattice so the existing Bayer
 * threshold has something to resolve, and the channel alternates between two adjacent values
 * in the same ordered pattern as everything else. What it costs is half a level of lift and
 * half a level of jitter on flat colour — neither visible, and the jitter is the entire
 * point.
 *
 * ⚠️ Colour only, not alpha. The amplitude that reaches the composite is scaled by a, so it
 * is strongest exactly where the crushing is worst and fades out where there is nothing left
 * to crush — which is the behaviour wanted. Biasing alpha as well would just make the scrim
 * a fifth of a percent denser and change nothing else.
 */
constexpr qreal kColourDitherBias = 0.5;

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

            line[x] = qRgba(quantise(rf + kColourDitherBias, th),
                            quantise(gf + kColourDitherBias, th),
                            quantise(bf + kColourDitherBias, th),
                            quantise(af, th));
        }
    }

    if (size != nullptr) {
        *size = QSize(w, h);
    }
    return img;
}
