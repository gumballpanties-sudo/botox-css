#pragma once
#include <cmath>

// Pure fireman (ladder catch) math. No engine reads, no globals -- tests/fireman_test.cpp.
// CSS engine facts these encode (css-community-master mp/src/game/shared):
//   LadderMove            gamemovement.cpp:2854-3014 -- grab = 2u hull trace along the PITCHED
//                         wishdir (:2876-2892), IN_JUMP -> WALK + velocity = 270 * plane.normal
//                         SAME tick (:2934-2939). No cooldown, no ride.
//   CCSGameMovement::OnLadder cs_gamemovement.cpp:450-456 (rejects normal.z == 1.0 exactly)
//   AirAccelerate         gamemovement.cpp -- addspeed = min(wishspeed,30) - dot(v,wishdir),
//                         accelspeed = airaccel * wishspeed * dt (wishspeed NOT capped there)
namespace fireman
{

inline constexpr float kFlingSpeed = 270.f;   // LadderMove :2939
inline constexpr float kAirCap     = 30.f;    // AirAccelerate wishspd cap

// A latch off a face steeper than this still flings (:2939 has no angle gate) -- up, or into the floor.
inline bool IsFlatLadderFace(float normalZ) { return std::fabs(normalZ) < 0.3f; }

// A hull pressed flush against a face starts solid and returns a zero plane.
inline bool IsDegenerateNormal(float normalLenSq) { return normalLenSq < 0.01f; }

// OnLadder's own reject (normal.z == 1, a floor) + flat face + non-degenerate.
inline bool CanFling(float normalZ, float normalLenSq)
{
    return !IsDegenerateNormal(normalLenSq) && normalZ != 1.0f && IsFlatLadderFace(normalZ);
}

// Sim END state of a grab cmd: did LadderMove fling us? Velocity was ASSIGNED 270 * normal
// (|normal| = 1, z up to +-81 on a 0.3 face), then one tick of gravity (two half steps).
// The grab's own wish is 1 u/s, so AirMove adds ~0.15 -- nothing else touches it.
inline bool IsFlung(float vx, float vy, float vz, float gravity, float dt)
{
    const float z = vz + gravity * dt;
    return std::fabs(std::sqrt(vx * vx + vy * vy + z * z) - kFlingSpeed) < 20.f;
}

// Ticks until the floor `depth` below, falling at `fallSpeed` (positive = down). Free fall.
inline float FallTicks(float fallSpeed, float depth, float gravity, float dt)
{
    if (depth <= 0.f || dt <= 0.f) return 0.f;
    if (gravity <= 0.f) return fallSpeed > 0.f ? depth / fallSpeed / dt : 1e6f;
    const float t = (-fallSpeed + std::sqrt(fallSpeed * fallSpeed + 2.f * gravity * depth)) / gravity;
    return t / dt;
}

// CSS AirAccelerate, horizontal part, 1:1. (wx, wy) unit wishdir.
inline void AirAccel2D(float& vx, float& vy, float wx, float wy, float wishspeed, float airaccel, float dt)
{
    const float wishspd = wishspeed > kAirCap ? kAirCap : wishspeed;
    const float add = wishspd - (vx * wx + vy * wy);
    if (add <= 0.f) return;
    float acc = airaccel * wishspeed * dt;
    if (acc > add) acc = add;
    vx += acc * wx; vy += acc * wy;
}

// Station keeping (botox fireman 2026-08-16, tested good): want = toward the target at the speed
// the BRAKE can still undo (sqrt(2*a*d), a = 0.6 * airaccel * maxspeed), everything across it
// zeroed; the wish is the velocity error. Never capped at 30 -- 30 caps GAIN, not carried speed.
// (rx, ry) = target - position. Returns wishspeed (0 = write no move), unit dir in (wx, wy).
inline float KeepWish(float rx, float ry, float vx, float vy, float airaccel, float maxspeed, float dt,
                      float& wx, float& wy)
{
    wx = wy = 0.f;
    if (airaccel <= 0.f || dt <= 0.f || maxspeed <= 0.f) return 0.f;
    const float d = std::sqrt(rx * rx + ry * ry);
    float s = std::sqrt(2.f * 0.6f * airaccel * maxspeed * d);
    if (s > 450.f) s = 450.f;
    const float wantX = d > 1e-4f ? rx / d * s : 0.f, wantY = d > 1e-4f ? ry / d * s : 0.f;
    const float dx = wantX - vx, dy = wantY - vy;
    const float m = std::sqrt(dx * dx + dy * dy);
    if (m < 0.5f) return 0.f;
    wx = dx / m; wy = dy / m;
    const float ws = m / (airaccel * dt);
    return ws > maxspeed ? maxspeed : ws;
}

// Ticks for KeepWish to bring the hull within `tol` of the target, -1 = not inside maxTicks.
// Same controller + same AirAccelerate as the live steer, so gate and steering cannot disagree.
inline int ParkTicks(float rx, float ry, float vx, float vy, float airaccel, float maxspeed, float dt,
                     int maxTicks, float tol)
{
    for (int t = 0; t <= maxTicks; ++t)
    {
        if (rx * rx + ry * ry <= tol * tol) return t;
        float wx, wy;
        const float ws = KeepWish(rx, ry, vx, vy, airaccel, maxspeed, dt, wx, wy);
        if (ws > 0.f) AirAccel2D(vx, vy, wx, wy, ws, airaccel, dt);
        rx -= vx * dt; ry -= vy * dt;
    }
    return -1;
}

// LadderMove attached velocity, 1:1 (gamemovement.cpp:2919-3010, CSTRIKE dampen included, not
// ducked, no IN_JUMP, off the floor). View angles deg, n = ladder plane normal (unit), btn speeds
// = +-200 per key like :2921-2932. Writes the velocity LadderMove assigns.
inline void LadderVel(float pitch, float yaw, float fwdSpeed, float rightSpeed,
                      float nx, float ny, float nz, float& vx, float& vy, float& vz)
{
    constexpr float kD2R = 3.14159265358979f / 180.f;
    const float sp = std::sin(pitch * kD2R), cp = std::cos(pitch * kD2R);
    const float sy = std::sin(yaw * kD2R), cy = std::cos(yaw * kD2R);
    const float F[3] = { cp * cy, cp * sy, -sp };   // AngleVectors, roll 0
    const float R[3] = { sy, -cy, 0.f };
    vx = vy = vz = 0.f;
    if (fwdSpeed == 0.f && rightSpeed == 0.f) return;   // :3009 Init
    const float n[3] = { nx, ny, nz };
    float v[3], perp[3], tmp[3], cross[3], lat[3];
    for (int i = 0; i < 3; ++i) v[i] = F[i] * fwdSpeed + R[i] * rightSpeed;
    // perp = up x n, normalized
    perp[0] = -n[1]; perp[1] = n[0]; perp[2] = 0.f;
    const float pl = std::sqrt(perp[0] * perp[0] + perp[1] * perp[1]);
    if (pl > 0.f) { perp[0] /= pl; perp[1] /= pl; }
    const float normal = v[0] * n[0] + v[1] * n[1] + v[2] * n[2];
    for (int i = 0; i < 3; ++i) { cross[i] = n[i] * normal; lat[i] = v[i] - cross[i]; }
    // tmp = n x perp
    tmp[0] = n[1] * perp[2] - n[2] * perp[1];
    tmp[1] = n[2] * perp[0] - n[0] * perp[2];
    tmp[2] = n[0] * perp[1] - n[1] * perp[0];
    const float tmpDist  = tmp[0] * lat[0] + tmp[1] * lat[1] + tmp[2] * lat[2];
    const float perpDist = perp[0] * lat[0] + perp[1] * lat[1] + perp[2] * lat[2];
    float a[3];
    for (int i = 0; i < 3; ++i) a[i] = perp[i] * perpDist + cross[i];
    const float al = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    const float angleDot = al > 0.f ? (a[0] * n[0] + a[1] * n[1] + a[2] * n[2]) / al : 0.f;
    if (angleDot < -0.707f)   // sv_ladder_angle default, dampen 0.2
        for (int i = 0; i < 3; ++i) lat[i] = tmp[i] * tmpDist + perp[i] * 0.2f * perpDist;
    vx = lat[0] - normal * tmp[0];
    vy = lat[1] - normal * tmp[1];
    vz = lat[2] - normal * tmp[2];
}

// Fastest descent while attached: IN_FORWARD + IN_MOVERIGHT, pitch 89 (down), yaw so the right
// vector points along the face normal. V = 200*fwd + 200*right -> into-face part flips to -up:
// vz = -(200 sin89 + 200) ~ -400, 2x IN_BACK's 200. Yaw is 89 deg off the normal, not 90: that
// cancels the 200*cos89 = 3.5 u/s sideways slide pitch 89 leaves (tan(off) = 1/cos(pitch)).
inline constexpr float kDescentPitch = 89.f;
inline float FastDescentYaw(float nx, float ny)
{
    constexpr float kR2D = 180.f / 3.14159265358979f;
    const float off = std::atan2(1.f, std::cos(kDescentPitch / kR2D)) * kR2D;
    return std::atan2(ny, nx) * kR2D + off;
}

} // namespace fireman
