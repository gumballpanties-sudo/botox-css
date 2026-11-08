#pragma once
// FmPlane: brush planes as IEngineTrace::GetBrushInfo returns them, Vector4D (nx, ny, nz, dist),
// normal pointing OUT of the brush, solid where dot(n, p) < dist on every plane.
//
// This file used to also hold the "fake movement" seam-gate math (FmSeam_BoxInBrush/HasFace/
// BrushCounts/BottomEdge/Stick) -- all deleted with fake movement (11-03/11-04 tabs round, no
// botox csgo equivalent). The FmPlane struct stays: ps_map.h (MVPS-owned, whole-map pixel surf
// scan) takes it as a parameter type and is NOT fake movement.
struct FmPlane { float nx, ny, nz, d; };
