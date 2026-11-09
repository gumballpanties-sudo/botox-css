#include "aim.h"
#include "backtrack.h"

#include "sdk.h"
#include "events.h"
#include "core.h"
#include "aimbot.h"
#include "menu.h"
#include "hud_aim.h"   // unit hud: HudAim_Restore on unload
#include "../visuals/esp_preview.h"   // EspPreview::OnEject

#include "../../dependencies/imgui/imgui.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

void DebugLog(const char* fmt, ...);


// =============================================================================================
// load / unload
// =============================================================================================

static bool s_bActive = false;
static bool s_bTried  = false;

bool Aim_Active() { return s_bActive; }

// CBaseHudChat::ChatPrintf (vtable 19). Text goes in as the format: caller strips '%'.
void Aim_ChatPrint(const char* text)
{
	if (I::ClientModeShared && I::ClientModeShared->m_pChatElement)
		I::ClientModeShared->m_pChatElement->ChatPrintf(0, text);
}

// The hooks the aim half installs. Every other MAKE_HOOK in hooks.cpp is compiled (so it
// registers) but never initialised. Rules used to pick this list:
//   - it feeds the aimbot (entity cache, bone/anim fidelity, prediction), or
//   - it is what makes silent/psilent aim actually silent (the cmd encode + input read path), or
//   - it is the clean-POV-demo fix the user asked for (CPrediction::GetLocalViewAngles), and
//   - it is NOT a target dih already hooks (CreateMove is the exception: it moved here whole,
//     see CHLClient_CreateMove in hooks.cpp).
static const std::vector<std::string> s_vHookWhitelist =
{
	// command path -- the aimbot pipeline itself, and the tick manipulation it rides on
	"CHLClient_CreateMove",
	"CBasePlayer_ItemPostFrame",

	// silent aim: keep the engine from writing our aim angle back into the real view, and
	// keep the encoded command carrying the aim angle while the view keeps the real one.
	//
	// CInput_GetUserCmd and CInput_ValidateUserCmd are gone from this tree entirely: dih hooks
	// both itself, off the same signatures and with the same bodies (movement.cpp, h_GetUserCmd
	// / h_ValidateUserCmd -- they are what makes dih's own post-CreateMove button edits survive).
	// MinHook allows one hook per target, so installing them from both halves would just make
	// one of the two fail.
	//
	// IEngineTrace_TraceRay / IEngineTrace_SetTraceEntity are gone too. The TraceRay hook only
	// ever existed inside "#ifdef DEBUG_TRACES" (never defined), so whitelisting it installed
	// nothing, and SetTraceEntity's CONTENTS_NOSTARTSOLID restore read a mask that only the
	// never-whitelisted CM_BoxTrace hook ever wrote -- it was a pure passthrough.
	"CHLClient_EncodeUserCmdToBuffer",
	"CClientState_ProcessFixAngle",

	// clean POV demos -- the demo cmdinfo's local view angle
	"CPrediction_GetLocalViewAngles",

	// entity data the target selection and the projectile lead read. RecvProxy_SimulationTime
	// is what makes a choking player's simulation time correct, and H::Entities::Store builds
	// the per-target velocity history off that delta -- the projectile aimbot leads on it.
	"CHLClient_FrameStageNotify",
	"RecvProxy_SimulationTime",
	"CL_ProcessPacketEntities",

	// bone + animation fidelity for hitbox aiming
	"CBaseAnimating_UpdateClientSideAnimation",
	"CBaseAnimating_MaintainSequenceTransitions",
	"CBaseAnimating_Interpolate",
	"CBaseViewModel_Interpolate",   // remove interp: viewmodel cycle steps per tick
	"CBaseAnimating_SetSequence",
	"CBaseEntity_BaseInterpolatePart1",
	"CBaseEntity_ResetLatched",

	// movement simulation (projectile lead) + engine prediction
	"CBaseEntity_SetAbsVelocity",
	"CBaseEntity_EstimateAbsVelocity",
	"CPrediction_RunSimulation",

	// the aimbot's own on-screen overlays (tick indicator, crit / no-spread readout)
	"IEngineVGui_Paint",

	// esp preview fake entity out of a map (esp_preview.cpp): our model_t*, neutral light state
	"CModelInfoClient_GetModel",
	"R_LightcacheGetDynamic",

	// silences the datatable spam the netvar walk provokes
	"DataTable_Warning",

	// CHLClient_DispatchUserMessage NOT here: its body is a dead pass-through (hooks.cpp) and it
	// took slot 36 from removals/chat translator (visuals.cpp h_DispatchUserMessage, st=3).
};

