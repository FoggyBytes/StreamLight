#pragma once

#include <QColor>
#include <QString>

/**
 * Two colours that are guaranteed to work together, from one picture or one seed.
 *
 * <p>Written for the launch curtain, which needed a background from a game's box art, and
 * now shared with the host stage, which needs one from whatever the user picked for that
 * host. Both want the same thing and would otherwise each get their own "grab the dominant
 * colour" loop — and two of those drift apart the first time one of them is tuned.</p>
 *
 * <p>The load-bearing part is not finding the colour, it is <b>normalising</b> it. Artwork
 * is vivid but dark, or bright but washed out, and handing a caller the raw pixel means
 * every caller has to correct it before use. Hue carries the identity; brightness and
 * saturation are ours to set.</p>
 */
namespace CoverPalette
{
    /** The neutral pair. Used whenever the source gives us nothing worth using. */
    void neutral(QColor& accent, QColor& deep);

    /**
     * Reads a local image file and derives the pair from its dominant colour.
     *
     * A cover that is nearly black or nearly grey yields a muddy gradient, which looks
     * worse than no gradient at all — so those are rejected and the neutral pair is
     * returned instead.
     *
     * @return true when the picture actually supplied a hue.
     */
    bool fromImageFile(const QString& localPath, QColor& accent, QColor& deep);

    /**
     * Derives the pair from a single colour the user chose, so picking a colour and picking
     * a picture end in the same place instead of being two separate looks.
     */
    bool fromSeed(const QColor& seed, QColor& accent, QColor& deep);
}
