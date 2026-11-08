#pragma once
#include "sdk.h"
#include "aim_record.h"

// botox csgo-sdk hacks/lagcomp/lagcomp.cpp 1:1, MINUS extended backtrack (fake latency:
// m_backtrack_extend, the incoming sequence spoof, update/clear/add_latency_to_net_channel).
// Also gone with it, because it only ever fired on a csgo listen server: hosting push ticks
// (sv_lagpushticks is a csgo convar, TF2 has none, so botox's is_hosting() is always false here)
// and live_accepted (centre - lerp only crosses 0.2 with extend on).
//
// Records: one ring per player, filled at FRAME_NET_UPDATE_END (hooks.cpp; botox records at
// render_start BEFORE OnRenderStart = same state: network origin, abs recompute on).
// Validity = the server's own check (player_lagcompensation.cpp:379-412): a record is sent as
// tick_count = TIME_TO_TICKS(sim + lerp), and the server keeps it only while
// |correct - (now - sim)| <= 0.2 and sim is newer than curtime - sv_maxunlag.
//
// Readers: aim_hitscan.cpp (aim at backtrack / record fallback, CommitShot on the fire tick)
// and RunBacktrack (botox aimbot.cpp run_backtrack: a fire cmd with no aim shot goes out on the
// tick of the record nearest the bullet). Log `BT:`.
class CBacktrack
{
public:
	enum class EShot : int { None, Record, Live };

	// botox max_records = 128 tick x (sv_maxunlag 1.0 + 0.2). TF2 is 66 tick: same window = 80.
	static constexpr int kMaxRecords = 96;
	static constexpr float kEdgeMargin = 0.03f;     // botox k_edge_margin
	static constexpr float kMaxBacktrack = 0.2f;    // botox k_max_backtrack

	float LerpTime();
	float MaxUnlag();
	float RealLatency();
	float TimeLimit();
	float CorrectTime();
	float WindowCenter();
	float ServerTime();

	bool Enabled();
	bool AimAt();

	void BeginCommand(CUserCmd* pCmd, CCSPlayer* pLocal);
	bool IsValid(float flSimTime, float flMargin = kEdgeMargin);
	// IsValid + no further behind the live sim than TimeLimit. every shot path uses this
	bool Usable(CBaseEntity* pEntity, float flSimTime);
	void CommitShot(TickRecord* pRecord, CUserCmd* pCmd);   // nullptr = live shot

	// nearest usable record body centre inside flFOV (GetTargets gate when the live one is out)
	bool RecordInFOV(CBaseEntity* pEntity, const Vec3& vEye, const Vec3& vAngles, float flFOV, float& flFOVTo, Vec3& vPos, Vec3& vAngleTo);

	// bt chams (aim/chams.cpp, botox draw_model_execute.cpp backtrack switch)
	TickRecord* OldestRecord(CBaseEntity* pEntity);   // botox oldest_record: oldest usable
	float ModelTime();                                // botox model_time: sim time the drawn model sits at
	bool Owns(int iIndex, const TickRecord* pRecord) { auto p = GetRecords(iIndex); return p && pRecord >= p && pRecord < p + kMaxRecords; }
	TickRecord* m_pTarget = nullptr;                  // botox g_ctx.m_record: the record the aim picked this cmd

	void Store();               // FRAME_NET_UPDATE_END
	void RunBacktrack(CCSPlayer* pLocal, CWeaponCSBase* pWeapon, CUserCmd* pCmd);
	// RunMeleeSmack REMOVED (11-03 CS:GO port): TF2-only, non-knife melee lag-compensates at a
	// delayed "smack" tick (tf_weaponbase_melee.cpp). CS:GO/CSS knife swings connect immediately on
	// the attack tick like every other weapon -- there is no separate smack-tick mechanic to port,
	// and botox's own aimbot has no melee-aim path at all (see aimbot.cpp report).
	void Release();             // Aim_Unload

	// ring of player iIndex (nullptr = none yet), Count() slots of it in use. m_flSimTime 0 = empty
	TickRecord* GetRecords(int iIndex) { return iIndex > 0 && iIndex < 65 ? m_aRecords[iIndex] : nullptr; }
	int Count() const { return m_iMaxAllocations; }

	EShot m_eShot = EShot::None;

private:
	void RefreshWindow();
	void UpdateAllocations();
	void Reset(int iIndex);

	// botox record_t = player + sim + valid + origin + matrix. player/valid only fed botox's
	// backtrack chams / trail (not in this build); the rest is TickRecord, which CanHit takes.
	TickRecord* m_aRecords[65] = {};
	int m_aLocation[65] = {};
	int m_iMaxAllocations = 0;

	int m_iServerTick = 0;
	CUserCmd* m_pLastCmd = nullptr;

	int m_iWindowFrame = -1;
	float m_flWindowCenter = 0.f, m_flWindowUnlag = 1.f, m_flWindowLimit = 0.2f;
};

ADD_FEATURE(CBacktrack, Backtrack);
