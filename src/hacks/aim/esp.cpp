#include "esp.h"
#include "../esp/esp_math.h"
#include "../esp/esp_fx.h"
#include "../esp/esp_queue.h"
#include "../esp/esp_vars.h"
#include "../visuals/chams_layers.h"   // ChamsLayers::SoundAlpha = botox n_sound_esp::alpha
#include "../hud/avatar_pure.h"        // n_avatar::FriendIdToSteam64 -- plain arithmetic, no Steam/d3d9 types
#include "../menu/cfg/u_esp.h"         // 11-01 vars: edicts group (primitives only)
#include "auto_wall.h"                 // F::AutoWall.GetDamage -- wallbangable flag, CCSPlayer*/CWeaponCSBase* now (AIM3)
#include "grenade_aim.h"               // CssGrenadeThrowVelocity/CssGrenadeBouncePath -- grenade path draw physics
#include "backtrack.h"                 // F::Backtrack -- lag compensated trail (ring of TickRecord)
// NOTE: esp.cpp only pushes ESPC_WPNICON (EspQ::WeaponIcon, pure POD, esp_queue.h) -- the actual
// EspWeaponIcon_Get/D3D9 texture resolve (esp_weapon_icons.h) runs on the render thread inside
// render.cpp's RenderEspQueue, same split as ESPC_AVATAR/Avatar_Get. Do NOT include
// esp_weapon_icons.h here: it pulls globals/globals.h's ConVar/IClientEntityList/etc, which
// collide with aim/defs.h's own versions already loaded in this TU (verified: C2011 redefinition
// across the whole file the moment both are present together).
#include <cfloat>
#include <cstdio>

void DebugLog(const char* fmt, ...);   // dllmain.cpp
bool Chams_SoundStamp(int entindex, float& ms, float* origin);   // aim/chams.cpp, botox g_sound_esp store

// ============================================================================================
// botox hacks/visuals/players/players.cpp + edicts/edicts.cpp + grenade/grenade_path.cpp +
// players/sound_esp/sound_esp.cpp, line for line where CS:S allows. CSS departures (grepped
// css-community-master first, per AGENT_BRIEF):
//   - flags: botox's real e_player_flags (money/armor/helmet/kit/defusing/walking/scoped/
//     flashed/bomb/hostage/immunity/wallbangable) -- NOT the task-brief's guessed "reloading"
//     row. "walking" is IMPOSSIBLE in CS:S (no m_bIsWalking anywhere in the source, that is a
//     CS:GO-only control; dropped). "hostage"/"immunity"/"wallbangable" have no verified data
//     path yet (see per-flag comments + NEEDS FROM OTHERS in the report) -- wired to the menu
//     bit but always false rather than faked.
//   - scoped: CS:S has no m_bIsScoped-style bool at all (weapon_awp.cpp/weapon_scout.cpp just
//     call SetFOV. scoped == m_iFOV() in [1,90) (90 = default FOV, m_iFOV 0 means "use default").
//   - weapon name: CSWeaponID table (css.h), not a localized hud name.
//   - buildings (TF2 sentry/dispenser/tele) removed, no analog -- DrawBomb (planted C4 world
//     text + timer bar) takes its place, botox edicts.cpp bomb().
//   - player loops: entindex 1..GetMaxClients() + IsPlayer() ("CCSPlayer" class name). Slots
//     above maxclients hold weapons/ragdolls -- treating them as players drew boxes on dropped
//     guns / empty spots and read m_hActiveWeapon past a small entity (crash 11-04).
//   - frustum test: SDK::IsOnScreen (botox: engine cull_box)
// ============================================================================================

namespace
{
	inline float FontH() { return g_espQFontH.load(std::memory_order_relaxed); }
	inline uint32_t Black(float a) { return EspColRGBA(0.f, 0.f, 0.f, a); }

	struct SBox { int l, t, r, b, w, h; };

	// c_base_entity::get_bounding_box: the 8 collision-box corners, every one in front of the camera
	bool ScreenBox(const Vec3& o, const Vec3& mn, const Vec3& mx, SBox& box)
	{
		float l = FLT_MAX, t = FLT_MAX, r = -FLT_MAX, b = -FLT_MAX;
		for (int i = 0; i < 8; i++)
		{
			const Vec3 p = { o.x + ((i & 1) ? mx.x : mn.x), o.y + ((i & 2) ? mx.y : mn.y), o.z + ((i & 4) ? mx.z : mn.z) };
			Vec3 s;
			if (!SDK::W2S(p, s))
				return false;
			l = std::min(l, s.x); t = std::min(t, s.y); r = std::max(r, s.x); b = std::max(b, s.y);
		}
		box = { (int)l, (int)t, (int)r, (int)b, (int)r - (int)l, (int)b - (int)t };
		return true;
	}

	// g_utilities.truncate_utf8: first n code points
	std::string TruncateUtf8(const char* s, size_t n)
	{
		size_t i = 0, cps = 0;
		while (s[i] && cps < n)
		{
			const unsigned char c = (unsigned char)s[i];
			i += c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
			++cps;
		}
		std::string out(s, s + i);
		if (s[i])
			out += "...";
		return out;
	}

	// CSWeaponID order (css.h), lower case like botox's own hud-name lowering.
	const char* WeaponName(CSWeaponID eID)
	{
		static const char* const k[WEAPON_MAX] = {
			"none", "p228", "glock", "scout", "he grenade", "xm1014", "c4", "mac10", "aug",
			"smoke grenade", "elite", "five-seven", "ump45", "sg550", "galil", "famas", "usp",
			"awp", "mp5navy", "m249", "m3", "m4a1", "tmp", "g3sg1", "flashbang", "deagle",
			"sg552", "ak47", "knife", "p90", "shieldgun", "kevlar", "assaultsuit", "nvg" };
		return eID > WEAPON_NONE && eID < WEAPON_MAX ? k[eID] : nullptr;
	}

	// mstudiobone_t (studio.h): parent at +0x4, flags at +0xA0, stride 0xD8. No pointers: x86 == x64.
	int BoneParent(const studiohdr_t* h, int i) { return *reinterpret_cast<const int*>(reinterpret_cast<const byte*>(h) + h->boneindex + i * 0xD8 + 0x4); }
	int BoneFlags(const studiohdr_t* h, int i)  { return *reinterpret_cast<const int*>(reinterpret_cast<const byte*>(h) + h->boneindex + i * 0xD8 + 0xA0); }
	Vec3 BonePos(const matrix3x4* m, int b) { return { m[b][0][3], m[b][1][3], m[b][2][3] }; }

	// screen-space polyline from world points, split where a point goes behind the camera (never bridged)
	struct Lines
	{
		std::vector<std::vector<float>> v;
		std::vector<float> cur;
		void Add(const Vec3& w) { Vec3 s; if (SDK::W2S(w, s)) { cur.push_back(s.x); cur.push_back(s.y); } else Flush(); }
		void Flush() { if (cur.size() >= 4) v.push_back(cur); cur.clear(); }
		void Push(uint32_t col, float thick) { Flush(); for (auto& l : v) EspQ::Polyline(l.data(), int(l.size() / 2), col, thick); }
	};

	// push_smooth_skeleton: chains of degree-2 nodes become centripetal Catmull-Rom splines
	struct SkelEdge { int child, parent; Vec3 cpos, ppos; };
	Vec3 SkeletonSpline(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float u)
	{
		auto knot = [](const Vec3& a, const Vec3& b) { return std::max(std::sqrt((b - a).Length()), 1e-3f); };
		auto mix = [](const Vec3& a, const Vec3& b, float ta, float tb, float t) { return a * ((tb - t) / (tb - ta)) + b * ((t - ta) / (tb - ta)); };
		const float t1 = knot(p0, p1), t2 = t1 + knot(p1, p2), t3 = t2 + knot(p2, p3);
		const float t = t1 + (t2 - t1) * u;
		const Vec3 a1 = mix(p0, p1, 0.f, t1, t), a2 = mix(p1, p2, t1, t2, t), a3 = mix(p2, p3, t2, t3, t);
		return mix(mix(a1, a2, 0.f, t2, t), mix(a2, a3, t1, t3, t), t1, t2, t);
	}
	void SmoothSkeleton(const std::vector<SkelEdge>& edges, uint32_t col, float thick)
	{
		constexpr int k_nodes = MAXSTUDIOBONES + 1, k_samples = 8;
		int degree[k_nodes] = {};
		for (const auto& e : edges) degree[e.child]++, degree[e.parent]++;
		std::vector<bool> used(edges.size(), false);
		Lines lines;
		auto emit = [&](const std::vector<Vec3>& p)
		{
			const int m = int(p.size());
			for (int j = 0; j + 1 < m; j++)
			{
				const Vec3 p0 = j > 0 ? p[j - 1] : p[0] * 2.f - p[1];
				const Vec3 p3 = j + 2 < m ? p[j + 2] : p[m - 1] * 2.f - p[m - 2];
				for (int s = 0; s < k_samples; s++)
					lines.Add(SkeletonSpline(p0, p[j], p[j + 1], p3, float(s) / k_samples));
			}
			lines.Add(p[m - 1]);
			lines.Flush();
		};
		for (int pass = 0; pass < 2; pass++)
			for (size_t first = 0; first < edges.size(); first++)
			{
				if (used[first]) continue;
				const auto& e0 = edges[first];
				const bool fromChild = degree[e0.child] != 2;
				if (pass == 0 && !fromChild && degree[e0.parent] == 2) continue;
				int node = fromChild || pass == 1 ? e0.child : e0.parent;
				std::vector<Vec3> chain{ node == e0.child ? e0.cpos : e0.ppos };
				for (size_t cur = first;;)
				{
					used[cur] = true;
					const auto& e = edges[cur];
					const bool fwd = e.child == node;
					node = fwd ? e.parent : e.child;
					chain.push_back(fwd ? e.ppos : e.cpos);
					if (degree[node] != 2) break;
					size_t next = edges.size();
					for (size_t k = 0; k < edges.size(); k++)
						if (!used[k] && (edges[k].child == node || edges[k].parent == node)) next = k;
					if (next == edges.size()) break;
					cur = next;
				}
				emit(chain);
			}
		const float c = thick;
		for (auto& l : lines.v)
		{
			EspQ::Polyline(l.data(), int(l.size() / 2), col, c);
			if (c > 1.5f)
			{
				EspQ::Circle(l[0], l[1], c * 0.5f, col, -1.f);   // thick < 0 = filled (round caps)
				EspQ::Circle(l[l.size() - 2], l[l.size() - 1], c * 0.5f, col, -1.f);
			}
		}
	}

