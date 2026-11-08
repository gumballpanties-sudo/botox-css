// pjseam.h -- ref pixeljump seam height test, no engine deps (tests/pjseam_test.cpp)
//
// ref (z, pz) 1:1 (decomp: both sign branches reduce to the same rule):
//   same integer part (trunc toward zero), and the hull's hundredths bucket is the point's
//   or up to 2 BELOW it. Seam 638.029 -> z in [638.00, 638.03). 637.984 is under the lip's
//   integer = falls past, never pins (the old |dz| <= 0.05 window fired on it, rode=0).
#pragma once

namespace pjs
{
inline bool SeamHit(float z, float pz)
{
    const int iz = (int)z, ip = (int)pz;
    if (iz != ip)
        return false;
    const int hz = (int)((z - (float)iz) * 100.f);
    const int hp = (int)((pz - (float)ip) * 100.f);
    const int d = hp - hz;
    return d >= 0 && d <= 2;
}
}
