#pragma once
// Pure math of the botox players esp port -- no SDK/engine/ImGui types, unit-tested standalone
// (tests/esp_math_test.cpp). Every function quotes the botox line it reproduces
// (botox hacks/visuals/players/players.cpp).
#include <algorithm>
#include <cmath>
#include <vector>

namespace EspMath
{
	struct V2 { float x, y; };

	// players() :571-583. 0 = cull. Fade runs over the last quarter of max distance.
	inline float DistanceAlpha(float dist, float maxDist, bool fade)
	{
		if (maxDist > 0.f && dist > maxDist) return 0.f;
		if (fade && maxDist > 0.f)
		{
			const float start = maxDist * 0.75f;
			if (dist > start) return 1.f - (dist - start) / (maxDist - start);
		}
		return 1.f;
	}

	// hit_hitboxes / damage_numbers: full for 70% of the life, linear out over the last 30%
	inline float LifeAlpha(float elapsed, float duration)
	{
		if (duration <= 0.f || elapsed < 0.f || elapsed >= duration) return 0.f;
		const float f = elapsed / duration;
		return f < 0.7f ? 1.f : 1.f - (f - 0.7f) / 0.3f;
	}

	// c_color::from_hsb (s = b = 1 as players() uses it); out = r,g,b 0..1
	inline void Hsb(float hue, float* rgb)
	{
		const float h = std::fmod(hue, 1.0f) / (60.0f / 360.0f);
		const int   i = (int)h;
		const float f = h - (float)i, p = 0.f, q = 1.f - f, t = f;
		float r, g, b;
		switch (i)
		{
		case 0:  r = 1.f, g = t, b = p; break;
		case 1:  r = q, g = 1.f, b = p; break;
		case 2:  r = p, g = 1.f, b = t; break;
		case 3:  r = p, g = q, b = 1.f; break;
		case 4:  r = t, g = p, b = 1.f; break;
		default: r = 1.f, g = p, b = q; break;
		}
		rgb[0] = r; rgb[1] = g; rgb[2] = b;
	}
	// players() :723-724: green at full health, red at none
	inline float HealthHue(float factor) { return (factor * 120.f) / 360.f; }

	// players() :717-721: the shown health drains at 100/s, jumps straight up on a heal
	inline float AnimateHealth(float shown, float health, float frameTime)
	{
		return shown > health ? shown - 100.f * frameTime : health;
	}

	// players() :796-798: thickness capped at a quarter of the box's short side
	inline float BoxThickness(float wanted, float w, float h)
	{
		const float cap = std::fmax(std::floor(std::fmin(w, h) / 4.f), 1.f);
		return wanted < 1.f ? 1.f : (wanted > cap ? cap : wanted);
	}

	// players() :747-754: rounding scales with box height (full at 200px), corner boxes cap it
	inline float BoxRounding(float wanted, bool boxOn, bool corner, int w, int h)
	{
		float r = boxOn ? std::fmax(wanted, 0.f) * std::fmin((float)h / 200.f, 1.f) : 0.f;
		if (corner) r = std::fmin(r, (float)std::min(w / 3, h / 5));
		return r;
	}

	// players() :633-640: unit direction on screen toward a target at world delta (local - target),
	// yaw in radians. (0,-1) = straight ahead (screen up).
	inline V2 OofDir(float yawRad, float dx, float dy)
	{
		float x = std::cos(yawRad) * dy - std::sin(yawRad) * dx;
		float y = std::cos(yawRad) * dx + std::sin(yawRad) * dy;
		const float len = std::sqrt(x * x + y * y);
		if (len) { x /= len; y /= len; }
		return { x, y };
	}

	// push_ring_arrow: tip first (imgui fan-fills from it), then left, notch, right
	struct RingArrow { V2 p[4]; };
	inline RingArrow RingArrowPoints(V2 pos, float x, float y, float width, float height, bool interium)
	{
		const float gap = interium ? 0.f : 3.f;
		const V2 base = { pos.x + x * gap, pos.y + y * gap };
		const float hw = width * 0.5f, notch = height * (interium ? 0.2f : 0.15f);
		return { { { base.x + x * height, base.y + y * height },
		           { base.x - y * hw, base.y + x * hw },
		           { base.x + x * notch, base.y + y * notch },
		           { base.x + y * hw, base.y - x * hw } } };
	}

	// players() :1178-1254, the corner health bar: one L-shaped track round the picked corner
	// (rounded if radius > 0), its 1px-longer outline copy, and the fill = the last `factor` of
	// the track's length measured from its bottom end.
	struct CornerBar { std::vector<V2> track, outer, fill; float radius = 0.f, cx = 0.f, cy = 0.f; };
	inline CornerBar CornerBarPaths(float left, float top, float right, float bottom, float armX, float armY,
	                                float baseRounding, float thickness, int pick, float factor)
	{
		CornerBar o;
		const float off = 3.f + thickness * 0.5f;
		o.radius = baseRounding > 0.f ? std::fmin(baseRounding, std::fmin(armX, armY)) + off : 0.f;
		o.cx = left - off; o.cy = bottom + off;
		const bool flipX = pick & 1, flipY = pick >= 2;
		auto place = [&](V2 p) { if (flipX) p.x = left + right - p.x; if (flipY) p.y = top + bottom - p.y; return p; };
		auto build = [&](float ext)
		{
			std::vector<V2> path{ { o.cx, bottom - armY - ext } };
			auto push = [&](V2 p) { if (std::fabs(p.x - path.back().x) + std::fabs(p.y - path.back().y) > 0.01f) path.push_back(p); };
			const int seg = o.radius > 0.f ? 12 : 0;
			for (int i = 0; i <= seg; i++)
			{
				const float a = 3.14159265f - 1.57079633f * (seg ? (float)i / seg : 0.f);
				push({ o.cx + o.radius + o.radius * std::cos(a), o.cy - o.radius + o.radius * std::sin(a) });
			}
			push({ left + armX + ext, o.cy });
			for (auto& p : path) p = place(p);
			return path;
		};
		o.track = build(0.f); o.outer = build(1.f);
		o.fill.push_back(o.track.back());
		float total = 0.f;
		for (size_t i = 1; i < o.track.size(); i++)
			total += std::hypot(o.track[i].x - o.track[i - 1].x, o.track[i].y - o.track[i - 1].y);
		float keep = total * (factor < 0.f ? 0.f : factor > 1.f ? 1.f : factor);
		for (size_t i = o.track.size() - 1; i > 0 && keep > 0.f; i--)
		{
			const V2 a = o.track[i], b = o.track[i - 1];
			const float seg = std::hypot(b.x - a.x, b.y - a.y);
			if (seg >= keep) { o.fill.push_back({ a.x + (b.x - a.x) * (keep / seg), a.y + (b.y - a.y) * (keep / seg) }); break; }
			o.fill.push_back(b);
			keep -= seg;
		}
		return o;
	}
	// players() :1311-1313: the health number's anchor, out along the corner's diagonal
	inline V2 CornerBarTextAnchor(const CornerBar& b, float left, float top, float right, float bottom, float thickness, bool outline, int pick)
	{
		const float reach = b.radius + thickness * 0.5f + (outline ? 1.f : 0.f) + 2.f;
		V2 p = { b.cx + b.radius - reach * 0.70710678f, b.cy - b.radius + reach * 0.70710678f };
		if (pick & 1) p.x = left + right - p.x;
		if (pick >= 2) p.y = top + bottom - p.y;
		return p;
	}
}
