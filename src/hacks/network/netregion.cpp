#include "netregion.h"
#include "../../globals/globals.h"

// All TF2-only net settings / net graph hook features removed 11-03 (DE-TF2 pass).
// See netregion.h for what and why. This file now holds only the interface lookup
// other units' hooks still call.

// MAT_SYSTEM_SURFACE_INTERFACE_VERSION "MatSystemSurface008", confirmed in CSS source
// (css-community-master/mp/src/public/VGuiMatSurface/IMatSystemSurface.h:46) -- same
// interface name the SDK unit already resolves at aim/defs.h (sig_check.md: OK, 1 hit).
// The old TF2-only inline PatternScan (anchored on the TF2 netgraph DrawColoredText call
// site, verified only against tf/bin/x64/client.dll) is gone with that feature; this is
// the plain, engine-generic CreateInterface lookup that was already the fallback path.
void* FindMatSystemSurface()
{
    const char* names[] = { "MatSystemSurface008", "MatSystemSurface007",
                            "VGUI_Surface031", "VGUI_Surface030", nullptr };
    for (int i = 0; names[i]; ++i) {
        if (void* s = GetInterface("vguimatsurface.dll", names[i]))
            return s;
    }
    return nullptr;
}
