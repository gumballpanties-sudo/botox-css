#pragma once
#include "sdk.h"

#include "aim_global.h"

class CAimbotHitscan
{
private:
	std::vector<Target_t> GetTargets(CCSPlayer* pLocal, CWeaponCSBase* pWeapon);
	std::vector<Target_t> SortTargets(CCSPlayer* pLocal, CWeaponCSBase* pWeapon);

	int CanHit(Target_t& tTarget, CCSPlayer* pLocal, CWeaponCSBase* pWeapon);
	// botox hit_chance(): share of this tick's cone (m_flInaccuracy/m_flSpread) aimed at vPoint
	// that lands on any hitbox of the skeleton
	float HitChance(const mstudiohitboxset_t* pSet, const matrix3x4* aBones, float flModelScale, const Vec3& vPoint);

	// CS:GO 1:1 (botox aimbot.cpp on_create_move_post): no AimType switch any more -- the
	// decision is Camlock > Silent > visible-smooth, all driven off Vars::Aimbot::General /
	// WeaponClass. iMethod kept as a parameter (0 = plain/instant) only for the one internal
	// caller (laser-pointer-equivalent path has none in CS, so always the main 3-way).
	bool Aim(Vec3 vCurAngle, Vec3 vToAngle, Vec3& vOut, float flSmooth);
	void Aim(CUserCmd* pCmd, Vec3& vAngle, bool bCamlock, bool bSilent, float flSmooth);
	bool ShouldFire(CCSPlayer* pLocal, CWeaponCSBase* pWeapon, CUserCmd* pCmd, const Target_t& tTarget);

	Vec3 m_vEyePos = {};
	// botox aim -= silent ? punch * recoil_scale : want. Taken off the target before smoothing;
	// bullet direction = aim angle + m_vComp, which is what the ray checks trace.
	Vec3 m_vComp = {};
	// botox scan_t m_live / m_records: pass 0 = live (+ records with aim at backtrack),
	// pass 1 = records only, run when pass 0 found nothing (backtrack on, aim at off)
	int m_iBtPass = 0;

	// botox m_lock_index / m_target_hitbox: the player (and hitbox) last aimed at. Scanned first
	// every tick and kept while it can be hit; General::NoSwitch holds it even when it can't.
	int m_iLock = 0, m_iLockHitbox = -1;

	// botox weapon_cone(), read once per Run: 0/0 = no spread or nospread on
	float m_flInaccuracy = 0.f, m_flSpread = 0.f;
	float m_flPickHc = 1.f;   // hit chance of the last CanHit pick (trace only)

	// botox weapon_settings() + enabled_hitboxes() for the held weapon, set at the top of Run:
	// the class override when on, else general. Groups = Hitscan::HitboxGroupsEnum bits.
	float m_flFOV = 0.f, m_flSmooth = 1.f;
	int m_iHitboxGroups = 0;

public:
	void Run(CCSPlayer* pLocal, CWeaponCSBase* pWeapon, CUserCmd* pCmd);
	void ResetLock() { m_iLock = 0, m_iLockHitbox = -1; }
};

ADD_FEATURE(CAimbotHitscan, AimbotHitscan);
