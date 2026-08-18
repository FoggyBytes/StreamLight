#pragma once

#include <QQuickImageProvider>

/**
 * Gradients that do not band, by dithering them where dithering belongs.
 *
 * <p><b>The problem.</b> A gradient is computed in floating point and written to an 8-bit
 * surface. A ramp that crosses few levels over many pixels therefore lands as a staircase:
 * the app's own floor runs 21 → 34 in the green channel over 70% of the window height, which
 * is thirteen steps spread across six hundred pixels — one hard edge every fifty px, in the
 * dark end where the eye is most sensitive to them. More gradient stops cannot help: the
 * steps are the output depth, not the interpolation.</p>
 *
 * <p><b>Why the obvious fix was worse.</b> The previous attempt laid a tile of faint noise
 * over the finished gradient. Compositing cannot do this evenly: white at alpha 5 over a
 * near-black background adds about five levels, while black at the same alpha subtracts a
 * third of one. So the pattern was five times too strong AND lopsided, and it read as bright
 * static grain rather than disappearing — which is exactly what it was reported as.</p>
 *
 * <p><b>What this does instead.</b> It builds the gradient itself and applies an ordered
 * (Bayer) threshold at the moment each channel is rounded to 8 bits. The perturbation is
 * ±0.5 of one level by construction — symmetric, and the same size whether the pixel is
 * nearly black or nearly white, because it is added before the rounding rather than blended
 * on top of the result. That is the whole difference: dithering a quantisation, instead of
 * decorating one that already happened.</p>
 *
 * <p>Alpha is dithered too. The host card's scrim fades to transparent, and a staircase in
 * the alpha channel bands just as visibly as one in a colour channel.</p>
 *
 * <p><b>URL form:</b> {@code image://gradient/<v|h>/<pos>:<aarrggbb>[,<pos>:<aarrggbb>…]} —
 * `v` runs top to bottom, `h` left to right. Build it with gui/DitheredGradient.qml rather
 * than by hand; that component also asks for the one size that makes the result exact.</p>
 */
class DitheredGradientProvider : public QQuickImageProvider
{
public:
    DitheredGradientProvider();

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};
