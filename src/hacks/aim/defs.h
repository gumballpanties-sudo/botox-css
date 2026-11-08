#pragma once
// The engine surface the aim code compiles against: value types, enums, the client
// interfaces and the entity/weapon classes. Folded into one header -- it is included as one
// unit anyway, and the pieces reference each other in both directions.

// ================ types (src/hacks/aim/types.h) ================
#include "types.h"

// ================ SDK/Definitions/Misc/CommonMacros.h ================

// ================ SDK/Definitions/Misc/BaseTypes.h ================
template< class T >
T Clamp(T const& val, T const& minVal, T const& maxVal)
{
	if (val < minVal)
		return minVal;
	else if (val > maxVal)
		return maxVal;
	else
		return val;
}

template< class T >
T Min(T const& val1, T const& val2)
{
	return val1 < val2 ? val1 : val2;
}

template< class T >
T Max(T const& val1, T const& val2)
{
	return val1 > val2 ? val1 : val2;
}

typedef int qboolean;
typedef unsigned char BYTE;
typedef unsigned char byte;

typedef float vec_t;

typedef struct color32_s
{
	byte r, g, b, a;
} color32;

struct colorVec
{
	unsigned r, g, b, a;
};

struct vrect_t
{
	int x, y, width, height;
	vrect_t* pnext;
};

struct wrect_t
{
	int	left;
	int right;
	int top;
	int bottom;
};

using uint32 = unsigned int;
typedef __int32 int32;
typedef unsigned __int32 uint32;
typedef __int64 int64;
typedef unsigned __int64 uint64;
typedef uint32 RTime32;
typedef unsigned int uint;

// ================ SDK/Definitions/Definitions.h ================
#define TICK_INTERVAL I::GlobalVars->interval_per_tick
#define TIME_TO_TICKS(dt) (static_cast<int>(0.5f + static_cast<float>(dt) / TICK_INTERVAL))
#define TICKS_TO_TIME(t) (TICK_INTERVAL * (t))

#define ROUND_TO_TICKS(t) (TICKS_TO_TIME(TIME_TO_TICKS(t)))

#if defined(TF_DLL) || defined(TF_CLIENT_DLL)
#else
#endif 

#define MAX_WEAPONS 48

#define WEAPON_NOCLIP -1

#define TEAM_SPECTATOR 1
#define LAST_SHARED_TEAM TEAM_SPECTATOR

#define DMG_AIRBOAT (1<<25)

#define TF_DAMAGE_CRIT_CHANCE 0.02f
#define TF_DAMAGE_CRIT_CHANCE_RAPID 0.02f
#define TF_DAMAGE_CRIT_DURATION_RAPID 2.0f
#define TF_DAMAGE_CRIT_CHANCE_MELEE 0.15f
#define TF_DAMAGE_CRIT_MULTIPLIER 3.0f

#define WEAPON_RANDOM_RANGE 10000

#define TF_TEAM_AUTOASSIGN (TF_TEAM_COUNT + 1 )
#define TF_TEAM_HALLOWEEN TF_TEAM_AUTOASSIGN

#define TF_WEAPON_PRIMARY_MODE 0
#define TF_PARTICLE_MAX_CHARGE_TIME 2.f

#define DMG_USE_HITLOCATIONS (DMG_AIRBOAT)

#define	TF_STUN_CONTROLS (1<<1)
#define TF_STUN_LOSER_STATE (1<<6)

#define TF_ARROW_MAX_CHARGE_TIME 5.0f

#define MAX_PLAYERS 101
#define MAX_PLAYERS_ARRAY_SAFE (MAX_PLAYERS + 1)

#define ABSOLUTE_PLAYER_LIMIT 255
#define MAX_PLAYER_NAME_LENGTH 32
#define	MAX_EDICT_BITS 11
#define	MAX_EDICTS (1 << MAX_EDICT_BITS)
#define SIGNED_GUID_LEN 32
#define INVALID_EHANDLE_INDEX 0xFFFFFFFF
#define NUM_SERIAL_NUM_BITS 16
#define NUM_SERIAL_NUM_SHIFT_BITS (32 - NUM_SERIAL_NUM_BITS)
#define ENT_ENTRY_MASK (( 1 << NUM_SERIAL_NUM_BITS) - 1)
#define MAX_CUSTOM_FILES 4

#define	FL_ONGROUND (1<<0)
#define FL_DUCKING (1<<1)
#define FL_ANIMDUCKING (1<<2)
#define	FL_WATERJUMP (1<<3)
#define FL_FROZEN (1<<6)
#define	FL_INWATER (1<<10)

#define	LIFE_ALIVE 0
#define	LIFE_DEAD 2

#define MAX_AREA_STATE_BYTES 32
#define MAX_AREA_PORTAL_STATE_BYTES 24

// Following values should be +16384, -16384, +15/16, -15/16
// NOTE THAT IF THIS GOES ANY BIGGER THEN DISK NODES/LEAVES CANNOT USE SHORTS TO STORE THE BOUNDS
#define MAX_COORD_INTEGER			(16384)

// Width of the coord system, which is TOO BIG to send as a client/server coordinate value
#define COORD_EXTENT				(2*MAX_COORD_INTEGER)

// Maximum traceable distance ( assumes cubic world and trace from one corner to opposite )
// COORD_EXTENT * sqrt(3)
#define MAX_TRACE_LENGTH			( 1.732050807569f * COORD_EXTENT )		

// This value is the LONGEST possible range (limited by max valid coordinate number, not 2x)

#define VALVE_RAND_MAX 0x7fff

enum MoveType_t
{
	MOVETYPE_NONE = 0,
	MOVETYPE_ISOMETRIC,
	MOVETYPE_WALK,
	MOVETYPE_STEP,
	MOVETYPE_FLY,
	MOVETYPE_FLYGRAVITY,
	MOVETYPE_VPHYSICS,
	MOVETYPE_PUSH,
	MOVETYPE_NOCLIP,
	MOVETYPE_LADDER,
	MOVETYPE_OBSERVER,
	MOVETYPE_CUSTOM,
	MOVETYPE_LAST = MOVETYPE_CUSTOM,
	MOVETYPE_MAX_BITS = 4
};

enum SolidType_t
{
	SOLID_NONE = 0,
	SOLID_BSP = 1,
	SOLID_BBOX = 2,
	SOLID_OBB = 3,
	SOLID_OBB_YAW = 4,
	SOLID_CUSTOM = 5,
	SOLID_VPHYSICS = 6,
	SOLID_LAST
};

enum
{
	EF_BONEMERGE = 0x001,
	EF_BRIGHTLIGHT = 0x002,
	EF_DIMLIGHT = 0x004,
	EF_NOINTERP = 0x008,
	EF_NOSHADOW = 0x010,
	EF_NODRAW = 0x020,
	EF_NORECEIVESHADOW = 0x040,
	EF_BONEMERGE_FASTCULL = 0x080,
	EF_ITEM_BLINK = 0x100,
	EF_PARENT_ANIMATES = 0x200,
	EF_MAX_BITS = 10
};

#pragma region Hitboxes

enum EHitboxes
{
	HITBOX_HEAD,
	HITBOX_PELVIS,
	HITBOX_SPINE0,
	HITBOX_SPINE1,
	HITBOX_SPINE2,
	HITBOX_SPINE3,
	HITBOX_LEFT_UPPERARM,
	HITBOX_LEFT_FOREARM,
	HITBOX_LEFT_HAND,
	HITBOX_RIGHT_UPPERARM,
	HITBOX_RIGHT_FOREARM,
	HITBOX_RIGHT_HAND,
	HITBOX_LEFT_THIGH,
	HITBOX_LEFT_CALF,
	HITBOX_LEFT_FOOT,
	HITBOX_RIGHT_THIGH,
	HITBOX_RIGHT_CALF,
	HITBOX_RIGHT_FOOT,
	HITBOX_MAX
};

// 1: scout, 2: soldier, 3: pyro, 4: demoman, 5: heavy, 6: engineer, 7: medic, 8: sniper, 9: spy

#pragma region Mann vs Machine
enum EBot6Hitboxes
{	// engineer bot has no pelvis
	HITBOX_BOT6_HEAD,
	HITBOX_BOT6_SPINE0,
	HITBOX_BOT6_SPINE1,
	HITBOX_BOT6_SPINE2,
	HITBOX_BOT6_SPINE3,
	HITBOX_BOT6_LEFT_UPPERARM,
	HITBOX_BOT6_LEFT_FOREARM,
	HITBOX_BOT6_LEFT_HAND,
	HITBOX_BOT6_RIGHT_UPPERARM,
	HITBOX_BOT6_RIGHT_FOREARM,
	HITBOX_BOT6_RIGHT_HAND,
	HITBOX_BOT6_LEFT_THIGH,
	HITBOX_BOT6_LEFT_CALF,
	HITBOX_BOT6_LEFT_FOOT,
	HITBOX_BOT6_RIGHT_THIGH,
	HITBOX_BOT6_RIGHT_CALF,
	HITBOX_BOT6_RIGHT_FOOT,
	HITBOX_BOT6_MAX
};
#pragma endregion

#pragma region Versus Saxton Hale
enum ESaxtonHitboxes
{
	HITBOX_SAXTON_HEAD,
	HITBOX_SAXTON_NECK,
	HITBOX_SAXTON_LEFT_FOOT,
	HITBOX_SAXTON_RIGHT_FOOT,
	HITBOX_SAXTON_LEFT_CALF,
	HITBOX_SAXTON_RIGHT_CALF,
	HITBOX_SAXTON_LEFT_THIGH,
	HITBOX_SAXTON_RIGHT_THIGH,
	HITBOX_SAXTON_LEFT_HAND,
	HITBOX_SAXTON_RIGHT_HAND,
	HITBOX_SAXTON_LEFT_FOREARM,
	HITBOX_SAXTON_RIGHT_FOREARM,
	HITBOX_SAXTON_LEFT_UPPERARM,
	HITBOX_SAXTON_RIGHT_UPPERARM,
	HITBOX_SAXTON_PELVIS,
	HITBOX_SAXTON_SPINE0,
	HITBOX_SAXTON_SPINE1,
	HITBOX_SAXTON_SPINE2,
	HITBOX_SAXTON_SPINE3,
	HITBOX_SAXTON_MAX
};
#pragma endregion

#pragma region Zombie Infection
// these models are all terrible
// reused where possible
// not adding garbage hitboxes that shouldn't exist

enum EZombie128Hitboxes
{
	HITBOX_ZOMBIE128_PELVIS,
	HITBOX_ZOMBIE128_SPINE0,
	HITBOX_ZOMBIE128_SPINE1,
	HITBOX_ZOMBIE128_SPINE2,
	HITBOX_ZOMBIE128_SPINE3,
	HITBOX_ZOMBIE128_NECK,
	HITBOX_ZOMBIE128_HEAD,
	HITBOX_ZOMBIE128_LEFT_COLLAR,
	HITBOX_ZOMBIE128_RIGHT_COLLAR,
	HITBOX_ZOMBIE128_LEFT_UPPERARM,
	HITBOX_ZOMBIE128_RIGHT_UPPERARM,
	HITBOX_ZOMBIE128_LEFT_FOREARM,
	HITBOX_ZOMBIE128_RIGHT_FOREARM,
	HITBOX_ZOMBIE128_LEFT_THIGH,
	HITBOX_ZOMBIE128_RIGHT_THIGH,
	HITBOX_ZOMBIE128_LEFT_CALF,
	HITBOX_ZOMBIE128_RIGHT_CALF,
	HITBOX_ZOMBIE128_LEFT_FOOT,
	HITBOX_ZOMBIE128_RIGHT_FOOT,
	HITBOX_ZOMBIE128_LEFT_HAND,
	HITBOX_ZOMBIE128_RIGHT_HAND,
	HITBOX_ZOMBIE128_MAX
};

enum EZombie3Hitboxes
{
	HITBOX_ZOMBIE3_PELVIS,
	HITBOX_ZOMBIE3_SPINE0,
	HITBOX_ZOMBIE3_SPINE1,
	HITBOX_ZOMBIE3_SPINE2,
	HITBOX_ZOMBIE3_SPINE3,
	HITBOX_ZOMBIE3_NECK,
	HITBOX_ZOMBIE3_HEAD,
	HITBOX_ZOMBIE3_LEFT_COLLAR,
	HITBOX_ZOMBIE3_LEFT_UPPERARM,
	HITBOX_ZOMBIE3_LEFT_FOREARM,
	HITBOX_ZOMBIE3_LEFT_HAND,
	HITBOX_ZOMBIE3_RIGHT_COLLAR,
	HITBOX_ZOMBIE3_RIGHT_UPPERARM,
	HITBOX_ZOMBIE3_RIGHT_FOREARM,
	HITBOX_ZOMBIE3_RIGHT_HAND,
	HITBOX_ZOMBIE3_LEFT_THIGH,
	HITBOX_ZOMBIE3_LEFT_CALF,
	HITBOX_ZOMBIE3_LEFT_FOOT,
	HITBOX_ZOMBIE3_RIGHT_THIGH = 19,
	HITBOX_ZOMBIE3_RIGHT_CALF,
	HITBOX_ZOMBIE3_RIGHT_FOOT,
	HITBOX_ZOMBIE3_MAX
};

enum EZombie4Hitboxes
{
	HITBOX_ZOMBIE4_PELVIS,
	HITBOX_ZOMBIE4_SPINE0,
	HITBOX_ZOMBIE4_SPINE1,
	HITBOX_ZOMBIE4_SPINE2,
	HITBOX_ZOMBIE4_SPINE3,
	HITBOX_ZOMBIE4_LEFT_COLLAR,
	HITBOX_ZOMBIE4_LEFT_UPPERARM,
	HITBOX_ZOMBIE4_RIGHT_COLLAR,
	HITBOX_ZOMBIE4_RIGHT_UPPERARM,
	HITBOX_ZOMBIE4_LEFT_THIGH,
	HITBOX_ZOMBIE4_LEFT_CALF,
	HITBOX_ZOMBIE4_RIGHT_THIGH,
	HITBOX_ZOMBIE4_RIGHT_CALF,
	HITBOX_ZOMBIE4_LEFT_FOREARM,
	HITBOX_ZOMBIE4_RIGHT_FOREARM,
	HITBOX_ZOMBIE4_NECK,
	HITBOX_ZOMBIE4_HEAD,
	HITBOX_ZOMBIE4_LEFT_HAND,
	HITBOX_ZOMBIE4_RIGHT_HAND,
	HITBOX_ZOMBIE4_LEFT_FOOT,
	HITBOX_ZOMBIE4_RIGHT_FOOT,
	HITBOX_ZOMBIE4_MAX
};

enum EZombie5Hitboxes
{
	HITBOX_ZOMBIE5_PELVIS,
	HITBOX_ZOMBIE5_SPINE0,
	HITBOX_ZOMBIE5_SPINE1,
	HITBOX_ZOMBIE5_SPINE2,
	HITBOX_ZOMBIE5_SPINE3,
	HITBOX_ZOMBIE5_NECK,
	HITBOX_ZOMBIE5_HEAD,
	HITBOX_ZOMBIE5_LEFT_COLLAR,
	HITBOX_ZOMBIE5_RIGHT_COLLAR,
	HITBOX_ZOMBIE5_LEFT_UPPERARM,
	HITBOX_ZOMBIE5_RIGHT_UPPERARM,
	HITBOX_ZOMBIE5_LEFT_FOREARM,
	HITBOX_ZOMBIE5_RIGHT_FOREARM,
	HITBOX_ZOMBIE5_LEFT_THIGH,
	HITBOX_ZOMBIE5_RIGHT_THIGH,
	HITBOX_ZOMBIE5_LEFT_CALF,
	HITBOX_ZOMBIE5_RIGHT_CALF,
	HITBOX_ZOMBIE5_LEFT_HAND,
	HITBOX_ZOMBIE5_RIGHT_HAND,
	HITBOX_ZOMBIE5_LEFT_FOOT,
	HITBOX_ZOMBIE5_RIGHT_FOOT,
	HITBOX_ZOMBIE5_MAX
};

enum EZombie6Hitboxes
{
	HITBOX_ZOMBIE6_PELVIS,
	HITBOX_ZOMBIE6_RIGHT_THIGH,
	HITBOX_ZOMBIE6_RIGHT_CALF,
	HITBOX_ZOMBIE6_SPINE0,
	HITBOX_ZOMBIE6_SPINE1,
	HITBOX_ZOMBIE6_SPINE2,
	HITBOX_ZOMBIE6_SPINE3,
	HITBOX_ZOMBIE6_NECK,
	HITBOX_ZOMBIE6_HEAD,
	HITBOX_ZOMBIE6_LEFT_THIGH,
	HITBOX_ZOMBIE6_LEFT_CALF,
	HITBOX_ZOMBIE6_LEFT_COLLAR,
	HITBOX_ZOMBIE6_LEFT_UPPERARM,
	HITBOX_ZOMBIE6_LEFT_FOREARM,
	HITBOX_ZOMBIE6_RIGHT_COLLAR,
	HITBOX_ZOMBIE6_RIGHT_UPPERARM,
	HITBOX_ZOMBIE6_RIGHT_FOREARM,
	HITBOX_ZOMBIE6_LEFT_FOOT,
	HITBOX_ZOMBIE6_RIGHT_FOOT,
	HITBOX_ZOMBIE6_RIGHT_HAND,
	HITBOX_ZOMBIE6_LEFT_HAND,
	HITBOX_ZOMBIE6_MAX
};

enum EZombie7Hitboxes
{
	HITBOX_ZOMBIE7_PELVIS,
	HITBOX_ZOMBIE7_SPINE0,
	HITBOX_ZOMBIE7_SPINE1,
	HITBOX_ZOMBIE7_SPINE2,
	HITBOX_ZOMBIE7_SPINE3,
	HITBOX_ZOMBIE7_NECK,
	HITBOX_ZOMBIE7_HEAD,
	HITBOX_ZOMBIE7_LEFT_COLLAR,
	HITBOX_ZOMBIE7_RIGHT_COLLAR,
	HITBOX_ZOMBIE7_LEFT_UPPERARM,
	HITBOX_ZOMBIE7_RIGHT_UPPERARM,
	HITBOX_ZOMBIE7_LEFT_FOREARM,
	HITBOX_ZOMBIE7_RIGHT_FOREARM,
	HITBOX_ZOMBIE7_LEFT_THIGH,
	HITBOX_ZOMBIE7_RIGHT_THIGH,
	HITBOX_ZOMBIE7_LEFT_CALF,
	HITBOX_ZOMBIE7_RIGHT_CALF,
	HITBOX_ZOMBIE7_LEFT_HAND,
	HITBOX_ZOMBIE7_RIGHT_HAND,
	HITBOX_ZOMBIE7_LEFT_FOOT = 22,
	HITBOX_ZOMBIE7_RIGHT_FOOT,
	HITBOX_ZOMBIE7_MAX
};

enum EZombie9Hitboxes
{
	HITBOX_ZOMBIE9_PELVIS,
	HITBOX_ZOMBIE9_SPINE0,
	HITBOX_ZOMBIE9_SPINE1,
	HITBOX_ZOMBIE9_SPINE2,
	HITBOX_ZOMBIE9_SPINE3,
	HITBOX_ZOMBIE9_NECK,
	HITBOX_ZOMBIE9_HEAD,
	HITBOX_ZOMBIE9_LEFT_COLLAR,
	HITBOX_ZOMBIE9_RIGHT_COLLAR,
	HITBOX_ZOMBIE9_LEFT_UPPERARM,
	HITBOX_ZOMBIE9_RIGHT_UPPERARM,
	HITBOX_ZOMBIE9_LEFT_FOREARM,
	HITBOX_ZOMBIE9_RIGHT_FOREARM,
	HITBOX_ZOMBIE9_LEFT_HAND,
	HITBOX_ZOMBIE9_RIGHT_HAND,
	HITBOX_ZOMBIE9_LEFT_THIGH,
	HITBOX_ZOMBIE9_RIGHT_THIGH,
	HITBOX_ZOMBIE9_LEFT_CALF,
	HITBOX_ZOMBIE9_RIGHT_CALF,
	HITBOX_ZOMBIE9_LEFT_FOOT,
	HITBOX_ZOMBIE9_RIGHT_FOOT,
	HITBOX_ZOMBIE9_MAX
};
#pragma endregion

#pragma endregion

enum MinigunState_t
{
	AC_STATE_IDLE = 0,
	AC_STATE_STARTFIRING,
	AC_STATE_FIRING,
	AC_STATE_SPINNING,
	AC_STATE_DRYFIRE
};

typedef enum
{
	GROUND = 0,
	STUCK,
	LADDER
} IntervalType_t;

enum
{
	SPEED_CROPPED_RESET = 0,
	SPEED_CROPPED_DUCK = 1,
	SPEED_CROPPED_WEAPON = 2
};

enum RifleTypes_t
{
	RIFLE_NORMAL = 0,
	RIFLE_JARATE,
	RIFLE_MACHINA,
	RIFLE_CLASSIC,
};

enum DetonateTypes_t
{
	TF_DETONATE_MODE_STANDARD = 0,
	TF_DETONATE_MODE_DOT,
	TF_DETONATE_MODE_AIR,
};

enum FlareGunTypes_t
{
	FLAREGUN_NORMAL = 0,
	FLAREGUN_DETONATE,
	FLAREGUN_GRORDBORT,
	FLAREGUN_SCORCHSHOT
};

class CViewVectors
{
public:
	Vector m_vView;
	Vector m_vHullMin;
	Vector m_vHullMax;
	Vector m_vDuckHullMin;
	Vector m_vDuckHullMax;
	Vector m_vDuckView;
	Vector m_vObsHullMin;
	Vector m_vObsHullMax;
	Vector m_vDeadViewHeight;
};

enum UserMessageType
{
	Geiger = 0,
	Train,
	HudText,
	SayText,
	SayText2,
	TextMsg,
	ResetHUD,
	GameTitle,
	ItemPickup,
	ShowMenu,
	Shake,
	Fade,
	VGUIMenu,
	Rumble,
	CloseCaption,
	SendAudio,
	VoiceMask,
	RequestState,
	Damage,
	HintText,
	KeyHintText,
	HudMsg,
	AmmoDenied,
	AchievementEvent,
	UpdateRadar,
	VoiceSubtitle,
	HudNotify,
	HudNotifyCustom,
	PlayerStatsUpdate,
	MapStatsUpdate,
	PlayerIgnited,
	PlayerIgnitedInv,
	HudArenaNotify,
	UpdateAchievement,
	TrainingMsg,
	TrainingObjective,
	DamageDodged,
	PlayerJarated,
	PlayerExtinguished,
	PlayerJaratedFade,
	PlayerShieldBlocked,
	BreakModel,
	CheapBreakModel,
	BreakModel_Pumpkin,
	BreakModelRocketDud,
	CallVoteFailed,
	VoteStart,
	VotePass,
	VoteFailed,
	VoteSetup,
	PlayerBonusPoints,
	RDTeamPointsChanged,
	SpawnFlyingBird,
	PlayerGodRayEffect,
	PlayerTeleportHomeEffect,
	MVMStatsReset,
	MVMPlayerEvent,
	MVMResetPlayerStats,
	MVMWaveFailed,
	MVMAnnouncement,
	MVMPlayerUpgradedEvent,
	MVMVictory,
	MVMWaveChange,
	MVMLocalPlayerUpgradesClear,
	MVMLocalPlayerUpgradesValue,
	MVMResetPlayerWaveSpendingStats,
	MVMLocalPlayerWaveSpendingValue,
	MVMResetPlayerUpgradeSpending,
	MVMServerKickTimeUpdate,
	PlayerLoadoutUpdated,
	PlayerTauntSoundLoopStart,
	PlayerTauntSoundLoopEnd,
	ForcePlayerViewAngles,
	BonusDucks,
	EOTLDuckEvent,
	PlayerPickupWeapon,
	QuestObjectiveCompleted,
	SPHapWeapEvent,
	HapDmg,
	HapPunch,
	HapSetDrag,
	HapSetConst,
	HapMeleeContact
};

enum
{
	SERVER_MODIFICATION_ITEM_DURATION_IN_MINUTES = 120
};

enum
{
	ENTITY_DISSOLVE_NORMAL = 0,
	ENTITY_DISSOLVE_ELECTRICAL,
	ENTITY_DISSOLVE_ELECTRICAL_LIGHT,
	ENTITY_DISSOLVE_CORE,
	ENTITY_DISSOLVE_BITS = 3
};

enum
{
	OBS_ALLOW_ALL = 0,
	OBS_ALLOW_TEAM,
	OBS_ALLOW_NONE,
	OBS_ALLOW_NUM_MODES
};

enum
{
	TYPE_TEXT = 0,
	TYPE_INDEX,
	TYPE_URL,
	TYPE_FILE
};

enum
{
	EFL_KILLME = (1 << 0),
	EFL_DORMANT = (1 << 1),
	EFL_NOCLIP_ACTIVE = (1 << 2),
	EFL_SETTING_UP_BONES = (1 << 3),
	EFL_KEEP_ON_RECREATE_ENTITIES = (1 << 4),
	EFL_HAS_PLAYER_CHILD = (1 << 4),
	EFL_DIRTY_SHADOWUPDATE = (1 << 5),
	EFL_NOTIFY = (1 << 6),
	EFL_FORCE_CHECK_TRANSMIT = (1 << 7),
	EFL_BOT_FROZEN = (1 << 8),
	EFL_SERVER_ONLY = (1 << 9),
	EFL_NO_AUTO_EDICT_ATTACH = (1 << 10),
	EFL_DIRTY_ABSTRANSFORM = (1 << 11),
	EFL_DIRTY_ABSVELOCITY = (1 << 12),
	EFL_DIRTY_ABSANGVELOCITY = (1 << 13),
	EFL_DIRTY_SURROUNDING_COLLISION_BOUNDS = (1 << 14),
	EFL_DIRTY_SPATIAL_PARTITION = (1 << 15),
	EFL_IN_SKYBOX = (1 << 17),
	EFL_USE_PARTITION_WHEN_NOT_SOLID = (1 << 18),
	EFL_TOUCHING_FLUID = (1 << 19),
	EFL_IS_BEING_LIFTED_BY_BARNACLE = (1 << 20),
	EFL_NO_ROTORWASH_PUSH = (1 << 21),
	EFL_NO_THINK_FUNCTION = (1 << 22),
	EFL_NO_GAME_PHYSICS_SIMULATION = (1 << 23),
	EFL_CHECK_UNTOUCH = (1 << 24),
	EFL_DONTBLOCKLOS = (1 << 25),
	EFL_DONTWALKON = (1 << 26),
	EFL_NO_DISSOLVE = (1 << 27),
	EFL_NO_MEGAPHYSCANNON_RAGDOLL = (1 << 28),
	EFL_NO_WATER_VELOCITY_CHANGE = (1 << 29),
	EFL_NO_PHYSCANNON_INTERACTION = (1 << 30),
	EFL_NO_DAMAGE_FORCES = (1 << 31)
};

class CBaseEntity;

struct CUtlString
{
	char* m_pString;
};

enum
{
	VISION_MODE_NONE = 0,
	VISION_MODE_PYRO,
	VISION_MODE_HALLOWEEN,
	VISION_MODE_ROME,
	MAX_VISION_MODES
};

class CHudTexture
{
public:
	void* vftp;
	char szShortName[64];
	char szTextureFile[64];
	bool bRenderUsingFont;
	bool bPrecached;
	char cCharacterInFont;
	unsigned long hFont;
	int textureId;
	float texCoords[4];
	wrect_t rc;

public:
	int Width() const
	{
		return rc.right - rc.left;
	}
	int Height() const
	{
		return rc.bottom - rc.top;
	}
};

enum class ETFClassID
{
	CTFWearableRazorback = 341,
	CTFWearableDemoShield = 338,
	CTFWearableLevelableItem = 340,
	CTFWearableCampaignItem = 337,
	CTFBaseRocket = 185,
	CTFWeaponBaseMerasmusGrenade = 325,
	CTFWeaponBaseMelee = 324,
	CTFWeaponBaseGun = 323,
	CTFWeaponBaseGrenadeProj = 322,
	CTFWeaponBase = 321,
	CTFWearableRobotArm = 342,
	CTFRobotArm = 287,
	CTFWrench = 344,
	CTFProjectile_ThrowableBreadMonster = 279,
	CTFProjectile_ThrowableBrick = 280,
	CTFProjectile_ThrowableRepel = 281,
	CTFProjectile_Throwable = 278,
	CTFThrowable = 319,
	CTFSyringeGun = 315,
	CTFKatana = 225,
	CTFSword = 314,
	CSniperDot = 118,
	CTFSniperRifleClassic = 308,
	CTFSniperRifleDecap = 309,
	CTFSniperRifle = 307,
	CTFChargedSMG = 197,
	CTFSMG = 306,
	CTFSlap = 305,
	CTFShovel = 304,
	CTFShotgunBuildingRescue = 303,
	CTFPEPBrawlerBlaster = 241,
	CTFSodaPopper = 310,
	CTFShotgun_Revenge = 301,
	CTFScatterGun = 297,
	CTFShotgun_Pyro = 300,
	CTFShotgun_HWG = 299,
	CTFShotgun_Soldier = 302,
	CTFShotgun = 298,
	CTFRocketPack = 296,
	CTFCrossbow = 201,
	CTFRocketLauncher_Mortar = 295,
	CTFRocketLauncher_AirStrike = 293,
	CTFRocketLauncher_DirectHit = 294,
	CTFRocketLauncher = 292,
	CTFRevolver = 286,
	CTFDRGPomson = 202,
	CTFRaygun = 284,
	CTFPistol_ScoutSecondary = 246,
	CTFPistol_ScoutPrimary = 245,
	CTFPistol_Scout = 244,
	CTFPistol = 243,
	CTFPipebombLauncher = 242,
	CTFWeaponPDA_Spy = 332,
	CTFWeaponPDA_Engineer_Destroy = 331,
	CTFWeaponPDA_Engineer_Build = 330,
	CTFWeaponPDAExpansion_Teleporter = 334,
	CTFWeaponPDAExpansion_Dispenser = 333,
	CTFWeaponPDA = 329,
	CTFParticleCannon = 239,
	CTFParachute_Secondary = 238,
	CTFParachute_Primary = 237,
	CTFParachute = 236,
	CTFMinigun = 234,
	CTFMedigunShield = 231,
	CWeaponMedigun = 352,
	CTFProjectile_MechanicalArmOrb = 263,
	CTFMechanicalArm = 230,
	CTFLunchBox_Drink = 229,
	CTFLunchBox = 228,
	CLaserDot = 78,
	CTFLaserPointer = 227,
	CTFKnife = 226,
	CTFGasManager = 212,
	CTFProjectile_JarGas = 261,
	CTFJarGas = 223,
	CTFProjectile_Cleaver = 254,
	CTFProjectile_JarMilk = 262,
	CTFProjectile_Jar = 260,
	CTFCleaver = 198,
	CTFJarMilk = 224,
	CTFJar = 222,
	CTFWeaponInvis = 328,
	CTFCannon = 196,
	CTFGrenadeLauncher = 216,
	CTFGrenadePipebombProjectile = 217,
	CTFGrapplingHook = 215,
	CTFFlareGun_Revenge = 210,
	CTFFlareGun = 209,
	CTFFlameRocket = 207,
	CTFFlameThrower = 208,
	CTFFists = 205,
	CTFFireAxe = 204,
	CTFWeaponFlameBall = 327,
	CTFCompoundBow = 200,
	CTFClub = 199,
	CTFBuffItem = 195,
	CTFStickBomb = 312,
	CTFBreakableSign = 194,
	CTFBottle = 192,
	CTFBreakableMelee = 193,
	CTFBonesaw = 190,
	CTFBall_Ornament = 182,
	CTFStunBall = 313,
	CTFBat_Giftwrap = 188,
	CTFBat_Wood = 189,
	CTFBat_Fish = 187,
	CTFBat = 186,
	CTFProjectile_EnergyRing = 256,
	CTFDroppedWeapon = 203,
	CTFWeaponSapper = 335,
	CTFWeaponBuilder = 326,
	C_TFWeaponBuilder = 0,
	CTFProjectile_Rocket = 264,
	CTFProjectile_Flare = 257,
	CTFProjectile_EnergyBall = 255,
	CTFProjectile_GrapplingHook = 258,
	CTFProjectile_HealingBolt = 259,
	CTFProjectile_Arrow = 252,
	CMannVsMachineStats = 80,
	CTFTankBoss = 316,
	CTFBaseBoss = 183,
	CBossAlpha = 0,
	NextBotCombatCharacter = 357,
	CTFProjectile_SpellKartBats = 268,
	CTFProjectile_SpellKartOrb = 269,
	CTFHellZap = 220,
	CTFProjectile_SpellLightningOrb = 270,
	CTFProjectile_SpellTransposeTeleport = 277,
	CTFProjectile_SpellMeteorShower = 271,
	CTFProjectile_SpellSpawnBoss = 274,
	CTFProjectile_SpellMirv = 272,
	CTFProjectile_SpellPumpkin = 273,
	CTFProjectile_SpellSpawnHorde = 275,
	CTFProjectile_SpellSpawnZombie = 276,
	CTFProjectile_SpellBats = 266,
	CTFProjectile_SpellFireball = 267,
	CTFSpellBook = 311,
	CHightower_TeleportVortex = 74,
	CTeleportVortex = 160,
	CZombie = 354,
	CMerasmusDancer = 83,
	CMerasmus = 82,
	CHeadlessHatman = 73,
	CEyeballBoss = 48,
	CTFBotHintEngineerNest = 191,
	CBotNPCMinion = 0,
	CBotNPC = 0,
	CPasstimeGun = 94,
	CTFViewModel = 320,
	CRobotDispenser = 112,
	CTFRobotDestruction_Robot = 288,
	CTFReviveMarker = 285,
	CTFPumpkinBomb = 282,
	CTFProjectile_BallOfFire = 253,
	CTFBaseProjectile = 184,
	CTFPointManager = 250,
	CBaseObjectUpgrade = 11,
	CTFRobotDestructionLogic = 291,
	CTFRobotDestruction_RobotGroup = 289,
	CTFRobotDestruction_RobotSpawn = 290,
	CTFPlayerDestructionLogic = 248,
	CPlayerDestructionDispenser = 101,
	CTFMinigameLogic = 233,
	CTFHalloweenMinigame_FallingPlatforms = 219,
	CTFHalloweenMinigame = 218,
	CTFMiniGame = 232,
	CTFPowerupBottle = 251,
	CTFItem = 221,
	CHalloweenSoulPack = 71,
	CTFGenericBomb = 213,
	CBonusRoundLogic = 23,
	CTFGameRulesProxy = 211,
	CTETFParticleEffect = 179,
	CTETFExplosion = 178,
	CTETFBlood = 177,
	CTFFlameManager = 206,
	CHalloweenGiftPickup = 69,
	CBonusDuckPickup = 21,
	CHalloweenPickup = 70,
	CCaptureFlagReturnIcon = 27,
	CCaptureFlag = 26,
	CBonusPack = 22,
	CTFTeam = 318,
	CTFTauntProp = 317,
	CTFPlayerResource = 249,
	CTFPlayer = 247,
	CTFRagdoll = 283,
	CTEPlayerAnimEvent = 165,
	CTFPasstimeLogic = 240,
	CPasstimeBall = 93,
	CTFObjectiveResource = 235,
	CTFGlow = 214,
	CTEFireBullets = 152,
	CTFBuffBanner = 0,
	CTFAmmoPack = 181,
	CObjectTeleporter = 89,
	CObjectSentrygun = 88,
	CTFProjectile_SentryRocket = 265,
	CObjectSapper = 87,
	CObjectCartDispenser = 85,
	CObjectDispenser = 86,
	CMonsterResource = 84,
	CFuncRespawnRoomVisualizer = 64,
	CFuncRespawnRoom = 63,
	CFuncPasstimeGoal = 61,
	CFuncForceField = 57,
	CCaptureZone = 28,
	CCurrencyPack = 31,
	CBaseObject = 10,
	CTestTraceline = 176,
	CTEWorldDecal = 180,
	CTESpriteSpray = 174,
	CTESprite = 173,
	CTESparks = 172,
	CTESmoke = 171,
	CTEShowLine = 169,
	CTEProjectedDecal = 167,
	CTEPlayerDecal = 166,
	CTEPhysicsProp = 164,
	CTEParticleSystem = 163,
	CTEMuzzleFlash = 162,
	CTELargeFunnel = 159,
	CTEKillPlayerAttachments = 158,
	CTEImpact = 157,
	CTEGlowSprite = 156,
	CTEShatterSurface = 168,
	CTEFootprintDecal = 154,
	CTEFizz = 153,
	CTEExplosion = 151,
	CTEEnergySplash = 150,
	CTEEffectDispatch = 149,
	CTEDynamicLight = 148,
	CTEDecal = 146,
	CTEClientProjectile = 145,
	CTEBubbleTrail = 144,
	CTEBubbles = 143,
	CTEBSPDecal = 142,
	CTEBreakModel = 141,
	CTEBloodStream = 140,
	CTEBloodSprite = 139,
	CTEBeamSpline = 138,
	CTEBeamRingPoint = 137,
	CTEBeamRing = 136,
	CTEBeamPoints = 135,
	CTEBeamLaser = 134,
	CTEBeamFollow = 133,
	CTEBeamEnts = 132,
	CTEBeamEntPoint = 131,
	CTEBaseBeam = 130,
	CTEArmorRicochet = 129,
	CTEMetalSparks = 161,
	CSteamJet = 123,
	CSmokeStack = 117,
	DustTrail = 355,
	CFireTrail = 50,
	SporeTrail = 362,
	SporeExplosion = 361,
	RocketTrail = 359,
	SmokeTrail = 360,
	CPropVehicleDriveable = 108,
	ParticleSmokeGrenade = 358,
	CParticleFire = 90,
	MovieExplosion = 356,
	CTEGaussExplosion = 155,
	CEnvQuadraticBeam = 43,
	CEmbers = 36,
	CEnvWind = 47,
	CPrecipitation = 107,
	CBaseTempEntity = 17,
	CWeaponIFMSteadyCam = 351,
	CWeaponIFMBaseCamera = 350,
	CWeaponIFMBase = 349,
	CTFWearableVM = 343,
	CTFWearable = 336,
	CTFWearableItem = 339,
	CEconWearable = 35,
	CBaseAttributableItem = 3,
	CEconEntity = 34,
	CHandleTest = 72,
	CTeamplayRoundBasedRulesProxy = 126,
	CTeamRoundTimer = 127,
	CSpriteTrail = 122,
	CSpriteOriented = 121,
	CSprite = 120,
	CRagdollPropAttached = 111,
	CRagdollProp = 110,
	CPoseController = 106,
	CGameRulesProxy = 68,
	CInfoLadderDismount = 75,
	CFuncLadder = 58,
	CEnvDetailController = 40,
	CWorld = 353,
	CWaterLODControl = 348,
	CWaterBullet = 347,
	CVoteController = 346,
	CVGuiScreen = 345,
	CPropJeep = 0,
	CPropVehicleChoreoGeneric = 0,
	CTest_ProxyToggle_Networkable = 175,
	CTesla = 170,
	CTeamTrainWatcher = 128,
	CBaseTeamObjectiveResource = 16,
	CTeam = 125,
	CSun = 124,
	CParticlePerformanceMonitor = 91,
	CSpotlightEnd = 119,
	CSlideshowDisplay = 116,
	CShadowControl = 115,
	CSceneEntity = 114,
	CRopeKeyframe = 113,
	CRagdollManager = 109,
	CPhysicsPropMultiplayer = 98,
	CPhysBoxMultiplayer = 96,
	CBasePropDoor = 15,
	CDynamicProp = 33,
	CPointWorldText = 105,
	CPointCommentaryNode = 104,
	CPointCamera = 103,
	CPlayerResource = 102,
	CPlasma = 100,
	CPhysMagnet = 99,
	CPhysicsProp = 97,
	CPhysBox = 95,
	CParticleSystem = 92,
	CMaterialModifyControl = 81,
	CLightGlow = 79,
	CInfoOverlayAccessor = 77,
	CFuncTrackTrain = 67,
	CFuncSmokeVolume = 66,
	CFuncRotating = 65,
	CFuncReflectiveGlass = 62,
	CFuncOccluder = 60,
	CFuncMonitor = 59,
	CFunc_LOD = 54,
	CTEDust = 147,
	CFunc_Dust = 53,
	CFuncConveyor = 56,
	CBreakableSurface = 25,
	CFuncAreaPortalWindow = 55,
	CFish = 51,
	CEntityFlame = 38,
	CFireSmoke = 49,
	CEnvTonemapController = 46,
	CEnvScreenEffect = 44,
	CEnvScreenOverlay = 45,
	CEnvProjectedTexture = 42,
	CEnvParticleScript = 41,
	CFogController = 52,
	CEntityParticleTrail = 39,
	CEntityDissolve = 37,
	CDynamicLight = 32,
	CColorCorrectionVolume = 30,
	CColorCorrection = 29,
	CBreakableProp = 24,
	CBasePlayer = 13,
	CBaseFlex = 8,
	CBaseEntity = 7,
	CBaseDoor = 6,
	CBaseCombatCharacter = 4,
	CBaseAnimatingOverlay = 2,
	CBoneFollower = 20,
	CBaseAnimating = 1,
	CInfoLightingRelative = 76,
	CAI_BaseNPC = 0,
	CBeam = 19,
	CBaseViewModel = 18,
	CBaseProjectile = 14,
	CBaseParticleEntity = 12,
	CBaseGrenade = 9,
	CBaseCombatWeapon = 5
};

enum
{
	TF_STATE_ACTIVE = 0,
	TF_STATE_WELCOME,
	TF_STATE_OBSERVER,
	TF_STATE_DYING,
	TF_STATE_COUNT
};

enum
{
	TF_CLASS_UNDEFINED = 0,
	TF_CLASS_SCOUT,
	TF_CLASS_SNIPER,
	TF_CLASS_SOLDIER,
	TF_CLASS_DEMOMAN,
	TF_CLASS_MEDIC,
	TF_CLASS_HEAVY,
	TF_CLASS_PYRO,
	TF_CLASS_SPY,
	TF_CLASS_ENGINEER,
	TF_CLASS_CIVILIAN,
	TF_CLASS_COUNT_ALL,
	TF_CLASS_RANDOM
};

enum
{
	TF_TEAM_RED = LAST_SHARED_TEAM + 1,
	TF_TEAM_BLUE,
	TF_TEAM_COUNT
};

enum
{
	TEAM_ROLE_NONE = 0,
	TEAM_ROLE_DEFENDERS,
	TEAM_ROLE_ATTACKERS,
	NUM_TEAM_ROLES
};

enum ETFWeaponType
{
	TF_WEAPON_NONE,
	TF_WEAPON_BAT,
	TF_WEAPON_BAT_WOOD,
	TF_WEAPON_BOTTLE,
	TF_WEAPON_FIREAXE,
	TF_WEAPON_CLUB,
	TF_WEAPON_CROWBAR,
	TF_WEAPON_KNIFE,
	TF_WEAPON_FISTS,
	TF_WEAPON_SHOVEL,
	TF_WEAPON_WRENCH,
	TF_WEAPON_BONESAW,
	TF_WEAPON_SHOTGUN_PRIMARY,
	TF_WEAPON_SHOTGUN_SOLDIER,
	TF_WEAPON_SHOTGUN_HWG,
	TF_WEAPON_SHOTGUN_PYRO,
	TF_WEAPON_SCATTERGUN,
	TF_WEAPON_SNIPERRIFLE,
	TF_WEAPON_MINIGUN,
	TF_WEAPON_SMG,
	TF_WEAPON_SYRINGEGUN_MEDIC,
	TF_WEAPON_TRANQ,
	TF_WEAPON_ROCKETLAUNCHER,
	TF_WEAPON_GRENADELAUNCHER,
	TF_WEAPON_PIPEBOMBLAUNCHER,
	TF_WEAPON_FLAMETHROWER,
	TF_WEAPON_GRENADE_NORMAL,
	TF_WEAPON_GRENADE_CONCUSSION,
	TF_WEAPON_GRENADE_NAIL,
	TF_WEAPON_GRENADE_MIRV,
	TF_WEAPON_GRENADE_MIRV_DEMOMAN,
	TF_WEAPON_GRENADE_NAPALM,
	TF_WEAPON_GRENADE_GAS,
	TF_WEAPON_GRENADE_EMP,
	TF_WEAPON_GRENADE_CALTROP,
	TF_WEAPON_GRENADE_PIPEBOMB,
	TF_WEAPON_GRENADE_SMOKE_BOMB,
	TF_WEAPON_GRENADE_HEAL,
	TF_WEAPON_GRENADE_STUNBALL,
	TF_WEAPON_GRENADE_JAR,
	TF_WEAPON_GRENADE_JAR_MILK,
	TF_WEAPON_PISTOL,
	TF_WEAPON_PISTOL_SCOUT,
	TF_WEAPON_REVOLVER,
	TF_WEAPON_NAILGUN,
	TF_WEAPON_PDA,
	TF_WEAPON_PDA_ENGINEER_BUILD,
	TF_WEAPON_PDA_ENGINEER_DESTROY,
	TF_WEAPON_PDA_SPY,
	TF_WEAPON_BUILDER,
	TF_WEAPON_MEDIGUN,
	TF_WEAPON_GRENADE_MIRVBOMB,
	TF_WEAPON_FLAMETHROWER_ROCKET,
	TF_WEAPON_GRENADE_DEMOMAN,
	TF_WEAPON_SENTRY_BULLET,
	TF_WEAPON_SENTRY_ROCKET,
	TF_WEAPON_DISPENSER,
	TF_WEAPON_INVIS,
	TF_WEAPON_FLAREGUN,
	TF_WEAPON_LUNCHBOX,
	TF_WEAPON_JAR,
	TF_WEAPON_COMPOUND_BOW,
	TF_WEAPON_BUFF_ITEM,
	TF_WEAPON_PUMPKIN_BOMB,
	TF_WEAPON_SWORD,
	TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT,
	TF_WEAPON_LIFELINE,
	TF_WEAPON_LASER_POINTER,
	TF_WEAPON_DISPENSER_GUN,
	TF_WEAPON_SENTRY_REVENGE,
	TF_WEAPON_JAR_MILK,
	TF_WEAPON_HANDGUN_SCOUT_PRIMARY,
	TF_WEAPON_BAT_FISH,
	TF_WEAPON_CROSSBOW,
	TF_WEAPON_STICKBOMB,
	TF_WEAPON_HANDGUN_SCOUT_SECONDARY,
	TF_WEAPON_SODA_POPPER,
	TF_WEAPON_SNIPERRIFLE_DECAP,
	TF_WEAPON_RAYGUN,
	TF_WEAPON_PARTICLE_CANNON,
	TF_WEAPON_MECHANICAL_ARM,
	TF_WEAPON_DRG_POMSON,
	TF_WEAPON_BAT_GIFTWRAP,
	TF_WEAPON_GRENADE_ORNAMENT_BALL,
	TF_WEAPON_FLAREGUN_REVENGE,
	TF_WEAPON_PEP_BRAWLER_BLASTER,
	TF_WEAPON_CLEAVER,
	TF_WEAPON_GRENADE_CLEAVER,
	TF_WEAPON_STICKY_BALL_LAUNCHER,
	TF_WEAPON_GRENADE_STICKY_BALL,
	TF_WEAPON_SHOTGUN_BUILDING_RESCUE,
	TF_WEAPON_CANNON,
	TF_WEAPON_THROWABLE,
	TF_WEAPON_GRENADE_THROWABLE,
	TF_WEAPON_PDA_SPY_BUILD,
	TF_WEAPON_GRENADE_WATERBALLOON,
	TF_WEAPON_HARVESTER_SAW,
	TF_WEAPON_SPELLBOOK,
	TF_WEAPON_SPELLBOOK_PROJECTILE,
	TF_WEAPON_SNIPERRIFLE_CLASSIC,
	TF_WEAPON_PARACHUTE,
	TF_WEAPON_GRAPPLINGHOOK,
	TF_WEAPON_PASSTIME_GUN,
	TF_WEAPON_CHARGED_SMG,
	TF_WEAPON_BREAKABLE_SIGN,
	TF_WEAPON_ROCKETPACK,
	TF_WEAPON_SLAP,
	TF_WEAPON_JAR_GAS,
	TF_WEAPON_GRENADE_JAR_GAS,
	TF_WEAPON_FLAME_BALL
};

enum ETFWeapons
{
	Scout_m_Scattergun = 13,
	Scout_m_ScattergunR = 200,
	Scout_m_ForceANature = 45,
	Scout_m_TheShortstop = 220,
	Scout_m_TheSodaPopper = 448,
	Scout_m_FestiveScattergun = 669,
	Scout_m_BabyFacesBlaster = 772,
	Scout_m_SilverBotkillerScattergunMkI = 799,
	Scout_m_GoldBotkillerScattergunMkI = 808,
	Scout_m_RustBotkillerScattergunMkI = 888,
	Scout_m_BloodBotkillerScattergunMkI = 897,
	Scout_m_CarbonadoBotkillerScattergunMkI = 906,
	Scout_m_DiamondBotkillerScattergunMkI = 915,
	Scout_m_SilverBotkillerScattergunMkII = 964,
	Scout_m_GoldBotkillerScattergunMkII = 973,
	Scout_m_FestiveForceANature = 1078,
	Scout_m_TheBackScatter = 1103,
	Scout_m_NightTerror = 15002,
	Scout_m_TartanTorpedo = 15015,
	Scout_m_CountryCrusher = 15021,
	Scout_m_BackcountryBlaster = 15029,
	Scout_m_SpruceDeuce = 15036,
	Scout_m_CurrentEvent = 15053,
	Scout_m_MacabreWeb = 15065,
	Scout_m_Nutcracker = 15069,
	Scout_m_BlueMew = 15106,
	Scout_m_FlowerPower = 15107,
	Scout_m_ShottoHell = 15108,
	Scout_m_CoffinNail = 15131,
	Scout_m_KillerBee = 15151,
	Scout_m_Corsair = 15157,
	Scout_s_ScoutsPistol = 23,
	Scout_s_PistolR = 209,
	Scout_s_BonkAtomicPunch = 46,
	Scout_s_VintageLugermorph = 160,
	Scout_s_CritaCola = 163,
	Scout_s_MadMilk = 222,
	Scout_s_Lugermorph = 294,
	Scout_s_TheWinger = 449,
	Scout_s_PrettyBoysPocketPistol = 773,
	Scout_s_TheFlyingGuillotine = 812,
	Scout_s_TheFlyingGuillotineG = 833,
	Scout_s_MutatedMilk = 1121,
	Scout_s_FestiveBonk = 1145,
	Scout_s_RedRockRoscoe = 15013,
	Scout_s_HomemadeHeater = 15018,
	Scout_s_HickoryHolepuncher = 15035,
	Scout_s_LocalHero = 15041,
	Scout_s_BlackDahlia = 15046,
	Scout_s_SandstoneSpecial = 15056,
	Scout_s_MacabreWeb = 15060,
	Scout_s_Nutcracker = 15061,
	Scout_s_BlueMew = 15100,
	Scout_s_BrainCandy = 15101,
	Scout_s_ShottoHell = 15102,
	Scout_s_DressedToKill = 15126,
	Scout_s_Blitzkrieg = 15148,
	Scout_s_TheCAPPER = 30666,
	Scout_t_Bat = 0,
	Scout_t_BatR = 190,
	Scout_t_TheSandman = 44,
	Scout_t_TheHolyMackerel = 221,
	Scout_t_TheCandyCane = 317,
	Scout_t_TheBostonBasher = 325,
	Scout_t_SunonaStick = 349,
	Scout_t_TheFanOWar = 355,
	Scout_t_TheAtomizer = 450,
	Scout_t_ThreeRuneBlade = 452,
	Scout_t_TheConscientiousObjector = 474,
	Scout_t_UnarmedCombat = 572,
	Scout_t_TheWrapAssassin = 648,
	Scout_t_FestiveBat = 660,
	Scout_t_TheFreedomStaff = 880,
	Scout_t_TheBatOuttaHell = 939,
	Scout_t_TheMemoryMaker = 954,
	Scout_t_FestiveHolyMackerel = 999,
	Scout_t_TheHamShank = 1013,
	Scout_t_TheNecroSmasher = 1123,
	Scout_t_TheCrossingGuard = 1127,
	Scout_t_Batsaber = 30667,
	Scout_t_PrinnyMachete = 30758,
	Soldier_m_RocketLauncher = 18,
	Soldier_m_RocketLauncherR = 205,
	Soldier_m_TheDirectHit = 127,
	Soldier_m_TheBlackBox = 228,
	Soldier_m_RocketJumper = 237,
	Soldier_m_TheLibertyLauncher = 414,
	Soldier_m_TheCowMangler5000 = 441,
	Soldier_m_TheOriginal = 513,
	Soldier_m_FestiveRocketLauncher = 658,
	Soldier_m_TheBeggarsBazooka = 730,
	Soldier_m_SilverBotkillerRocketLauncherMkI = 800,
	Soldier_m_GoldBotkillerRocketLauncherMkI = 809,
	Soldier_m_RustBotkillerRocketLauncherMkI = 889,
	Soldier_m_BloodBotkillerRocketLauncherMkI = 898,
	Soldier_m_CarbonadoBotkillerRocketLauncherMkI = 907,
	Soldier_m_DiamondBotkillerRocketLauncherMkI = 916,
	Soldier_m_SilverBotkillerRocketLauncherMkII = 965,
	Soldier_m_GoldBotkillerRocketLauncherMkII = 974,
	Soldier_m_FestiveBlackBox = 1085,
	Soldier_m_TheAirStrike = 1104,
	Soldier_m_WoodlandWarrior = 15006,
	Soldier_m_SandCannon = 15014,
	Soldier_m_AmericanPastoral = 15028,
	Soldier_m_SmalltownBringdown = 15043,
	Soldier_m_ShellShocker = 15052,
	Soldier_m_AquaMarine = 15057,
	Soldier_m_Autumn = 15081,
	Soldier_m_BlueMew = 15104,
	Soldier_m_BrainCandy = 15105,
	Soldier_m_CoffinNail = 15129,
	Soldier_m_HighRollers = 15130,
	Soldier_m_Warhawk = 15150,
	Soldier_s_SoldiersShotgun = 10,
	Soldier_s_ShotgunR = 199,
	Soldier_s_TheBuffBanner = 129,
	Soldier_s_Gunboats = 133,
	Soldier_s_TheBattalionsBackup = 226,
	Soldier_s_TheConcheror = 354,
	Soldier_s_TheReserveShooter = 415,
	Soldier_s_TheRighteousBison = 442,
	Soldier_s_TheMantreads = 444,
	Soldier_s_FestiveBuffBanner = 1001,
	Soldier_s_TheBASEJumper = 1101,
	Soldier_s_FestiveShotgun = 1141,
	Soldier_s_PanicAttack = 1153,
	Soldier_s_BackwoodsBoomstick = 15003,
	Soldier_s_RusticRuiner = 15016,
	Soldier_s_CivicDuty = 15044,
	Soldier_s_LightningRod = 15047,
	Soldier_s_Autumn = 15085,
	Soldier_s_FlowerPower = 15109,
	Soldier_s_CoffinNail = 15132,
	Soldier_s_DressedtoKill = 15133,
	Soldier_s_RedBear = 15152,
	Soldier_t_Shovel = 6,
	Soldier_t_ShovelR = 196,
	Soldier_t_TheEqualizer = 128,
	Soldier_t_ThePainTrain = 154,
	Soldier_t_TheHalfZatoichi = 357,
	Soldier_t_TheMarketGardener = 416,
	Soldier_t_TheDisciplinaryAction = 447,
	Soldier_t_TheConscientiousObjector = 474,
	Soldier_t_TheEscapePlan = 775,
	Soldier_t_TheFreedomStaff = 880,
	Soldier_t_TheBatOuttaHell = 939,
	Soldier_t_TheMemoryMaker = 954,
	Soldier_t_TheHamShank = 1013,
	Soldier_t_TheNecroSmasher = 1123,
	Soldier_t_TheCrossingGuard = 1127,
	Soldier_t_PrinnyMachete = 30758,
	Pyro_m_FlameThrower = 21,
	Pyro_m_FlameThrowerR = 208,
	Pyro_m_TheBackburner = 40,
	Pyro_m_TheDegreaser = 215,
	Pyro_m_ThePhlogistinator = 594,
	Pyro_m_FestiveFlameThrower = 659,
	Pyro_m_TheRainblower = 741,
	Pyro_m_SilverBotkillerFlameThrowerMkI = 798,
	Pyro_m_GoldBotkillerFlameThrowerMkI = 807,
	Pyro_m_RustBotkillerFlameThrowerMkI = 887,
	Pyro_m_BloodBotkillerFlameThrowerMkI = 896,
	Pyro_m_CarbonadoBotkillerFlameThrowerMkI = 905,
	Pyro_m_DiamondBotkillerFlameThrowerMkI = 914,
	Pyro_m_SilverBotkillerFlameThrowerMkII = 963,
	Pyro_m_GoldBotkillerFlameThrowerMkII = 972,
	Pyro_m_FestiveBackburner = 1146,
	Pyro_m_DragonsFury = 1178,
	Pyro_m_ForestFire = 15005,
	Pyro_m_BarnBurner = 15017,
	Pyro_m_BovineBlazemaker = 15030,
	Pyro_m_EarthSkyandFire = 15034,
	Pyro_m_FlashFryer = 15049,
	Pyro_m_TurbineTorcher = 15054,
	Pyro_m_Autumn = 15066,
	Pyro_m_PumpkinPatch = 15067,
	Pyro_m_Nutcracker = 15068,
	Pyro_m_Balloonicorn = 15089,
	Pyro_m_Rainbow = 15090,
	Pyro_m_CoffinNail = 15115,
	Pyro_m_Warhawk = 15141,
	Pyro_m_NostromoNapalmer = 30474,
	Pyro_s_PyrosShotgun = 12,
	Pyro_s_ShotgunR = 199,
	Pyro_s_TheFlareGun = 39,
	Pyro_s_TheDetonator = 351,
	Pyro_s_TheReserveShooter = 415,
	Pyro_s_TheManmelter = 595,
	Pyro_s_TheScorchShot = 740,
	Pyro_s_FestiveFlareGun = 1081,
	Pyro_s_FestiveShotgun = 1141,
	Pyro_s_PanicAttack = 1153,
	Pyro_s_ThermalThruster = 1179,
	Pyro_s_GasPasser = 1180,
	Pyro_s_BackwoodsBoomstick = 15003,
	Pyro_s_RusticRuiner = 15016,
	Pyro_s_CivicDuty = 15044,
	Pyro_s_LightningRod = 15047,
	Pyro_s_Autumn = 15085,
	Pyro_s_FlowerPower = 15109,
	Pyro_s_CoffinNail = 15132,
	Pyro_s_DressedtoKill = 15133,
	Pyro_s_RedBear = 15152,
	Pyro_t_FireAxe = 2,
	Pyro_t_FireAxeR = 192,
	Pyro_t_TheAxtinguisher = 38,
	Pyro_t_Homewrecker = 153,
	Pyro_t_ThePowerjack = 214,
	Pyro_t_TheBackScratcher = 326,
	Pyro_t_SharpenedVolcanoFragment = 348,
	Pyro_t_ThePostalPummeler = 457,
	Pyro_t_TheMaul = 466,
	Pyro_t_TheConscientiousObjector = 474,
	Pyro_t_TheThirdDegree = 593,
	Pyro_t_TheLollichop = 739,
	Pyro_t_NeonAnnihilator = 813,
	Pyro_t_NeonAnnihilatorG = 834,
	Pyro_t_TheFreedomStaff = 880,
	Pyro_t_TheBatOuttaHell = 939,
	Pyro_t_TheMemoryMaker = 954,
	Pyro_t_TheFestiveAxtinguisher = 1000,
	Pyro_t_TheHamShank = 1013,
	Pyro_t_TheNecroSmasher = 1123,
	Pyro_t_TheCrossingGuard = 1127,
	Pyro_t_HotHand = 1181,
	Pyro_t_PrinnyMachete = 30758,
	Demoman_m_GrenadeLauncher = 19,
	Demoman_m_GrenadeLauncherR = 206,
	Demoman_m_TheLochnLoad = 308,
	Demoman_m_AliBabasWeeBooties = 405,
	Demoman_m_TheBootlegger = 608,
	Demoman_m_TheLooseCannon = 996,
	Demoman_m_FestiveGrenadeLauncher = 1007,
	Demoman_m_TheBASEJumper = 1101,
	Demoman_m_TheIronBomber = 1151,
	Demoman_m_Autumn = 15077,
	Demoman_m_MacabreWeb = 15079,
	Demoman_m_Rainbow = 15091,
	Demoman_m_SweetDreams = 15092,
	Demoman_m_CoffinNail = 15116,
	Demoman_m_TopShelf = 15117,
	Demoman_m_Warhawk = 15142,
	Demoman_m_ButcherBird = 15158,
	Demoman_s_StickybombLauncher = 20,
	Demoman_s_StickybombLauncherR = 207,
	Demoman_s_TheScottishResistance = 130,
	Demoman_s_TheCharginTarge = 131,
	Demoman_s_StickyJumper = 265,
	Demoman_s_TheSplendidScreen = 406,
	Demoman_s_FestiveStickybombLauncher = 661,
	Demoman_s_SilverBotkillerStickybombLauncherMkI = 797,
	Demoman_s_GoldBotkillerStickybombLauncherMkI = 806,
	Demoman_s_RustBotkillerStickybombLauncherMkI = 886,
	Demoman_s_BloodBotkillerStickybombLauncherMkI = 895,
	Demoman_s_CarbonadoBotkillerStickybombLauncherMkI = 904,
	Demoman_s_DiamondBotkillerStickybombLauncherMkI = 913,
	Demoman_s_SilverBotkillerStickybombLauncherMkII = 962,
	Demoman_s_GoldBotkillerStickybombLauncherMkII = 971,
	Demoman_s_TheTideTurner = 1099,
	Demoman_s_FestiveTarge = 1144,
	Demoman_s_TheQuickiebombLauncher = 1150,
	Demoman_s_SuddenFlurry = 15009,
	Demoman_s_CarpetBomber = 15012,
	Demoman_s_BlastedBombardier = 15024,
	Demoman_s_RooftopWrangler = 15038,
	Demoman_s_LiquidAsset = 15045,
	Demoman_s_PinkElephant = 15048,
	Demoman_s_Autumn = 15082,
	Demoman_s_PumpkinPatch = 15083,
	Demoman_s_MacabreWeb = 15084,
	Demoman_s_SweetDreams = 15113,
	Demoman_s_CoffinNail = 15137,
	Demoman_s_DressedtoKill = 15138,
	Demoman_s_Blitzkrieg = 15155,
	Demoman_t_Bottle = 1,
	Demoman_t_BottleR = 191,
	Demoman_t_TheEyelander = 132,
	Demoman_t_ThePainTrain = 154,
	Demoman_t_TheScotsmansSkullcutter = 172,
	Demoman_t_HorselessHeadlessHorsemannsHeadtaker = 266,
	Demoman_t_UllapoolCaber = 307,
	Demoman_t_TheClaidheamhMor = 327,
	Demoman_t_TheHalfZatoichi = 357,
	Demoman_t_ThePersianPersuader = 404,
	Demoman_t_TheConscientiousObjector = 474,
	Demoman_t_NessiesNineIron = 482,
	Demoman_t_TheScottishHandshake = 609,
	Demoman_t_TheFreedomStaff = 880,
	Demoman_t_TheBatOuttaHell = 939,
	Demoman_t_TheMemoryMaker = 954,
	Demoman_t_TheHamShank = 1013,
	Demoman_t_FestiveEyelander = 1082,
	Demoman_t_TheNecroSmasher = 1123,
	Demoman_t_TheCrossingGuard = 1127,
	Demoman_t_PrinnyMachete = 30758,
	Heavy_m_Minigun = 15,
	Heavy_m_MinigunR = 202,
	Heavy_m_Natascha = 41,
	Heavy_m_IronCurtain = 298,
	Heavy_m_TheBrassBeast = 312,
	Heavy_m_Tomislav = 424,
	Heavy_m_FestiveMinigun = 654,
	Heavy_m_SilverBotkillerMinigunMkI = 793,
	Heavy_m_GoldBotkillerMinigunMkI = 802,
	Heavy_m_TheHuoLongHeater = 811,
	Heavy_m_TheHuoLongHeaterG = 832,
	Heavy_m_Deflector_mvm = 850,
	Heavy_m_RustBotkillerMinigunMkI = 882,
	Heavy_m_BloodBotkillerMinigunMkI = 891,
	Heavy_m_CarbonadoBotkillerMinigunMkI = 900,
	Heavy_m_DiamondBotkillerMinigunMkI = 909,
	Heavy_m_SilverBotkillerMinigunMkII = 958,
	Heavy_m_GoldBotkillerMinigunMkII = 967,
	Heavy_m_KingoftheJungle = 15004,
	Heavy_m_IronWood = 15020,
	Heavy_m_AntiqueAnnihilator = 15026,
	Heavy_m_WarRoom = 15031,
	Heavy_m_CitizenPain = 15040,
	Heavy_m_BrickHouse = 15055,
	Heavy_m_MacabreWeb = 15086,
	Heavy_m_PumpkinPatch = 15087,
	Heavy_m_Nutcracker = 15088,
	Heavy_m_BrainCandy = 15098,
	Heavy_m_MisterCuddles = 15099,
	Heavy_m_CoffinNail = 15123,
	Heavy_m_DressedtoKill = 15124,
	Heavy_m_TopShelf = 15125,
	Heavy_m_ButcherBird = 15147,
	Heavy_s_HeavysShotgun = 11,
	Heavy_s_ShotgunR = 199,
	Heavy_s_Sandvich = 42,
	Heavy_s_TheDalokohsBar = 159,
	Heavy_s_TheBuffaloSteakSandvich = 311,
	Heavy_s_TheFamilyBusiness = 425,
	Heavy_s_Fishcake = 433,
	Heavy_s_RoboSandvich = 863,
	Heavy_s_FestiveSandvich = 1002,
	Heavy_s_FestiveShotgun = 1141,
	Heavy_s_PanicAttack = 1153,
	Heavy_s_SecondBanana = 1190,
	Heavy_s_BackwoodsBoomstick = 15003,
	Heavy_s_RusticRuiner = 15016,
	Heavy_s_CivicDuty = 15044,
	Heavy_s_LightningRod = 15047,
	Heavy_s_Autumn = 15085,
	Heavy_s_FlowerPower = 15109,
	Heavy_s_CoffinNail = 15132,
	Heavy_s_DressedtoKill = 15133,
	Heavy_s_RedBear = 15152,
	Heavy_t_Fists = 5,
	Heavy_t_FistsR = 195,
	Heavy_t_TheKillingGlovesofBoxing = 43,
	Heavy_t_GlovesofRunningUrgently = 239,
	Heavy_t_WarriorsSpirit = 310,
	Heavy_t_FistsofSteel = 331,
	Heavy_t_TheEvictionNotice = 426,
	Heavy_t_TheConscientiousObjector = 474,
	Heavy_t_ApocoFists = 587,
	Heavy_t_TheHolidayPunch = 656,
	Heavy_t_TheFreedomStaff = 880,
	Heavy_t_TheBatOuttaHell = 939,
	Heavy_t_TheMemoryMaker = 954,
	Heavy_t_TheHamShank = 1013,
	Heavy_t_FestiveGlovesofRunningUrgently = 1084,
	Heavy_t_TheBreadBite = 1100,
	Heavy_t_TheNecroSmasher = 1123,
	Heavy_t_TheCrossingGuard = 1127,
	Heavy_t_GlovesofRunningUrgentlyMvM = 1184,
	Heavy_t_PrinnyMachete = 30758,
	Engi_m_EngineersShotgun = 9,
	Engi_m_ShotgunR = 199,
	Engi_m_TheFrontierJustice = 141,
	Engi_m_TheWidowmaker = 527,
	Engi_m_ThePomson6000 = 588,
	Engi_m_TheRescueRanger = 997,
	Engi_m_FestiveFrontierJustice = 1004,
	Engi_m_FestiveShotgun = 1141,
	Engi_m_PanicAttack = 1153,
	Engi_m_BackwoodsBoomstick = 15003,
	Engi_m_RusticRuiner = 15016,
	Engi_m_CivicDuty = 15044,
	Engi_m_LightningRod = 15047,
	Engi_m_Autumn = 15085,
	Engi_m_FlowerPower = 15109,
	Engi_m_CoffinNail = 15132,
	Engi_m_DressedtoKill = 15133,
	Engi_m_RedBear = 15152,
	Engi_s_EngineersPistol = 22,
	Engi_s_PistolR = 209,
	Engi_s_TheWrangler = 140,
	Engi_s_VintageLugermorph = 160,
	Engi_s_Lugermorph = 294,
	Engi_s_TheShortCircuit = 528,
	Engi_s_FestiveWrangler = 1086,
	Engi_s_RedRockRoscoe = 15013,
	Engi_s_HomemadeHeater = 15018,
	Engi_s_HickoryHolepuncher = 15035,
	Engi_s_LocalHero = 15041,
	Engi_s_BlackDahlia = 15046,
	Engi_s_SandstoneSpecial = 15056,
	Engi_s_MacabreWeb = 15060,
	Engi_s_Nutcracker = 15061,
	Engi_s_BlueMew = 15100,
	Engi_s_BrainCandy = 15101,
	Engi_s_ShottoHell = 15102,
	Engi_s_DressedToKill = 15126,
	Engi_s_Blitzkrieg = 15148,
	Engi_s_TheCAPPER = 30666,
	Engi_s_TheGigarCounter = 30668,
	Engi_t_Wrench = 7,
	Engi_t_WrenchR = 197,
	Engi_t_TheGunslinger = 142,
	Engi_t_TheSouthernHospitality = 155,
	Engi_t_GoldenWrench = 169,
	Engi_t_TheJag = 329,
	Engi_t_TheEurekaEffect = 589,
	Engi_t_FestiveWrench = 662,
	Engi_t_SilverBotkillerWrenchMkI = 795,
	Engi_t_GoldBotkillerWrenchMkI = 804,
	Engi_t_RustBotkillerWrenchMkI = 884,
	Engi_t_BloodBotkillerWrenchMkI = 893,
	Engi_t_CarbonadoBotkillerWrenchMkI = 902,
	Engi_t_DiamondBotkillerWrenchMkI = 911,
	Engi_t_SilverBotkillerWrenchMkII = 960,
	Engi_t_GoldBotkillerWrenchMkII = 969,
	Engi_t_TheNecroSmasher = 1123,
	Engi_t_Nutcracker = 15073,
	Engi_t_Autumn = 15074,
	Engi_t_Boneyard = 15075,
	Engi_t_DressedtoKill = 15139,
	Engi_t_TopShelf = 15140,
	Engi_t_TorquedtoHell = 15114,
	Engi_t_Airwolf = 15156,
	Engi_t_PrinnyMachete = 30758,
	Engi_p_ConstructionPDA = 25,
	Engi_p_ConstructionPDAR = 737,
	Engi_p_DestructionPDA = 26,
	Engi_p_PDA = 28,
	Medic_m_SyringeGun = 17,
	Medic_m_SyringeGunR = 204,
	Medic_m_TheBlutsauger = 36,
	Medic_m_CrusadersCrossbow = 305,
	Medic_m_TheOverdose = 412,
	Medic_m_FestiveCrusadersCrossbow = 1079,
	Medic_s_MediGun = 29,
	Medic_s_MediGunR = 211,
	Medic_s_TheKritzkrieg = 35,
	Medic_s_TheQuickFix = 411,
	Medic_s_FestiveMediGun = 663,
	Medic_s_SilverBotkillerMediGunMkI = 796,
	Medic_s_GoldBotkillerMediGunMkI = 805,
	Medic_s_RustBotkillerMediGunMkI = 885,
	Medic_s_BloodBotkillerMediGunMkI = 894,
	Medic_s_CarbonadoBotkillerMediGunMkI = 903,
	Medic_s_DiamondBotkillerMediGunMkI = 912,
	Medic_s_SilverBotkillerMediGunMkII = 961,
	Medic_s_GoldBotkillerMediGunMkII = 970,
	Medic_s_TheVaccinator = 998,
	Medic_s_MaskedMender = 15008,
	Medic_s_WrappedReviver = 15010,
	Medic_s_ReclaimedReanimator = 15025,
	Medic_s_CivilServant = 15039,
	Medic_s_SparkofLife = 15050,
	Medic_s_Wildwood = 15078,
	Medic_s_FlowerPower = 15097,
	Medic_s_DressedToKill = 15121,
	Medic_s_HighRollers = 15122,
	Medic_s_CoffinNail = 15123,
	Medic_s_Blitzkrieg = 15145,
	Medic_s_Corsair = 15146,
	Medic_t_Bonesaw = 8,
	Medic_t_BonesawR = 198,
	Medic_t_TheUbersaw = 37,
	Medic_t_TheVitaSaw = 173,
	Medic_t_Amputator = 304,
	Medic_t_TheSolemnVow = 413,
	Medic_t_TheConscientiousObjector = 474,
	Medic_t_TheFreedomStaff = 880,
	Medic_t_TheBatOuttaHell = 939,
	Medic_t_TheMemoryMaker = 954,
	Medic_t_FestiveUbersaw = 1003,
	Medic_t_TheHamShank = 1013,
	Medic_t_TheNecroSmasher = 1123,
	Medic_t_TheCrossingGuard = 1127,
	Medic_t_FestiveBonesaw = 1143,
	Medic_t_PrinnyMachete = 30758,
	Sniper_m_SniperRifle = 14,
	Sniper_m_SniperRifleR = 201,
	Sniper_m_TheHuntsman = 56,
	Sniper_m_TheSydneySleeper = 230,
	Sniper_m_TheBazaarBargain = 402,
	Sniper_m_TheMachina = 526,
	Sniper_m_FestiveSniperRifle = 664,
	Sniper_m_TheHitmansHeatmaker = 752,
	Sniper_m_SilverBotkillerSniperRifleMkI = 792,
	Sniper_m_GoldBotkillerSniperRifleMkI = 801,
	Sniper_m_TheAWPerHand = 851,
	Sniper_m_RustBotkillerSniperRifleMkI = 881,
	Sniper_m_BloodBotkillerSniperRifleMkI = 890,
	Sniper_m_CarbonadoBotkillerSniperRifleMkI = 899,
	Sniper_m_DiamondBotkillerSniperRifleMkI = 908,
	Sniper_m_SilverBotkillerSniperRifleMkII = 957,
	Sniper_m_GoldBotkillerSniperRifleMkII = 966,
	Sniper_m_FestiveHuntsman = 1005,
	Sniper_m_TheFortifiedCompound = 1092,
	Sniper_m_TheClassic = 1098,
	Sniper_m_NightOwl = 15000,
	Sniper_m_PurpleRange = 15007,
	Sniper_m_LumberFromDownUnder = 15019,
	Sniper_m_ShotintheDark = 15023,
	Sniper_m_Bogtrotter = 15033,
	Sniper_m_Thunderbolt = 15059,
	Sniper_m_PumpkinPatch = 15070,
	Sniper_m_Boneyard = 15071,
	Sniper_m_Wildwood = 15072,
	Sniper_m_Balloonicorn = 15111,
	Sniper_m_Rainbow = 15112,
	Sniper_m_CoffinNail = 15135,
	Sniper_m_DressedtoKill = 15136,
	Sniper_m_Airwolf = 15154,
	Sniper_m_ShootingStar = 30665,
	Sniper_s_SMG = 16,
	Sniper_s_SMGR = 203,
	Sniper_s_TheRazorback = 57,
	Sniper_s_Jarate = 58,
	Sniper_s_DarwinsDangerShield = 231,
	Sniper_s_CozyCamper = 642,
	Sniper_s_TheCleanersCarbine = 751,
	Sniper_s_FestiveJarate = 1083,
	Sniper_s_TheSelfAwareBeautyMark = 1105,
	Sniper_s_FestiveSMG = 1149,
	Sniper_s_WoodsyWidowmaker = 15001,
	Sniper_s_PlaidPotshotter = 15022,
	Sniper_s_TreadplateTormenter = 15032,
	Sniper_s_TeamSprayer = 15037,
	Sniper_s_LowProfile = 15058,
	Sniper_s_Wildwood = 15076,
	Sniper_s_BlueMew = 15110,
	Sniper_s_HighRollers = 15134,
	Sniper_s_Blitzkrieg = 15153,
	Sniper_t_Kukri = 3,
	Sniper_t_KukriR = 193,
	Sniper_t_TheTribalmansShiv = 171,
	Sniper_t_TheBushwacka = 232,
	Sniper_t_TheShahanshah = 401,
	Sniper_t_TheConscientiousObjector = 474,
	Sniper_t_TheFreedomStaff = 880,
	Sniper_t_TheBatOuttaHell = 939,
	Sniper_t_TheMemoryMaker = 954,
	Sniper_t_TheHamShank = 1013,
	Sniper_t_TheNecroSmasher = 1123,
	Sniper_t_TheCrossingGuard = 1127,
	Sniper_t_PrinnyMachete = 30758,
	Spy_m_Revolver = 24,
	Spy_m_RevolverR = 210,
	Spy_m_TheAmbassador = 61,
	Spy_m_BigKill = 161,
	Spy_m_LEtranger = 224,
	Spy_m_TheEnforcer = 460,
	Spy_m_TheDiamondback = 525,
	Spy_m_FestiveAmbassador = 1006,
	Spy_m_FestiveRevolver = 1142,
	Spy_m_PsychedelicSlugger = 15011,
	Spy_m_OldCountry = 15027,
	Spy_m_Mayor = 15042,
	Spy_m_DeadReckoner = 15051,
	Spy_m_Boneyard = 15062,
	Spy_m_Wildwood = 15063,
	Spy_m_MacabreWeb = 15064,
	Spy_m_FlowerPower = 15103,
	Spy_m_TopShelf = 15128,
	Spy_m_CoffinNail = 15129,
	Spy_m_Blitzkrieg = 15149,
	Spy_s_Sapper = 735,
	Spy_s_SapperR = 736,
	Spy_s_TheRedTapeRecorder = 810,
	Spy_s_TheRedTapeRecorderG = 831,
	Spy_s_TheApSapG = 933,
	Spy_s_FestiveSapper = 1080,
	Spy_s_TheSnackAttack = 1102,
	Spy_t_Knife = 4,
	Spy_t_KnifeR = 194,
	Spy_t_YourEternalReward = 225,
	Spy_t_ConniversKunai = 356,
	Spy_t_TheBigEarner = 461,
	Spy_t_TheWangaPrick = 574,
	Spy_t_TheSharpDresser = 638,
	Spy_t_TheSpycicle = 649,
	Spy_t_FestiveKnife = 665,
	Spy_t_TheBlackRose = 727,
	Spy_t_SilverBotkillerKnifeMkI = 794,
	Spy_t_GoldBotkillerKnifeMkI = 803,
	Spy_t_RustBotkillerKnifeMkI = 883,
	Spy_t_BloodBotkillerKnifeMkI = 892,
	Spy_t_CarbonadoBotkillerKnifeMkI = 901,
	Spy_t_DiamondBotkillerKnifeMkI = 910,
	Spy_t_SilverBotkillerKnifeMkII = 959,
	Spy_t_GoldBotkillerKnifeMkII = 968,
	Spy_t_Boneyard = 15062,
	Spy_t_BlueMew = 15094,
	Spy_t_BrainCandy = 15095,
	Spy_t_StabbedtoHell = 15096,
	Spy_t_DressedtoKill = 15118,
	Spy_t_TopShelf = 15119,
	Spy_t_Blitzkrieg = 15143,
	Spy_t_Airwolf = 15144,
	Spy_t_PrinnyMachete = 30758,
	Spy_d_DisguiseKitPDA = 27,
	Spy_w_InvisWatch = 30,
	Spy_w_InvisWatchR = 212,
	Spy_w_TheDeadRinger = 59,
	Spy_w_TheCloakandDagger = 60,
	Spy_w_EnthusiastsTimepiece = 297,
	Spy_w_TheQuackenbirdt = 947,
	Misc_t_FryingPan = 264,
	Misc_t_GoldFryingPan = 1071,
	Misc_t_Saxxy = 423
};

enum struct EWeaponType
{
	UNKNOWN,
	HITSCAN,
	PROJECTILE,
	MELEE
};

enum EWeaponSlot
{
	SLOT_PRIMARY,
	SLOT_SECONDARY,
	SLOT_MELEE
};

enum ProjectileType_t
{
	TF_PROJECTILE_NONE,
	TF_PROJECTILE_BULLET,
	TF_PROJECTILE_ROCKET,
	TF_PROJECTILE_PIPEBOMB,
	TF_PROJECTILE_PIPEBOMB_REMOTE,
	TF_PROJECTILE_SYRINGE,
	TF_PROJECTILE_FLARE,
	TF_PROJECTILE_JAR,
	TF_PROJECTILE_ARROW,
	TF_PROJECTILE_FLAME_ROCKET,
	TF_PROJECTILE_JAR_MILK,
	TF_PROJECTILE_HEALING_BOLT,
	TF_PROJECTILE_ENERGY_BALL,
	TF_PROJECTILE_ENERGY_RING,
	TF_PROJECTILE_PIPEBOMB_PRACTICE,
	TF_PROJECTILE_CLEAVER,
	TF_PROJECTILE_STICKY_BALL,
	TF_PROJECTILE_CANNONBALL,
	TF_PROJECTILE_BUILDING_REPAIR_BOLT,
	TF_PROJECTILE_FESTIVE_ARROW,
	TF_PROJECTILE_THROWABLE,
	TF_PROJECTILE_SPELL,
	TF_PROJECTILE_FESTIVE_JAR,
	TF_PROJECTILE_FESTIVE_HEALING_BOLT,
	TF_PROJECTILE_BREADMONSTER_JARATE,
	TF_PROJECTILE_BREADMONSTER_MADMILK,
	TF_PROJECTILE_GRAPPLINGHOOK,
	TF_PROJECTILE_SENTRY_ROCKET,
	TF_PROJECTILE_BREAD_MONSTER,
	TF_PROJECTILE_JAR_GAS,
	TF_PROJECTILE_BALLOFFIRE,
	TF_NUM_PROJECTILES
};

enum ETFCond
{
	TF_COND_INVALID = -1,
	TF_COND_AIMING,
	TF_COND_ZOOMED,
	TF_COND_DISGUISING,
	TF_COND_DISGUISED,
	TF_COND_STEALTHED,
	TF_COND_INVULNERABLE,
	TF_COND_TELEPORTED,
	TF_COND_TAUNTING,
	TF_COND_INVULNERABLE_WEARINGOFF,
	TF_COND_STEALTHED_BLINK,
	TF_COND_SELECTED_TO_TELEPORT,
	TF_COND_CRITBOOSTED,
	TF_COND_TMPDAMAGEBONUS,
	TF_COND_FEIGN_DEATH,
	TF_COND_PHASE,
	TF_COND_STUNNED,
	TF_COND_OFFENSEBUFF,
	TF_COND_SHIELD_CHARGE,
	TF_COND_DEMO_BUFF,
	TF_COND_ENERGY_BUFF,
	TF_COND_RADIUSHEAL,
	TF_COND_HEALTH_BUFF,
	TF_COND_BURNING,
	TF_COND_HEALTH_OVERHEALED,
	TF_COND_URINE,
	TF_COND_BLEEDING,
	TF_COND_DEFENSEBUFF,
	TF_COND_MAD_MILK,
	TF_COND_MEGAHEAL,
	TF_COND_REGENONDAMAGEBUFF,
	TF_COND_MARKEDFORDEATH,
	TF_COND_NOHEALINGDAMAGEBUFF,
	TF_COND_SPEED_BOOST,
	TF_COND_CRITBOOSTED_PUMPKIN,
	TF_COND_CRITBOOSTED_USER_BUFF,
	TF_COND_CRITBOOSTED_DEMO_CHARGE,
	TF_COND_SODAPOPPER_HYPE,
	TF_COND_CRITBOOSTED_FIRST_BLOOD,
	TF_COND_CRITBOOSTED_BONUS_TIME,
	TF_COND_CRITBOOSTED_CTF_CAPTURE,
	TF_COND_CRITBOOSTED_ON_KILL,
	TF_COND_CANNOT_SWITCH_FROM_MELEE,
	TF_COND_DEFENSEBUFF_NO_CRIT_BLOCK,
	TF_COND_REPROGRAMMED,
	TF_COND_CRITBOOSTED_RAGE_BUFF,
	TF_COND_DEFENSEBUFF_HIGH,
	TF_COND_SNIPERCHARGE_RAGE_BUFF,
	TF_COND_DISGUISE_WEARINGOFF,
	TF_COND_MARKEDFORDEATH_SILENT,
	TF_COND_DISGUISED_AS_DISPENSER,
	TF_COND_SAPPED,
	TF_COND_INVULNERABLE_HIDE_UNLESS_DAMAGED,
	TF_COND_INVULNERABLE_USER_BUFF,
	TF_COND_HALLOWEEN_BOMB_HEAD,
	TF_COND_HALLOWEEN_THRILLER,
	TF_COND_RADIUSHEAL_ON_DAMAGE,
	TF_COND_CRITBOOSTED_CARD_EFFECT,
	TF_COND_INVULNERABLE_CARD_EFFECT,
	TF_COND_MEDIGUN_UBER_BULLET_RESIST,
	TF_COND_MEDIGUN_UBER_BLAST_RESIST,
	TF_COND_MEDIGUN_UBER_FIRE_RESIST,
	TF_COND_MEDIGUN_SMALL_BULLET_RESIST,
	TF_COND_MEDIGUN_SMALL_BLAST_RESIST,
	TF_COND_MEDIGUN_SMALL_FIRE_RESIST,
	TF_COND_STEALTHED_USER_BUFF,
	TF_COND_MEDIGUN_DEBUFF,
	TF_COND_STEALTHED_USER_BUFF_FADING,
	TF_COND_BULLET_IMMUNE,
	TF_COND_BLAST_IMMUNE,
	TF_COND_FIRE_IMMUNE,
	TF_COND_PREVENT_DEATH,
	TF_COND_MVM_BOT_STUN_RADIOWAVE,
	TF_COND_HALLOWEEN_SPEED_BOOST,
	TF_COND_HALLOWEEN_QUICK_HEAL,
	TF_COND_HALLOWEEN_GIANT,
	TF_COND_HALLOWEEN_TINY,
	TF_COND_HALLOWEEN_IN_HELL,
	TF_COND_HALLOWEEN_GHOST_MODE,
	TF_COND_MINICRITBOOSTED_ON_KILL,
	TF_COND_OBSCURED_SMOKE,
	TF_COND_PARACHUTE_ACTIVE,
	TF_COND_BLASTJUMPING,
	TF_COND_HALLOWEEN_KART,
	TF_COND_HALLOWEEN_KART_DASH,
	TF_COND_BALLOON_HEAD,
	TF_COND_MELEE_ONLY,
	TF_COND_SWIMMING_CURSE,
	TF_COND_FREEZE_INPUT,
	TF_COND_HALLOWEEN_KART_CAGE,
	TF_COND_HASRUNE, //TF_COND_DONOTUSE_0
	TF_COND_RUNE_STRENGTH,
	TF_COND_RUNE_HASTE,
	TF_COND_RUNE_REGEN,
	TF_COND_RUNE_RESIST,
	TF_COND_RUNE_VAMPIRE,
	TF_COND_RUNE_REFLECT,
	TF_COND_RUNE_PRECISION,
	TF_COND_RUNE_AGILITY,
	TF_COND_GRAPPLINGHOOK,
	TF_COND_GRAPPLINGHOOK_SAFEFALL,
	TF_COND_GRAPPLINGHOOK_LATCHED,
	TF_COND_GRAPPLINGHOOK_BLEEDING,
	TF_COND_AFTERBURN_IMMUNE,
	TF_COND_RUNE_KNOCKOUT,
	TF_COND_RUNE_IMBALANCE,
	TF_COND_CRITBOOSTED_RUNE_TEMP,
	TF_COND_PASSTIME_INTERCEPTION,
	TF_COND_SWIMMING_NO_EFFECTS,
	TF_COND_PURGATORY,
	TF_COND_RUNE_KING,
	TF_COND_RUNE_PLAGUE,
	TF_COND_RUNE_SUPERNOVA,
	TF_COND_PLAGUE,
	TF_COND_KING_BUFFED,
	TF_COND_TEAM_GLOWS,
	TF_COND_KNOCKED_INTO_AIR,
	TF_COND_COMPETITIVE_WINNER,
	TF_COND_COMPETITIVE_LOSER,
	TF_COND_HEALING_DEBUFF,
	TF_COND_PASSTIME_PENALTY_DEBUFF,
	TF_COND_GRAPPLED_TO_PLAYER,
	TF_COND_GRAPPLED_BY_PLAYER,
	TF_COND_PARACHUTE_DEPLOYED,
	TF_COND_GAS,
	TF_COND_BURNING_PYRO,
	TF_COND_ROCKETPACK,
	TF_COND_LOST_FOOTING,
	TF_COND_AIR_CURRENT,
	TF_COND_HALLOWEEN_HELL_HEAL,
	TF_COND_POWERUPMODE_DOMINANT
};

enum gamerules_roundstate_t
{
	// initialize the game, create teams
	GR_STATE_INIT = 0,

	//Before players have joined the game. Periodically checks to see if enough players are ready
	//to start a game. Also reverts to this when there are no active players
	GR_STATE_PREGAME,

	//The game is about to start, wait a bit and spawn everyone
	GR_STATE_STARTGAME,

	//All players are respawned, frozen in place
	GR_STATE_PREROUND,

	//Round is on, playing normally
	GR_STATE_RND_RUNNING,

	//Someone has won the round
	GR_STATE_TEAM_WIN,

	//Noone has won, manually restart the game, reset scores
	GR_STATE_RESTART,

	//Noone has won, restart the game
	GR_STATE_STALEMATE,

	//Game is over, showing the scoreboard etc
	GR_STATE_GAME_OVER,

	//Game is in a bonus state, transitioned to after a round ends
	GR_STATE_BONUS,

	//Game is awaiting the next wave/round of a multi round experience
	GR_STATE_BETWEEN_RNDS,

	GR_NUM_ROUND_STATES
};

enum
{
	TF_ARENA_NOTIFICATION_CAREFUL = 0,
	TF_ARENA_NOTIFICATION_SITOUT,
	TF_ARENA_NOTIFICATION_NOPLAYERS
};

enum
{
	MVM_EVENT_POPFILE_NONE = 0,
	MVM_EVENT_POPFILE_HALLOWEEN,
	MVM_EVENT_POPFILE_MAX_TYPES
};

enum
{
	DRAW_ARROW_UP,
	DRAW_ARROW_LEFT,
	DRAW_ARROW_RIGHT
};

enum
{
	TF_HUDTYPE_UNDEFINED = 0,
	TF_HUDTYPE_CTF,
	TF_HUDTYPE_CP,
	TF_HUDTYPE_ESCORT,
	TF_HUDTYPE_ARENA,
	TF_HUDTYPE_TRAINING
};

enum
{
	TF_BUILDING_SENTRY = (1 << 0),
	TF_BUILDING_DISPENSER = (1 << 1),
	TF_BUILDING_TELEPORT = (1 << 2)
};

enum
{
	TF_ITEM_UNDEFINED = 0,
	TF_ITEM_CAPTURE_FLAG = (1 << 0),
	TF_ITEM_HEALTH_KIT = (1 << 1),
	TF_ITEM_ARMOR = (1 << 2),
	TF_ITEM_AMMO_PACK = (1 << 3),
	TF_ITEM_GRENADE_PACK = (1 << 4)
};

enum
{
	MODE_TELEPORTER_ENTRANCE = 0,
	MODE_TELEPORTER_EXIT
};

enum
{
	MODE_SENTRYGUN_NORMAL = 0,
	MODE_SENTRYGUN_DISPOSABLE
};

enum
{
	MODE_SAPPER_NORMAL = 0,
	MODE_SAPPER_ANTI_ROBOT,
	MODE_SAPPER_ANTI_ROBOT_RADIUS
};

enum
{
	DISPENSER_STATE_IDLE,
	DISPENSER_STATE_UPGRADING
};

enum
{
	TELEPORTER_STATE_BUILDING = 0,
	TELEPORTER_STATE_IDLE,
	TELEPORTER_STATE_READY,
	TELEPORTER_STATE_SENDING,
	TELEPORTER_STATE_RECEIVING,
	TELEPORTER_STATE_RECEIVING_RELEASE,
	TELEPORTER_STATE_RECHARGING,
	TELEPORTER_STATE_UPGRADING,
};
enum
{
	SENTRY_STATE_INACTIVE = 0,
	SENTRY_STATE_SEARCHING,
	SENTRY_STATE_ATTACKING,
	SENTRY_STATE_UPGRADING,
	SENTRY_NUM_STATES
};

enum
{
	OF_ALLOW_REPEAT_PLACEMENT = 0x01,
	OF_MUST_BE_BUILT_ON_ATTACHMENT = 0x02,
	OF_DOESNT_HAVE_A_MODEL = 0x04,
	OF_PLAYER_DESTRUCTION = 0x08,
	OF_BIT_COUNT = 4
};

enum
{
	BS_IDLE = 0,
	BS_SELECTING,
	BS_PLACING,
	BS_PLACING_INVALID
};

enum
{
	BUILDER_OBJECT_BITS = 8,
	BUILDER_INVALID_OBJECT = ((1 << BUILDER_OBJECT_BITS) - 1)
};

enum
{
	MVM_UPGRADE_QUALITY_LOW = 1,
	MVM_UPGRADE_QUALITY_NORMAL,
	MVM_UPGRADE_QAULITY_HIGH
};

enum
{
	TF_SAPEVENT_NONE = 0,
	TF_SAPEVENT_PLACED,
	TF_SAPEVENT_DONE
};

enum ETFMatchGroup
{
	k_eTFMatchGroup_Invalid = -1,
	k_eTFMatchGroup_MvM_Practice = 0,
	k_eTFMatchGroup_MvM_MannUp = 1,
	k_eTFMatchGroup_First = 0,
	k_eTFMatchGroup_MvM_Default = 0,
	k_eTFMatchGroup_MvM_First = 0,
	k_eTFMatchGroup_MvM_Last = 1,
	k_eTFMatchGroup_Ladder_6v6 = 2,
	k_eTFMatchGroup_Ladder_9v9 = 3,
	k_eTFMatchGroup_Ladder_12v12 = 4,
	k_eTFMatchGroup_Ladder_Default = 2,
	k_eTFMatchGroup_Ladder_First = 2,
	k_eTFMatchGroup_Ladder_Last = 4,
	k_eTFMatchGroup_Casual_6v6 = 5,
	k_eTFMatchGroup_Casual_9v9 = 6,
	k_eTFMatchGroup_Casual_12v12 = 7,
	k_eTFMatchGroup_Casual_Default = 7,
	k_eTFMatchGroup_Casual_First = 5,
	k_eTFMatchGroup_Casual_Last = 7,
	k_eTFMatchGroup_Event_Placeholder = 8,
	k_eTFMatchGroup_Event_Default = 8,
	k_eTFMatchGroup_Event_First = 8,
	k_eTFMatchGroup_Event_Last = 8
};

enum
{
	PARTITION_ENGINE_SOLID_EDICTS = (1 << 0),
	PARTITION_ENGINE_TRIGGER_EDICTS = (1 << 1),
	PARTITION_CLIENT_SOLID_EDICTS = (1 << 2),
	PARTITION_CLIENT_RESPONSIVE_EDICTS = (1 << 3),
	PARTITION_ENGINE_NON_STATIC_EDICTS = (1 << 4),
	PARTITION_CLIENT_STATIC_PROPS = (1 << 5),
	PARTITION_ENGINE_STATIC_PROPS = (1 << 6),
	PARTITION_CLIENT_NON_STATIC_EDICTS = (1 << 7)
};

enum soundlevel_t
{
	SNDLVL_NONE = 0,
	SNDLVL_20dB = 20,
	SNDLVL_25dB = 25,
	SNDLVL_30dB = 30,
	SNDLVL_35dB = 35,
	SNDLVL_40dB = 40,
	SNDLVL_45dB = 45,
	SNDLVL_50dB = 50,
	SNDLVL_55dB = 55,
	SNDLVL_IDLE = 60,
	SNDLVL_60dB = 60,
	SNDLVL_65dB = 65,
	SNDLVL_STATIC = 66,
	SNDLVL_70dB = 70,
	SNDLVL_NORM = 75,
	SNDLVL_75dB = 75,
	SNDLVL_80dB = 80,
	SNDLVL_TALKING = 80,
	SNDLVL_85dB = 85,
	SNDLVL_90dB = 90,
	SNDLVL_95dB = 95,
	SNDLVL_100dB = 100,
	SNDLVL_105dB = 105,
	SNDLVL_110dB = 110,
	SNDLVL_120dB = 120,
	SNDLVL_130dB = 130,
	SNDLVL_GUNFIRE = 140,
	SNDLVL_140dB = 140,
	SNDLVL_150dB = 150,
	SNDLVL_180dB = 180
};

enum
{
	WL_NotInWater = 0,
	WL_Feet,
	WL_Waist,
	WL_Eyes
};

class CAudioSource;

struct StartSoundParams_t;   // body dropped: only ever named through a pointer here

// ================ SDK/Definitions/Interfaces/Interface.h ================
#include "utils.h"

class IBaseInterface
{
public:
	virtual	~IBaseInterface() {}
};

typedef void* (*CreateInterfaceFn)(const char* pName, int* pReturnCode);

// ================ SDK/Definitions/Misc/VGUI.h ================
typedef unsigned long long VPANEL;
typedef unsigned long HScheme;
typedef unsigned long HTexture;
typedef unsigned long HCursor;
typedef unsigned long HFont;

// ================ SDK/Definitions/Misc/InputEnums.h ================

enum
{
	MAX_JOYSTICKS = 1,
	MOUSE_BUTTON_COUNT = 5,
	MAX_NOVINT_DEVICES = 2
};

enum JoystickAxis_t
{
	JOY_AXIS_X = 0,
	JOY_AXIS_Y,
	JOY_AXIS_Z,
	JOY_AXIS_R,
	JOY_AXIS_U,
	JOY_AXIS_V,
	MAX_JOYSTICK_AXES
};

enum
{
	MS_WM_XBUTTONDOWN = 0x020B,
	MS_WM_XBUTTONUP = 0x020C,
	MS_WM_XBUTTONDBLCLK = 0x020D,
	MS_MK_BUTTON4 = 0x0020,
	MS_MK_BUTTON5 = 0x0040
};

struct InputEvent_t
{
	int m_nType = 0;
	int m_nTick = 0;
	int m_nData = 0;
	int m_nData2 = 0;
	int m_nData3 = 0;
};

typedef enum
{
	SK_NULL,
	SK_BUTTON_A,
	SK_BUTTON_B,
	SK_BUTTON_X,
	SK_BUTTON_Y,
	SK_BUTTON_UP,
	SK_BUTTON_RIGHT,
	SK_BUTTON_DOWN,
	SK_BUTTON_LEFT,
	SK_BUTTON_LEFT_BUMPER,
	SK_BUTTON_RIGHT_BUMPER,
	SK_BUTTON_LEFT_TRIGGER,
	SK_BUTTON_RIGHT_TRIGGER,
	SK_BUTTON_LEFT_GRIP,
	SK_BUTTON_RIGHT_GRIP,
	SK_BUTTON_LPAD_TOUCH,
	SK_BUTTON_RPAD_TOUCH,
	SK_BUTTON_LPAD_CLICK,
	SK_BUTTON_RPAD_CLICK,
	SK_BUTTON_LPAD_UP,
	SK_BUTTON_LPAD_RIGHT,
	SK_BUTTON_LPAD_DOWN,
	SK_BUTTON_LPAD_LEFT,
	SK_BUTTON_RPAD_UP,
	SK_BUTTON_RPAD_RIGHT,
	SK_BUTTON_RPAD_DOWN,
	SK_BUTTON_RPAD_LEFT,
	SK_BUTTON_SELECT,
	SK_BUTTON_START,
	SK_BUTTON_STEAM,
	SK_BUTTON_INACTIVE_START,
	SK_VBUTTON_F1,
	SK_VBUTTON_F2,
	SK_VBUTTON_F3,
	SK_VBUTTON_F4,
	SK_VBUTTON_F5,
	SK_VBUTTON_F6,
	SK_VBUTTON_F7,
	SK_VBUTTON_F8,
	SK_VBUTTON_F9,
	SK_VBUTTON_F10,
	SK_VBUTTON_F11,
	SK_VBUTTON_F12,
	SK_MAX_KEYS
} sKey_t;

enum ESteamPadAxis
{
	LEFTPAD_AXIS_X,
	LEFTPAD_AXIS_Y,
	RIGHTPAD_AXIS_X,
	RIGHTPAD_AXIS_Y,
	LEFT_TRIGGER_AXIS,
	RIGHT_TRIGGER_AXIS,
	GYRO_AXIS_PITCH,
	GYRO_AXIS_ROLL,
	GYRO_AXIS_YAW,
	MAX_STEAMPADAXIS = GYRO_AXIS_YAW
};

enum
{
	LASTINPUT_KBMOUSE = 0,
	LASTINPUT_CONTROLLER = 1,
	LASTINPUT_STEAMCONTROLLER = 2
};

enum GameActionSet_t
{
	GAME_ACTION_SET_NONE = -1,
	GAME_ACTION_SET_MENUCONTROLS = 0,
	GAME_ACTION_SET_FPSCONTROLS,
	GAME_ACTION_SET_IN_GAME_HUD,
	GAME_ACTION_SET_SPECTATOR
};

enum GameActionSetFlags_t
{
	GAME_ACTION_SET_FLAGS_NONE = 0,
	GAME_ACTION_SET_FLAGS_TAUNTING = (1 << 0)
};

// ================ SDK/Definitions/Misc/ButtonCode.h ================
enum
{
	JOYSTICK_MAX_BUTTON_COUNT = 32,
	JOYSTICK_POV_BUTTON_COUNT = 4,
	JOYSTICK_AXIS_BUTTON_COUNT = MAX_JOYSTICK_AXES * 2
};

enum
{
	STEAMCONTROLLER_MAX_BUTTON_COUNT = SK_MAX_KEYS - 1,
	STEAMCONTROLLER_AXIS_BUTTON_COUNT = MAX_STEAMPADAXIS * 2
};

// The KEY_*/MOUSE_* table is gone: nothing in this tree names a button code. The type
// itself stays -- the interface virtuals below take one by value.
typedef int ButtonCode_t;

// ================ SDK/Definitions/Main/CUserCmd.h ================
class CUserCmd
{
public:
	virtual ~CUserCmd() {};
	int	command_number = 0;
	int	tick_count = 0;
	QAngle viewangles = {};
	float forwardmove = 0.0f;
	float sidemove = 0.0f;
	float upmove = 0.0f;
	int buttons = 0;
	unsigned char impulse = 0;
	int weaponselect = 0;
	int weaponsubtype = 0;
	int random_seed = 0;
	short mousedx = 0;
	short mousedy = 0;
	bool hasbeenpredicted = false;

	CUserCmd() {};

	CUserCmd(const CUserCmd& other)
	{
		command_number = other.command_number;
		tick_count = other.tick_count;
		viewangles = other.viewangles;
		forwardmove = other.forwardmove;
		sidemove = other.sidemove;
		upmove = other.upmove;
		buttons = other.buttons;
		impulse = other.impulse;
		weaponselect = other.weaponselect;
		weaponsubtype = other.weaponsubtype;
		random_seed = other.random_seed;
		mousedx = other.mousedx;
		mousedy = other.mousedy;
		hasbeenpredicted = other.hasbeenpredicted;
	}
};

#define IN_ATTACK (1 << 0)
#define IN_JUMP (1 << 1)
#define IN_DUCK (1 << 2)
#define IN_FORWARD (1 << 3)
#define IN_BACK (1 << 4)
#define IN_MOVELEFT (1 << 9)
#define IN_MOVERIGHT (1 << 10)
#define IN_ATTACK2 (1 << 11)

// ================ SDK/Definitions/Misc/IClientMode.h ================
class CViewSetup;
class CBaseEntity;
class CBasePlayer;
class Panel;
class AnimationController;
class KeyValues;

enum
{
	MM_NONE = 0,
	MM_SAY,
	MM_SAY_TEAM
};

enum HeadtrackMovementMode_t
{
	HMM_SHOOTFACE_MOVEFACE = 0,
	HMM_SHOOTFACE_MOVETORSO,
	HMM_SHOOTMOUSE_MOVEFACE,
	HMM_SHOOTBOUNDEDMOUSE_LOOKFACE_MOVEFACE,
	HMM_SHOOTBOUNDEDMOUSE_LOOKFACE_MOVEMOUSE,
	HMM_SHOOTMOVELOOKMOUSEFACE,
	HMM_SHOOTMOVEMOUSE_LOOKFACE,
	HMM_SHOOTMOVELOOKMOUSE,
	HMM_LAST,
	HMM_NOOVERRIDE = HMM_LAST
};

class IClientMode
{
public:
	virtual ~IClientMode() {}
	virtual void InitViewport() = 0;
	virtual void Init() = 0;
	virtual void VGui_Shutdown() = 0;
	virtual void Shutdown() = 0;
	virtual void Enable() = 0;
	virtual void Disable() = 0;
	virtual void Layout() = 0;
	virtual Panel* GetViewport() = 0;
	virtual AnimationController* GetViewportAnimationController() = 0;
	virtual void ProcessInput(bool bActive) = 0;
	virtual bool ShouldDrawDetailObjects() = 0;
	virtual bool ShouldDrawEntity(CBaseEntity* pEnt) = 0;
	virtual bool ShouldDrawLocalPlayer(CBasePlayer* pPlayer) = 0;
	virtual bool ShouldDrawParticles() = 0;
	virtual bool ShouldDrawFog(void) = 0;
	virtual void OverrideView(CViewSetup* pSetup) = 0;
	virtual int KeyInput(int down, ButtonCode_t keynum, const char* pszCurrentBinding) = 0;
	virtual void StartMessageMode(int iMessageModeType) = 0;
	virtual Panel* GetMessagePanel() = 0;
	virtual void OverrideMouseInput(float* x, float* y) = 0;
	virtual bool CreateMove(float flInputSampleTime, CUserCmd* cmd) = 0;
	virtual void LevelInit(const char* newmap) = 0;
	virtual void LevelShutdown(void) = 0;
	virtual bool ShouldDrawViewModel(void) = 0;
	virtual bool ShouldDrawCrosshair(void) = 0;
	virtual void AdjustEngineViewport(int& x, int& y, int& width, int& height) = 0;
	virtual void PreRender(CViewSetup* pSetup) = 0;
	virtual void PostRender(void) = 0;
	virtual void PostRenderVGui() = 0;
	virtual void ActivateInGameVGuiContext(Panel* pPanel) = 0;
	virtual void DeactivateInGameVGuiContext() = 0;
	virtual float GetViewModelFOV(void) = 0;
	virtual bool CanRecordDemo(char* errorMsg, int length) const = 0;
	virtual void ComputeVguiResConditions(KeyValues* pkvConditions) = 0;
	virtual wchar_t* GetServerName() = 0;
	virtual void SetServerName(wchar_t* name) = 0;
	virtual wchar_t* GetMapName() = 0;
	virtual void SetMapName(wchar_t* name) = 0;
	virtual bool DoPostScreenSpaceEffects(const CViewSetup* pSetup) = 0;
	virtual void DisplayReplayMessage(const char* pLocalizeName, float flDuration, bool bUrgent, const char* pSound, bool bDlg) = 0;
	virtual void Update() = 0;
	virtual bool ShouldBlackoutAroundHUD() = 0;
	virtual HeadtrackMovementMode_t ShouldOverrideHeadtrackControl() = 0;
	virtual bool IsInfoPanelAllowed() = 0;
	virtual void InfoPanelDisplayed() = 0;
	virtual bool IsHTMLInfoPanelAllowed() = 0;
	virtual void OnDemoRecordStart(char const* pDemoBaseName) = 0;
	virtual void OnDemoRecordStop() = 0;
};

// ================ SDK/Definitions/Misc/bitbuf.h ================
#pragma warning (disable : 4244)
#pragma warning (disable : 4002)
class Vec3;

#include <stdint.h>
#include <cassert>

#define nullAssert assert

#if _DEBUG
#define BITBUF_INLINE inline
#else
#define BITBUF_INLINE FORCEINLINE
#endif

#define RESTRICT

//-----------------------------------------------------------------------------
// You can define a handler function that will be called in case of 
// out-of-range values and overruns here.
//
// NOTE: the handler is only called in debug mode.
//
// Call SetBitBufErrorHandler to install a handler.
//-----------------------------------------------------------------------------

typedef enum
{
	BITBUFERROR_VALUE_OUT_OF_RANGE = 0,		// Tried to write a value with too few bits.
	BITBUFERROR_BUFFER_OVERRUN,				// Was about to overrun a buffer.

	BITBUFERROR_NUM_ERRORS
} BitBufErrorType;

typedef void(*BitBufErrorHandler)(BitBufErrorType errorType, const char* pDebugName);

#if defined( _DEBUG )
extern void InternalBitBufErrorHandler(BitBufErrorType errorType, const char* pDebugName);
#define CallErrorHandler( errorType, pDebugName ) InternalBitBufErrorHandler( errorType, pDebugName );
#else
#define CallErrorHandler( errorType, pDebugName )
#endif

// Use this to install the error handler. Call with NULL to uninstall your error handler.
void SetBitBufErrorHandler(BitBufErrorHandler fn);

#define BITS_PER_INT 32

//-----------------------------------------------------------------------------
// Helpers.
//-----------------------------------------------------------------------------

template <typename T>
inline T DWordSwapC(T dw)
{
	uint32_t temp;

	temp = *((uint32_t*)&dw) >> 24;
	temp |= ((*((uint32_t*)&dw) & 0x00FF0000) >> 8);
	temp |= ((*((uint32_t*)&dw) & 0x0000FF00) << 8);
	temp |= ((*((uint32_t*)&dw) & 0x000000FF) << 24);

	return *((T*)&temp);
}

inline int BitByte(int bits)
{
	// return PAD_NUMBER( bits, 8 ) >> 3;
	return (bits + 7) >> 3;
}

#define DWordSwap DWordSwapC

inline long LittleLong(long val) { int test = 1; return (*(char*)&test == 1) ? val : DWordSwap(val); }
inline uint32_t LittleDWord(uint32_t val) { int test = 1; return (*(char*)&test == 1) ? val : DWordSwap(val); }

// Pass floats by pointer for swapping to avoid truncation in the fpu

#define FORCEINLINE __forceinline

FORCEINLINE unsigned long LoadLittleDWord(const unsigned long* base, unsigned int dwordIndex)
{
	return LittleDWord(base[dwordIndex]);
}

FORCEINLINE void StoreLittleDWord(unsigned long* base, unsigned int dwordIndex, unsigned long dword)
{
	base[dwordIndex] = LittleDWord(dword);
}

//-----------------------------------------------------------------------------
// namespaced helpers
//-----------------------------------------------------------------------------

class bf_write
{
public:
	bf_write();
	bf_write(void* pData, int nBytes, int nMaxBits = -1);
	bf_write(const char* pDebugName, void* pData, int nBytes, int nMaxBits = -1);
	void			StartWriting(void* pData, int nBytes, int iStartBit = 0, int nMaxBits = -1);
	void			Reset();
	void			SetAssertOnOverflow(bool bAssert);
	const char* GetDebugName();
	void			SetDebugName(const char* pDebugName);
	void			WriteOneBit(int nValue);
	void			WriteOneBitNoCheck(int nValue);
	void			WriteOneBitAt(int iBit, int nValue);
	void			WriteUBitLong(unsigned int data, int numbits, bool bCheckRange = true);
	void			WriteSBitLong(int data, int numbits);
	bool			WriteBits(const void* pIn, int nBits);
	void			WriteUBitVar(unsigned int data);
	int				ByteSizeVarInt32(uint32_t data);
	int				ByteSizeVarInt64(uint64_t data);
	void			WriteBitFloat(float val);
	int				GetNumBytesWritten() const;
	int				GetNumBitsWritten() const;
	int				GetMaxNumBits();
	int				GetNumBitsLeft();
	int				GetNumBytesLeft();
	unsigned char* GetData();
	const unsigned char* GetData() const;
	bool			CheckForOverflow(int nBits);
	inline bool		IsOverflowed() const { return m_bOverflow; }
	void			SetOverflowFlag();

public:
	unsigned long* m_pData;
	int				m_nDataBytes;
	int				m_nDataBits;
	int				m_iCurBit;
	bool			m_bOverflow;
	bool			m_bAssertOnOverflow;
	const char* m_pDebugName;
};

//-----------------------------------------------------------------------------
// Inlined methods
//-----------------------------------------------------------------------------

// How many bytes are filled in?
inline int bf_write::GetNumBytesWritten() const
{
	return BitByte(m_iCurBit);
}

inline int bf_write::GetNumBitsWritten() const
{
	return m_iCurBit;
}

inline int bf_write::GetMaxNumBits()
{
	return m_nDataBits;
}

inline int bf_write::GetNumBitsLeft()
{
	return m_nDataBits - m_iCurBit;
}

inline int bf_write::GetNumBytesLeft()
{
	return GetNumBitsLeft() >> 3;
}

inline unsigned char* bf_write::GetData()
{
	return (unsigned char*)m_pData;
}

inline const unsigned char* bf_write::GetData()	const
{
	return (unsigned char*)m_pData;
}

BITBUF_INLINE bool bf_write::CheckForOverflow(int nBits)
{
	if (m_iCurBit + nBits > m_nDataBits)
	{
		SetOverflowFlag();
		CallErrorHandler(BITBUFERROR_BUFFER_OVERRUN, GetDebugName());
	}

	return m_bOverflow;
}

BITBUF_INLINE void bf_write::SetOverflowFlag()
{
	m_bOverflow = true;
}

BITBUF_INLINE void bf_write::WriteOneBitNoCheck(int nValue)
{
#if __i386__
	if (nValue)
		m_pData[m_iCurBit >> 5] |= 1u << (m_iCurBit & 31);
	else
		m_pData[m_iCurBit >> 5] &= ~(1u << (m_iCurBit & 31));
#else
	extern unsigned long g_LittleBits[32];
	if (nValue)
		m_pData[m_iCurBit >> 5] |= g_LittleBits[m_iCurBit & 31];
	else
		m_pData[m_iCurBit >> 5] &= ~g_LittleBits[m_iCurBit & 31];
#endif

	++m_iCurBit;
}

inline void bf_write::WriteOneBit(int nValue)
{
	if (m_iCurBit >= m_nDataBits)
	{
		SetOverflowFlag();
		CallErrorHandler(BITBUFERROR_BUFFER_OVERRUN, GetDebugName());
		return;
	}
	WriteOneBitNoCheck(nValue);
}

inline void	bf_write::WriteOneBitAt(int iBit, int nValue)
{
	if (iBit >= m_nDataBits)
	{
		SetOverflowFlag();
		CallErrorHandler(BITBUFERROR_BUFFER_OVERRUN, GetDebugName());
		return;
	}

#if __i386__
	if (nValue)
		m_pData[iBit >> 5] |= 1u << (iBit & 31);
	else
		m_pData[iBit >> 5] &= ~(1u << (iBit & 31));
#else
	extern unsigned long g_LittleBits[32];
	if (nValue)
		m_pData[iBit >> 5] |= g_LittleBits[iBit & 31];
	else
		m_pData[iBit >> 5] &= ~g_LittleBits[iBit & 31];
#endif
}

BITBUF_INLINE void bf_write::WriteUBitLong(unsigned int curData, int numbits, bool bCheckRange) RESTRICT
{
#ifdef _DEBUG
	// Make sure it doesn't overflow.
	if (bCheckRange && numbits < 32)
	{
		if (curData >= (unsigned long)(1 << numbits))
		{
			CallErrorHandler(BITBUFERROR_VALUE_OUT_OF_RANGE, GetDebugName());
		}
	}
	nullAssert(numbits >= 0 && numbits <= 32);
#endif

	if (GetNumBitsLeft() < numbits)
	{
		m_iCurBit = m_nDataBits;
		SetOverflowFlag();
		CallErrorHandler(BITBUFERROR_BUFFER_OVERRUN, GetDebugName());
		return;
	}

	int iCurBitMasked = m_iCurBit & 31;
	int iDWord = m_iCurBit >> 5;
	m_iCurBit += numbits;

	// Mask in a dword.
	nullAssert((iDWord * 4 + sizeof(long)) <= (unsigned int)m_nDataBytes);
	unsigned long* RESTRICT pOut = &m_pData[iDWord];

	// Rotate data into dword alignment
	curData = (curData << iCurBitMasked) | (curData >> (32 - iCurBitMasked));

	// Calculate bitmasks for first and second word
	unsigned int temp = 1 << (numbits - 1);
	unsigned int mask1 = (temp * 2 - 1) << iCurBitMasked;
	unsigned int mask2 = (temp - 1) >> (31 - iCurBitMasked);

	// Only look beyond current word if necessary (avoid access violation)
	int i = mask2 & 1;
	unsigned long dword1 = LoadLittleDWord(pOut, 0);
	unsigned long dword2 = LoadLittleDWord(pOut, i);

	// Drop bits into place
	dword1 ^= (mask1 & (curData ^ dword1));
	dword2 ^= (mask2 & (curData ^ dword2));

	// Note reversed order of writes so that dword1 wins if mask2 == 0 && i == 0
	StoreLittleDWord(pOut, i, dword2);
	StoreLittleDWord(pOut, 0, dword1);
}

// writes an unsigned integer with variable bit length
BITBUF_INLINE void bf_write::WriteUBitVar(unsigned int data)
{
	/* Reference:
	if ( data < 0x10u )
		WriteUBitLong( 0, 2 ), WriteUBitLong( data, 4 );
	else if ( data < 0x100u )
		WriteUBitLong( 1, 2 ), WriteUBitLong( data, 8 );
	else if ( data < 0x1000u )
		WriteUBitLong( 2, 2 ), WriteUBitLong( data, 12 );
	else
		WriteUBitLong( 3, 2 ), WriteUBitLong( data, 32 );
	*/
	// a < b ? -1 : 0 translates into a CMP, SBB instruction pair
	// with no flow control. should also be branchless on consoles.
	int n = (data < 0x10u ? -1 : 0) + (data < 0x100u ? -1 : 0) + (data < 0x1000u ? -1 : 0);
	WriteUBitLong(data * 4 + n + 3, 6 + n * 4 + 12);
	if (data >= 0x1000u)
	{
		WriteUBitLong(data >> 16, 16);
	}
}

// write raw IEEE float bits in little endian form
BITBUF_INLINE void bf_write::WriteBitFloat(float val)
{
	long intVal;

	nullAssert(sizeof(long) == sizeof(float));
	nullAssert(sizeof(float) == 4);

	intVal = *((long*)&val);
	WriteUBitLong(intVal, 32);
}

//-----------------------------------------------------------------------------
// This is useful if you just want a buffer to write into on the stack.
//-----------------------------------------------------------------------------

class bf_read
{
public:
	bf_read();
	bf_read(const void* pData, int nBytes, int nBits = -1);
	bf_read(const char* pDebugName, const void* pData, int nBytes, int nBits = -1);
	void			StartReading(const void* pData, int nBytes, int iStartBit = 0, int nBits = -1);
	void			Reset();
	void			SetAssertOnOverflow(bool bAssert);
	const char* GetDebugName() const { return m_pDebugName; }
	void			SetDebugName(const char* pName);
	int				ReadOneBit();
protected:
	int				ReadOneBitNoCheck();				// Faster version, doesn't check bounds and is inlined.
	bool			CheckForOverflow(int nBits);
public:
	void            ReadBits(void* pOut, int nBits);
	int             ReadBitsClamped_ptr(void* pOut, size_t outSizeBytes, size_t nBits);
	unsigned int	ReadUBitLong(int numbits) RESTRICT;
	unsigned int	ReadUBitVar();
	unsigned int	ReadUBitVarInternal(int encodingType);
	float			ReadBitFloat();
	BITBUF_INLINE int	ReadChar() { return (char)ReadUBitLong(8); }
	bool			ReadString(char* pStr, int bufLen, bool bLine = false, int* pOutNumChars = NULL);
	char* ReadAndAllocateString(bool* pOverflow = 0);
	int				CompareBits(bf_read* RESTRICT other, int bits) RESTRICT;
	int				GetNumBytesLeft();
	int				GetNumBytesRead();
	int				GetNumBitsLeft();
	int				GetNumBitsRead() const;
	inline bool		IsOverflowed() const { return m_bOverflow; }
	inline bool		Seek(int iBit);					// Seek to a specific bit.
	inline bool		SeekRelative(int iBitDelta);	// Seek to an offset from the current position.
	void			SetOverflowFlag();
public:
	unsigned long* m_pData;
	int					m_nDataBytes;
	int					m_nDataBits;
	int					m_iCurBit;
	bool				m_bOverflow;
	bool				m_bAssertOnOverflow;
	const char* m_pDebugName;
};

//-----------------------------------------------------------------------------
// Inlines.
//-----------------------------------------------------------------------------

inline int bf_read::GetNumBytesRead()
{
	return BitByte(m_iCurBit);
}

inline int bf_read::GetNumBitsLeft()
{
	return m_nDataBits - m_iCurBit;
}

inline int bf_read::GetNumBytesLeft()
{
	return GetNumBitsLeft() >> 3;
}

inline int bf_read::GetNumBitsRead() const
{
	return m_iCurBit;
}

inline bool bf_read::Seek(int iBit)
{
	if (iBit < 0 || iBit > m_nDataBits)
	{
		SetOverflowFlag();
		m_iCurBit = m_nDataBits;
		return false;
	}
	else
	{
		m_iCurBit = iBit;
		return true;
	}
}

// Seek to an offset from the current position.
inline bool	bf_read::SeekRelative(int iBitDelta)
{
	return Seek(m_iCurBit + iBitDelta);
}

inline bool bf_read::CheckForOverflow(int nBits)
{
	if (m_iCurBit + nBits > m_nDataBits)
	{
		SetOverflowFlag();
		CallErrorHandler(BITBUFERROR_BUFFER_OVERRUN, GetDebugName());
	}

	return m_bOverflow;
}

inline int bf_read::ReadOneBitNoCheck()
{
#if VALVE_LITTLE_ENDIAN
	unsigned int value = ((unsigned long* RESTRICT)m_pData)[m_iCurBit >> 5] >> (m_iCurBit & 31);
#else
	unsigned char value = m_pData[m_iCurBit >> 3] >> (m_iCurBit & 7);
#endif
	++m_iCurBit;
	return value & 1;
}

inline int bf_read::ReadOneBit()
{
	if (GetNumBitsLeft() <= 0)
	{
		SetOverflowFlag();
		CallErrorHandler(BITBUFERROR_BUFFER_OVERRUN, GetDebugName());
		return 0;
	}
	return ReadOneBitNoCheck();
}

inline float bf_read::ReadBitFloat()
{
	union { uint32_t u; float f; } c = { ReadUBitLong(32) };
	return c.f;
}

BITBUF_INLINE unsigned int bf_read::ReadUBitVar()
{
	// six bits: low 2 bits for encoding + first 4 bits of value
	unsigned int sixbits = ReadUBitLong(6);
	unsigned int encoding = sixbits & 3;
	if (encoding)
	{
		// this function will seek back four bits and read the full value
		return ReadUBitVarInternal(encoding);
	}
	return sixbits >> 2;
}

BITBUF_INLINE unsigned int bf_read::ReadUBitLong(int numbits) RESTRICT
{
	nullAssert(numbits > 0 && numbits <= 32);

	if (GetNumBitsLeft() < numbits)
	{
		m_iCurBit = m_nDataBits;
		SetOverflowFlag();
		CallErrorHandler(BITBUFERROR_BUFFER_OVERRUN, GetDebugName());
		return 0;
	}

	unsigned int iStartBit = m_iCurBit & 31u;
	int iLastBit = m_iCurBit + numbits - 1;
	unsigned int iWordOffset1 = m_iCurBit >> 5;
	unsigned int iWordOffset2 = iLastBit >> 5;
	m_iCurBit += numbits;

#if __i386__
	unsigned int bitmask = (2 << (numbits - 1)) - 1;
#else
	extern unsigned long g_ExtraMasks[33];
	unsigned int bitmask = g_ExtraMasks[numbits];
#endif

	unsigned int dw1 = LoadLittleDWord((unsigned long* RESTRICT)m_pData, iWordOffset1) >> iStartBit;
	unsigned int dw2 = LoadLittleDWord((unsigned long* RESTRICT)m_pData, iWordOffset2) << (32 - iStartBit);

	return (dw1 | dw2) & bitmask;
}

BITBUF_INLINE int bf_read::CompareBits(bf_read* RESTRICT other, int numbits) RESTRICT
{
	return (ReadUBitLong(numbits) != other->ReadUBitLong(numbits));
}

// ================ SDK/Definitions/Interfaces/IGameEvents.h ================

class KeyValues;

class IGameEvent
{
public:
	virtual ~IGameEvent() {};
	virtual const char* GetName() const = 0;
	virtual bool IsReliable() const = 0;
	virtual bool IsLocal() const = 0;
	virtual bool IsEmpty(const char* keyName = nullptr) = 0;
	virtual bool GetBool(const char* keyName = nullptr, bool defaultValue = false) = 0;
	virtual int GetInt(const char* keyName = nullptr, int defaultValue = 0) = 0;
	virtual float GetFloat(const char* keyName = nullptr, float defaultValue = 0.0f) = 0;
	virtual const char* GetString(const char* keyName = nullptr, const char* defaultValue = "") = 0;
	virtual void SetBool(const char* keyName, bool value) = 0;
	virtual void SetInt(const char* keyName, int value) = 0;
	virtual void SetFloat(const char* keyName, float value) = 0;
	virtual void SetString(const char* keyName, const char* value) = 0;
};

class IGameEventListener2
{
public:
	virtual	~IGameEventListener2(void) {};
	virtual void FireGameEvent(IGameEvent* event) = 0;
};

class IGameEventManager2 : public IBaseInterface
{
public:
	virtual	~IGameEventManager2(void) {};
	virtual int LoadEventsFromFile(const char* filename) = 0;
	virtual void Reset() = 0;
	virtual bool AddListener(IGameEventListener2* listener, const char* name, bool bServerSide) = 0;
	virtual bool FindListener(IGameEventListener2* listener, const char* name) = 0;
	virtual void RemoveListener(IGameEventListener2* listener) = 0;
	virtual IGameEvent* CreateEvent(const char* name, bool bForce = false) = 0;
	virtual bool FireEvent(IGameEvent* event, bool bDontBroadcast = false) = 0;
	virtual bool FireEventClientSide(IGameEvent* event) = 0;
	virtual IGameEvent* DuplicateEvent(IGameEvent* event) = 0;
	virtual void FreeEvent(IGameEvent* event) = 0;
	virtual bool SerializeEvent(IGameEvent* event, bf_write* buf) = 0;
	virtual IGameEvent* UnserializeEvent(bf_read* buf) = 0;
};

MAKE_INTERFACE_VERSION(IGameEventManager2, GameEventManager, "engine.dll", "GAMEEVENTSMANAGER002");

// ================ SDK/Definitions/Misc/CGameEventListener.h ================
class CGameEventListener : public IGameEventListener2
{
public:
	virtual void FireGameEvent(IGameEvent* event) = 0;

private:
	bool m_bRegisteredForEvents;
};

// ================ SDK/Definitions/Interfaces/CClientModeShared.h ================

class CBaseEntity;
class CBasePlayer;
class CViewSetup;

class CBaseHudChat
{
public:
	VIRTUAL_ARGS(ChatPrintf, void, 19, (int pIndex, const char* fmt, ...), this, pIndex, 0, fmt);
	VIRTUAL_ARGS(StartMessageMode, void, 20, (int iMessageModeType), this, iMessageModeType);

};

class CClientModeShared : public IClientMode, public CGameEventListener
{
public:
	virtual ~CClientModeShared() {};
	virtual void Init() = 0;
	virtual void InitViewport() = 0;
	virtual void VGui_Shutdown() = 0;
	virtual void Shutdown() = 0;
	virtual void LevelInit(const char* newmap) = 0;
	virtual void LevelShutdown(void) = 0;
	virtual void Enable() = 0;
	virtual void Disable() = 0;
	virtual void Layout() = 0;
	virtual void ReloadScheme(bool flushLowLevel) = 0;
	virtual void OverrideView(CViewSetup* pSetup) = 0;
	virtual bool ShouldDrawDetailObjects() = 0;
	virtual bool ShouldDrawEntity(CBaseEntity* pEnt) = 0;
	virtual bool ShouldDrawLocalPlayer(CBasePlayer* pPlayer) = 0;
	virtual bool ShouldDrawViewModel() = 0;
	virtual bool ShouldDrawParticles() = 0;
	virtual bool ShouldDrawCrosshair(void) = 0;
	virtual bool ShouldBlackoutAroundHUD() override = 0;
	virtual HeadtrackMovementMode_t ShouldOverrideHeadtrackControl() override = 0;
	virtual void AdjustEngineViewport(int& x, int& y, int& width, int& height) = 0;
	virtual void PreRender(CViewSetup* pSetup) = 0;
	virtual void PostRender() = 0;
	virtual void PostRenderVGui() = 0;
	virtual void ProcessInput(bool bActive) = 0;
	virtual bool CreateMove(float flInputSampleTime, CUserCmd* cmd) = 0;
	virtual void Update() = 0;
	virtual int KeyInput(int down, ButtonCode_t keynum, const char* pszCurrentBinding) = 0;
	virtual int HudElementKeyInput(int down, ButtonCode_t keynum, const char* pszCurrentBinding) = 0;
	virtual void OverrideMouseInput(float* x, float* y) = 0;
	virtual void StartMessageMode(int iMessageModeType) = 0;
	virtual Panel* GetMessagePanel() = 0;
	virtual void ActivateInGameVGuiContext(Panel* pPanel) = 0;
	virtual void DeactivateInGameVGuiContext() = 0;
	virtual bool ShouldDrawFog(void) = 0;
	virtual float GetViewModelFOV(void) = 0;
	virtual Panel* GetViewport() {}
	virtual AnimationController* GetViewportAnimationController() {}
	virtual void FireGameEvent(IGameEvent* event) = 0;
	virtual bool CanRecordDemo(char* errorMsg, int length) const { return true; }
	virtual int HandleSpectatorKeyInput(int down, ButtonCode_t keynum, const char* pszCurrentBinding) = 0;
	virtual void ComputeVguiResConditions(KeyValues* pkvConditions) override = 0;
	virtual wchar_t* GetServerName() { return nullptr; }
	virtual void SetServerName(wchar_t* name) {}
	virtual wchar_t* GetMapName() { return nullptr; }
	virtual void SetMapName(wchar_t* name) {}
	virtual bool DoPostScreenSpaceEffects(const CViewSetup* pSetup) = 0;
	virtual void DisplayReplayMessage(const char* pLocalizeName, float flDuration, bool bUrgent, const char* pSound, bool bDlg) = 0;
	virtual bool IsInfoPanelAllowed() override { return true; }
	virtual void InfoPanelDisplayed() override {}
	virtual bool IsHTMLInfoPanelAllowed() override { return true; }
	virtual void OnDemoRecordStart(char const* pDemoBaseName) override {}
	virtual void OnDemoRecordStop() override {}

	// CSS has no REPLAY_ENABLED block (TF2 pad 24 -> offset 48 = CSS m_pWeaponSelection: "chat
	// print" called weapon selection slot 19 = weapon switch). Pad = m_pViewport only. CSS x64
	// ClientModeShared::StartMessageMode (vtable 18) reads chat at [rcx+0x20].
	char szPad[8];
	CBaseHudChat* m_pChatElement;

};
static_assert(offsetof(CClientModeShared, m_pChatElement) == 0x20, "CSS m_pChatElement");

// CSS x64 GetClientModeNormal(): `lea rbx, g_ClientModeNormal` (static object, no deref). The
// TF2 pattern hit `mov rcx, g_pHaptics` in CSS.
MAKE_INTERFACE_SIGNATURE(CClientModeShared, ClientModeShared, "client.dll", "48 8D 1D ? ? ? ? 48 8B CB 89 05 ? ? ? ? E8 ? ? ? ? 48 8B 0D ? ? ? ? 48 8D 05", 0x0, 0);

// ================ SDK/Definitions/Main/UtlVector.h ================
#include <cassert>
#include <stdlib.h>

#pragma warning (push)
#pragma warning (disable : 4100)
#pragma warning (disable : 4514)
#pragma warning (disable : 26495)

template <class T>
inline void Construct(T* pMemory) {
	::new(pMemory) T;
}

template <class T>
inline void CopyConstruct(T* pMemory, T const& src) {
	::new(pMemory) T(src);
}

template <class T>
inline void Destruct(T* pMemory) {
	pMemory->~T();
}

//-----------------------------------------------------------------------------
// The CUtlMemory class:
// A growable memory class which doubles in size by default.
//-----------------------------------------------------------------------------
template< class T >
class CUtlMemory {
public:
	// constructor, destructor
	CUtlMemory(int nGrowSize = 0, int nInitSize = 0);
	CUtlMemory(T* pMemory, int numElements);
	~CUtlMemory();

	// element access
	T& operator[](int i);
	T const& operator[](int i) const;
	T& Element(int i);
	T const& Element(int i) const;

	// Can we use this index?
	bool IsIdxValid(int i) const;

	// Gets the base address (can change when adding elements!)
	T* Base();
	T const* Base() const;

	// Attaches the buffer to external memory....
	void SetExternalBuffer(T* pMemory, int numElements);

	// Size
	int NumAllocated() const;
	int Count() const;

	// Grows the memory, so that at least allocated + num elements are allocated
	void Grow(int num = 1);

	// Memory deallocation
	void Purge();

	// is the memory externally allocated?
	bool IsExternallyAllocated() const;

	// Set the size by which the memory grows
	void SetGrowSize(int size);

private:
	enum {
		EXTERNAL_BUFFER_MARKER = -1,
	};

	T* m_pMemory;
	int m_nAllocationCount;
	int m_nGrowSize;
};

//-----------------------------------------------------------------------------
// constructor, destructor
//-----------------------------------------------------------------------------
template< class T >
CUtlMemory<T>::CUtlMemory(int nGrowSize, int nInitAllocationCount) : m_pMemory(0),
m_nAllocationCount(nInitAllocationCount), m_nGrowSize(nGrowSize) {
	//Assert((nGrowSize >= 0) && (nGrowSize != EXTERNAL_BUFFER_MARKER));
	if (m_nAllocationCount) {
		m_pMemory = (T*)malloc(m_nAllocationCount * sizeof(T));
	}
}

template< class T >
CUtlMemory<T>::CUtlMemory(T* pMemory, int numElements) : m_pMemory(pMemory),
m_nAllocationCount(numElements) {
	// Special marker indicating externally supplied memory
	m_nGrowSize = EXTERNAL_BUFFER_MARKER;
}

template< class T >
CUtlMemory<T>::~CUtlMemory() {
	Purge();
}

//-----------------------------------------------------------------------------
// Attaches the buffer to external memory....
//-----------------------------------------------------------------------------
template< class T >
void CUtlMemory<T>::SetExternalBuffer(T* pMemory, int numElements) {
	// Blow away any existing allocated memory
	Purge();

	m_pMemory = pMemory;
	m_nAllocationCount = numElements;

	// Indicate that we don't own the memory
	m_nGrowSize = EXTERNAL_BUFFER_MARKER;
}

//-----------------------------------------------------------------------------
// element access
//-----------------------------------------------------------------------------
template< class T >
inline T& CUtlMemory<T>::operator[](int i) {
	//Assert(IsIdxValid(i));
	return m_pMemory[i];
}

template< class T >
inline T const& CUtlMemory<T>::operator[](int i) const {
	//Assert(IsIdxValid(i));
	return m_pMemory[i];
}

template< class T >
inline T& CUtlMemory<T>::Element(int i) {
	//Assert(IsIdxValid(i));
	return m_pMemory[i];
}

template< class T >
inline T const& CUtlMemory<T>::Element(int i) const {
	//Assert(IsIdxValid(i));
	return m_pMemory[i];
}

//-----------------------------------------------------------------------------
// is the memory externally allocated?
//-----------------------------------------------------------------------------
template< class T >
bool CUtlMemory<T>::IsExternallyAllocated() const {
	return m_nGrowSize == EXTERNAL_BUFFER_MARKER;
}

template< class T >
void CUtlMemory<T>::SetGrowSize(int nSize) {
	//Assert((nSize >= 0) && (nSize != EXTERNAL_BUFFER_MARKER));
	m_nGrowSize = nSize;
}

//-----------------------------------------------------------------------------
// Gets the base address (can change when adding elements!)
//-----------------------------------------------------------------------------
template< class T >
inline T* CUtlMemory<T>::Base() {
	return m_pMemory;
}

template< class T >
inline T const* CUtlMemory<T>::Base() const {
	return m_pMemory;
}

//-----------------------------------------------------------------------------
// Size
//-----------------------------------------------------------------------------
template< class T >
inline int CUtlMemory<T>::NumAllocated() const {
	return m_nAllocationCount;
}

template< class T >
inline int CUtlMemory<T>::Count() const {
	return m_nAllocationCount;
}

//-----------------------------------------------------------------------------
// Is element index valid?
//-----------------------------------------------------------------------------
template< class T >
inline bool CUtlMemory<T>::IsIdxValid(int i) const {
	return (i >= 0) && (i < m_nAllocationCount);
}

#pragma warning (push)
#pragma warning (disable : 6308)

//-----------------------------------------------------------------------------
// Grows the memory
//-----------------------------------------------------------------------------
template< class T >
void CUtlMemory<T>::Grow(int num) {
	assert(num > 0);

	if (IsExternallyAllocated()) {
		// Can't grow a buffer whose memory was externally allocated 
		assert(0);
		return;
	}

	// Make sure we have at least numallocated + num allocations.
	// Use the grow rules specified for this memory (in m_nGrowSize)
	int nAllocationRequested = m_nAllocationCount + num;
	while (m_nAllocationCount < nAllocationRequested) {
		if (m_nAllocationCount != 0) {
			if (m_nGrowSize) {
				m_nAllocationCount += m_nGrowSize;
			}
			else {
				m_nAllocationCount += m_nAllocationCount;
			}
		}
		else {
			// Compute an allocation which is at least as big as a cache line...
			m_nAllocationCount = (31 + sizeof(T)) / sizeof(T);
			assert(m_nAllocationCount != 0);
		}
	}

	if (m_pMemory) {
		m_pMemory = (T*)realloc(m_pMemory, m_nAllocationCount * sizeof(T));
	}
	else {
		m_pMemory = (T*)malloc(m_nAllocationCount * sizeof(T));
	}
}

#pragma warning (pop)

//-----------------------------------------------------------------------------
// Memory deallocation
//-----------------------------------------------------------------------------
template< class T >
void CUtlMemory<T>::Purge() {
	if (!IsExternallyAllocated()) {
		if (m_pMemory) {
			free((void*)m_pMemory);
			m_pMemory = 0;
		}
		m_nAllocationCount = 0;
	}
}

template<class T>
class CUtlVector {
public:
	typedef T ElemType_t;

	// constructor, destructor
	CUtlVector(int growSize = 0, int initSize = 0);
	CUtlVector(T* pMemory, int numElements);
	~CUtlVector();

	// Copy the array.
	CUtlVector<T>& operator=(const CUtlVector<T>& other);

	// element access
	T& operator[](int i);
	T const& operator[](int i) const;
	T& Element(int i);
	T const& Element(int i) const;

	// Gets the base address (can change when adding elements!)
	T* Base();
	T const* Base() const;

	// Returns the number of elements in the vector
	// SIZE IS DEPRECATED!
	int Count() const;
	int Size() const;	// don't use me!

	// Is element index valid?
	bool IsValidIndex(int i) const;

	int AddToTail();
	int InsertBefore(int elem);

	int AddToTail(T const& src);
	int InsertBefore(int elem, T const& src);

	int AddMultipleToTail(int num, const T* pToCopy = NULL);
	int InsertMultipleBefore(int elem, int num, const T* pToCopy = NULL);	// If pToCopy is set, then it's an array of length 'num' and
	int InsertMultipleAfter(int elem, int num);

	// Calls RemoveAll() then AddMultipleToTail.
	void SetSize(int size);
	void SetCount(int count);

	// Calls SetSize and copies each element.
	void CopyArray(T const* pArray, int size);

	// Add the specified array to the tail.
	int AddVectorToTail(CUtlVector<T> const& src);

	// Finds an element (element needs operator== defined)
	int Find(T const& src) const;

	bool HasElement(T const& src);

	// Makes sure we have at least this many elements
	void EnsureCount(int num);

	// Element removal
	void FastRemove(int elem);	// doesn't preserve order
	void Remove(int elem);		// preserves order, shifts elements
	void RemoveAll();				// doesn't deallocate memory

	// Memory deallocation
	void Purge();

	// Purges the list and calls delete on each element in it.
	void PurgeAndDeleteElements();

	// Set the size by which it grows when it needs to allocate more memory.
	void SetGrowSize(int size);

	// Can't copy this unless we explicitly do it!
	CUtlVector(CUtlVector const& vec) {
		assert(0);
	}

protected:
	// Grows the vector
	void GrowVector(int num = 1);

	// Shifts elements....
	void ShiftElementsRight(int elem, int num = 1);
	void ShiftElementsLeft(int elem, int num = 1);

	// For easier access to the elements through the debugger
	void ResetDbgInfo();

	CUtlMemory<T> m_Memory;
	int m_Size;

	// For easier access to the elements through the debugger
	// it's in release builds so this can be used in libraries correctly
	T* m_pElements;
};

//-----------------------------------------------------------------------------
// For easier access to the elements through the debugger
//-----------------------------------------------------------------------------

template< class T >
inline void CUtlVector<T>::ResetDbgInfo() {
	m_pElements = m_Memory.Base();
}

//-----------------------------------------------------------------------------
// constructor, destructor
//-----------------------------------------------------------------------------

template< class T >
inline CUtlVector<T>::CUtlVector(int growSize, int initSize) :
	m_Memory(growSize, initSize), m_Size(0) {
	ResetDbgInfo();
}

template< class T >
inline CUtlVector<T>::CUtlVector(T* pMemory, int numElements) :
	m_Memory(pMemory, numElements), m_Size(0) {
	ResetDbgInfo();
}

template< class T >
inline CUtlVector<T>::~CUtlVector() {
	Purge();
}

template<class T>
inline CUtlVector<T>& CUtlVector<T>::operator=(const CUtlVector<T>& other) {
	CopyArray(other.Base(), other.Count());
	return *this;
}

//-----------------------------------------------------------------------------
// element access
//-----------------------------------------------------------------------------

template< class T >
inline T& CUtlVector<T>::operator[](int i) {
	assert(IsValidIndex(i));
	return m_Memory[i];
}

template< class T >
inline T const& CUtlVector<T>::operator[](int i) const {
	assert(IsValidIndex(i));
	return m_Memory[i];
}

template< class T >
inline T& CUtlVector<T>::Element(int i) {
	assert(IsValidIndex(i));
	return m_Memory[i];
}

template< class T >
inline T const& CUtlVector<T>::Element(int i) const {
	assert(IsValidIndex(i));
	return m_Memory[i];
}

//-----------------------------------------------------------------------------
// Gets the base address (can change when adding elements!)
//-----------------------------------------------------------------------------

template< class T >
inline T* CUtlVector<T>::Base() {
	return m_Memory.Base();
}

template< class T >
inline T const* CUtlVector<T>::Base() const {
	return m_Memory.Base();
}

//-----------------------------------------------------------------------------
// Count
//-----------------------------------------------------------------------------

template< class T >
inline int CUtlVector<T>::Size() const {
	return m_Size;
}

template< class T >
inline int CUtlVector<T>::Count() const {
	return m_Size;
}

//-----------------------------------------------------------------------------
// Is element index valid?
//-----------------------------------------------------------------------------

template< class T >
inline bool CUtlVector<T>::IsValidIndex(int i) const {
	return (i >= 0) && (i < m_Size);
}

//-----------------------------------------------------------------------------
// Returns in invalid index
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Grows the vector
//-----------------------------------------------------------------------------
template< class T >
void CUtlVector<T>::GrowVector(int num) {
	if (m_Size + num - 1 >= m_Memory.NumAllocated()) {
		m_Memory.Grow(m_Size + num - m_Memory.NumAllocated());
	}

	m_Size += num;
	ResetDbgInfo();
}

//-----------------------------------------------------------------------------
// Makes sure we have at least this many elements
//-----------------------------------------------------------------------------
template< class T >
void CUtlVector<T>::EnsureCount(int num) {
	if (Count() < num)
		AddMultipleToTail(num - Count());
}

//-----------------------------------------------------------------------------
// Shifts elements
//-----------------------------------------------------------------------------
template< class T >
void CUtlVector<T>::ShiftElementsRight(int elem, int num) {
	assert(IsValidIndex(elem) || (m_Size == 0) || (num == 0));
	int numToMove = m_Size - elem - num;
	if ((numToMove > 0) && (num > 0))
		memmove(&Element(elem + num), &Element(elem), numToMove * sizeof(T));
}

template< class T >
void CUtlVector<T>::ShiftElementsLeft(int elem, int num) {
	assert(IsValidIndex(elem) || (m_Size == 0) || (num == 0));
	int numToMove = m_Size - elem - num;
	if ((numToMove > 0) && (num > 0)) {
		memmove(&Element(elem), &Element(elem + num), numToMove * sizeof(T));

#ifdef _DEBUG
		memset(&Element(m_Size - num), 0xDD, num * sizeof(T));
#endif
	}
}

//-----------------------------------------------------------------------------
// Adds an element, uses default constructor
//-----------------------------------------------------------------------------

template< class T >
inline int CUtlVector<T>::AddToTail() {
	return InsertBefore(m_Size);
}

template< class T >
int CUtlVector<T>::InsertBefore(int elem) {
	// Can insert at the end
	assert((elem == Count()) || IsValidIndex(elem));

	GrowVector();
	ShiftElementsRight(elem);
	Construct(&Element(elem));
	return elem;
}

//-----------------------------------------------------------------------------
// Adds an element, uses copy constructor
//-----------------------------------------------------------------------------

template< class T >
inline int CUtlVector<T>::AddToTail(T const& src) {
	return InsertBefore(m_Size, src);
}

template< class T >
int CUtlVector<T>::InsertBefore(int elem, T const& src) {
	// Can insert at the end
	assert((elem == Count()) || IsValidIndex(elem));

	GrowVector();
	ShiftElementsRight(elem);
	CopyConstruct(&Element(elem), src);
	return elem;
}

//-----------------------------------------------------------------------------
// Adds multiple elements, uses default constructor
//-----------------------------------------------------------------------------

template< class T >
inline int CUtlVector<T>::AddMultipleToTail(int num, const T* pToCopy) {
	return InsertMultipleBefore(m_Size, num, pToCopy);
}

template< class T >
int CUtlVector<T>::InsertMultipleAfter(int elem, int num) {
	return InsertMultipleBefore(elem + 1, num);
}

template< class T >
void CUtlVector<T>::SetCount(int count) {
	RemoveAll();
	AddMultipleToTail(count);
}

template< class T >
inline void CUtlVector<T>::SetSize(int size) {
	SetCount(size);
}

template< class T >
void CUtlVector<T>::CopyArray(T const* pArray, int size) {
	SetSize(size);
	for (int i = 0; i < size; i++)
		(*this)[i] = pArray[i];
}

template< class T >
int CUtlVector<T>::AddVectorToTail(CUtlVector const& src) {
	int base = Count();

	// Make space.
	AddMultipleToTail(src.Count());

	// Copy the elements.
	for (int i = 0; i < src.Count(); i++)
		(*this)[base + i] = src[i];

	return base;
}

template< class T >
inline int CUtlVector<T>::InsertMultipleBefore(int elem, int num, const T* pToInsert) {
	if (num == 0)
		return elem;

	// Can insert at the end
	assert((elem == Count()) || IsValidIndex(elem));

	GrowVector(num);
	ShiftElementsRight(elem, num);

	// Invoke default constructors
	for (int i = 0; i < num; ++i)
		Construct(&Element(elem + i));

	// Copy stuff in?
	if (pToInsert) {
		for (int i = 0; i < num; i++) {
			Element(elem + i) = pToInsert[i];
		}
	}

	return elem;
}

//-----------------------------------------------------------------------------
// Finds an element (element needs operator== defined)
//-----------------------------------------------------------------------------
template< class T >
int CUtlVector<T>::Find(T const& src) const {
	for (int i = 0; i < Count(); ++i) {
		if (Element(i) == src)
			return i;
	}
	return -1;
}

template< class T >
bool CUtlVector<T>::HasElement(T const& src) {
	return (Find(src) >= 0);
}

//-----------------------------------------------------------------------------
// Element removal
//-----------------------------------------------------------------------------

template< class T >
void CUtlVector<T>::FastRemove(int elem) {
	assert(IsValidIndex(elem));

	Destruct(&Element(elem));
	if (m_Size > 0) {
		Q_memcpy(&Element(elem), &Element(m_Size - 1), sizeof(T));
		--m_Size;
	}
}

template< class T >
void CUtlVector<T>::Remove(int elem) {
	Destruct(&Element(elem));
	ShiftElementsLeft(elem);
	--m_Size;
}

template< class T >
void CUtlVector<T>::RemoveAll() {
	for (int i = m_Size; --i >= 0;)
		Destruct(&Element(i));

	m_Size = 0;
}

//-----------------------------------------------------------------------------
// Memory deallocation
//-----------------------------------------------------------------------------

template< class T >
void CUtlVector<T>::Purge() {
	RemoveAll();
	m_Memory.Purge();
	ResetDbgInfo();
}

template<class T>
inline void CUtlVector<T>::PurgeAndDeleteElements() {
	for (int i = 0; i < m_Size; i++)
		delete Element(i);

	Purge();
}

template< class T >
void CUtlVector<T>::SetGrowSize(int size) {
	m_Memory.SetGrowSize(size);
}

#pragma warning (pop)

// ================ SDK/Definitions/Main/INetMessage.h ================
#define	MAX_OSPATH 260 // max length of a filesystem pathname

// shared commands used by all streams, handled by stream layer
// server to client
//#define svc_HLTV				16	// HLTV control messages
//#define	svc_TerrainMod		22	// modification to the terrain/displacement. NOTE: This is now unused!
// Message from server side to client side entity
// client to server
// matchmaking

class INetChannel;

enum
{
	GENERIC = 0,	// must be first and is default group
	LOCALPLAYER,	// bytes for local player entity update
	OTHERPLAYERS,	// bytes for other players update
	ENTITIES,		// all other entity bytes
	SOUNDS,			// game sounds
	EVENTS,			// event messages
	USERMESSAGES,	// user messages
	ENTMESSAGES,	// entity messages
	VOICE,			// voice data
	STRINGTABLE,	// a stringtable update
	MOVE,			// client move cmds
	STRINGCMD,		// string command
	SIGNON,			// various signondata
	TOTAL,			// must be last and is not a real group
};

class INetMessage
{
public:
	virtual ~INetMessage() {};
	virtual void SetNetChannel(INetChannel* netchan) = 0; // netchannel this message is from/for
	virtual void SetReliable(bool state) = 0;             // set to true if it's a reliable message
	virtual bool Process(void) = 0; // calles the recently set handler to process this message
	virtual bool ReadFromBuffer(bf_read& buffer) = 0; // returns true if parsing was OK
	virtual bool WriteToBuffer(bf_write& buffer) = 0; // returns true if writing was OK
	virtual bool IsReliable(void) const = 0; // true, if message needs reliable handling
	virtual int GetType(void) const = 0;         // returns module specific header tag eg svc_serverinfo
	virtual int GetGroup(void) const = 0;        // returns net message group of this message
	virtual const char* GetName(void) const = 0; // returns network message name, eg "svc_serverinfo"
	virtual INetChannel* GetNetChannel(void) const = 0;
	virtual const char* ToString(void) const = 0; // returns a human readable string about message content
};

class CNetMessage : public INetMessage
{
public:
	CNetMessage()
	{
		m_bReliable = true;
		m_NetChannel = nullptr;
	}

	virtual ~CNetMessage() {};

	virtual int GetGroup() const { return GENERIC; }
	INetChannel* GetNetChannel() const { return m_NetChannel; }

	virtual void SetReliable(bool state) { m_bReliable = state; };
	virtual bool IsReliable() const { return m_bReliable; };
	virtual void SetNetChannel(INetChannel* netchan) { m_NetChannel = netchan; }
	virtual bool Process() { return false; }; // no handler set

protected:
	bool m_bReliable;          // true if message should be send reliable
	INetChannel* m_NetChannel; // netchannel this message is from/for
	byte pad0[8];
};

class SVC_FixAngle : public CNetMessage
{
	SVC_FixAngle() { m_bReliable = false; };
	SVC_FixAngle(bool bRelative, QAngle angle) { m_bReliable = false; m_bRelative = bRelative; m_Angle = angle; }

public:
	byte pad1[8];
	bool m_bRelative;
	QAngle m_Angle;
};

class SVC_PacketEntities : public CNetMessage
{
	int	GetGroup() const { return ENTITIES; }

public:

	byte pad1[8];
	int			m_nMaxEntries;
	int			m_nUpdatedEntries;
	bool		m_bIsDelta;
	bool		m_bUpdateBaseline;
	int			m_nBaseline;
	int			m_nDeltaFrom;
	int			m_nLength;
	bf_read		m_DataIn;
	bf_write	m_DataOut;
};

// ================ SDK/Definitions/Main/INetworkMessage.h ================
class INetChannel;

enum
{
	NETWORKSYSTEM_FIRST_GROUP = 1
};

// ================ SDK/Definitions/Misc/IAppSystem.h ================
enum InitReturnVal_t
{
	INIT_FAILED = 0,
	INIT_OK,
	INIT_LAST_VAL,
};

class IAppSystem
{
public:
	virtual bool Connect(CreateInterfaceFn factory) = 0;
	virtual void Disconnect() = 0;
	virtual void* QueryInterface(const char* pInterfaceName) = 0;
	virtual InitReturnVal_t Init() = 0;
	virtual void Shutdown() = 0;
};

// ================ SDK/Definitions/Misc/NetAdr.h ================

// ================ SDK/Definitions/Main/INetworkSystem.h ================
enum
{
	NETWORKSYSTEM_DEFAULT_SERVER_PORT = 27001,
	NETWORKSYSTEM_DEFAULT_CLIENT_PORT = 27002
};

// ================ SDK/Definitions/Main/NetChannel.h ================
enum
{
	FRAG_NORMAL_STREAM = 0,
	FRAG_FILE_STREAM,
	MAX_STREAMS
};

class CUtlBuffer;

class INetChannel;

//{
//	DECLARE_FIXEDSIZE_ALLOCATOR(CNetPacket);
//
//public:
//	netadr_t m_From;
//	CUDPSocket *m_pSource;
//	float m_flReceivedTime;
//	unsigned char *m_pData;
//	bf_read m_Message;
//	int m_nSizeInBytes;
//
//private:
//	int m_nRefCount;
//};

// ================ SDK/Definitions/Main/INetChannelInfo.h ================
#define FLOW_OUTGOING 0		
#define FLOW_INCOMING 1
#define MAX_FLOWS 2

class INetChannelInfo
{
public:
	virtual const char* GetName(void) const = 0;
	virtual const char* GetAddress(void) const = 0;
	virtual float GetTime(void) const = 0;
	virtual float GetTimeConnected(void) const = 0;
	virtual int GetBufferSize(void) const = 0;
	virtual int GetDataRate(void) const = 0;
	virtual bool IsLoopback(void) const = 0;
	virtual bool IsTimingOut(void) const = 0;
	virtual bool IsPlayback(void) const = 0;
	virtual float GetLatency(int flow) const = 0;
	virtual float GetAvgLatency(int flow) const = 0;
	virtual float GetAvgLoss(int flow) const = 0;
	virtual float GetAvgChoke(int flow) const = 0;
	virtual float GetAvgData(int flow) const = 0;
	virtual float GetAvgPackets(int flow) const = 0;
	virtual int GetTotalData(int flow) const = 0;
	virtual int GetSequenceNr(int flow) const = 0;
	virtual bool IsValidPacket(int flow, int frame_number) const = 0;
	virtual float GetPacketTime(int flow, int frame_number) const = 0;
	virtual int GetPacketBytes(int flow, int frame_number, int group) const = 0;
	virtual bool GetStreamProgress(int flow, int* received, int* total) const = 0;
	virtual float GetTimeSinceLastReceived(void) const = 0;
	virtual	float GetCommandInterpolationAmount(int flow, int frame_number) const = 0;
	virtual void GetPacketResponseLatency(int flow, int frame_number, int* pnLatencyMsecs, int* pnChoke) const = 0;
	virtual void GetRemoteFramerate(float* pflFrameTime, float* pflFrameTimeStdDeviation) const = 0;
	virtual float GetTimeoutSeconds() const = 0;
};

// ================ SDK/Definitions/Main/INetChannel.h ================

class INetChannel;   // body dropped: only ever named through a pointer here

// ================ SDK/Definitions/Misc/dt_common.h ================
//========= Copyright  1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//

#pragma warning (push)
#pragma warning (disable : 26495)

//#include <stddef.h>
#include <cstdio>

// Max number of properties in a datatable and its children.

// Gets the size of a variable in a class.

// SendProp::m_Flags.

// Note that the bit count is ignored in this case.

#define SPROP_INSIDEARRAY		(1<<8)	// This tells us that the property is inside an array, so it shouldn't be put into the
// flattened property list. Its array will point at it when it needs to.

// SendProxy_DataTableToDataTable that always send the data to all clients.

// (ie: for all automatically-chained base classes).
// In this case, it can get rid of this SendPropDataTable altogether and spare the
// trouble of walking the hierarchy more than necessary.

// This is server side only, it's used to mark properties whose SendProxy_* functions encode against gpGlobals->tickcount (the only ones that currently do this are
//  m_flAnimTime and m_flSimulationTime.  MODs shouldn't need to mess with this probably

// See SPROP_NUMFLAGBITS_NETWORKED for the ones which are networked

// Used by the SendProp and RecvProp functions to disable debug checks on type sizes.

// Use this to extern send and receive datatables, and reference them.

class SendProp;

typedef enum
{
	DPT_Int = 0,
	DPT_Float,
	DPT_Vector,
	DPT_VectorXY,
	DPT_String,
	DPT_Array,	// An array of the base types (can't be of datatables).
	DPT_DataTable,
#if 0 // We can't ship this since it changes the size of DTVariant to be 20 bytes instead of 16 and that breaks MODs!!!
	DPT_Quaternion,
#endif
	DPT_NUMSendPropTypes
} SendPropType;

#pragma warning (push)
#pragma warning (disable : 26812)

class DVariant
{
public:
	DVariant() { m_Type = DPT_Float; }
	DVariant(float val) { m_Type = DPT_Float; m_Float = val; }

	const char* ToString()
	{
		static char text[128];

		switch (m_Type)
		{
		case DPT_Int:
			sprintf_s(text, sizeof(text), "%i", m_Int);
			break;
		case DPT_Float:
			sprintf_s(text, sizeof(text), "%.3f", m_Float);
			break;
		case DPT_Vector:
			sprintf_s(text, sizeof(text), "(%.3f,%.3f,%.3f)",
				m_Vector[0], m_Vector[1], m_Vector[2]);
			break;
#if 0 // We can't ship this since it changes the size of DTVariant to be 20 bytes instead of 16 and that breaks MODs!!!
		case DPT_Quaternion:
			sprintf_s(text, sizeof(text), "(%.3f,%.3f,%.3f %.3f)",
				m_Vector[0], m_Vector[1], m_Vector[2], m_Vector[3]);
			break;
#endif
		case DPT_String:
			if (m_pString)
				return m_pString;
			else
				return "NULL";
			break;
		case DPT_Array:
			sprintf_s(text, sizeof(text), "Array");
			break;
		case DPT_DataTable:
			sprintf_s(text, sizeof(text), "DataTable");
			break;
		default:
			sprintf_s(text, sizeof(text), "DVariant type %i unknown", m_Type);
			break;
		}

		return text;
	}

	union
	{
		float	m_Float;
		long	m_Int;
		char* m_pString;
		void* m_pData;	// For DataTables.
#if 0 // We can't ship this since it changes the size of DTVariant to be 20 bytes instead of 16 and that breaks MODs!!!
		float	m_Vector[4];
#else
		float	m_Vector[3];
#endif
	};
	SendPropType	m_Type;
};

#pragma warning (pop)

// This can be used to set the # of bits used to transmit a number between 0 and nMaxElements-1.

#pragma warning (pop)

// ================ SDK/Definitions/Misc/dt_recv.h ================
//========= Copyright  1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//

#ifndef DATATABLE_RECV_H
#define DATATABLE_RECV_H

#ifdef _WIN32
#endif

class RecvTable;
class RecvProp;

// This is passed into RecvProxy functions.
class CRecvProxyData
{
public:
	const RecvProp* m_pRecvProp;		// The property it's receiving.

	DVariant		m_Value;			// The value given to you to store.

	int				m_iElement;			// Which array element you're getting.

	int				m_ObjectID;			// The object being referred to.
};

//-----------------------------------------------------------------------------
// pStruct = the base structure of the datatable this variable is in (like C_BaseEntity)
// pOut    = the variable that this this proxy represents (like C_BaseEntity::m_SomeValue).
//
// Convert the network-standard-type value in m_Value into your own format in pStruct/pOut.
//-----------------------------------------------------------------------------
typedef void(*RecvVarProxyFn)(const CRecvProxyData* pData, void* pStruct, void* pOut);

// ------------------------------------------------------------------------ //
// ArrayLengthRecvProxies are optionally used to get the length of the 
// incoming array when it changes.
// ------------------------------------------------------------------------ //
typedef void(*ArrayLengthRecvProxyFn)(void* pStruct, int objectID, int currentArrayLength);

// NOTE: DataTable receive proxies work differently than the other proxies.
// pData points at the object + the recv table's offset.
// pOut should be set to the location of the object to unpack the data table into.
// If the parent object just contains the child object, the default proxy just does *pOut = pData.
// If the parent object points at the child object, you need to dereference the pointer here.
// NOTE: don't ever return null from a DataTable receive proxy function. Bad things will happen.
typedef void(*DataTableRecvVarProxyFn)(const RecvProp* pProp, void** pOut, void* pData, int objectID);

// This is used to fork over the standard proxy functions to the engine so it can
// make some optimizations.
class CStandardRecvProxies
{
public:
	CStandardRecvProxies();

	RecvVarProxyFn m_Int32ToInt8;
	RecvVarProxyFn m_Int32ToInt16;
	RecvVarProxyFn m_Int32ToInt32;
	RecvVarProxyFn m_FloatToFloat;
	RecvVarProxyFn m_VectorToVector;
};

class CRecvDecoder;

class RecvProp
{
	// This info comes from the receive data table.
public:
	RecvProp();

	void					InitArray(int nElements, int elementStride);

	int						GetNumElements() const;
	void					SetNumElements(int nElements);

	int						GetElementStride() const;
	void					SetElementStride(int stride);

	int						GetFlags() const;

	const char* GetName() const;
	SendPropType			GetType() const;

	RecvTable* GetDataTable() const;
	void					SetDataTable(RecvTable* pTable);

	RecvVarProxyFn			GetProxyFn() const;
	void					SetProxyFn(RecvVarProxyFn fn);

	DataTableRecvVarProxyFn	GetDataTableProxyFn() const;
	void					SetDataTableProxyFn(DataTableRecvVarProxyFn fn);

	int						GetOffset() const;
	void					SetOffset(int o);

	// Arrays only.
	RecvProp* GetArrayProp() const;
	void					SetArrayProp(RecvProp* pProp);

	// Arrays only.
	void					SetArrayLengthProxy(ArrayLengthRecvProxyFn proxy);
	ArrayLengthRecvProxyFn	GetArrayLengthProxy() const;

	bool					IsInsideArray() const;
	void					SetInsideArray();

	// Some property types bind more data to the prop in here.
	const void* GetExtraData() const;
	void				SetExtraData(const void* pData);

public:

	const char* m_pVarName;
	SendPropType			m_RecvType;
	int						m_Flags;
	int						m_StringBufferSize;

private:

	bool					m_bInsideArray;		// Set to true by the engine if this property sits inside an array.
	// Extra data that certain special property types bind to the property here.
	const void* m_pExtraData;

	// If this is an array (DPT_Array).
	RecvProp* m_pArrayProp;
	ArrayLengthRecvProxyFn	m_ArrayLengthProxy;

	RecvVarProxyFn			m_ProxyFn;
	DataTableRecvVarProxyFn	m_DataTableProxyFn;	// For RDT_DataTable.

	RecvTable* m_pDataTable;		// For RDT_DataTable.
	int						m_Offset;

	int						m_ElementStride;
	int						m_nElements;

	// If it's one of the numbered "000", "001", etc properties in an array, then
	// these can be used to get its array property name for debugging.
	const char* m_pParentArrayPropName;
};

class RecvTable
{
public:

	typedef RecvProp	PropType;

	RecvTable();
	RecvTable(RecvProp* pProps, int nProps, const char* pNetTableName);
	~RecvTable();

	void		Construct(RecvProp* pProps, int nProps, const char* pNetTableName);

	int			GetNumProps();
	RecvProp* GetProp(int i);

	const char* GetName();

	// Used by the engine while initializing array props.
	void		SetInitialized(bool bInitialized);
	bool		IsInitialized() const;

	// Used by the engine.
	void		SetInMainList(bool bInList);
	bool		IsInMainList() const;

public:

	// Properties described in a table.
	RecvProp* m_pProps;
	int				m_nProps;

	// The decoder. NOTE: this covers each RecvTable AND all its children (ie: its children
	// will have their own decoders that include props for all their children).
	CRecvDecoder* m_pDecoder;

	const char* m_pNetTableName;	// The name matched between client and server.

private:

	bool			m_bInitialized;
	bool			m_bInMainList;
};

inline int RecvTable::GetNumProps()
{
	return m_nProps;
}

inline RecvProp* RecvTable::GetProp(int i)
{
	//Assert( i >= 0 && i < m_nProps ); 
	return &m_pProps[i];
}

inline const char* RecvTable::GetName()
{
	return m_pNetTableName;
}

inline void RecvTable::SetInitialized(bool bInitialized)
{
	m_bInitialized = bInitialized;
}

inline bool RecvTable::IsInitialized() const
{
	return m_bInitialized;
}

inline void RecvTable::SetInMainList(bool bInList)
{
	m_bInMainList = bInList;
}

inline bool RecvTable::IsInMainList() const
{
	return m_bInMainList;
}

// ------------------------------------------------------------------------------------------------------ //
// See notes on BEGIN_SEND_TABLE for a description. These macros work similarly.
// ------------------------------------------------------------------------------------------------------ //

// Just specify the name and offset. Used for strings and data tables.

// StaticDataTable does *pOut = pData.

// PointerDataTable does *pOut = *((void**)pData)   (ie: pData is a pointer to the object to decode into).

// This is here so the RecvTable can look more like the SendTable.

#if 0 // We can't ship this since it changes the size of DTVariant to be 20 bytes instead of 16 and that breaks MODs!!!

#endif

// Use the macro to let it automatically generate a table name. You shouldn't 
// ever need to reference the table name. If you want to exclude this array, then
// reference the name of the variable in varTemplate.

//
// Use this if you want to completely manage the way the array data is stored.
// You'll need to provide a proxy inside varTemplate that looks for 'iElement'
// to figure out where to store the specified element.
//

// Use this and pass the array name and it will figure out the count and stride automatically.

// Use this and pass the array name and it will figure out the count and stride automatically.

// Use this one to specify the element count and stride manually.

// ---------------------------------------------------------------------------------------- //
// Inlines.
// ---------------------------------------------------------------------------------------- //
inline void RecvProp::InitArray(int nElements, int elementStride)
{
	m_RecvType = DPT_Array;
	m_nElements = nElements;
	m_ElementStride = elementStride;
}

inline int RecvProp::GetNumElements() const
{
	return m_nElements;
}

inline void RecvProp::SetNumElements(int nElements)
{
	m_nElements = nElements;
}

inline int RecvProp::GetElementStride() const
{
	return m_ElementStride;
}

inline void RecvProp::SetElementStride(int stride)
{
	m_ElementStride = stride;
}

inline int RecvProp::GetFlags() const
{
	return m_Flags;
}

inline const char* RecvProp::GetName() const
{
	return m_pVarName;
}

inline SendPropType RecvProp::GetType() const
{
	return m_RecvType;
}

inline RecvTable* RecvProp::GetDataTable() const
{
	return m_pDataTable;
}

inline void RecvProp::SetDataTable(RecvTable* pTable)
{
	m_pDataTable = pTable;
}

inline RecvVarProxyFn RecvProp::GetProxyFn() const
{
	return m_ProxyFn;
}

inline void RecvProp::SetProxyFn(RecvVarProxyFn fn)
{
	m_ProxyFn = fn;
}

inline DataTableRecvVarProxyFn RecvProp::GetDataTableProxyFn() const
{
	return m_DataTableProxyFn;
}

inline void RecvProp::SetDataTableProxyFn(DataTableRecvVarProxyFn fn)
{
	m_DataTableProxyFn = fn;
}

inline int RecvProp::GetOffset() const
{
	return m_Offset;
}

inline void RecvProp::SetOffset(int o)
{
	m_Offset = o;
}

inline RecvProp* RecvProp::GetArrayProp() const
{
	return m_pArrayProp;
}

inline void RecvProp::SetArrayProp(RecvProp* pProp)
{
	m_pArrayProp = pProp;
}

inline void RecvProp::SetArrayLengthProxy(ArrayLengthRecvProxyFn proxy)
{
	m_ArrayLengthProxy = proxy;
}

inline ArrayLengthRecvProxyFn RecvProp::GetArrayLengthProxy() const
{
	return m_ArrayLengthProxy;
}

inline bool RecvProp::IsInsideArray() const
{
	return m_bInsideArray;
}

inline void RecvProp::SetInsideArray()
{
	m_bInsideArray = true;
}

inline const void* RecvProp::GetExtraData() const
{
	return m_pExtraData;
}

inline void RecvProp::SetExtraData(const void* pData)
{
	m_pExtraData = pData;
}

#endif // DATATABLE_RECV_H

// ================ SDK/Definitions/Misc/ClientClass.h ================
class IClientNetworkable;

typedef IClientNetworkable* (*CreateClientClassFn)(int entnum, int serialNum);
typedef IClientNetworkable* (*CreateEventFn)();

class ClientClass
{
public:
	const char* GetName() { return m_pNetworkName; }

public:
	CreateClientClassFn	m_pCreateFn;
	CreateEventFn m_pCreateEventFn;
	const char* m_pNetworkName;
	RecvTable* m_pRecvTable;
	ClientClass* m_pNext;
	int m_ClassID;
};

// ================ SDK/Definitions/Misc/bitvec.h ================
//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//===========================================================================//

#ifndef BITVEC_H
#define BITVEC_H
#ifdef _WIN32
#endif

#include <limits.h>
#include <cstring>

// Pad a number so it lies on an N byte boundary.
// So PAD_NUMBER(0,4) is 0 and PAD_NUMBER(1,4) is 4

//-----------------------------------------------------------------------------
// Support functions
//-----------------------------------------------------------------------------

#define BITS_PER_INT		32

#if _WIN32 && !defined(_X360)
#include <intrin.h>
#pragma intrinsic(_BitScanForward)
#endif

//-------------------------------------

inline int GetBitForBitnum(int bitNum)
{
	static int bitsForBitnum[] =
	{
		(1 << 0),
		(1 << 1),
		(1 << 2),
		(1 << 3),
		(1 << 4),
		(1 << 5),
		(1 << 6),
		(1 << 7),
		(1 << 8),
		(1 << 9),
		(1 << 10),
		(1 << 11),
		(1 << 12),
		(1 << 13),
		(1 << 14),
		(1 << 15),
		(1 << 16),
		(1 << 17),
		(1 << 18),
		(1 << 19),
		(1 << 20),
		(1 << 21),
		(1 << 22),
		(1 << 23),
		(1 << 24),
		(1 << 25),
		(1 << 26),
		(1 << 27),
		(1 << 28),
		(1 << 29),
		(1 << 30),
		(1 << 31),
	};

	return bitsForBitnum[(bitNum) & (BITS_PER_INT - 1)];
}

//-----------------------------------------------------------------------------
// template CBitVecT
//
// Defines the operations relevant to any bit array. Simply requires a base
// class that implements GetNumBits(), Base(), GetNumDWords() & ValidateOperand()
//
// CVarBitVec and CBitVec<int> are the actual classes generally used
// by clients
//

//-----------------------------------------------------------------------------
// class CVarBitVecBase
//
// Defines the operations necessary for a variable sized bit array

//-----------------------------------------------------------------------------
// class CFixedBitVecBase
//
// Defines the operations necessary for a fixed sized bit array. 
// 

//-------------------------------------

//-----------------------------------------------------------------------------
//
// The actual classes used
//

// inheritance instead of typedef to allow forward declarations

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: Destructor
// Input  :
// Output :
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose:
// Input  :
// Output :
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose:
// Input  :
// Output :
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose:
// Input  :
// Output :
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose:
// Input  :
// Output :
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: Copy a bit string
// Input  :
// Output :
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: Are all bits zero?
// Input  :
// Output :
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: Are all bits set?
// Input  :
// Output :
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: Sets all bits
// Input  :
// Output :
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: Clears all bits
// Input  :
// Output :
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Unrolled loops for some common sizes

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: Resizes the bit string to a new number of bits
// Input  : resizeNumBits - 
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: Allocate the storage for the ints
// Input  : numInts - 
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: Reallocate the storage for the ints
// Input  : numInts - 
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: Free storage allocated with AllocInts
//-----------------------------------------------------------------------------

// ------------------------------------------------------------------------ //
// CBitVecAccessor inlines.
// ------------------------------------------------------------------------ //

//=============================================================================

#endif // BITVEC_H

// ================ SDK/Definitions/Misc/dt_send.h ================
//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//

#ifndef DATATABLE_SEND_H
#define DATATABLE_SEND_H

#ifdef _WIN32
#endif

#define ABSOLUTE_PLAYER_LIMIT 255

// ------------------------------------------------------------------------ //
// Send proxies can be used to convert a variable into a networkable type 
// (a good example is converting an edict pointer into an integer index).

// These allow you to translate data. For example, if you had a user-entered 
// string number like "10" (call the variable pUserStr) and wanted to encode 
// it as an integer, you would use a SendPropInt32 and write a proxy that said:
// pOut->m_Int = atoi(pUserStr);

// pProp       : the SendProp that has the proxy
// pStructBase : the base structure (like CBaseEntity*).
// pData       : the address of the variable to proxy.
// pOut        : where to output the proxied value.
// iElement    : the element index if this data is part of an array (or 0 if not).
// objectID    : entity index for debugging purposes.

// Return false if you don't want the engine to register and send a delta to
// the clients for this property (regardless of whether it actually changed or not).
// ------------------------------------------------------------------------ //
typedef void (*SendVarProxyFn)(const SendProp* pProp, const void* pStructBase, const void* pData, DVariant* pOut, int iElement, int objectID);

// Return the pointer to the data for the datatable.
// If the proxy returns null, it's the same as if pRecipients->ClearAllRecipients() was called.
class CSendProxyRecipients;

typedef void* (*SendTableProxyFn)(
	const SendProp* pProp,
	const void* pStructBase,
	const void* pData,
	CSendProxyRecipients* pRecipients,
	int objectID);

// This tells the engine that the send proxy will not modify the pointer
// - it only plays with the recipients. This must be set on proxies that work
// this way, otherwise the engine can't track which properties changed
// in NetworkStateChanged().

// Max # of datatable send proxies you can have in a tree.

// ------------------------------------------------------------------------ //
// Datatable send proxies are used to tell the engine where the datatable's 
// data is and to specify which clients should get the data. 
//
// pRecipients is the object that allows you to specify which clients will
// receive the data.
// ------------------------------------------------------------------------ //
class CSendProxyRecipients
{
public:
	// Was CBitVec<ABSOLUTE_PLAYER_LIMIT> plus five setters, none of which anything here calls.
	// CBitVec is nothing but this array (no virtuals, no other members), so dropping it takes
	// the whole bitvec header with it. Same size, same layout.
	uint32	m_Bits[(ABSOLUTE_PLAYER_LIMIT + 31) / 32];
};

// ------------------------------------------------------------------------ //
// ArrayLengthSendProxies are used when you want to specify an array's length
// dynamically.
// ------------------------------------------------------------------------ //
typedef int (*ArrayLengthSendProxyFn)(const void* pStruct, int objectID);

class RecvProp;
class SendTable;
class CSendTablePrecalc;

// -------------------------------------------------------------------------------------------------------------- //
// SendProp.
// -------------------------------------------------------------------------------------------------------------- //

// If SendProp::GetDataTableProxyIndex() returns this, then the proxy is one that always sends
// the data to all clients, so we don't need to store the results.

class SendProp
{
public:
	SendProp()
	{
		m_pVarName = NULL;
		m_Offset = 0;
		m_pDataTable = NULL;
		m_ProxyFn = NULL;
		m_pExcludeDTName = NULL;
		m_pParentArrayPropName = NULL;

		m_Type = DPT_Int;
		m_Flags = 0;
		m_nBits = 0;

		m_fLowValue = 0.0f;
		m_fHighValue = 0.0f;
		m_pArrayProp = 0;
		m_ArrayLengthProxy = 0;
		m_nElements = 1;
		m_ElementStride = -1;
	}
	virtual				~SendProp() {};

	void				Clear() {};

	int					GetOffset() const;
	void				SetOffset(int i);

	SendVarProxyFn		GetProxyFn() const;
	void				SetProxyFn(SendVarProxyFn f);

	SendTableProxyFn	GetDataTableProxyFn() const;
	void				SetDataTableProxyFn(SendTableProxyFn f);

	SendTable* GetDataTable() const;
	void				SetDataTable(SendTable* pTable);

	char const* GetExcludeDTName() const;

	const char* GetName() const;

	bool				IsInsideArray() const;	// Returns true if SPROP_INSIDEARRAY is set.
	void				SetInsideArray();

	// Arrays only.
	void				SetArrayProp(SendProp* pProp);
	SendProp* GetArrayProp() const;

	// Arrays only.
	void					SetArrayLengthProxy(ArrayLengthSendProxyFn fn);
	ArrayLengthSendProxyFn	GetArrayLengthProxy() const;

	int					GetNumElements() const;
	void				SetNumElements(int nElements);

	int					GetElementStride() const;

	SendPropType		GetType() const;

	int					GetFlags() const;

	// Some property types bind more data to the SendProp in here.
	const void* GetExtraData() const;
	void				SetExtraData(const void* pData);

public:

	RecvProp* m_pMatchingRecvProp;	// This is temporary and only used while precalculating
	// data for the decoders.

	SendPropType	m_Type;
	int				m_nBits;
	float			m_fLowValue;
	float			m_fHighValue;

	SendProp* m_pArrayProp;					// If this is an array, this is the property that defines each array element.
	ArrayLengthSendProxyFn	m_ArrayLengthProxy;	// This callback returns the array length.

	int				m_nElements;		// Number of elements in the array (or 1 if it's not an array).
	int				m_ElementStride;	// Pointer distance between array elements.

	const char* m_pExcludeDTName;			// If this is an exclude prop, then this is the name of the datatable to exclude a prop from.
	const char* m_pParentArrayPropName;

	const char* m_pVarName;
	float			m_fHighLowMul;

private:

	int					m_Flags;				// SPROP_ flags.

	SendVarProxyFn		m_ProxyFn;				// NULL for DPT_DataTable.
	SendTableProxyFn	m_DataTableProxyFn;		// Valid for DPT_DataTable.

	SendTable* m_pDataTable;

	// SENDPROP_VECTORELEM makes this negative to start with so we can detect that and
	// set the SPROP_IS_VECTOR_ELEM flag.
	int					m_Offset;

	// Extra data bound to this property.
	const void* m_pExtraData;
};

inline int SendProp::GetOffset() const
{
	return m_Offset;
}

inline void SendProp::SetOffset(int i)
{
	m_Offset = i;
}

inline SendVarProxyFn SendProp::GetProxyFn() const
{
	//Assert(m_Type != DPT_DataTable);
	return m_ProxyFn;
}

inline void SendProp::SetProxyFn(SendVarProxyFn f)
{
	m_ProxyFn = f;
}

inline SendTableProxyFn SendProp::GetDataTableProxyFn() const
{
	//Assert(m_Type == DPT_DataTable);
	return m_DataTableProxyFn;
}

inline void SendProp::SetDataTableProxyFn(SendTableProxyFn f)
{
	m_DataTableProxyFn = f;
}

inline SendTable* SendProp::GetDataTable() const
{
	return m_pDataTable;
}

inline void SendProp::SetDataTable(SendTable* pTable)
{
	m_pDataTable = pTable;
}

inline char const* SendProp::GetExcludeDTName() const
{
	return m_pExcludeDTName;
}

inline const char* SendProp::GetName() const
{
	return m_pVarName;
}

inline bool	SendProp::IsInsideArray() const
{
	return (m_Flags & SPROP_INSIDEARRAY) != 0;
}

inline void SendProp::SetInsideArray()
{
	m_Flags |= SPROP_INSIDEARRAY;
}

inline void SendProp::SetArrayProp(SendProp* pProp)
{
	m_pArrayProp = pProp;
}

inline SendProp* SendProp::GetArrayProp() const
{
	return m_pArrayProp;
}

inline void SendProp::SetArrayLengthProxy(ArrayLengthSendProxyFn fn)
{
	m_ArrayLengthProxy = fn;
}

inline ArrayLengthSendProxyFn SendProp::GetArrayLengthProxy() const
{
	return m_ArrayLengthProxy;
}

inline int SendProp::GetNumElements() const
{
	return m_nElements;
}

inline void SendProp::SetNumElements(int nElements)
{
	m_nElements = nElements;
}

inline int SendProp::GetElementStride() const
{
	return m_ElementStride;
}

inline SendPropType SendProp::GetType() const
{
	return m_Type;
}

inline int SendProp::GetFlags() const
{
	return m_Flags;
}

inline const void* SendProp::GetExtraData() const
{
	return m_pExtraData;
}

inline void SendProp::SetExtraData(const void* pData)
{
	m_pExtraData = pData;
}

// -------------------------------------------------------------------------------------------------------------- //
// SendTable.
// -------------------------------------------------------------------------------------------------------------- //

class SendTable
{
public:

	typedef SendProp PropType;

	SendTable();
	SendTable(SendProp* pProps, int nProps, const char* pNetTableName);
	~SendTable();

	void		Construct(SendProp* pProps, int nProps, const char* pNetTableName);

	const char* GetName() const;

	int			GetNumProps() const;
	SendProp* GetProp(int i);

	// Used by the engine.
	bool		IsInitialized() const;
	void		SetInitialized(bool bInitialized);

	// Used by the engine while writing info into the signon.
	void		SetWriteFlag(bool bHasBeenWritten);
	bool		GetWriteFlag() const;

	bool		HasPropsEncodedAgainstTickCount() const;
	void		SetHasPropsEncodedAgainstTickcount(bool bState);

public:

	SendProp* m_pProps;
	int			m_nProps;

	const char* m_pNetTableName;	// The name matched between client and server.

	// The engine hooks the SendTable here.
	CSendTablePrecalc* m_pPrecalc;

protected:
	bool		m_bInitialized : 1;
	bool		m_bHasBeenWritten : 1;
	bool		m_bHasPropsEncodedAgainstCurrentTickCount : 1; // m_flSimulationTime and m_flAnimTime, e.g.
};

inline const char* SendTable::GetName() const
{
	return m_pNetTableName;
}

inline int SendTable::GetNumProps() const
{
	return m_nProps;
}

inline SendProp* SendTable::GetProp(int i)
{
	//Assert(i >= 0 && i < m_nProps);
	return &m_pProps[i];
}

inline bool SendTable::IsInitialized() const
{
	return m_bInitialized;
}

inline void SendTable::SetInitialized(bool bInitialized)
{
	m_bInitialized = bInitialized;
}

inline bool SendTable::GetWriteFlag() const
{
	return m_bHasBeenWritten;
}

inline void SendTable::SetWriteFlag(bool bHasBeenWritten)
{
	m_bHasBeenWritten = bHasBeenWritten;
}

inline bool SendTable::HasPropsEncodedAgainstTickCount() const
{
	return m_bHasPropsEncodedAgainstCurrentTickCount;
}

inline void SendTable::SetHasPropsEncodedAgainstTickcount(bool bState)
{
	m_bHasPropsEncodedAgainstCurrentTickCount = bState;
}

// ------------------------------------------------------------------------------------------------------ //
// Use BEGIN_SEND_TABLE if you want to declare a SendTable and have it inherit all the properties from
// its base class. There are two requirements for this to work:

// 1. Its base class must have a static SendTable pointer member variable called m_pClassSendTable which
//    points to its send table. The DECLARE_SERVERCLASS and IMPLEMENT_SERVERCLASS macros do this automatically.

// 2. Your class must typedef its base class as BaseClass. So it would look like this:
//    class Derived : public CBaseEntity
//    {
//    typedef CBaseEntity BaseClass;
//    };

// If you don't want to interit a base class's properties, use BEGIN_SEND_TABLE_NOBASE.
// ------------------------------------------------------------------------------------------------------ //

// Normal offset of is invalid on non-array-types, this is dubious as hell. The rest of the codebase converted to the
// legit offsetof from the C headers, so we'll use the old impl here to avoid exposing temptation to others

// These can simplify creating the variables.
// Note: currentSendDTClass::MakeANetworkVar_##varName equates to currentSendDTClass. It's
// there as a check to make sure all networked variables use the CNetworkXXXX macros in network_var.h.

// NOTE: Be VERY careful to specify any other vector elems for the same vector IN ORDER and 
// right after each other, otherwise it might miss the Y or Z component in SP.
//
// Note: this macro specifies a negative offset so the engine can detect it and setup m_pNext

// Use this when you're not using a CNetworkVar to represent the data you're sending.

// ------------------------------------------------------------------------ //
// Built-in proxy types.
// See the definition of SendVarProxyFn for information about these.
// ------------------------------------------------------------------------ //
#if 0 // We can't ship this since it changes the size of DTVariant to be 20 bytes instead of 16 and that breaks MODs!!!
#endif

// pData is the address of a data table.

// pData is the address of a pointer to a data table.

// Used on player entities - only sends the data to the local player (objectID-1).

// ------------------------------------------------------------------------ //
// Use these functions to setup your data tables.
// ------------------------------------------------------------------------ //

#if 0 // We can't ship this since it changes the size of DTVariant to be 20 bytes instead of 16 and that breaks MODs!!!
#endif

// The data table encoder looks at DVariant::m_pData.

// Use the macro to let it automatically generate a table name. You shouldn't 
// ever need to reference the table name. If you want to exclude this array, then
// reference the name of the variable in varTemplate.

// Use this and pass the array name and it will figure out the count and stride automatically.

//
// Use this when you want to send a variable-length array of data but there is no physical array you can point it at.
// You need to provide:
// 1. A proxy function that returns the current length of the array.
// 2. The maximum length the array will ever be.
// 2. A SendProp describing what the elements are comprised of.
// 3. In the SendProp, you'll want to specify a proxy function so you can go grab the data from wherever it is.
// 4. A property name that matches the definition on the client.
//

// Use this one to specify the element count and stride manually.

// Use these to create properties that exclude other properties. This is useful if you want to use most of 
// a base class's datatable data, but you want to override some of its variables.

#endif // DATATABLE_SEND_H

// ================ SDK/Definitions/Misc/ServerClass.h ================

// ================ SDK/Definitions/Interfaces/CClientState.h ================
MAKE_SIGNATURE(CBaseClientState_SendStringCmd, "engine.dll", "48 81 EC ? ? ? ? 48 8B 49", 0x0);

class PackedEntity;   // body dropped: only ever named through a pointer here

struct CClockDriftMgr
{
	float m_ClockOffsets[16];
	int m_iCurClockOffset;
	int m_nServerTick;
	int m_nClientTick;
};

class CClientState
{
public:
	byte pad0[24];
	int m_Socket;
	INetChannel* m_NetChannel;
	unsigned int m_nChallengeNr;
	double m_flConnectTime;
	int m_nRetryNumber;
	char m_szRetryAddress[MAX_OSPATH];
	CUtlString m_sRetrySourceTag;
	int m_retryChallenge;
	int m_nSignonState;
	double m_flNextCmdTime;
	int m_nServerCount;
	unsigned __int64 m_ulGameServerSteamID;
	int m_nCurrentSequence;
	CClockDriftMgr m_ClockDriftMgr;
	int m_nDeltaTick;
	bool m_bPaused;
	float m_flPausedExpireTime;
	int m_nViewEntity;
	int m_nPlayerSlot;
	char m_szLevelFileName[128];
	byte pad1[132];
	char m_szLevelBaseName[128];
	byte pad2[132];
	int m_nMaxClients;
	PackedEntity* m_pEntityBaselines[2][MAX_EDICTS];
	byte pad3[2068];
	void* m_StringTableContainer;
	bool m_bRestrictServerCommands;
	bool m_bRestrictClientCommands;
	byte pad4[106];
	bool insimulation;
	int oldtickcount;
	float m_tickRemainder;
	float m_frameTime;
	int lastoutgoingcommand;
	int chokedcommands;
	int last_command_ack;
	int command_ack;
	int m_nSoundSequence;
	bool ishltv;
	bool isreplay;
	byte pad5[278];
	int demonum;
	CUtlString demos[32];
	byte pad6[344184];
	bool m_bMarkedCRCsUnverified;

public:
	void SendStringCmd(const char* command)
	{
		reinterpret_cast<void(*)(void*, const char*)>(S::CBaseClientState_SendStringCmd())(this, command);
	}
};

MAKE_INTERFACE_SIGNATURE(CClientState, ClientState, "engine.dll", "48 8D 0D ? ? ? ? E8 ? ? ? ? F3 0F 5E 05", 0x0, 0);

// ================ SDK/Definitions/Interfaces/CGlobalVarsBase.h ================
class CSaveRestoreData;

class CGlobalVarsBase
{
public:
	float realtime;
	int framecount;
	float absoluteframetime;
	float curtime;
	float frametime;
	int maxClients;
	int tickcount;
	float interval_per_tick;
	float interpolation_amount;
	int simTicksThisFrame;
	int network_protocol;
	CSaveRestoreData* pSaveData;
	bool m_bClient;
	int nTimestampNetworkingBase;
	int nTimestampRandomizeWindow;
};

MAKE_INTERFACE_SIGNATURE(CGlobalVarsBase, GlobalVars, "engine.dll", "48 8D 05 ? ? ? ? C3 CC CC CC CC CC CC CC CC 48 8B CA", 0x0, 0);

// ================ SDK/Definitions/Misc/ChecksumCRC.h ================
typedef unsigned int CRC32_t;

void CRC32_Init(CRC32_t* pulCRC);
void CRC32_ProcessBuffer(CRC32_t* pulCRC, const void* p, int len);
void CRC32_Final(CRC32_t* pulCRC);
CRC32_t	CRC32_GetTableEntry(unsigned int slot);

// ================ SDK/Definitions/Misc/CViewSetup.h ================
enum StereoEye_t
{
	STEREO_EYE_MONO = 0,
	STEREO_EYE_LEFT = 1,
	STEREO_EYE_RIGHT = 2,
	STEREO_EYE_MAX = 3
};

class CViewSetup
{
public:
	int x;
	int m_nUnscaledX;
	int y;
	int m_nUnscaledY;
	int width;
	int m_nUnscaledWidth;
	int height;
	StereoEye_t m_eStereoEye;
	int m_nUnscaledHeight;
	bool m_bOrtho;
	float m_OrthoLeft;
	float m_OrthoTop;
	float m_OrthoRight;
	float m_OrthoBottom;
	float fov;
	float fovViewmodel;
	Vector origin;
	QAngle angles;
	float zNear;
	float zFar;
	float zNearViewmodel;
	float zFarViewmodel;
	bool m_bRenderToSubrectOfLargerScreen;
	float m_flAspectRatio;
	bool m_bOffCenter;
	float m_flOffCenterTop;
	float m_flOffCenterBottom;
	float m_flOffCenterLeft;
	float m_flOffCenterRight;
	bool m_bDoBloomAndToneMapping;
	bool m_bCacheFullSceneState;
	bool m_bViewToProjectionOverride;
	VMatrix m_ViewToProjection;
};

// ================ SDK/Definitions/Misc/Datamap.h ================
struct inputdata_t;

typedef enum _fieldtypes
{
	FIELD_VOID = 0,
	FIELD_FLOAT,
	FIELD_STRING,
	FIELD_VECTOR,
	FIELD_QUATERNION,
	FIELD_INTEGER,
	FIELD_BOOLEAN,
	FIELD_SHORT,
	FIELD_CHARACTER,
	FIELD_COLOR32,
	FIELD_EMBEDDED,
	FIELD_CUSTOM,
	FIELD_CLASSPTR,
	FIELD_EHANDLE,
	FIELD_EDICT,
	FIELD_POSITION_VECTOR,
	FIELD_TIME,
	FIELD_TICK,
	FIELD_MODELNAME,
	FIELD_SOUNDNAME,
	FIELD_INPUT,
	FIELD_FUNCTION,
	FIELD_VMATRIX,
	FIELD_VMATRIX_WORLDSPACE,
	FIELD_MATRIX3X4_WORLDSPACE,
	FIELD_INTERVAL,
	FIELD_MODELINDEX,
	FIELD_MATERIALINDEX,
	FIELD_VECTOR2D,
	FIELD_TYPECOUNT
} fieldtype_t;

struct typedescription_t;
class ISaveRestoreOps;

typedef void (*inputfunc_t)(inputdata_t& data);

struct datamap_t;
struct typedescription_t;

enum
{
	TD_OFFSET_NORMAL = 0,
	TD_OFFSET_PACKED = 1,
	TD_OFFSET_COUNT
};

struct typedescription_t
{
	fieldtype_t fieldType;
	const char* fieldName;
	int fieldOffset[TD_OFFSET_COUNT];
	unsigned short fieldSize;
	short flags;
	const char* externalName;
	ISaveRestoreOps* pSaveRestoreOps;
	inputfunc_t inputFunc;
	datamap_t* td;
	int fieldSizeInBytes;
	struct typedescription_t* override_field;
	int override_count;
	float fieldTolerance;
};

struct datamap_t
{
	typedescription_t* dataDesc;
	int dataNumFields;
	char const* dataClassName;
	datamap_t* baseMap;
	bool chains_validated;
	bool packed_offsets_computed;
	int packed_size;
};

// ================ SDK/Definitions/Misc/Modes.h ================
typedef struct vmode_s
{
	int width;
	int height;
	int bpp;
	int refreshRate;
} vmode_t;

// ================ SDK/Definitions/Interfaces/CHLClient.h ================
#include <Windows.h>

struct vrect_t;
struct ScreenFade_t;
class CEngineSprite;
class CGlobalVarsBase;
class CSaveRestoreData;
struct datamap_t;
struct typedescription_t;
class CStandardRecvProxies;
class IFileList;
class CRenamedRecvTableInfo;
class CMouthInfo;
class IConVar;

typedef struct _XUSER_DATA
{
	BYTE type;

	union
	{
		int nData;
		int64 i64Data;
		double dblData;
		struct
		{
			uint cbData;
			char* pwszData;
		} string;
		float fData;
		struct
		{
			uint cbData;
			char* pbData;
		} binary;
	};
} XUSER_DATA, * PXUSER_DATA;

typedef struct _XUSER_PROPERTY
{
	DWORD dwPropertyId;
	XUSER_DATA value;
} XUSER_PROPERTY, * PXUSER_PROPERTY;

typedef struct _XUSER_CONTEXT
{
	DWORD dwContextId;
	DWORD dwValue;
} XUSER_CONTEXT, * PXUSER_CONTEXT;

enum ClientFrameStage_t
{
	FRAME_UNDEFINED = -1,
	FRAME_START,
	FRAME_NET_UPDATE_START,
	FRAME_NET_UPDATE_POSTDATAUPDATE_START,
	FRAME_NET_UPDATE_POSTDATAUPDATE_END,
	FRAME_NET_UPDATE_END,
	FRAME_RENDER_START,
	FRAME_RENDER_END
};

class IBaseClientDLL
{
public:
	virtual int Init(CreateInterfaceFn appSystemFactory, CreateInterfaceFn physicsFactory, CGlobalVarsBase* pGlobals) = 0;
	virtual void PostInit() = 0;
	virtual void Shutdown(void) = 0;
	virtual bool ReplayInit(CreateInterfaceFn replayFactory) = 0;
	virtual bool ReplayPostInit() = 0;
	virtual void LevelInitPreEntity(char const* pMapName) = 0;
	virtual void LevelInitPostEntity() = 0;
	virtual void LevelShutdown(void) = 0;
	virtual ClientClass* GetAllClasses(void) = 0;
	virtual int HudVidInit(void) = 0;
	virtual void HudProcessInput(bool bActive) = 0;
	virtual void HudUpdate(bool bActive) = 0;
	virtual void HudReset(void) = 0;
	virtual void HudText(const char* message) = 0;
	virtual void IN_ActivateMouse(void) = 0;
	virtual void IN_DeactivateMouse(void) = 0;
	virtual void IN_Accumulate(void) = 0;
	virtual void IN_ClearStates(void) = 0;
	virtual bool IN_IsKeyDown(const char* name, bool& isdown) = 0;
	virtual void IN_OnMouseWheeled(int nDelta) = 0;
	virtual int IN_KeyEvent(int eventcode, ButtonCode_t keynum, const char* pszCurrentBinding) = 0;
	virtual void CreateMove(int sequence_number, float input_sample_frametime, bool active) = 0;
	virtual void ExtraMouseSample(float frametime, bool active) = 0;
	virtual bool WriteUsercmdDeltaToBuffer(bf_write* buf, int from, int to, bool isnewcommand) = 0;
	virtual void EncodeUserCmdToBuffer(bf_write& buf, int slot) = 0;
	virtual void DecodeUserCmdFromBuffer(bf_read& buf, int slot) = 0;
	virtual void View_Render(vrect_t* rect) = 0;
	virtual void RenderView(const CViewSetup& view, int nClearFlags, int whatToDraw) = 0;
	virtual void View_Fade(ScreenFade_t* pSF) = 0;
	virtual void SetCrosshairAngle(const QAngle& angle) = 0;
	virtual void InitSprite(CEngineSprite* pSprite, const char* loadname) = 0;
	virtual void ShutdownSprite(CEngineSprite* pSprite) = 0;
	virtual int GetSpriteSize(void) const = 0;
	virtual void VoiceStatus(int entindex, qboolean bTalking) = 0;
	virtual void InstallStringTableCallback(char const* tableName) = 0;
	virtual void FrameStageNotify(ClientFrameStage_t curStage) = 0;
	virtual bool DispatchUserMessage(int msg_type, bf_read& msg_data) = 0;
	virtual CSaveRestoreData* SaveInit(int size) = 0;
	virtual void SaveWriteFields(CSaveRestoreData*, const char*, void*, datamap_t*, typedescription_t*, int) = 0;
	virtual void SaveReadFields(CSaveRestoreData*, const char*, void*, datamap_t*, typedescription_t*, int) = 0;
	virtual void PreSave(CSaveRestoreData*) = 0;
	virtual void Save(CSaveRestoreData*) = 0;
	virtual void WriteSaveHeaders(CSaveRestoreData*) = 0;
	virtual void ReadRestoreHeaders(CSaveRestoreData*) = 0;
	virtual void Restore(CSaveRestoreData*, bool) = 0;
	virtual void DispatchOnRestore() = 0;
	virtual CStandardRecvProxies* GetStandardRecvProxies() = 0;
	virtual void WriteSaveGameScreenshot(const char* pFilename) = 0;
	virtual void EmitSentenceCloseCaption(char const* tokenstream) = 0;
	virtual void EmitCloseCaption(char const* captionname, float duration) = 0;
	virtual bool CanRecordDemo(char* errorMsg, int length) const = 0;
	virtual void OnDemoRecordStart(char const* pDemoBaseName) = 0;
	virtual void OnDemoRecordStop() = 0;
	virtual void OnDemoPlaybackStart(char const* pDemoBaseName) = 0;
	virtual void OnDemoPlaybackStop() = 0;
	virtual bool ShouldDrawDropdownConsole() = 0;
	virtual int GetScreenWidth() = 0;
	virtual int GetScreenHeight() = 0;
	virtual void WriteSaveGameScreenshotOfSize(const char* pFilename, int width, int height, bool bCreatePowerOf2Padded = false, bool bWriteVTF = false) = 0;
	virtual bool GetPlayerView(CViewSetup& playerView) = 0;
	virtual void SetupGameProperties(CUtlVector< XUSER_CONTEXT >& contexts, CUtlVector< XUSER_PROPERTY >& properties) = 0;
	virtual uint GetPresenceID(const char* pIDName) = 0;
	virtual const char* GetPropertyIdString(const uint id) = 0;
	virtual void GetPropertyDisplayString(uint id, uint value, char* pOutput, int nBytes) = 0;
	virtual void StartStatsReporting(HANDLE handle, bool bArbitrated) = 0;
	virtual void InvalidateMdlCache() = 0;
	virtual void IN_SetSampleTime(float frametime) = 0;
	virtual void ReloadFilesInList(IFileList* pFilesToReload) = 0;
	virtual bool HandleUiToggle() = 0;
	virtual bool ShouldAllowConsole() = 0;
	virtual CRenamedRecvTableInfo* GetRenamedRecvTableInfos() = 0;
	virtual CMouthInfo* GetClientUIMouthInfo() = 0;
	virtual void FileReceived(const char* fileName, unsigned int transferID) = 0;
	virtual const char* TranslateEffectForVisionFilter(const char* pchEffectType, const char* pchEffectName) = 0;
	virtual void ClientAdjustStartSoundParams(struct StartSoundParams_t& params) = 0;
	virtual bool DisconnectAttempt(void) = 0;
	virtual bool IsConnectedUserInfoChangeAllowed(IConVar* pCvar) = 0;
};

MAKE_INTERFACE_VERSION(IBaseClientDLL, Client, "client.dll", "VClient017");

// ================ SDK/Definitions/Main/IHandleEntity.h ================
class CBaseHandle;

class IHandleEntity
{
public:
	virtual ~IHandleEntity() {}
	virtual void SetRefEHandle(const CBaseHandle& handle) = 0;
	virtual const CBaseHandle& GetRefEHandle() const = 0;
};

// ================ SDK/Definitions/Main/CBaseHandle.h ================
class IHandleEntity;

class CBaseHandle
{
public:
	CBaseHandle();
	CBaseHandle(const CBaseHandle& other);
	CBaseHandle(unsigned long value);
	CBaseHandle(int iEntry, int iSerialNumber);
	void Init(int iEntry, int iSerialNumber);
	void Term();
	bool IsValid() const;
	int GetEntryIndex() const;
	int GetSerialNumber() const;
	int ToInt() const;
	bool operator !=(const CBaseHandle& other) const;
	bool operator ==(const CBaseHandle& other) const;
	bool operator ==(const IHandleEntity* pEnt) const;
	bool operator !=(const IHandleEntity* pEnt) const;
	bool operator <(const CBaseHandle& other) const;
	bool operator <(const IHandleEntity* pEnt) const;
	const CBaseHandle& operator=(const IHandleEntity* pEntity);
	const CBaseHandle& Set(const IHandleEntity* pEntity);
	IHandleEntity* Get() const;

protected:
	unsigned long m_Index;
};

inline CBaseHandle::CBaseHandle()
{
	m_Index = INVALID_EHANDLE_INDEX;
}

inline CBaseHandle::CBaseHandle(const CBaseHandle& other)
{
	m_Index = other.m_Index;
}

inline CBaseHandle::CBaseHandle(unsigned long value)
{
	m_Index = value;
}

inline CBaseHandle::CBaseHandle(int iEntry, int iSerialNumber)
{
	Init(iEntry, iSerialNumber);
}

inline void CBaseHandle::Init(int iEntry, int iSerialNumber)
{
	m_Index = iEntry | (iSerialNumber << NUM_SERIAL_NUM_SHIFT_BITS);
}

inline void CBaseHandle::Term()
{
	m_Index = INVALID_EHANDLE_INDEX;
}

inline bool CBaseHandle::IsValid() const
{
	return m_Index != INVALID_EHANDLE_INDEX;
}

inline int CBaseHandle::GetEntryIndex() const
{
	return m_Index & ENT_ENTRY_MASK;
}

inline int CBaseHandle::GetSerialNumber() const
{
	return m_Index >> NUM_SERIAL_NUM_SHIFT_BITS;
}

inline int CBaseHandle::ToInt() const
{
	return (int)m_Index;
}

inline bool CBaseHandle::operator !=(const CBaseHandle& other) const
{
	return m_Index != other.m_Index;
}

inline bool CBaseHandle::operator ==(const CBaseHandle& other) const
{
	return m_Index == other.m_Index;
}

inline bool CBaseHandle::operator ==(const IHandleEntity* pEnt) const
{
	return Get() == pEnt;
}

inline bool CBaseHandle::operator !=(const IHandleEntity* pEnt) const
{
	return Get() != pEnt;
}

inline bool CBaseHandle::operator <(const CBaseHandle& other) const
{
	return m_Index < other.m_Index;
}

inline bool CBaseHandle::operator <(const IHandleEntity* pEntity) const
{
	unsigned long otherIndex = (pEntity) ? pEntity->GetRefEHandle().m_Index : INVALID_EHANDLE_INDEX;
	return m_Index < otherIndex;
}

inline const CBaseHandle& CBaseHandle::operator=(const IHandleEntity* pEntity)
{
	return Set(pEntity);
}

inline const CBaseHandle& CBaseHandle::Set(const IHandleEntity* pEntity)
{
	if (pEntity)
	{
		*this = pEntity->GetRefEHandle();
	}
	else
	{
		m_Index = INVALID_EHANDLE_INDEX;
	}

	return *this;
}

template <class T>
class CHandle : public CBaseHandle
{
public:
	CHandle();
	CHandle(int iEntry, int iSerialNumber);
	CHandle(const CBaseHandle& handle);
	CHandle(T* pVal);

	static CHandle<T> FromIndex(int index);

	T* Get() const;
	void Set(const T* pVal);

	operator T* ();
	operator T* () const;

	bool               operator !() const;
	bool               operator==(T* val) const;
	bool               operator!=(T* val) const;
	const CBaseHandle& operator=(const T* val);

	T* operator->() const;
};

template <class T>
CHandle<T>::CHandle()
{
}

template <class T>
CHandle<T>::CHandle(int iEntry, int iSerialNumber)
{
	Init(iEntry, iSerialNumber);
}

template <class T>
CHandle<T>::CHandle(const CBaseHandle& handle) : CBaseHandle(handle)
{
}

template <class T>
CHandle<T>::CHandle(T* pObj)
{
	Term();
	Set(pObj);
}

template <class T>
CHandle<T> CHandle<T>::FromIndex(int index)
{
	CHandle<T> ret;
	ret.m_Index = index;
	return ret;
}

template <class T>
T* CHandle<T>::Get() const
{
	return reinterpret_cast<T*>(CBaseHandle::Get());
}

template <class T>
CHandle<T>::operator T* ()
{
	return Get();
}

template <class T>
CHandle<T>::operator T* () const
{
	return Get();
}

template <class T>
bool CHandle<T>::operator !() const
{
	return !Get();
}

template <class T>
bool CHandle<T>::operator==(T* val) const
{
	return Get() == val;
}

template <class T>
bool CHandle<T>::operator!=(T* val) const
{
	return Get() != val;
}

template <class T>
void CHandle<T>::Set(const T* pVal)
{
	CBaseHandle::Set(reinterpret_cast<const IHandleEntity*>(pVal));
}

template <class T>
const CBaseHandle& CHandle<T>::operator=(const T* val)
{
	Set(val);
	return *this;
}

template <class T>
T* CHandle<T>::operator ->() const
{
	return Get();
}

// ================ SDK/Definitions/Misc/IConVar.h ================
class IConVar;
class CCommand;

#define FCVAR_DEVELOPMENTONLY (1<<1)
#define FCVAR_HIDDEN (1<<4)
#define FCVAR_CHEAT (1<<14)
#define FCVAR_NEVER_AS_STRING (1<<12)
#define FCVAR_NOT_CONNECTED (1<<22)

typedef void (*FnChangeCallback_t)(IConVar* var, const char* pOldValue, float flOldValue);

class IConVar
{
public:
	virtual void SetValue(const char* pValue) = 0;
	virtual void SetValue(float flValue) = 0;
	virtual void SetValue(int nValue) = 0;
	virtual const char* GetName(void) const = 0;
	virtual bool IsFlagSet(int nFlag) const = 0;
};

// ================ SDK/Definitions/Misc/ConVar.h ================
class ConVar;
class CCommand;
class ConCommand;
class ConCommandBase;

class IConCommandBaseAccessor
{
public:
	virtual bool RegisterConCommandBase(ConCommandBase* pVar) = 0;
};

typedef void (*FnCommandCallback_t)(const CCommand& command);

class ConCommandBase
{
public:
	virtual ~ConCommandBase(void) = 0;
	virtual	bool IsCommand(void) const = 0;
	virtual bool IsFlagSet(int flag) const = 0;
	virtual void AddFlags(int flags) = 0;
	virtual const char* GetName(void) const = 0;
	virtual const char* GetHelpText(void) const = 0;
	virtual bool IsRegistered(void) const = 0;
	virtual void* GetDLLIdentifier() const = 0;
	virtual void CreateBase(const char* pName, const char* pHelpString = 0, int flags = 0) = 0;
	virtual void Init() = 0;

public:
	ConCommandBase* m_pNext;
	bool m_bRegistered;
	const char* m_pszName;
	const char* m_pszHelpString;
	int m_nFlags;

protected:
	static ConCommandBase* s_pConCommandBases;
	static IConCommandBaseAccessor* s_pAccessor;
};

enum
{
	COMMAND_MAX_ARGC = 64,
	COMMAND_MAX_LENGTH = 512
};

class CCommand
{
public:
	int ArgC() const;
	const char** ArgV() const;
	const char* ArgS() const;
	const char* GetCommandString() const;
	const char* operator[](int nIndex) const;
	const char* Arg(int nIndex) const;

	int m_nArgc;
	int m_nArgv0Size;
	char m_pArgSBuffer[COMMAND_MAX_LENGTH];
	char m_pArgvBuffer[COMMAND_MAX_LENGTH];
	const char* m_ppArgv[COMMAND_MAX_ARGC];
};
inline int CCommand::ArgC() const
{
	return m_nArgc;
}
inline const char** CCommand::ArgV() const
{
	return m_nArgc ? (const char**)m_ppArgv : NULL;
}
inline const char* CCommand::ArgS() const
{
	return m_nArgv0Size ? &m_pArgSBuffer[m_nArgv0Size] : "";
}
inline const char* CCommand::GetCommandString() const
{
	return m_nArgc ? m_pArgSBuffer : "";
}
inline const char* CCommand::operator[](int nIndex) const
{
	return Arg(nIndex);
}
inline const char* CCommand::Arg(int nIndex) const
{
	if (nIndex < 0 || nIndex >= m_nArgc)
		return "";
	return m_ppArgv[nIndex];
}

class ConCommand;   // body dropped: only ever named through a pointer here

class ConVar : public ConCommandBase, public IConVar
{
public:
	virtual ~ConVar(void) {};
	virtual bool IsFlagSet(int flag) const = 0;
	virtual const char* GetHelpText(void) const = 0;
	virtual bool IsRegistered(void) const = 0;
	virtual const char* GetName(void) const = 0;
	virtual void AddFlags(int flags) = 0;
	virtual	bool IsCommand(void) const = 0;

	inline float GetFloat(void) const;
	inline int GetInt(void) const;
	inline bool GetBool() const { return !!GetInt(); }
	inline char const* GetString(void) const;

	virtual void SetValue(const char* value) = 0;
	virtual void SetValue(float value) = 0;
	virtual void SetValue(int value) = 0;

private:
	virtual void InternalSetValue(const char* value) = 0;
	virtual void InternalSetFloatValue(float fNewValue, bool bForce = false) = 0;
	virtual void InternalSetIntValue(int nValue) = 0;
	virtual bool ClampValue(float& value) = 0;
	virtual void ChangeStringValue(const char* tempVal, float flOldValue) = 0;
	virtual void Init() = 0;

	int GetFlags() { return m_pParent->m_nFlags; }

public:
	ConVar* m_pParent;
	const char* m_pszDefaultValue;
	char* m_pszString;
	int m_StringLength;
	float m_fValue;
	int m_nValue;
	bool m_bHasMin;
	float m_fMinVal;
	bool m_bHasMax;
	float m_fMaxVal;
	bool m_bHasCompMin;
	float m_fCompMinVal;
	bool m_bHasCompMax;
	float m_fCompMaxVal;
	bool m_bCompetitiveRestrictions;
	FnChangeCallback_t m_fnChangeCallback;
};

inline float ConVar::GetFloat(void) const
{
	return m_pParent->m_fValue;
}

inline int ConVar::GetInt(void) const
{
	return m_pParent->m_nValue;
}

inline const char* ConVar::GetString(void) const
{
	if (m_nFlags & FCVAR_NEVER_AS_STRING)
		return "FCVAR_NEVER_AS_STRING";

	return (m_pParent->m_pszString) ? m_pParent->m_pszString : "";
}

// ================ SDK/Definitions/Interfaces/IVEngineClient.h ================
struct model_t;
class CSentence;
class IMaterial;
class CAudioSource;
class SurfInfo;
class ISpatialQuery;
class IMaterialSystem;
class CPhysCollide;
class INetChannelInfo;
struct client_textmessage_t;
class IAchievementMgr;
class CGamestatsData;
class KeyValues;

typedef struct player_info_s
{
	char name[MAX_PLAYER_NAME_LENGTH];
	int userID;
	char guid[SIGNED_GUID_LEN + 1];
	uint32 friendsID;
	char friendsName[MAX_PLAYER_NAME_LENGTH];
	bool fakeplayer;
	bool ishltv;
	bool isreplay;
	CRC32_t customFiles[MAX_CUSTOM_FILES];
	unsigned char filesDownloaded;
} player_info_t;

struct AudioState_t
{
	Vector m_Origin;
	QAngle m_Angles;
	bool m_bIsUnderwater;
};

enum SkyboxVisibility_t
{
	SKYBOX_NOT_VISIBLE = 0,
	SKYBOX_3DSKYBOX_VISIBLE,
	SKYBOX_2DSKYBOX_VISIBLE,
};

struct OcclusionParams_t
{
	float m_flMaxOccludeeArea;
	float m_flMinOccluderArea;
};

class IVEngineClient
{
public:
	virtual int GetIntersectingSurfaces(const model_t* model, const Vector& vCenter, const float radius, const bool bOnlyVisibleSurfaces, SurfInfo* pInfos, const int nMaxInfos) = 0;
	virtual Vector GetLightForPoint(const Vector& pos, bool bClamp) = 0;
	virtual IMaterial* TraceLineMaterialAndLighting(const Vector& start, const Vector& end, Vector& diffuseLightColor, Vector& baseColor) = 0;
	virtual const char* ParseFile(const char* data, char* token, int maxlen) = 0;
	virtual bool CopyLocalFile(const char* source, const char* destination) = 0;
	virtual void GetScreenSize(int& width, int& height) = 0;
	virtual void ServerCmd(const char* szCmdString, bool bReliable = true) = 0;
	virtual void ClientCmd(const char* szCmdString) = 0;
	virtual bool GetPlayerInfo(int ent_num, player_info_t* pinfo) = 0;
	virtual int GetPlayerForUserID(int userID) = 0;
	virtual client_textmessage_t* TextMessageGet(const char* pName) = 0;
	virtual bool Con_IsVisible(void) = 0;
	virtual int GetLocalPlayer(void) = 0;
	virtual const model_t* LoadModel(const char* pName, bool bProp = false) = 0;
	virtual float Time(void) = 0;
	virtual float GetLastTimeStamp(void) = 0;
	virtual CSentence* GetSentence(CAudioSource* pAudioSource) = 0;
	virtual float GetSentenceLength(CAudioSource* pAudioSource) = 0;
	virtual bool IsStreaming(CAudioSource* pAudioSource) const = 0;
	virtual void GetViewAngles(QAngle& va) = 0;
	inline Vec3 GetViewAngles() { QAngle vOut; GetViewAngles(vOut); return vOut; }
	virtual void SetViewAngles(QAngle& va) = 0;
	virtual int GetMaxClients(void) = 0;
	virtual	const char* Key_LookupBinding(const char* pBinding) = 0;
	virtual const char* Key_BindingForKey(ButtonCode_t code) = 0;
	virtual void StartKeyTrapMode(void) = 0;
	virtual bool CheckDoneKeyTrapping(ButtonCode_t& code) = 0;
	virtual bool IsInGame(void) = 0;
	virtual bool IsConnected(void) = 0;
	virtual bool IsDrawingLoadingImage(void) = 0;
	virtual void Con_NPrintf(int pos, const char* fmt, ...) = 0;
	virtual void Con_NXPrintf(const struct con_nprint_s* info, const char* fmt, ...) = 0;
	virtual int IsBoxVisible(const Vector& mins, const Vector& maxs) = 0;
	virtual int IsBoxInViewCluster(const Vector& mins, const Vector& maxs) = 0;
	virtual bool CullBox(const Vector& mins, const Vector& maxs) = 0;
	virtual void Sound_ExtraUpdate(void) = 0;
	virtual const char* GetGameDirectory(void) = 0;
	virtual const VMatrix& WorldToScreenMatrix() = 0;
	virtual const VMatrix& WorldToViewMatrix() = 0;
	virtual int GameLumpVersion(int lumpId) const = 0;
	virtual int GameLumpSize(int lumpId) const = 0;
	virtual bool LoadGameLump(int lumpId, void* pBuffer, int size) = 0;
	virtual int LevelLeafCount() const = 0;
	virtual ISpatialQuery* GetBSPTreeQuery() = 0;
	virtual void LinearToGamma(float* linear, float* gamma) = 0;
	virtual float LightStyleValue(int style) = 0;
	virtual void ComputeDynamicLighting(const Vector& pt, const Vector* pNormal, Vector& color) = 0;
	virtual void GetAmbientLightColor(Vector& color) = 0;
	virtual int GetDXSupportLevel() = 0;
	virtual bool SupportsHDR() = 0;
	virtual void Mat_Stub(IMaterialSystem* pMatSys) = 0;
	virtual void GetChapterName(char* pchBuff, int iMaxLength) = 0;
	virtual char const* GetLevelName(void) = 0;
	virtual int	GetLevelVersion(void) = 0;
	virtual struct IVoiceTweak_s* GetVoiceTweakAPI(void) = 0;
	virtual void EngineStats_BeginFrame(void) = 0;
	virtual void EngineStats_EndFrame(void) = 0;
	virtual void FireEvents() = 0;
	virtual int GetLeavesArea(int* pLeaves, int nLeaves) = 0;
	virtual bool DoesBoxTouchAreaFrustum(const Vector& mins, const Vector& maxs, int iArea) = 0;
	virtual void SetAudioState(const AudioState_t& state) = 0;
	virtual int SentenceGroupPick(int groupIndex, char* name, int nameBufLen) = 0;
	virtual int SentenceGroupPickSequential(int groupIndex, char* name, int nameBufLen, int sentenceIndex, int reset) = 0;
	virtual int SentenceIndexFromName(const char* pSentenceName) = 0;
	virtual const char* SentenceNameFromIndex(int sentenceIndex) = 0;
	virtual int SentenceGroupIndexFromName(const char* pGroupName) = 0;
	virtual const char* SentenceGroupNameFromIndex(int groupIndex) = 0;
	virtual float SentenceLength(int sentenceIndex) = 0;
	virtual void ComputeLighting(const Vector& pt, const Vector* pNormal, bool bClamp, Vector& color, Vector* pBoxColors = NULL) = 0;
	virtual void ActivateOccluder(int nOccluderIndex, bool bActive) = 0;
	virtual bool IsOccluded(const Vector& vecAbsMins, const Vector& vecAbsMaxs) = 0;
	virtual void* SaveAllocMemory(size_t num, size_t size) = 0;
	virtual void SaveFreeMemory(void* pSaveMem) = 0;
	virtual INetChannelInfo* GetNetChannelInfo(void) = 0;
	virtual void DebugDrawPhysCollide(const CPhysCollide* pCollide, IMaterial* pMaterial, matrix3x4& transform, const color32& color) = 0;
	virtual void CheckPoint(const char* pName) = 0;
	virtual void DrawPortals() = 0;
	virtual bool IsPlayingDemo(void) = 0;
	virtual bool IsRecordingDemo(void) = 0;
	virtual bool IsPlayingTimeDemo(void) = 0;
	virtual int GetDemoRecordingTick(void) = 0;
	virtual int GetDemoPlaybackTick(void) = 0;
	virtual int GetDemoPlaybackStartTick(void) = 0;
	virtual float GetDemoPlaybackTimeScale(void) = 0;
	virtual int GetDemoPlaybackTotalTicks(void) = 0;
	virtual bool IsPaused(void) = 0;
	virtual bool IsTakingScreenshot(void) = 0;
	virtual bool IsHLTV(void) = 0;
	virtual bool IsLevelMainMenuBackground(void) = 0;
	virtual void GetMainMenuBackgroundName(char* dest, int destlen) = 0;
	virtual void GetVideoModes(int& nCount, vmode_s*& pModes) = 0;
	virtual void SetOcclusionParameters(const OcclusionParams_t& params) = 0;
	virtual void GetUILanguage(char* dest, int destlen) = 0;
	virtual SkyboxVisibility_t IsSkyboxVisibleFromPoint(const Vector& vecPoint) = 0;
	virtual const char* GetMapEntitiesString() = 0;
	virtual bool IsInEditMode(void) = 0;
	virtual float GetScreenAspectRatio() = 0;
	virtual bool REMOVED_SteamRefreshLogin(const char* password, bool isSecure) = 0;
	virtual bool REMOVED_SteamProcessCall(bool& finished) = 0;
	virtual unsigned int GetEngineBuildNumber() = 0;
	virtual const char* GetProductVersionString() = 0;
	virtual void GrabPreColorCorrectedFrame(int x, int y, int width, int height) = 0;
	virtual bool IsHammerRunning() const = 0;
	virtual void ExecuteClientCmd(const char* szCmdString) = 0;
	virtual bool MapHasHDRLighting(void) = 0;
	virtual int	GetAppID() = 0;
	virtual Vector GetLightForPointFast(const Vector& pos, bool bClamp) = 0;
	virtual void ClientCmd_Unrestricted(const char* szCmdString) = 0;
	virtual void SetRestrictServerCommands(bool bRestrict) = 0;
	virtual void SetRestrictClientCommands(bool bRestrict) = 0;
	virtual void SetOverlayBindProxy(int iOverlayID, void* pBindProxy) = 0;
	virtual bool CopyFrameBufferToMaterial(const char* pMaterialName) = 0;
	virtual void ChangeTeam(const char* pTeamName) = 0;
	virtual void ReadConfiguration(const bool readDefault = false) = 0;
	virtual void SetAchievementMgr(IAchievementMgr* pAchievementMgr) = 0;
	virtual IAchievementMgr* GetAchievementMgr() = 0;
	virtual bool MapLoadFailed(void) = 0;
	virtual void SetMapLoadFailed(bool bState) = 0;
	virtual bool IsLowViolence() = 0;
	virtual const char* GetMostRecentSaveGame(void) = 0;
	virtual void SetMostRecentSaveGame(const char* lpszFilename) = 0;
	virtual void StartXboxExitingProcess() = 0;
	virtual bool IsSaveInProgress() = 0;
	virtual uint OnStorageDeviceAttached(void) = 0;
	virtual void OnStorageDeviceDetached(void) = 0;
	virtual void ResetDemoInterpolation(void) = 0;
	virtual void SetGamestatsData(CGamestatsData* pGamestatsData) = 0;
	virtual CGamestatsData* GetGamestatsData() = 0;
	virtual void ServerCmdKeyValues(KeyValues* pKeyValues) = 0;
	virtual bool IsSkippingPlayback(void) = 0;
	virtual bool IsLoadingDemo(void) = 0;
	virtual bool IsPlayingDemoALocallyRecordedDemo() = 0;
	virtual	const char* Key_LookupBindingExact(const char* pBinding) = 0;
	virtual void AddPhonemeFile(const char* pszPhonemeFile) = 0;
	virtual float GetPausedExpireTime(void) = 0;
	virtual bool StartDemoRecording(const char* pszFilename, const char* pszFolder = NULL) = 0;
	virtual void StopDemoRecording(void) = 0;
	virtual void TakeScreenshot(const char* pszFilename, const char* pszFolder = NULL) = 0;
};

MAKE_INTERFACE_VERSION(IVEngineClient, EngineClient, "engine.dll", "VEngineClient014");

// ================ SDK/Definitions/Misc/IMatchGroupDescription.h ================

enum EMatchMode
{
	eMatchMode_Invalid,
	eMatchMode_MatchMaker_CompleteFromQueue,
	eMatchMode_MatchMaker_LateJoinDropIn,
	eMatchMode_MatchMaker_LateJoinMatchBased,
	eMatchMode_Manual_NewMatch,
	eMatchMode_Manual_ExistingMatchBased
};

enum EMMPenaltyPool
{
	eMMPenaltyPool_Invalid = -1,
	eMMPenaltyPool_Casual = 0,
	eMMPenaltyPool_Ranked,
	eMMPenaltyPool_Count
};

enum EMatchType_t
{
	MATCH_TYPE_NONE = 0,
	MATCH_TYPE_MVM,
	MATCH_TYPE_COMPETITIVE,
	MATCH_TYPE_CASUAL
};

enum EMMRating
{
	k_nMMRating_LowestValue = -1,
	k_nMMRating_Invalid = -1,
	k_nMMRating_First = 0,
	k_nMMRating_6v6_DRILLO = 0,
	k_nMMRating_6v6_DRILLO_PlayerAcknowledged = 1,
	k_nMMRating_6v6_GLICKO = 2,
	k_nMMRating_12v12_DRILLO = 3,
	k_nMMRating_Casual_12v12_GLICKO = 4,
	k_nMMRating_Casual_XP = 5,
	k_nMMRating_Casual_XP_PlayerAcknowledged = 6,
	k_nMMRating_6v6_Rank = 7,
	k_nMMRating_6v6_Rank_PlayerAcknowledged = 8,
	k_nMMRating_Casual_12v12_Rank = 9,
	k_nMMRating_Casual_12v12_Rank_PlayerAcknowledged = 10,
	k_nMMRating_6v6_GLICKO_PlayerAcknowledged = 11,
	k_nMMRating_Comp_12v12_Rank = 12,
	k_nMMRating_Comp_12v12_Rank_PlayerAcknowledged = 13,
	k_nMMRating_Comp_12v12_GLICKO = 14,
	k_nMMRating_Comp_12v12_GLICKO_PlayerAcknowledged = 15,
	k_nMMRating_Last = 15,
};

enum EMatchGroupLeaderboard
{
	k_eMatchGroupLeaderboard_Invalid = -1,
	k_eMatchGroupLeaderboard_Ladder6v6 = 0,
	k_eMatchGroupLeaderboard_Casual12v12,
	k_eMatchGroupLeaderboard_Count
};

class IProgressionDesc;   // body dropped: only ever named through a pointer here

class IMatchGroupDescription
{
public:
public:
	byte pad0[8];
	const ETFMatchGroup m_eMatchGroup;
	const IProgressionDesc* m_pProgressionDesc;
	bool m_bActive;
	EMatchMode m_eLateJoinMode;
	EMMPenaltyPool m_ePenaltyPool;
	bool m_bUsesSkillRatings;
	bool m_bSupportsLowPriorityQueue;
	const ConVar* m_pmm_required_score;
	bool m_bUseMatchHud;
	const char* m_pszExecFileName;
	const ConVar* m_pmm_match_group_size;
	const ConVar* m_pmm_match_group_size_minimum;
	bool m_bShowPreRoundDoors;
	bool m_bShowPostRoundDoors;
	const char* m_pszMatchEndKickWarning;
	const char* m_pszMatchStartSound;
	bool m_bAutoReady;
	bool m_bUseMatchSummaryStage;
	bool m_bDistributePerformanceMedals;
	bool m_bUseFirstBlood;
	bool m_bUseReducedBonusTime;
	bool m_bUseAutoBalance;
	bool m_bAllowTeamChange;
	bool m_bRandomWeaponCrits;
	bool m_bFixedWeaponSpread;
	bool m_bRequireCompleteMatch;
	bool m_bTrustedServersOnly;
	bool m_bForceClientSettings;
	bool m_bAllowDrawingAtMatchSummary;
	bool m_bAllowSpecModeChange;
	bool m_bAutomaticallyRequeueAfterMatchEnds;
	bool m_bUsesMapVoteOnRoundEnd;
	bool m_bScramblesTeamsOnRollingMatch;
	bool m_bUsesXP;
	bool m_bUsesDashboardOnRoundEnd;
	bool m_bUsesSurveys;
	bool m_bStrictMatchmakerScoring;
	const char* m_pszModeNameLocToken;
	const char* m_pszRichPresenceLocToken;
	bool m_bAllowPartyJoins;
	bool m_bAllowPartySpectate;
	bool m_bContractProgressAllowed;
	EMatchType_t m_eMatchType;
	int m_nNumWinsToExitPlacement;
	int m_nNumPlacementMatchesPerDay;
	EMMRating m_eCurrentDisplayRating;
	EMMRating m_eLastAckdDisplayRating;
	EMMRating m_eCurrentDisplayRank;
	EMMRating m_eLastAckdDisplayRank;
	bool m_bUsesStickyRanks;
	bool m_bUsesStrictAbandons;
	bool m_bRequiresCompetitiveAccess;
	EMatchGroupLeaderboard  m_eLeaderboard;
	bool m_bUsesStrictSpectatorRules;
};

// ================ SDK/Definitions/Interfaces/CTFGameRules.h ================
MAKE_SIGNATURE(TFGameRules, "client.dll", "48 8B 0D ? ? ? ? 4C 8B C3 48 8B D7 48 8B 01 FF 90 ? ? ? ? 84 C0", 0x0);
// CSS_STUB tf2 api: CTFGameRules_GetCurrentMatchGroup / GetMatchGroupDescription sigs
// removed (MISSING in CSS, sig_check.md) -- TF2 matchmaking (comp/mvm/etc queue groups),
// no CSS equivalent. CTFGameRules::GetCurrentMatchGroup()/GetMatchGroupDescription()
// below stubbed; no other caller in this unit's files.

class CBaseEntity;
typedef CHandle<CBaseEntity> EHANDLE;

class CTeamplayRules
{
public:
};

class CTeamplayRoundBasedRules : public CTeamplayRules
{
public:
	NETVAR(m_iRoundState, int, "CTeamplayRoundBasedRulesProxy", "m_iRoundState");
	NETVAR(m_iWinningTeam, int, "CTeamplayRoundBasedRulesProxy", "m_iWinningTeam");
};

class CTFGameRules : public CTeamplayRoundBasedRules
{
public:
	NETVAR(m_bPlayingMannVsMachine, bool, "CTFGameRulesProxy", "m_bPlayingMannVsMachine");
	NETVAR(m_bTruceActive, bool, "CTFGameRulesProxy", "m_bTruceActive");

	VIRTUAL(GetViewVectors, CViewVectors*, 31, this);

	// CSS_STUB tf2 api: no match-group concept in CSS.
	inline int GetCurrentMatchGroup() { return 0; }
	inline IMatchGroupDescription* GetMatchGroupDescription() { return nullptr; }
};

namespace I
{
	inline CTFGameRules* TFGameRules()
	{
		return *reinterpret_cast<CTFGameRules**>(U::Memory.RelToAbs(S::TFGameRules()));
	}
};

// ================ SDK/Definitions/Interfaces/IClientEntityList.h ================
class IClientEntity;
class ClientClass;
class IClientNetworkable;
class IClientUnknown;

class IClientEntityList
{
public:
	virtual IClientNetworkable* GetClientNetworkable(int entnum) = 0;
	virtual IClientNetworkable* GetClientNetworkableFromHandle(CBaseHandle hEnt) = 0;
	virtual IClientUnknown* GetClientUnknownFromHandle(CBaseHandle hEnt) = 0;
	virtual IClientEntity* GetClientEntity(int entnum) = 0;
	virtual IClientEntity* GetClientEntityFromHandle(CBaseHandle hEnt) = 0;
	virtual int NumberOfEntities(bool bIncludeNonNetworkable) = 0;
	virtual int GetHighestEntityIndex(void) = 0;
	virtual void SetMaxEntities(int maxents) = 0;
	virtual int GetMaxEntities() = 0;
};

MAKE_INTERFACE_VERSION(IClientEntityList, ClientEntityList, "client.dll", "VClientEntityList003");

inline IHandleEntity* CBaseHandle::Get() const
{
	return reinterpret_cast<IHandleEntity*>(I::ClientEntityList->GetClientEntityFromHandle(m_Index));
}

// ================ SDK/Definitions/Interfaces/ICVar.h ================
class ConCommandBase;
class ConCommand;

typedef int CVarDLLIdentifier_t;

class IConsoleDisplayFunc
{
public:
	virtual void ColorPrint(const Color_t& clr, const char* pMessage) = 0;
	virtual void Print(const char* pMessage) = 0;
	virtual void DPrint(const char* pMessage) = 0;
};

class ICvarQuery : public IAppSystem
{
public:
	virtual bool AreConVarsLinkable(const ConVar* child, const ConVar* parent) = 0;
};

class ICvar : public IAppSystem
{
public:
	virtual CVarDLLIdentifier_t AllocateDLLIdentifier() = 0;
	virtual void RegisterConCommand(ConCommandBase* pCommandBase) = 0;
	virtual void UnregisterConCommand(ConCommandBase* pCommandBase) = 0;
	virtual void UnregisterConCommands(CVarDLLIdentifier_t id) = 0;
	virtual const char* GetCommandLineValue(const char* pVariableName) = 0;
	virtual ConCommandBase* FindCommandBase(const char* name) = 0;
	virtual const ConCommandBase* FindCommandBase(const char* name) const = 0;
	virtual ConVar* FindVar(const char* var_name) = 0;
	virtual const ConVar* FindVar(const char* var_name) const = 0;
	virtual ConCommand* FindCommand(const char* name) = 0;
	virtual const ConCommand* FindCommand(const char* name) const = 0;
	virtual ConCommandBase* GetCommands(void) = 0;
	virtual const ConCommandBase* GetCommands(void) const = 0;
	virtual void InstallGlobalChangeCallback(FnChangeCallback_t callback) = 0;
	virtual void RemoveGlobalChangeCallback(FnChangeCallback_t callback) = 0;
	virtual void CallGlobalChangeCallbacks(ConVar* var, const char* pOldString, float flOldValue) = 0;
	virtual void InstallConsoleDisplayFunc(IConsoleDisplayFunc* pDisplayFunc) = 0;
	virtual void RemoveConsoleDisplayFunc(IConsoleDisplayFunc* pDisplayFunc) = 0;
	virtual void ConsoleColorPrintf(const Color_t& clr, const char* pFormat, ...) const = 0;
	virtual void ConsolePrintf(const char* pFormat, ...) const = 0;
	virtual void ConsoleDPrintf(const char* pFormat, ...) const = 0;
	virtual void RevertFlaggedConVars(int nFlag) = 0;
	virtual void InstallCVarQuery(ICvarQuery* pQuery) = 0;
	virtual bool IsMaterialThreadSetAllowed() const = 0;
	virtual void QueueMaterialThreadSetValue(ConVar* pConVar, const char* pValue) = 0;
	virtual void QueueMaterialThreadSetValue(ConVar* pConVar, int nValue) = 0;
	virtual void QueueMaterialThreadSetValue(ConVar* pConVar, float flValue) = 0;
	virtual bool HasQueuedMaterialThreadConVarSets() const = 0;
	virtual int ProcessQueuedMaterialThreadConVarSets() = 0;

	class ICVarIteratorInternal
	{
	public:
		virtual ~ICVarIteratorInternal() {}
		virtual void SetFirst(void) = 0;
		virtual void Next(void) = 0;
		virtual	bool IsValid(void) = 0;
		virtual ConCommandBase* Get(void) = 0;
	};

	class Iterator
	{
	public:
		inline Iterator(ICvar* icvar);
		inline ~Iterator(void);
		inline void SetFirst(void);
		inline void Next(void);
		inline bool IsValid(void);
		inline ConCommandBase* Get(void);
	private:
		ICVarIteratorInternal* m_pIter;
	};

	virtual ICVarIteratorInternal* FactoryInternalIterator(void) = 0;
};

inline ICvar::Iterator::Iterator(ICvar* icvar)
{
	m_pIter = icvar->FactoryInternalIterator();
}

inline ICvar::Iterator::~Iterator(void)
{
	//delete m_pIter;
}

inline void ICvar::Iterator::SetFirst(void)
{
	m_pIter->SetFirst();
}

inline void ICvar::Iterator::Next(void)
{
	m_pIter->Next();
}

inline bool ICvar::Iterator::IsValid(void)
{
	return m_pIter->IsValid();
}

inline ConCommandBase* ICvar::Iterator::Get(void)
{
	return m_pIter->Get();
}

MAKE_INTERFACE_VERSION(ICvar, CVar, "vstdlib.dll", "VEngineCvar004");

// ================ SDK/Definitions/Main/CBaseTrace.h ================
#define DISPSURF_FLAG_SURFACE (1<<0)
#define DISPSURF_FLAG_WALKABLE (1<<1)
#define DISPSURF_FLAG_BUILDABLE (1<<2)
#define DISPSURF_FLAG_SURFPROP1 (1<<3)
#define DISPSURF_FLAG_SURFPROP2 (1<<4)

struct cplane_t
{
	Vector normal{};
	float dist{};
	byte type{};
	byte signbits{};
	byte pad[2]{};
};

class CBaseTrace
{
public:
	bool IsDispSurface(void) { return ((dispFlags & DISPSURF_FLAG_SURFACE) != 0); }
	bool IsDispSurfaceWalkable(void) { return ((dispFlags & DISPSURF_FLAG_WALKABLE) != 0); }
	bool IsDispSurfaceBuildable(void) { return ((dispFlags & DISPSURF_FLAG_BUILDABLE) != 0); }
	bool IsDispSurfaceProp1(void) { return ((dispFlags & DISPSURF_FLAG_SURFPROP1) != 0); }
	bool IsDispSurfaceProp2(void) { return ((dispFlags & DISPSURF_FLAG_SURFPROP2) != 0); }

public:
	Vector startpos{};
	Vector endpos{};
	cplane_t plane{};
	float fraction{};
	int contents{};
	unsigned short	dispFlags{};
	bool allsolid{};
	bool startsolid{};
};

// ================ SDK/Definitions/Misc/BSPFlags.h ================

#define	CONTENTS_SOLID			0x1		// an eye is never valid in a solid
#define	CONTENTS_WINDOW			0x2		// translucent, but not watery (glass)
#define	CONTENTS_GRATE			0x8		// alpha-tested "grate" textures.  Bullets/sight pass through, but solids don't
#define	CONTENTS_WATER			0x20

// unused 
// NOTE: If it's visible, grab from the top + update LAST_VISIBLE_CONTENTS
// if not visible, then grab from the bottom.

#define CONTENTS_TEAM1			0x800	// per team contents used to differentiate collisions 
#define CONTENTS_TEAM2			0x1000	// between players and objects on different teams
#define CONTENTS_REDTEAM		CONTENTS_TEAM1
#define CONTENTS_BLUETEAM		CONTENTS_TEAM2

// ignore CONTENTS_OPAQUE on surfaces that have SURF_NODRAW

// hits entities which are MOVETYPE_PUSH (doors, plats, etc.)
#define CONTENTS_MOVEABLE		0x4000

// remaining contents are non-visible, and don't eat brushes

#define	CONTENTS_PLAYERCLIP		0x10000

// currents can be added to any other contents, and may be mixed

#define	CONTENTS_MONSTER		0x2000000	// should never be on a brush, only in game
#define	CONTENTS_DEBRIS			0x4000000
#define CONTENTS_HITBOX			0x40000000	// use accurate hitboxes on trace
#define CONTENTS_NOSTARTSOLID	0x80000000	// don't skip entities or displacements when starting in solid

// NOTE: These are stored in a short in the engine now.  Don't use more than 16 bits
#define	SURF_SKY		0x0004		// don't draw, but add to skybox
#define	SURF_NODRAW		0x0080	// don't bother referencing the texture

// -----------------------------------------------------
// spatial content masks - used for spatial queries (traceline,etc.)
// -----------------------------------------------------
// everything that is normally solid
#define	MASK_SOLID					(CONTENTS_SOLID|CONTENTS_MOVEABLE|CONTENTS_WINDOW|CONTENTS_MONSTER|CONTENTS_GRATE)
// everything that blocks player movement
#define	MASK_PLAYERSOLID			(CONTENTS_SOLID|CONTENTS_MOVEABLE|CONTENTS_PLAYERCLIP|CONTENTS_WINDOW|CONTENTS_MONSTER|CONTENTS_GRATE)
// blocks npc movement
// water physics in these contents
// everything that blocks lighting
// everything that blocks lighting, but with monsters added.
// everything that blocks line of sight for AI
// everything that blocks line of sight for AI plus NPCs
// everything that blocks line of sight for players
// everything that blocks line of sight for players, but with monsters added.
// bullets see these as solid
#define	MASK_SHOT					(CONTENTS_SOLID|CONTENTS_MOVEABLE|CONTENTS_MONSTER|CONTENTS_WINDOW|CONTENTS_DEBRIS|CONTENTS_HITBOX)
// non-raycasted weapons see this as solid (includes grates)
// hits solids (not grates) and passes through everything else
// everything normally solid, except monsters (world+brush only)
// everything normally solid for player movement, except monsters (world+brush only)
// everything normally solid for npc movement, except monsters (world+brush only)
// just the world, used for route rebuilding
// These are things that can split areaportals

// UNDONE: This is untested, any moving water

// everything that blocks corpse movement
// UNDONE: Not used yet / may be deleted

// ================ SDK/Definitions/Misc/VCollide.h ================
class CPhysCollide;

struct vcollide_t
{
	unsigned short solidCount : 15;
	unsigned short isPacked : 1;
	unsigned short descSize;
	CPhysCollide** solids;
	char* pKeyValues;
};

// ================ SDK/Definitions/Main/CModel.h ================
struct model_t;

struct csurface_t
{
	const char* name;
	short surfaceProps;
	unsigned short	flags;
};

class __declspec(align(16))VectorAligned : public Vec3
{
public:
	inline VectorAligned(void) { };

	inline VectorAligned(float x, float y, float z) {
		Set(x, y, z);
	}

	explicit VectorAligned(const Vec3& othr) {
		Set(othr.x, othr.y, othr.z);
	}

	VectorAligned& operator=(const Vec3& othr) {
		Set(othr.x, othr.y, othr.z);
		return *this;
	}

	float w = 0.0f;
};

struct Ray_t
{
	VectorAligned m_Start{};
	VectorAligned m_Delta{};
	VectorAligned m_StartOffset{};
	VectorAligned m_Extents{};
	bool m_IsRay{};
	bool m_IsSwept{};

	void Init(Vector const& start, Vector const& end)
	{
		m_Delta = end - start;
		m_IsSwept = m_Delta.LengthSqr() != 0;
		m_Extents.Set();
		m_IsRay = true;
		m_StartOffset.Set();
		m_Start = start;
	}

	void Init(Vector const& start, Vector const& end, Vector const& mins, Vector const& maxs)
	{
		m_Delta = end - start;
		m_IsSwept = m_Delta.LengthSqr() != 0;
		m_Extents = maxs - mins;
		m_Extents *= 0.5f;
		m_IsRay = m_Extents.LengthSqr() == 0;
		m_StartOffset = mins + maxs;
		m_StartOffset *= 0.5f;
		m_Start = start + m_StartOffset;
		m_StartOffset *= -1.0f;
	}

};

// ================ SDK/Definitions/Interfaces/IEngineTrace.h ================
class CGameTrace;
typedef CGameTrace trace_t;
class ICollideable;
class CTraceListData;
class CPhysCollide;
struct cplane_t;

enum TraceType_t
{
	TRACE_EVERYTHING = 0,
	TRACE_WORLD_ONLY,
	TRACE_ENTITIES_ONLY,
	TRACE_EVERYTHING_FILTER_PROPS
};

class ITraceFilter
{
public:
	virtual bool ShouldHitEntity(IHandleEntity* pEntity, int contentsMask) = 0;
	virtual TraceType_t	GetTraceType() const = 0;
};

class IEntityEnumerator
{
public:
	virtual bool EnumEntity(IHandleEntity* pHandleEntity) = 0;
};

class IEngineTrace
{
public:
	virtual int	GetPointContents(const Vector& vecAbsPosition, IHandleEntity** ppEntity = NULL) = 0;
	virtual int	GetPointContents_Collideable(ICollideable* pCollide, const Vector& vecAbsPosition) = 0;
	virtual void ClipRayToEntity(const Ray_t& ray, unsigned int fMask, IHandleEntity* pEnt, trace_t* pTrace) = 0;
	virtual void ClipRayToCollideable(const Ray_t& ray, unsigned int fMask, ICollideable* pCollide, trace_t* pTrace) = 0;
	virtual void TraceRay(const Ray_t& ray, unsigned int fMask, ITraceFilter* pTraceFilter, trace_t* pTrace) = 0;
	virtual void SetupLeafAndEntityListRay(const Ray_t& ray, CTraceListData& traceData) = 0;
	virtual void SetupLeafAndEntityListBox(const Vector& vecBoxMin, const Vector& vecBoxMax, CTraceListData& traceData) = 0;
	virtual void TraceRayAgainstLeafAndEntityList(const Ray_t& ray, CTraceListData& traceData, unsigned int fMask, ITraceFilter* pTraceFilter, trace_t* pTrace) = 0;
	virtual void SweepCollideable(ICollideable* pCollide, const Vector& vecAbsStart, const Vector& vecAbsEnd, const QAngle& vecAngles, unsigned int fMask, ITraceFilter* pTraceFilter, trace_t* pTrace) = 0;
	virtual void EnumerateEntities(const Ray_t& ray, bool triggers, IEntityEnumerator* pEnumerator) = 0;
	virtual void EnumerateEntities(const Vector& vecAbsMins, const Vector& vecAbsMaxs, IEntityEnumerator* pEnumerator) = 0;
	virtual ICollideable* GetCollideable(IHandleEntity* pEntity) = 0;
	virtual int GetStatByIndex(int index, bool bClear) = 0;
	virtual void GetBrushesInAABB(const Vector& vMins, const Vector& vMaxs, CUtlVector<int>* pOutput, int iContentsMask = 0xFFFFFFFF) = 0;
	virtual CPhysCollide* GetCollidableFromDisplacementsInAABB(const Vector& vMins, const Vector& vMaxs) = 0;
	virtual bool GetBrushInfo(int iBrush, CUtlVector<Vector4D>* pPlanesOut, int* pContentsOut) = 0;
	virtual bool PointOutsideWorld(const Vector& ptTest) = 0;
	virtual int GetLeafContainingPoint(const Vector& ptTest) = 0;
};

MAKE_INTERFACE_VERSION(IEngineTrace, EngineTrace, "engine.dll", "EngineTraceClient003");

// ================ SDK/Definitions/Interfaces/IEngineVGui.h ================
enum VGuiPanel_t
{
	PANEL_ROOT = 0,
	PANEL_GAMEUIDLL,
	PANEL_CLIENTDLL,
	PANEL_TOOLS,
	PANEL_INGAMESCREENS,
	PANEL_GAMEDLL,
	PANEL_CLIENTDLL_TOOLS
};

enum PaintMode_t
{
	PAINT_UIPANELS = (1 << 0),
	PAINT_INGAMEPANELS = (1 << 1),
	PAINT_CURSOR = (1 << 2)
};

class IEngineVGui
{
public:
	virtual ~IEngineVGui(void) {}
	virtual VPANEL GetPanel(VGuiPanel_t type) = 0;
	virtual bool IsGameUIVisible() = 0;
};

MAKE_INTERFACE_VERSION(IEngineVGui, EngineVGui, "engine.dll", "VEngineVGui002");

// ================ SDK/Definitions/Main/MD5.h ================
#define MD5_DIGEST_LENGTH 16

struct MD5Value_t
{
	unsigned char bits[MD5_DIGEST_LENGTH];

	void Zero();
	bool IsZero() const;

	bool operator==(const MD5Value_t& src) const;
	bool operator!=(const MD5Value_t& src) const;

};

typedef struct
{
	unsigned int	buf[4];
	unsigned int	bits[2];
	unsigned char	in[64];
} MD5Context_t;

void MD5Init(MD5Context_t* context);
void MD5Update(MD5Context_t* context, unsigned char const* buf, unsigned int len);
void MD5Final(unsigned char digest[MD5_DIGEST_LENGTH], MD5Context_t* context);
unsigned int MD5_PseudoRandom(unsigned int nSeed);
bool MD5_Compare(const MD5Value_t& data, const MD5Value_t& compare);

inline bool MD5Value_t::operator==(const MD5Value_t& src) const {
	return MD5_Compare(*this, src);
}

inline bool MD5Value_t::operator!=(const MD5Value_t& src) const {
	return !MD5_Compare(*this, src);
}

// ================ SDK/Definitions/Interfaces/IFileSystem.h ================
typedef void* (*FSAllocFunc_t)(const char* pszFilename, unsigned nBytes);
typedef uint32_t PathTypeQuery_t;
typedef void (*FileSystemLoggingFunc_t)(const char* fileName, const char* accessType);

enum FSAsyncStatus_t
{
	FSASYNC_ERR_NOT_MINE = -8,
	FSASYNC_ERR_RETRY_LATER = -7,
	FSASYNC_ERR_ALIGNMENT = -6,
	FSASYNC_ERR_FAILURE = -5,
	FSASYNC_ERR_READING = -4,
	FSASYNC_ERR_NOMEMORY = -3,
	FSASYNC_ERR_UNKNOWNID = -2,
	FSASYNC_ERR_FILEOPEN = -1,
	FSASYNC_OK = 0,
	FSASYNC_STATUS_PENDING,
	FSASYNC_STATUS_INPROGRESS,
	FSASYNC_STATUS_ABORTED,
	FSASYNC_STATUS_UNSERVICED,
};

struct FileAsyncRequest_t;
class CUtlBuffer;
class KeyValues;
class IFileList;
typedef void (*FSAsyncCallbackFunc_t)(const FileAsyncRequest_t& request, int nBytesRead, FSAsyncStatus_t err);

class IBaseFileSystem;   // body dropped: only ever named through a pointer here

// ================ SDK/Definitions/Interfaces/IMoveHelper.h ================
class CGameTrace;
class IPhysicsSurfaceProps;
enum PLAYER_ANIM;

typedef CBaseHandle EntityHandle_t;

class IMoveHelper
{
public:
	virtual	char const* GetName(EntityHandle_t handle) const = 0;
	virtual void ResetTouchList(void) = 0;
	virtual bool AddToTouched(const CGameTrace& tr, const Vector& impactvelocity) = 0;
	virtual void ProcessImpacts(void) = 0;
	virtual void Con_NPrintf(int idx, char const* fmt, ...) = 0;
	virtual void StartSound(const Vector& origin, int channel, char const* sample, float volume, soundlevel_t soundlevel, int fFlags, int pitch) = 0;
	virtual void StartSound(const Vector& origin, const char* soundname) = 0;
	virtual void PlaybackEventFull(int flags, int clientindex, unsigned short eventindex, float delay, Vector& origin, Vector& angles, float fparam1, float fparam2, int iparam1, int iparam2, int bparam1, int bparam2) = 0;
	virtual bool PlayerFallingDamage(void) = 0;
	virtual void PlayerSetAnimation(PLAYER_ANIM playerAnim) = 0;
	virtual IPhysicsSurfaceProps* GetSurfaceProps(void) = 0;
	virtual bool IsWorldEntity(const CBaseHandle& handle) = 0;
	virtual void SetHost(CBasePlayer* host) = 0;

protected:
	virtual ~IMoveHelper() {}
};

MAKE_INTERFACE_SIGNATURE(IMoveHelper, MoveHelper, "client.dll", "48 8B 0D ? ? ? ? 48 8B 01 FF 50 ? 0F B7 D7", 0x0, 1);

// ================ SDK/Definitions/Interfaces/IGameMovement.h ================
class CTFPlayer;

class CMoveData
{
public:
	bool m_bFirstRunOfFunctions : 1;
	bool m_bGameCodeMovedPlayer : 1;
	EntityHandle_t m_nPlayerHandle;
	int m_nImpulseCommand;
	QAngle m_vecViewAngles;
	QAngle m_vecAbsViewAngles;
	int m_nButtons;
	int m_nOldButtons;
	float m_flForwardMove;
	float m_flOldForwardMove;
	float m_flSideMove;
	float m_flUpMove;
	float m_flMaxSpeed;
	float m_flClientMaxSpeed;
	Vector m_vecVelocity;
	QAngle m_vecAngles;
	QAngle m_vecOldAngles;
	float m_outStepHeight;
	Vector m_outWishVel;
	Vector m_outJumpVel;
	Vector m_vecConstraintCenter;
	float m_flConstraintRadius;
	float m_flConstraintWidth;
	float m_flConstraintSpeedFactor;
	Vector m_vecAbsOrigin;
};

class CBasePlayer;

class IGameMovement
{
public:
	virtual ~IGameMovement(void) {}

	virtual void	ProcessMovement(CBasePlayer* pPlayer, CMoveData* pMove) = 0;
	virtual void	StartTrackPredictionErrors(CBasePlayer* pPlayer) = 0;
	virtual void	FinishTrackPredictionErrors(CBasePlayer* pPlayer) = 0;
	virtual void	DiffPrint(char const* fmt, ...) = 0;
	virtual Vector	GetPlayerMins(bool ducked) const = 0;
	virtual Vector	GetPlayerMaxs(bool ducked) const = 0;
	virtual Vector	GetPlayerViewOffset(bool ducked) const = 0;
};

class CGameMovement : public IGameMovement
{
public:
	virtual			~CGameMovement(void) {}

	virtual void	ProcessMovement(CBasePlayer* pPlayer, CMoveData* pMove) = 0;
	virtual void	StartTrackPredictionErrors(CBasePlayer* pPlayer) = 0;
	virtual void	FinishTrackPredictionErrors(CBasePlayer* pPlayer) = 0;
	virtual void	DiffPrint(char const* fmt, ...) = 0;
	virtual Vector	GetPlayerMins(bool ducked) const = 0;
	virtual Vector	GetPlayerMaxs(bool ducked) const = 0;
	virtual Vector	GetPlayerViewOffset(bool ducked) const = 0;

	typedef enum
	{
		GROUND = 0,
		STUCK,
		LADDER
	} IntervalType_t;

	virtual void	TracePlayerBBox(const Vector& start, const Vector& end, unsigned int fMask, int collisionGroup, trace_t& pm) = 0;
	virtual void	TryTouchGround(const Vector& start, const Vector& end, const Vector& mins, const Vector& maxs, unsigned int fMask, int collisionGroup, trace_t& pm) = 0;
	virtual unsigned int	PlayerSolidMask(bool brushOnly = false) = 0;
	virtual void	PlayerMove(void) = 0;
	virtual float	CalcRoll(const QAngle& angles, const Vector& velocity, float rollangle, float rollspeed) = 0;
	virtual	void	DecayPunchAngle(void) = 0;
	virtual void	CheckWaterJump(void) = 0;
	virtual void	WaterMove(void) = 0;
	virtual void	AirAccelerate(Vector& wishdir, float wishspeed, float accel) = 0;
	virtual void	AirMove(void) = 0;
	virtual float	GetAirSpeedCap(void) = 0;
	virtual bool	CanAccelerate() = 0;
	virtual void	Accelerate(Vector& wishdir, float wishspeed, float accel) = 0;
	virtual void	WalkMove(void) = 0;
	virtual void	FullWalkMove() = 0;
	virtual void	OnJump(float fImpulse) = 0;
	virtual void	OnLand(float fVelocity) = 0;
	virtual void	OnTryPlayerMoveCollision(trace_t& tr) = 0;
	virtual Vector	GetPlayerMins(void) const = 0;
	virtual Vector	GetPlayerMaxs(void) const = 0;
	virtual int		GetCheckInterval(IntervalType_t type) = 0;
	virtual bool	CheckJumpButton(void) = 0;
	virtual void    FullTossMove(void) = 0;
	virtual void	FullLadderMove() = 0;
	virtual int		TryPlayerMove(Vector* pFirstDest = NULL, trace_t* pFirstTrace = NULL, float flSlideMultiplier = 0.f) = 0;
	virtual bool	LadderMove(void) = 0;
	virtual bool	OnLadder(trace_t& trace) = 0;
	virtual float	LadderDistance(void) const = 0;
	virtual unsigned int	LadderMask(void) const = 0;
	virtual float	ClimbSpeed(void) const = 0;
	virtual float	LadderLateralMultiplier(void) const = 0;
	virtual int		CheckStuck(void) = 0;
	virtual bool	CheckWater(void) = 0;
	virtual void	CategorizePosition(void) = 0;
	virtual void	CheckParameters(void) = 0;
	virtual	void	ReduceTimers(void) = 0;
	virtual void	CheckFalling(void) = 0;
	virtual void	PlayerRoughLandingEffects(float fvol) = 0;
	virtual void	Duck(void) = 0;
	virtual void	HandleDuckingSpeedCrop() = 0;
	virtual void	FinishUnDuck(void) = 0;
	virtual void	FinishDuck(void) = 0;
	virtual bool	CanUnduck() = 0;
	virtual CBaseHandle	TestPlayerPosition(const Vector& pos, int collisionGroup, trace_t& pm) = 0;
	virtual void	SetGroundEntity(trace_t* pm) = 0;
	virtual void	StepMove(Vector& vecDestination, trace_t& trace) = 0;
	virtual bool	GameHasLadders() const = 0;

	enum
	{
		MAX_PC_CACHE_SLOTS = 3,
	};

	CBasePlayer*	player;
	CMoveData*		mv;
	int				m_nOldWaterLevel;
	float			m_flWaterEntryTime;
	int				m_nOnLadder;
	Vector			m_vecForward;
	Vector			m_vecRight;
	Vector			m_vecUp;
	int				m_CachedGetPointContents[MAX_PLAYERS_ARRAY_SAFE][MAX_PC_CACHE_SLOTS];
	Vector			m_CachedGetPointContentsPoint[MAX_PLAYERS_ARRAY_SAFE][MAX_PC_CACHE_SLOTS];
	Vector			m_vecProximityMins;
	Vector			m_vecProximityMaxs;
	float			m_fFrameTime;
	int				m_iSpeedCropped;
	float			m_flStuckCheckTime[MAX_PLAYERS_ARRAY_SAFE][2];
};

class CTFGameMovement : public CGameMovement
{
public:
	virtual void	PlayerMove() = 0;
	virtual unsigned int	PlayerSolidMask(bool brushOnly = false) = 0;
	virtual void	ProcessMovement(CBasePlayer* pBasePlayer, CMoveData* pMove) = 0;
	virtual bool	CanAccelerate() = 0;
	virtual bool	CheckJumpButton() = 0;
	virtual int		CheckStuck(void) = 0;
	virtual bool	CheckWater(void) = 0;
	virtual void	WaterMove(void) = 0;
	virtual void	FullWalkMove() = 0;
	virtual void	WalkMove(void) = 0;
	virtual void	AirMove(void) = 0;
	virtual void	FullTossMove(void) = 0;
	virtual void	CategorizePosition(void) = 0;
	virtual void	CheckFalling(void) = 0;
	virtual void	Duck(void) = 0;
	virtual Vector	GetPlayerViewOffset(bool ducked) const = 0;
	virtual float	GetAirSpeedCap(void) = 0;
	virtual void	TracePlayerBBox(const Vector& start, const Vector& end, unsigned int fMask, int collisionGroup, trace_t& pm) = 0;
	virtual CBaseHandle	TestPlayerPosition(const Vector& pos, int collisionGroup, trace_t& pm) = 0;
	virtual void	StepMove(Vector& vecDestination, trace_t& trace) = 0;
	virtual bool	GameHasLadders() const = 0;
	virtual void	SetGroundEntity(trace_t* pm) = 0;
	virtual void	PlayerRoughLandingEffects(float fvol) = 0;
	virtual void	HandleDuckingSpeedCrop(void) = 0;
	virtual void	CheckWaterJump(void) = 0;

	Vector			m_vecWaterPoint;
	CTFPlayer*		m_pTFPlayer;
	bool			m_isPassingThroughEnemies;
};

MAKE_INTERFACE_VERSION(CTFGameMovement, GameMovement, "client.dll", "GameMovement001");

// ================ SDK/Definitions/Interfaces/IInput.h ================
struct kbutton_t;
struct CameraThirdData_t;

class CVerifiedUserCmd
{
public:
	CUserCmd m_cmd;
	CRC32_t m_crc;
};

class IInput
{
public:
	virtual	void Init_All(void) = 0;
	virtual void Shutdown_All(void) = 0;
	virtual int GetButtonBits(int) = 0;
	virtual void CreateMove(int sequence_number, float input_sample_frametime, bool active) = 0;
	virtual void ExtraMouseSample(float frametime, bool active) = 0;
	virtual bool WriteUsercmdDeltaToBuffer(bf_write* buf, int from, int to, bool isnewcommand) = 0;
	virtual void EncodeUserCmdToBuffer(bf_write& buf, int slot) = 0;
	virtual void DecodeUserCmdFromBuffer(bf_read& buf, int slot) = 0;
	virtual CUserCmd* GetUserCmd(int sequence_number) = 0;
	virtual void MakeWeaponSelection(void* weapon) = 0;
	virtual float KeyState(kbutton_t* key) = 0;
	virtual int KeyEvent(int eventcode, ButtonCode_t keynum, const char* pszCurrentBinding) = 0;
	virtual kbutton_t* FindKey(const char* name) = 0;
	virtual void ControllerCommands(void) = 0;
	virtual void Joystick_Advanced(void) = 0;
	virtual void Joystick_SetSampleTime(float frametime) = 0;
	virtual void IN_SetSampleTime(float frametime) = 0;
	virtual void AccumulateMouse(void) = 0;
	virtual void ActivateMouse(void) = 0;
	virtual void DeactivateMouse(void) = 0;
	virtual void ClearStates(void) = 0;
	virtual float GetLookSpring(void) = 0;
	virtual void GetFullscreenMousePos(int* mx, int* my, int* unclampedx = 0, int* unclampedy = 0) = 0;
	virtual void SetFullscreenMousePos(int mx, int my) = 0;
	virtual void ResetMouse(void) = 0;
	virtual	float GetLastForwardMove(void) = 0;
	virtual	float Joystick_GetForward(void) = 0;
	virtual	float Joystick_GetSide(void) = 0;
	virtual	float Joystick_GetPitch(void) = 0;
	virtual	float Joystick_GetYaw(void) = 0;
	virtual void CAM_Think(void) = 0;
	virtual int CAM_IsThirdPerson(void) = 0;
	virtual void CAM_ToThirdPerson(void) = 0;
	virtual void CAM_ToFirstPerson(void) = 0;
	virtual void CAM_StartMouseMove(void) = 0;
	virtual void CAM_EndMouseMove(void) = 0;
	virtual void CAM_StartDistance(void) = 0;
	virtual void CAM_EndDistance(void) = 0;
	virtual int CAM_InterceptingMouse(void) = 0;
	virtual void CAM_ToOrthographic() = 0;
	virtual	bool CAM_IsOrthographic() const = 0;
	virtual	void CAM_OrthographicSize(float& w, float& h) const = 0;
	virtual void SetPreferredGameActionSet(GameActionSet_t action_set) = 0;
	virtual GameActionSet_t GetPreferredGameActionSet() = 0;
	virtual void SetGameActionSetFlags(GameActionSetFlags_t action_set_flags) = 0;
	virtual void LevelInit(void) = 0;
	virtual void ClearInputButton(int bits) = 0;
	virtual	void CAM_SetCameraThirdData(CameraThirdData_t* pCameraData, const QAngle& vecCameraOffset) = 0;
	virtual void CAM_CameraThirdThink(void) = 0;
	virtual	bool EnableJoystickMode() = 0;
	virtual bool IsSteamControllerActive() = 0;

public:
	byte pad0[256];
	CUserCmd* m_pCommands;
	CVerifiedUserCmd* m_pVerifiedCommands;
};

MAKE_INTERFACE_SIGNATURE(IInput, Input, "client.dll", "48 8B 0D ? ? ? ? 48 8B 01 FF 90 ? ? ? ? 85 C0 0F 84 ? ? ? ? F3 0F 10 05", 0x0, 1);

// ================ SDK/Definitions/Misc/AnalogCode.h ================

// ================ SDK/Definitions/Interfaces/IInputSystem.h ================

// ================ SDK/Definitions/Interfaces/IKeyValuesSystem.h ================
typedef int HKeySymbol;
class IBaseFileSystem;
class KeyValues;

class IKeyValuesSystem
{
public:
	virtual void RegisterSizeofKeyValues(int size) = 0;
	virtual void* AllocKeyValuesMemory(int size) = 0;
	virtual void FreeKeyValuesMemory(void* pMem) = 0;
	virtual HKeySymbol GetSymbolForString(const char* name, bool bCreate = true) = 0;
	virtual const char* GetStringForSymbol(HKeySymbol symbol) = 0;
	virtual void AddKeyValuesToMemoryLeakList(void* pMem, HKeySymbol name) = 0;
	virtual void RemoveKeyValuesFromMemoryLeakList(void* pMem) = 0;
	virtual void AddFileKeyValuesToCache(const KeyValues* _kv, const char* resourceName, const char* pathID) = 0;
	virtual bool LoadFileKeyValuesFromCache(KeyValues* _outKv, const char* resourceName, const char* pathID, IBaseFileSystem* filesystem) const = 0;
	virtual void InvalidateCache() = 0;
	virtual void InvalidateCacheForFile(const char* resourceName, const char* pathID) = 0;
	virtual void SetKeyValuesExpressionSymbol(const char* name, bool bValue) = 0;
	virtual bool GetKeyValuesExpressionSymbol(const char* name) = 0;
	virtual HKeySymbol GetSymbolForStringCaseSensitive(HKeySymbol& hCaseInsensitiveSymbol, const char* name, bool bCreate = true) = 0;
};

MAKE_INTERFACE_NULL(IKeyValuesSystem, KeyValuesSystem);

// ================ SDK/Definitions/Misc/Deformations.h ================

// ================ SDK/Definitions/Misc/ImageFormat.h ================

//typedef enum _D3DFORMAT D3DFORMAT;

enum ImageFormat
{
	IMAGE_FORMAT_UNKNOWN = -1,
	IMAGE_FORMAT_RGBA8888 = 0,
	IMAGE_FORMAT_ABGR8888,
	IMAGE_FORMAT_RGB888,
	IMAGE_FORMAT_BGR888,
	IMAGE_FORMAT_RGB565,
	IMAGE_FORMAT_I8,
	IMAGE_FORMAT_IA88,
	IMAGE_FORMAT_P8,
	IMAGE_FORMAT_A8,
	IMAGE_FORMAT_RGB888_BLUESCREEN,
	IMAGE_FORMAT_BGR888_BLUESCREEN,
	IMAGE_FORMAT_ARGB8888,
	IMAGE_FORMAT_BGRA8888,
	IMAGE_FORMAT_DXT1,
	IMAGE_FORMAT_DXT3,
	IMAGE_FORMAT_DXT5,
	IMAGE_FORMAT_BGRX8888,
	IMAGE_FORMAT_BGR565,
	IMAGE_FORMAT_BGRX5551,
	IMAGE_FORMAT_BGRA4444,
	IMAGE_FORMAT_DXT1_ONEBITALPHA,
	IMAGE_FORMAT_BGRA5551,
	IMAGE_FORMAT_UV88,
	IMAGE_FORMAT_UVWQ8888,
	IMAGE_FORMAT_RGBA16161616F,
	IMAGE_FORMAT_RGBA16161616,
	IMAGE_FORMAT_UVLX8888,
	IMAGE_FORMAT_R32F,
	IMAGE_FORMAT_RGB323232F,
	IMAGE_FORMAT_RGBA32323232F,
	IMAGE_FORMAT_NV_DST16,
	IMAGE_FORMAT_NV_DST24,
	IMAGE_FORMAT_NV_INTZ,
	IMAGE_FORMAT_NV_RAWZ,
	IMAGE_FORMAT_ATI_DST16,
	IMAGE_FORMAT_ATI_DST24,
	IMAGE_FORMAT_NV_NULL,
	IMAGE_FORMAT_ATI2N,
	IMAGE_FORMAT_ATI1N,
	IMAGE_FORMAT_DXT1_RUNTIME,
	IMAGE_FORMAT_DXT5_RUNTIME,
	NUM_IMAGE_FORMATS
};

// ================ SDK/Definitions/Misc/IColorCorrectionSystem.h ================

// ================ SDK/Definitions/Misc/IMaterialSystemHardwareConfig.h ================

// ================ SDK/Definitions/Misc/IRefCounted.h ================
class IRefCounted
{
public:
	virtual int AddRef() = 0;
	virtual int Release() = 0;
};

// ================ SDK/Definitions/Misc/LightDesc.h ================
#pragma warning (disable : 26812)

// ================ SDK/Definitions/Misc/TextureGroupNames.h ================

// ================ SDK/Definitions/Misc/VTF.h ================
class CUtlBuffer;

// ================ SDK/Definitions/Interfaces/IMaterialSystem.h ================

class IMaterial;
class ITexture;
class KeyValues;
class IFileList;

typedef uint64 VertexFormat_t;

enum MaterialPropertyTypes_t
{
	MATERIAL_PROPERTY_NEEDS_LIGHTMAP = 0,
	MATERIAL_PROPERTY_OPACITY,
	MATERIAL_PROPERTY_REFLECTIVITY,
	MATERIAL_PROPERTY_NEEDS_BUMPED_LIGHTMAPS
};

typedef unsigned int MorphFormat_t;

enum
{
	MATERIAL_ADAPTER_NAME_LENGTH = 512
};

class ITexture;

class IMaterialSystem;   // body dropped: only ever named through a pointer here

// ================ SDK/Definitions/Misc/Cursor.h ================

typedef unsigned long HCursor;

// ================ SDK/Definitions/Misc/IImage.h ================
typedef unsigned long HTexture;

class IImage;   // body dropped: only ever named through a pointer here

// ================ SDK/Definitions/Misc/KeyCode.h ================
typedef ButtonCode_t KeyCode;

// ================ SDK/Definitions/Misc/MouseCode.h ================
typedef ButtonCode_t MouseCode;

// ================ SDK/Definitions/Misc/IHTML.h ================

class IHTML;   // body dropped: only ever named through a pointer here

class IHTMLEvents;   // body dropped: only ever named through a pointer here

// ================ SDK/Definitions/Misc/IVguiMatInfoVar.h ================
class IVguiMatInfoVar
{
public:
	virtual ~IVguiMatInfoVar() {}
	virtual int GetIntValue(void) const = 0;
	virtual void SetIntValue(int val) = 0;
};

// ================ SDK/Definitions/Misc/IVguiMatInfo.h ================
class IVguiMatInfo
{
public:
	virtual ~IVguiMatInfo() {}
	virtual IVguiMatInfoVar* FindVarFactory(const char* varName, bool* found) = 0;
	virtual int GetNumAnimationFrames() = 0;
};

// ================ SDK/Definitions/Misc/ISurface.h ================
#undef CreateFont
#undef PlaySound

struct Vertex_t
{
	Vector2D m_Position = {};
	Vector2D m_TexCoord = {};

	Vertex_t() {}

	Vertex_t(const Vector2D& pos, const Vector2D& coord = Vector2D(0.0f, 0.0f)) {
		m_Position = pos;
		m_TexCoord = coord;
	}

	void Init(const Vector2D& pos, const Vector2D& coord = Vector2D(0.0f, 0.0f)) {
		m_Position = pos;
		m_TexCoord = coord;
	}
};

enum FontDrawType_t
{
	FONT_DRAW_DEFAULT = 0,
	FONT_DRAW_NONADDITIVE,
	FONT_DRAW_ADDITIVE,
	FONT_DRAW_TYPE_COUNT = 2,
};

struct CharRenderInfo
{
	int x = 0, y = 0;
	Vertex_t* verts = nullptr;
	int textureId = 0;
	int abcA = 0;
	int abcB = 0;
	int abcC = 0;
	int fontTall = 0;
	HFont currentFont = {};
	FontDrawType_t drawType = {};
	wchar_t ch = {};
	bool valid = false;
	bool shouldclip = false;
};

struct IntRect
{
	int x0 = 0;
	int y0 = 0;
	int x1 = 0;
	int y1 = 0;
};

enum SurfaceFeature_e
{
	ANTIALIASED_FONTS = 1,
	DROPSHADOW_FONTS = 2,
	ESCAPE_KEY = 3,
	OPENING_NEW_HTML_WINDOWS = 4,
	FRAME_MINIMIZE_MAXIMIZE = 5,
	OUTLINE_FONTS = 6,
	DIRECT_HWND_RENDER = 7,
};

enum EFontFlags
{
	FONTFLAG_NONE,
	FONTFLAG_ITALIC = 0x001,
	FONTFLAG_UNDERLINE = 0x002,
	FONTFLAG_STRIKEOUT = 0x004,
	FONTFLAG_SYMBOL = 0x008,
	FONTFLAG_ANTIALIAS = 0x010,
	FONTFLAG_GAUSSIANBLUR = 0x020,
	FONTFLAG_ROTARY = 0x040,
	FONTFLAG_DROPSHADOW = 0x080,
	FONTFLAG_ADDITIVE = 0x100,
	FONTFLAG_OUTLINE = 0x200,
	FONTFLAG_CUSTOM = 0x400,
	FONTFLAG_BITMAP = 0x800
};

class ISurface : public IAppSystem
{
public:
	virtual void Shutdown() = 0;
	virtual void RunFrame() = 0;
	virtual VPANEL GetEmbeddedPanel() = 0;
	virtual void SetEmbeddedPanel(VPANEL pPanel) = 0;
	virtual void PushMakeCurrent(VPANEL panel, bool useInsets) = 0;
	virtual void PopMakeCurrent(VPANEL panel) = 0;
	virtual void DrawSetColor(int r, int g, int b, int a) = 0;
	virtual void DrawSetColor(Color_t col) = 0;
	virtual void DrawFilledRect(int x0, int y0, int x1, int y1) = 0;
	virtual void DrawFilledRectArray(IntRect* pRects, int numRects) = 0;
	virtual void DrawOutlinedRect(int x0, int y0, int x1, int y1) = 0;
	virtual void DrawLine(int x0, int y0, int x1, int y1) = 0;
	virtual void DrawPolyLine(int* px, int* py, int numPoints) = 0;
	virtual void DrawSetTextFont(HFont font) = 0;
	virtual void DrawSetTextColor(int r, int g, int b, int a) = 0;
	virtual void DrawSetTextColor(Color_t col) = 0;
	virtual void DrawSetTextPos(int x, int y) = 0;
	virtual void DrawGetTextPos(int& x, int& y) = 0;
	virtual void DrawPrintText(const wchar_t* text, int textLen, FontDrawType_t drawType = FONT_DRAW_DEFAULT) = 0;
	virtual void DrawUnicodeChar(wchar_t wch, FontDrawType_t drawType = FONT_DRAW_DEFAULT) = 0;
	virtual void DrawFlushText() = 0;
	virtual IHTML* CreateHTMLWindow(IHTMLEvents* events, VPANEL context) = 0;
	virtual void PaintHTMLWindow(IHTML* htmlwin) = 0;
	virtual void DeleteHTMLWindow(IHTML* htmlwin) = 0;
	virtual int	DrawGetTextureId(char const* filename) = 0;
	virtual bool DrawGetTextureFile(int id, char* filename, int maxlen) = 0;
	virtual void DrawSetTextureFile(int id, const char* filename, int hardwareFilter, bool forceReload) = 0;
	virtual void DrawSetTextureRGBA(int id, const unsigned char* rgba, int wide, int tall, int hardwareFilter, bool forceReload) = 0;
	virtual void DrawSetTexture(int id) = 0;
	virtual void DrawGetTextureSize(int id, int& wide, int& tall) = 0;
	virtual void DrawTexturedRect(int x0, int y0, int x1, int y1) = 0;
	virtual bool IsTextureIDValid(int id) = 0;
	virtual bool DeleteTextureByID(int id) = 0;
	virtual int CreateNewTextureID(bool procedural = false) = 0;
	virtual void GetScreenSize(int& wide, int& tall) = 0;
	virtual void SetAsTopMost(VPANEL panel, bool state) = 0;
	virtual void BringToFront(VPANEL panel) = 0;
	virtual void SetForegroundWindow(VPANEL panel) = 0;
	virtual void SetPanelVisible(VPANEL panel, bool state) = 0;
	virtual void SetMinimized(VPANEL panel, bool state) = 0;
	virtual bool IsMinimized(VPANEL panel) = 0;
	virtual void FlashWindow(VPANEL panel, bool state) = 0;
	virtual void SetTitle(VPANEL panel, const wchar_t* title) = 0;
	virtual void SetAsToolBar(VPANEL panel, bool state) = 0;
	virtual void CreatePopup(VPANEL panel, bool minimised, bool showTaskbarIcon = true, bool disabled = false, bool mouseInput = true, bool kbInput = true) = 0;
	virtual void SwapBuffers(VPANEL panel) = 0;
	virtual void Invalidate(VPANEL panel) = 0;
	virtual void SetCursor(HCursor cursor) = 0;
	virtual void SetCursorAlwaysVisible(bool visible) = 0;
	virtual bool IsCursorVisible() = 0;
	virtual void ApplyChanges() = 0;
	virtual bool IsWithin(int x, int y) = 0;
	virtual bool HasFocus() = 0;
	virtual bool SupportsFeature(SurfaceFeature_e feature) = 0;
	virtual void RestrictPaintToSinglePanel(VPANEL panel) = 0;
	virtual void SetModalPanel(VPANEL) = 0;
	virtual VPANEL GetModalPanel() = 0;
	virtual void UnlockCursor() = 0;
	virtual void LockCursor() = 0;
	virtual void SetTranslateExtendedKeys(bool state) = 0;
	virtual VPANEL GetTopmostPopup() = 0;
	virtual void SetTopLevelFocus(VPANEL panel) = 0;
	virtual HFont CreateFont() = 0;
	virtual bool SetFontGlyphSet(HFont font, const char* windowsFontName, int tall, int weight, int blur, int scanlines, int flags, int nRangeMin = 0, int nRangeMax = 0) = 0;
	virtual bool AddCustomFontFile(const char* fontName, const char* fontFileName) = 0;
	virtual int GetFontTall(HFont font) = 0;
	virtual int GetFontTallRequested(HFont font) = 0;
	virtual int GetFontAscent(HFont font, wchar_t wch) = 0;
	virtual bool IsFontAdditive(HFont font) = 0;
	virtual void GetCharABCwide(HFont font, int ch, int& a, int& b, int& c) = 0;
	virtual int GetCharacterWidth(HFont font, int ch) = 0;
	virtual void GetTextSize(HFont font, const wchar_t* text, int& wide, int& tall) = 0;
	virtual VPANEL GetNotifyPanel() = 0;
	virtual void SetNotifyIcon(VPANEL context, HTexture icon, VPANEL panelToReceiveMessages, const char* text) = 0;
	virtual void PlaySound(const char* fileName) = 0;
	virtual int GetPopupCount() = 0;
	virtual VPANEL GetPopup(int index) = 0;
	virtual bool ShouldPaintChildPanel(VPANEL childPanel) = 0;
	virtual bool RecreateContext(VPANEL panel) = 0;
	virtual void AddPanel(VPANEL panel) = 0;
	virtual void ReleasePanel(VPANEL panel) = 0;
	virtual void MovePopupToFront(VPANEL panel) = 0;
	virtual void MovePopupToBack(VPANEL panel) = 0;
	virtual void SolveTraverse(VPANEL panel, bool forceApplySchemeSettings = false) = 0;
	virtual void PaintTraverse(VPANEL panel) = 0;
	virtual void EnableMouseCapture(VPANEL panel, bool state) = 0;
	virtual void GetWorkspaceBounds(int& x, int& y, int& wide, int& tall) = 0;
	virtual void GetAbsoluteWindowBounds(int& x, int& y, int& wide, int& tall) = 0;
	virtual void GetProportionalBase(int& width, int& height) = 0;
	virtual void CalculateMouseVisible() = 0;
	virtual bool NeedKBInput() = 0;
	virtual bool HasCursorPosFunctions() = 0;
	virtual void SurfaceGetCursorPos(int& x, int& y) = 0;
	virtual void SurfaceSetCursorPos(int x, int y) = 0;
	virtual void DrawTexturedLine(const Vertex_t& a, const Vertex_t& b) = 0;
	virtual void DrawOutlinedCircle(int x, int y, int radius, int segments) = 0;
	virtual void DrawTexturedPolyLine(const Vertex_t* p, int n) = 0;
	virtual void DrawTexturedSubRect(int x0, int y0, int x1, int y1, float texs0, float text0, float texs1, float text1) = 0;
	virtual void DrawTexturedPolygon(int n, Vertex_t* pVertice, bool bClipVertices = true) = 0;
	virtual const wchar_t* GetTitle(VPANEL panel) = 0;
	virtual bool IsCursorLocked(void) const = 0;
	virtual void SetWorkspaceInsets(int left, int top, int right, int bottom) = 0;
	virtual bool DrawGetUnicodeCharRenderInfo(wchar_t ch, CharRenderInfo& info) = 0;
	virtual void DrawRenderCharFromInfo(const CharRenderInfo& info) = 0;
	virtual void DrawSetAlphaMultiplier(float alpha) = 0;
	virtual float DrawGetAlphaMultiplier() = 0;
	virtual void SetAllowHTMLJavaScript(bool state) = 0;
	virtual void OnScreenSizeChanged(int nOldWidth, int nOldHeight) = 0;
	virtual HCursor CreateCursorFromFile(char const* curOrAniFile, char const* pPathID = 0) = 0;
	virtual IVguiMatInfo* DrawGetTextureMatInfoFactory(int id) = 0;
	virtual void PaintTraverseEx(VPANEL panel, bool paintPopups = false) = 0;
	virtual float GetZPos() const = 0;
	virtual void SetPanelForInput(VPANEL vpanel) = 0;
	virtual void DrawFilledRectFastFade(int x0, int y0, int x1, int y1, int fadeStartPt, int fadeEndPt, unsigned int alpha0, unsigned int alpha1, bool bHorizontal) = 0;
	virtual void DrawFilledRectFade(int x0, int y0, int x1, int y1, unsigned int alpha0, unsigned int alpha1, bool bHorizontal) = 0;
	virtual void DrawSetTextureRGBAEx(int id, const unsigned char* rgba, int wide, int tall, ImageFormat imageFormat) = 0;
	virtual void DrawSetTextScale(float sx, float sy) = 0;
	virtual bool SetBitmapFontGlyphSet(HFont font, const char* windowsFontName, float scalex, float scaley, int flags) = 0;
	virtual bool AddBitmapFontFile(const char* fontFileName) = 0;
	virtual void SetBitmapFontName(const char* pName, const char* pFontFilename) = 0;
	virtual const char* GetBitmapFontName(const char* pName) = 0;
	virtual void ClearTemporaryFontCache(void) = 0;
	virtual IImage* GetIconImageForFullPath(char const* pFullPath) = 0;
	virtual void DrawUnicodeString(const wchar_t* pwString, FontDrawType_t drawType = FONT_DRAW_DEFAULT) = 0;
	virtual void PrecacheFontCharacters(HFont font, const wchar_t* pCharacters) = 0;
	virtual const char* GetResolutionKey(void) const = 0;
	virtual const char* GetFontName(HFont font) = 0;
	virtual const char* GetFontFamilyName(HFont font) = 0;
	virtual void GetKernedCharWidth(HFont font, wchar_t ch, wchar_t chBefore, wchar_t chAfter, float& wide, float& abcA) = 0;
	virtual bool ForceScreenSizeOverride(bool bState, int wide, int tall) = 0;
	virtual bool ForceScreenPosOffset(bool bState, int x, int y) = 0;
	virtual void OffsetAbsPos(int& x, int& y) = 0;
	virtual void ResetFontCaches() = 0;
	virtual int GetTextureNumFrames(int id) = 0;
	virtual void DrawSetTextureFrame(int id, int nFrame, unsigned int* pFrameCache) = 0;
	virtual bool IsScreenSizeOverrideActive(void) = 0;
	virtual bool IsScreenPosOverrideActive(void) = 0;
	virtual void DestroyTextureID(int id) = 0;
	virtual void DrawUpdateRegionTextureRGBA(int nTextureID, int x, int y, const unsigned char* pchData, int wide, int tall, ImageFormat imageFormat) = 0;
	virtual bool BHTMLWindowNeedsPaint(IHTML* htmlwin) = 0;
	virtual const char* GetWebkitHTMLUserAgentString() = 0;
	virtual void* Deprecated_AccessChromeHTMLController() = 0;
	virtual void SetFullscreenViewport(int x, int y, int w, int h) = 0;
	virtual void GetFullscreenViewport(int& x, int& y, int& w, int& h) = 0;
	virtual void PushFullscreenViewport() = 0;
	virtual void PopFullscreenViewport() = 0;
	virtual void SetSoftwareCursor(bool bUseSoftwareCursor) = 0;
	virtual void PaintSoftwareCursor() = 0;
};

// ================ SDK/Definitions/Interfaces/IMatSystemSurface.h ================
MAKE_SIGNATURE(CMatSystemSurface_StartDrawing, "vguimatsurface.dll", "40 53 56 57 48 83 EC ? 48 8B F9 80 3D", 0x0);
MAKE_SIGNATURE(CMatSystemSurface_FinishDrawing, "vguimatsurface.dll", "40 53 48 83 EC ? 33 C9", 0x0);

class VMatrix;
class IMaterial;
class ITexture;
struct InputEvent_t;

typedef void (*GetMouseCallback_t)(int& x, int& y);
typedef void (*SetMouseCallback_t)(int x, int y);
typedef void (*PlaySoundFunc_t)(const char* pFileName);

class IMatSystemSurface : public ISurface
{
public:
	virtual void AttachToWindow(void* hwnd, bool bLetAppDriveInput = false) = 0;
	virtual void EnableWindowsMessages(bool bEnable) = 0;
	virtual void Begin3DPaint(int iLeft, int iTop, int iRight, int iBottom, bool bRenderToTexture = true) = 0;
	virtual void End3DPaint() = 0;
	virtual void DisableClipping(bool bDisable) = 0;
	virtual void GetClippingRect(int& left, int& top, int& right, int& bottom, bool& bClippingDisabled) = 0;
	virtual void SetClippingRect(int left, int top, int right, int bottom) = 0;
	virtual bool IsCursorLocked() const = 0;
	virtual void SetMouseCallbacks(GetMouseCallback_t getFunc, SetMouseCallback_t setFunc) = 0;
	virtual void InstallPlaySoundFunc(PlaySoundFunc_t soundFunc) = 0;
	virtual void DrawColoredCircle(int centerx, int centery, float radius, int r, int g, int b, int a) = 0;
	virtual int DrawColoredText(HFont font, int x, int y, int r, int g, int b, int a, const char* fmt, ...) = 0;
	virtual void DrawColoredTextRect(HFont font, int x, int y, int w, int h, int r, int g, int b, int a, const char* fmt, ...) = 0;
	virtual void DrawTextHeight(HFont font, int w, int& h, const char* fmt, ...) = 0;
	virtual int	DrawTextLen(HFont font, const char* fmt, ...) = 0;
	virtual void DrawPanelIn3DSpace(VPANEL pRootPanel, const VMatrix& panelCenterToWorld, int nPixelWidth, int nPixelHeight, float flWorldWidth, float flWorldHeight) = 0;
	virtual void DrawSetTextureMaterial(int id, IMaterial* pMaterial) = 0;
	virtual bool HandleInputEvent(const InputEvent_t& event) = 0;
	virtual void Set3DPaintTempRenderTarget(const char* pRenderTargetName) = 0;
	virtual void Reset3DPaintTempRenderTarget(void) = 0;
	virtual IMaterial* DrawGetTextureMaterial(int id) = 0;
	virtual void GetFullscreenViewportAndRenderTarget(int& x, int& y, int& w, int& h, ITexture** ppRenderTarget) = 0;
	virtual void SetFullscreenViewportAndRenderTarget(int x, int y, int w, int h, ITexture* pRenderTarget) = 0;
	virtual int  DrawGetTextureId(ITexture* pTexture) = 0;
	virtual void BeginSkinCompositionPainting() = 0;
	virtual void EndSkinCompositionPainting() = 0;

	SIGNATURE(StartDrawing, void, CMatSystemSurface, this);
	SIGNATURE(FinishDrawing, void, CMatSystemSurface, this);
};

MAKE_INTERFACE_VERSION(IMatSystemSurface, MatSystemSurface, "vguimatsurface.dll", "VGUI_Surface030");

// ================ SDK/Definitions/Interfaces/IMemAlloc.h ================
typedef size_t(*MemAllocFailHandler_t)(size_t);
class IVirtualMemorySection;

struct GenericMemoryStat_t
{
	const char* name;
	int value;
};

class IMemAlloc
{
public:
	virtual void* Alloc(size_t nSize) = 0;
	virtual void* Realloc(void* pMem, size_t nSize) = 0;
	virtual void Free(void* pMem) = 0;
	virtual void* Expand_NoLongerSupported(void* pMem, size_t nSize) = 0;
	virtual void* Alloc(size_t nSize, const char* pFileName, int nLine) = 0;
	virtual void* Realloc(void* pMem, size_t nSize, const char* pFileName, int nLine) = 0;
	virtual void  Free(void* pMem, const char* pFileName, int nLine) = 0;
	virtual void* Expand_NoLongerSupported(void* pMem, size_t nSize, const char* pFileName, int nLine) = 0;
	virtual size_t GetSize(void* pMem) = 0;
	virtual void PushAllocDbgInfo(const char* pFileName, int nLine) = 0;
	virtual void PopAllocDbgInfo() = 0;
	virtual int32 CrtSetBreakAlloc(int32 lNewBreakAlloc) = 0;
	virtual	int CrtSetReportMode(int nReportType, int nReportMode) = 0;
	virtual int CrtIsValidHeapPointer(const void* pMem) = 0;
	virtual int CrtIsValidPointer(const void* pMem, unsigned int size, int access) = 0;
	virtual int CrtCheckMemory(void) = 0;
	virtual int CrtSetDbgFlag(int nNewFlag) = 0;
	virtual void CrtMemCheckpoint(_CrtMemState* pState) = 0;
	virtual void DumpStats() = 0;
	virtual void DumpStatsFileBase(char const* pchFileBase) = 0;
	virtual size_t ComputeMemoryUsedBy(char const* pchSubStr) = 0;
	virtual void* CrtSetReportFile(int nRptType, void* hFile) = 0;
	virtual void* CrtSetReportHook(void* pfnNewHook) = 0;
	virtual int CrtDbgReport(int nRptType, const char* szFile, int nLine, const char* szModule, const char* pMsg) = 0;
	virtual int heapchk() = 0;
	virtual bool IsDebugHeap() = 0;
	virtual void GetActualDbgInfo(const char*& pFileName, int& nLine) = 0;
	virtual void RegisterAllocation(const char* pFileName, int nLine, size_t nLogicalSize, size_t nActualSize, unsigned nTime) = 0;
	virtual void RegisterDeallocation(const char* pFileName, int nLine, size_t nLogicalSize, size_t nActualSize, unsigned nTime) = 0;
	virtual int GetVersion() = 0;
	virtual void CompactHeap() = 0;
	virtual MemAllocFailHandler_t SetAllocFailHandler(MemAllocFailHandler_t pfnMemAllocFailHandler) = 0;
	virtual void DumpBlockStats(void*) = 0;
	virtual void SetStatsExtraInfo(const char* pMapName, const char* pComment) = 0;
	virtual size_t MemoryAllocFailed() = 0;
	virtual void CompactIncremental() = 0;
	virtual void OutOfMemory(size_t nBytesAttempted = 0) = 0;
	virtual void* RegionAlloc(int region, size_t nSize) = 0;
	virtual void* RegionAlloc(int region, size_t nSize, const char* pFileName, int nLine) = 0;
	virtual void GlobalMemoryStatus(size_t* pUsedMemory, size_t* pFreeMemory) = 0;
	virtual IVirtualMemorySection* AllocateVirtualMemorySection(size_t numMaxBytes) = 0;
	virtual int GetGenericMemoryStats(GenericMemoryStat_t** ppMemoryStats) = 0;
	virtual ~IMemAlloc() {};
	virtual uint32 GetDebugInfoSize() = 0;
	virtual void SaveDebugInfo(void* pvDebugInfo) = 0;
	virtual void RestoreDebugInfo(const void* pvDebugInfo) = 0;
	virtual void InitDebugInfo(void* pvDebugInfo, const char* pchRootFileName, int nLine) = 0;
};

MAKE_INTERFACE_EXPORT(IMemAlloc, MemAlloc, "tier0.dll", "g_pMemAlloc", 1);

// ================ SDK/Definitions/Interfaces/IPanel.h ================
#ifdef GetClassName
#undef GetClassName
#endif
#ifdef SendMessage
#undef SendMessage
#endif

class IClientPanel;
class SurfacePlat;

class IPanel : public IBaseInterface
{
public:
	virtual void Init(VPANEL vguiPanel, IClientPanel* panel) = 0;
	virtual void SetPos(VPANEL vguiPanel, int x, int y) = 0;
	virtual void GetPos(VPANEL vguiPanel, int& x, int& y) = 0;
	virtual void SetSize(VPANEL vguiPanel, int wide, int tall) = 0;
	virtual void GetSize(VPANEL vguiPanel, int& wide, int& tall) = 0;
	virtual void SetMinimumSize(VPANEL vguiPanel, int wide, int tall) = 0;
	virtual void GetMinimumSize(VPANEL vguiPanel, int& wide, int& tall) = 0;
	virtual void SetZPos(VPANEL vguiPanel, int z) = 0;
	virtual int  GetZPos(VPANEL vguiPanel) = 0;
	virtual void GetAbsPos(VPANEL vguiPanel, int& x, int& y) = 0;
	virtual void GetClipRect(VPANEL vguiPanel, int& x0, int& y0, int& x1, int& y1) = 0;
	virtual void SetInset(VPANEL vguiPanel, int left, int top, int right, int bottom) = 0;
	virtual void GetInset(VPANEL vguiPanel, int& left, int& top, int& right, int& bottom) = 0;
	virtual void SetVisible(VPANEL vguiPanel, bool state) = 0;
	virtual bool IsVisible(VPANEL vguiPanel) = 0;
	virtual void SetParent(VPANEL vguiPanel, VPANEL newParent) = 0;
	virtual int GetChildCount(VPANEL vguiPanel) = 0;
	virtual VPANEL GetChild(VPANEL vguiPanel, int index) = 0;
	virtual CUtlVector< VPANEL >& GetChildren(VPANEL vguiPanel) = 0;
	virtual VPANEL GetParent(VPANEL vguiPanel) = 0;
	virtual void MoveToFront(VPANEL vguiPanel) = 0;
	virtual void MoveToBack(VPANEL vguiPanel) = 0;
	virtual bool HasParent(VPANEL vguiPanel, VPANEL potentialParent) = 0;
	virtual bool IsPopup(VPANEL vguiPanel) = 0;
	virtual void SetPopup(VPANEL vguiPanel, bool state) = 0;
	virtual bool IsFullyVisible(VPANEL vguiPanel) = 0;
	virtual HScheme GetScheme(VPANEL vguiPanel) = 0;
	virtual bool IsProportional(VPANEL vguiPanel) = 0;
	virtual bool IsAutoDeleteSet(VPANEL vguiPanel) = 0;
	virtual void DeletePanel(VPANEL vguiPanel) = 0;
	virtual void SetKeyBoardInputEnabled(VPANEL vguiPanel, bool state) = 0;
	virtual void SetMouseInputEnabled(VPANEL vguiPanel, bool state) = 0;
	virtual bool IsKeyBoardInputEnabled(VPANEL vguiPanel) = 0;
	virtual bool IsMouseInputEnabled(VPANEL vguiPanel) = 0;
	virtual void Solve(VPANEL vguiPanel) = 0;
	virtual const char* GetName(VPANEL vguiPanel) = 0;
	virtual const char* GetClassName(VPANEL vguiPanel) = 0;
	virtual void SendMessage(VPANEL vguiPanel, KeyValues* params, VPANEL ifromPanel) = 0;
	virtual void Think(VPANEL vguiPanel) = 0;
	virtual void PerformApplySchemeSettings(VPANEL vguiPanel) = 0;
	virtual void PaintTraverse(VPANEL vguiPanel, bool forceRepaint, bool allowForce = true) = 0;
	virtual void Repaint(VPANEL vguiPanel) = 0;
	virtual VPANEL IsWithinTraverse(VPANEL vguiPanel, int x, int y, bool traversePopups) = 0;
	virtual void OnChildAdded(VPANEL vguiPanel, VPANEL child) = 0;
	virtual void OnSizeChanged(VPANEL vguiPanel, int newWide, int newTall) = 0;
	virtual void InternalFocusChanged(VPANEL vguiPanel, bool lost) = 0;
	virtual bool RequestInfo(VPANEL vguiPanel, KeyValues* outputData) = 0;
	virtual void RequestFocus(VPANEL vguiPanel, int direction = 0) = 0;
	virtual bool RequestFocusPrev(VPANEL vguiPanel, VPANEL existingPanel) = 0;
	virtual bool RequestFocusNext(VPANEL vguiPanel, VPANEL existingPanel) = 0;
	virtual VPANEL GetCurrentKeyFocus(VPANEL vguiPanel) = 0;
	virtual int GetTabPosition(VPANEL vguiPanel) = 0;
	virtual SurfacePlat* Plat(VPANEL vguiPanel) = 0;
	virtual void SetPlat(VPANEL vguiPanel, SurfacePlat* Plat) = 0;
	virtual Panel* GetPanel(VPANEL vguiPanel, const char* destinationModule) = 0;
	virtual bool IsEnabled(VPANEL vguiPanel) = 0;
	virtual void SetEnabled(VPANEL vguiPanel, bool state) = 0;
	virtual bool IsTopmostPopup(VPANEL vguiPanel) = 0;
	virtual void SetTopmostPopup(VPANEL vguiPanel, bool state) = 0;
	virtual void SetSiblingPin(VPANEL vguiPanel, VPANEL newSibling, byte iMyCornerToPin = 0, byte iSiblingCornerToPinTo = 0) = 0;
};

MAKE_INTERFACE_VERSION(IPanel, Panel, "vgui2.dll", "VGUI_Panel009");

// ================ SDK/Definitions/Main/IMaterial.h ================
class IMaterialVar;
class ITexture;

enum MaterialVarFlags_t
{
	MATERIAL_VAR_DEBUG = (1 << 0),
	MATERIAL_VAR_NO_DEBUG_OVERRIDE = (1 << 1),
	MATERIAL_VAR_NO_DRAW = (1 << 2),
	MATERIAL_VAR_USE_IN_FILLRATE_MODE = (1 << 3),
	MATERIAL_VAR_VERTEXCOLOR = (1 << 4),
	MATERIAL_VAR_VERTEXALPHA = (1 << 5),
	MATERIAL_VAR_SELFILLUM = (1 << 6),
	MATERIAL_VAR_ADDITIVE = (1 << 7),
	MATERIAL_VAR_ALPHATEST = (1 << 8),
	MATERIAL_VAR_MULTIPASS = (1 << 9),
	MATERIAL_VAR_ZNEARER = (1 << 10),
	MATERIAL_VAR_MODEL = (1 << 11),
	MATERIAL_VAR_FLAT = (1 << 12),
	MATERIAL_VAR_NOCULL = (1 << 13),
	MATERIAL_VAR_NOFOG = (1 << 14),
	MATERIAL_VAR_IGNOREZ = (1 << 15),
	MATERIAL_VAR_DECAL = (1 << 16),
	MATERIAL_VAR_ENVMAPSPHERE = (1 << 17),
	MATERIAL_VAR_NOALPHAMOD = (1 << 18),
	MATERIAL_VAR_ENVMAPCAMERASPACE = (1 << 19),
	MATERIAL_VAR_BASEALPHAENVMAPMASK = (1 << 20),
	MATERIAL_VAR_TRANSLUCENT = (1 << 21),
	MATERIAL_VAR_NORMALMAPALPHAENVMAPMASK = (1 << 22),
	MATERIAL_VAR_NEEDS_SOFTWARE_SKINNING = (1 << 23),
	MATERIAL_VAR_OPAQUETEXTURE = (1 << 24),
	MATERIAL_VAR_ENVMAPMODE = (1 << 25),
	MATERIAL_VAR_SUPPRESS_DECALS = (1 << 26),
	MATERIAL_VAR_HALFLAMBERT = (1 << 27),
	MATERIAL_VAR_WIREFRAME = (1 << 28),
	MATERIAL_VAR_ALLOWALPHATOCOVERAGE = (1 << 29),
	MATERIAL_VAR_IGNORE_ALPHA_MODULATION = (1 << 30)
};

enum PreviewImageRetVal_t
{
	MATERIAL_PREVIEW_IMAGE_BAD = 0,
	MATERIAL_PREVIEW_IMAGE_OK,
	MATERIAL_NO_PREVIEW_IMAGE
};

class IMaterial
{
public:
	virtual const char* GetName() const = 0;
	virtual const char* GetTextureGroupName() const = 0;
	virtual PreviewImageRetVal_t GetPreviewImageProperties(int* width, int* height, ImageFormat* imageFormat, bool* isTranslucent) const = 0;
	virtual PreviewImageRetVal_t GetPreviewImage(unsigned char* data, int width, int height, ImageFormat imageFormat) const = 0;
	virtual int GetMappingWidth() = 0;
	virtual int GetMappingHeight() = 0;
	virtual int GetNumAnimationFrames() = 0;
	virtual bool InMaterialPage(void) = 0;
	virtual	void GetMaterialOffset(float* pOffset) = 0;
	virtual void GetMaterialScale(float* pScale) = 0;
	virtual IMaterial* GetMaterialPage(void) = 0;
	virtual IMaterialVar* FindVar(const char* varName, bool* found, bool complain = true) = 0;
	virtual void IncrementReferenceCount(void) = 0;
	virtual void DecrementReferenceCount(void) = 0;
	inline void AddRef() { IncrementReferenceCount(); }
	inline void Release() { DecrementReferenceCount(); }
	virtual int GetEnumerationID(void) const = 0;
	virtual void GetLowResColorSample(float s, float t, float* color) const = 0;
	virtual void RecomputeStateSnapshots() = 0;
	virtual bool IsTranslucent() = 0;
	virtual bool IsAlphaTested() = 0;
	virtual bool IsVertexLit() = 0;
	virtual VertexFormat_t	GetVertexFormat() const = 0;
	virtual bool HasProxy(void) const = 0;
	virtual bool UsesEnvCubemap(void) = 0;
	virtual bool NeedsTangentSpace(void) = 0;
	virtual bool NeedsPowerOfTwoFrameBufferTexture(bool bCheckSpecificToThisFrame = true) = 0;
	virtual bool NeedsFullFrameBufferTexture(bool bCheckSpecificToThisFrame = true) = 0;
	virtual bool NeedsSoftwareSkinning(void) = 0;
	virtual void AlphaModulate(float alpha) = 0;
	virtual void ColorModulate(float r, float g, float b) = 0;
	virtual void SetMaterialVarFlag(MaterialVarFlags_t flag, bool on) = 0;
	virtual bool GetMaterialVarFlag(MaterialVarFlags_t flag) const = 0;
	virtual void GetReflectivity(Vector& reflect) = 0;
	virtual bool GetPropertyFlag(MaterialPropertyTypes_t type) = 0;
	virtual bool IsTwoSided() = 0;
	virtual void SetShader(const char* pShaderName) = 0;
	virtual int GetNumPasses(void) = 0;
	virtual int GetTextureMemoryBytes(void) = 0;
	virtual void Refresh() = 0;
	virtual bool NeedsLightmapBlendAlpha(void) = 0;
	virtual bool NeedsSoftwareLighting(void) = 0;
	virtual int ShaderParamCount() const = 0;
	virtual IMaterialVar** GetShaderParams(void) = 0;
	virtual bool IsErrorMaterial() const = 0;
	virtual void SetUseFixedFunctionBakedLighting(bool bEnable) = 0;
	virtual float GetAlphaModulation() = 0;
	virtual void GetColorModulation(float* r, float* g, float* b) = 0;
	virtual MorphFormat_t GetMorphFormat() const = 0;
	virtual IMaterialVar* FindVarFast(char const* pVarName, unsigned int* pToken) = 0;
	virtual void SetShaderAndParams(KeyValues* pKeyValues) = 0;
	virtual const char* GetShaderName() const = 0;
	virtual void DeleteIfUnreferenced() = 0;
	virtual bool IsSpriteCard() = 0;
	virtual void CallBindProxy(void* proxyData) = 0;
	virtual IMaterial* CheckProxyReplacement(void* proxyData) = 0;
	virtual void RefreshPreservingMaterialVars() = 0;
	virtual bool WasReloadedFromWhitelist() = 0;
	virtual bool IsPrecached() const = 0;
};

inline bool IsErrorMaterial(IMaterial* pMat)
{
	return !pMat || pMat->IsErrorMaterial();
}

// ================ SDK/Definitions/Misc/IMDLCache.h ================
struct studiohdr_t;
struct vcollide_t;
struct virtualmodel_t;

typedef unsigned short MDLHandle_t;

enum
{
	MDLHANDLE_INVALID = (MDLHandle_t)~0
};

// ================ SDK/Definitions/Misc/LocalFlexController.h ================
#pragma warning (disable : 26812)

enum LocalFlexController_t
{
	DUMMY_FLEX_CONTROLLER = 0x7fffffff
};

inline LocalFlexController_t& operator++(LocalFlexController_t& a) { return a = LocalFlexController_t(int(a) + 1); }
inline LocalFlexController_t& operator--(LocalFlexController_t& a) { return a = LocalFlexController_t(int(a) - 1); }
inline LocalFlexController_t  operator++(LocalFlexController_t& a, int) { LocalFlexController_t t = a; a = LocalFlexController_t(int(a) + 1); return t; }
inline LocalFlexController_t  operator--(LocalFlexController_t& a, int) { LocalFlexController_t t = a; a = LocalFlexController_t(int(a) - 1); return t; }

// ================ SDK/Definitions/Misc/Studio.h ================

class IMaterial;
struct virtualmodel_t;

#define MAXSTUDIOBONES 128

#define BONE_USED_BY_ANYTHING 0x0007FF00
// studio.h:415-416. HITBOX is the mask every SetupBones call in this build actually wants:
// the bone cache only hits when (GetReadableBones() & boneMask) == boneMask, so ANYTHING
// (all eleven bits) never hits and forces a full rebuild -- see CAimbotGlobal::PlayerBoneInFOV.
// ATTACHMENT is here for the fallback that note names, not because anything uses it today.
#define BONE_USED_BY_HITBOX 0x00000100
#define BONE_USED_BY_ATTACHMENT 0x00000200

struct mstudiobbox_t
{
	int bone;
	int group;
	Vector bbmin;
	Vector bbmax;
	int szhitboxnameindex;
	Vec3 angle;
	float m_radius;
	uint8_t _padding[0x10];

};

struct mstudioevent_t
{
	float cycle;
	int event;
	int type;
	char options[64];
	int szeventindex;
};

struct studiohdr_t;

struct mstudioanimdesc_t
{
	int baseptr;
	int sznameindex;
	float fps;
	int flags;
	int numframes;
	int nummovements;
	int movementindex;
	int unused1[6];
	int animblock;
	int animindex;
	int numikrules;
	int ikruleindex;
	int animblockikruleindex;
	int numlocalhierarchy;
	int localhierarchyindex;
	int sectionindex;
	int sectionframes;
	short zeroframespan;
	short zeroframecount;
	int zeroframeindex;
	mutable float zeroframestalltime;
};

struct mstudioactivitymodifier_t
{
	int sznameindex;
};

struct mstudioseqdesc_t
{
	int baseptr;
	int szlabelindex;
	int szactivitynameindex;
	int flags;
	int activity;
	int actweight;
	int numevents;
	int eventindex;
	inline mstudioevent_t* pEvent(int i) const { return (mstudioevent_t*)(((byte*)this) + eventindex) + i; };
	Vector bbmin;
	Vector bbmax;
	int numblends;
	int animindexindex;

	inline int anim(int x, int y) const
	{
		if (x >= groupsize[0])
		{
			x = groupsize[0] - 1;
		}

		if (y >= groupsize[1])
		{
			y = groupsize[1] - 1;
		}

		int offset = y * groupsize[0] + x;
		short* blends = (short*)(((byte*)this) + animindexindex);
		int value = (int)blends[offset];
		return value;
	}

	int movementindex;
	int groupsize[2];
	int paramindex[2];
	float paramstart[2];
	float paramend[2];
	int paramparent;
	float fadeintime;
	float fadeouttime;
	int localentrynode;
	int localexitnode;
	int nodeflags;
	float entryphase;
	float exitphase;
	float lastframe;
	int nextseq;
	int pose;
	int numikrules;
	int numautolayers;
	int autolayerindex;
	int weightlistindex;
	inline float* pBoneweight(int i) const { return ((float*)(((byte*)this) + weightlistindex) + i); };
	inline float weight(int i) const { return *(pBoneweight(i)); };
	int posekeyindex;
	int numiklocks;
	int iklockindex;
	int keyvalueindex;
	int keyvaluesize;
	inline const char* KeyValueText(void) const { return keyvaluesize != 0 ? ((char*)this) + keyvalueindex : NULL; }
	int cycleposeindex;
	int activitymodifierindex;
	int numactivitymodifiers;
	inline mstudioactivitymodifier_t* pActivityModifier(int i) const { return activitymodifierindex != 0 ? (mstudioactivitymodifier_t*)(((byte*)this) + activitymodifierindex) + i : NULL; };
	int unused[5];
};

class CStudioHdr;

struct mstudiotexture_t
{
	int sznameindex;
	int flags;
	int used;
	int unused1;
	IMaterial* material;
	void* clientmaterial;
	int unused[10];
};

struct mstudiohitboxset_t
{
	int sznameindex;
	int numhitboxes;
	int hitboxindex;
	inline mstudiobbox_t* pHitbox(int i) const { return (mstudiobbox_t*)(((byte*)this) + hitboxindex) + i; };
};

struct virtualmodel_t;

struct studiohdr2_t
{
	int numsrcbonetransform;
	int srcbonetransformindex;
	int	illumpositionattachmentindex;
	float flMaxEyeDeflection;
	inline float MaxEyeDeflection() const { return flMaxEyeDeflection != 0.0f ? flMaxEyeDeflection : 0.866f; }
	int linearboneindex;
	int sznameindex;
	int m_nBoneFlexDriverCount;
	int m_nBoneFlexDriverIndex;
	int reserved[56];
};

struct studiohdr_t
{
	int id;
	int version;
	int checksum;
	char name[64];
	int length;
	Vector eyeposition;
	Vector illumposition;
	Vector hull_min;
	Vector hull_max;
	Vector view_bbmin;
	Vector view_bbmax;
	int flags;
	int numbones;
	int boneindex;
	int numbonecontrollers;
	int bonecontrollerindex;
	int numhitboxsets;
	int hitboxsetindex;

	mstudiohitboxset_t* pHitboxSet(int i) const
	{
		return (mstudiohitboxset_t*)(((byte*)this) + hitboxsetindex) + i;
	};

	inline mstudiobbox_t* pHitbox(int i, int set) const
	{
		mstudiohitboxset_t const* s = pHitboxSet(set);
		if (!s)
			return NULL;

		return s->pHitbox(i);
	};

	int numlocalanim;
	int localanimindex;
	inline mstudioanimdesc_t* pLocalAnimdesc(int i) const { if (i < 0 || i >= numlocalanim) i = 0; return (mstudioanimdesc_t*)(((byte*)this) + localanimindex) + i; };
	int numlocalseq;
	int localseqindex;
	inline mstudioseqdesc_t* pLocalSeqdesc(int i) const { if (i < 0 || i >= numlocalseq) i = 0; return (mstudioseqdesc_t*)(((byte*)this) + localseqindex) + i; };
	int activitylistversion;
	int eventsindexed;
	int numtextures;
	int textureindex;
	inline mstudiotexture_t* pTexture(int i) const { return (mstudiotexture_t*)(((byte*)this) + textureindex) + i; };
	int numcdtextures;
	int cdtextureindex;
	int numskinref;
	int numskinfamilies;
	int skinindex;
	int numbodyparts;
	int bodypartindex;
	int numlocalattachments;
	int localattachmentindex;
	int numlocalnodes;
	int localnodeindex;
	int localnodenameindex;
	int numflexdesc;
	int flexdescindex;
	int numflexcontrollers;
	int flexcontrollerindex;
	int numflexrules;
	int flexruleindex;
	int numikchains;
	int ikchainindex;
	int nummouths;
	int mouthindex;
	int numlocalposeparameters;
	int localposeparamindex;
	int surfacepropindex;
	int keyvalueindex;
	int keyvaluesize;
	inline const char* KeyValueText(void) const { return keyvaluesize != 0 ? ((char*)this) + keyvalueindex : NULL; }
	int numlocalikautoplaylocks;
	int localikautoplaylockindex;
	float mass;
	int contents;
	int numincludemodels;
	int includemodelindex;
	void* virtualModel;
	int szanimblocknameindex;
	int numanimblocks;
	int animblockindex;
	void* animblockModel;
	int bonetablebynameindex;
	void* pVertexBase;
	void* pIndexBase;
	byte constdirectionallightdot;
	byte rootLOD;
	byte numAllowedRootLODs;
	byte unused[1];
	int unused4;
	int numflexcontrollerui;
	int flexcontrolleruiindex;
	float flVertAnimFixedPointScale;
	int unused3[1];
	int studiohdr2index;
	studiohdr2_t* pStudioHdr2() const { return (studiohdr2_t*)(((byte*)this) + studiohdr2index); }
	inline float MaxEyeDeflection() const { return studiohdr2index ? pStudioHdr2()->MaxEyeDeflection() : 0.866f; }
	int unused2[1];
};

class CStudioHdr;

// ================ SDK/Definitions/Interfaces/IStudioRender.h ================
struct studiohdr_t;
class IMaterial;
struct Ray_t;
class IMaterialSystem;

typedef void (*StudioRender_Printf_t)(const char* fmt, ...);

enum
{
	ADDDECAL_TO_ALL_LODS = -1
};

enum
{
	STUDIORENDER_DRAW_ENTIRE_MODEL = 0,
	STUDIORENDER_DRAW_OPAQUE_ONLY = 0x01,
	STUDIORENDER_DRAW_TRANSLUCENT_ONLY = 0x02,
	STUDIORENDER_DRAW_GROUP_MASK = 0x03,
	STUDIORENDER_DRAW_NO_FLEXES = 0x04,
	STUDIORENDER_DRAW_STATIC_LIGHTING = 0x08,
	STUDIORENDER_DRAW_ACCURATETIME = 0x10,
	STUDIORENDER_DRAW_NO_SHADOWS = 0x20,
	STUDIORENDER_DRAW_GET_PERF_STATS = 0x40,
	STUDIORENDER_DRAW_WIREFRAME = 0x80,
	STUDIORENDER_DRAW_ITEM_BLINK = 0x100,
	STUDIORENDER_SHADOWDEPTHTEXTURE = 0x200,
	STUDIORENDER_SSAODEPTHTEXTURE = 0x1000,
	STUDIORENDER_GENERATE_STATS = 0x8000
};

enum
{
	USESHADOWLOD = -2
};

// ================ SDK/Definitions/Interfaces/IUniformRandomStream.h ================

// ================ SDK/Definitions/Interfaces/IViewRender.h ================
class CViewSetup;
class CBaseEntity;
struct vrect_t;
class ITexture;
class IMaterial;

// ================ SDK/Definitions/Interfaces/IVModelInfo.h ================
class IMaterial;
class KeyValues;
struct vcollide_t;
struct model_t;
class CGameTrace;
struct cplane_t;
typedef CGameTrace trace_t;
struct studiohdr_t;
struct virtualmodel_t;
typedef unsigned char byte;
class CPhysCollide;
typedef unsigned short MDLHandle_t;
class CUtlBuffer;
class IClientRenderable;

class IModelLoadCallback
{
public:
	virtual void OnModelLoadComplete(const model_t* pModel) = 0;
};

class IVModelInfo
{
public:
	virtual ~IVModelInfo(void) {}
	virtual const model_t* GetModel(int modelindex) = 0;
	virtual int GetModelIndex(const char* name) const = 0;
	virtual const char* GetModelName(const model_t* model) const = 0;
	virtual vcollide_t* GetVCollide(const model_t* model) = 0;
	virtual vcollide_t* GetVCollide(int modelindex) = 0;
	virtual void GetModelBounds(const model_t* model, Vector& mins, Vector& maxs) const = 0;
	virtual	void GetModelRenderBounds(const model_t* model, Vector& mins, Vector& maxs) const = 0;
	virtual int GetModelFrameCount(const model_t* model) const = 0;
	virtual int GetModelType(const model_t* model) const = 0;
	virtual void* GetModelExtraData(const model_t* model) = 0;
	virtual bool ModelHasMaterialProxy(const model_t* model) const = 0;
	virtual bool IsTranslucent(model_t const* model) const = 0;
	virtual bool IsTranslucentTwoPass(const model_t* model) const = 0;
	virtual void RecomputeTranslucency(const model_t* model, int nSkin, int nBody, void* pClientRenderable, float fInstanceAlphaModulate = 1.0f) = 0;
	virtual int GetModelMaterialCount(const model_t* model) const = 0;
	virtual void GetModelMaterials(const model_t* model, int count, IMaterial** ppMaterial) = 0;
	virtual bool IsModelVertexLit(const model_t* model) const = 0;
	virtual const char* GetModelKeyValueText(const model_t* model) = 0;
	virtual bool GetModelKeyValue(const model_t* model, CUtlBuffer& buf) = 0;
	virtual float GetModelRadius(const model_t* model) = 0;
	virtual const studiohdr_t* FindModel(const studiohdr_t* pStudioHdr, void** cache, const char* modelname) const = 0;
	virtual const studiohdr_t* FindModel(void* cache) const = 0;
	virtual	virtualmodel_t* GetVirtualModel(const studiohdr_t* pStudioHdr) const = 0;
	virtual byte* GetAnimBlock(const studiohdr_t* pStudioHdr, int iBlock) const = 0;
	virtual void GetModelMaterialColorAndLighting(const model_t* model, Vector const& origin, QAngle const& angles, trace_t* pTrace, Vector& lighting, Vector& matColor) = 0;
	virtual void GetIlluminationPoint(const model_t* model, IClientRenderable* pRenderable, Vector const& origin, QAngle const& angles, Vector* pLightingCenter) = 0;
	virtual int GetModelContents(int modelIndex) = 0;
	virtual studiohdr_t* GetStudiomodel(const model_t* mod) = 0;
	virtual int GetModelSpriteWidth(const model_t* model) const = 0;
	virtual int GetModelSpriteHeight(const model_t* model) const = 0;
	virtual void SetLevelScreenFadeRange(float flMinSize, float flMaxSize) = 0;
	virtual void GetLevelScreenFadeRange(float* pMinArea, float* pMaxArea) const = 0;
	virtual void SetViewScreenFadeRange(float flMinSize, float flMaxSize) = 0;
	virtual unsigned char ComputeLevelScreenFade(const Vector& vecAbsOrigin, float flRadius, float flFadeScale) const = 0;
	virtual unsigned char ComputeViewScreenFade(const Vector& vecAbsOrigin, float flRadius, float flFadeScale) const = 0;
	virtual int GetAutoplayList(const studiohdr_t* pStudioHdr, unsigned short** pAutoplayList) const = 0;
	virtual CPhysCollide* GetCollideForVirtualTerrain(int index) = 0;
	virtual bool IsUsingFBTexture(const model_t* model, int nSkin, int nBody, void /*IClientRenderable*/* pClientRenderable) const = 0;
	virtual const model_t* FindOrLoadModel(const char* name) { /*Warning("IVModelInfo::FindOrLoadModel is now obsolte.\n"); return NULL; */ }
	virtual MDLHandle_t GetCacheHandle(const model_t* model) const = 0;
	virtual int GetBrushModelPlaneCount(const model_t* model) const = 0;
	virtual void GetBrushModelPlane(const model_t* model, int nIndex, cplane_t& plane, Vector* pOrigin) const = 0;
	virtual int GetSurfacepropsForVirtualTerrain(int index) = 0;
	virtual void OnLevelChange() = 0;
	virtual int GetModelClientSideIndex(const char* name) const = 0;
	virtual int RegisterDynamicModel(const char* name, bool bClientSide) = 0;
	virtual bool IsDynamicModelLoading(int modelIndex) = 0;
	virtual void AddRefDynamicModel(int modelIndex) = 0;
	virtual void ReleaseDynamicModel(int modelIndex) = 0;
	virtual bool RegisterModelLoadCallback(int modelindex, IModelLoadCallback* pCallback, bool bCallImmediatelyIfLoaded = true) = 0;
	virtual void UnregisterModelLoadCallback(int modelindex, IModelLoadCallback* pCallback) = 0;
};

class IVModelInfoClient : public IVModelInfo
{
public:
	virtual void OnDynamicModelsStringTableChange(int nStringIndex, const char* pString, const void* pData) = 0;
	virtual const model_t* FindOrLoadModel(const char* name) = 0;
};

MAKE_INTERFACE_VERSION(IVModelInfoClient, ModelInfoClient, "engine.dll", "VModelInfoClient006");

// ================ SDK/Definitions/Interfaces/IVModelRender.h ================
class IClientRenderable;
struct model_t;

typedef unsigned short ModelInstanceHandle_t;

enum
{
	MODEL_INSTANCE_INVALID = (ModelInstanceHandle_t)~0
};

// ================ SDK/Definitions/Misc/VPlane.h ================

class VPlane
{
public:
	Vector m_Normal;
	vec_t m_Dist;
};

// ================ SDK/Definitions/Interfaces/IVRenderView.h ================
class IClientRenderable;
class IClientEntity;
class CViewSetup;
struct model_t;

enum
{
	FRUSTUM_RIGHT = 0,
	FRUSTUM_LEFT = 1,
	FRUSTUM_TOP = 2,
	FRUSTUM_BOTTOM = 3,
	FRUSTUM_NEARZ = 4,
	FRUSTUM_FARZ = 5,
	FRUSTUM_NUMPLANES = 6
};

enum
{
	DRAWWORLDLISTS_DRAW_STRICTLYABOVEWATER = 0x001,
	DRAWWORLDLISTS_DRAW_STRICTLYUNDERWATER = 0x002,
	DRAWWORLDLISTS_DRAW_INTERSECTSWATER = 0x004,
	DRAWWORLDLISTS_DRAW_WATERSURFACE = 0x008,
	DRAWWORLDLISTS_DRAW_SKYBOX = 0x010,
	DRAWWORLDLISTS_DRAW_CLIPSKYBOX = 0x020,
	DRAWWORLDLISTS_DRAW_SHADOWDEPTH = 0x040,
	DRAWWORLDLISTS_DRAW_REFRACTION = 0x080,
	DRAWWORLDLISTS_DRAW_REFLECTION = 0x100,
	DRAWWORLDLISTS_DRAW_SSAO = 0x800
};

enum
{
	MAT_SORT_GROUP_STRICTLY_ABOVEWATER = 0,
	MAT_SORT_GROUP_STRICTLY_UNDERWATER,
	MAT_SORT_GROUP_INTERSECTS_WATER_SURFACE,
	MAT_SORT_GROUP_WATERSURFACE,
	MAX_MAT_SORT_GROUPS
};

enum ERenderDepthMode
{
	DEPTH_MODE_NORMAL = 0,
	DEPTH_MODE_SHADOW = 1,
	DEPTH_MODE_SSA0 = 2,
	DEPTH_MODE_OVERRIDE = 3,
	DEPTH_MODE_MAX
};

typedef VPlane Frustum[FRUSTUM_NUMPLANES];

typedef unsigned short LeafIndex_t;
typedef short LeafFogVolume_t;

enum
{
	INVALID_LEAF_INDEX = (LeafIndex_t)~0
};

struct WorldListInfo_t
{
	int m_ViewFogVolume;
	int m_LeafCount;
	LeafIndex_t* m_pLeafList;
	LeafFogVolume_t* m_pLeafFogVolume;
};

class IWorldRenderList : public IRefCounted
{
};

struct VisibleFogVolumeInfo_t
{
	int m_nVisibleFogVolume;
	int m_nVisibleFogVolumeLeaf;
	bool m_bEyeInFogVolume;
	float m_flDistanceToWater;
	float m_flWaterHeight;
	IMaterial* m_pFogVolumeMaterial;
};

struct BrushVertex_t
{
	Vector m_Pos;
	Vector m_Normal;
	Vector m_TangentS;
	Vector m_TangentT;
	Vector2D m_TexCoord;
	Vector2D m_LightmapCoord;
};

struct VisOverrideData_t
{
	Vector m_vecVisOrigin;
	float m_fDistToAreaPortalTolerance;
};

class IBrushSurface
{
public:
	virtual void ComputeTextureCoordinate(Vector const& worldPos, Vector2D& texCoord) = 0;
	virtual void ComputeLightmapCoordinate(Vector const& worldPos, Vector2D& lightmapCoord) = 0;
	virtual int  GetVertexCount() const = 0;
	virtual void GetVertexData(BrushVertex_t* pVerts) = 0;
	virtual IMaterial* GetMaterial() = 0;
};

class IBrushRenderer
{
public:
	virtual bool RenderBrushModelSurface(IClientEntity* pBaseEntity, IBrushSurface* pBrushSurface) = 0;
};

enum DrawBrushModelMode_t
{
	DBM_DRAW_ALL = 0,
	DBM_DRAW_OPAQUE_ONLY,
	DBM_DRAW_TRANSLUCENT_ONLY
};

enum
{
	VIEW_SETUP_VIS_EX_RETURN_FLAGS_USES_RADIAL_VIS = 0x00000001
};

class IVRenderView
{
public:
	virtual void DrawBrushModel(IClientEntity* baseentity, model_t* model, const Vector& origin, const QAngle& angles, bool bUnused) = 0;
	virtual void DrawIdentityBrushModel(IWorldRenderList* pList, model_t* model) = 0;
	virtual void TouchLight(struct dlight_t* light) = 0;
	virtual void Draw3DDebugOverlays(void) = 0;
	virtual void SetBlend(float blend) = 0;
	virtual float GetBlend(void) = 0;
	virtual void SetColorModulation(float const* blend) = 0;
	virtual void GetColorModulation(float* blend) = 0;
	virtual void SceneBegin(void) = 0;
	virtual void SceneEnd(void) = 0;
	virtual void GetVisibleFogVolume(const Vector& eyePoint, VisibleFogVolumeInfo_t* pInfo) = 0;
	virtual IWorldRenderList* CreateWorldList() = 0;
	virtual void BuildWorldLists(IWorldRenderList* pList, WorldListInfo_t* pInfo, int iForceFViewLeaf, const VisOverrideData_t* pVisData = NULL, bool bShadowDepth = false, float* pReflectionWaterHeight = NULL) = 0;
	virtual void DrawWorldLists(IWorldRenderList* pList, unsigned long flags, float waterZAdjust) = 0;
	virtual void DrawTopView(bool enable) = 0;
	virtual void TopViewBounds(Vector2D const& mins, Vector2D const& maxs) = 0;
	virtual void DrawLights(void) = 0;
	virtual void DrawMaskEntities(void) = 0;
	virtual void DrawTranslucentSurfaces(IWorldRenderList* pList, int sortIndex, unsigned long flags, bool bShadowDepth) = 0;
	virtual void DrawLineFile(void) = 0;
	virtual void DrawLightmaps(IWorldRenderList* pList, int pageId) = 0;
	virtual void ViewSetupVis(bool novis, int numorigins, const Vector origin[]) = 0;
	virtual bool AreAnyLeavesVisible(int* leafList, int nLeaves) = 0;
	virtual	void VguiPaint(void) = 0;
	virtual void ViewDrawFade(byte* color, IMaterial* pMaterial) = 0;
	virtual void OLD_SetProjectionMatrix(float fov, float zNear, float zFar) = 0;
	virtual colorVec GetLightAtPoint(Vector& pos) = 0;
	virtual int GetViewEntity(void) = 0;
	virtual float GetFieldOfView(void) = 0;
	virtual unsigned char** GetAreaBits(void) = 0;
	virtual void SetFogVolumeState(int nVisibleFogVolume, bool bUseHeightFog) = 0;
	virtual void InstallBrushSurfaceRenderer(IBrushRenderer* pBrushRenderer) = 0;
	virtual void DrawBrushModelShadow(IClientRenderable* pRenderable) = 0;
	virtual	bool LeafContainsTranslucentSurfaces(IWorldRenderList* pList, int sortIndex, unsigned long flags) = 0;
	virtual bool DoesBoxIntersectWaterVolume(const Vector& mins, const Vector& maxs, int leafWaterDataID) = 0;
	virtual void SetAreaState(unsigned char chAreaBits[MAX_AREA_STATE_BYTES], unsigned char chAreaPortalBits[MAX_AREA_PORTAL_STATE_BYTES]) = 0;
	virtual void VGui_Paint(int mode) = 0;
	virtual void Push3DView(const CViewSetup& view, int nFlags, ITexture* pRenderTarget, Frustum frustumPlanes) = 0;
	virtual void Push2DView(const CViewSetup& view, int nFlags, ITexture* pRenderTarget, Frustum frustumPlanes) = 0;
	virtual void PopView(Frustum frustumPlanes) = 0;
	virtual void SetMainView(const Vector& vecOrigin, const QAngle& angles) = 0;
	virtual void ViewSetupVisEx(bool novis, int numorigins, const Vector origin[], unsigned int& returnFlags) = 0;
	virtual void OverrideViewFrustum(Frustum custom) = 0;
	virtual void DrawBrushModelShadowDepth(IClientEntity* baseentity, model_t* model, const Vector& origin, const QAngle& angles, ERenderDepthMode DepthMode) = 0;
	virtual void UpdateBrushModelLightmap(model_t* model, IClientRenderable* pRenderable) = 0;
	virtual void BeginUpdateLightmaps(void) = 0;
	virtual void EndUpdateLightmaps(void) = 0;
	virtual void OLD_SetOffCenterProjectionMatrix(float fov, float zNear, float zFar, float flAspectRatio, float flBottom, float flTop, float flLeft, float flRight) = 0;
	virtual void OLD_SetProjectionMatrixOrtho(float left, float top, float right, float bottom, float zNear, float zFar) = 0;
	virtual void Push3DView(const CViewSetup& view, int nFlags, ITexture* pRenderTarget, Frustum frustumPlanes, ITexture* pDepthTexture) = 0;
	virtual void GetMatricesForView(const CViewSetup& view, VMatrix* pWorldToView, VMatrix* pViewToProjection, VMatrix* pWorldToProjection, VMatrix* pWorldToPixels) = 0;
	virtual void DrawBrushModelEx(IClientEntity* baseentity, model_t* model, const Vector& origin, const QAngle& angles, DrawBrushModelMode_t mode) = 0;

	inline Color_t GetColorModulation()
	{
		float blend[3] = {};
		GetColorModulation(blend);
		return { byte(blend[0] * 255), byte(blend[1] * 255), byte(blend[2] * 255) };
	}

	inline void SetColorModulation(float r, float g, float b)
	{
		float blend[3] = { r, g, b };
		SetColorModulation(blend);
	}

	inline void SetColorModulation(Color_t clr)
	{
		float blend[3] = { clr.r / 255.f, clr.g / 255.f, clr.b / 255.f };
		SetColorModulation(blend);
	}
};

MAKE_INTERFACE_VERSION(IVRenderView, RenderView, "engine.dll", "VEngineRenderView014");

// ================ SDK/Definitions/Misc/IPrediction.h ================
class IPrediction
{
public:
	virtual ~IPrediction(void) {};
	virtual void Init(void) = 0;
	virtual void Shutdown(void) = 0;
	virtual void Update(int startframe, bool validframe, int incoming_acknowledged, int outgoing_command) = 0;
	virtual void PreEntityPacketReceived(int commands_acknowledged, int current_world_update_packet) = 0;
	virtual void PostEntityPacketReceived(void) = 0;
	virtual void PostNetworkDataReceived(int commands_acknowledged) = 0;
	virtual void OnReceivedUncompressedPacket(void) = 0;
	virtual void GetViewOrigin(Vector& org) = 0;
	virtual void SetViewOrigin(Vector& org) = 0;
	virtual void GetViewAngles(QAngle& ang) = 0;
	virtual void SetViewAngles(QAngle& ang) = 0;
	virtual void GetLocalViewAngles(QAngle& ang) = 0;
	virtual void SetLocalViewAngles(QAngle& ang) = 0;
};

// ================ SDK/Definitions/Interfaces/Prediction.h ================

class CMoveData;
class CUserCmd;
class CBasePlayer;

// WARNING: do NOT count vtable slots off this listing - it is written in CPrediction's declaration order,
// not vtable order. MSVC puts the IPrediction BASE slots first (see Misc/IPrediction.h: dtor=0 ...
// GetViewAngles=10, SetViewAngles=11, GetLocalViewAngles=12, SetLocalViewAngles=13) and appends the virtuals
// NEW to CPrediction after them: InPrediction=14, IsFirstTimePredicted=15, GetIncomingPacketNumber=16,
// RunCommand=17, SetupMove=18, FinishMove=19, SetIdealPitch=20, _Update=21. Direct C++ calls through this
// class resolve correctly either way; only hand-computed GetVirtual() indices go wrong (the Clean POV demos
// hook once detoured IsFirstTimePredicted because "15" was counted from this list).
class CPrediction : public IPrediction
{
public:
	virtual ~CPrediction() {};
	virtual void Init() = 0;
	virtual void Shutdown() = 0;
	virtual void Update(int startframe, bool validframe, int incoming_acknowledged, int outgoing_command) = 0;
	virtual void OnReceivedUncompressedPacket() = 0;
	virtual void PreEntityPacketReceived(int commands_acknowledged, int current_world_update_packet) = 0;
	virtual void PostEntityPacketReceived() = 0;
	virtual void PostNetworkDataReceived(int commands_acknowledged) = 0;
	virtual bool InPrediction() = 0;
	virtual bool IsFirstTimePredicted() = 0;
	virtual int	GetIncomingPacketNumber() = 0;
	virtual void GetViewOrigin(Vec3& org) = 0;
	virtual void SetViewOrigin(Vec3& org) = 0;
	virtual void GetViewAngles(QAngle& ang) = 0;
	virtual void SetViewAngles(QAngle& ang) = 0;
	virtual void GetLocalViewAngles(QAngle& ang) = 0;
	virtual void SetLocalViewAngles(QAngle& ang) = 0;
	virtual void RunCommand(CBasePlayer* player, CUserCmd* ucmd, IMoveHelper* moveHelper) = 0;
	virtual void SetupMove(CBasePlayer* player, CUserCmd* ucmd, IMoveHelper* pHelper, CMoveData* move) = 0;
	virtual void FinishMove(CBasePlayer* player, CUserCmd* ucmd, CMoveData* move) = 0;
	virtual void SetIdealPitch(CBasePlayer* player, const Vec3& origin, const Vec3& angles, const Vec3& viewheight) = 0;
	virtual void _Update(bool received_new_world_update, bool validframe, int incoming_acknowledged, int outgoing_command) = 0;

	CHandle<CBaseEntity> m_hLastGround;
	bool m_bInPrediction;
	bool m_bFirstTimePredicted;
	bool m_bOldCLPredictValue;
	bool m_bEnginePaused;
	int m_nPreviousStartFrame;
	int m_nCommandsPredicted;
	int m_nServerCommandsAcknowledged;
	int m_bPreviousAckHadErrors;
	int m_bPreviousAckErrorTriggersFullLatchReset;
	CUtlVector<CHandle<CBaseEntity>> m_EntsWithPredictionErrorsInLastAck;
	int m_nIncomingPacketNumber;
	float m_flIdealPitch;
};

MAKE_INTERFACE_VERSION(CPrediction, Prediction, "client.dll", "VClientPrediction001");

// ================ SDK/Definitions/Interfaces/ViewRenderBeams.h ================
class CBaseEntity;

enum
{
	FBEAM_STARTENTITY = 0x00000001,
	FBEAM_ENDENTITY = 0x00000002,
	FBEAM_FADEIN = 0x00000004,
	FBEAM_FADEOUT = 0x00000008,
	FBEAM_SINENOISE = 0x00000010,
	FBEAM_SOLID = 0x00000020,
	FBEAM_SHADEIN = 0x00000040,
	FBEAM_SHADEOUT = 0x00000080,
	FBEAM_ONLYNOISEONCE = 0x00000100,		// Only calculate our noise once
	FBEAM_NOTILE = 0x00000200,
	FBEAM_USE_HITBOXES = 0x00000400,		// Attachment indices represent hitbox indices instead when this is set.
	FBEAM_STARTVISIBLE = 0x00000800,		// Has this client actually seen this beam's start entity yet?
	FBEAM_ENDVISIBLE = 0x00001000,		// Has this client actually seen this beam's end entity yet?
	FBEAM_ISACTIVE = 0x00002000,
	FBEAM_FOREVER = 0x00004000,
	FBEAM_HALOBEAM = 0x00008000,		// When drawing a beam with a halo, don't ignore the segments and endwidth
	FBEAM_REVERSED = 0x00010000,
	NUM_BEAM_FLAGS = 17	// KEEP THIS UPDATED!
};

// ================ SDK/Definitions/Interfaces/VPhysics.h ================
#define k_flMaxVelocity 2000.0f
#define k_flMaxAngularVelocity 360.0f * 10.0f
#define DEFAULT_MIN_FRICTION_MASS 10.0f
#define DEFAULT_MAX_FRICTION_MASS 2500.0f

struct vcollide_t;

enum
{
	COLLIDE_POLY = 0,
	COLLIDE_MOPP = 1,
	COLLIDE_BALL = 2,
	COLLIDE_VIRTUAL = 3,
};

class IPhysCollide
{
public:
	virtual ~IPhysCollide() {}
	//virtual void AddReference() = 0;
	//virtual void ReleaseReference() = 0;

	// get a surface manager 
	virtual void* CreateSurfaceManager(short&) const = 0;
	virtual void GetAllLedges(void* ledges) const = 0;
	virtual unsigned int GetSerializationSize() const = 0;
	virtual unsigned int SerializeToBuffer(char* pDest, bool bSwap = false) const = 0;
	virtual int GetVCollideIndex() const = 0;
	virtual Vec3 GetMassCenter() const = 0;
	virtual void SetMassCenter(const Vec3& massCenter) = 0;
	virtual Vec3 GetOrthographicAreas() const = 0;
	virtual void SetOrthographicAreas(const Vec3& areas) = 0;
	virtual float GetSphereRadius() const = 0;
	virtual void OutputDebugInfo() const = 0;
};

class CPhysCollide : public IPhysCollide
{
public:
	virtual Vec3 GetOrthographicAreas() const { return Vec3(1, 1, 1); }
	virtual float GetSphereRadius() const { return 0; }
	virtual void SetOrthographicAreas(const Vec3& areas) {}
};

// ------------------------------------------------------------------------------------
// UNITS:
// ------------------------------------------------------------------------------------
// NOTE:  Coordinates are in HL units.  1 unit == 1 inch.  X is east (forward), Y is north (left), Z is up (up)
// QAngle are pitch (around y), Yaw (around Z), Roll (around X)
// AngularImpulse are exponetial maps (an axis in HL units scaled by a "twist" angle in degrees)
//		They can be transformed like normals/covectors and added linearly
// mass is kg, volume is in^3, acceleration is in/s^2, velocity is in/s

// density is kg/m^3 (water ~= 998 at room temperature)
// preferably, these would be in kg/in^3, but the range of those numbers makes them not very human readable
// having water be about 1000 is really convenient for data entry.
// Since volume is in in^3 and density is in kg/m^3: 
//	density = (mass / volume) * CUBIC_METERS_PER_CUBIC_INCH
// Force is applied using impulses (kg*in/s)
// Torque is applied using impulses (kg*degrees/s)
// ------------------------------------------------------------------------------------

// 2.2 lbs / kg

// convert from pounds to kg

class IPhysicsShadowController;
class IPhysicsObject
{
public:
	virtual ~IPhysicsObject(void) {}
	virtual bool IsStatic() const = 0;
	virtual bool IsAsleep() const = 0;
	virtual bool IsTrigger() const = 0;
	virtual bool IsFluid() const = 0;
	virtual bool IsHinged() const = 0;
	virtual bool IsCollisionEnabled() const = 0;
	virtual bool IsGravityEnabled() const = 0;
	virtual bool IsDragEnabled() const = 0;
	virtual bool IsMotionEnabled() const = 0;
	virtual bool IsMoveable() const = 0;
	virtual bool IsAttachedToConstraint(bool bExternalOnly) const = 0;
	virtual void EnableCollisions(bool enable) = 0;
	virtual void EnableGravity(bool enable) = 0;
	virtual void EnableDrag(bool enable) = 0;
	virtual void EnableMotion(bool enable) = 0;
	virtual void SetGameData(void* pGameData) = 0;
	virtual void* GetGameData(void) const = 0;
	virtual void SetGameFlags(unsigned short userFlags) = 0;
	virtual unsigned short GetGameFlags(void) const = 0;
	virtual void SetGameIndex(unsigned short gameIndex) = 0;
	virtual unsigned short GetGameIndex(void) const = 0;
	virtual void SetCallbackFlags(unsigned short callbackflags) = 0;
	virtual unsigned short GetCallbackFlags(void) const = 0;
	virtual void Wake(void) = 0;
	virtual void Sleep(void) = 0;
	virtual void RecheckCollisionFilter() = 0;
	virtual void RecheckContactPoints() = 0;
	virtual void SetMass(float mass) = 0;
	virtual float GetMass(void) const = 0;
	virtual float GetInvMass(void) const = 0;
	virtual Vec3 GetInertia(void) const = 0;
	virtual Vec3 GetInvInertia(void) const = 0;
	virtual void SetInertia(const Vec3& inertia) = 0;
	virtual void SetDamping(const float* speed, const float* rot) = 0;
	virtual void GetDamping(float* speed, float* rot) const = 0;
	virtual void SetDragCoefficient(float* pDrag, float* pAngularDrag) = 0;
	virtual void SetBuoyancyRatio(float ratio) = 0;
	virtual int GetMaterialIndex() const = 0;
	virtual void SetMaterialIndex(int materialIndex) = 0;
	virtual unsigned int GetContents() const = 0;
	virtual void SetContents(unsigned int contents) = 0;
	virtual float GetSphereRadius() const = 0;
	virtual float GetEnergy() const = 0;
	virtual Vec3 GetMassCenterLocalSpace() const = 0;
	virtual void SetPosition(const Vec3& worldPosition, const QAngle& angles, bool isTeleport) = 0;
	virtual void SetPositionMatrix(const matrix3x4& matrix, bool isTeleport) = 0;
	virtual void GetPosition(Vec3* worldPosition, QAngle* angles) const = 0;
	virtual void GetPositionMatrix(matrix3x4* positionMatrix) const = 0;
	virtual void SetVelocity(const Vec3* velocity, const Vec3* angularVelocity) = 0;
	virtual void SetVelocityInstantaneous(const Vec3* velocity, const Vec3* angularVelocity) = 0;
	virtual void GetVelocity(Vec3* velocity, Vec3* angularVelocity) const = 0;
	virtual void AddVelocity(const Vec3* velocity, const Vec3* angularVelocity) = 0;
	virtual void GetVelocityAtPoint(const Vec3& worldPosition, Vec3* pVelocity) const = 0;
	virtual void GetImplicitVelocity(Vec3* velocity, Vec3* angularVelocity) const = 0;
	virtual void LocalToWorld(Vec3* worldPosition, const Vec3& localPosition) const = 0;
	virtual void WorldToLocal(Vec3* localPosition, const Vec3& worldPosition) const = 0;
	virtual void LocalToWorldVector(Vec3* worldVector, const Vec3& localVector) const = 0;
	virtual void WorldToLocalVector(Vec3* localVector, const Vec3& worldVector) const = 0;
	virtual void ApplyForceCenter(const Vec3& forceVector) = 0;
	virtual void ApplyForceOffset(const Vec3& forceVector, const Vec3& worldPosition) = 0;
	virtual void ApplyTorqueCenter(const Vec3& torque) = 0;
	virtual void CalculateForceOffset(const Vec3& forceVector, const Vec3& worldPosition, Vec3* centerForce, Vec3* centerTorque) const = 0;
	virtual void CalculateVelocityOffset(const Vec3& forceVector, const Vec3& worldPosition, Vec3* centerVelocity, Vec3* centerAngularVelocity) const = 0;
	virtual float CalculateLinearDrag(const Vec3& unitDirection) const = 0;
	virtual float CalculateAngularDrag(const Vec3& objectSpaceRotationAxis) const = 0;
	virtual bool GetContactPoint(Vec3* contactPoint, IPhysicsObject** contactObject) const = 0;
	virtual void SetShadow(float maxSpeed, float maxAngularSpeed, bool allowPhysicsMovement, bool allowPhysicsRotation) = 0;
	virtual void UpdateShadow(const Vec3& targetPosition, const QAngle& targetAngles, bool tempDisableGravity, float timeOffset) = 0;
	virtual int GetShadowPosition(Vec3* position, QAngle* angles) const = 0;
	virtual /*IPhysicsShadowController*/void* GetShadowController(void) const = 0;
	virtual void RemoveShadowController() = 0;
	virtual float ComputeShadowControl(/*const hlshadowcontrol_params_t &*/void* params, float secondsToArrival, float dt) = 0;
	virtual const CPhysCollide* GetCollide(void) const = 0;
	virtual const char* GetName() const = 0;
	virtual void BecomeTrigger() = 0;
	virtual void RemoveTrigger() = 0;
	virtual void BecomeHinged(int localAxis) = 0;
	virtual void RemoveHinged() = 0;
	virtual /*IPhysicsFrictionSnapshot*/void* CreateFrictionSnapshot() = 0;
	virtual void DestroyFrictionSnapshot(/*IPhysicsFrictionSnapshot*/void* pSnapshot) = 0;
	virtual void OutputDebugInfo() const = 0;

	void* m_pGameData;
	void* m_pObject;
	const CPhysCollide* m_pCollide;
	IPhysicsShadowController* m_pShadow;

	Vec3			m_dragBasis;
	Vec3			m_angDragBasis;

	// these 5 should pack into a short
	// pack new bools here
	bool			m_shadowTempGravityDisable : 5;
	bool			m_hasTouchedDynamic : 1;
	bool			m_asleepSinceCreation : 1;
	bool			m_forceSilentDelete : 1;
	unsigned char	m_sleepState : 2;
	unsigned char	m_hingedAxis : 3;
	unsigned char	m_collideType : 3;
	unsigned short	m_gameIndex;

private:
	unsigned short	m_materialIndex;
	unsigned short	m_activeIndex;

	unsigned short	m_callbacks;
	unsigned short	m_gameFlags;
	unsigned int	m_contentsMask;

	float			m_volume;
	float			m_buoyancyRatio;
	float			m_dragCoefficient;
	float			m_angDragCoefficient;
};
class IPhysicsEnvironment;
class IPhysicsSurfaceProps;
class IPhysicsConstraint;
class IPhysicsConstraintGroup;
class IPhysicsFluidController;
class IPhysicsSpring;
class IPhysicsVehicleController;
class IConvexInfo;
class IPhysicsObjectPairHash;
class IPhysicsCollisionSet;
class IPhysicsPlayerController;

struct Ray_t;
struct constraint_ragdollparams_t;
struct constraint_hingeparams_t;
struct constraint_fixedparams_t;
struct constraint_ballsocketparams_t;
struct constraint_slidingparams_t;
struct constraint_pulleyparams_t;
struct constraint_lengthparams_t;
struct constraint_groupparams_t;

struct vehicleparams_t;

struct fluidparams_t;
struct springparams_t;
struct objectparams_t
{
	Vec3* massCenterOverride;
	float		mass;
	float		inertia;
	float		damping;
	float		rotdamping;
	float		rotInertiaLimit;
	const char* pName;				// used only for debugging
	void* pGameData;
	float		volume;
	float		dragCoefficient;
	bool		enableCollisions;
};
class CGameTrace;
typedef CGameTrace trace_t;
struct physics_stats_t;
struct physics_performanceparams_t
{
	int		maxCollisionsPerObjectPerTimestep;		// object will be frozen after this many collisions (visual hitching vs. CPU cost)
	int		maxCollisionChecksPerTimestep;			// objects may penetrate after this many collision checks (can be extended in AdditionalCollisionChecksThisTick)
	float	maxVelocity;							// limit world space linear velocity to this (in / s)
	float	maxAngularVelocity;						// limit world space angular velocity to this (degrees / s)
	float	lookAheadTimeObjectsVsWorld;			// predict collisions this far (seconds) into the future
	float	lookAheadTimeObjectsVsObject;			// predict collisions this far (seconds) into the future
	float	minFrictionMass;						// min mass for friction solves (constrains dynamic range of mass to improve stability)
	float	maxFrictionMass;						// mas mass for friction solves

	void Defaults()
	{
		maxCollisionsPerObjectPerTimestep = 6;
		maxCollisionChecksPerTimestep = 250;
		maxVelocity = k_flMaxVelocity;
		maxAngularVelocity = k_flMaxAngularVelocity;
		lookAheadTimeObjectsVsWorld = 1.0f;
		lookAheadTimeObjectsVsObject = 0.5f;
		minFrictionMass = DEFAULT_MIN_FRICTION_MASS;
		maxFrictionMass = DEFAULT_MAX_FRICTION_MASS;
	}
};
struct virtualmeshparams_t;

//enum PhysInterfaceId_t;
struct physsaveparams_t;
struct physrestoreparams_t;
struct physprerestoreparams_t;

class IVPhysicsDebugOverlay
{
public:
	virtual void AddEntityTextOverlay(int ent_index, int line_offset, float duration, int r, int g, int b, int a, const char* format, ...) = 0;
	virtual void AddBoxOverlay(const Vec3& origin, const Vec3& mins, const Vec3& max, QAngle const& orientation, int r, int g, int b, int a, float duration) = 0;
	virtual void AddTriangleOverlay(const Vec3& p1, const Vec3& p2, const Vec3& p3, int r, int g, int b, int a, bool noDepthTest, float duration) = 0;
	virtual void AddLineOverlay(const Vec3& origin, const Vec3& dest, int r, int g, int b, bool noDepthTest, float duration) = 0;
	virtual void AddTextOverlay(const Vec3& origin, float duration, const char* format, ...) = 0;
	virtual void AddTextOverlay(const Vec3& origin, int line_offset, float duration, const char* format, ...) = 0;
	virtual void AddScreenTextOverlay(float flXPos, float flYPos, float flDuration, int r, int g, int b, int a, const char* text) = 0;
	virtual void AddSweptBoxOverlay(const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& max, const QAngle& angles, int r, int g, int b, int a, float flDuration) = 0;
	virtual void AddTextOverlayRGB(const Vec3& origin, int line_offset, float duration, float r, float g, float b, float alpha, const char* format, ...) = 0;
};

class IPhysics : public IAppSystem
{
public:
	virtual IPhysicsEnvironment* CreateEnvironment(void) = 0;
	virtual void DestroyEnvironment(IPhysicsEnvironment*) = 0;
	virtual IPhysicsEnvironment* GetActiveEnvironmentByIndex(int index) = 0;

	// Creates a fast hash of pairs of objects
	// Useful for maintaining a table of object relationships like pairs that do not collide.
	virtual IPhysicsObjectPairHash* CreateObjectPairHash() = 0;
	virtual void						DestroyObjectPairHash(IPhysicsObjectPairHash* pHash) = 0;

	// holds a cache of these by id.  So you can get by id to search for the previously created set
	// UNDONE: Sets are currently limited to 32 elements.  More elements will return NULL in create.
	// NOTE: id is not allowed to be zero.
	virtual IPhysicsCollisionSet* FindOrCreateCollisionSet(unsigned int id, int maxElementCount) = 0;
	virtual IPhysicsCollisionSet* FindCollisionSet(unsigned int id) = 0;
	virtual void						DestroyAllCollisionSets() = 0;
};

// CPhysConvex is a single convex solid
class CPhysConvex;
// CPhysPolysoup is an abstract triangle soup mesh
class CPhysPolysoup;
class ICollisionQuery;
class IVPhysicsKeyParser;
struct convertconvexparams_t;

class CPolyhedron;

// UNDONE: Find a better place for this?  Should be in collisionutils, but it's needs VPHYSICS' solver.
struct truncatedcone_t
{
	Vec3	origin;
	Vec3	normal;
	float	h;			// height of the cone (hl units)
	float	theta;		// cone angle (degrees)
};

class IPhysicsCollision
{
public:
	virtual ~IPhysicsCollision(void) {}

	// produce a convex element from verts (convex hull around verts)
	virtual CPhysConvex* ConvexFromVerts(Vec3** pVerts, int vertCount) = 0;
	// produce a convex element from planes (csg of planes)
	virtual CPhysConvex* ConvexFromPlanes(float* pPlanes, int planeCount, float mergeDistance) = 0;
	// calculate volume of a convex element
	virtual float			ConvexVolume(CPhysConvex* pConvex) = 0;

	virtual float			ConvexSurfaceArea(CPhysConvex* pConvex) = 0;
	// store game-specific data in a convex solid
	virtual void			SetConvexGameData(CPhysConvex* pConvex, unsigned int gameData) = 0;
	// If not converted, free the convex elements with this call
	virtual void			ConvexFree(CPhysConvex* pConvex) = 0;
	virtual CPhysConvex* BBoxToConvex(const Vec3& mins, const Vec3& maxs) = 0;
	// produce a convex element from a convex polyhedron
	virtual CPhysConvex* ConvexFromConvexPolyhedron(const CPolyhedron& ConvexPolyhedron) = 0;
	// produce a set of convex triangles from a convex polygon, normal is assumed to be on the side with forward point ordering, which should be clockwise, output will need to be able to hold exactly (iPointCount-2) convexes
	virtual void			ConvexesFromConvexPolygon(const Vec3& vPolyNormal, const Vec3* pPoints, int iPointCount, CPhysConvex** pOutput) = 0;

	// concave objects
	// create a triangle soup
	virtual CPhysPolysoup* PolysoupCreate(void) = 0;
	// destroy the container and memory
	virtual void			PolysoupDestroy(CPhysPolysoup* pSoup) = 0;
	// add a triangle to the soup
	virtual void			PolysoupAddTriangle(CPhysPolysoup* pSoup, const Vec3& a, const Vec3& b, const Vec3& c, int materialIndex7bits) = 0;
	// convert the convex into a compiled collision model
	virtual CPhysCollide* ConvertPolysoupToCollide(CPhysPolysoup* pSoup, bool useMOPP) = 0;

	// Convert an array of convex elements to a compiled collision model (this deletes the convex elements)
	virtual CPhysCollide* ConvertConvexToCollide(CPhysConvex** pConvex, int convexCount) = 0;
	virtual CPhysCollide* ConvertConvexToCollideParams(CPhysConvex** pConvex, int convexCount, const convertconvexparams_t& convertParams) = 0;
	// Free a collide that was created with ConvertConvexToCollide()
	virtual void			DestroyCollide(CPhysCollide* pCollide) = 0;

	// Get the memory size in bytes of the collision model for serialization
	virtual int				CollideSize(CPhysCollide* pCollide) = 0;
	// serialize the collide to a block of memory
	virtual int				CollideWrite(char* pDest, CPhysCollide* pCollide, bool bSwap = false) = 0;
	// unserialize the collide from a block of memory
	virtual CPhysCollide* UnserializeCollide(char* pBuffer, int size, int index) = 0;

	// compute the volume of a collide
	virtual float			CollideVolume(CPhysCollide* pCollide) = 0;
	// compute surface area for tools
	virtual float			CollideSurfaceArea(CPhysCollide* pCollide) = 0;

	// Get the support map for a collide in the given direction
	virtual Vec3			CollideGetExtent(const CPhysCollide* pCollide, const Vec3& collideOrigin, const QAngle& collideAngles, const Vec3& direction) = 0;

	// Get an AABB for an oriented collision model
	virtual void			CollideGetAABB(Vec3* pMins, Vec3* pMaxs, const CPhysCollide* pCollide, const Vec3& collideOrigin, const QAngle& collideAngles) = 0;

	virtual void			CollideGetMassCenter(CPhysCollide* pCollide, Vec3* pOutMassCenter) = 0;
	virtual void			CollideSetMassCenter(CPhysCollide* pCollide, const Vec3& massCenter) = 0;
	// get the approximate cross-sectional area projected orthographically on the bbox of the collide
	// NOTE: These are fractional areas - unitless.  Basically this is the fraction of the OBB on each axis that
	// would be visible if the object were rendered orthographically.
	// NOTE: This has been precomputed when the collide was built or this function will return 1,1,1
	virtual Vec3			CollideGetOrthographicAreas(const CPhysCollide* pCollide) = 0;
	virtual void			CollideSetOrthographicAreas(CPhysCollide* pCollide, const Vec3& areas) = 0;

	// query the vcollide index in the physics model for the instance
	virtual int				CollideIndex(const CPhysCollide* pCollide) = 0;

	// Convert a bbox to a collide
	virtual CPhysCollide* BBoxToCollide(const Vec3& mins, const Vec3& maxs) = 0;
	virtual int				GetConvexesUsedInCollideable(const CPhysCollide* pCollideable, CPhysConvex** pOutputArray, int iOutputArrayLimit) = 0;

	// Trace an AABB against a collide
	virtual void TraceBox(const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& maxs, const CPhysCollide* pCollide, const Vec3& collideOrigin, const QAngle& collideAngles, trace_t* ptr) = 0;
	virtual void TraceBox(const Ray_t& ray, const CPhysCollide* pCollide, const Vec3& collideOrigin, const QAngle& collideAngles, trace_t* ptr) = 0;
	virtual void TraceBox(const Ray_t& ray, unsigned int contentsMask, IConvexInfo* pConvexInfo, const CPhysCollide* pCollide, const Vec3& collideOrigin, const QAngle& collideAngles, trace_t* ptr) = 0;

	// Trace one collide against another
	virtual void TraceCollide(const Vec3& start, const Vec3& end, const CPhysCollide* pSweepCollide, const QAngle& sweepAngles, const CPhysCollide* pCollide, const Vec3& collideOrigin, const QAngle& collideAngles, trace_t* ptr) = 0;

	// relatively slow test for box vs. truncated cone
	virtual bool			IsBoxIntersectingCone(const Vec3& boxAbsMins, const Vec3& boxAbsMaxs, const truncatedcone_t& cone) = 0;

	// loads a set of solids into a vcollide_t
	virtual void			VCollideLoad(vcollide_t* pOutput, int solidCount, const char* pBuffer, int size, bool swap = false) = 0;
	// destroyts the set of solids created by VCollideLoad
	virtual void			VCollideUnload(vcollide_t* pVCollide) = 0;

	// begins parsing a vcollide.  NOTE: This keeps pointers to the text
	// If you free the text and call members of IVPhysicsKeyParser, it will crash
	virtual IVPhysicsKeyParser* VPhysicsKeyParserCreate(const char* pKeyData) = 0;
	// Free the parser created by VPhysicsKeyParserCreate
	virtual void			VPhysicsKeyParserDestroy(IVPhysicsKeyParser* pParser) = 0;

	// creates a list of verts from a collision mesh
	virtual int				CreateDebugMesh(CPhysCollide const* pCollisionModel, Vec3** outVerts) = 0;
	// destroy the list of verts created by CreateDebugMesh
	virtual void			DestroyDebugMesh(int vertCount, Vec3* outVerts) = 0;

	// create a queryable version of the collision model
	virtual ICollisionQuery* CreateQueryModel(CPhysCollide* pCollide) = 0;
	// destroy the queryable version
	virtual void			DestroyQueryModel(ICollisionQuery* pQuery) = 0;

	virtual IPhysicsCollision* ThreadContextCreate(void) = 0;
	virtual void			ThreadContextDestroy(IPhysicsCollision* pThreadContex) = 0;

	virtual CPhysCollide* CreateVirtualMesh(const virtualmeshparams_t& params) = 0;
	virtual bool			SupportsVirtualMesh() = 0;

	virtual bool			GetBBoxCacheSize(int* pCachedSize, int* pCachedCount) = 0;

	// extracts a polyhedron that defines a CPhysConvex's shape
	virtual CPolyhedron* PolyhedronFromConvex(CPhysConvex* const pConvex, bool bUseTempPolyhedron) = 0;

	// dumps info about the collide to Msg()
	virtual void			OutputDebugInfo(const CPhysCollide* pCollide) = 0;
	virtual unsigned int	ReadStat(int statID) = 0;
};

MAKE_INTERFACE_VERSION(IPhysicsCollision, PhysicsCollision, "vphysics.dll", "VPhysicsCollision007");

// this can be used to post-process a collision model
class ICollisionQuery;   // body dropped: only ever named through a pointer here

//-----------------------------------------------------------------------------
// Purpose: Ray traces from game engine.
//-----------------------------------------------------------------------------
class IPhysicsGameTrace
{
public:
	virtual void VehicleTraceRay(const Ray_t& ray, void* pVehicle, trace_t* pTrace) = 0;
	virtual	void VehicleTraceRayWithWater(const Ray_t& ray, void* pVehicle, trace_t* pTrace) = 0;
	virtual bool VehiclePointInWater(const Vec3& vecPoint) = 0;
};

// The caller should implement this to return contents masks per convex on a collide
class IConvexInfo
{
public:
	virtual unsigned int GetContents(int convexGameData) = 0;
};

class IPhysicsCollisionEvent;   // body dropped: only ever named through a pointer here

class IPhysicsObjectEvent
{
public:
	// these can be used to optimize out queries on sleeping objects
	// Called when an object is woken after sleeping
	virtual void ObjectWake(IPhysicsObject* pObject) = 0;
	// called when an object goes to sleep (no longer simulating)
	virtual void ObjectSleep(IPhysicsObject* pObject) = 0;
};

class IPhysicsConstraintEvent
{
public:
	// the constraint is now inactive, the game code is required to delete it or re-activate it.
	virtual void ConstraintBroken(IPhysicsConstraint*) = 0;
};

// UNDONE: At some point allow this to be parameterized using hlshadowcontrol_params_t.
// All of the infrastructure is in place to do that.

class IPhysicsShadowController;   // body dropped: only ever named through a pointer here

class IPhysicsMotionController;

// Callback for simulation
class IMotionEvent
{
public:
	// These constants instruct the simulator as to how to apply the values copied to linear & angular
	// GLOBAL/LOCAL refer to the coordinate system of the values, whereas acceleration/force determine whether or not
	// mass is divided out (forces must be divided by mass to compute acceleration)
	enum simresult_e { SIM_NOTHING = 0, SIM_LOCAL_ACCELERATION, SIM_LOCAL_FORCE, SIM_GLOBAL_ACCELERATION, SIM_GLOBAL_FORCE };
	virtual simresult_e	Simulate(IPhysicsMotionController* pController, IPhysicsObject* pObject, float deltaTime, Vec3& linear, Vec3& angular) = 0;
};

class IPhysicsMotionController;   // body dropped: only ever named through a pointer here

// -------------------
// Collision filter function.  Return 0 if objects should not be tested for collisions, nonzero otherwise
// Install with IPhysicsEnvironment::SetCollisionFilter()
// -------------------
class IPhysicsCollisionSolver;   // body dropped: only ever named through a pointer here

enum PhysicsTraceType_t
{
	VPHYSICS_TRACE_EVERYTHING = 0,
	VPHYSICS_TRACE_STATIC_ONLY,
	VPHYSICS_TRACE_MOVING_ONLY,
	VPHYSICS_TRACE_TRIGGERS_ONLY,
	VPHYSICS_TRACE_STATIC_AND_MOVING,
};

class IPhysicsTraceFilter
{
public:
	virtual bool ShouldHitObject(IPhysicsObject* pObject, int contentsMask) = 0;
	virtual PhysicsTraceType_t	GetTraceType() const = 0;
};

class IPhysicsEnvironment
{
public:
	virtual ~IPhysicsEnvironment(void) {}

	virtual void SetDebugOverlay(CreateInterfaceFn debugOverlayFactory) = 0;
	virtual IVPhysicsDebugOverlay* GetDebugOverlay(void) = 0;

	// gravity is a 3-vector in in/s^2
	virtual void			SetGravity(const Vec3& gravityVector) = 0;
	virtual void			GetGravity(Vec3* pGravityVector) const = 0;

	// air density is in kg / m^3 (water is 1000)
	// This controls drag, air that is more dense has more drag.
	virtual void			SetAirDensity(float density) = 0;
	virtual float			GetAirDensity(void) const = 0;

	// object creation
	// create a polygonal object.  pCollisionModel was created by the physics builder DLL in a pre-process.
	virtual IPhysicsObject* CreatePolyObject(const CPhysCollide* pCollisionModel, int materialIndex, const Vec3& position, const QAngle& angles, objectparams_t* pParams) = 0;
	// same as above, but this one cannot move or rotate (infinite mass/inertia)
	virtual IPhysicsObject* CreatePolyObjectStatic(const CPhysCollide* pCollisionModel, int materialIndex, const Vec3& position, const QAngle& angles, objectparams_t* pParams) = 0;
	// Create a perfectly spherical object
	virtual IPhysicsObject* CreateSphereObject(float radius, int materialIndex, const Vec3& position, const QAngle& angles, objectparams_t* pParams, bool isStatic) = 0;
	// destroy an object created with CreatePolyObject() or CreatePolyObjectStatic()
	virtual void DestroyObject(IPhysicsObject*) = 0;

	// Create a polygonal fluid body out of the specified collision model
	// This object will affect any other objects that collide with the collision model
	virtual IPhysicsFluidController* CreateFluidController(IPhysicsObject* pFluidObject, fluidparams_t* pParams) = 0;
	// Destroy an object created with CreateFluidController()
	virtual void DestroyFluidController(IPhysicsFluidController*) = 0;

	// Create a simulated spring that connects 2 objects
	virtual IPhysicsSpring* CreateSpring(IPhysicsObject* pObjectStart, IPhysicsObject* pObjectEnd, springparams_t* pParams) = 0;
	virtual void DestroySpring(IPhysicsSpring*) = 0;

	// Create a constraint in the space of pReferenceObject which is attached by the constraint to pAttachedObject
	virtual IPhysicsConstraint* CreateRagdollConstraint(IPhysicsObject* pReferenceObject, IPhysicsObject* pAttachedObject, IPhysicsConstraintGroup* pGroup, const constraint_ragdollparams_t& ragdoll) = 0;
	virtual IPhysicsConstraint* CreateHingeConstraint(IPhysicsObject* pReferenceObject, IPhysicsObject* pAttachedObject, IPhysicsConstraintGroup* pGroup, const constraint_hingeparams_t& hinge) = 0;
	virtual IPhysicsConstraint* CreateFixedConstraint(IPhysicsObject* pReferenceObject, IPhysicsObject* pAttachedObject, IPhysicsConstraintGroup* pGroup, const constraint_fixedparams_t& fixed) = 0;
	virtual IPhysicsConstraint* CreateSlidingConstraint(IPhysicsObject* pReferenceObject, IPhysicsObject* pAttachedObject, IPhysicsConstraintGroup* pGroup, const constraint_slidingparams_t& sliding) = 0;
	virtual IPhysicsConstraint* CreateBallsocketConstraint(IPhysicsObject* pReferenceObject, IPhysicsObject* pAttachedObject, IPhysicsConstraintGroup* pGroup, const constraint_ballsocketparams_t& ballsocket) = 0;
	virtual IPhysicsConstraint* CreatePulleyConstraint(IPhysicsObject* pReferenceObject, IPhysicsObject* pAttachedObject, IPhysicsConstraintGroup* pGroup, const constraint_pulleyparams_t& pulley) = 0;
	virtual IPhysicsConstraint* CreateLengthConstraint(IPhysicsObject* pReferenceObject, IPhysicsObject* pAttachedObject, IPhysicsConstraintGroup* pGroup, const constraint_lengthparams_t& length) = 0;

	virtual void DestroyConstraint(IPhysicsConstraint*) = 0;

	virtual IPhysicsConstraintGroup* CreateConstraintGroup(const constraint_groupparams_t& groupParams) = 0;
	virtual void DestroyConstraintGroup(IPhysicsConstraintGroup* pGroup) = 0;

	virtual IPhysicsShadowController* CreateShadowController(IPhysicsObject* pObject, bool allowTranslation, bool allowRotation) = 0;
	virtual void						DestroyShadowController(IPhysicsShadowController*) = 0;

	virtual IPhysicsPlayerController* CreatePlayerController(IPhysicsObject* pObject) = 0;
	virtual void						DestroyPlayerController(IPhysicsPlayerController*) = 0;

	virtual IPhysicsMotionController* CreateMotionController(IMotionEvent* pHandler) = 0;
	virtual void						DestroyMotionController(IPhysicsMotionController* pController) = 0;

	virtual IPhysicsVehicleController* CreateVehicleController(IPhysicsObject* pVehicleBodyObject, const vehicleparams_t& params, unsigned int nVehicleType, IPhysicsGameTrace* pGameTrace) = 0;
	virtual void						DestroyVehicleController(IPhysicsVehicleController*) = 0;

	// install a function to filter collisions/penentration
	virtual void			SetCollisionSolver(IPhysicsCollisionSolver* pSolver) = 0;

	// run the simulator for deltaTime seconds
	virtual void			Simulate(float deltaTime) = 0;
	// true if currently running the simulator (i.e. in a callback during physenv->Simulate())
	virtual bool			IsInSimulation() const = 0;

	// Manage the timestep (period) of the simulator.  The main functions are all integrated with
	// this period as dt.
	virtual float			GetSimulationTimestep() const = 0;
	virtual void			SetSimulationTimestep(float timestep) = 0;

	// returns the current simulation clock's value.  This is an absolute time.
	virtual float			GetSimulationTime() const = 0;
	virtual void			ResetSimulationClock() = 0;
	// returns the current simulation clock's value at the next frame.  This is an absolute time.
	virtual float			GetNextFrameTime(void) const = 0;

	// Collision callbacks (game code collision response)
	virtual void			SetCollisionEventHandler(IPhysicsCollisionEvent* pCollisionEvents) = 0;
	virtual void			SetObjectEventHandler(IPhysicsObjectEvent* pObjectEvents) = 0;
	virtual	void			SetConstraintEventHandler(IPhysicsConstraintEvent* pConstraintEvents) = 0;

	virtual void			SetQuickDelete(bool bQuick) = 0;

	virtual int				GetActiveObjectCount() const = 0;
	virtual void			GetActiveObjects(IPhysicsObject** pOutputObjectList) const = 0;
	virtual const IPhysicsObject** GetObjectList(int* pOutputObjectCount) const = 0;
	virtual bool			TransferObject(IPhysicsObject* pObject, IPhysicsEnvironment* pDestinationEnvironment) = 0;

	virtual void			CleanupDeleteList(void) = 0;
	virtual void			EnableDeleteQueue(bool enable) = 0;

	// Save/Restore methods
	virtual bool			Save(const physsaveparams_t& params) = 0;
	virtual void			PreRestore(const physprerestoreparams_t& params) = 0;
	virtual bool			Restore(const physrestoreparams_t& params) = 0;
	virtual void			PostRestore() = 0;

	// Debugging:
	virtual bool IsCollisionModelUsed(CPhysCollide* pCollide) const = 0;

	// Physics world version of the enginetrace API:
	virtual void TraceRay(const Ray_t& ray, unsigned int fMask, IPhysicsTraceFilter* pTraceFilter, trace_t* pTrace) = 0;
	virtual void SweepCollideable(const CPhysCollide* pCollide, const Vec3& vecAbsStart, const Vec3& vecAbsEnd,
		const QAngle& vecAngles, unsigned int fMask, IPhysicsTraceFilter* pTraceFilter, trace_t* pTrace) = 0;

	// performance tuning
	virtual void GetPerformanceSettings(physics_performanceparams_t* pOutput) const = 0;
	virtual void SetPerformanceSettings(const physics_performanceparams_t* pSettings) = 0;

	// perf/cost statistics
	virtual void ReadStats(physics_stats_t* pOutput) = 0;
	virtual void ClearStats() = 0;

	virtual unsigned int	GetObjectSerializeSize(IPhysicsObject* pObject) const = 0;
	virtual void			SerializeObjectToBuffer(IPhysicsObject* pObject, unsigned char* pBuffer, unsigned int bufferSize) = 0;
	virtual IPhysicsObject* UnserializeObjectFromBuffer(void* pGameData, unsigned char* pBuffer, unsigned int bufferSize, bool enableCollisions) = 0;

	virtual void EnableConstraintNotify(bool bEnable) = 0;
	virtual void DebugCheckContacts(void) = 0;
};

MAKE_INTERFACE_VERSION(IPhysics, Physics, "vphysics.dll", "VPhysics031");

// ================ SDK/Definitions/Interfaces.h ================

// DirectX interface with conflict resolution
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN  
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d9.h>

class CNullInterfaces
{
private:
	bool m_bFailed = false;

public:
	bool Initialize();
};

ADD_FEATURE_CUSTOM(CNullInterfaces, Interfaces, H);

// ================ SDK/Definitions/Main/IClientUnknown.h ================
class IClientNetworkable;
class CBaseEntity;
class IClientRenderable;
class ICollideable;
class IClientEntity;
class IClientThinkable;

class IClientUnknown : public IHandleEntity
{
public:
	virtual ICollideable* GetCollideable() = 0;
	virtual IClientNetworkable* GetClientNetworkable() = 0;
	virtual IClientRenderable* GetClientRenderable() = 0;
	virtual IClientEntity* GetIClientEntity() = 0;
	virtual CBaseEntity* GetBaseEntity() = 0;
	virtual IClientThinkable* GetClientThinkable() = 0;
};

// ================ SDK/Definitions/Main/IClientNetworkable.h ================
class IClientEntity;

enum ShouldTransmitState_t
{
	SHOULDTRANSMIT_START = 0,
	SHOULDTRANSMIT_END
};

enum DataUpdateType_t
{
	DATA_UPDATE_CREATED = 0,
	DATA_UPDATE_DATATABLE_CHANGED
};

class IClientNetworkable
{
public:
	virtual IClientUnknown* GetIClientUnknown() = 0;
	virtual void Release() = 0;
	virtual ClientClass* GetClientClass() = 0;
	virtual void NotifyShouldTransmit(ShouldTransmitState_t state) = 0;
	virtual void OnPreDataChanged(DataUpdateType_t updateType) = 0;
	virtual void OnDataChanged(DataUpdateType_t updateType) = 0;
	virtual void PreDataUpdate(DataUpdateType_t updateType) = 0;
	virtual void PostDataUpdate(DataUpdateType_t updateType) = 0;
	virtual bool IsDormant(void) = 0;
	virtual int entindex(void) const = 0;
	virtual void ReceiveMessage(int classID, bf_read& msg) = 0;
	virtual void* GetDataTableBasePtr() = 0;
	virtual void SetDestroyedOnRecreateEntities(void) = 0;
	virtual void OnDataUnchangedInPVS() = 0;

	inline ETFClassID GetClassID()
	{
		if (auto pClientClass = GetClientClass())
			return static_cast<ETFClassID>(pClientClass->m_ClassID);

		return static_cast<ETFClassID>(0);
	}
};

// ================ SDK/Definitions/Main/IClientRenderable.h ================
typedef unsigned short ClientShadowHandle_t;

enum
{
	CLIENTSHADOW_INVALID_HANDLE = (ClientShadowHandle_t)~0
};

enum ShadowType_t
{
	SHADOWS_NONE = 0,
	SHADOWS_SIMPLE,
	SHADOWS_RENDER_TO_TEXTURE,
	SHADOWS_RENDER_TO_TEXTURE_DYNAMIC,
	SHADOWS_RENDER_TO_DEPTH_TEXTURE
};

class IPVSNotify
{
public:
	virtual void OnPVSStatusChanged(bool bInPVS) = 0;
};

using ClientRenderHandle_t = void*;
using ModelInstanceHandle_t = unsigned short;
struct model_t;

class IClientRenderable
{
public:
	virtual IClientUnknown* GetIClientUnknown() = 0;
	virtual Vector const& GetRenderOrigin(void) = 0;
	virtual QAngle const& GetRenderAngles(void) = 0;
	virtual bool ShouldDraw(void) = 0;
	virtual bool IsTransparent(void) = 0;
	virtual bool UsesPowerOfTwoFrameBufferTexture() = 0;
	virtual bool UsesFullFrameBufferTexture() = 0;
	virtual ClientShadowHandle_t GetShadowHandle() const = 0;
	virtual ClientRenderHandle_t& RenderHandle() = 0;
	virtual const model_t* GetModel() const = 0;
	virtual int DrawModel(int flags) = 0;
	virtual int	GetBody() = 0;
	virtual void ComputeFxBlend() = 0;
	virtual int	GetFxBlend(void) = 0;
	virtual void GetColorModulation(float* color) = 0;
	virtual bool LODTest() = 0;
	virtual bool SetupBones(matrix3x4* pBoneToWorldOut, int nMaxBones, int boneMask, float currentTime) = 0;
	virtual void SetupWeights(const matrix3x4* pBoneToWorld, int nFlexWeightCount, float* pFlexWeights, float* pFlexDelayedWeights) = 0;
	virtual void DoAnimationEvents(void) = 0;
	virtual IPVSNotify* GetPVSNotifyInterface() = 0;
	virtual void GetRenderBounds(Vector& mins, Vector& maxs) = 0;
	virtual void GetRenderBoundsWorldspace(Vector& mins, Vector& maxs) = 0;
	virtual void GetShadowRenderBounds(Vector& mins, Vector& maxs, ShadowType_t shadowType) = 0;
	virtual bool ShouldReceiveProjectedTextures(int flags) = 0;
	virtual bool GetShadowCastDistance(float* pDist, ShadowType_t shadowType) const = 0;
	virtual bool GetShadowCastDirection(Vector* pDirection, ShadowType_t shadowType) const = 0;
	virtual bool IsShadowDirty() = 0;
	virtual void MarkShadowDirty(bool bDirty) = 0;
	virtual IClientRenderable* GetShadowParent() = 0;
	virtual IClientRenderable* FirstShadowChild() = 0;
	virtual IClientRenderable* NextShadowPeer() = 0;
	virtual ShadowType_t ShadowCastType() = 0;
	virtual void CreateModelInstance() = 0;
	virtual ModelInstanceHandle_t GetModelInstance() = 0;
	virtual const matrix3x4& RenderableToWorldTransform() = 0;
	virtual int LookupAttachment(const char* pAttachmentName) = 0;
	virtual	bool GetAttachment(int number, Vector& origin, QAngle& angles) = 0;
	virtual bool GetAttachment(int number, matrix3x4& matrix) = 0;
	virtual float* GetRenderClipPlane(void) = 0;
	virtual int GetSkin() = 0;
	virtual bool IsTwoPass(void) = 0;
	virtual void OnThreadedDrawSetup() = 0;
	virtual bool UsesFlexDelayedWeights() = 0;
	virtual void RecordToolMessage() = 0;
	virtual bool IgnoresZBuffer(void) const = 0;
};

// ================ SDK/Definitions/Main/IClientThinkable.h ================
class CClientThinkHandlePtr;
typedef CClientThinkHandlePtr* ClientThinkHandle_t;

class IClientThinkable
{
public:
	virtual IClientUnknown* GetIClientUnknown() = 0;
	virtual void ClientThink() = 0;
	virtual ClientThinkHandle_t	GetThinkHandle() = 0;
	virtual void SetThinkHandle(ClientThinkHandle_t hThink) = 0;
	virtual void Release() = 0;
};

// ================ SDK/Definitions/Main/IClientEntity.h ================
class CMouthInfo;
struct SpatializationInfo_t;

class IClientEntity : public IClientUnknown, public IClientRenderable, public IClientNetworkable, public IClientThinkable
{
public:
	virtual void Release(void) = 0;
	virtual const Vector& GetAbsOrigin(void) const = 0;
	virtual const QAngle& GetAbsAngles(void) const = 0;
	virtual CMouthInfo* GetMouth(void) = 0;
	virtual bool GetSoundSpatialization(SpatializationInfo_t& info) = 0;

	template <typename T> inline T* As() { return reinterpret_cast<T*>(this); }
};

// ================ SDK/Definitions/Misc/ISpatialPartition.h ================
struct Ray_t;
class IHandleEntity;

typedef unsigned short SpatialPartitionHandle_t;

enum
{
	PARTITION_INVALID_HANDLE = (SpatialPartitionHandle_t)~0
};

// ================ SDK/Definitions/Main/CGameTrace.h ================
class CBaseEntity;

class CGameTrace : public CBaseTrace
{
public:
	bool DidHit() const;

	float fractionleftsolid{};
	csurface_t surface{};
	int hitgroup{};
	short physicsbone{};
	CBaseEntity* m_pEnt{};
	int hitbox{};
};

inline bool CGameTrace::DidHit() const
{
	return fraction < 1.f || allsolid || startsolid;
}

typedef CGameTrace trace_t;

// ================ SDK/Definitions/Main/CCollisionProperty.h ================

MAKE_SIGNATURE(CCollisionProperty_CalcNearestPoint, "client.dll", "48 89 5C 24 ? 57 48 83 EC ? 49 8B D8 48 8B F9 4C 8D 44 24", 0x0);

struct model_t;

class ICollideable
{
public:
	virtual void* GetEntityHandle() = 0;
	virtual const Vec3& OBBMinsPreScaled() const = 0;
	virtual const Vec3& OBBMaxsPreScaled() const = 0;
	virtual const Vec3& OBBMins() const = 0;
	virtual const Vec3& OBBMaxs() const = 0;
	virtual void			WorldSpaceTriggerBounds(Vec3* pVecWorldMins, Vec3* pVecWorldMaxs) const = 0;
	virtual bool			TestCollision(const Ray_t& ray, unsigned int fContentsMask, CGameTrace& tr) = 0;
	virtual bool			TestHitboxes(const Ray_t& ray, unsigned int fContentsMask, CGameTrace& tr) = 0;
	virtual int				GetCollisionModelIndex() = 0;
	virtual const model_t* GetCollisionModel() = 0;
	virtual const Vec3& GetCollisionOrigin() const = 0;
	virtual const Vec3& GetCollisionAngles() const = 0;
	virtual const matrix3x4& CollisionToWorldTransform() const = 0;
	virtual SolidType_t		GetSolid() const = 0;
	virtual int				GetSolidFlags() const = 0;
	virtual void* GetIClientUnknown() = 0;
	virtual int				GetCollisionGroup() const = 0;
	virtual void			WorldSpaceSurroundingBounds(Vec3* pVecMins, Vec3* pVecMaxs) = 0;
	virtual bool			ShouldTouchTrigger(int triggerSolidFlags) const = 0;
	virtual const matrix3x4* GetRootParentToWorldTransform() const = 0;
};

class CCollisionProperty : public ICollideable
{
public:
	CBaseEntity* m_pOuter;
	Vec3 m_vecMinsPreScaled;
	Vec3 m_vecMaxsPreScaled;
	Vec3 m_vecMins;
	Vec3 m_vecMaxs;
	float m_flRadius;
	unsigned short m_usSolidFlags;
	SpatialPartitionHandle_t m_Partition;
	unsigned char m_nSurroundType;
	unsigned char m_nSolidType;
	unsigned char m_triggerBloat;
	bool m_bUniformTriggerBloat;
	Vec3 m_vecSpecifiedSurroundingMinsPreScaled;
	Vec3 m_vecSpecifiedSurroundingMaxsPreScaled;
	Vec3 m_vecSpecifiedSurroundingMins;
	Vec3 m_vecSpecifiedSurroundingMaxs;
	Vec3 m_vecSurroundingMins;
	Vec3 m_vecSurroundingMaxs;

public:
	SIGNATURE_ARGS(CalcNearestPoint, void, CCollisionProperty, (const Vec3& vecWorldPt, Vec3* pVecNearestWorldPt), this, std::ref(vecWorldPt), pVecNearestWorldPt);
};

// ================ SDK/Definitions/Main/CParticleProperty.h ================
class CBaseEntity;

// ================ SDK/Definitions/Misc/CInterpolatedVar.h ================
template< typename Type, bool IS_ARRAY >
struct CInterpolatedVarEntryBase
{
	float changetime;
	int count;
	Type* value;
};

template<typename T>
class CSimpleRingBuffer
{
public:
	T* m_pElements;
	unsigned short m_maxElement;
	unsigned short m_firstElement;
	unsigned short m_count;
	unsigned short m_growSize;
};

class IInterpolatedVar;   // body dropped: only ever named through a pointer here

template<typename Type, bool IS_ARRAY>
class CInterpolatedVarArrayBase
{
protected:
	typedef CInterpolatedVarEntryBase<Type, IS_ARRAY> CInterpolatedVarEntry;
	typedef CSimpleRingBuffer<CInterpolatedVarEntry> CVarHistory;

public:
	IInterpolatedVar* m_vTable;
	Type* m_pValue;
	CVarHistory m_VarHistory;
	Type* m_LastNetworkedValue;
	float m_LastNetworkedTime;
	byte m_fType;
	byte m_nMaxCount;
	byte* m_bLooping;
	float m_InterpolationAmount;
	const char* m_pDebugName;
	bool m_bDebug : 1;
};

template<typename Type>
class CInterpolatedVar : public CInterpolatedVarArrayBase< Type, false >
{

};

// ================ SDK/Definitions/Main/CBaseEntity.h ================

MAKE_SIGNATURE(CBaseEntity_SetAbsOrigin, "client.dll", "48 89 5C 24 ? 57 48 83 EC ? 48 8B FA 48 8B D9 E8 ? ? ? ? F3 0F 10 83", 0x0);
MAKE_SIGNATURE(CBaseEntity_SetAbsVelocity, "client.dll", "48 89 5C 24 ? 57 48 83 EC ? F3 0F 10 81 ? ? ? ? 48 8B DA 0F 2E 02", 0x0);
MAKE_SIGNATURE(CBaseEntity_EstimateAbsVelocity, "client.dll", "48 89 5C 24 ? 57 48 83 EC ? 48 8B FA 48 8B D9 E8 ? ? ? ? 48 3B D8", 0x0);
// ref CBaseEntity.h sig, unique in x64 client.dll (file off 0x1d01f0): m_iMostRecentModelBoneCounter
// = g_iModelBoneCounter - 1, m_flLastBoneSetupTime = -FLT_MAX. backtrack.cpp record capture.
MAKE_SIGNATURE(CBaseEntity_InvalidateBoneCache, "client.dll", "8B 05 ? ? ? ? FF C8 C7 81", 0x0);

typedef CHandle<CBaseEntity> EHANDLE;

#define MULTIPLAYER_BACKUP 90

class IInterpolatedVar;

class CBaseEntity : public IClientEntity
{
public:
	NETVAR(m_flSimulationTime, float, "CBaseEntity", "m_flSimulationTime");
	NETVAR(m_vecOrigin, Vec3, "CBaseEntity", "m_vecOrigin");
	NETVAR(m_iTeamNum, int, "CBaseEntity", "m_iTeamNum");
	NETVAR(m_flGravity, float, "CBaseEntity", "m_flGravity");
	NETVAR(m_hOwnerEntity, EHANDLE, "CBaseEntity", "m_hOwnerEntity");
	NETVAR_EMBED(m_Collision, CCollisionProperty*, "CBaseEntity", "m_Collision");
	NETVAR(m_vecMinsPreScaled, Vec3, "CBaseEntity", "m_vecMinsPreScaled");
	NETVAR(m_vecMaxsPreScaled, Vec3, "CBaseEntity", "m_vecMaxsPreScaled");
	NETVAR(m_vecMins, Vec3, "CBaseEntity", "m_vecMins");
	NETVAR(m_vecMaxs, Vec3, "CBaseEntity", "m_vecMaxs");
	NETVAR(m_nSolidType, int, "CBaseEntity", "m_nSolidType");
	NETVAR(m_usSolidFlags, int, "CBaseEntity", "m_usSolidFlags");
	NETVAR(m_nSurroundType, int, "CBaseEntity", "m_nSurroundType");
	NETVAR(m_triggerBloat, int, "CBaseEntity", "m_triggerBloat");
	NETVAR(m_bUniformTriggerBloat, bool, "CBaseEntity", "m_bUniformTriggerBloat");
	NETVAR(m_vecSpecifiedSurroundingMinsPreScaled, Vec3, "CBaseEntity", "m_vecSpecifiedSurroundingMinsPreScaled");
	NETVAR(m_vecSpecifiedSurroundingMaxsPreScaled, Vec3, "CBaseEntity", "m_vecSpecifiedSurroundingMaxsPreScaled");
	NETVAR(m_vecSpecifiedSurroundingMins, Vec3, "CBaseEntity", "m_vecSpecifiedSurroundingMins");
	NETVAR(m_vecSpecifiedSurroundingMaxs, Vec3, "CBaseEntity", "m_vecSpecifiedSurroundingMaxs");
	
	NETVAR_OFF(m_flOldSimulationTime, float, "CBaseEntity", "m_flSimulationTime", 4);
	NETVAR_OFF(m_MoveType, byte, "CTFPlayer", "m_nWaterLevel", -4);

	// CRITICAL CSS fix (rev2, coordinator-flagged): GetClassID() (ETFClassID) reads
	// ClientClass::m_ClassID, a per-binary RecvTable-registration-order index -- not a stable
	// cross-game constant. The classname compare below was ALSO wrong: ClientClass::m_pNetworkName
	// is the SERVER class name, never the DT_ table name (public/client_class.h:106
	// `ClientClass __g_##clientClassName##ClientClass(#serverClassName, ...)`; CSS source
	// c_cs_player.cpp:675 `IMPLEMENT_CLIENTCLASS_DT(C_CSPlayer, DT_CSPlayer, CCSPlayer)` gives
	// "CCSPlayer", not "DT_CSPlayer" -- every compare against a "DT_*" string here was always
	// false). Fixed to the real server class name.
	inline bool IsPlayer()
	{
		auto pClass = GetClientClass();
		return pClass && pClass->GetName() && !strcmp(pClass->GetName(), "CCSPlayer");
	}
	// TF2-only concepts (buildings, NPCs): confirmed no CSS counterpart (css-community-master
	// has no CObject*/boss entities) -- kept as false stubs, callers are all TF2 building-
	// target filters in aim_global.cpp that simply never match on CSS now.
	inline bool IsBuilding() { return false; }
	inline bool IsSentrygun() { return false; }
	inline bool IsDispenser() { return false; }
	inline bool IsTeleporter() { return false; }
	inline bool IsNPC() { return false; }

	// CSS 15, same as TF2. 13 = GetDataDescMap (live C_CSPlayer vt[13] returns the "C_BaseEntity"
	// datadesc, vt[15] the "C_CSPlayer" pred map; c_baseentity.h:184-186 DATADESC/CLIENTCLASS/PREDICTABLE).
	// 13 made engine prediction restore the full datadesc unpacked -> m_ModelName smashed, join crash 11-03.
	VIRTUAL(GetPredDescMap, datamap_t*, 15, this);
	//VIRTUAL(IsPlayer, bool, 132, this);
	//VIRTUAL(IsNPC, bool, 135, this);
	VIRTUAL(EyeAngles, Vec3&, 143, this); // CSS: SAME slot (vtable_check.md)

	SIGNATURE_ARGS(SetAbsOrigin, void, CBaseEntity, (const Vec3& vOrigin), this, std::ref(vOrigin));
	SIGNATURE(InvalidateBoneCache, void, CBaseEntity, this);
	SIGNATURE_ARGS(EstimateAbsVelocity, void, CBaseEntity, (Vec3& vVelocity), this, std::ref(vVelocity));
	inline Vec3 GetAbsVelocity()
	{
		Vec3 vOut;
		EstimateAbsVelocity(vOut);
		return vOut;
	}

	int GetIntermediateDataSize();
	Vec3 GetSize();
	Vec3 GetOffset();
	Vec3 GetCenter();
	int IsInValidTeam();
	int SolidMask();
	int GetHitboxToBase(int nHitbox);
};

// ================ SDK/Definitions/Main/CBaseAnimating.h ================
MAKE_SIGNATURE(CBaseAnimating_FrameAdvance, "client.dll", "48 89 5C 24 ? 48 89 6C 24 ? 57 48 81 EC ? ? ? ? 44 0F 29 54 24", 0x0);
MAKE_SIGNATURE(CBaseAnimating_SequenceDuration, "client.dll", "48 89 5C 24 ? 57 48 83 EC ? 80 B9 ? ? ? ? ? 48 8B D9 8B B9", 0x0);

class CBaseAnimating : public CBaseEntity
{
public:
	NETVAR(m_nSequence, int, "CBaseAnimating", "m_nSequence");
	NETVAR(m_nHitboxSet, int, "CBaseAnimating", "m_nHitboxSet");
	NETVAR(m_flModelScale, float, "CBaseAnimating", "m_flModelScale");
	NETVAR(m_flPlaybackRate, float, "CBaseAnimating", "m_flPlaybackRate");
	NETVAR(m_flCycle, float, "CBaseAnimating", "m_flCycle");
	inline std::array<float, 24>& m_flPoseParameter()
	{
		static int nOffset = U::NetVars.GetNetVar("CBaseAnimating", "m_flPoseParameter");
		return *reinterpret_cast<std::array<float, 24>*>(uintptr_t(this) + nOffset);
	}

	NETVAR_OFF(GetModelPtr, CStudioHdr*, "CBaseAnimating", "m_nMuzzleFlashParity", 16);
	NETVAR_OFF(m_bSequenceLoops, bool, "CBaseAnimating", "m_flFadeScale", 13);

	VIRTUAL_ARGS(GetAttachment, bool, 71, (int number, Vec3& origin), this, number, std::ref(origin)) // CSS: SAME
	VIRTUAL_ARGS(FireEvent, void, 174, (const Vector& origin, const QAngle& angles, int event, const char* options), this, std::ref(origin), std::ref(angles), event, options); // CSS: MOVED from 175 (vtable_check.md, sim=1.00)

	SIGNATURE_ARGS(FrameAdvance, float, CBaseAnimating, (float flInterval), this, flInterval);
	SIGNATURE(SequenceDuration, float, CBaseAnimating, this);
	inline float SequenceDuration(int iSequence)
	{
		int iOriginalSequence = m_nSequence();
		m_nSequence() = iSequence;
		bool bReturn = S::CBaseAnimating_SequenceDuration.Call<float>(this);
		m_nSequence() = iOriginalSequence;
		return bReturn;
	}

	int GetNumOfHitboxes();
	Vec3 GetHitboxOrigin(matrix3x4* aBones, int nHitbox, Vec3 vOffset = {});
	Vec3 GetHitboxCenter(matrix3x4* aBones, int nHitbox, Vec3 vOffset = {});
	void GetHitboxInfo(matrix3x4* aBones, int nHitbox, Vec3* pCenter = nullptr, Vec3* pMins = nullptr, Vec3* pMaxs = nullptr, matrix3x4* pMatrix = nullptr, Vec3 vOffset = {});
};

class CBaseAnimatingOverlay : public CBaseAnimating
{
public:

};

class CCurrencyPack : public CBaseAnimating
{
public:
};

class CHalloweenPickup : public CBaseAnimating
{
public:
};

class CHalloweenGiftPickup : public CHalloweenPickup
{
public:
};

// ================ SDK/Definitions/Main/CBaseFlex.h ================
class IHasLocalToGlobalFlexSettings
{
public:
	virtual void EnsureTranslations(const /*flexsettinghdr_t*/ void* pSettinghdr) = 0;
};

class CBaseFlex : public CBaseAnimatingOverlay, public IHasLocalToGlobalFlexSettings
{
public:
};

// ================ SDK/Definitions/Main/CBaseCombatCharacter.h ================
class CTFWeaponBase;

class CBaseCombatCharacter : public CBaseFlex
{
public:
	NETVAR(m_flNextAttack, float, "CBaseCombatCharacter", "m_flNextAttack");
	NETVAR(m_hActiveWeapon, EHANDLE, "CBaseCombatCharacter", "m_hActiveWeapon");
	//NETVAR(m_hMyWeapons, EHANDLE, "CBaseCombatCharacter", "m_hMyWeapons");

	CHandle<CTFWeaponBase>(&m_hMyWeapons())[MAX_WEAPONS];
	CTFWeaponBase* GetWeaponFromSlot(int nSlot);
};

// ================ SDK/Definitions/Main/CEconEntity.h ================
class CEconEntity : public CBaseAnimating
{
public:
	NETVAR(m_iItemDefinitionIndex, int, "CEconEntity", "m_iItemDefinitionIndex");

};

// ================ SDK/Definitions/Main/CBaseCombatWeapon.h ================
class CBaseCombatWeapon : public CEconEntity
{
public:
	NETVAR(m_iClip1, int, "CBaseCombatWeapon", "m_iClip1");
	NETVAR(m_iPrimaryAmmoType, int, "CBaseCombatWeapon", "m_iPrimaryAmmoType");
	NETVAR(m_flNextPrimaryAttack, float, "CBaseCombatWeapon", "m_flNextPrimaryAttack");
	NETVAR(m_flNextSecondaryAttack, float, "CBaseCombatWeapon", "m_flNextSecondaryAttack");
	NETVAR(m_hOwner, EHANDLE, "CBaseCombatWeapon", "m_hOwner");

	NETVAR_OFF(m_bInReload, bool, "CBaseCombatWeapon", "m_flTimeWeaponIdle", 4);
	NETVAR_OFF(m_bRemoveable, bool, "CBaseCombatWeapon", "m_iState", -12);
	NETVAR_OFF(m_bReloadsSingly, bool, "CBaseCombatWeapon", "m_iClip2", 24);

	// CSS slots: all MOVED/resolved via vtable_check.md disasm. CanBeSelected/CheckReload
	// confirmed MOVED by content match (bool flag test / byte-flag check, matches TF2
	// semantics). GetMaxClip1/GetName were NOT FOUND/AMBIGUOUS by byte fingerprint (both
	// trivial one-liners that tie with dozens of slots) -- resolved instead by the same
	// -17 declaration-order shift confirmed by CanBeSelected/CheckReload, then verified by
	// direct disasm: slot 305 reads an int field off a WeaponInfo-lookup struct (matches
	// GetMaxClip1 int return), slot 317 returns struct_base+0x5a as a pointer (matches
	// GetName const char* return, an inline string field). GetSlot (own class below, not
	// here) uses the same cluster/shift.
	VIRTUAL(CanBeSelected, bool, 216, this);
	VIRTUAL(CheckReload, void, 261, this);
	VIRTUAL(GetMaxClip1, int, 305, this);
	VIRTUAL(GetName, const char*, 317, this);
};

// ================ SDK/Definitions/Main/CBaseObject.h ================
class CBaseObject : public CBaseCombatCharacter
{
public:
	NETVAR(m_iHealth, int, "CBaseObject", "m_iHealth");
	NETVAR(m_iMaxHealth, int, "CBaseObject", "m_iMaxHealth");
	NETVAR(m_bHasSapper, bool, "CBaseObject", "m_bHasSapper");
	NETVAR(m_bBuilding, bool, "CBaseObject", "m_bBuilding");
	NETVAR(m_bMiniBuilding, bool, "CBaseObject", "m_bMiniBuilding");
	NETVAR(m_iUpgradeLevel, int, "CBaseObject", "m_iUpgradeLevel");
	// esp (c_baseobject.cpp:55-75 recv table)
	NETVAR(m_bPlacing, bool, "CBaseObject", "m_bPlacing");
	NETVAR(m_bCarried, bool, "CBaseObject", "m_bCarried");
	NETVAR(m_flPercentageConstructed, float, "CBaseObject", "m_flPercentageConstructed");
	NETVAR(m_bDisabled, bool, "CBaseObject", "m_bDisabled");
	NETVAR(m_hBuilder, EHANDLE, "CBaseObject", "m_hBuilder");
	NETVAR(m_iUpgradeMetal, int, "CBaseObject", "m_iUpgradeMetal");
	NETVAR(m_iUpgradeMetalRequired, int, "CBaseObject", "m_iUpgradeMetalRequired");
	NETVAR(m_bPlasmaDisable, bool, "CBaseObject", "m_bPlasmaDisable");

	// GetNumBuildPoints/GetBuildPointAttachmentIndex removed: CBaseObject (TF2 buildings) has
	// no CSS counterpart (confirmed, vtable_check.md "TF2-ONLY class"), and no caller remained
	// (grepped) -- CSS has no building entities at all, not even a stub target.
};

class CObjectSentrygun : public CBaseObject
{
public:
	NETVAR(m_iAmmoShells, int, "CObjectSentrygun", "m_iAmmoShells");
	NETVAR(m_iAmmoRockets, int, "CObjectSentrygun", "m_iAmmoRockets");

	int MaxAmmoShells();
	void GetAmmoCount(int& iShells, int& iMaxShells, int& iRockets, int& iMaxRockets);
};

class CObjectDispenser : public CBaseObject
{
public:
};

class CObjectTeleporter : public CBaseObject
{
public:
};

// ================ SDK/Definitions/Main/CEconWearable.h ================
class CEconWearable : public CEconEntity
{
public:
};

// ================ SDK/Definitions/Main/CBasePlayer.h ================
MAKE_SIGNATURE(CBasePlayer_GetAmmoCount, "client.dll", "48 89 5C 24 ? 57 48 83 EC ? 48 63 DA 48 8B F9 83 FB", 0x0);

class CBasePlayer : public CBaseCombatCharacter
{
public:
	NETVAR(m_bDucked, bool, "CBasePlayer", "m_bDucked");
	NETVAR(m_bDucking, bool, "CBasePlayer", "m_bDucking");
	NETVAR(m_bInDuckJump, bool, "CBasePlayer", "m_bInDuckJump");
	NETVAR(m_flDucktime, float, "CBasePlayer", "m_flDucktime");
	NETVAR(m_flDuckJumpTime, float, "CBasePlayer", "m_flDuckJumpTime");
	NETVAR(m_vecPunchAngle, Vec3, "CBasePlayer", "m_vecPunchAngle");
	NETVAR(m_vecViewOffset, Vec3, "CBasePlayer", "m_vecViewOffset[0]");
	NETVAR(m_nTickBase, int, "CBasePlayer", "m_nTickBase");
	NETVAR(m_hGroundEntity, EHANDLE, "CBasePlayer", "m_hGroundEntity");
	NETVAR(m_vecVelocity, Vec3, "CBasePlayer", "m_vecVelocity[0]");
	NETVAR(m_vecBaseVelocity, Vec3, "CBasePlayer", "m_vecBaseVelocity");
	NETVAR(m_hConstraintEntity, EHANDLE, "CBasePlayer", "m_hConstraintEntity");
	NETVAR(m_vecConstraintCenter, Vec3, "CBasePlayer", "m_vecConstraintCenter");
	NETVAR(m_flConstraintRadius, float, "CBasePlayer", "m_flConstraintRadius");
	NETVAR(m_flConstraintWidth, float, "CBasePlayer", "m_flConstraintWidth");
	NETVAR(m_flConstraintSpeedFactor, float, "CBasePlayer", "m_flConstraintSpeedFactor");
	NETVAR(m_nWaterLevel, byte, "CBasePlayer", "m_nWaterLevel");
	NETVAR(m_iFOV, int, "CBasePlayer", "m_iFOV");
	NETVAR(m_iHealth, int, "CBasePlayer", "m_iHealth");
	NETVAR(m_lifeState, byte, "CBasePlayer", "m_lifeState");
	NETVAR(m_flMaxspeed, float, "CBasePlayer", "m_flMaxspeed");
	NETVAR(m_fFlags, int, "CBasePlayer", "m_fFlags");
	NETVAR(m_hViewModel, EHANDLE, "CBasePlayer", "m_hViewModel[0]");

	NETVAR_OFF(m_nButtons, int, "CBasePlayer", "m_hConstraintEntity", -12);
	NETVAR_OFF(m_pCurrentCommand, CUserCmd*, "CBasePlayer", "m_hConstraintEntity", -8);

	CONDGET(IsOnGround, m_fFlags(), FL_ONGROUND);
	CONDGET(IsDucking, m_fFlags(), FL_DUCKING);

	VIRTUAL(Think, void, 122, this);

	SIGNATURE_ARGS(GetAmmoCount, int, CBasePlayer, (int iAmmoType), this, iAmmoType);

	bool IsAlive();
	Vec3 GetShootPos();
	Vec3 GetEyePosition();
	bool IsSwimming();
	bool IsUnderwater();
};

// ================ SDK/Definitions/Main/CBaseProjectile.h ================
enum
{
	TF_GL_MODE_REGULAR = 0,
	TF_GL_MODE_REMOTE_DETONATE,
	TF_GL_MODE_REMOTE_DETONATE_PRACTICE,
	TF_GL_MODE_CANNONBALL
};

class CBaseProjectile : public CBaseAnimating
{
public:
	NETVAR(m_hOriginalLauncher, EHANDLE, "CBaseProjectile", "m_hOriginalLauncher");
};

class CBaseGrenade : public CBaseProjectile
{
public:
	NETVAR(m_flDamage, float, "CBaseGrenade", "m_flDamage");
	NETVAR(m_hThrower, EHANDLE, "CBaseGrenade", "m_hThrower");
	NETVAR(m_vecVelocity, Vec3, "CBaseGrenade", "m_vecVelocity");
	NETVAR(m_fFlags, int, "CBaseGrenade", "m_fFlags");
};

class CTFBaseRocket : public CBaseProjectile
{
public:
	NETVAR(m_vInitialVelocity, Vec3, "CTFBaseRocket", "m_vInitialVelocity");
	NETVAR(m_vecOrigin, Vec3, "CTFBaseRocket", "m_vecOrigin");
	NETVAR(m_iDeflected, int, "CTFBaseRocket", "m_iDeflected");
	NETVAR(m_hLauncher, EHANDLE, "CTFBaseRocket", "m_hLauncher");
};

class CTFBaseProjectile : public CBaseProjectile
{
public:
	NETVAR(m_hLauncher, EHANDLE, "CTFBaseProjectile", "m_hLauncher");
};

class CTFWeaponBaseGrenadeProj : public CBaseGrenade
{
public:
	NETVAR(m_vInitialVelocity, Vec3, "CTFWeaponBaseGrenadeProj", "m_vInitialVelocity");
	NETVAR(m_iDeflected, int, "CTFWeaponBaseGrenadeProj", "m_iDeflected");
	NETVAR(m_vecOrigin, Vec3, "CTFWeaponBaseGrenadeProj", "m_vecOrigin");
};

class CTFProjectile_Rocket : public CTFBaseRocket
{
public:
};

class CTFProjectile_EnergyBall : public CTFBaseRocket
{
public:
};

class CTFProjectile_Flare : public CTFBaseRocket
{
public:
};

class CTFProjectile_Arrow : public CTFBaseRocket
{
public:
	NETVAR(m_iProjectileType, int, "CTFProjectile_Arrow", "m_iProjectileType");

	bool CanHeadshot();
};

class CTFGrenadePipebombProjectile : public CTFWeaponBaseGrenadeProj
{
public:
	NETVAR(m_bTouched, bool, "CTFGrenadePipebombProjectile", "m_bTouched");
	NETVAR(m_iType, int, "CTFGrenadePipebombProjectile", "m_iType");
	NETVAR(m_hLauncher, EHANDLE, "CTFGrenadePipebombProjectile", "m_hLauncher");

	NETVAR_OFF(m_flCreationTime, float, "CTFGrenadePipebombProjectile", "m_iType", 4);

	bool HasStickyEffects();
};

// ================ SDK/Definitions/Main/CCaptureFlag.h ================
class CCaptureFlag : public CBaseEntity
{
public:
	NETVAR(m_nType, int, "CCaptureFlag", "m_nType");
};

// ================ SDK/Definitions/Main/KeyValues.h ================
#include <stdint.h>

enum types_t
{
	TYPE_NONE = 0,
	TYPE_STRING,
	TYPE_INT,
	TYPE_FLOAT,
	TYPE_PTR,
	TYPE_WSTRING,
	TYPE_COLOR,
	TYPE_UINT64,
	TYPE_NUMTYPES,
};

class KeyValues
{
private:
	int m_iKeyName;
	char* m_sValue;
	wchar_t* m_wsValue;

	union
	{
		int m_iValue;
		float m_flValue;
		void* m_pValue;
		unsigned char m_Color[4];
	};

	char m_iDataType;
	char m_bHasEscapeSequences;
	char m_bEvaluateConditionals;
	char unused[1];

	KeyValues* m_pPeer;
	KeyValues* m_pSub;
	KeyValues* m_pChain;
	void* m_pUnknown40;   // ctor zeroes +0x40; engine allocs 0x48 since 2026-09-30 build

public:
	class AutoDelete
	{
	public:
		explicit inline AutoDelete(KeyValues* pKeyValues) : m_pKeyValues(pKeyValues) {}
		explicit inline AutoDelete(const char* pchKVName) : m_pKeyValues(new KeyValues(pchKVName)) {}
		inline ~AutoDelete(void) { if (m_pKeyValues) m_pKeyValues->DeleteThis(); }
		inline void Assign(KeyValues* pKeyValues) { m_pKeyValues = pKeyValues; }
		KeyValues* operator->() { return m_pKeyValues; }
		operator KeyValues* () { return m_pKeyValues; }
	private:
		AutoDelete(AutoDelete const& x); // forbid
		AutoDelete& operator= (AutoDelete const& x); // forbid
		KeyValues* m_pKeyValues;
	};

	bool LoadFromBuffer(char const* resource_name, const char* buffer, void* file_system = 0, const char* path_id = 0);
	void Initialize(const char* name);
	KeyValues(const char* name);

	void* operator new(size_t iAllocSize);
	void* operator new(size_t iAllocSize, int nBlockUse, const char* pFileName, int nLine);
	void operator delete(void* pMem);
	void operator delete(void* pMem, int nBlockUse, const char* pFileName, int nLine);

	KeyValues* FindKey(const char* keyName, bool bCreate = false);

	const char* GetName() const;

	int GetInt(const char* keyName, int defaultValue = 0);
	uint64_t GetUint64(const char* keyName, uint64_t defaultValue = 0);
	float GetFloat(const char* keyName, float defaultValue = 0.0f);
	const char* GetString(const char* keyName, const char* defaultValue = "");
	bool GetBool(const char* keyName, bool defaultValue = false /*, bool* optGotDefault = NULL*/);
	bool IsEmpty(const char* keyName);

	void SetString(const char* keyName, const char* value);
	void SetInt(const char* keyName, int value);
	void SetFloat(const char* keyName, float value);
	void SetColor(const char* keyName, Color_t value);
	void SetBool(const char* keyName, bool value);

	void DeleteThis();
};

typedef KeyValues::AutoDelete KeyValuesAD;

// ================ SDK/Definitions/Main/CEconDefinition.h ================

// ================ SDK/Definitions/Main/CEntitySphereQuery.h ================
#define MAX_SPHERE_QUERY	512

#pragma warning (disable : 26495)

MAKE_SIGNATURE(CEntitySphereQuery, "client.dll", "40 53 48 83 EC ? 48 8B D9 C7 44 24 ? ? ? ? ? 33 C9", 0x0);

//credits to KGB
class CEntitySphereQuery
{
public:
	CEntitySphereQuery(const Vec3& center, const float radius, const int flagMask = 0, const int partitionMask = PARTITION_CLIENT_NON_STATIC_EDICTS)
	{
		S::CEntitySphereQuery.Call<void>(this, std::ref(center), radius, flagMask, partitionMask);
	}

	inline CBaseEntity* GetCurrentEntity()
	{
		return (m_nListIndex < m_nListCount) ? m_pList[m_nListIndex] : nullptr;
	}

	inline void NextEntity()
	{
		m_nListIndex++;
	}

private:
	int m_nListIndex, m_nListCount;
	CBaseEntity* m_pList[MAX_SPHERE_QUERY];
};

// ================ SDK/Definitions/Misc/Activity.h ================
// The ACT_* table is gone: nothing in this tree names an activity. Only the type itself
// is still needed, for the CMultiPlayerAnimState members and virtuals declared below.
typedef int Activity;

// ================ SDK/Definitions/Misc/CRangeCheckedVar.h ================
template< class T, int minValue, int maxValue, int startValue >
class CRangeCheckedVar
{
public:

	inline CRangeCheckedVar()
	{
		m_Val = startValue;
	}

	inline CRangeCheckedVar(const T& value)
	{
		*this = value;
	}

	inline void Clamp()
	{
		if (m_Val < minValue)
			m_Val = minValue;

		else if (m_Val > maxValue)
			m_Val = maxValue;
	}

	inline operator const T& () const
	{
		return m_Val;
	}

	inline CRangeCheckedVar<T, minValue, maxValue, startValue>& operator=(const T& value)
	{
		//RangeCheck(value, minValue, maxValue);
		m_Val = value;
		return *this;
	}

	inline CRangeCheckedVar<T, minValue, maxValue, startValue>& operator+=(const T& value)
	{
		return (*this = m_Val + value);
	}

	inline CRangeCheckedVar<T, minValue, maxValue, startValue>& operator-=(const T& value)
	{
		return (*this = m_Val - value);
	}

	inline CRangeCheckedVar<T, minValue, maxValue, startValue>& operator*=(const T& value)
	{
		return (*this = m_Val * value);
	}

	inline CRangeCheckedVar<T, minValue, maxValue, startValue>& operator/=(const T& value)
	{
		return (*this = m_Val / value);
	}

private:
	T m_Val;
};

// ================ SDK/Definitions/Main/CMultiPlayerAnimState.h ================
class CAnimationLayer
{
public:
	CRangeCheckedVar<int, -1, 65535, 0>	m_nSequence;
	CRangeCheckedVar<float, -2, 2, 0> m_flPrevCycle;
	CRangeCheckedVar<float, -5, 5, 0> m_flWeight;
	int m_nOrder;
	CRangeCheckedVar<float, -50, 50, 1> m_flPlaybackRate;
	CRangeCheckedVar<float, -2, 2, 0> m_flCycle;
	float m_flLayerAnimtime;
	float m_flLayerFadeOuttime;
	float m_flBlendIn;
	float m_flBlendOut;
	bool m_bClientBlend;
};

typedef enum
{
	LEGANIM_9WAY,
	LEGANIM_8WAY,
	LEGANIM_GOLDSRC
} LegAnimType_t;

enum PlayerAnimEvent_t
{
	PLAYERANIMEVENT_ATTACK_PRIMARY,
	PLAYERANIMEVENT_ATTACK_SECONDARY,
	PLAYERANIMEVENT_ATTACK_GRENADE,
	PLAYERANIMEVENT_RELOAD,
	PLAYERANIMEVENT_RELOAD_LOOP,
	PLAYERANIMEVENT_RELOAD_END,
	PLAYERANIMEVENT_JUMP,
	PLAYERANIMEVENT_SWIM,
	PLAYERANIMEVENT_DIE,
	PLAYERANIMEVENT_FLINCH_CHEST,
	PLAYERANIMEVENT_FLINCH_HEAD,
	PLAYERANIMEVENT_FLINCH_LEFTARM,
	PLAYERANIMEVENT_FLINCH_RIGHTARM,
	PLAYERANIMEVENT_FLINCH_LEFTLEG,
	PLAYERANIMEVENT_FLINCH_RIGHTLEG,
	PLAYERANIMEVENT_DOUBLEJUMP,
	PLAYERANIMEVENT_CANCEL,
	PLAYERANIMEVENT_SPAWN,
	PLAYERANIMEVENT_SNAP_YAW,
	PLAYERANIMEVENT_CUSTOM,
	PLAYERANIMEVENT_CUSTOM_GESTURE,
	PLAYERANIMEVENT_CUSTOM_SEQUENCE,
	PLAYERANIMEVENT_CUSTOM_GESTURE_SEQUENCE,
	PLAYERANIMEVENT_ATTACK_PRE,
	PLAYERANIMEVENT_ATTACK_POST,
	PLAYERANIMEVENT_GRENADE1_DRAW,
	PLAYERANIMEVENT_GRENADE2_DRAW,
	PLAYERANIMEVENT_GRENADE1_THROW,
	PLAYERANIMEVENT_GRENADE2_THROW,
	PLAYERANIMEVENT_VOICE_COMMAND_GESTURE,
	PLAYERANIMEVENT_DOUBLEJUMP_CROUCH,
	PLAYERANIMEVENT_STUN_BEGIN,
	PLAYERANIMEVENT_STUN_MIDDLE,
	PLAYERANIMEVENT_STUN_END,
	PLAYERANIMEVENT_PASSTIME_THROW_BEGIN,
	PLAYERANIMEVENT_PASSTIME_THROW_MIDDLE,
	PLAYERANIMEVENT_PASSTIME_THROW_END,
	PLAYERANIMEVENT_PASSTIME_THROW_CANCEL,
	PLAYERANIMEVENT_ATTACK_PRIMARY_SUPER,
	PLAYERANIMEVENT_COUNT
};

struct GestureSlot_t
{
	int m_iGestureSlot;
	Activity m_iActivity;
	bool m_bAutoKill;
	bool m_bActive;
	CAnimationLayer* m_pAnimLayer;
};

struct MultiPlayerPoseData_t
{
	int m_iMoveX;
	int m_iMoveY;
	int m_iAimYaw;
	int m_iAimPitch;
	int m_iBodyHeight;
	int m_iMoveYaw;
	int m_iMoveScale;
	float m_flEstimateYaw;
	float m_flLastAimTurnTime;

	void Init()
	{
		m_iMoveX = 0;
		m_iMoveY = 0;
		m_iAimYaw = 0;
		m_iAimPitch = 0;
		m_iBodyHeight = 0;
		m_iMoveYaw = 0;
		m_iMoveScale = 0;
		m_flEstimateYaw = 0.0f;
		m_flLastAimTurnTime = 0.0f;
	}
};

struct DebugPlayerAnimData_t
{
	float m_flSpeed;
	float m_flAimPitch;
	float m_flAimYaw;
	float m_flBodyHeight;
	Vector2D m_vecMoveYaw;
};

struct MultiPlayerMovementData_t
{
	float m_flWalkSpeed;
	float m_flRunSpeed;
	float m_flSprintSpeed;
	float m_flBodyYawRate;
};

enum
{
	GESTURE_SLOT_ATTACK_AND_RELOAD,
	GESTURE_SLOT_GRENADE,
	GESTURE_SLOT_JUMP,
	GESTURE_SLOT_SWIM,
	GESTURE_SLOT_FLINCH,
	GESTURE_SLOT_VCD,
	GESTURE_SLOT_CUSTOM,
	GESTURE_SLOT_COUNT
};

class CMultiPlayerAnimState
{
public:
	virtual ~CMultiPlayerAnimState() = 0;
	virtual void ClearAnimationState() = 0;
	virtual void DoAnimationEvent(PlayerAnimEvent_t event, int nData = 0) = 0;
	virtual Activity CalcMainActivity() = 0;
	virtual void Update(float eyeYaw, float eyePitch) = 0;
	virtual void Release() = 0;
	virtual Activity TranslateActivity(Activity actDesired) = 0;
	virtual void SetRunSpeed(float flSpeed) = 0;
	virtual void SetWalkSpeed(float flSpeed) = 0;
	virtual void SetSprintSpeed(float flSpeed) = 0;
	virtual void ShowDebugInfo() = 0;
	virtual void DebugShowAnimState(int iStartLine) = 0;
	virtual void Init(CBasePlayer* pPlayer, MultiPlayerMovementData_t& movementData) = 0;
	virtual int SelectWeightedSequence(Activity activity) = 0;
	virtual void RestartMainSequence() = 0;
	virtual void GetOuterAbsVelocity(Vector& vel) = 0;
	virtual bool HandleJumping(Activity& idealActivity) = 0;
	virtual bool HandleDucking(Activity& idealActivity) = 0;
	virtual bool HandleMoving(Activity& idealActivity) = 0;
	virtual bool HandleSwimming(Activity& idealActivity) = 0;
	virtual bool HandleDying(Activity& idealActivity) = 0;
	virtual void RestartGesture(int iGestureSlot, Activity iGestureActivity, bool bAutoKill = true) = 0;
	virtual float GetGesturePlaybackRate() = 0;
	virtual void PlayFlinchGesture(Activity iActivity) = 0;
	virtual float CalcMovementSpeed(bool* bIsMoving) = 0;
	virtual float CalcMovementPlaybackRate(bool* bIsMoving) = 0;
	virtual void ComputePoseParam_MoveYaw(CStudioHdr* pStudioHdr) = 0;
	virtual void ComputePoseParam_AimPitch(CStudioHdr* pStudioHdr) = 0;
	virtual void ComputePoseParam_AimYaw(CStudioHdr* pStudioHdr) = 0;
	virtual void EstimateYaw() = 0;
	virtual float GetCurrentMaxGroundSpeed() = 0;
	virtual void ComputeSequences(CStudioHdr* pStudioHdr) = 0;
	virtual bool ShouldUpdateAnimState() = 0;

public:
	bool m_bForceAimYaw;
	CUtlVector<GestureSlot_t> m_aGestureSlots;
	CBasePlayer* m_pEntity;
	QAngle m_angRender;
	bool m_bPoseParameterInit;
	MultiPlayerPoseData_t m_PoseParameterData;
	DebugPlayerAnimData_t m_DebugAnimData;
	bool m_bCurrentFeetYawInitialized;
	float m_flLastAnimationStateClearTime;
	float m_flEyeYaw;
	float m_flEyePitch;
	float m_flGoalFeetYaw;
	float m_flCurrentFeetYaw;
	float m_flLastAimTurnTime;
	MultiPlayerMovementData_t m_MovementData;
	bool m_bJumping;
	float m_flJumpStartTime;
	bool m_bFirstJumpFrame;
	bool m_bInSwim;
	bool m_bFirstSwimFrame;
	bool m_bDying;
	bool m_bFirstDyingFrame;
	Activity m_eCurrentMainSequenceActivity;
	int m_nSpecificMainSequence;
	CHandle<void> m_hActiveWeapon;
	float m_flLastGroundSpeedUpdateTime;
	CInterpolatedVar<float> m_iv_flMaxGroundSpeed;
	float m_flMaxGroundSpeed;
	int m_nMovementSequence;
	LegAnimType_t m_LegAnimType;
};

class CTFPlayerAnimState : public CMultiPlayerAnimState
{
public:

	virtual void		ClearAnimationState() = 0;
	virtual Activity	TranslateActivity(Activity actDesired) = 0;
	virtual void		Update(float eyeYaw, float eyePitch) = 0;
	virtual void		CheckStunAnimation() = 0;
	virtual Activity	CalcMainActivity() = 0;
	virtual void		ComputePoseParam_AimYaw(CStudioHdr* pStudioHdr) = 0;
	virtual float		GetCurrentMaxGroundSpeed() = 0;
	virtual float		GetGesturePlaybackRate() = 0;
	virtual bool		ShouldUpdateAnimState() = 0;
	virtual void		GetOuterAbsVelocity(Vec3& vel) = 0;
	virtual void		RestartGesture(int iGestureSlot, Activity iGestureActivity, bool bAutoKill = true) = 0;

	CBaseEntity* m_pTFPlayer;
	bool		 m_bInAirWalk;
	float		 m_flHoldDeployedPoseUntilTime;
	float		 m_flTauntMoveX;
	float		 m_flTauntMoveY;
	float		 m_flVehicleLeanVel;
	float		 m_flVehicleLeanPos;
	Vec3		 m_vecSmoothedUp;
};

// ================ SDK/Definitions/Main/CPredictionCopy.h ================

MAKE_SIGNATURE(CPredictionCopy_TransferData, "client.dll", "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56 48 83 EC ? 8B 3D", 0x0);

#define PC_DATA_PACKED			true
#define PC_DATA_NORMAL			false

enum
{
	PC_EVERYTHING = 0,
	PC_NON_NETWORKED_ONLY,
	PC_NETWORKED_ONLY,
};

typedef void (*FN_FIELD_COMPARE)(const char* classname, const char* fieldname, const char* fieldtype,
	bool networked, bool noterrorchecked, bool differs, bool withintolerance, const char* value);

class CPredictionCopy
{
public:
	typedef enum
	{
		DIFFERS = 0,
		IDENTICAL,
		WITHINTOLERANCE,
	} difftype_t;

	CPredictionCopy(int type, void* dest, bool dest_packed, void const* src, bool src_packed,
		bool counterrors = false, bool reporterrors = false, bool performcopy = true,
		bool describefields = false, FN_FIELD_COMPARE func = nullptr)
	{
		m_nType = type;
		m_pDest = dest;
		m_pSrc = src;
		m_nDestOffsetIndex = dest_packed ? TD_OFFSET_PACKED : TD_OFFSET_NORMAL;
		m_nSrcOffsetIndex = src_packed ? TD_OFFSET_PACKED : TD_OFFSET_NORMAL;
		m_bErrorCheck = counterrors;
		m_bReportErrors = reporterrors;
		m_bPerformCopy = performcopy;
		m_bDescribeFields = describefields;

		m_pCurrentField = nullptr;
		m_pCurrentMap = nullptr;
		m_pCurrentClassName = nullptr;
		m_bShouldReport = false;
		m_bShouldDescribe = false;
		m_nErrorCount = 0;

		m_FieldCompareFunc = func;
	}

	int TransferData(const char* operation, int entindex, datamap_t* dmap)
	{
		return S::CPredictionCopy_TransferData.Call<int>(this, operation, entindex, dmap);
	}

public:
	int m_nType;
	void* m_pDest;
	void const* m_pSrc;
	int m_nDestOffsetIndex;
	int m_nSrcOffsetIndex;

	bool m_bErrorCheck;
	bool m_bReportErrors;
	bool m_bDescribeFields;
	typedescription_t* m_pCurrentField;
	char const* m_pCurrentClassName;
	datamap_t* m_pCurrentMap;
	bool m_bShouldReport;
	bool m_bShouldDescribe;
	int m_nErrorCount;
	bool m_bPerformCopy;

	FN_FIELD_COMPARE	m_FieldCompareFunc;

	typedescription_t* m_pWatchField;
	char const* m_pOperation;
};

// ================ SDK/Definitions/Main/CSniperDot.h ================
class CSniperDot : public CBaseEntity
{
public:
};

// ================ SDK/Definitions/Main/CTFPlayer.h ================
// CSS_STUB tf2 api: TeamFortress_CalculateMaxSpeed / CTFPlayer_GetEffectiveInvisibilityLevel
// sigs removed (MISSING in CSS, sig_check.md) -- special-ability speed calc and cloak
// invisibility are TF2-only concepts. CalculateMaxSpeed()/GetEffectiveInvisibilityLevel()
// below stubbed (0.f invis is CSS-correct, not just a compile stopgap: CSS has no cloak).

class CTFPlayer : public CBasePlayer
{
public:
	NETVAR(m_nWaterLevel, byte, "CTFPlayer", "m_nWaterLevel");
	NETVAR(m_iClass, int, "CTFPlayer", "m_iClass");
	NETVAR(m_flDuckTimer, float, "CTFPlayer", "m_flDuckTimer");   // ref CTFPlayer.h:38, auto rocket jump duck gate
	NETVAR(m_nPlayerCond, int, "CTFPlayer", "m_nPlayerCond");
	NETVAR(m_bJumping, bool, "CTFPlayer", "m_bJumping");
	NETVAR(m_iCritMult, int, "CTFPlayer", "m_iCritMult");
	NETVAR(m_iStunFlags, int, "CTFPlayer", "m_iStunFlags");
	NETVAR(m_bFeignDeathReady, bool, "CTFPlayer", "m_bFeignDeathReady");
	NETVAR(m_flStealthNoAttackExpire, float, "CTFPlayer", "m_flStealthNoAttackExpire");
	NETVAR(_condition_bits, int, "CTFPlayer", "_condition_bits");
	NETVAR(m_nPlayerCondEx, int, "CTFPlayer", "m_nPlayerCondEx");
	NETVAR(m_nPlayerCondEx2, int, "CTFPlayer", "m_nPlayerCondEx2");
	NETVAR(m_nPlayerCondEx3, int, "CTFPlayer", "m_nPlayerCondEx3");
	NETVAR(m_nPlayerCondEx4, int, "CTFPlayer", "m_nPlayerCondEx4");
	NETVAR(m_vecOrigin, Vec3, "CTFPlayer", "m_vecOrigin");
	NETVAR(m_angEyeAnglesX, float, "CTFPlayer", "m_angEyeAngles[0]");
	NETVAR(m_angEyeAnglesY, float, "CTFPlayer", "m_angEyeAngles[1]");
	NETVAR(m_bAllowMoveDuringTaunt, bool, "CTFPlayer", "m_bAllowMoveDuringTaunt");
	NETVAR(m_flTauntYaw, float, "CTFPlayer", "m_flTauntYaw");
	NETVAR(m_flLastDamageTime, float, "CTFPlayer", "m_flLastDamageTime");
	NETVAR(m_iSpawnCounter, int, "CTFPlayer", "m_iSpawnCounter");
	NETVAR(m_bViewingCYOAPDA, bool, "CTFPlayer", "m_bViewingCYOAPDA");

	NETVAR_OFF(m_flInvisibility, float, "CTFPlayer", "m_flInvisChangeCompleteTime", -8);
	NETVAR_OFF(m_flTankPressure, float, "CTFPlayer", "m_Shared", 636);
	NETVAR_OFF(m_PlayerAnimState, CTFPlayerAnimState*, "CTFPlayer", "m_hItem", -88);

	// CSS_STUB tf2 api: no special-ability speed modifiers or cloak in CSS.
	inline float CalculateMaxSpeed(bool bIgnoreSpecialAbility = false) { return m_flMaxspeed(); }
	inline float GetEffectiveInvisibilityLevel() { return 0.f; }

	Vec3 GetEyeAngles();
	Vec3 GetViewOffset(); // use on nonlocal players
	bool InCond(ETFCond eCond);
	void RemoveCond(ETFCond eCond); // bits only
	bool IsAGhost();
	bool IsTaunting();
	bool IsInvisible(float flValue = 1.f);
	bool IsInvulnerable();
	bool IsCritBoosted();
	bool IsMiniCritBoosted();
	bool CanAttack(bool bCloak = true, bool bLocal = true);
	float GetCritMult();
};

class CTFRagdoll : public CBaseFlex
{
public:
	NETVAR(m_iClass, int, "CTFRagdoll", "m_iClass");

};

// ================ SDK/Definitions/Misc/IGameResources.h ================
class IGameResources
{
public:
	/*virtual	~IGameResources() {};
	virtual const char *GetTeamName(int index) = 0;
	virtual int GetTeamScore(int index) = 0;
	virtual const Color &GetTeamColor(int index) = 0;
	virtual bool IsConnected(int index) = 0;
	virtual bool IsAlive(int index) = 0;
	virtual bool IsFakePlayer(int index) = 0;
	virtual bool IsLocalPlayer(int index) = 0;
	virtual const char *GetPlayerName(int index) = 0;
	virtual int GetPlayerScore(int index) = 0;
	virtual int GetPing(int index) = 0;
	virtual int GetDeaths(int index) = 0;
	virtual int GetFrags(int index) = 0;
	virtual int GetTeam(int index) = 0;
	virtual int GetHealth(int index) = 0;*/
};

// ================ SDK/Definitions/Misc/String.h ================

// ================ SDK/Definitions/Main/CPlayerResource.h ================
#define PLAYER_ERROR_NAME "ERRORNAME"

class CPlayerResource : public CBaseEntity, public IGameResources
{
public:
	NETVAR_ARRAY(m_iPing, int, "CPlayerResource", "m_iPing");
	NETVAR_ARRAY(m_bAlive, bool, "CPlayerResource", "m_bAlive");
	NETVAR_ARRAY(m_iHealth, int, "CPlayerResource", "m_iHealth");
	NETVAR_ARRAY(m_iUserID, int, "CPlayerResource", "m_iUserID");

	NETVAR_ARRAY_OFF(m_szName, const char*, "CPlayerResource", "m_iPing", -816);

	inline bool IsFakePlayer(int iIndex, bool bPlayerInfo = false)
	{
		if (!bPlayerInfo)
			return !m_iPing(iIndex);

		player_info_t tInfo;
		if (I::EngineClient->GetPlayerInfo(iIndex, &tInfo))
			return tInfo.fakeplayer;
		return false;
	}

	inline const char* GetName(int iIndex)
	{
		const char* sName = m_szName(iIndex);
		return sName ? sName : PLAYER_ERROR_NAME;
	}
};

// ================ SDK/Definitions/Main/CTFPlayerResource.h ================

class CTFPlayerResource : public CPlayerResource
{
public:
	NETVAR_ARRAY(m_iMaxHealth, int, "CTFPlayerResource", "m_iMaxHealth");
	NETVAR_ARRAY(m_iDamage, int, "CTFPlayerResource", "m_iDamage");
	NETVAR_ARRAY(m_flConnectTime, float, "CTFPlayerResource", "m_flConnectTime");
};

// ================ SDK/Definitions/Main/CTFWeaponBase.h ================
// CSS_STUB tf2 api: CTFWeaponBase_GetSpreadAngles / CalcIsAttackCritical / IncrementAmmo
// sigs removed (MISSING in CSS, sig_check.md). GetSpreadAngles/CalcIsAttackCritical are
// TF2 crit/spread-pattern concepts, no CSS equivalent. IncrementAmmo (per-shell reload
// anim event, hooks.cpp:472) DOES have a CSS analog (M3/XM1014 shotgun single-shell
// reload) but needs its own CSS sig -- stubbed no-op below, NEEDS FROM OTHERS.

class CTFPlayer;
class CTFGrenadePipebombProjectile;

//credits: KGB (all weapon info stuff below)

typedef enum
{
	EMPTY,
	SINGLE,
	SINGLE_NPC,
	WPN_DOUBLE,
	DOUBLE_NPC,
	BURST,
	RELOAD,
	RELOAD_NPC,
	MELEE_MISS,
	MELEE_HIT,
	MELEE_HIT_WORLD,
	SPECIAL1,
	SPECIAL2,
	SPECIAL3,
	TAUNT,
	DEPLOY,
	NUM_SHOOT_SOUND_TYPES,
} WeaponSound_t;

#define MAX_WEAPON_STRING 80
#define MAX_WEAPON_PREFIX 16
#define MAX_WEAPON_AMMO_NAME 32

class FileWeaponInfo_t
{
public:
	void* m_pVtable;

	bool bParsedScript;
	bool bLoadedHudElements;

	// SHARED
	char szClassName[MAX_WEAPON_STRING];
	char szPrintName[MAX_WEAPON_STRING]; // Name for showing in HUD, etc.

	char szViewModel[MAX_WEAPON_STRING];       // View model of this weapon
	char szWorldModel[MAX_WEAPON_STRING];      // Model of this weapon seen carried by the player
	char szAnimationPrefix[MAX_WEAPON_PREFIX]; // Prefix of the animations that should be used by the player carrying this weapon
	int  iSlot;                                // inventory slot.
	int  iPosition;                            // position in the inventory slot.
	int  iMaxClip1;                            // max primary clip size (-1 if no clip)
	int  iMaxClip2;                            // max secondary clip size (-1 if no clip)
	int  iDefaultClip1;                        // amount of primary ammo in the gun when it's created
	int  iDefaultClip2;                        // amount of secondary ammo in the gun when it's created
	int  iWeight;                              // this value used to determine this weapon's importance in autoselection.
	int  iRumbleEffect;                        // Which rumble effect to use when fired? (xbox)
	bool bAutoSwitchTo;                        // whether this weapon should be considered for autoswitching to
	bool bAutoSwitchFrom;                      // whether this weapon can be autoswitched away from when picking up another weapon or ammo
	int  iFlags;                               // miscellaneous weapon flags
	char szAmmo1[MAX_WEAPON_AMMO_NAME];        // "primary" ammo type
	char szAmmo2[MAX_WEAPON_AMMO_NAME];        // "secondary" ammo type

	// Sound blocks
	char aShootSounds[NUM_SHOOT_SOUND_TYPES][MAX_WEAPON_STRING];

	int  iAmmoType;
	int  iAmmo2Type;
	bool m_bMeleeWeapon; // Melee weapons can always "fire" regardless of ammo.

	// This tells if the weapon was built right-handed (defaults to true).
	// This helps cl_righthand make the decision about whether to flip the model or not.
	bool m_bBuiltRightHanded;
	bool m_bAllowFlipping; // False to disallow flipping the model, regardless of whether
	// it is built left or right handed.

	// CLIENT DLL
	// Sprite data, read from the data file
	int   iSpriteCount;
	CHudTexture* iconActive;
	CHudTexture* iconInactive;
	CHudTexture* iconAmmo;
	CHudTexture* iconAmmo2;
	CHudTexture* iconCrosshair;
	CHudTexture* iconAutoaim;
	CHudTexture* iconZoomedCrosshair;
	CHudTexture* iconZoomedAutoaim;
	CHudTexture* iconSmall;

	// TF2 specific
	bool bShowUsageHint; // if true, then when you receive the weapon, show a hint about it
};

struct WeaponData_t
{
	int   m_nDamage;
	int   m_nBulletsPerShot;
	float m_flRange;
	float m_flSpread;
	float m_flPunchAngle;
	float m_flTimeFireDelay;   // Time to delay between firing
	float m_flTimeIdle;        // Time to idle after firing
	float m_flTimeIdleEmpty;   // Time to idle after firing last bullet in clip
	float m_flTimeReloadStart; // Time to start into a reload (ie. shotgun)
	float m_flTimeReload;      // Time to reload
	bool  m_bDrawCrosshair;    // Should the weapon draw a crosshair
	int   m_iProjectile;       // The type of projectile this mode fires
	int   m_iAmmoPerShot;      // How much ammo each shot consumes
	float m_flProjectileSpeed; // Start speed for projectiles (nail, etc.); NOTE: union with something non-projectile
	float m_flSmackDelay;      // how long after swing should damage happen for melee weapons
	bool  m_bUseRapidFireCrits;

	void Init()
	{
		m_nDamage = 0;
		m_nBulletsPerShot = 0;
		m_flRange = 0.f;
		m_flSpread = 0.f;
		m_flPunchAngle = 0.f;
		m_flTimeFireDelay = 0.f;
		m_flTimeIdle = 0.f;
		m_flTimeIdleEmpty = 0.f;
		m_flTimeReloadStart = 0.f;
		m_flTimeReload = 0.f;
		m_iProjectile = 0;
		m_iAmmoPerShot = 0;
		m_flProjectileSpeed = 0.f;
		m_flSmackDelay = 0.f;
		m_bUseRapidFireCrits = false;
	};
};

class CTFWeaponInfo : public FileWeaponInfo_t
{
public:
	WeaponData_t m_WeaponData[2];                    //0x06F8
	int          m_iWeaponType;                      //0x0778
	int          m_bGrenade;                         //0x077C
	float        m_flDamageRadius;                   //0x0780
	float        m_flPrimerTime;                     //0x0784
	bool         m_bLowerWeapon;                     //0x0788
	bool         m_bSuppressGrenTimer;               //0x0789
	bool         m_bHasTeamSkins_Viewmodel;          //0x078A
	bool         m_bHasTeamSkins_Worldmodel;         //0x078B
	char         m_szMuzzleFlashModel[128];          //0x078C
	float        m_flMuzzleFlashModelDuration;       //0x080C
	char         m_szMuzzleFlashParticleEffect[128]; //0x0810
	char         m_szTracerEffect[128];              //0x0890
	bool         m_bDoInstantEjectBrass;             //0x0910
	char         m_szBrassModel[128];                //0x0911
	char         m_szExplosionSound[128];            //0x0991
	char         m_szExplosionEffect[128];           //0x0A11
	char         m_szExplosionPlayerEffect[128];     //0x0A91
	char         m_szExplosionWaterEffect[128];      //0x0B11
	bool         m_bDontDrop;                        //0x0B91

	WeaponData_t const& GetWeaponData(int iWeapon) const { return m_WeaponData[iWeapon]; }
};

class CTFWeaponBase : public CBaseCombatWeapon
{
public:
	NETVAR(m_iReloadMode, int, "CTFWeaponBase", "m_iReloadMode");
	NETVAR(m_bReloadedThroughAnimEvent, bool, "CTFWeaponBase", "m_bReloadedThroughAnimEvent");
	NETVAR(m_flReloadPriorNextFire, float, "CTFWeaponBase", "m_flReloadPriorNextFire");
	NETVAR(m_flLastFireTime, float, "CTFWeaponBase", "m_flLastFireTime");
	NETVAR(m_flObservedCritChance, float, "CTFWeaponBase", "m_flObservedCritChance");
	NETVAR(m_flEnergy, float, "CTFWeaponBase", "m_flEnergy");

	NETVAR_OFF(GetWeaponInfo, CTFWeaponInfo*, "CTFWeaponBase", "m_flEffectBarRegenTime", 16);
	NETVAR_OFF(m_flSmackTime, float, "CTFWeaponBase", "m_nInspectStage", 28);
	NETVAR_OFF(m_flCritTokenBucket, float, "CTFWeaponBase", "m_iReloadMode", -244);
	NETVAR_OFF(m_nCritChecks, int, "CTFWeaponBase", "m_iReloadMode", -240);
	NETVAR_OFF(m_nCritSeedRequests, int, "CTFWeaponBase", "m_iReloadMode", -236);
	NETVAR_OFF(m_iWeaponMode, int, "CTFWeaponBase", "m_iReloadMode", -4);
	NETVAR_OFF(m_flCritTime, float, "CTFWeaponBase", "m_flLastCritCheckTime", -4);
	NETVAR_OFF(m_iCurrentSeed, int, "CTFWeaponBase", "m_flLastCritCheckTime", 8);
	NETVAR_OFF(m_flLastRapidFireCritCheckTime, float, "CTFWeaponBase", "m_flLastCritCheckTime", 12);
	inline void* m_pMeter()
	{
		static int nOffset = U::NetVars.GetNetVar("CTFWeaponBase", "m_flEffectBarRegenTime") + -40;
		return reinterpret_cast<void*>(uintptr_t(this) + nOffset);
	};

	// CSS slots: GetSlot/GetWeaponID resolved via the same WeaponInfo-accessor cluster and
	// -17 shift as CBaseCombatWeapon's own virtuals above (disasm-verified: slot 314 reads
	// an int field, matches GetSlot's int return). GetWeaponID=371 is C_WeaponCSBase's OWN
	// new virtual (not a CBaseCombatWeapon one): disasm CONFIRMED const-return "xor eax,eax"
	// at the base-class default, matching weapon_csbase.h's `virtual CSWeaponID GetWeaponID()
	// { return WEAPON_NONE; }` -- real weapons override it with their own constant, so the
	// vtable call still returns the correct per-weapon ID despite the base stub doing nothing.
	VIRTUAL(GetSlot, int, 314, this);
	VIRTUAL(GetWeaponID, int, 371, this);
	// GetDamageType/IsEnergyWeapon/AreRandomCritsEnabled removed: TF2 crit/energy-weapon
	// concepts, no CSS counterpart, no caller remained (grepped). GetWeaponSpread kept as a
	// stub (live callers in aim_hitscan.cpp/nospread_hitscan.cpp, not owned by this unit):
	// its TF2 slot 470 was already out-of-range/invalid in TF2 itself (vtable_check.md), so
	// this was never a real vtable call to begin with. GetSwingRange removed (also invalid
	// slot in TF2, no caller).
	inline float GetWeaponSpread() { return 0.f; } // CSS_STUB: see above, no verified slot
	
	// CSS_STUB tf2 api: see removal note above.
	inline void IncrementAmmo() {}
	inline bool CalcIsAttackCritical() { return false; }
	inline void GetSpreadAngles(Vec3& out) { out = {}; }
	Vec3 GetSpreadAngles()
	{
		Vec3 vOut;
		GetSpreadAngles(vOut);
		return vOut;
	}

	bool HasPrimaryAmmoForShot();
	bool CanPrimaryAttack();
	bool CanSecondaryAttack();
	bool CanFireCriticalShot(bool bIsHeadshot = false);
	bool IsInReload();
	bool CanHeadshot();
	bool AmbassadorCanHeadshot(float flCurTime = I::GlobalVars->curtime);
	float GetDamage(bool bAttribHookValue = true);
	float GetFireRate(bool bAttribHookValue = true);
	int GetBulletsPerShot(bool bAttribHookValue = true);
	int GetAmmoPerShot(bool bAttribHookValue = true);
	bool IsRapidFire();
	float GetSmackDelay();
	float GetRange();
};

class CTFKnife : public CTFWeaponBase
{
public:
};

class CTFMinigun : public CTFWeaponBase
{
public:
	NETVAR(m_iWeaponState, int, "CTFMinigun", "m_iWeaponState");
};

class CWeaponMedigun : public CTFWeaponBase
{
public:
	NETVAR(m_hHealingTarget, EHANDLE, "CWeaponMedigun", "m_hHealingTarget");

};

class CTFPipebombLauncher : public CTFWeaponBase
{
public:
	NETVAR(m_flChargeBeginTime, float, "CTFPipebombLauncher", "m_flChargeBeginTime");

	int GetDetonateType();
};

class CTFSniperRifle : public CTFWeaponBase
{
public:
	NETVAR(m_flChargedDamage, float, "CTFSniperRifle", "m_flChargedDamage");

	int GetRifleType();
	float GetHeadshotMult(CTFPlayer* pTarget = nullptr);
	float GetBodyshotMult(CTFPlayer* pTarget = nullptr);
};

class CTFGrenadeLauncher : public CTFWeaponBase
{
public:
	NETVAR(m_flDetonateTime, float, "CTFGrenadeLauncher", "m_flDetonateTime");

	int GetDetonateType();
};

class CTFSniperRifleClassic : public CTFSniperRifle
{
public:
};

class CTFParticleCannon : public CTFWeaponBase
{
public:
	NETVAR(m_flChargeBeginTime, float, "CTFParticleCannon", "m_flChargeBeginTime");
};

class CTFFlareGun : public CTFWeaponBase
{
public:
	int GetFlareGunType();
};

class CTFThrowable : public CTFWeaponBase
{
public:
	NETVAR(m_flChargeBeginTime, float, "CTFThrowable", "m_flChargeBeginTime");
};

class CTFGrapplingHook : public CTFWeaponBase
{
public:
	NETVAR(m_hProjectile, EHANDLE, "CTFGrapplingHook", "m_hProjectile");
};

class CTFSpellBook : public CTFThrowable
{
public:
};

// ================ SDK/Definitions/Main/IAchievementMgr.h ================
#ifndef IACHIEVEMENTMGR_H
#define IACHIEVEMENTMGR_H
#ifdef _WIN32
#endif

class CBaseAchievement;

class IAchievement
{
public:
	virtual int GetAchievementID() = 0;
	virtual const char* GetName() = 0;
	virtual int GetFlags() = 0;
	virtual int GetGoal() = 0;
	virtual int GetCount() = 0;
	virtual bool IsAchieved() = 0;
	virtual int GetPointValue() = 0;
	virtual bool ShouldSaveWithGame() = 0;
	virtual bool ShouldHideUntilAchieved() = 0;
	virtual bool ShouldShowOnHUD() = 0;
	virtual void SetShowOnHUD(bool bShow) = 0;
};

class IAchievementMgr
{
public:
	virtual IAchievement* GetAchievementByIndex(int index) = 0;
	virtual CBaseAchievement* GetAchievementByID(int id) = 0;
	virtual int GetAchievementCount() = 0;
	virtual void InitializeAchievements() = 0;
	virtual void AwardAchievement(int iAchievementID) = 0;
	virtual void OnMapEvent(const char* pchEventName) = 0;
	virtual void DownloadUserData() = 0;
	virtual void EnsureGlobalStateLoaded() = 0;
	virtual void SaveGlobalStateIfDirty(bool bAsync) = 0;
	virtual bool HasAchieved(const char* pchName) = 0;
	virtual bool WereCheatsEverOn() = 0;
};

// flags for IAchievement::GetFlags

// Update this for changes in either abstract class in this file

#endif

// ================ SDK/Definitions/Main/IMaterialVar.h ================
class IMaterial;
class VMatrix;
class ITexture;

class IMaterialVar;   // body dropped: only ever named through a pointer here

// ================ SDK/Definitions/Main/ITexture.h ================
class ITexture;

class ITexture;   // body dropped: only ever named through a pointer here

// ================ SDK/Definitions/Main/PlayerStats.h ================
#ifndef ARRAYSIZE
	#define ARRAYSIZE(A) (sizeof(A)/sizeof((A)[0]))
#endif

typedef uint64 itemid_t;
typedef int32 entityquality_t;

// ================ SDK/Definitions/Classes.h ================