bool Aim_Init()
{
	if (s_bTried)
		return s_bActive;
	s_bTried = true;

	if (!U::Signatures.Initialize())
	{
		DebugLog("aim: signature init failed");
		return false;
	}
	if (!U::Interfaces.Initialize())
	{
		DebugLog("aim: interface init failed");
		return false;
	}
	if (!H::Interfaces.Initialize())
		DebugLog("aim: null-interface init incomplete (steam side), continuing");

	if (!U::Hooks.Initialize(s_vHookWhitelist))
	{
		DebugLog("aim: hook init failed");
		return false;
	}
	if (!H::Events.Initialize())
		DebugLog("aim: event listener init incomplete, continuing");

	H::ConVars.Unlock();
	H::Fonts.Reload(Vars::Menu::Scale[DEFAULT_BIND]);
	// No config load here: dih's own LoadConfig already ran (MainThread, before PollThread) and
	// it carries our vars now -- see the config section below.

	s_bActive = true;
	DebugLog("aim: aimbot subsystem loaded (%d hooks)", int(s_vHookWhitelist.size()));
	return true;
}

void Aim_Unload()
{
	if (!s_bActive)
		return;

	G::Unload = true;
	EspPreview::OnEject();
	HudAim_Restore();   // unit hud: net_graphtext back to the player's value
	U::Hooks.Unload();
	F::Backtrack.Release();   // after the hooks: no FSN / CreateMove writes into the rings
	H::Events.Unload();
	H::ConVars.Restore();
	s_bActive = false;
}


// =============================================================================================
// stubs: fail-text sink + menu console line
// =============================================================================================

// Both sinks end in dih's debug.log. DebugLog is declared at the top of this file rather than
// by including globals.h -- that header drags the whole dih feature surface in with it.
void CCore::AppendFailText(const char* sMessage)
{
	DebugLog("aim: %s", sMessage ? sMessage : "(null)");
}

// The menu console line. dih has no such pane, so the line goes to debug.log --
// same information, same call sites, no ImGui dependency.
void CMenu::AddOutput(const char* sFunction, const char* sLog, Color_t tColor)
{
	std::lock_guard tLock(m_tMutex);

	m_vOutput.emplace_front(sFunction ? sFunction : "", sLog ? sLog : "", m_vOutput.size(), tColor);
	if (m_vOutput.size() > m_iMaxOutputSize)
		m_vOutput.pop_back();

	DebugLog("aim %s %s", sFunction ? sFunction : "", sLog ? sLog : "");
}


// =============================================================================================
// config: aim vars inside dih's own config file
// =============================================================================================

// Persistence for the aim vars, written INTO dih's own config file.
//
// The aim tree has no config file of its own: dih's SaveConfig / LoadConfig
// (src/hacks/menu/config.cpp) call the two functions here, so the aim settings live in
// C:otox-tf2\default.cfg next to everything else and the menu's existing save/load buttons cover
// them with no extra button.
//
// Keys are the var's own registered name ("Vars::Aimbot::General::AimFOV"), which is unique and
// contains no '=' -- dih's parser splits on the first one. Only the four scalar types the aim
// tree uses are handled; no aim var is a string, gradient or box.

// Every var whose name starts with one of these is ours. Vars.h registers the whole var tree
// (menu theme, ESP colours, ...) including entries no compiled feature reads, and writing all
// of it would fill dih's config with dead keys.
static bool IsAimVar(const std::string& sName)
{
	static const char* kPrefixes[] =
	{
		"Vars::Aimbot::",
		"Vars::Misc::Game::CleanPOVDemos",
		"Vars::Misc::Game::AntiCheatCompatibility",
	};
	for (auto sPrefix : kPrefixes)
	{
		if (sName.rfind(sPrefix, 0) == 0)
			return true;
	}
	return false;
}