	// hitboxes: 8 world corners each, through the bone matrix (box AABBs -- CS:S hitboxes are
	// boxes/capsules through the same pHitbox->bbmin/bbmax fields, generic either way)
	void HitboxCorners(const studiohdr_t* pHdr, const matrix3x4* aBones, std::vector<std::array<Vec3, 8>>& out)
	{
		auto pSet = pHdr->pHitboxSet(0);
		if (!pSet) return;
		for (int i = 0; i < pSet->numhitboxes; i++)
		{
			auto pBox = pSet->pHitbox(i);
			if (!pBox || pBox->bone < 0 || pBox->bone >= MAXSTUDIOBONES) continue;
			std::array<Vec3, 8> c;
			for (int k = 0; k < 8; k++)
				Math::VectorTransform(Vec3((k & 1) ? pBox->bbmax.x : pBox->bbmin.x, (k & 2) ? pBox->bbmax.y : pBox->bbmin.y,
					(k & 4) ? pBox->bbmax.z : pBox->bbmin.z), aBones[pBox->bone], c[k]);
			out.push_back(c);
		}
	}
	constexpr int k_boxEdges[12][2] = { { 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 }, { 4, 5 }, { 5, 7 }, { 7, 6 }, { 6, 4 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
	void PushHitboxes(const std::vector<std::array<Vec3, 8>>& boxes, uint32_t col, float thick)
	{
		for (const auto& c : boxes)
		{
			Vec3 s[8]; bool ok[8];
			for (int k = 0; k < 8; k++) ok[k] = SDK::W2S(c[k], s[k]);
			for (const auto& e : k_boxEdges)
				if (ok[e[0]] && ok[e[1]])
					EspQ::Line(s[e[0]].x, s[e[0]].y, s[e[1]].x, s[e[1]].y, col, thick);
		}
	}

	// botox players() per-entity layout state: box, fade, the four padding stacks, rounding
	struct Part
	{
		SBox box = {};
		float a = 1.f;
		float pad[4] = {};   // e_padding_direction: left, top, right, bottom
		float round = 0.f;
		Vec3 vOrigin, vMins, vMaxs;
		int key = 0;          // entindex, 0 = labels fixed (esp_declutter.h)
		float prio = 0.f;     // distance: nearer labels hold their spot
	};
	enum { PAD_LEFT, PAD_TOP, PAD_RIGHT, PAD_BOTTOM };
	// label stacks per entity: name row, below-box rows, flag column, health number
	enum { STK_NAME, STK_BOTTOM, STK_FLAGS, STK_HP };
	int StackKey(const Part& p, int kind) { return p.key ? p.key * 4 + kind : 0; }

	void PartRounding(Part& p, bool boxOn)   // :747-754
	{
		p.round = EspMath::BoxRounding(g_espBoxRounding, boxOn, g_espBoxCorner, p.box.r - p.box.l, p.box.b - p.box.t);
	}

	void PartFill(const Part& p)   // :756-772
	{
		const auto& b = p.box;
		if (g_espBoxFillGradient)
			EspQ::Grad((float)b.l, (float)b.t, (float)b.r, (float)b.b, EspCol(g_espBoxFillColor, p.a), EspCol(g_espBoxFillBottomColor, p.a), p.round);
		else
			EspQ::Rect((float)b.l, (float)b.t, (float)b.r, (float)b.b, EspCol(g_espBoxFillColor, p.a), 0, true, p.round, 1.f, 0);
	}

	void PartBox(const Part& p, const float* col)   // :774-855
	{
		const auto& b = p.box;
		const uint8_t outline = !g_espBoxOutline ? 0
			: g_espBoxOutlineSide == 0 ? ESPF_OUT_INNER : g_espBoxOutlineSide == 1 ? ESPF_OUT_OUTER : uint8_t(ESPF_OUT_INNER | ESPF_OUT_OUTER);
		const float oth = std::max(g_espBoxOutlineThickness, 1.f);
		const float th = EspMath::BoxThickness(g_espBoxThickness, float(b.r - b.l), float(b.b - b.t));
		const uint32_t c = EspCol(col, p.a), oc = EspCol(g_espBoxOutlineColor, p.a);

		if (g_espBox3d)
		{
			Vec3 s[8];
			for (int i = 0; i < 8; i++)
			{
				const Vec3 w = { p.vOrigin.x + ((i & 1) ? p.vMaxs.x : p.vMins.x), p.vOrigin.y + ((i & 2) ? p.vMaxs.y : p.vMins.y),
					p.vOrigin.z + ((i & 4) ? p.vMaxs.z : p.vMins.z) };
				if (!SDK::W2S(w, s[i])) return;
			}
			if (outline)
				for (const auto& e : k_boxEdges)
					EspQ::Line(s[e[0]].x, s[e[0]].y, s[e[1]].x, s[e[1]].y, oc, th + oth * 2.f);
			for (const auto& e : k_boxEdges)
				EspQ::Line(s[e[0]].x, s[e[0]].y, s[e[1]].x, s[e[1]].y, c, th);
		}
		else if (!g_espBoxCorner)
			EspQ::Rect((float)b.l, (float)b.t, (float)b.r, (float)b.b, c, oc, false, p.round, th, outline, oth);
		else
		{
			const float inset = (th - 1.f) * 0.5f, cr = std::max(p.round - inset, 0.f);
			if (outline)
				EspQ::CornerRect(b.l + inset, b.t + inset, b.r - inset, b.b - inset, oc, th + oth, cr);
			EspQ::CornerRect(b.l + inset, b.t + inset, b.r - inset, b.b - inset, c, th, cr);
		}
	}

	// :860-928. Returns the name's top y (the name avatar centres on it), NAN if not drawn.
	float PartName(Part& p, const std::string& s, const float* col)
	{
		const auto& b = p.box;
		const float h = FontH();
		const float x = b.l + b.w * 0.5f;
		float y;
		switch (g_espNamePosition)
		{
		case 1:  y = b.b + 2 + p.pad[PAD_BOTTOM]; break;
		case 2:  y = b.t + 2.f; break;
		case 3:  y = b.b - 2 - h; break;
		default: y = b.t - 8 - h; break;
		}
		EspQ::Text(x, y, s.c_str(), EspCol(col, p.a), Black(p.a), 0.5f);
		if (g_espNamePosition == 0) p.pad[PAD_TOP] -= h - 20;
		else if (g_espNamePosition == 1) p.pad[PAD_BOTTOM] += h + 1.f;
		return y;
	}

	// below-the-box text row (health number standalone, weapon name, distance): centred, stacks
	void PartBottomText(Part& p, const char* s, uint32_t col)
	{
		EspQ::Stack _s(StackKey(p, STK_BOTTOM), false, p.prio);
		EspQ::Text(p.box.l + p.box.w * 0.5f, p.box.b + 2 + p.pad[PAD_BOTTOM], s, col, Black(p.a), 0.5f);
		p.pad[PAD_BOTTOM] += FontH() + 1.f;
	}

	void PartDistance(Part& p, float dist)   // :1077-1105
	{
		char t[16];
		switch (g_espDistanceUnit)
		{
		case 1:  snprintf(t, sizeof(t), "%.0fu", dist); break;
		case 2:  snprintf(t, sizeof(t), "%.0fft", dist / 16.f); break;
		default: snprintf(t, sizeof(t), "%.0fm", dist / 52.49f); break;
		}
		PartBottomText(p, t, EspCol(g_espDistanceColor, p.a));
	}

	struct FlagRow { const char* label; bool alert; };
	void PartFlags(const Part& p, const FlagRow* rows, int n)   // :1160-1175
	{
		EspQ::Stack _s(StackKey(p, STK_FLAGS), false, p.prio);
		float y = (float)p.box.t;
		for (int i = 0; i < n; i++)
		{
			EspQ::Text(p.box.r + 4.f, y, rows[i].label, EspCol(rows[i].alert ? g_espFlagsAlertColor : g_espFlagsColor, p.a), Black(p.a));
			y += FontH() + 1.f;
		}
	}

	// :1178-1407. factor = animated health / max; hp = the number text. boxOn = botox m_players_box,
	// followText = health number on, style "follow health bar".
	void PartHealthBar(const Part& p, float factor, const char* hp, bool boxOn, bool followText)
	{
		const auto& b = p.box;
		float hue[4] = { 0, 0, 0, 1 };
		EspMath::Hsb(EspMath::HealthHue(factor), hue);
		const float th = g_espHealthBarThickness, h = FontH();
		const bool outline = g_espHealthBarOutline;
		const float white[4] = { 1, 1, 1, 1 };
		const uint32_t followCol = EspCol(g_espHealthTextCustomColor ? g_espHealthTextColor : white, p.a);

		if (g_espHealthBarSide == 4)
		{
			const int bw = b.r - b.l, bh = b.b - b.t;
			const float cornerSize = std::clamp(g_espHealthBarCornerSize, 0.1f, 1.f);
			const float evenArm = boxOn ? (float)std::min(bw / 3, bh / 5) : (float)std::min(bw, bh) * cornerSize;
			const bool rideBox = boxOn && g_espBoxCorner && !g_espBox3d;
			const float armX = rideBox ? (float)(bw / 3) : evenArm, armY = rideBox ? (float)(bh / 5) : evenArm;
			const float baseRound = boxOn ? p.round
				: std::max(g_espHealthBarCornerRounding, 0.f) * std::min((float)(b.b - b.t) / 200.f, 1.f);
			const int pick = std::clamp(g_espHealthBarCornerPick, 0, 3);
			const auto bar = EspMath::CornerBarPaths((float)b.l, (float)b.t, (float)b.r, (float)b.b, armX, armY, baseRound, th, pick, factor);

			float yTop = FLT_MAX, yBot = -FLT_MAX;
			for (const auto& q : bar.track) { yTop = std::min(yTop, q.y); yBot = std::max(yBot, q.y); }
			yTop -= th * 0.5f; yBot += th * 0.5f;

			auto flat = [](const std::vector<EspMath::V2>& v) { std::vector<float> f; for (auto& q : v) { f.push_back(q.x); f.push_back(q.y); } return f; };
			if (outline) { auto o = flat(bar.outer); EspQ::Polyline(o.data(), int(bar.outer.size()), Black(p.a), th + 2.f); }
			{ auto t = flat(bar.track); EspQ::Polyline(t.data(), int(bar.track.size()), EspCol(g_espHealthBarBgColor, p.a), th); }
			if (bar.fill.size() >= 2)
			{
				auto f = flat(bar.fill);
				const uint32_t fillCol = EspCol(g_espHealthBarCustomColor ? g_espHealthBarColor : hue, p.a);
				if (g_espHealthBarCustomColor && g_espHealthBarGradient)
					EspQ::PolylineGrad(f.data(), int(bar.fill.size()), fillCol, EspCol(g_espHealthBarBottomColor, p.a), th, yTop, yBot);
				else
					EspQ::Polyline(f.data(), int(bar.fill.size()), fillCol, th);
			}
			if (followText)
			{
				EspQ::Stack _s(StackKey(p, STK_HP), true, p.prio);
				const auto tip = EspMath::CornerBarTextAnchor(bar, (float)b.l, (float)b.t, (float)b.r, (float)b.b, th, outline, pick);
				const bool flipX = pick & 1, flipY = pick >= 2;
				EspQ::Text(tip.x, flipY ? tip.y - h : tip.y, hp, followCol, Black(p.a), flipX ? 0.f : 1.f);
			}
			return;
		}

		const int side = g_espHealthBarSide;
		const bool vertical = side == 0 || side == 1;
		float x0, y0, x1, y1;
		switch (side)
		{
		case 1:  x0 = b.r + 3.f; y0 = (float)b.t; x1 = b.r + 3.f + th; y1 = (float)b.b; break;
		case 2:  x0 = (float)b.l; y0 = b.t - 3.f - th; x1 = (float)b.r; y1 = b.t - 3.f; break;
		case 3:  x0 = (float)b.l; y0 = b.b + 3.f; x1 = (float)b.r; y1 = b.b + 3.f + th; break;
		default: x0 = b.l - 3.f - th; y0 = (float)b.t; x1 = b.l - 3.f; y1 = (float)b.b; break;
		}
		float fx0 = x0, fy0 = y0, fx1 = x1, fy1 = y1;
		if (vertical) fy0 = y1 - (y1 - y0) * factor;
		else          fx1 = x0 + (x1 - x0) * factor;

		const uint8_t ofl = outline ? ESPF_OUT_OUTER : 0;
		const bool gradient = g_espHealthBarCustomColor && g_espHealthBarGradient;
		const uint32_t top = EspCol(g_espHealthBarColor, p.a);
		EspQ::Rect(x0, y0, x1, y1, EspCol(g_espHealthBarBgColor, p.a), Black(p.a), true, 0.f, 1.f, ofl);
		EspQ::Rect(fx0, fy0, fx1, fy1, gradient ? 0u : g_espHealthBarCustomColor ? top : EspCol(hue, p.a), Black(p.a), true, 0.f, 1.f, ofl);
		if (gradient)
		{	// gradient pinned to the track, the fill reveals it
			const float t = std::clamp((fy0 - y0) / std::max(y1 - y0, 1.f), 0.f, 1.f);
			const float* tc = g_espHealthBarColor; const float* bc = g_espHealthBarBottomColor;
			const float mixed[4] = { tc[0] + (bc[0] - tc[0]) * t, tc[1] + (bc[1] - tc[1]) * t, tc[2] + (bc[2] - tc[2]) * t, tc[3] + (bc[3] - tc[3]) * t };
			EspQ::Grad(fx0, fy0, fx1, fy1, EspCol(mixed, p.a), EspCol(bc, p.a));
		}
		if (followText)
		{
			EspQ::Stack _s(StackKey(p, STK_HP), true, p.prio);
			if (vertical)
				EspQ::Text(side == 1 ? x1 + 2.f : x0 - 2.f, fy0, hp, followCol, Black(p.a), side == 1 ? 0.f : 1.f);
			else
				EspQ::Text(fx1, y0 - h, hp, followCol, Black(p.a), 0.5f);
		}
	}

	// botox n_sound_esp::player_gate, on the shared chams sound stamps
	float SoundGate(CCSPlayer* pPlayer, CCSPlayer* pLocal)
	{
		if (!g_espSoundOnly || pPlayer->m_iTeamNum() == pLocal->m_iTeamNum())
			return 1.f;
		float ms, o[3];
		if (!Chams_SoundStamp(pPlayer->entindex(), ms, o))
			return 0.f;
		if (g_chamsSoundMaxDistance > 0.f && (Vec3(o[0], o[1], o[2]) - pLocal->GetAbsOrigin()).Length() > g_chamsSoundMaxDistance)
			return 0.f;
		return ChamsLayers::SoundAlpha((EspFx_NowMs() - ms) / 1000.f, g_espSoundDuration, g_espSoundFade);
	}

	uint64_t SteamId(int iIndex)
	{
		player_info_t tInfo;
		if (!I::EngineClient->GetPlayerInfo(iIndex, &tInfo) || tInfo.fakeplayer)
			return 0;
		return n_avatar::FriendIdToSteam64(tInfo.friendsID);
	}

	// players() :629-701 / sound_arrows: ring position round the screen centre
	EspMath::V2 RingPos(float x, float y)
	{
		const float W = (float)H::Draw.m_nScreenW, Hh = (float)H::Draw.m_nScreenH;
		const float ring = (float)g_espOofDistance;
		const float rx = g_espOofAspect && Hh > 0.f ? ring * (W / Hh) : ring;
		return { W * 0.5f + x * rx, Hh * 0.5f + y * ring };
	}
	void PushRingArrow(EspMath::V2 pos, float x, float y, uint32_t col)
	{
		const bool interium = g_espOofStyle == 1;
		const auto a = EspMath::RingArrowPoints(pos, x, y, g_espOofWidth, g_espOofHeight, interium);
		const float pts[8] = { a.p[0].x, a.p[0].y, a.p[1].x, a.p[1].y, a.p[2].x, a.p[2].y, a.p[3].x, a.p[3].y };
		if (!interium)
		{
			EspQ::Poly(pts, 4, col);
			return;
		}
		// pyramid seen from above minus its back face: see-through notched arrow, rim, ridge notch -> tip
		const uint32_t alpha = (uint32_t)(((col >> 24) & 0xffu) * 0.45f);
		EspQ::Poly(pts, 4, (col & 0x00ffffffu) | (alpha << 24), true, col, 1.5f);
		EspQ::Line(a.p[2].x, a.p[2].y, a.p[0].x, a.p[0].y, col, 1.5f);
	}

	// botox impl_t::rect / corner_rect for the edicts boxes (1 px, botox draws them unscaled)
	void EdictBox(const SBox& b, uint32_t col, uint32_t outline, bool corner, bool drawOutline)
	{
		if (!corner)
			EspQ::Rect((float)b.l, (float)b.t, (float)b.r, (float)b.b, col, outline, false, 0.f, 1.f,
				drawOutline ? uint8_t(ESPF_OUT_INNER | ESPF_OUT_OUTER) : uint8_t(0));
		else
		{
			if (drawOutline) EspQ::CornerRect((float)b.l, (float)b.t, (float)b.r, (float)b.b, outline, 2.f, 0.f);
			EspQ::CornerRect((float)b.l, (float)b.t, (float)b.r, (float)b.b, col, 1.f, 0.f);
		}
	}

	// "models/weapons/c_models/c_awp/c_awp.mdl" -> "awp"
	std::string ModelStem(CBaseEntity* pEnt)
	{
		const auto pModel = pEnt ? pEnt->GetModel() : nullptr;
		const char* n = pModel ? I::ModelInfoClient->GetModelName(pModel) : nullptr;
		if (!n) return {};
		const char* slash = strrchr(n, '/');
		std::string s = slash ? slash + 1 : n;
		if (s.size() > 4 && s.compare(s.size() - 4, 4, ".mdl") == 0) s.resize(s.size() - 4);
		if (s.size() > 2 && (s[0] == 'c' || s[0] == 'w') && s[1] == '_') s.erase(0, 2);
		return s;
	}

	std::string StemName(std::string s)
	{
		for (char& c : s) if (c == '_') c = ' ';
		return s;
	}

	// Weapon C4 carry check: botox entity->get_weapons_handle() loop,
	// compared against item_definition_index == weapon_c4 there; here the handle array is typed
	// CHandle<CTFWeaponBase> (defs.h CBaseCombatCharacter::m_hMyWeapons, shared SDK-wide name),
	// reinterpreted as CWeaponCSBase -- same technique esp.cpp already used for CTFWeaponBase.
	bool IsCarryingC4(CCSPlayer* pPlayer)
	{
		auto& weapons = pPlayer->m_hMyWeapons();   // CHandle<CTFWeaponBase>[MAX_WEAPONS] : public CBaseHandle
		for (int i = 0; i < MAX_WEAPONS; i++)
		{
			auto pEnt = I::ClientEntityList->GetClientEntityFromHandle(weapons[i]);
			auto pWpn = pEnt ? pEnt->As<CWeaponCSBase>() : nullptr;
			if (pWpn && pWpn->GetWeaponID() == WEAPON_C4)
				return true;
		}
		return false;
	}

	// EF_HOSTAGE: CHostage::m_leader (css.h:366, CNetworkVar(EHANDLE, m_leader), confirmed a real
	// networked field -- AIM3's class-name fix). Network name is the bare server class ("CHostage",
	// not "DT_CHostage" -- same fix as the grenade class check above).
	bool IsGrabbingHostage(CCSPlayer* pPlayer)
	{
		const int iIndex = pPlayer->entindex();
		const int iHighest = I::ClientEntityList->GetHighestEntityIndex();
		for (int i = 65; i <= iHighest; i++)
		{
			auto pClient = I::ClientEntityList->GetClientEntity(i);
			auto pEnt = pClient ? pClient->As<CBaseEntity>() : nullptr;
			if (!pEnt || pEnt->IsDormant())
				continue;
			auto pClass = pEnt->GetClientClass();
			if (!pClass || strcmp(pClass->GetName(), "CHostage") != 0)
				continue;
			auto pHostage = pEnt->As<CHostage>();
			if (pHostage && !pHostage->m_isRescued() && pHostage->LeaderIndex() == iIndex)
				return true;
		}
		return false;
	}

	// EF_WALLBANGABLE: F::AutoWall.GetDamage now takes CCSPlayer*/CWeaponCSBase* (AIM3's class-
	// name migration), matching H::Entities.GetLocal()/GetWeapon() exactly -- no cast needed.
	// A couple of hitbox-ish points (head, chest), same spirit as botox's own head/chest/stomach/
	// pelvis loop (players.cpp:419), without the cached bone-hitbox lookup that loop used.
	bool IsWallbangable(CCSPlayer* pLocal, CCSPlayer* pPlayer)
	{
		auto pWeapon = H::Entities.GetWeapon();
		if (!pLocal->IsAlive() || !pWeapon)
			return false;
		if (F::AutoWall.GetDamage(pLocal, pWeapon, pPlayer->GetEyePosition()) >= 1.f)
			return true;
		const Vec3 vMid = pPlayer->GetAbsOrigin() + Vec3(0.f, 0.f, (pPlayer->m_vecMins().z + pPlayer->m_vecMaxs().z) * 0.5f);
		return F::AutoWall.GetDamage(pLocal, pWeapon, vMid) >= 1.f;
	}
}

void CEsp::OnPlayerHurt(CCSPlayer* pVictim, int iDamage)
{
	if (!Esp_On() || iDamage <= 0)
		return;
	const int iVictim = pVictim->entindex();
	const float flNow = I::GlobalVars->realtime;

	// botox hitboxes_on_hurt
	if (g_espHitboxes && g_espHitboxesMode == 1)
	{
		matrix3x4 aBones[MAXSTUDIOBONES];
		const auto pModel = pVictim->GetModel();
		const auto pHdr = pModel ? I::ModelInfoClient->GetStudiomodel(pModel) : nullptr;
		if (pHdr && pVictim->SetupBones(aBones, MAXSTUDIOBONES, BONE_USED_BY_HITBOX, I::GlobalVars->curtime))
		{
			HitBoxes t; t.flTime = flNow;
			HitboxCorners(pHdr, aBones, t.vBoxes);
			if (!t.vBoxes.empty())
			{
				if (m_vHitBoxes.size() >= 16) m_vHitBoxes.erase(m_vHitBoxes.begin());
				m_vHitBoxes.push_back(std::move(t));
			}
		}
	}

	// botox on_player_hurt (damage origin: CS player_hurt has no hit group here either -> same
	// botox fallback, origin + 50)
	if (!g_espDamageNumbers)
		return;
	if (g_espDmgStack)
		for (auto& e : m_vDamage)
			if (e.iVictim == iVictim && flNow - e.flTime <= 0.35f)
			{
				e.iDamage += iDamage;
				e.flTime = flNow;
				return;
			}
	if (m_vDamage.size() >= 32) m_vDamage.erase(m_vDamage.begin());
	Vec3 vOrigin = pVictim->GetAbsOrigin(); vOrigin.z += 50.f;
	m_vDamage.push_back({ vOrigin, iDamage, iVictim, flNow });
}

void CEsp::DrawDamageNumbers()   // botox damage_numbers
{
	if (m_vDamage.empty()) return;
	if (!g_espDamageNumbers) { m_vDamage.clear(); return; }
	const float flNow = I::GlobalVars->realtime, h = FontH();
	char t[16];
	for (auto it = m_vDamage.begin(); it != m_vDamage.end();)
	{
		const float el = flNow - it->flTime;
		if (el < 0.f || el >= g_espDmgDuration || g_espDmgDuration <= 0.f) { it = m_vDamage.erase(it); continue; }
		Vec3 w = it->vOrigin; w.z += el * g_espDmgSpeed;
		Vec3 s;
		if (SDK::W2S(w, s))
		{
			const float a = EspMath::LifeAlpha(el, g_espDmgDuration);
			snprintf(t, sizeof(t), "%d", it->iDamage);
			EspQ::Text(s.x, s.y - h * 0.5f, t, EspCol(g_espDmgColor, a), Black(a), 0.5f);
		}
		++it;
	}
}

void CEsp::DrawHitHitboxes()   // botox hit_hitboxes
{
	if (m_vHitBoxes.empty()) return;
	if (!g_espHitboxes || g_espHitboxesMode != 1) { m_vHitBoxes.clear(); return; }
	const float dur = std::max(g_espHitboxesDuration, 0.1f), flNow = I::GlobalVars->realtime;
	for (auto it = m_vHitBoxes.begin(); it != m_vHitBoxes.end();)
	{
		const float el = flNow - it->flTime;
		if (el < 0.f || el >= dur) { it = m_vHitBoxes.erase(it); continue; }
		PushHitboxes(it->vBoxes, EspCol(g_espHitboxColor, EspMath::LifeAlpha(el, dur)), g_espHitboxesThickness);
		++it;
	}
}

void CEsp::DrawPlayers(CCSPlayer* pLocal)
{
	const Vec3 vView = I::EngineClient->GetViewAngles();
	const float flYaw = DEG2RAD(vView.y);
	const Vec3 vLocalOrigin = pLocal->GetAbsOrigin(), vEye = pLocal->GetEyePosition();

	const int nMax = std::min(I::EngineClient->GetMaxClients(), 64);
	for (int iIndex = 1; iIndex <= nMax; iIndex++)
	{
		auto pClient = I::ClientEntityList->GetClientEntity(iIndex);
		auto pPlayer = pClient ? pClient->As<CCSPlayer>() : nullptr;
		if (!pPlayer || pPlayer == pLocal || !pPlayer->IsPlayer() || pPlayer->IsDormant() || !pPlayer->IsAlive() || pPlayer->m_iHealth() <= 0)
			continue;
		const bool bTeam = pPlayer->m_iTeamNum() == pLocal->m_iTeamNum();
		if (bTeam && !g_espTeammates)
			continue;

		const Vec3 vOrigin = pPlayer->GetAbsOrigin();
		const float flDist = vLocalOrigin.DistTo(vOrigin);
		float a = EspMath::DistanceAlpha(flDist, g_espMaxDistance, g_espDistanceFade) * SoundGate(pPlayer, pLocal);
		if (a <= 0.f)
			continue;
		a = std::min(a, 1.f);

		// visibility, traced once on first ask (botox is_player_visible: our eye -> his networked eye)
		int iVis = -1;
		auto visible = [&]() { if (iVis < 0) iVis = SDK::VisPos(pLocal, pPlayer, vEye, vOrigin + pPlayer->m_vecViewOffset()) ? 1 : 0; return iVis == 1; };

		// ---- out of fov arrows (players() :628-701); screen-space ring, never shown in esp preview ----
		if (g_espOofArrows && g_espOofType != 1 && !SDK::IsOnScreen(pPlayer, vOrigin))
		{
			const Vec3 d = vLocalOrigin - vOrigin;
			const auto dir = EspMath::OofDir(flYaw, d.x, d.y);
			const uint32_t col = EspCol(visible() ? g_espOofColor : g_espOofWallColor, a);
			const auto pos = RingPos(dir.x, dir.y);
			const uint64_t sid = g_espOofAvatar ? SteamId(iIndex) : 0;
			if (sid)
			{
				const float size = std::min(g_espOofWidth, g_espOofHeight), r = size * 0.5f;
				EspQ::Avatar(pos.x - r, pos.y - r, pos.x + r, pos.y + r, sid, a, r);
				EspQ::Circle(pos.x, pos.y, r, col, 1.5f);
			}
			else
				PushRingArrow(pos, dir.x, dir.y, col);
		}

		// ---- gather: everything DrawPlayer needs, read once here (botox players() gather half).
		// ScreenBox runs twice per player (here, to gate ++m_nDrawn without recomputing it inside
		// the shared DrawPlayer; again inside DrawPlayer itself, off the struct's raw origin/mins/
		// maxs). Cheap, deterministic, same result -- and CSS's ScreenBox never reads I::EngineClient
		// in the first place (axis-aligned corners only, no tight-hull yaw trim), so unlike the TF2
		// port there is no view-yaw field to carry on the struct for it. ----
		SBox box;
		if (!ScreenBox(vOrigin, pPlayer->m_vecMins(), pPlayer->m_vecMaxs(), box))
			continue;
		++m_nDrawn;

		EspPlayer player;
		player.m_alpha = a;
		player.m_origin = vOrigin; player.m_mins = pPlayer->m_vecMins(); player.m_maxs = pPlayer->m_vecMaxs();
		player.m_distance = flDist;
		player.m_index = iIndex;

		const int iHealth = pPlayer->m_iHealth();
		const int iMaxHealth = 100;   // CS:S players: fixed 100 max HP, no player-resource lookup needed
		player.m_health = iHealth;

		m_flAnimHealth[iIndex] = EspMath::AnimateHealth(m_flAnimHealth[iIndex], (float)iHealth, I::GlobalVars->frametime);
		player.m_healthFactor = std::clamp(m_flAnimHealth[iIndex] / (float)iMaxHealth, 0.f, 1.f);

		player_info_t tInfo{};
		std::string sName;
		if (I::EngineClient->GetPlayerInfo(iIndex, &tInfo))
			sName = TruncateUtf8(tInfo.name, 24);
		if (tInfo.fakeplayer)
			sName.insert(0, "[bot] ");
		player.m_name = std::move(sName);

		player.m_steamId = g_espAvatar ? SteamId(iIndex) : 0;

		if (auto pWeapon = pPlayer->m_hActiveWeapon()->As<CWeaponCSBase>())
		{
			player.m_weaponId = pWeapon->GetWeaponID();
			// :989-1021 ammo bar: guns only (a weapon with a clip). g_espAmmoBar also gates the
			// m_flAmmo animation below (shared mutable per-entity state), not just the draw toggle --
			// stays here so the state doesn't drift while the bar happens to be off, same as live.
			const int iMaxClip = pWeapon->GetMaxClip1();
			if (g_espAmmoBar && iMaxClip > 0 && pWeapon->m_iClip1() >= 0)
			{
				const float ammo = (float)pWeapon->m_iClip1();
				m_flAmmo[iIndex] = m_flAmmo[iIndex] > ammo ? m_flAmmo[iIndex] - 2.f * I::GlobalVars->frametime : ammo;
				player.m_hasAmmo = true;
				player.m_ammoFactor = std::clamp(m_flAmmo[iIndex] / (float)iMaxClip, 0.f, 1.f);
			}
		}

		if (g_espFlags)
		{
			player.m_money = pPlayer->Account();
			// EF_HOSTAGE/EF_WALLBANGABLE: unblocked by AIM3's class-name fix (network names are
			// bare server class names -- CCSPlayer, CHostage, CWeaponCSBase -- not DT_-prefixed);
			// F::AutoWall.GetDamage/CHostage::m_leader both now match what's available here.
			// EF_IMMUNITY still false, genuinely unrelated to that fix: botox's is_god() is a
			// behavioural detector (shots landing with no matching damage event) needing a
			// FireBullet-impact + player-hurt hook not owned by esp.cpp, and has_immunity() is
			// CS:GO gun-game trade-up immunity -- CS:S has no gun-game mode, confirmed no
			// equivalent exists. Wired to the menu bit, always false rather than faked.
			const bool bOn[EF_COUNT] = {
				pPlayer->Account() > 0, pPlayer->Armor() > 0, pPlayer->HasHelmet(), pPlayer->HasDefuser(),
				pPlayer->IsDefusing(), pPlayer->m_iFOV() > 0 && pPlayer->m_iFOV() < 90, pPlayer->m_flFlashDuration() > 0.f,
				IsCarryingC4(pPlayer), IsGrabbingHostage(pPlayer), false,
				!bTeam && (g_espFlagList & (1 << EF_WALLBANGABLE)) && IsWallbangable(pLocal, pPlayer) };
			for (int i = 0; i < EF_COUNT; i++)
				player.m_flags[i] = bOn[i];
		}

		// ---- bones: skeleton, hitboxes, head avatar, backtrack trail (botox players() :833-873) ----
		const bool bWantBones = g_espSkeleton || g_espHitboxes && g_espHitboxesMode == 0 || player.m_steamId && g_espAvatarPosition == 3 ||
		                        g_espBacktrackTrail;
		const auto pModel = bWantBones ? pPlayer->GetModel() : nullptr;
		const auto pHdr = pModel ? I::ModelInfoClient->GetStudiomodel(pModel) : nullptr;
		matrix3x4 aBones[MAXSTUDIOBONES];   // stack local: outlives the DrawPlayer(player) call below, same scope
		if (pHdr && pPlayer->SetupBones(aBones, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, I::GlobalVars->curtime))
		{
			player.m_studio = pHdr;
			player.m_bones = aBones;
			// botox players.cpp:1517-1539 "lag compensated trail": the head of every record still in the ring
			// (aim/backtrack.h, same ring aimbot/chams read; m_bInvalid = botox !m_valid)
			if (g_espBacktrackTrail)
			{
				auto pTrailSet = pHdr->pHitboxSet(0);
				auto pTrailHead = pTrailSet && pTrailSet->numhitboxes > 12 ? pTrailSet->pHitbox(12) : nullptr;   // CS:S set 0: 12 = Bip01_Head1 (0 = pelvis)
				auto pRecords = F::Backtrack.GetRecords(iIndex);
				if (pTrailHead && pTrailHead->bone >= 0 && pTrailHead->bone < MAXSTUDIOBONES && pRecords)
				{
					const Vec3 vMid = (pTrailHead->bbmin + pTrailHead->bbmax) * 0.5f;
					for (int ri = 0; ri < F::Backtrack.Count(); ri++)
					{
						const TickRecord& r = pRecords[ri];
						if (r.m_flSimTime <= 0.f || r.m_bInvalid)
							continue;
						Vec3 vHeadPos;
						Math::VectorTransform(vMid, r.m_aBones[pTrailHead->bone], vHeadPos);
						player.m_trail.push_back(vHeadPos);
					}
				}
			}

			auto pHeadSet = pHdr->pHitboxSet(0);
			auto pHead = pHeadSet && pHeadSet->numhitboxes > 12 ? pHeadSet->pHitbox(12) : nullptr;   // CS:S set 0: 12 = Bip01_Head1 (0 = pelvis)
			if (player.m_steamId && g_espAvatarPosition == 3 && pHead && pHead->bone >= 0 && pHead->bone < MAXSTUDIOBONES)   // :1549-1583 cover head
			{
				const Vec3 e = pHead->bbmax - pHead->bbmin;
				player.m_headRadius = std::max(std::max(e.x, e.y), e.z) * 0.5f;
				Math::VectorTransform((pHead->bbmin + pHead->bbmax) * 0.5f, aBones[pHead->bone], player.m_head);
				Math::AngleVectors(vView, nullptr, &player.m_viewRight);
				player.m_hasHead = true;
			}
		}

		// trace only when a visibility colour reads it: box / name / skeleton, same shared cache as oof arrows
		const bool wantVis = (g_espBox && g_espBoxVisColors) || (g_espName && g_espNameVisColors) ||
			(player.m_bones && g_espSkeleton && g_espSkeletonVisColors);
		player.m_visible = wantVis && visible();

		DrawPlayer(player);
	}
}

void CEsp::DrawPlayer(const EspPlayer& player)
{
	// box (:636-665 ScreenBox, off the struct's raw hull -- CSS's ScreenBox is already entity/
	// engine free, so this is identical whether player came from the live loop above or esp
	// preview's own fake one)
	SBox box;
	if (!ScreenBox(player.m_origin, player.m_mins, player.m_maxs, box))
		return;

	char hp[16];
	snprintf(hp, sizeof(hp), g_espHealthSuffix ? "%dhp" : "%d", player.m_health);
	const float factor = player.m_healthFactor;
	float hue[4] = { 0, 0, 0, 1 };
	EspMath::Hsb(EspMath::HealthHue(factor), hue);

	Part p;
	p.box = box; p.a = player.m_alpha; p.vOrigin = player.m_origin; p.vMins = player.m_mins; p.vMaxs = player.m_maxs;
	p.key = player.m_index; p.prio = player.m_distance;
	const auto& b = p.box;
	const float a = player.m_alpha;
	const uint64_t sid = player.m_steamId;

	PartRounding(p, g_espBox);
	if (g_espBoxFill)
		PartFill(p);
	if (g_espBox)
		PartBox(p, g_espBoxVisColors ? (player.m_visible ? g_espBoxVisibleColor : g_espBoxInvisibleColor) : g_espBoxColor);

	if (g_espName)
	{
		// name below the box = first row of the below-box stack; the name avatar rides with the name
		EspQ::Stack _s(StackKey(p, g_espNamePosition == 1 ? STK_BOTTOM : STK_NAME), g_espNamePosition == 0, p.prio);
		const float y = PartName(p, player.m_name, g_espNameVisColors ? (player.m_visible ? g_espNameVisibleColor : g_espNameInvisibleColor) : g_espNameColor);
		if (sid && g_espAvatarPosition == 0 && !player.m_name.empty())   // an empty name queues no text to sit left of
		{
			const float size = g_espAvatarSize;
			const float ay = y + (FontH() - size) * 0.5f;
			EspQ::Avatar(-size - 3.f, ay, -3.f, ay + size, sid, a, size * 0.5f, true);
		}
	}

	if (g_espHealthText && g_espHealthTextStyle == 0)
		PartBottomText(p, hp, EspCol(g_espHealthTextCustomColor ? g_espHealthTextColor : hue, a));

	if (player.m_hasAmmo)   // :989-1021 ammo bar; the clip/max-clip gather already applied g_espAmmoBar
	{
		EspQ::Rect((float)b.l, b.b + 3.f, (float)(b.l + b.w), b.b + 5.f, Black(a), Black(a), false, 0.f, 1.f, ESPF_OUT_OUTER);
		EspQ::Rect((float)b.l, b.b + 3.f, b.l + b.w * player.m_ammoFactor, b.b + 5.f, EspCol(g_espAmmoBarColor, a), Black(a), false, 0.f, 1.f, ESPF_OUT_OUTER);
		p.pad[PAD_BOTTOM] += 6.f;
	}
	if (g_espWeaponName)
		if (const char* w = WeaponName(player.m_weaponId))
			PartBottomText(p, w, EspCol(g_espWeaponNameColor, a));

	// botox :237-242 "weapon icon": real CS:S weapon-select glyph (Cstrike.ttf "CSweapons"
	// scheme block, aim/esp_weapon_icons.h), queued as ESPC_WPNICON and resolved to a texture on
	// the render thread (same shape as ESPC_AVATAR/Avatar_Get). player.m_weaponId defaults to
	// WEAPON_NONE when the live gather found no active weapon (or there is no weapon at all --
	// the esp preview's fake player has one).
	if (g_espWeaponIcon && player.m_weaponId != WEAPON_NONE)
	{
		EspQ::Stack _s(StackKey(p, STK_BOTTOM), false, p.prio);
		const float cxw = p.box.l + p.box.w * 0.5f, yw = p.box.b + 2 + p.pad[PAD_BOTTOM] + FontH() * 0.5f;
		EspQ::WeaponIcon(cxw - 6.f, yw - 6.f, cxw + 6.f, yw + 6.f, (int)player.m_weaponId, EspCol(g_espWeaponIconColor, a));
		p.pad[PAD_BOTTOM] += 13.f;
	}

	if (g_espDistance)
		PartDistance(p, player.m_distance);

	if (g_espFlags)
	{
		char sMoney[16];
		snprintf(sMoney, sizeof(sMoney), "$%d", player.m_money);
		static const bool kAlert[EF_COUNT] = { false, false, false, false, true, false, true, true, false, false, true };
		FlagRow rows[EF_COUNT]; int n = 0;
		for (int i = 0; i < EF_COUNT; i++)
			if ((g_espFlagList & (1 << i)) && player.m_flags[i])
				rows[n++] = { i == EF_MONEY ? sMoney : EspFlagNames()[i], kAlert[i] };
		PartFlags(p, rows, n);
	}

	if (g_espHealthBar)
		PartHealthBar(p, factor, hp, g_espBox, g_espHealthText && g_espHealthTextStyle == 1);

	// ---- bones: skeleton, hitboxes, backtrack trail, head avatar ----
	if (player.m_studio && player.m_bones)
	{
		const studiohdr_t* pHdr = player.m_studio;
		const matrix3x4* aBones = player.m_bones;

		if (g_espSkeleton)   // :1409-1511
		{
			const uint32_t col = EspCol(g_espSkeletonVisColors ? (player.m_visible ? g_espSkeletonVisibleColor : g_espSkeletonInvisibleColor) : g_espSkeletonColor, a);
			const matrix3x4* m = aBones;
			const int nBones = std::min(pHdr->numbones, (int)MAXSTUDIOBONES);
			std::vector<SkelEdge> edges;
			for (int i = 0; i < nBones; i++)
			{
				if (!(BoneFlags(pHdr, i) & BONE_USED_BY_HITBOX)) continue;
				int par = BoneParent(pHdr, i);
				while (par >= 0 && par < nBones && !(BoneFlags(pHdr, par) & BONE_USED_BY_HITBOX))
					par = BoneParent(pHdr, par);
				if (par < 0 || par >= nBones) continue;
				const Vec3 c = BonePos(m, i), pp = BonePos(m, par);
				if (g_espSkeletonType == 2)
				{
					edges.push_back({ i, par, c, pp });
					continue;
				}
				Vec3 s0, s1;
				if (SDK::W2S(c, s0) && SDK::W2S(pp, s1))
					EspQ::Line(s0.x, s0.y, s1.x, s1.y, col, g_espSkeletonThickness);
			}
			if (g_espSkeletonType == 2)
				SmoothSkeleton(edges, col, g_espSkeletonThickness);
		}

		if (g_espHitboxes && g_espHitboxesMode == 0)
		{
			std::vector<std::array<Vec3, 8>> boxes;
			HitboxCorners(pHdr, aBones, boxes);
			PushHitboxes(boxes, EspCol(g_espHitboxColor, a), g_espHitboxesThickness);
		}

		// botox players.cpp:1517-1539 "lag compensated trail" 1:1: a white 1px dot per record head (gathered)
		if (g_espBacktrackTrail)
			for (const Vec3& vHeadPos : player.m_trail)
			{
				Vec3 vScreen;
				if (SDK::W2S(vHeadPos, vScreen))
					EspQ::Rect(vScreen.x, vScreen.y, vScreen.x + 1.f, vScreen.y + 1.f,
					           EspColRGBA(1.f, 1.f, 1.f, a), Black(a), true, 0.f, 1.f, 0);
			}

		if (sid && g_espAvatarPosition == 3 && player.m_hasHead)   // :1549-1583 cover head
		{
			Vec3 sh, se;
			if (SDK::W2S(player.m_head, sh) && SDK::W2S(player.m_head + player.m_viewRight * player.m_headRadius, se))
			{
				const float r = fabsf(se.x - sh.x) * g_espAvatarHeadScale;
				if (r > 0.5f)
					EspQ::Avatar(sh.x - r, sh.y - r, sh.x + r, sh.y + r, sid, a, r);
			}
		}
	}

	if (sid && (g_espAvatarPosition == 1 || g_espAvatarPosition == 2))   // :1584-1601 above / below box
	{
		const float size = g_espAvatarSize;
		const bool below = g_espAvatarPosition == 2;
		EspQ::Stack _s(StackKey(p, below ? STK_BOTTOM : STK_NAME), !below, p.prio);
		const float x = b.l + (b.w - size) * 0.5f;
		const float y = below ? b.b + 2 + p.pad[PAD_BOTTOM] : b.t - 5 - size - p.pad[PAD_TOP];
		EspQ::Avatar(x, y, x + size, y + size, sid, a, size * 0.5f);
	}
}

void CEsp::DrawSoundArrows(CCSPlayer* pLocal)   // botox sound_arrows
{
	if (!g_espOofArrows || g_espOofType == 0)
		return;
	const float flYaw = DEG2RAD(I::EngineClient->GetViewAngles().y);
	const Vec3 vLocal = pLocal->GetAbsOrigin();
	const float W = (float)H::Draw.m_nScreenW, Hh = (float)H::Draw.m_nScreenH;
	const float flNow = EspFx_NowMs();
	for (int i = 1; i <= 64; i++)
	{
		float ms, o[3];
		if (i == pLocal->entindex() || !Chams_SoundStamp(i, ms, o))
			continue;
		auto pClient = I::ClientEntityList->GetClientEntity(i);
		auto pEnt = pClient ? pClient->As<CBaseEntity>() : nullptr;
		if (!pEnt || !pEnt->IsPlayer() || pEnt->m_iTeamNum() == pLocal->m_iTeamNum())
			continue;   // botox think(): enemies only
		const Vec3 vSound(o[0], o[1], o[2]);
		if (g_chamsSoundMaxDistance > 0.f && vLocal.DistTo(vSound) > g_chamsSoundMaxDistance)
			continue;
		const float a = ChamsLayers::SoundAlpha((flNow - ms) / 1000.f, g_espOofSoundDuration, g_espOofSoundFade);
		if (a <= 0.f)
			continue;
		Vec3 s;
		if (SDK::W2S(vSound, s) && s.x >= 0.f && s.y >= 0.f && s.x <= W && s.y <= Hh)
			continue;
		const Vec3 d = vLocal - vSound;
		const auto dir = EspMath::OofDir(flYaw, d.x, d.y);
		PushRingArrow(RingPos(dir.x, dir.y), dir.x, dir.y, EspCol(g_espOofSoundColor, a));
	}
}

// Planted C4: botox edicts.cpp bomb(). No buildings in CS:S (sentry/dispenser/tele had no
// analog, removed). "CPlantedC4" is the exact IMPLEMENT_CLIENTCLASS_DT 3rd arg / server class
// name (c_plantedc4.cpp:25) GetName() actually returns -- not the DT_PlantedC4 send-table name.
void CEsp::DrawBomb(CCSPlayer* pLocal)
{
	if (!g_espC4 && !g_espC4Bar)
		return;
	const float h = FontH();
	const int iHighest = I::ClientEntityList->GetHighestEntityIndex();
	for (int i = 1; i <= iHighest; i++)
	{
		auto pClient = I::ClientEntityList->GetClientEntity(i);
		auto pEnt = pClient ? pClient->As<CBaseEntity>() : nullptr;
		if (!pEnt) continue;
		auto pClass = pEnt->GetClientClass();
		if (!pClass || strcmp(pClass->GetName(), "CPlantedC4") != 0) continue;
		auto pC4 = pEnt->As<CPlantedC4>();
		if (!pC4->IsTicking()) continue;

		const float explodeIn = pC4->SecondsToBlow();
		if (explodeIn <= 0.f) continue;
		const float defuseIn = pC4->m_flDefuseCountDown() - I::GlobalVars->curtime;
		const bool bDefusing = defuseIn > 0.f;
		const float* col = bDefusing ? (defuseIn <= explodeIn ? g_espC4DefuseColor : g_espC4FailColor) : g_espC4Color;

		if (g_espC4)
		{
			Vec3 s;
			if (SDK::W2S(pEnt->GetAbsOrigin(), s))
			{
				float topPad = 0.f;
				if (g_espC4Timer)
				{
					char t[32];
					if (bDefusing) snprintf(t, sizeof(t), "%.1f / %.1f", defuseIn, explodeIn);
					else           snprintf(t, sizeof(t), "%.1f", explodeIn);
					EspQ::Text(s.x, s.y, t, EspCol(col), Black(1.f), 0.5f);
					topPad = h;
				}
				if (g_espC4Icon)
					EspQ::Text(s.x, s.y - h - topPad, "c4", EspCol(col), Black(1.f), 0.5f);
			}
		}

		if (g_espC4Bar)
		{
			const float W = (float)H::Draw.m_nScreenW;
			constexpr float barW = 220.f, barH = 8.f;
			const float left = W * 0.5f - barW * 0.5f;
			const float top = (float)g_espC4BarPosition;
			static auto mp_c4timer = H::ConVars.FindVar("mp_c4timer");
			const float fuse = mp_c4timer && mp_c4timer->GetFloat() > 0.f ? mp_c4timer->GetFloat() : 40.f;
			const float factor = std::clamp(explodeIn / fuse, 0.f, 1.f);
			EspQ::Rect(left, top, left + barW, top + barH, Black(0.63f), Black(1.f), true, 0.f, 1.f, ESPF_OUT_OUTER);
			EspQ::Rect(left, top, left + barW * factor, top + barH, EspCol(col), Black(1.f), true, 0.f, 1.f, 0);
			char t[32];
			if (bDefusing) snprintf(t, sizeof(t), "%.1f  (defuse %.1f)", explodeIn, defuseIn);
			else            snprintf(t, sizeof(t), "%.1f", explodeIn);
			EspQ::Text(W * 0.5f, top - h - 1.f, t, EspCol(col), Black(1.f), 0.5f);
		}
		return;   // one bomb in play
	}
}

// ---- world entities: vgui surface, unchanged look -------------------------------------------
static Color_t ToColor(const float* c)
{
	return { (byte)(c[0] * 255.f + 0.5f), (byte)(c[1] * 255.f + 0.5f), (byte)(c[2] * 255.f + 0.5f), (byte)(c[3] * 255.f + 0.5f) };
}

// World-space line in SCREEN space: this runs off IEngineVGui_Paint (2D surface pass), where the
// IRenderUtils 3D calls draw nothing.
static void Line3D(const Vec3& a, const Vec3& b, Color_t c)
{
	Vec3 sa, sb;
	if (SDK::W2S(a, sa) && SDK::W2S(b, sb))
		H::Draw.Line((int)sa.x, (int)sa.y, (int)sb.x, (int)sb.y, c);
}

// botox edicts dropped_weapons(): box (plain with inner+outer outline, or corner with a 2 px
// outline under it), name above the box. Fades out from 600 to 850 units like botox. Dropped
// weapon classname is the weapon's own, same table css.h uses for CSS_WeaponIDFromClientClassName
// (DT_WeaponAWP etc never changes just because the gun is lying on the ground, not held).
// No TF2 ammo packs in CS:S -- g_esxAmmo removed (u_esp.h banner). Icon row = g_esxDroppedIcon.
void CEsp::DrawWorld(CCSPlayer* pLocal)
{
	if (!g_esxDropped)
		return;
	const Vec3 vLocal = pLocal->GetAbsOrigin();
	const float h = FontH();
	const int iHighest = I::ClientEntityList->GetHighestEntityIndex();
	for (int i = 1; i <= iHighest; i++)
	{
		auto pClient = I::ClientEntityList->GetClientEntity(i);
		auto pEnt = pClient ? pClient->As<CBaseEntity>() : nullptr;
		if (!pEnt || pEnt->IsDormant())
			continue;
		auto pClass = pEnt->GetClientClass();
		// a dropped weapon is a C_WeaponCSBase with no owner (CBaseCombatWeapon::m_hOwner unset);
		// CSS_WeaponIDFromClientClassName matches every DT_Weapon*/DT_HEGrenade/etc string.
		const CSWeaponID eDroppedId = pClass ? CSS_WeaponIDFromClientClassName(pClass->GetName()) : WEAPON_NONE;
		if (eDroppedId == WEAPON_NONE)
			continue;
		auto pWpn = reinterpret_cast<CWeaponCSBase*>(pEnt);
		if (pWpn->m_hOwner().Get() != nullptr)
			continue;   // held, not dropped

		SBox b;
		if (!ScreenBox(pEnt->GetAbsOrigin(), pEnt->m_vecMins(), pEnt->m_vecMaxs(), b) || b.w <= 0 || b.h <= 0)
			continue;
		const float fade = std::clamp((vLocal.DistTo(pEnt->GetAbsOrigin()) - 600.f) / 250.f, 0.f, 1.f);
		const float a = 1.f - fade;
		if (a <= 0.f)
			continue;

		if (g_esxDroppedBox)
			EdictBox(b, EspCol(g_esxDroppedBoxColor, a), EspCol(g_esxDroppedBoxOutlineColor, a), g_esxDroppedBoxCorner, g_esxDroppedBoxOutline);

		const float cx = b.l + b.w * 0.5f;
		const float top = (float)b.t - 3.f;
		const std::string stem = ModelStem(pEnt);
		if (g_esxDroppedName && !stem.empty())
		{
			const float na = std::max(g_esxDroppedNameColor[3] - fade, 0.f);   // botox: name alpha = colour alpha - fade
			const float c[4] = { g_esxDroppedNameColor[0], g_esxDroppedNameColor[1], g_esxDroppedNameColor[2], na };
			EspQ::Text(cx, top - h, StemName(stem).c_str(), EspCol(c), Black(na), 0.5f);
		}

		// botox :560-565 "icons##dropped weapons icon": real CS:S weapon-select glyph, same
		// ESPC_WPNICON queue command the player row's weapon icon uses. Sits left of the name text
		// so the two rows never overlap.
		if (g_esxDroppedIcon)
		{
			const float ia = std::max(g_esxDroppedIconColor[3] - fade, 0.f);
			const float c[4] = { g_esxDroppedIconColor[0], g_esxDroppedIconColor[1], g_esxDroppedIconColor[2], ia };
			const float iy = top - h * 0.5f;
			EspQ::WeaponIcon(cx - 24.f, iy - 7.f, cx - 10.f, iy + 7.f, (int)eDroppedId, EspCol(c));
		}
	}
}

// botox edicts projectiles(): every live grenade, name (+ icon) above its origin. CS:S's 3
// grenade subclasses (HE/flash/smoke) share ONE client network class, server class name
// "CBaseCSGrenadeProjectile" (grepped basecsgrenade_projectile.cpp:32 IMPLEMENT_NETWORKCLASS_
// ALIASED(BaseCSGrenadeProjectile, DT_BaseCSGrenadeProjectile) -> IMPLEMENT_CLIENTCLASS's 3rd
// macro arg, confirmed = ClientClass::m_pNetworkName per public/client_class.h -- NOT the
// "DT_..." table name, AIM3's fix for the same bug elsewhere: network names are bare server
// class names), so a class-id split can't tell HE from flash from smoke either way. Split by
// WORLDMODEL instead: each subclass
// still SetModel()s its own real model on throw (grepped css-community-master server/cstrike):
//   hegrenade_projectile.cpp:13      models/Weapons/w_eq_fraggrenade_thrown.mdl
//   flashbang_projectile.cpp:15      models/Weapons/w_eq_flashbang_thrown.mdl
//   smokegrenade_projectile.cpp:15   models/Weapons/w_eq_smokegrenade_thrown.mdl
// Icon: botox edicts.cpp:196 draws g_utilities.get_weapon_icon(char_icon_index) from its own
// bundled weapon-icon font (font_name_icon_12) -- that font/glyph table isn't in this repo
// (grepped src/dependencies/fonts: no per-weapon icon font, only the menu cog glyph in
// icon_font.h) and botox's assets aren't something to fabricate (ref-botox-assets-repo).
// Drawn as a flat colour square in its place (UNVERIFIED glyph, see report NEEDS FROM OTHERS)
// at the SAME spot botox's icon sits (icon at `out`, name 15px above -- edicts.cpp:193/201).
static const char* ThrownTypeName(CBaseEntity* pEnt)
{
	const auto pModel = pEnt->GetModel();
	const char* szModel = pModel ? I::ModelInfoClient->GetModelName(pModel) : nullptr;
	if (szModel && strstr(szModel, "fraggrenade"))  return "high explosive grenade";
	if (szModel && strstr(szModel, "flashbang"))    return "flashbang";
	if (szModel && strstr(szModel, "smokegrenade")) return "smoke grenade";
	return "grenade";
}

void CEsp::DrawThrown(CCSPlayer* pLocal)
{
	if (!g_esxThrown || (!g_esxThrownName && !g_esxThrownIcon))
		return;
	const uint32_t nameCol = EspCol(g_esxThrownNameColor), iconCol = EspCol(g_esxThrownIconColor), black = Black(1.f);
	const int iHighest = I::ClientEntityList->GetHighestEntityIndex();
	for (int i = 1; i <= iHighest; i++)
	{
		auto pClient = I::ClientEntityList->GetClientEntity(i);
		auto pEnt = pClient ? pClient->As<CBaseEntity>() : nullptr;
		if (!pEnt || pEnt->IsDormant() || pEnt == pLocal)
			continue;
		auto pClass = pEnt->GetClientClass();
		if (!pClass || strcmp(pClass->GetName(), "CBaseCSGrenadeProjectile") != 0)
			continue;
		Vec3 s;
		if (!SDK::W2S(pEnt->GetAbsOrigin(), s))
			continue;
		const char* name = ThrownTypeName(pEnt);
		if (g_esxThrownName)
			EspQ::Text(s.x, s.y - 15.f, name, nameCol, black, 0.5f);
		if (g_esxThrownIcon)
			EspQ::Rect(s.x - 4.f, s.y - 4.f, s.x + 4.f, s.y + 4.f, iconCol, black, true, 0.f, 1.f, 0);
	}
}

// botox grenade_path: the local player's throw, simulated tick by tick with traces; bounce
// squares, a detonation dot. CS:S grenades (HE/flash/smoke): GetGrenadeElasticity() = 0.45f
// (physics_main_shared.cpp:54, exact match for botox's own clip_velocity elasticity below --
// no change needed there). Fuse differs by type (resources/css-community-master):
//   HE/flash  GRENADE_TIMER = 1.5f (weapon_basecsgrenade.cpp:29, both ::Create() calls pass it
//             explicitly) -- a real timer, simulate exactly that long.
//   smoke     smokegrenade_projectile.cpp Think_Detonate: no timer at all, detonates the instant
//             GetAbsVelocity().Length() <= 0.1 (i.e. settled). The held weapon's own GetWeaponID()
//             already tells us which of the 3 this throw is (no SDK2 GetClassname needed for this
//             one -- iWeapon is the weapon in hand, not the flying projectile's class), so the
//             smoke case runs the tick loop until it settles instead of capping at a fixed fuse.
// No splash-radius ring: that was botox's TF_ATTRIB mult_explosion_radius econ-attribute hook,
// no CS:S analog (CS:S grenade damage falls off by distance in code, not a fixed radius stat).
void CEsp::DrawGrenadePath(CCSPlayer* pLocal)
{
	if (!g_esxGrenadePath || !pLocal->IsAlive())
		return;
	auto pWeapon = pLocal->m_hActiveWeapon()->As<CWeaponCSBase>();
	if (!pWeapon || !pWeapon->IsGrenade())
		return;
	const CSWeaponID iWeapon = pWeapon->GetWeaponID();
	const bool bSmoke = iWeapon == WEAPON_SMOKEGRENADE;
	// HE/flash: the real 1.5s GRENADE_TIMER. Smoke has no timer (CssGrenadeBouncePath's own
	// settle check handles it) -- give it room to bounce to a stop instead of an arbitrary cutoff.
	const float flFuseCap = bSmoke ? 5.f : 1.5f;

	const Vec3 vEye = pLocal->m_vecOrigin() + pLocal->m_vecViewOffset();
	Vec3 vOrigin, vVel;   // CSS ThrowGrenade: remapped pitch, hull-traced 16u spawn (grenade_aim.cpp)
	CssGrenadeThrow(vEye, I::EngineClient->GetViewAngles(), pLocal->m_vecVelocity(), pLocal, vOrigin, vVel);

	// basecsgrenade_projectile.h:52 GetGrenadeGravity() = 0.4f -- grenades fall at sv_gravity*0.4,
	// not full sv_gravity (matches grenade_aim.cpp's k_gravity_modifier; this draw was missing it).
	static const auto sv_gravity = H::ConVars.FindVar("sv_gravity");
	const float flGravity = (sv_gravity ? sv_gravity->GetFloat() : 800.f) * 0.4f;

	std::vector<Vec3> pts; std::vector<bool> bounce; bool bDetonated; Vec3 vDetonate;
	CssGrenadeBouncePath(vOrigin, vVel, flGravity, 0.45f, flFuseCap, pLocal, pts, bounce, bDetonated, vDetonate);
	if (pts.size() < 2)
		return;

	const uint32_t lineCol = EspCol(g_esxGrenadePathColor), bounceCol = EspCol(g_esxGrenadePathBounceColor),
		detCol = EspCol(g_esxGrenadePathDetonateColor);
	const float thick = g_esxGrenadePathThickness;
	Lines path;
	for (const Vec3& p : pts)
		path.Add(p);
	path.Push(lineCol, thick);
	for (size_t i = 1; i < pts.size(); i++)   // botox: bounce squares over the line (pts[0] = origin, no flag)
	{
		Vec3 s;
		if (bounce[i - 1] && SDK::W2S(pts[i], s))
			EspQ::Rect(s.x - 2.f, s.y - 2.f, s.x + 2.f, s.y + 2.f, bounceCol, 0, true, 0.f, 1.f, 0);
	}
	if (!bDetonated)
		return;
	Vec3 s;
	if (SDK::W2S(vDetonate, s))
		EspQ::Circle(s.x, s.y, 3.f, detCol, -1.f);
}

// Projectile path draw: already-live grenades' own current pos/velocity, forward-simulated with
// CssGrenadeBouncePath (grenade_aim.cpp) -- same CSS gravity/elasticity physics DrawGrenadePath's
// own throw preview uses, no TF2 ProjSim involved (that header/class no longer exists in this
// port). Matches live grenades the same CBaseCSGrenadeProjectile way DrawThrown does.
void CEsp::DrawProjectilePaths(CCSPlayer* pLocal)
{
	if (!g_espProjPath) return;
	// basecsgrenade_projectile.h:52 GetGrenadeGravity() = 0.4f, same fix as DrawGrenadePath above.
	static const auto sv_gravity = H::ConVars.FindVar("sv_gravity");
	const float flGravity = (sv_gravity ? sv_gravity->GetFloat() : 800.f) * 0.4f;
	const Color_t tColor = ToColor(g_espProjPathColor);

	const int iHighest = I::ClientEntityList->GetHighestEntityIndex();
	for (int i = 1; i <= iHighest; i++)
	{
		auto pClient = I::ClientEntityList->GetClientEntity(i);
		auto pEnt = pClient ? pClient->As<CBaseEntity>() : nullptr;
		if (!pEnt || pEnt->IsDormant())
			continue;
		auto pClass = pEnt->GetClientClass();
		if (!pClass || strcmp(pClass->GetName(), "CBaseCSGrenadeProjectile") != 0)
			continue;

		// CBaseCSGrenadeProjectile doesn't network a live m_vecVelocity (only the launch-time
		// m_vInitialVelocity, basecsgrenade_projectile.cpp:38) -- EstimateAbsVelocity (defs.h,
		// SIGNATURE_ARGS) works on any CBaseEntity, networked or not.
		std::vector<Vec3> pts; std::vector<bool> bounce; bool bDetonated; Vec3 vDetonate;
		CssGrenadeBouncePath(pEnt->GetAbsOrigin(), pEnt->GetAbsVelocity(), flGravity, 0.45f, 3.f,
			pEnt, pts, bounce, bDetonated, vDetonate);
		for (size_t j = 1; j < pts.size(); j++)
			Line3D(pts[j - 1], pts[j], tColor);
	}
}

// Sound pings: pushed from visuals.cpp's EmitSound hook, copied out under esp_fx.h's lock.
void CEsp::DrawSoundPings()
{
	if (!g_espSoundPings) return;
	EspFxSoundPing pings[kEspFxMaxSoundPings];
	const float flNow = EspFx_NowMs();
	constexpr float flLifeMs = 600.f;
	const int n = EspFx_CopySoundPings(pings, kEspFxMaxSoundPings, flNow, flLifeMs);
	for (int i = 0; i < n; i++)
	{
		const float flAge = (flNow - pings[i].bornMs) / flLifeMs;   // 0..1
		const Vec3 vCenter(pings[i].x, pings[i].y, pings[i].z);
		const float flRadius = 4.f + flAge * 20.f;
		Color_t tColor = ToColor(g_espSoundColor);
		tColor.a = (byte)(tColor.a * (1.f - flAge));
		constexpr int kSeg = 16;
		for (int s = 0; s < kSeg; s++)
		{
			const float a0 = s * (6.2831853f / kSeg), a1 = (s + 1) * (6.2831853f / kSeg);
			Line3D(vCenter + Vec3(cosf(a0) * flRadius, sinf(a0) * flRadius, 0.f),
			       vCenter + Vec3(cosf(a1) * flRadius, sinf(a1) * flRadius, 0.f), tColor);
		}
	}
}

void CEsp::Draw(CCSPlayer* pLocal)
{
	EspQ::Begin();
	if (pLocal)
	{
		static bool s_bLoggedCall = false, s_bLoggedDrawn = false;
		if (!s_bLoggedCall && g_espEnable)
		{
			s_bLoggedCall = true;
			DebugLog("esp: Draw reached (screen %dx%d)", H::Draw.m_nScreenW, H::Draw.m_nScreenH);
		}

		m_nDrawn = 0;
		if (Esp_On())
		{
			DrawPlayers(pLocal);
			DrawSoundArrows(pLocal);
			DrawDamageNumbers();
			DrawHitHitboxes();
		}
		else
			m_vDamage.clear(), m_vHitBoxes.clear();
		DrawBomb(pLocal);
		if (!s_bLoggedDrawn && m_nDrawn > 0)
		{
			s_bLoggedDrawn = true;
			DebugLog("esp: queued %d players this frame (%d cmds)", m_nDrawn, int(g_espQBuild.cmds.size()));
		}

		DrawWorld(pLocal);
		DrawThrown(pLocal);
		DrawGrenadePath(pLocal);
		DrawProjectilePaths(pLocal);
		DrawSoundPings();
	}
	EspQ::Commit(GetTickCount64(), (float)H::Draw.m_nScreenW, (float)H::Draw.m_nScreenH);
}
