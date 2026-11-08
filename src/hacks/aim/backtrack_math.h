#pragma once
// Engine-free half of backtrack.cpp (tests/backtrack_test.cpp).
#include <algorithm>
#include <cmath>
#include <utility>

namespace bt
{
	// botox lagcomp.cpp is_valid: sim newer than the server's dead time (curtime - sv_maxunlag,
	// int-truncated in player_lagcompensation.cpp:258) and within 0.2 - margin of the centre
	inline bool WindowValid(float flSimTime, float flNow, float flMargin, float flUnlag, float flCenter)
	{
		if (flSimTime <= 0.f || flSimTime < std::floor(flNow + flMargin - flUnlag))
			return false;
		return std::fabs(flCenter - (flNow - flSimTime)) <= 0.2f - flMargin;
	}

	// botox lagcomp.cpp usable: + no further behind the live sim than the time limit
	inline bool Usable(float flSimTime, float flLiveSim, float flNow, float flMargin, float flUnlag, float flCenter, float flLimit)
	{
		return WindowValid(flSimTime, flNow, flMargin, flUnlag, flCenter) && flLiveSim - flSimTime <= flLimit + 1e-3f;
	}

	// segment a -> b vs box [lo, hi] (slab test): true = touches, tOut = first touch 0..1
	inline bool SegmentHitsBox(const float* a, const float* b, const float* lo, const float* hi, float& tOut)
	{
		float t0 = 0.f, t1 = 1.f;
		for (int k = 0; k < 3; k++)
		{
			const float d = b[k] - a[k];
			if (std::fabs(d) < 1e-6f)
			{
				if (a[k] < lo[k] || a[k] > hi[k])
					return false;
				continue;
			}
			float ta = (lo[k] - a[k]) / d, tb = (hi[k] - a[k]) / d;
			if (ta > tb) std::swap(ta, tb);
			t0 = std::max(t0, ta);
			t1 = std::min(t1, tb);
			if (t0 > t1)
				return false;
		}
		tOut = t0;
		return true;
	}

	inline int TimeToTicks(float t, float flInterval) { return static_cast<int>(0.5f + t / flInterval); }

	// TF2 server, player_lagcompensation.cpp:379-412: true = it rewinds to OUR tick_count,
	// false = the 0.2s sanity check overrides it with its own latency tick
	inline bool ServerAccepts(int iTickCount, int iServerTick, float flLatencyOut, float flLerp, float flUnlag, float flInterval)
	{
		const int iLerpTicks = TimeToTicks(flLerp, flInterval);
		const float flCorrect = std::clamp(flLatencyOut + iLerpTicks * flInterval, 0.f, flUnlag);
		const int iTarget = iTickCount - iLerpTicks;
		return std::fabs(flCorrect - (iServerTick - iTarget) * flInterval) <= 0.2f;
	}
}