void Aim_SaveVars(FILE* f)
{
	if (!f)
		return;

	for (auto pBase : G::Vars)
	{
		if (pBase->m_iFlags & NOSAVE || !IsAimVar(pBase->m_sName))
			continue;

		if (auto pVar = pBase->As<bool>())
			fprintf(f, "%s=%d\n", pVar->m_sName.c_str(), pVar->Value ? 1 : 0);
		else if (auto pVar2 = pBase->As<int>())
			fprintf(f, "%s=%d\n", pVar2->m_sName.c_str(), pVar2->Value);
		else if (auto pVar3 = pBase->As<float>())
			fprintf(f, "%s=%.4f\n", pVar3->m_sName.c_str(), pVar3->Value);
		else if (auto pVar4 = pBase->As<Color_t>())
			fprintf(f, "%s=%d %d %d %d\n", pVar4->m_sName.c_str(),
				pVar4->Value.r, pVar4->Value.g, pVar4->Value.b, pVar4->Value.a);
	}
}

bool Aim_LoadVar(const char* szKey, const char* szValue)
{
	if (!szKey || !szValue)
		return false;
	// Cheap reject before the linear walk: every key of ours starts with the namespace.
	if (strncmp(szKey, "Vars::", 6) != 0)
		return false;

	for (auto pBase : G::Vars)
	{
		if (pBase->m_sName != szKey)
			continue;
		// NOSAVE = no menu row: eat old saved value, keep default
		if (pBase->m_iFlags & NOSAVE)
			return true;

		// Map[DEFAULT_BIND] is the value the config layer round-trips; Value is what the
		// features read. Both are set so a load takes effect without a menu interaction.
		if (auto pVar = pBase->As<bool>())
			pVar->Value = pVar->Map[DEFAULT_BIND] = atoi(szValue) != 0;
		else if (auto pVar2 = pBase->As<int>())
			pVar2->Value = pVar2->Map[DEFAULT_BIND] = atoi(szValue);
		else if (auto pVar3 = pBase->As<float>())
			pVar3->Value = pVar3->Map[DEFAULT_BIND] = float(atof(szValue));
		else if (auto pVar4 = pBase->As<Color_t>())
		{
			int r = 0, g = 0, b = 0, a = 0;
			if (sscanf(szValue, "%d %d %d %d", &r, &g, &b, &a) == 4)
				pVar4->Value = pVar4->Map[DEFAULT_BIND] = Color_t(byte(r), byte(g), byte(b), byte(a));
		}
		else
			return false;

		return true;
	}

	return false;
}


// =============================================================================================
// menu: the aimbot tab body
// =============================================================================================

// The aimbot tab in dih's own menu (src/hacks/menu/menu.cpp calls Aim_RenderMenuTab from its
// tab switch). It lives on this side because it reads the aim tree's ConfigVar objects, and
// those drag in Vars.h/SDK.h -- which dih's menu.cpp must not include.
//
// Nothing here hardcodes a label, a range or a dropdown entry. Every widget reads the metadata
// the CVar macro already recorded on the var (m_vTitle, m_vValues, m_unMin/Max/Step, m_sExtra,
// the SLIDER_*/DROPDOWN_* flags), so the options, their names and their limits come from the
// var declarations by construction and stay correct if one changes.
//
// Layout is botox tab_aimbot 1:1: menu_columns_begin/next/end + menu_group_begin/end boxes,
// a feature row is a Checkbox, its secondary tuning sits behind that row's cog (OptionPopup).
#include "../menu/menu_chrome.h"

// Can't include src/hacks/menu/menu.h here: it pulls in dih's own SDK (src/globals.h), which
// redefines type names (ConVar, RecvProp, ...) that aim/defs.h already defines differently --
// same wall as the MenuSection decls above used to work around. Signatures copied from there.
namespace ImGui
{
	void OptionPopup(const char* str_id, const std::function<void()>& function, const ImVec2& window_size);
	bool Keybind(const char* label, int* key, int* mode, bool has_style = true);
}

