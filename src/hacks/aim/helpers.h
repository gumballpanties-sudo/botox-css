#pragma once
// Engine-facing helpers shared by the aim features: surface drawing, the per-tick entity
// cache, convar access, the overlay font and the trace filters.

// ================ SDK/Helpers/Fonts/Fonts.h ================
#include "utils.h"
#include "defs.h"
#include "css.h"   // CSWeaponID (CTraceFilterHitscan's sniper-weapon list below)
#include <unordered_map>

// The engine surface fonts the aim overlays draw with. One face: the tick / crit / no-spread
// readouts all use FONT_INDICATORS. Everything is a stock GDI family, so nothing is embedded.
enum EFonts
{
	FONT_INDICATORS
};

struct Font_t
{
	const char* m_szName;
	int m_nTall, m_nFlags, m_nWeight;
	unsigned long m_dwFont;
};

class CFonts
{
private:
	std::unordered_map<EFonts, Font_t> m_mFonts = {};

public:
	void Reload(float flDPI = 1.f);
	const Font_t& GetFont(EFonts eFont);
};

ADD_FEATURE_CUSTOM(CFonts, Fonts, H);

// ================ SDK/Helpers/Draw/Draw.h ================
#include "vars.h"

MAKE_SIGNATURE(VertexColor, "engine.dll", "48 89 05 ? ? ? ? E8 ? ? ? ? 48 8B 7C 24", 0x0);
MAKE_SIGNATURE(VertexColorIgnoreZ, "engine.dll", "48 89 05 ? ? ? ? 48 83 C4 ? C3 CC CC CC CC CC CC CC CC CC 48 8B C4", 0x0);

// Per-layer accessor for the stacked panel outlines (Vars::Menu::Style). Returns false when i is
// outside the 4 fixed layers. Centralizes the layer CVars so the ImGui path (DrawStyledBackground)
// and the surface path (CDraw::PanelOutline) read identical settings. Layers stack outermost.
inline bool GetStyleOutlineLayer(int i, Color_t& tColor, float& flThick, float& flBlur)
{
	switch (i)
	{
	case 0: tColor = Vars::Menu::Style::OutlineColor1.Value; flThick = Vars::Menu::Style::OutlineThickness1.Value; flBlur = Vars::Menu::Style::OutlineBlur1.Value; return true;
	case 1: tColor = Vars::Menu::Style::OutlineColor2.Value; flThick = Vars::Menu::Style::OutlineThickness2.Value; flBlur = Vars::Menu::Style::OutlineBlur2.Value; return true;
	case 2: tColor = Vars::Menu::Style::OutlineColor3.Value; flThick = Vars::Menu::Style::OutlineThickness3.Value; flBlur = Vars::Menu::Style::OutlineBlur3.Value; return true;
	case 3: tColor = Vars::Menu::Style::OutlineColor4.Value; flThick = Vars::Menu::Style::OutlineThickness4.Value; flBlur = Vars::Menu::Style::OutlineBlur4.Value; return true;
	default: return false;
	}
}

enum EAlign
{
	ALIGN_TOPLEFT,
	ALIGN_TOP,
	ALIGN_TOPRIGHT,
	ALIGN_LEFT,
	ALIGN_CENTER,
	ALIGN_RIGHT,
	ALIGN_BOTTOMLEFT,
	ALIGN_BOTTOM,
	ALIGN_BOTTOMRIGHT
};

enum Scale_
{
	Scale_None = 0,
	Scale_Round = 1,
	Scale_Floor = 2,
	Scale_Ceil = 3
};

class CDraw
{
private:

public:
	inline float Scale(float flN = 1.f, int iFlags = Scale_None, float flScale = Vars::Menu::Scale.Value)
	{
		flN *= flScale;
		switch (iFlags)
		{
		case Scale_Round: flN = roundf(flN); break;
		case Scale_Floor: flN = floorf(flN); break;
		case Scale_Ceil: flN = ceilf(flN); break;
		}
		return flN;
	}

	void Start(bool bBadFontCheck = false);
	void End();
	void StartClipping(int x, int y, int w, int h);
	void EndClipping();

	void UpdateScreenSize();
	void UpdateW2SMatrix();

	Vec2 GetTextSize(const char* text, const Font_t& tFont);
	Vec2 GetTextSize(const wchar_t* text, const Font_t& tFont);

	void String(const Font_t& tFont, int x, int y, Color_t tColor, EAlign eAlign, const char* str);
	void String(const Font_t& tFont, int x, int y, Color_t tColor, EAlign eAlign, const wchar_t* wstr);
	// iStyle: -1 = default (single drop if CheapText, else full outline), 0 = shadow (single offset), 1 = full outline.
	void StringOutlined(const Font_t& tFont, int x, int y, Color_t tColor, Color_t tColorOut, EAlign eAlign, const char* str, bool bAlpha = true, int iStyle = -1);
	void StringOutlined(const Font_t& tFont, int x, int y, Color_t tColor, Color_t tColorOut, EAlign eAlign, const wchar_t* wstr, bool bAlpha = true, int iStyle = -1);

