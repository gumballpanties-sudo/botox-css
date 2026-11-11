#pragma once
// botox chams layer list (botox hacks/chams/chams.h + menu.cpp chams_dropdown): up to 8 ordered
// material slots, each with its own colour. Pure, no engine types: shared by the menu (dih TU),
// the chams draw (aim TU, aim/chams.cpp) and tests/chams_layers_test.cpp.
//
// Config form is one string per list (config.cpp STR entry), "mat:RRGGBBAA,mat:RRGGBBAA,..." --
// one entry instead of 8 slots + 8 colours each.
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ChamsLayers
{
	// botox n_chams::e_chams_material order/names. TF2 material behind each: aim/chams.cpp.
	enum : int {
		// TwoTone = no material: model's own materials, DIP swaps in the two tone shader (visuals/lsd_chams.cpp)
		// Fresnel..Sky = Atlas-TF2 chams (Neon = its "lsd"), appended so old configs keep their indices
		Original = 0, Flat, Shaded, Wireframe, Glow, Metallic, Pearlescent, Ambient, Gold, Damascus, Crystal, Wet, Lsd, TwoTone,
		Fresnel, Lava, Pearl, Bubble, Neon, Network, Soap, Aurora, Aquarium, Sky,
		Count
	};
	inline constexpr int kMaxLayers = 8;      // botox chams_max_layers
	inline constexpr int kCfgLen    = 160;    // 8 * strlen("11:FFFFFFFF,") + one ":FFFFFFFF" dark = 105, headroom

	inline const char* const kNames[Count] = { "original", "flat", "shaded", "wireframe", "glow", "metallic",
	                                           "pearlescent", "ambient", "gold", "damascus", "crystal", "wet", "lsd", "two tone",
	                                           "fresnel", "lava", "pearl", "bubble", "neon", "network", "soap", "aurora", "aquarium", "sky" };

	// drawn by the shader chams DIP swap (lsd_chams.h ShaderChams_*), carrier material + fx_cham_ps[index]
	inline bool IsShader(int mat) { return mat > Original && mat < Count && mat != Lsd && mat != TwoTone; }

	struct List
	{
		int   slots[kMaxLayers];        // -1 = empty, packed to the front
		float colors[kMaxLayers][4];
		float darks[kMaxLayers][4];     // botox m_chams_*_dark_colors: two tone shadow side, unused by other materials
		int   used;                     // first `used` slots are valid
	};

	inline void SetDefaultDark(float* d) { d[0] = d[1] = d[2] = 0.f; d[3] = 1.f; }   // botox c_color(0, 0, 0, 255)

	inline void Clear(List& l)
	{
		for (int i = 0; i < kMaxLayers; ++i) {
			l.slots[i] = -1;
			l.colors[i][0] = l.colors[i][1] = l.colors[i][2] = l.colors[i][3] = 1.f;
			SetDefaultDark(l.darks[i]);
		}
		l.used = 0;
	}

	inline void FromRgba(unsigned long rgba, float* c)
	{
		c[0] = ((rgba >> 24) & 0xFF) / 255.f;
		c[1] = ((rgba >> 16) & 0xFF) / 255.f;
		c[2] = ((rgba >> 8) & 0xFF) / 255.f;
		c[3] = (rgba & 0xFF) / 255.f;
	}

	// Entry = "mat:RRGGBBAA" plus an optional ":RRGGBBAA" dark colour (two tone writes it).
	// Garbage in -> the bad entry is skipped, never a half-written slot.
	inline void Parse(const char* s, List& out)
	{
		Clear(out);
		if (!s) return;
		while (*s && out.used < kMaxLayers) {
			char* end = nullptr;
			const long mat = strtol(s, &end, 10);
			if (end != s && *end == ':') {
				char* end2 = nullptr;
				const unsigned long rgba = strtoul(end + 1, &end2, 16);
				bool ok = end2 == end + 9 && mat >= 0 && mat < Count;
				unsigned long dark = 0x000000FF;
				if (ok && *end2 == ':') {
					char* end3 = nullptr;
					dark = strtoul(end2 + 1, &end3, 16);
					ok = end3 == end2 + 9;
					end2 = end3;
				}
				if (ok) {
					FromRgba(rgba, out.colors[out.used]);
					FromRgba(dark, out.darks[out.used]);
					out.slots[out.used++] = (int)mat;
				}
				end = end2;
			}
			const char* comma = strchr(end ? end : s, ',');
			if (!comma) break;
			s = comma + 1;
		}
	}

	inline unsigned ToByte(float f) { f = f < 0.f ? 0.f : (f > 1.f ? 1.f : f); return (unsigned)(f * 255.f + 0.5f); }

	inline unsigned ToRgba(const float* c) { return (ToByte(c[0]) << 24) | (ToByte(c[1]) << 16) | (ToByte(c[2]) << 8) | ToByte(c[3]); }

	inline void Format(const List& l, char* buf, size_t size)
	{
		if (!size) return;
		buf[0] = '\0';
		size_t w = 0;
		for (int i = 0; i < l.used && i < kMaxLayers; ++i) {
			const int n = l.slots[i] == TwoTone
				? snprintf(buf + w, size - w, w ? ",%d:%08X:%08X" : "%d:%08X:%08X", l.slots[i], ToRgba(l.colors[i]), ToRgba(l.darks[i]))
				: snprintf(buf + w, size - w, w ? ",%d:%08X" : "%d:%08X", l.slots[i], ToRgba(l.colors[i]));
			if (n <= 0 || (size_t)n >= size - w) { buf[w] = '\0'; break; }
			w += (size_t)n;
		}
	}

	// botox chams_dropdown toggle: present -> removed (later slots shift down); absent -> appended
	// with a white colour. Returns false when the list is full.
	inline bool Toggle(List& l, int mat)
	{
		for (int i = 0; i < l.used; ++i) {
			if (l.slots[i] != mat) continue;
			for (int j = i; j < l.used - 1; ++j) {
				l.slots[j] = l.slots[j + 1];
				for (int k = 0; k < 4; ++k) { l.colors[j][k] = l.colors[j + 1][k]; l.darks[j][k] = l.darks[j + 1][k]; }
			}
			l.slots[--l.used] = -1;
			return true;
		}
		if (l.used >= kMaxLayers) return false;
		l.slots[l.used] = mat;
		for (int k = 0; k < 4; ++k) l.colors[l.used][k] = 1.f;
		SetDefaultDark(l.darks[l.used]);
		++l.used;
		return true;
	}

	inline int IndexOf(const List& l, int mat)
	{
		for (int i = 0; i < l.used; ++i) if (l.slots[i] == mat) return i;
		return -1;
	}

	// botox sound_esp.cpp alpha(): full for the first half of `duration`, then linear to 0.
	inline float SoundAlpha(float elapsed, float duration, bool fade)
	{
		if (duration <= 0.f || elapsed < 0.f || elapsed > duration) return 0.f;
		if (!fade) return 1.f;
		const float hold = duration * 0.5f;
		return elapsed <= hold ? 1.f : 1.f - (elapsed - hold) / (duration - hold);
	}
}