// Rest of dih's menu is all lowercase, so every label the var metadata hands us gets folded
// here rather than in the declarations -- the vars keep their own capitalisation.
static std::string LowerLabel(const std::string& sText)
{
	std::string sOut = sText;
	for (char& c : sOut)
		c = char(std::tolower((unsigned char)c));
	return sOut;
}

// Var titles are sometimes "Label##uniqueid" and sometimes a { short, long } pair.
// Take the short one and cut at "##" -- ImGui would treat the tail as an id anyway, but the
// var name is a better id than a shared suffix.
static std::string VarLabel(BaseVar* pVar)
{
	std::string sTitle = pVar->m_vTitle.empty() ? pVar->m_sName : pVar->m_vTitle.front();
	if (auto i = sTitle.find("##"); i != std::string::npos)
		sTitle = sTitle.substr(0, i);
	return LowerLabel(sTitle) + "##" + pVar->m_sName;
}

static void VarToggle(ConfigVar<bool>& tVar)
{
	bool bValue = tVar.Value;
	if (ImGui::Checkbox(VarLabel(&tVar).c_str(), &bValue))
		tVar.Value = tVar.Map[DEFAULT_BIND] = bValue;
}

static void VarSlider(ConfigVar<float>& tVar)
{
	float flValue = tVar.Value;
	const char* szFormat = tVar.m_sExtra ? tVar.m_sExtra : "%g";
	// A value already outside the declared range keeps the bar draggable instead of
	// pinning the thumb at an end -- same rule as dih's own sliders.
	float flMin = tVar.m_unMin.f, flMax = tVar.m_unMax.f;
	if (flValue < flMin) flMin = flValue;
	if (flValue > flMax) flMax = flValue;
	if (ImGui::SliderFloat(VarLabel(&tVar).c_str(), &flValue, flMin, flMax, szFormat))
		tVar.Value = tVar.Map[DEFAULT_BIND] = flValue;
}

static void VarSlider(ConfigVar<int>& tVar)
{
	int iValue = tVar.Value;
	const char* szFormat = tVar.m_sExtra ? tVar.m_sExtra : "%d";
	int iMin = tVar.m_unMin.i, iMax = tVar.m_unMax.i;
	if (iValue < iMin) iMin = iValue;
	if (iValue > iMax) iMax = iValue;
	if (ImGui::SliderInt(VarLabel(&tVar).c_str(), &iValue, iMin, iMax, szFormat))
		tVar.Value = tVar.Map[DEFAULT_BIND] = iValue;
}

// Single-select enum: m_vValues in declaration order, value == index.
static void VarCombo(ConfigVar<int>& tVar)
{
	if (tVar.m_vValues.empty())
		return;

	// Names must outlive the Combo call, so keep the lowercased strings alive next to the
	// pointer array ImGui reads.
	std::vector<std::string> vNames;
	for (auto szItem : tVar.m_vValues)
	{
		if (!szItem || std::string(szItem) == "##Divider")
			continue;
		vNames.push_back(LowerLabel(szItem));
	}

	std::vector<const char*> vItems;
	for (auto& sName : vNames)
		vItems.push_back(sName.c_str());

	int iValue = std::clamp(tVar.Value, 0, int(vItems.size()) - 1);
	if (ImGui::Combo(VarLabel(&tVar).c_str(), &iValue, vItems.data(), int(vItems.size())))
		tVar.Value = tVar.Map[DEFAULT_BIND] = iValue;
}

