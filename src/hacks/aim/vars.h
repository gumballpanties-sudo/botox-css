#pragma once
#include "defs.h"
#include "utils.h"
#include <windows.h>
#include <unordered_map>
#include <map>
#include <typeinfo>

#define DEFAULT_BIND -1

// forward declartion of ConfigVar
template <class T>
class ConfigVar;

class BaseVar
{
public:
	size_t m_iType;
	std::string m_sName;
	int m_iFlags = 0;

	std::vector<const char*> m_vTitle;
	const char* m_sSection;
	union {
		int i = 0;
		float f;
	} m_unMin;
	union {
		int i = 0;
		float f;
	} m_unMax;
	union {
		int i = 0;
		float f;
	} m_unStep;
	std::vector<const char*> m_vValues = {};
	const char* m_sExtra = nullptr;

	// getter for ConfigVar
	template <class T>
	inline ConfigVar<T>* As()
	{
		if (typeid(T).hash_code() != m_iType)
			return nullptr;

		return reinterpret_cast<ConfigVar<T>*>(this);
	}
};

namespace G
{
	inline std::vector<BaseVar*> Vars = {};
};

template <class T>
class ConfigVar : public BaseVar
{
public:
	T Default;
	T Value;
	std::unordered_map<int, T> Map = {};
	ConfigVar(T tValue, std::string sName, const char* sSection, std::vector<const char*> vTitle, int iFlags = 0, std::vector<const char*> vValues = {}, const char* sNone = nullptr)
	{
		Default = tValue;
		Value = tValue;
		Map[DEFAULT_BIND] = tValue;

		m_iType = typeid(T).hash_code();
		m_sName = sName;
		m_iFlags = iFlags;

		m_vTitle = vTitle;
		m_sSection = sSection;
		m_vValues = vValues;
		m_sExtra = sNone;

		G::Vars.push_back(this);
	}
	ConfigVar(T tValue, std::string sName, const char* sSection, std::vector<const char*> vTitle, int iFlags, int iMin, int iMax, int iStep = 1, const char* sFormat = "%i")
	{
		Default = tValue;
		Value = tValue;
		Map[DEFAULT_BIND] = tValue;

		m_iType = typeid(T).hash_code();
		m_sName = sName;
		m_iFlags = iFlags;

		m_vTitle = vTitle;
		m_sSection = sSection;
		m_unMin.i = iMin;
		m_unMax.i = iMax;
		m_unStep.i = iStep;
		m_sExtra = sFormat;

		G::Vars.push_back(this);
	}
	ConfigVar(T tValue, std::string sName, const char* sSection, std::vector<const char*> vTitle, int iFlags, float flMin, float flMax, float flStep = 1.f, const char* sFormat = "%g")
	{
		Default = tValue;
		Value = tValue;
		Map[DEFAULT_BIND] = tValue;

		m_iType = typeid(T).hash_code();
		m_sName = sName;
		m_iFlags = iFlags;

		m_vTitle = vTitle;
		m_sSection = sSection;
		m_unMin.f = flMin;
		m_unMax.f = flMax;
		m_unStep.f = flStep;
		m_sExtra = sFormat;

		G::Vars.push_back(this);
	}

	inline T& operator[](int i)
	{
		return Map[i];
	}
	inline bool contains(int i) const
	{
		return Map.contains(i);
	}
};

