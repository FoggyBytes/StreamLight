#include "coverpalette.h"

#include <QImage>

namespace
{
    // Same normalisation for both entry points: whatever the hue came from, the pair that
    // comes out sits in the same brightness and saturation band. That is what makes a
    // user-chosen colour and a colour lifted off box art look like the same feature.
    void normalise(int h, int s, QColor& accent, QColor& deep)
    {
        accent = QColor::fromHsv(h, qBound(90, s, 220), 200);
        // The mid tone: same hue, deep enough to sit under text without being pure black —
        // black would make the accent read as a stripe rather than a glow.
        deep = QColor::fromHsv(h, qBound(60, s, 180), 38);
    }
}

namespace CoverPalette
{

void neutral(QColor& accent, QColor& deep)
{
    accent = QColor(0x46, 0x58, 0x6e);
    deep   = QColor(0x14, 0x18, 0x20);
}

bool fromImageFile(const QString& localPath, QColor& accent, QColor& deep)
{
    neutral(accent, deep);

    if (localPath.isEmpty()) {
        return false;
    }

    QImage img(localPath);
    if (img.isNull()) {
        return false;
    }

    // A handful of pixels is plenty: we want the dominant hue, not detail. Done once when
    // the picture is chosen, never per frame.
    img = img.scaled(8, 8, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
             .convertToFormat(QImage::Format_RGB32);

    QColor best;
    int bestScore = -1;
    int n = 0;

    for (int y = 0; y < img.height(); y++) {
        for (int x = 0; x < img.width(); x++) {
            const QColor c = img.pixelColor(x, y);
            n++;

            // Saturation times value: bright and colourful beats bright and grey.
            const int score = c.saturation() * c.value() / 255;
            if (score > bestScore) { bestScore = score; best = c; }
        }
    }

    if (n == 0 || best.saturation() < 45) {
        return false;   // no hue worth using
    }

    int h, s, v;
    best.getHsv(&h, &s, &v);
    normalise(h, s, accent, deep);
    return true;
}

bool fromSeed(const QColor& seed, QColor& accent, QColor& deep)
{
    neutral(accent, deep);

    if (!seed.isValid()) {
        return false;
    }

    int h, s, v;
    seed.getHsv(&h, &s, &v);

    // A grey or near-grey seed has no hue to normalise; HSV reports hue -1 for it, which
    // QColor::fromHsv would reject outright.
    if (h < 0 || s < 30) {
        return false;
    }

    normalise(h, s, accent, deep);
    return true;
}

}   // namespace CoverPalette