	void Line(int x1, int y1, int x2, int y2, Color_t tColor);
	void FillPolygon(std::vector<Vertex_t> vVertices, Color_t tColor);
	void LinePolygon(std::vector<Vertex_t> vVertices, Color_t tColor);

	void FillRect(int x, int y, int w, int h, Color_t tColor);
	void LineRect(int x, int y, int w, int h, Color_t tColor);
	void FillRectOutline(int x, int y, int w, int h, Color_t tColor, Color_t tColorOut = { 0, 0, 0, 255 });
	void LineRectOutline(int x, int y, int w, int h, Color_t tColor, Color_t tColorOut = { 0, 0, 0, 255 }, bool bInside = true);
	// Draws only the corners of a rect. flLengthFrac is the fraction (0-0.5) of each side used per corner.
	void LineCornerRect(int x, int y, int w, int h, Color_t tColor, float flLengthFrac = 0.25f);
	void LineCornerRectOutline(int x, int y, int w, int h, Color_t tColor, Color_t tColorOut = { 0, 0, 0, 255 }, float flLengthFrac = 0.25f, bool bInside = true);
	void FillRectPercent(int x, int y, int w, int h, float t, Color_t tColor, Color_t tColorOut = { 0, 0, 0, 255 }, EAlign eAlign = ALIGN_LEFT, bool bAdjust = false);
	void FillRoundRect(int x, int y, int w, int h, int iRadius, Color_t tColor, int iCount = 64);
	void LineRoundRect(int x, int y, int w, int h, int iRadius, Color_t tColor, int iCount = 64);
	// Shared styled-panel outline (Vars::Menu::Style Outline/Color/Thickness/Blur) for surface-drawn
	// panels (Minimal spectator list, Checkpoints). Drawn just outside the panel; thickness is built
	// from concentric rings and blur from faded expanded passes. Mirrors CMenu::DrawStyledBackground.
	void PanelOutline(int x, int y, int w, int h, int iRadius, float flAlpha = 1.f);

	CHudTexture* GetIcon(const char* szIcon, int eIconFormat = 0);
	// Procedural surface texture reused across frames (Config > STYLE effect mirror for the spectator
	// list). NewProceduralTexId() makes one id; DrawStyleTexture() re-uploads the RGBA8888 buffer into
	// it and draws it - whole texture (bFullUV, Balatro) or by screen-rect sub-UV (Blur).
	int NewProceduralTexId();
	void DrawStyleTexture(int iId, const unsigned char* rgba, int texW, int texH, int x, int y, int w, int h, bool bFullUV);
	void DrawHudTexture(float x, float y, float s, const CHudTexture* pTexture, Color_t tColor = { 255, 255, 255, 255 });
	void DrawHudTextureByName(float x, float y, float s, const char* sTexture, Color_t tColor = { 255, 255, 255, 255 });

	void RenderLine(const Vec3& vStart, const Vec3& vEnd, Color_t tColor, bool bZBuffer = false);
	void RenderBox(const Vec3& vOrigin, const Vec3& vMins, const Vec3& vMaxs, const Vec3& vAngles, Color_t tColor, bool bZBuffer = false, bool bInsideOut = false);
	void RenderWireframeBox(const Vec3& vOrigin, const Vec3& vMins, const Vec3& vMaxs, const Vec3& vAngles, Color_t tColor, bool bZBuffer = false);
	void RenderWireframeSweptBox(const Vector& vStart, const Vector& vEnd, const Vec3& vMins, const Vec3& vMaxs, const Vec3& vAngles, Color_t tColor, bool bZBuffer = false);
	void RenderTriangle(const Vector& vPoint1, const Vector& vPoint2, const Vector& vPoint3, Color_t tColor, bool bZBuffer = false);
	void RenderSphere(const Vector& vCenter, float flRadius, int nTheta, int nPhi, Color_t tColor, IMaterial* pMaterial);
	void RenderSphere(const Vector& vCenter, float flRadius, int nTheta, int nPhi, Color_t tColor, bool bZBuffer = false);

	int m_nScreenW = 0, m_nScreenH = 0;
	VMatrix m_mWorldToProjection = {};
};

ADD_FEATURE_CUSTOM(CDraw, Draw, H);

// ================ SDK/Helpers/Entities/Entities.h ================
#include "utils.h"
#include "vars.h"

// CS:GO/CSS botox aimbot targets players only (no buildings/pickups/NPCs/bombs/sentry-dot/
// sticky/flare groups -- all TF2-only, see DE-TF2 RULE). Kept as its own enum (not just a bool)
// since PlayerEnemy/PlayerTeam/PlayerAll are still 3 distinct buckets other code indexes by name.
Enum(Entity, Invalid = -1,
	PlayerAll, PlayerEnemy, PlayerTeam
)