#define NAMESPACE_BEGIN(name, ...)\
	namespace name {\
		inline const char* GetNamespace() { return "Vars::"#name"::"; }\
		inline const char* GetSubname() { return ""; }\
		inline const char* GetSection() { return !std::string(#__VA_ARGS__).empty() ? ""#__VA_ARGS__ : #name; }

#define SUBNAMESPACE_BEGIN(name, ...)\
	namespace name {\
		inline const char* GetSubname() { return #name"::"; }\
		inline const char* GetSection() { return !std::string(#__VA_ARGS__).empty() ? ""#__VA_ARGS__ : #name; }

#define NAMESPACE_END(name)\
	}
#define SUBNAMESPACE_END(name)\
	}

#define CVar(name, title, value, ...)\
	inline ConfigVar<decltype(value)> name = { value, std::format("{}{}{}", GetNamespace(), GetSubname(), #name), GetSection(), { title }, __VA_ARGS__ }
#define CVarValues(name, title, value, flags, none, ...)\
	inline ConfigVar<decltype(value)> name = { value, std::format("{}{}{}", GetNamespace(), GetSubname(), #name), GetSection(), { title }, flags, { __VA_ARGS__ }, none }
#define Enum(name, ...)\
	namespace name##Enum { enum name##Enum { __VA_ARGS__ }; }
#define CVarEnum(name, title, value, flags, none, values, ...)\
	CVarValues(name, title, value, flags, none, values);\
	Enum(name, __VA_ARGS__);

#define NONE 0
#define VISUAL (1 << 31)
#define NOSAVE (1 << 30)
#define NOBIND (1 << 29)
#define DEBUGVAR (1 << 28)

// flags to be automatically used in widgets. keep these as the same values as the flags in components, do not include visual flags
#define SLIDER_CLAMP (1 << 2)
#define SLIDER_MIN (1 << 3)
#define SLIDER_MAX (1 << 4)
#define SLIDER_PRECISION (1 << 5)
#define SLIDER_NOAUTOUPDATE (1 << 6)
#define DROPDOWN_MULTI (1 << 2)
#define DROPDOWN_MODIFIABLE (1 << 3)
#define DROPDOWN_CUSTOM (1 << 2)
#define DROPDOWN_AUTOUPDATE (1 << 3)

namespace Vars
{
	NAMESPACE_BEGIN(Menu)

		// TF2 crit hack / conditions / seed prediction indicators removed (spectators + ping live in dih's own hud)
		CVarEnum(Indicators, "Indicators", 0b00000, VISUAL | DROPDOWN_MULTI, nullptr,
			VA_LIST("Ticks"),
			Ticks = 1 << 0);

		CVar(TicksDisplay, "Ticks display", DragBox_t(), VISUAL | NOBIND);

		CVar(Scale, "Scale", 1.f, NOBIND | SLIDER_MIN | SLIDER_PRECISION | SLIDER_NOAUTOUPDATE, 0.75f, 2.f, 0.25f);
		CVar(CheapText, "Cheap text", false);

		SUBNAMESPACE_BEGIN(Theme)
			CVar(Accent, "Accent color", Color_t(200, 25, 25, 255), VISUAL);
			CVar(Background, "Drop down bg color", Color_t(0, 0, 0, 250), VISUAL);
			CVar(Active, "Active color", Color_t(255, 255, 255, 255), VISUAL);
			CVar(Inactive, "Inactive color", Color_t(150, 150, 150, 255), VISUAL);
		SUBNAMESPACE_END(Theme);

		// One shared background style for every overlay panel (main menu, watermark, media player,
		// pixel-surf point boxes, the Minimal spectator list and the Checkpoints window): a solid
		SUBNAMESPACE_BEGIN(Style)
			// Stacked panel outlines (up to 4), each drawn just outside the previous - port of
			// the OutlineLayer stack. OutlineCount = how many layers are active (0 = off).
			CVar(OutlineCount, "Outline layers", 0, VISUAL | SLIDER_MIN, 0, 4);
			CVar(OutlineColor1, "Outline 1 color", Color_t(0, 0, 0, 255), VISUAL);
			CVar(OutlineThickness1, "Outline 1 thickness", 1.f, VISUAL | SLIDER_PRECISION, 0.5f, 8.f, 0.5f, "%.1f");
			CVar(OutlineBlur1, "Outline 1 blur", 0.f, VISUAL | SLIDER_PRECISION, 0.f, 16.f, 0.5f, "%.1f");
			CVar(OutlineColor2, "Outline 2 color", Color_t(0, 0, 0, 200), VISUAL);
			CVar(OutlineThickness2, "Outline 2 thickness", 1.f, VISUAL | SLIDER_PRECISION, 0.5f, 8.f, 0.5f, "%.1f");
			CVar(OutlineBlur2, "Outline 2 blur", 0.f, VISUAL | SLIDER_PRECISION, 0.f, 16.f, 0.5f, "%.1f");
			CVar(OutlineColor3, "Outline 3 color", Color_t(0, 0, 0, 150), VISUAL);
			CVar(OutlineThickness3, "Outline 3 thickness", 1.f, VISUAL | SLIDER_PRECISION, 0.5f, 8.f, 0.5f, "%.1f");
			CVar(OutlineBlur3, "Outline 3 blur", 0.f, VISUAL | SLIDER_PRECISION, 0.f, 16.f, 0.5f, "%.1f");
			CVar(OutlineColor4, "Outline 4 color", Color_t(0, 0, 0, 100), VISUAL);
			CVar(OutlineThickness4, "Outline 4 thickness", 1.f, VISUAL | SLIDER_PRECISION, 0.5f, 8.f, 0.5f, "%.1f");
			CVar(OutlineBlur4, "Outline 4 blur", 0.f, VISUAL | SLIDER_PRECISION, 0.f, 16.f, 0.5f, "%.1f");
			// Custom font (used when FontPreset == Custom): an installed family name ("Tahoma",
			// "Segoe UI") or a file name/stem from the Fonts folders; blank = falls back to Kodchasan.
			CVar(Font, "Custom font", std::string(""), VISUAL);
		SUBNAMESPACE_END(Style);
	NAMESPACE_END(Menu);

	NAMESPACE_BEGIN(Colors)
		CVar(Local, "Local color", Color_t(255, 255, 255, 0), VISUAL);

		CVar(IndicatorTextGood, "Indicator text good", Color_t(150, 255, 150, 255), NOSAVE | DEBUGVAR);
		CVar(IndicatorTextMid, "Indicator text mid", Color_t(255, 200, 0, 255), NOSAVE | DEBUGVAR);
		CVar(IndicatorTextBad, "Indicator text bad", Color_t(255, 150, 150, 255), NOSAVE | DEBUGVAR);
		CVar(IndicatorTextMisc, "Indicator text misc", Color_t(100, 255, 255, 255), NOSAVE | DEBUGVAR);

		CVar(Line, "Line color", Color_t(255, 255, 255, 255), VISUAL);
		CVar(LineIgnoreZ, "Line ignore Z color", Color_t(255, 255, 255, 0), VISUAL);

		CVar(PlayerPath, "Player path color", Color_t(255, 255, 255, 0), VISUAL);
		CVar(PlayerPathIgnoreZ, "Player path ignore Z color", Color_t(255, 255, 255, 255), VISUAL);
		CVar(ProjectilePath, "Projectile path color", Color_t(255, 255, 255, 0), VISUAL);
		CVar(ProjectilePathIgnoreZ, "Projectile path ignore Z color", Color_t(255, 255, 255, 255), VISUAL);
		CVar(SplashRadius, "Splash radius color", Color_t(255, 255, 255, 0), VISUAL);
		CVar(RealPath, "Real path color", Color_t(255, 255, 255, 0), NOSAVE | DEBUGVAR);
		CVar(RealPathIgnoreZ, "Real path ignore Z color", Color_t(255, 255, 255, 255), NOSAVE | DEBUGVAR);

		CVar(BoundHitboxEdge, "Bound hitbox edge color", Color_t(255, 255, 255, 255), VISUAL);
		CVar(BoundHitboxEdgeIgnoreZ, "Bound hitbox edge ignore Z color", Color_t(255, 255, 255, 0), VISUAL);
		CVar(BoundHitboxFace, "Bound hitbox face color", Color_t(255, 255, 255, 0), VISUAL);
		CVar(BoundHitboxFaceIgnoreZ, "Bound hitbox face ignore Z color", Color_t(255, 255, 255, 0), VISUAL);

	NAMESPACE_END(Colors);

	NAMESPACE_BEGIN(Aimbot)
		SUBNAMESPACE_BEGIN(General, Aimbot)
			// Master enable -- botox "aimbot master switch" (hacks/menu/menu.cpp:953).
			CVar(Enabled, "Aimbot master switch", false);
			// Target selection is always FOV, so the sort method is not a var --
			// CAimbotGlobal::SortTargets takes the enum directly, and the melee paths pass
			// Distance explicitly because the knife wants the nearest target, not the centred one.
			CVarEnum(TargetSelection, "Target selection", 0, NOSAVE | DEBUGVAR, nullptr,
				VA_LIST("FOV", "Distance"),
				FOV, Distance);
			// CSS: players are the only aim target (TF2 sentries/dispensers/teleporters/stickies/
			// NPCs/bombs removed). Bit 0 unchanged, so saved configs keep their players bit.
			CVarEnum(Target, "Target", 0b0000001, NOSAVE | DROPDOWN_MULTI, nullptr,
				VA_LIST("Players"),
				Players = 1 << 0);
			// CSS: only "unsimulated" (choking/lagging target) means anything. TF2 invulnerable /
			// cloak-invisible / dead ringer / vaccinator / disguised / taunting removed.
			CVarEnum(Ignore, "Ignore", 0b0000000, NOSAVE | DROPDOWN_MULTI, nullptr,
				VA_LIST("Unsimulated"),
				Unsimulated = 1 << 0);
			CVar(AimFOV, VA_LIST("Aim FOV", "Hitscan aim FOV"), 5.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 180.f);   // botox m_aimbot_general_fov 5
			CVar(TickTolerance, "Tick tolerance", 4, NOSAVE | SLIDER_CLAMP, 0, 21);
			CVar(AutoShoot, "Auto shoot", false);
			// botox weapon_accuracy_nospread label is just "nospread" (hacks/menu/menu.cpp:1021).
			CVar(NoSpread, "Nospread", false);

			// activation bind -- mode 0 = always on, 1 = hold key, 2 = toggle.
			// Read by CAimbot::Run through AimbotKeyOK(); nothing downstream knows about it.
			CVar(AimKey, "Aim key", 0, NOBIND);
			CVarEnum(AimKeyMode, "Aim key mode", 1, NOBIND, nullptr,
				VA_LIST("Always on", "On hotkey", "Toggle"),
				AlwaysOn, OnHotkey, Toggle);

			CVar(NoSpreadOffset, "No spread offset", 0.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, -1.f, 1.f, 0.1f);
			CVar(NoSpreadAverage, "No spread average", 5, NOSAVE | DEBUGVAR | SLIDER_MIN, 1, 25);
			CVar(NoSpreadInterval, "No spread interval", 0.1f, NOSAVE | DEBUGVAR | SLIDER_MIN, 0.05f, 5.f, 0.1f, "%gs");
			CVar(NoSpreadBackupInterval, "No spread backup interval", 2.f, NOSAVE | DEBUGVAR | SLIDER_MIN, 2.f, 10.f, 0.1f, "%gs");

			// ===== CS:GO 1:1 port additions (11-03), botox n_aimbot::impl_t =====
			// botox m_aimbot_silent / m_aimbot_camlock. Camlock wins when both are set (botox:
			// "if (!silent||camlock||mixed) SetViewAngles" -- camlock forces silent off).
			CVar(Silent, "Silent aim", false);
			// botox m_aimbot_silent_step: 0 = no cap (bullet can land anywhere in FOV), >0 clamps
			// the per-tick pitch/yaw delta off the raw mouse view (aimbot.cpp:1195).
			CVar(SilentStep, "Silent aim step", 0.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 90.f, 5.f, "%.0f deg");
			CVar(Camlock, "Camlock", false);
			// botox m_aimbot_no_switch: hold the current lock while it stays a valid target
			// instead of re-picking the FOV-best one every tick (aimbot.cpp:1037).
			CVar(NoSwitch, "No target switching", false);
			// botox m_aimbot_target_team: is_valid_aim_target() lets teammates through too
			// (entity.cpp:251) -- ties into ShouldIgnore()/the hitscan target-group pick.
			CVar(TargetTeam, "Target team", false);
			// botox m_aimbot_scope_fov_scale: shrink the aim FOV cone by the zoom ratio while
			// scoped (aimbot.cpp:118 scale_fov_with_scope) -- NOT the same knob as ScopeFOV below
			// (dih-only scope-ZOOM hack, menu row removed 11-08, NOSAVE = off).
			CVar(ScaleFovWithScope, "Scale fov with scope", false);
			// botox settings.m_smooth: divisor, 1 = instant/no smoothing (silent ignores it
			// entirely -- the base is the mouse every tick, see aim_rcs.cpp/aim_hitscan.cpp).
			CVar(Smooth, "Smooth", 5.f, SLIDER_MIN | SLIDER_PRECISION, 1.f, 30.f, 0.5f);   // botox m_aimbot_general_smooth 5
			// botox m_aimbot_weapon_settings: which category the menu combo shows (saved, like botox)
			CVar(WeaponSettings, "Weapon settings", 0, SLIDER_CLAMP, 0, 7);
			CVar(Autowall, "Autowall", false);
			CVar(AutowallMinDamage, "Min damage", 20, SLIDER_CLAMP, 0, 100);
		SUBNAMESPACE_END(Global);

		SUBNAMESPACE_BEGIN(Hitscan)
			// per weapon-type enable (the menu section's own checkbox).
			CVar(Enabled, VA_LIST("Hitscan", "Hitscan aimbot"), true);
			// botox m_aimbot_hitboxes (the "general" hitboxes), e_aim_hitboxes group order 1:1. Each
			// group = botox enabled_hitboxes(): arms = upper arms only, legs = thighs only. Same bits
			// in every WeaponClass::<X>Hitboxes below. Renamed from Hitboxes: old saves held a
			// different bit layout (head/body/pelvis/arms/legs + bodyaim/headshot-only modifiers).
			CVarEnum(HitboxGroups, VA_LIST("Hitboxes", "General hitboxes"), 0b1111111, DROPDOWN_MULTI, nullptr,
				VA_LIST("Head", "Neck", "Chest", "Stomach", "Pelvis", "Arms", "Legs"),
				Head = 1 << 0, Neck = 1 << 1, Chest = 1 << 2, Stomach = 1 << 3, Pelvis = 1 << 4, Arms = 1 << 5, Legs = 1 << 6, All = 0b1111111);
			CVarValues(MultipointHitboxes, "Multipoint hitboxes", 0b00000, NOSAVE | DROPDOWN_MULTI, "All",
				VA_LIST("Head", "Body", "Pelvis", "Arms", "Legs"));
			// CSS modifiers only (TF2 wait for headshot / wait for charge / auto rev minigun /
			// extinguish team removed; old default had auto rev on)
			CVarEnum(Modifiers, VA_LIST("Modifiers", "Hitscan modifiers"), 0b000, NOSAVE | DROPDOWN_MULTI, nullptr,
				VA_LIST("Tapfire", "Scoped only", "Auto scope"),
				Tapfire = 1 << 0, ScopedOnly = 1 << 1, AutoScope = 1 << 2);
			CVar(MultipointScale, "Multipoint scale", 0.f, NOSAVE | SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 100.f, 5.f, "%g%%");
			CVar(TapfireDistance, "Tapfire distance", 1000.f, NOSAVE | SLIDER_MIN | SLIDER_PRECISION, 250.f, 1000.f, 50.f);

			CVarEnum(PeekCheck, "Peek check", 1, NOSAVE | DEBUGVAR, nullptr,
				VA_LIST("Off", "Doubletap only", "Always"),
				Off, DoubletapOnly, Always);
			CVar(PeekAmount, "Peek amount", 1, NOSAVE | DEBUGVAR, 0, 5);
			CVar(BoneSizeSubtract, "Bone size subtract", 1.f, NOSAVE | DEBUGVAR | SLIDER_MIN, 0.f, 4.f, 0.25f);
			CVar(BoneSizeMinimumScale, "Bone size minimum scale", 1.f, NOSAVE | DEBUGVAR | SLIDER_CLAMP, 0.f, 1.f, 0.1f);
		SUBNAMESPACE_END(HITSCAN);


		// botox m_backtrack_enable / m_aimbot_aim_at_backtrack / m_backtrack_time_limit (defaults
		// = botox variables.h). "extended"/"extend amount" (fake-latency window stretch, gated on
		// botox's is_hosting()/sv_lagpushticks) is N/A on CSS: sv_lagpushticks is a CS:GO-only
		// convar, confirmed absent from this CSS x64 build's engine.dll (PowerShell
		// Select-String over the live binary, no match) -- is_hosting() would always be false, so
		// the feature it gates could never fire. See aim/backtrack.h.
		SUBNAMESPACE_BEGIN(Backtrack, Aimbot)
			CVar(Enabled, "Backtrack", false);
			CVar(AimAt, "Aim at backtrack", false);
			CVar(TimeLimit, "Backtrack time", 200, SLIDER_CLAMP, 0, 200, 1, "%d ms");
		SUBNAMESPACE_END(Backtrack);

		// dih-only scope ZOOM-amount hack (gap.md Unit F) -- rescales the actual rendered scope
		// zoom level, NOT the aimbot's targeting cone (that's General::ScaleFovWithScope above,
		// the real botox "scale fov with scope" row). No botox row for this at all; kept as a
		// CSS-real extra; menu row removed 11-08 (user), NOSAVE so always off. Was renamed
		// there to avoid colliding with the botox-named row. See CHLClient_CreateMove (hooks.cpp).
		SUBNAMESPACE_BEGIN(ScopeFOV, Aimbot)
			CVar(Enabled, "Scope zoom scale", false, NOSAVE);
			CVar(Scale, "Scope zoom scale", 100.f, NOSAVE | SLIDER_CLAMP | SLIDER_PRECISION, 50.f, 200.f, 5.f, "%g%%");
		SUBNAMESPACE_END(ScopeFOV);

		// WeaponTuning (3-weapon FOV table: sniper/minigun/scattergun) DELETED -- dead since the
		// CS:GO 1:1 port (11-03 earlier pass already said so, left declared "just in case"; this
		// pass confirmed zero callers anywhere outside its own declaration + grepped the repo) and
		// not a botox row either way. WeaponClass below (every CS weapon class, FOV+smooth+RCS)
		// is what botox's "weapon settings" combo actually maps to.

		// ===== CS:GO 1:1 port additions (11-03), continued =====================================
		// botox rcs_offset / rcs_track / rcs_unwind / rcs_strength, standalone (runs with the
		// aimbot master switch off too -- strength comes from the per-class RCS% below).
		SUBNAMESPACE_BEGIN(RCS, Aimbot)
			CVar(General, "RCS (no class override)", 100.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 100.f, 5.f, "%g%%");
			CVar(Pitch, "RCS pitch", 100.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 100.f, 5.f, "%g%%");
			CVar(Yaw, "RCS yaw", 100.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 100.f, 5.f, "%g%%");
			CVar(Start, "RCS start bullet", 1, SLIDER_MIN, 1, 10);
			CVar(OnlyWithAimbot, "RCS only with aimbot", true);
		SUBNAMESPACE_END(RCS);

		// botox weapon_class_vars(): one override+FOV+smooth+RCS group per CS weapon class
		// (css_weapons.h ECSWeaponClass). Falls back to General::AimFOV / General::Smooth /
		// RCS::General when Override is off, exactly like botox's weapon_settings().
		SUBNAMESPACE_BEGIN(WeaponClass, Aimbot)
			// botox variables.h defaults 1:1: override off, fov 5, smooth 5, rcs 100, every hitbox group on.
			CVar(PistolOverride, "Pistol override", false);
			CVar(PistolFOV, "Pistol FOV", 5.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 180.f);
			CVar(PistolSmooth, "Pistol smooth", 5.f, SLIDER_MIN | SLIDER_PRECISION, 1.f, 30.f, 0.5f);
			CVar(PistolRCS, "Pistol RCS", 100.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 100.f, 5.f, "%g%%");
			CVarValues(PistolHitboxes, "Pistol hitboxes", 0b1111111, DROPDOWN_MULTI, nullptr, VA_LIST("Head", "Neck", "Chest", "Stomach", "Pelvis", "Arms", "Legs"));

			CVar(HeavyPistolOverride, "Heavy pistol override", false);
			CVar(HeavyPistolFOV, "Heavy pistol FOV", 5.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 180.f);
			CVar(HeavyPistolSmooth, "Heavy pistol smooth", 5.f, SLIDER_MIN | SLIDER_PRECISION, 1.f, 30.f, 0.5f);
			CVar(HeavyPistolRCS, "Heavy pistol RCS", 100.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 100.f, 5.f, "%g%%");
			CVarValues(HeavyPistolHitboxes, "Heavy pistol hitboxes", 0b1111111, DROPDOWN_MULTI, nullptr, VA_LIST("Head", "Neck", "Chest", "Stomach", "Pelvis", "Arms", "Legs"));

			CVar(SMGOverride, "SMG override", false);
			CVar(SMGFOV, "SMG FOV", 5.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 180.f);
			CVar(SMGSmooth, "SMG smooth", 5.f, SLIDER_MIN | SLIDER_PRECISION, 1.f, 30.f, 0.5f);
			CVar(SMGRCS, "SMG RCS", 100.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 100.f, 5.f, "%g%%");
			CVarValues(SMGHitboxes, "SMG hitboxes", 0b1111111, DROPDOWN_MULTI, nullptr, VA_LIST("Head", "Neck", "Chest", "Stomach", "Pelvis", "Arms", "Legs"));

			CVar(RifleOverride, "Rifle override", false);
			CVar(RifleFOV, "Rifle FOV", 5.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 180.f);
			CVar(RifleSmooth, "Rifle smooth", 5.f, SLIDER_MIN | SLIDER_PRECISION, 1.f, 30.f, 0.5f);
			CVar(RifleRCS, "Rifle RCS", 100.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 100.f, 5.f, "%g%%");
			CVarValues(RifleHitboxes, "Rifle hitboxes", 0b1111111, DROPDOWN_MULTI, nullptr, VA_LIST("Head", "Neck", "Chest", "Stomach", "Pelvis", "Arms", "Legs"));

			CVar(SniperOverride, "Sniper override", false);
			CVar(SniperFOV, "Sniper FOV", 5.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 180.f);
			CVar(SniperSmooth, "Sniper smooth", 5.f, SLIDER_MIN | SLIDER_PRECISION, 1.f, 30.f, 0.5f);
			CVar(SniperRCS, "Sniper RCS", 100.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 100.f, 5.f, "%g%%");
			CVarValues(SniperHitboxes, "Sniper hitboxes", 0b1111111, DROPDOWN_MULTI, nullptr, VA_LIST("Head", "Neck", "Chest", "Stomach", "Pelvis", "Arms", "Legs"));

			CVar(ScoutOverride, "Scout override", false);
			CVar(ScoutFOV, "Scout FOV", 5.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 180.f);
			CVar(ScoutSmooth, "Scout smooth", 5.f, SLIDER_MIN | SLIDER_PRECISION, 1.f, 30.f, 0.5f);
			CVar(ScoutRCS, "Scout RCS", 100.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 100.f, 5.f, "%g%%");
			CVarValues(ScoutHitboxes, "Scout hitboxes", 0b1111111, DROPDOWN_MULTI, nullptr, VA_LIST("Head", "Neck", "Chest", "Stomach", "Pelvis", "Arms", "Legs"));

			CVar(HeavyOverride, "Heavy override", false);
			CVar(HeavyFOV, "Heavy FOV", 5.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 180.f);
			CVar(HeavySmooth, "Heavy smooth", 5.f, SLIDER_MIN | SLIDER_PRECISION, 1.f, 30.f, 0.5f);
			CVar(HeavyRCS, "Heavy RCS", 100.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 100.f, 5.f, "%g%%");
			CVarValues(HeavyHitboxes, "Heavy hitboxes", 0b1111111, DROPDOWN_MULTI, nullptr, VA_LIST("Head", "Neck", "Chest", "Stomach", "Pelvis", "Arms", "Legs"));
		SUBNAMESPACE_END(WeaponClass);

		// botox hacks/aimbot/grenade_aim.cpp/h -- CSS has flash/HE/smoke only (css_weapons.h
		// CSGrenadeSlot), no molotov/incendiary/decoy/tagrenade (CS:GO-only weapons).
		SUBNAMESPACE_BEGIN(Grenade, Aimbot)
			CVar(Enabled, "Grenade aimbot", false);
			CVar(Key, "Grenade aim key", 0, NOBIND);
			CVarEnum(KeyMode, "Grenade aim key mode", 1, NOBIND, nullptr,
				VA_LIST("Always on", "On hotkey", "Toggle"),
				AlwaysOn, OnHotkey, Toggle);
			// botox menu.cpp:1040 label is just "grenades".
			CVarEnum(Types, "Grenades", 0b111, DROPDOWN_MULTI, nullptr,
				VA_LIST("Flashbang", "HE grenade", "Smoke"),
				Flash = 1 << 0, HE = 1 << 1, Smoke = 1 << 2);
			// botox menu.cpp:1041-1046 labels: "grenade fov", "arc", "silent", "auto throw",
			// "draw path" (the ##grenade aim suffix is an ImGui id, not part of the label).
			CVar(FOV, "Grenade FOV", 30.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 180.f);
			CVarEnum(Arc, "Arc", 0, NONE, nullptr,
				VA_LIST("Auto", "Low", "Lob"),
				Auto, Low, Lob);
			// botox "wall bounce" (m_grenade_aim_bounce, menu.cpp:1043): tries a one-reflection
			// path off a nearby wall when no direct/lob throw connects. Ported CSS-adapted
			// (grenade_math.h BounceVelocity/BounceLaunch) -- see grenade_aim.cpp FindWalls/
			// BounceTimes for the CSS speed-pitch-coupling difference from botox's fixed-speed
			// closed form.
			CVar(WallBounce, "Wall bounce", false);
			CVar(Silent, "Silent", false);
			CVar(AutoThrow, "Auto throw", false);
			CVar(DrawPath, "Draw path", true);
			CVar(PathColor, "Grenade path color", Color_t(255, 200, 0, 255), VISUAL);
		SUBNAMESPACE_END(Grenade);

	NAMESPACE_END(Aimbot);
	
	NAMESPACE_BEGIN(Doubletap)
		CVar(Doubletap, "Doubletap", false);
		CVar(Warp, "Warp", false);
		CVar(RechargeTicks, "Recharge ticks", false);
		CVar(AntiWarp, "Anti-warp", false);
		CVar(TickLimit, "Tick limit", 22, SLIDER_CLAMP, 2, 22);
		CVar(WarpRate, "Warp rate", 22, SLIDER_CLAMP, 2, 22);
		CVar(PassiveRecharge, "Passive recharge", 0, SLIDER_CLAMP, 0, 67);
	NAMESPACE_END(DoubleTap)

	NAMESPACE_BEGIN(Speedhack)
		CVar(Enabled, VA_LIST("Enabled", "Speedhack enabled"), false);
		CVar(Amount, VA_LIST("Amount", "SpeedHack amount"), 1, NONE, 1, 50);
	NAMESPACE_END(Speedhack);

	NAMESPACE_BEGIN(Visuals)
		SUBNAMESPACE_BEGIN(UI)
			CVar(RevealScoreboard, "Reveal scoreboard", false, VISUAL);
			CVar(CleanScreenshots, "Clean screenshots", false);
		SUBNAMESPACE_END(UI);

		SUBNAMESPACE_BEGIN(Thirdperson)
			CVar(Enabled, "Thirdperson", false, VISUAL);
			CVar(Distance, "Thirdperson distance", 150.f, VISUAL | SLIDER_PRECISION, 0.f, 400.f, 10.f);
			CVar(Up, "Thirdperson up", 0.f, VISUAL | SLIDER_PRECISION, -100.f, 100.f, 5.f);

			CVar(Scale, "Thirdperson scales", true, NOSAVE | DEBUGVAR);
		SUBNAMESPACE_END(ThirdPerson);

		SUBNAMESPACE_BEGIN(Removals)
			CVar(Interpolation, VA_LIST("Interpolation", "Remove interpolation"), false);
			CVar(Lerp, VA_LIST("Lerp", "Remove lerp"), false);
			CVar(Scope, VA_LIST("Scope", "Remove scope"), false, VISUAL);
			CVar(AngleForcing, VA_LIST("Angle forcing", "Remove angle forcing"), false, VISUAL);
		SUBNAMESPACE_END(Removals);

		SUBNAMESPACE_BEGIN(Viewmodel)
			CVar(OffsetX, VA_LIST("Offset X", "Viewmodel offset X"), 0.f, VISUAL | SLIDER_PRECISION, -45.f, 45.f, 5.f);
			CVar(OffsetY, VA_LIST("Offset Y", "Viewmodel offset Y"), 0.f, VISUAL | SLIDER_PRECISION, -45.f, 45.f, 5.f);
			CVar(OffsetZ, VA_LIST("Offset Z", "Viewmodel offset Z"), 0.f, VISUAL | SLIDER_PRECISION, -45.f, 45.f, 5.f);
		SUBNAMESPACE_END(Viewmodel);

		// Weather: fog changer + native precipitation (rain / snow),. See CWeather.
		SUBNAMESPACE_BEGIN(Beams) // as of now, these will stay out of the menu
			CVar(Model, "Model", std::string("sprites/physbeam.vmt"), VISUAL);
			CVar(Life, "Life", 2.f, VISUAL | SLIDER_MIN | SLIDER_PRECISION, 0.f, 10.f);
			CVar(Speed, "Speed", 0.2f, VISUAL | SLIDER_MIN | SLIDER_PRECISION, 0.f, 5.f);
		SUBNAMESPACE_END(Beams);

		SUBNAMESPACE_BEGIN(Line)
			CVar(Enabled, "Line tracers", false, VISUAL);
			CVar(DrawDuration, VA_LIST("Draw duration", "Line draw duration"), 5.f, VISUAL | SLIDER_MIN | SLIDER_PRECISION, 0.f, 10.f);
		SUBNAMESPACE_END(Line);

		SUBNAMESPACE_BEGIN(Hitbox)
			CVarEnum(BoundsEnabled, VA_LIST("Bounds enabled", "Hitbox bounds enabled"), 0b000, VISUAL | DROPDOWN_MULTI, "Off",
				VA_LIST("On shot", "On hit", "Aim point"),
				OnShot = 1 << 0, OnHit = 1 << 1, AimPoint = 1 << 2);
			CVar(DrawDuration, VA_LIST("Draw duration", "Hitbox draw duration"), 5.f, VISUAL | SLIDER_MIN | SLIDER_PRECISION, 0.f, 10.f);
		SUBNAMESPACE_END(Hitbox);

		SUBNAMESPACE_BEGIN(Simulation)
			Enum(Style, Off, Line, Separators, Spaced, Arrows, Boxes);
			CVarValues(PlayerPath, "Player path", 0, VISUAL, nullptr,
				"Off", "Line", "Separators", "Spaced", "Arrows", "Boxes");
			CVarValues(ProjectilePath, "Projectile path", 0, VISUAL, nullptr,
				"Off", "Line", "Separators", "Spaced", "Arrows", "Boxes");
			CVar(Timed, VA_LIST("Timed", "Timed path"), false, VISUAL);
			CVar(DrawDuration, VA_LIST("Draw duration", "Simulation draw duration"), 5.f, VISUAL | SLIDER_MIN | SLIDER_PRECISION, 0.f, 10.f);

			CVarValues(RealPath, "Real path", 0, NOSAVE | DEBUGVAR, nullptr,
				"Off", "Line", "Separators", "Spaced", "Arrows", "Boxes");
		SUBNAMESPACE_END(Simulation);

		SUBNAMESPACE_BEGIN(Trajectory)
			CVar(Override, "Simulation override", false, NOSAVE | DEBUGVAR);
			CVar(OffsetX, "Offset X", 16.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, -25.f, 25.f, 0.5f);
			CVar(OffsetY, "Offset Y", 8.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, -25.f, 25.f, 0.5f);
			CVar(OffsetZ, "Offset Z", -6.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, -25.f, 25.f, 0.5f);
			CVar(Pipes, "Pipes", true, NOSAVE | DEBUGVAR);
			CVar(Hull, "Hull", 5.f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0.f, 10.f, 0.5f);
			CVar(Speed, "Speed", 1200.f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0.f, 5000.f, 50.f);
			CVar(Gravity, "Gravity", 1.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, 0.f, 1.f, 0.1f);
			CVar(LifeTime, "Life time", 2.2f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0.f, 10.f, 0.1f);
			CVar(UpVelocity, "Up velocity", 200.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, 0.f, 1000.f, 50.f);
			CVar(AngularVelocityX, "Angular velocity X", 600.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, -1000.f, 1000.f, 50.f);
			CVar(AngularVelocityY, "Angular velocity Y", -1200.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, -1000.f, 1000.f, 50.f);
			CVar(AngularVelocityZ, "Angular velocity Z", 0.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, -1000.f, 1000.f, 50.f);
			CVar(Drag, "Drag", 1.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, 0.f, 2.f, 0.1f);
			CVar(DragX, "Drag X", 0.003902f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, 0.f, 0.1f, 0.01f, "%.15g");
			CVar(DragY, "Drag Y", 0.009962f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, 0.f, 0.1f, 0.01f, "%.15g");
			CVar(DragZ, "Drag Z", 0.009962f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, 0.f, 0.1f, 0.01f, "%.15g");
			CVar(AngularDragX, "Angular drag X", 0.003618f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, 0.f, 0.1f, 0.01f, "%.15g");
			CVar(AngularDragY, "Angular drag Y", 0.001514f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, 0.f, 0.1f, 0.01f, "%.15g");
			CVar(AngularDragZ, "Angular drag Z", 0.001514f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, 0.f, 0.1f, 0.01f, "%.15g");
			CVar(MaxVelocity, "Max velocity", 2000.f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0.f, 4000.f, 50.f);
			CVar(MaxAngularVelocity, "Max angular velocity", 3600.f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0.f, 7200.f, 50.f);
		SUBNAMESPACE_END(ProjectileTrajectory);

		// ESP tab "Model Preview" window: renders the heavy player model (stand_MELEE)
		SUBNAMESPACE_BEGIN(ModelPreview)
			CVar(Enabled, "Model preview", false, VISUAL);
		SUBNAMESPACE_END(ModelPreview);
	NAMESPACE_END(Visuals);

	NAMESPACE_BEGIN(Misc)
		SUBNAMESPACE_BEGIN(Movement)
			CVar(Bunnyhop, "Bunnyhop", false);
			CVar(LongJump, "Long jump", false);

		SUBNAMESPACE_END(Movement);

		SUBNAMESPACE_BEGIN(Game)
			CVar(AntiCheatCompatibility, "Anti-cheat compatibility", false);
			// Write your real view angle into your local POV demos so silent aim never snaps the first-person camera.
			CVar(CleanPOVDemos, "Clean POV demos", false);

		SUBNAMESPACE_END(Game);

	NAMESPACE_END(Misc);

	NAMESPACE_BEGIN(Debug)
		CVar(Info, "Debug info", false, NOSAVE);
		CVar(Logging, "Debug logging", false, NOSAVE);
	NAMESPACE_END(Debug);

}