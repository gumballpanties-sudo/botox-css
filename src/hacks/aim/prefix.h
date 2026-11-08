#pragma once
// Force-included (/FI) into every src/hacks/aim translation unit.
//
// Why: src/hacks/aim/vars.h defines a function-like macro named `Enum(...)` that expands to a
// namespace. The Windows COM headers declare methods called `Enum(` -- objidl.h IMoniker::Enum,
// propidlbase.h IPropertyStorage::Enum -- so any TU that reaches a COM header AFTER Vars.h dies
// with "syntax error: namespace", which dih's include graph (d3d9 + freetype) reaches. Pulling
// the COM headers in first means they are already parsed, and #pragma once keeps the later
// include a no-op.
//
// This changes no aim code -- it only fixes include ORDER.
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <objbase.h>
#include <objidl.h>
#include <d3d9.h>

// Windows.h is now in every aim TU, and winuser.h/wingdi.h define A/W wrapper macros whose
// names collide with Source SDK members. Undef only the ones that actually collide -- the
// Win32 functions themselves are still reachable by their explicit ...A/...W names, which is
// what the aim code calls anyway.
#undef GetProp        // winuser.h vs RecvTable::GetProp / CNetVars::GetProp
