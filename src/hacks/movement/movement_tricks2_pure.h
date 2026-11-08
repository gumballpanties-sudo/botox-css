#pragma once
// Unit A3 port: the pure-math decisions out of movement_tricks2.inl, pulled out so they're
// testable without the engine (see tests/movementtricks2_test.cpp). No engine types, no
// includes -- plain in/out, same idiom as movement_ex_pure.h.

// botox n_route::creep_wish (route_calc.cpp:836) / auto_align.cpp's own pixel-jump-lip call site:
// how hard to push into (positive) or out of (negative) a wall gap so the hull ends this tick
// creepGap off the face -- gentle enough that no hard push parks it (no hit = no 0.03125 flush).
// Binary search over wish magnitude for one AirAccelerate tick to close `need` of the gap; duck
// divides the wishspeed cap (botox's own `/0.34` -- duck's air-wish crop, pixelsurf.inl/
// autobounce_pure.h header already note the same crop elsewhere). Pure: plain floats in, no
// entity/cmd access, no engine calls -- see tests/movementtricks2_test.cpp.
inline float AaCreepWishSpeed(float vIn, float gap, float dt, float halfG, float vz, bool duck,
                              float airAccel, float airCap, float creepGap = 0.0005f, float creepShare = 0.75f)
{
    if (dt <= 0.f) return 0.f;
    const float want = gap > creepGap ? gap * creepShare / dt : 0.f;
    const bool  push = want > vIn;
    const float diff = want - vIn;
    const float need = diff < 0.f ? -diff : diff;
    const float cur  = push ? vIn : -vIn;
    const float friction = (vz + halfG) > 0.f ? 0.25f : 1.f;
    const float accel = airAccel * dt * friction;
    const auto  cap1  = [](float a, float b) { return a < b ? a : b; };
    const auto  add    = [&](float w) { return cap1(accel * w, cap1(w, airCap) - cur); };
    float lo = 0.f, w = 250.f;
    if (add(w) > need) {
        for (int i = 0; i < 32; ++i) {
            const float mid = 0.5f * (lo + w);
            if (add(mid) >= need) w = mid; else lo = mid;
        }
    }
    if (duck) w = w / 0.34f;
    if (need <= 0.f) w = 0.f;
    return push ? w : -w;
}
