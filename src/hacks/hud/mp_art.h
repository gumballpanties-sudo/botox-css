#pragma once
#include <cstdint>

// Cover art geometry for the media player, on its own so the gate test
// (tests/mediaplayer_test.cpp) can have it without winrt, d3d or imgui.
//
// The tile is square and the source is not: this machine's live session hands out a 150x83
// video thumbnail, album art is square, a phone photo can be 3:4. Scale the SHORT side to the
// tile and cut the middle out of the long one, rather than squashing the picture.
//
// The bounds go to winrt in SCALED space (that is the order it applies a BitmapTransform in),
// and a rect that runs past the scaled image throws -- which is why the rounding below is
// deliberate and why the test sweeps extreme aspect ratios.
namespace mp {

struct ArtCrop {
    uint32_t scaledW, scaledH;   // BitmapTransform::ScaledWidth / ScaledHeight
    uint32_t x, y, w, h;         // BitmapTransform::Bounds, inside scaledW x scaledH
};

// k = tile edge in px (64). srcW/srcH must both be non-zero.
inline ArtCrop ArtCropFor(uint32_t srcW, uint32_t srcH, uint32_t k)
{
    const double sc = (double)k / (double)(srcW < srcH ? srcW : srcH);

    uint32_t tw = (uint32_t)(srcW * sc + 0.5), th = (uint32_t)(srcH * sc + 0.5);

    // Belt and braces, and known to be: s * (k / s) is within 2 ulp of k, so the +0.5 always
    // carries it back to k and the test's 185k-case sweep cannot reach these two lines
    // (verified by deleting them -- the suite still passed). They stay because the thing they
    // prevent is a scaled image smaller than the crop rect, which does not misdraw, it throws
    // inside winrt's GetSoftwareBitmapAsync and takes the cover art out entirely.
    if (tw < k) tw = k;
    if (th < k) th = k;

    ArtCrop c{};
    c.scaledW = tw; c.scaledH = th;
    c.x = (tw - k) / 2; c.y = (th - k) / 2;
    c.w = k; c.h = k;
    return c;
}

} // namespace mp