// Bitmask dropdown (DROPDOWN_MULTI): one bit per NON-divider entry, in order. Dividers are
// layout only and do NOT consume a bit.
// Color_t is bytes (0-255); ImGui::ColorEdit4 wants floats (0-1) -- convert both ways (same
// shape as the menu VarColor helpers).
static void VarColor(ConfigVar<Color_t>& tVar, const char* szLabel)
{
	float col[4] = { tVar.Value.r / 255.f, tVar.Value.g / 255.f, tVar.Value.b / 255.f, tVar.Value.a / 255.f };
	if (ImGui::ColorEdit4(szLabel, col))
	{
		Color_t c{ (byte)(col[0] * 255.f + 0.5f), (byte)(col[1] * 255.f + 0.5f),
			(byte)(col[2] * 255.f + 0.5f), (byte)(col[3] * 255.f + 0.5f) };
		tVar.Value = tVar.Map[DEFAULT_BIND] = c;
	}
}

static void VarMulti(ConfigVar<int>& tVar, const char* szLabel = nullptr)
{
	if (tVar.m_vValues.empty())
		return;

	// Preview: the selected names, or the var's own "none" label when nothing is set.
	std::string sPreview;
	{
		int iBit = 0;
		for (auto szItem : tVar.m_vValues)
		{
			if (!szItem || std::string(szItem) == "##Divider")
				continue;
			if (tVar.Value & (1 << iBit))
				sPreview += (sPreview.empty() ? "" : ", ") + LowerLabel(szItem);
			iBit++;
		}
		if (sPreview.empty())
			sPreview = LowerLabel(tVar.m_sExtra ? tVar.m_sExtra : "none");
	}

	if (ImGui::BeginCombo(szLabel ? szLabel : VarLabel(&tVar).c_str(), sPreview.c_str()))
	{
		int iBit = 0;
		for (auto szItem : tVar.m_vValues)
		{
			if (!szItem || std::string(szItem) == "##Divider")
			{
				ImGui::Separator();
				continue;
			}

			const int iMask = 1 << iBit++;
			const bool bOn = (tVar.Value & iMask) != 0;
			// botox MultiCombo rows: Selectable, toggles without closing
			if (ImGui::Selectable((LowerLabel(szItem) + "##" + tVar.m_sName).c_str(), bOn, ImGuiSelectableFlags_DontClosePopups))
				tVar.Value = tVar.Map[DEFAULT_BIND] = tVar.Value ^ iMask;
		}
		ImGui::EndCombo();
	}
}


