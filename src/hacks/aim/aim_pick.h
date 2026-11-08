#pragma once
// botox csgo-sdk hacks/aimbot/aimbot.cpp scan_targets() hitbox pick, pure (tests/aimpick_test.cpp).
// Candidates come in crosshair-nearest first. Narrow cone: nearest one that passes the gate.
// Wide cone: of the top kHcCandidates, the one whose cone lands most on the body, nearest/held
// hitbox get kHcKeep so a near tie never flicks head <-> chest.
#include <cmath>

namespace aimpick
{
	constexpr int kSamples = 48;          // botox spread_samples_t::k_count
	constexpr int kHcCandidates = 12;     // botox k_hc_candidates
	constexpr float kHcKeep = 0.1f;       // botox k_hc_keep
	constexpr float kWideCone = 0.002f;   // botox cone_t::wide, inaccuracy + spread in tangent units

	inline bool WideCone(float flInaccuracy, float flSpread) { return flInaccuracy + flSpread > kWideCone; }

	// botox spread_samples_t: fixed FX_FireBullets draws, radius uniform like the game's
	// RandomFloat(0, cone). (x0,y0) scales by inaccuracy, (x1,y1) by spread, offsets add.
	struct Samples
	{
		float x0[kSamples], y0[kSamples], x1[kSamples], y1[kSamples];
		Samples()
		{
			for (int i = 0; i < kSamples; i++)
			{
				const float d0 = (i + 0.5f) / kSamples, t0 = i * 2.39996323f;
				const float d1 = std::fmod(0.5f + i * 0.61803399f, 1.f), t1 = 6.28318531f * std::fmod(i * 0.75487767f, 1.f);
				x0[i] = d0 * std::cos(t0); y0[i] = d0 * std::sin(t0);
				x1[i] = d1 * std::cos(t1); y1[i] = d1 * std::sin(t1);
			}
		}
	};
	inline const Samples& GetSamples() { static const Samples s; return s; }

	// share of the cone that lands. hit(x, y) = does the ray offset by (x right, y up) hit the body
	template <class Hit>
	float HitChance(float flInaccuracy, float flSpread, Hit&& hit)
	{
		const auto& s = GetSamples();
		int iHits = 0;
		for (int i = 0; i < kSamples; i++)
			if (hit(s.x0[i] * flInaccuracy + s.x1[i] * flSpread, s.y0[i] * flInaccuracy + s.y1[i] * flSpread))
				iHits++;
		return float(iHits) / kSamples;
	}

	// botox ranked loop. hc(i) / gate(i) / held(i) are lazy: gate is a trace, hc 48 rays.
	// Returns the picked index or -1, *pHc = its hit chance (1 on a narrow cone).
	template <class Hc, class Gate, class Held>
	int Pick(int n, bool bWide, Hc&& hc, Gate&& gate, Held&& held, float* pHc = nullptr)
	{
		int iFirst = -1, iPick = -1;
		float flPickScore = -1.f, flPickHc = 1.f;
		for (int i = 0; i < n; i++)
		{
			if (iFirst >= 0 && (!bWide || i >= kHcCandidates))
				break;

			// nothing visible yet: trace first, sample only what passed (same pick as botox's
			// hc-then-gate, the score skip needs iFirst >= 0; a hidden player cost every
			// candidate 48 rays x every hitbox before a single trace said no)
			if (iFirst < 0 && !gate(i))
				continue;

			float flHc = 1.f, flScore = 1.f;
			if (bWide)
			{
				flHc = hc(i);
				flScore = flHc + (i == 0 || held(i) ? kHcKeep : 0.f);
				if (iFirst >= 0 && flScore <= flPickScore)
					continue;
			}

			if (iFirst >= 0 && !gate(i))
				continue;

			if (iFirst < 0)
				iFirst = i;
			iPick = i, flPickScore = flScore, flPickHc = flHc;
		}
		if (pHc)
			*pHc = flPickHc;
		return iPick;
	}
}
