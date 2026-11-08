#pragma once
#include "sdk.h"
#include "css_weapons.h"

// Sniper scope FOV scale (gap.md Unit F). CSS has no zoom CONDITION (no TF_COND_ZOOMED
// equivalent -- CSS's cs_player_shared.h has no m_nPlayerCond at all): the AWP/SG550/G3SG1/
// Scout zoom state is purely the m_iFOV netvar dropping below the default 90 (weapon_csbase.cpp
// GetFOV()/GetDefaultFOV(), css_bridge.cpp CssBridge_IsScoped / css_weapons.h CSSIsScoped).
// Scaling it client-side here is self-affecting only: overwritten back to the real server value
// automatically on the next zoom-state network update, so there's nothing to restore.
namespace ScopeFOV
{
	// Called every CreateMove (hooks.cpp). No-ops instantly when off or not zoomed.
	inline void Run(CCSPlayer* pLocal)
	{
		static int s_iLastWritten = -1;
		static int s_iBaseFOV = 0;

		if (!Vars::Aimbot::ScopeFOV::Enabled.Value || !pLocal || !CSSIsScoped(pLocal))
		{
			s_iLastWritten = -1;
			return;
		}

		int& iFOV = pLocal->m_iFOV();
		// A value that isn't what we last wrote means the server sent a fresh zoom FOV (zoom
		// level changed, or we just entered zoom) -- that's the new base to scale from.
		if (iFOV != s_iLastWritten)
			s_iBaseFOV = iFOV;

		iFOV = int(s_iBaseFOV * (Vars::Aimbot::ScopeFOV::Scale.Value / 100.f));
		s_iLastWritten = iFOV;
	}
}
