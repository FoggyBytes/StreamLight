// Imported from Nonary/moonlight-qt, branch release/6.1.0-vrr18 at tag v6.1.0-vrr18 (1ad5848b), by Chase
// Payne. GPLv3, the same licence as StreamLight. The body is verbatim: only this note was
// added, so a later sync against Nonary is a plain diff. Say so here if you change anything.

#pragma once

#include <algorithm>
#include <cmath>

// The PyroWave author's objective bitrate regression (see
// pyrowave/pyrowave/eval-results/objective-bitrate-evaluation.md).
#include "../../../../pyrowave/pyrowave/eval-results/pyrowave_regression_results.h"

// Quality is PSNR-HVS-M-H in dB at a viewing distance of twice the screen
// height. The author found 35 dB tracks his subjective "good quality" curve.
constexpr double kPyroWaveGoodQualityDb = 35.0;

// The author's regression in Mbps for a whole-dB quality level, before the
// HDR factor. It covers 720p to 4K pixel counts; resolutions outside use the
// nearest end.
inline double pyroWaveRegressionMbits(int width, int height, int fps, bool chroma444, int db)
{
    const double pixels = double(width) * height;
    int w = width, h = height;
    if (pixels < PYROWAVE_REGRESSION_MIN_PIXELS) {
        w = 1280;
        h = 720;
    }
    else if (pixels > PYROWAVE_REGRESSION_MAX_PIXELS) {
        w = 3840;
        h = 2160;
    }
    db = std::clamp(db, PYROWAVE_REGRESSION_MIN_PSNR_HVS_M_H, PYROWAVE_REGRESSION_MAX_PSNR_HVS_M_H);
    return pyrowave_psnr_hvs_m_h_estimate_mbits(db, w, h, PYROWAVE_HEIGHT_FACTOR_2_00,
                                                chroma444 ? 1 : 0, std::max(fps, 1));
}

// The author allows 1.2x for HDR10.
inline double pyroWaveHdrFactor(bool hdr)
{
    return hdr ? 1.2 : 1.0;
}

// Bitrate in kbps for a quality level, interpolated between the regression's
// whole-dB levels and clamped to the range it covers.
inline double pyroWaveKbpsForQuality(int width, int height, int fps, bool chroma444, bool hdr, double db)
{
    db = std::clamp(db, double(PYROWAVE_REGRESSION_MIN_PSNR_HVS_M_H), double(PYROWAVE_REGRESSION_MAX_PSNR_HVS_M_H));
    const int lower = int(std::floor(db));
    const int upper = std::min(lower + 1, PYROWAVE_REGRESSION_MAX_PSNR_HVS_M_H);
    const double lowerMbits = pyroWaveRegressionMbits(width, height, fps, chroma444, lower);
    const double upperMbits = pyroWaveRegressionMbits(width, height, fps, chroma444, upper);
    const double mbits = lowerMbits + (upperMbits - lowerMbits) * (db - lower);
    return mbits * pyroWaveHdrFactor(hdr) * 1000.0;
}

// The quality level a bitrate reaches, the inverse of pyroWaveKbpsForQuality().
// Below the regression's lowest level it extrapolates that level's slope, so
// a starved bitrate still reads as worse than the floor.
inline double pyroWaveQualityDb(int width, int height, int fps, bool chroma444, bool hdr, double kbps)
{
    const double mbits = kbps / 1000.0 / pyroWaveHdrFactor(hdr);
    double previous = pyroWaveRegressionMbits(width, height, fps, chroma444, PYROWAVE_REGRESSION_MIN_PSNR_HVS_M_H);
    if (mbits < previous) {
        const double next = pyroWaveRegressionMbits(width, height, fps, chroma444,
                                                    PYROWAVE_REGRESSION_MIN_PSNR_HVS_M_H + 1);
        const double slope = std::max(next - previous, 1e-6);
        return std::max(0.0, PYROWAVE_REGRESSION_MIN_PSNR_HVS_M_H - (previous - mbits) / slope);
    }
    for (int db = PYROWAVE_REGRESSION_MIN_PSNR_HVS_M_H + 1; db <= PYROWAVE_REGRESSION_MAX_PSNR_HVS_M_H; ++db) {
        const double current = pyroWaveRegressionMbits(width, height, fps, chroma444, db);
        if (mbits <= current) {
            return db - 1 + (mbits - previous) / std::max(current - previous, 1e-6);
        }
        previous = current;
    }
    return PYROWAVE_REGRESSION_MAX_PSNR_HVS_M_H;
}

// The author's good-quality bitrate in kbps, including his HDR10 allowance,
// rounded up to the slider's 5 Mbps step for both defaults and calibration.
inline int pyroWaveRecommendedKbps(int width, int height, int fps, bool chroma444, bool hdr)
{
    const int kbps = int(pyroWaveKbpsForQuality(width, height, fps, chroma444, hdr, kPyroWaveGoodQualityDb));
    return int(std::ceil(kbps / 5000.0)) * 5000;
}