struct DormantData
{
	Vec3 Location;
	float LastUpdate = 0.f;
};

struct VelFixRecord
{
	Vec3 m_vecOrigin;
	float m_flSimulationTime;
};

class CEntities
{
private:
	bool ManageDormancy(CBaseEntity* pEntity);

	CCSPlayer* m_pLocal = nullptr;
	CWeaponCSBase* m_pLocalWeapon = nullptr;
	CCSPlayerResource* m_pPlayerResource = nullptr;

	std::unordered_map<EntityEnum::EntityEnum, std::vector<CBaseEntity*>> m_mGroups = {};

	std::unordered_map<int, float> m_mDeltaTimes = {}, m_mLagTimes = {};
	std::unordered_map<int, int> m_mChokes = {}, m_mSetTicks = {};
	std::unordered_map<int, Vec3> m_mOldAngles = {}, m_mEyeAngles = {};
	std::unordered_map<int, bool> m_mLagCompensation = {};
	std::unordered_map<int, DormantData> m_mDormancy = {};
	std::unordered_map<int, Vec3> m_mAvgVelocities = {};
	std::unordered_map<int, uint32_t> m_mModels = {};
	std::unordered_map<int, std::deque<VelFixRecord>> m_mOrigins = {};

public:
	void Store();
	void Clear(bool bShutdown = false);

	bool IsHealth(uint32_t uHash);
	bool IsAmmo(uint32_t uHash);
	bool IsPowerup(uint32_t uHash);
	bool IsSpellbook(uint32_t uHash);

	CCSPlayer* GetLocal();
	CWeaponCSBase* GetWeapon();
	CCSPlayerResource* GetResource();

	const std::vector<CBaseEntity*>& GetGroup(const EntityEnum::EntityEnum iGroup);

	float GetDeltaTime(int iIndex);
	int GetChoke(int iIndex);
	Vec3 GetEyeAngles(int iIndex);
	Vec3* GetAvgVelocity(int iIndex);
	void SetAvgVelocity(int iIndex, Vec3 vAvgVelocity);
	uint32_t GetModel(int iIndex);
	std::deque<VelFixRecord>* GetOrigins(int iIndex);

};

ADD_FEATURE_CUSTOM(CEntities, Entities, H);

// ================ SDK/Helpers/ConVars/ConVars.h ================
#include "utils.h"
#include <unordered_map>

class CConVars
{
private:
	std::unordered_map<uint32_t, ConVar*> m_mCVarMap = {};
	std::unordered_map<ConCommandBase*, int> m_mFlagMap = {};

	bool m_bUnlocked = false;

public:
	bool Unlock();
	bool Restore();
	ConVar* FindVar(const char* sCVar);
};

ADD_FEATURE_CUSTOM(CConVars, ConVars, H);

// ================ SDK/Helpers/TraceFilters/TraceFilters.h ================

enum
{
	SKIP_CHECK,
	FORCE_PASS,
	FORCE_HIT
};

enum
{
	WEAPON_INCLUDE,
	WEAPON_EXCLUDE
};
enum
{
	PLAYER_DEFAULT,
	PLAYER_NONE,
	PLAYER_ALL
};
enum
{
	OBJECT_DEFAULT,
	OBJECT_NONE,
	OBJECT_ALL
};

class CTraceFilterHitscan : public ITraceFilter
{
public:
	bool ShouldHitEntity(IHandleEntity* pServerEntity, int nContentsMask) override;
	TraceType_t GetTraceType() const override;
	CBaseEntity* pSkip = nullptr;

	int iTeam = -1;
	std::vector<int> vWeapons = { WEAPON_AWP, WEAPON_SG550, WEAPON_G3SG1, WEAPON_SCOUT };   // css_weapons.h sniper class
	int iType = FORCE_HIT;
	int iWeapon = WEAPON_EXCLUDE;
	bool bWeapon = false;
};

class CTraceFilterCollideable : public ITraceFilter
{
public:
	bool ShouldHitEntity(IHandleEntity* pServerEntity, int nContentsMask) override;
	TraceType_t GetTraceType() const override;
	CBaseEntity* pSkip = nullptr;

	int iTeam = -1;
	std::vector<int> vWeapons = {};   // TF2's crossbow/lunchbox healing-collide special case has no CSS equivalent
	int iType = FORCE_HIT;
	int iWeapon = WEAPON_INCLUDE;
	bool bWeapon = false;
	int iPlayer = PLAYER_DEFAULT;
	int iObject = OBJECT_ALL;
	bool bMisc = false;
};

class CTraceFilterWorldAndPropsOnly : public ITraceFilter
{
public:
	bool ShouldHitEntity(IHandleEntity* pServerEntity, int nContentsMask) override;
	TraceType_t GetTraceType() const override;
	CBaseEntity* pSkip = nullptr;

	int iTeam = -1;
};

// ================ SDK/Helpers/Particles/Particles.h ================
