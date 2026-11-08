#pragma once
// Engine-free half of grenade_aim.cpp's wall-bounce solve (tests/grenade_math_test.cpp).
// Own tiny vector, same convention as backtrack_math.h/bs_math.h -- no engine Vec3, no I::, no
// ConfigVar, so this is includable (and testable) with nothing but the standard library.
#include <algorithm>
#include <cmath>

namespace GrenadeMath
{
	struct V3
	{
		float x = 0.f, y = 0.f, z = 0.f;
	};
	inline V3 operator+(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
	inline V3 operator-(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
	inline V3 operator*(V3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
	inline float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	inline float Length(V3 a) { return std::sqrt(Dot(a, a)); }

	// Vertical wall plane for the grenade centre (botox grenade_aim.cpp wall_t): n . x = dist.
	struct Wall
	{
		V3 m_vNormal = {};
		float m_flDist = 0.f;
	};

	// CSS PhysicsAddGravityMove half-step (physics_main_shared.cpp:1282-1291): the move for one
	// tick is the AVERAGE of the pre- and post-gravity z velocity times dt, and the stored
	// velocity afterward is the post-gravity value. Exact for constant acceleration (it's the
	// trapezoid rule, which is exact for a linear integrand) -- NOT the same as plain Euler
	// (moving the whole step at the post-gravity velocity), which overdraws the drop every tick.
	// TracePath and CssGrenadeBouncePath (grenade_aim.cpp) both call this so there is one gravity
	// step to get right.
	inline float GravityStep(float flZVel, float flGravity, float flStep, float& flZVelOut)
	{
		flZVelOut = flZVel - flGravity * flStep;
		return (flZVel + flZVelOut) * 0.5f * flStep;
	}

	// CSS CBaseCSGrenade::ThrowGrenade pitch remap + speed (weapon_basecsgrenade.cpp:339-354).
	// CSS couples throw speed to the launch pitch -- CS:GO does not (fixed data->m_throw_velocity),
	// which is why this and BounceVelocity below differ from botox's closed-form bounce math.
	inline float ThrowPitchRemap(float flViewPitch)
	{
		float a = flViewPitch;
		if (a < 90.f)
			a = -10.f + a * (100.f / 90.f);
		else
		{
			a = 360.f - a;
			a = -10.f + a * -(80.f / 90.f);
		}
		return a;
	}

	inline float ThrowSpeed(float flThrownPitch) { return (std::min)((90.f - flThrownPitch) * 6.f, 750.f); }

	// Inverse of ThrowPitchRemap's first branch (view pitch -89..89 never reaches the second).
	inline bool ViewPitch(float flThrown, float& flView)
	{
		flView = (flThrown + 10.f) * 0.9f;
		return flView >= -89.f && flView <= 89.f;
	}

	// Math::VectorAngles hands pitch back as 0..360 (up = 270..360); throw math wants -180..180.
	inline float NormPitch(float p) { return p > 180.f ? p - 360.f : p; }

	// Source pitch of a direction, + = down (same sign as view angles).
	inline float DirPitch(V3 d) { return -std::atan2(d.z, std::sqrt(d.x * d.x + d.y * d.y)) * 57.2957795f; }

	// Direct throw that sits on vTarget t seconds after release. CSS ThrowGrenade
	// (weapon_basecsgrenade.cpp:356-370): spawn = eye + dir*16, vel = dir*speed + inherit; gravity is
	// exact at tick ends with GravityStep. So (16 + speed*t)*dir = D, D = target - eye - inherit*t
	// + 0.5*g*t^2 up -> dir = D/|D|, speed the geometry needs = (|D| - 16)/t. The game's speed for
	// that dir is ThrowSpeed(DirPitch(dir)); the root of (need - game) is a real throw.
	inline bool DirectLaunch(V3 vEye, V3 vInherit, V3 vTarget, float t, float flGravity, float flLaunchOffset,
		V3& vDir, float& flNeed)
	{
		if (t <= 0.f)
			return false;
		V3 vD = vTarget - vEye - vInherit * t;
		vD.z += 0.5f * flGravity * t * t;
		const float flLen = Length(vD);
		if (flLen <= flLaunchOffset + 1.f)
			return false;
		vDir = vD * (1.f / flLen);
		flNeed = (flLen - flLaunchOffset) / t;
		return true;
	}

	// botox grenade_aim.cpp:224-246 bounce_velocity, CSS-adapted: total launch velocity from
	// vSpawn that reaches vTarget at time t after exactly one elastic reflect off `wall`.
	// e/tau/s/t1/t2/span are the wall-normal-only time split (purely geometric: how long before
	// the bounce vs after, so the post-bounce normal speed is elasticity * pre-bounce normal
	// speed) -- this part does NOT depend on CS:GO's fixed throw speed or CSS's pitch-coupled one,
	// which is why botox's formula ports unchanged. What CHANGES for CSS: botox's bounce_times
	// compares the result against a known constant th.m_speed; CSS has none (BounceTimes in
	// grenade_aim.cpp instead derives the implied speed from this velocity's own pitch via
	// ThrowSpeed(DirPitch(...)) -- the launch dir already IS the thrown pitch, no remap -- and uses
	// THAT as the root-finding target).
	inline bool BounceVelocity(const Wall& tWall, V3 vSpawn, V3 vTarget, float t, float flGravity, float flElasticity,
		V3& vVelocity, float& flHitTime)
	{
		if (t <= 0.f || flElasticity <= 0.f)
			return false;

		const float e = Dot(vSpawn, tWall.m_vNormal) - tWall.m_flDist;
		const float tau = Dot(vTarget, tWall.m_vNormal) - tWall.m_flDist;
		if (e <= 0.f || tau <= 0.f)
			return false;

		const float s = (e + tau / flElasticity) / t;
		if (s <= 0.f)
			return false;
		const float t1 = e / s;
		const float t2 = t - t1;
		if (t1 <= 0.f || t2 <= 0.f)
			return false;
		const float flSpan = t1 + flElasticity * t2;

		const V3 vSide{ -tWall.m_vNormal.y, tWall.m_vNormal.x, 0.f };
		const float vt = Dot(vTarget - vSpawn, vSide) / flSpan;
		const float vz = (vTarget.z - vSpawn.z + 0.5f * flGravity * t1 * t1
			+ flElasticity * flGravity * t1 * t2 + 0.5f * flGravity * t2 * t2) / flSpan;

		vVelocity = tWall.m_vNormal * -s + vSide * vt;
		vVelocity.z = vz;
		flHitTime = t1;
		return true;
	}

	// The 16u spawn-offset-follows-direction re-solve, same idiom as grenade_aim.cpp's own
	// SolveDirection (fixed point over a few passes converges the launch-offset self-reference).
	// vInherit (player velocity, added on top of the throw) is subtracted before deriving the
	// offset direction -- the 16u hand offset follows the AIM direction, not the full velocity.
	inline bool BounceLaunch(V3 vEye, V3 vInherit, const Wall& tWall, V3 vTarget, float t, float flGravity,
		float flElasticity, float flLaunchOffset, V3& vVelocity, float& flHitTime)
	{
		V3 vSpawn = vEye;
		for (int i = 0; i < 4; ++i)
		{
			if (!BounceVelocity(tWall, vSpawn, vTarget, t, flGravity, flElasticity, vVelocity, flHitTime))
				return false;

			const V3 vLaunch = vVelocity - vInherit;
			const float flLen = Length(vLaunch);
			if (flLen < 1.f)
				return false;

			vSpawn = vEye + vLaunch * (flLaunchOffset / flLen);
		}
		return true;
	}
}