void Aim_RenderMenuTab()
{
	using namespace Vars;

	if (!Aim_Active())
	{
		ImGui::TextDisabled("aimbot subsystem not loaded -- see C:\\botox-css\\debug.log");
		return;
	}

	// botox csgo-sdk hacks/menu/menu.cpp RunAimbotTab 1:1: "main" (master + per-category weapon
	// settings) / "configuration" (rcs/autowall/backtrack/nospread) / "grenade aimbot" groups.
	// TF2-only groups this used to render (hitscan/projectile/melee/backstab/CritHack, demos) are
	// gone -- CS:S has one gun-aim path (hitscan) + a separate always-polled grenade aimbot, no
	// melee/backstab/crit concept (AGENT_BRIEF DE-TF2 rule). Zeus bug (botox's "configuration"
	// group also has a zeus x27 bug row) is NOT ported: the Zeus x27 taser is a CS:GO-only
	// weapon, absent from cs_weapon_parse.h/CSWeaponID -- can't exist in CSS, said here per user
	// direction instead of faked.
	// botox menu.cpp:926-948 weapon_configs[aim_config_max], same order + m_name strings
	struct WeaponCategory_t
	{
		const char* m_sName;
		ConfigVar<bool>* m_pOverride;
		ConfigVar<float>* m_pFOV;
		ConfigVar<float>* m_pSmooth;
		ConfigVar<float>* m_pRCS;
		ConfigVar<int>* m_pHitboxes;
	};
	using namespace Vars::Aimbot;
	static WeaponCategory_t s_vCategories[] =
	{
		{ "general", nullptr, &General::AimFOV, &General::Smooth, &RCS::General, &Hitscan::HitboxGroups },
		{ "pistol", &WeaponClass::PistolOverride, &WeaponClass::PistolFOV, &WeaponClass::PistolSmooth, &WeaponClass::PistolRCS, &WeaponClass::PistolHitboxes },
		{ "heavy pistol", &WeaponClass::HeavyPistolOverride, &WeaponClass::HeavyPistolFOV, &WeaponClass::HeavyPistolSmooth, &WeaponClass::HeavyPistolRCS, &WeaponClass::HeavyPistolHitboxes },
		{ "rifle", &WeaponClass::RifleOverride, &WeaponClass::RifleFOV, &WeaponClass::RifleSmooth, &WeaponClass::RifleRCS, &WeaponClass::RifleHitboxes },
		{ "smg", &WeaponClass::SMGOverride, &WeaponClass::SMGFOV, &WeaponClass::SMGSmooth, &WeaponClass::SMGRCS, &WeaponClass::SMGHitboxes },
		{ "sniper", &WeaponClass::SniperOverride, &WeaponClass::SniperFOV, &WeaponClass::SniperSmooth, &WeaponClass::SniperRCS, &WeaponClass::SniperHitboxes },
		{ "scout", &WeaponClass::ScoutOverride, &WeaponClass::ScoutFOV, &WeaponClass::ScoutSmooth, &WeaponClass::ScoutRCS, &WeaponClass::ScoutHitboxes },
		{ "heavy", &WeaponClass::HeavyOverride, &WeaponClass::HeavyFOV, &WeaponClass::HeavySmooth, &WeaponClass::HeavyRCS, &WeaponClass::HeavyHitboxes },
	};

	menu_columns_begin();

	// ---- main -------------------------------------------------------------------------
	// botox menu.cpp:952-983 row order 1:1: master switch, aim on key + aim key, silent aim +
	// silent aim step, camlock, no target switching, target team, scale fov with scope, then
	// the weapon settings combo (general/pistols/.../heavy, each with its own fov/smoothness/
	// rcs -- "general" IS General::AimFOV/Smooth/RCS::General, so there is no separate "aim fov"
	// row outside the combo, same as botox). Everything after the combo (max targets, auto shoot,
	// target/ignore multi-selects, tick tolerance) is a dih extra with no botox row at all --
	// kept, grouped at the end so the botox-matching rows read top to bottom in botox's order.
	if (menu_group_begin("main"))
	{
		VarToggle(General::Enabled);

		{
			// botox m_aimbot_on_key is a separate bool from the keybind; this tree folds it into
			// AimKeyMode (Always on / On hotkey / Toggle -- Toggle is a dih extra botox has no
			// equivalent for). Checkbox flips Always-on <-> On-hotkey; the Keybind's own mode
			// dropdown below still reaches Toggle directly.
			bool bOnKey = General::AimKeyMode.Value != General::AimKeyModeEnum::AlwaysOn;
			if (ImGui::Checkbox("aim on key", &bOnKey))
				General::AimKeyMode.Value = General::AimKeyMode.Map[DEFAULT_BIND] =
					bOnKey ? General::AimKeyModeEnum::OnHotkey : General::AimKeyModeEnum::AlwaysOn;
		}
		ImGui::Keybind("aim key", &General::AimKey.Value, &General::AimKeyMode.Value);
		General::AimKey.Map[DEFAULT_BIND] = General::AimKey.Value;
		General::AimKeyMode.Map[DEFAULT_BIND] = General::AimKeyMode.Value;

		VarToggle(General::Silent);
		VarSlider(General::SilentStep);

		VarToggle(General::Camlock); // botox: camlock wins over silent when both are set
		VarToggle(General::NoSwitch);
		VarToggle(General::TargetTeam);
		VarToggle(General::ScaleFovWithScope);

		// botox menu.cpp:967-983 1:1: combo, "override general##<name>" (not on general), then
		// "<name> fov" / "<name> smoothness" / "<name> rcs" / "hitboxes##<name>", always shown
		int& iCategory = General::WeaponSettings.Value;
		iCategory = std::clamp(iCategory, 0, int(std::size(s_vCategories)) - 1);
		if (ImGui::Combo("weapon settings", &iCategory, "general\0pistols\0heavy pistols\0rifles\0smg\0sniper\0scout\0heavy\0"))
			General::WeaponSettings.Map[DEFAULT_BIND] = iCategory;
		auto& tCat = s_vCategories[iCategory];
		auto Slider = [](ConfigVar<float>& tVar, const std::string& sLabel, float flMin, float flMax)
		{
			float flValue = tVar.Value;
			if (ImGui::SliderFloat(sLabel.c_str(), &flValue, flMin, flMax, "%.0f"))
				tVar.Value = tVar.Map[DEFAULT_BIND] = flValue;
		};
		if (tCat.m_pOverride)
		{
			bool bOverride = tCat.m_pOverride->Value;
			if (ImGui::Checkbox(std::format("override general##{}", tCat.m_sName).c_str(), &bOverride))
				tCat.m_pOverride->Value = tCat.m_pOverride->Map[DEFAULT_BIND] = bOverride;
		}
		Slider(*tCat.m_pFOV, std::format("{} fov", tCat.m_sName), 0.f, 180.f);
		Slider(*tCat.m_pSmooth, std::format("{} smoothness", tCat.m_sName), 1.f, 30.f);
		Slider(*tCat.m_pRCS, std::format("{} rcs", tCat.m_sName), 0.f, 100.f);
		VarMulti(*tCat.m_pHitboxes, std::format("hitboxes##{}", tCat.m_sName).c_str());

		// ---- dih extras, no botox row (KEPT EXTRAS, see report) ------------------------
		// target/ignore/tick tolerance rows removed 11-08 (user): vars NOSAVE, stay at default
		VarToggle(General::AutoShoot);
	}
	menu_group_end();

	menu_columns_next();

	// ---- configuration ------------------------------------------------------------------
	if (menu_group_begin("configuration"))
	{
		VarSlider(RCS::Start);
		VarSlider(RCS::Pitch);
		VarSlider(RCS::Yaw);
		VarToggle(RCS::OnlyWithAimbot);

		VarToggle(General::Autowall);
		if (General::Autowall.Value)
			ImGui::OptionPopup("autowall settings", [&]()
			{
				VarSlider(General::AutowallMinDamage);
			}, ImVec2(220.f, -1));

		VarToggle(Backtrack::Enabled);
		if (Backtrack::Enabled.Value)
			ImGui::OptionPopup("backtrack settings", [&]()
			{
				VarToggle(Backtrack::AimAt);
				VarSlider(Backtrack::TimeLimit);
			}, ImVec2(220.f, -1));

		// weapon_accuracy_nospread, read live off the game's own GetSpread()/GetInaccuracy()
		// vtable calls (css.h, see docs/port/vtable_check.md) -- held only across its shot.
		// botox's own nospread popup has an "sm_nospread" sourcemod-plugin toggle (a remote,
		// server-side path CS:GO servers can run): no CSS sourcemod plugin equivalent is wired
		// up in this port, not ported, said here instead of faking a checkbox for it.
		VarToggle(General::NoSpread);
		// modifiers / multipoint rows removed 11-08 (user): vars NOSAVE, stay off
	}
	menu_group_end();

	// ---- grenade aimbot ------------------------------------------------------------------
	if (menu_group_begin("grenade aimbot"))
	{
		VarToggle(Grenade::Enabled);
		ImGui::Keybind("grenade aim key", &Grenade::Key.Value, &Grenade::KeyMode.Value);
		Grenade::Key.Map[DEFAULT_BIND] = Grenade::Key.Value;
		Grenade::KeyMode.Map[DEFAULT_BIND] = Grenade::KeyMode.Value;

		VarMulti(Grenade::Types);
		VarSlider(Grenade::FOV);
		VarCombo(Grenade::Arc);
		VarToggle(Grenade::WallBounce);
		VarToggle(Grenade::Silent);
		VarToggle(Grenade::AutoThrow);
		VarToggle(Grenade::DrawPath);
		if (Grenade::DrawPath.Value)
			ImGui::OptionPopup("grenade aim path settings", [&]()
			{
				VarColor(Grenade::PathColor, "path##grenade_aim_color");
			}, ImVec2(220.f, -1));
	}
	menu_group_end();

	menu_columns_end();
}
