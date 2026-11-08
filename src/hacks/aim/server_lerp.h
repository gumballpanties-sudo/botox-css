#pragma once
#include <algorithm>

// The server's own lerp-time formula (source-sdk-2013 player.cpp:3484-3515), with the client's
// cvar values (userinfo carries them). Generic lag-compensation math, not backstab-specific --
// extracted out of the now-deleted bs_math.h (TF2 auto-backstab swing/rewind predictor, dead in
// this CSS tree) because aim_record.h's BacktrackLerp() is the one live caller, feeding
// CBacktrack::LerpTime()/CBacktrack::MaxUnlag() for plain backtrack lag comp.
// Pass minRatio = -1 when sv_client_min_interp_ratio is -1 or missing (no ratio clamp).
namespace Lerp
{
	inline float ServerLerp(bool interpolate, float clInterp, float clInterpRatio, float clUpdateRate,
		float svMinRate, float svMaxRate, float svMinRatio, float svMaxRatio)
	{
		if (!interpolate)
			return 0.f;
		// Source's clamp macro, not std::clamp: defined (max wins) when a server sets min > max
		auto srcClamp = [](float x, float lo, float hi) { return x > hi ? hi : x < lo ? lo : x; };
		float rate = float(int(clUpdateRate));
		if (svMaxRate > 0.f)
			rate = srcClamp(rate, float(int(svMinRate)), float(int(svMaxRate)));
		rate = std::max(rate, 1.f);
		float ratio = clInterpRatio == 0.f ? 1.f : clInterpRatio;
		if (svMinRatio != -1.f)
			ratio = srcClamp(ratio, svMinRatio, svMaxRatio);
		return std::max(clInterp, ratio / rate);
	}
}
