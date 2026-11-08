#pragma once
#include "../../globals/globals.h"

// Real CS:S weapon-select icon glyphs (font "Cstrike.ttf", scheme block "CSweapons") for the
// ESP "weapon icon##player" / "icons##dropped weapons icon" rows (botox menu_tab_visuals.cpp
// :237-242 / :560-565). Chars decrypted 11-04 from the live game's own scripts/weapon_*.ctx
// (ICE cipher, key "d7NSuLq2" -- css-community-master game/shared/cstrike/cs_gamerules.h
// GetEncryptionKey(), algorithm css-community-master mathlib/IceKey.cpp -- same public-domain
// reference the engine itself ships), TextureData.weapon_s.character block, one scratch decode
// per weapon, not guessed. Table in esp_weapon_icons.cpp.
//
// Lives in aim/ (not esp/) so it's auto-globbed by CMake like the rest of this unit's aim/*.cpp --
// esp/ currently ships headers only, consumed by aim/esp.cpp, and adding a .cpp there would need
// a CMakeLists.txt line only the orchestrator can add (AGENT_BRIEF "CMAKE" rule).
//
// Render-thread only (esp.cpp's draw queue already runs there). Lazily rasterizes the needed
// glyph via stb_truetype straight off disk (Cstrike.ttf lives at a fixed path under the live
// CS:S install, same hardcoded-path convention render.cpp already uses for verdana/tahoma) and
// uploads one small D3D9 texture per weapon id, cached forever (28 glyphs, never freed per frame
// like the avatar cache -- there's no per-id expiry to track). Game/font file missing -> returns
// nullptr, caller skips the icon (no crash, no fake square).
//
// weaponId: CSWeaponID value (plain int, this header stays includable from outside aim/ too).
// outAspect (optional): glyph width/height, so the caller can draw a non-square icon without
// distortion.
ImTextureID EspWeaponIcon_Get(int weaponId, float* outAspect = nullptr);

// Drops every cached D3D9 texture. Mirrors avatar_cache.h's Avatar_ReleaseAll/OnMapChange
// convention (map-change release only; this codebase's DEFAULT-pool caches don't hook a full
// device-reset path, see avatar_cache.cpp / gif_tex.cpp).
void EspWeaponIcon_ReleaseAll();
