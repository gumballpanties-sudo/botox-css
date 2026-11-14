#pragma once
// ESP settings: plain primitives, defined in src/dllmain.cpp, saved by hacks/menu/config.cpp.
// One extern list for every reader: esp.cpp (aim TU, must not see dih's globals.h), globals.h
// (menu/config) and render.cpp. Names follow botox g_variables (m_players_box -> g_espBox ...),
// defaults are botox's visual_variables.inc ones.

// ---- players (botox "players" group) ----
extern bool  g_espEnable;              // m_players
extern int   g_espKey;                 // m_players_key: mode 0 always on (default), 1 hold, 2 toggle
extern int   g_espKeyMode;
bool Esp_On();                         // g_espEnable + bind; mode 0 / no key = on, menu open too. Preview ignores it
extern bool  g_espTeammates;
extern float g_espMaxDistance;         // 0 = no limit
extern bool  g_espDistanceFade;        // fades over the last quarter of max distance
extern bool  g_espSoundOnly;           // enemies only drawn for a while after they make a sound
extern float g_espSoundDuration;
extern bool  g_espSoundFade;

extern bool  g_espBox;
extern bool  g_espBoxVisColors;
extern float g_espBoxVisibleColor[4];
extern float g_espBoxInvisibleColor[4];
extern float g_espBoxColor[4];
extern bool  g_espBox3d;
extern bool  g_espBoxCorner;
extern float g_espBoxThickness;
extern float g_espBoxRounding;
extern bool  g_espBoxOutline;
extern float g_espBoxOutlineColor[4];
extern float g_espBoxOutlineThickness;
extern int   g_espBoxOutlineSide;      // 0 inside, 1 outside, 2 both

extern bool  g_espBoxFill;
extern float g_espBoxFillColor[4];
extern bool  g_espBoxFillGradient;
extern float g_espBoxFillBottomColor[4];

extern bool  g_espName;
extern int   g_espNamePosition;        // 0 above box, 1 below box, 2 inside top, 3 inside bottom
extern bool  g_espNameVisColors;
extern float g_espNameVisibleColor[4];
extern float g_espNameInvisibleColor[4];
extern float g_espNameColor[4];

extern bool  g_espHealthBar;
extern int   g_espHealthBarSide;       // 0 left, 1 right, 2 top, 3 bottom, 4 corner
extern int   g_espHealthBarCornerPick; // 0 bottom left, 1 bottom right, 2 top left, 3 top right
extern float g_espHealthBarCornerRounding;
extern float g_espHealthBarCornerSize;
extern float g_espHealthBarThickness;
extern bool  g_espHealthBarOutline;
extern bool  g_espHealthBarCustomColor;
extern float g_espHealthBarColor[4];
extern bool  g_espHealthBarGradient;
extern float g_espHealthBarBottomColor[4];
extern float g_espHealthBarBgColor[4];

extern bool  g_espHealthText;
extern bool  g_espHealthSuffix;
extern int   g_espHealthTextStyle;     // 0 standalone bottom, 1 follow health bar
extern bool  g_espHealthTextCustomColor;
extern float g_espHealthTextColor[4];

extern bool  g_espWeaponName;
extern float g_espWeaponNameColor[4];
extern bool  g_espAmmoBar;
extern float g_espAmmoBarColor[4];

extern bool  g_espDistance;
extern int   g_espDistanceUnit;        // 0 meters, 1 units, 2 feet
extern float g_espDistanceColor[4];

extern bool  g_espFlags;
extern int   g_espFlagList;            // bitmask, bit i = EspFlagNames()[i]
extern float g_espFlagsColor[4];
extern float g_espFlagsAlertColor[4];

extern bool  g_espSkeleton;
extern bool  g_espSkeletonVisColors;
extern float g_espSkeletonVisibleColor[4];
extern float g_espSkeletonInvisibleColor[4];
extern float g_espSkeletonColor[4];
extern int   g_espSkeletonType;        // 0 normal, 2 smooth (1 was lag compensated, removed with backtrack)
extern float g_espSkeletonThickness;

extern bool  g_espHitboxes;
extern int   g_espHitboxesMode;        // 0 always, 1 on hit
extern float g_espHitboxColor[4];
extern float g_espHitboxesThickness;
extern float g_espHitboxesDuration;

extern bool  g_espAvatar;
extern int   g_espAvatarPosition;      // 0 next to name, 1 above box, 2 below box, 3 cover head
extern float g_espAvatarSize;
extern float g_espAvatarHeadScale;

extern bool  g_espDamageNumbers;
extern float g_espDmgColor[4];
extern float g_espDmgDuration;
extern float g_espDmgSpeed;
extern bool  g_espDmgStack;

extern bool  g_espOofArrows;
extern int   g_espOofType;             // 0 players, 1 enemy sounds, 2 players + sounds
extern float g_espOofColor[4];         // visible
extern float g_espOofWallColor[4];
extern float g_espOofSoundColor[4];
extern float g_espOofSoundDuration;
extern bool  g_espOofSoundFade;
extern int   g_espOofStyle;            // 0 default, 1 interium
extern float g_espOofWidth;
extern float g_espOofHeight;
extern int   g_espOofDistance;
extern bool  g_espOofAvatar;
extern bool  g_espOofAspect;

extern bool  g_espSoundPings;          // sound_esp: ring where an enemy made a sound (botox players.cpp sound_arrows)
extern float g_espSoundColor[4];

// ---- planted C4 (botox hacks/visuals/edicts/edicts.cpp bomb(): m_bomb_esp / m_bomb_timer_bar) ----
// No buildings in CS:S (TF2 sentry/dispenser/tele removed, no analog). Botox has no world hostage
// ESP either -- hostage state is a player flag only (EF_HOSTAGE below).
extern bool  g_espC4;                  // m_bomb_esp: world text at the bomb
extern bool  g_espC4Timer;             // m_bomb_esp_timer
extern bool  g_espC4Icon;              // m_bomb_esp_icon
extern float g_espC4Color[4];          // m_bomb_esp_color
extern float g_espC4DefuseColor[4];    // m_bomb_esp_defuse_color: being defused, winning the race
extern float g_espC4FailColor[4];      // m_bomb_esp_fail_color: being defused, losing the race
extern bool  g_espC4Bar;               // m_bomb_timer_bar: screen-centre countdown bar
extern int   g_espC4BarPosition;       // m_bomb_timer_bar_position: y, screen top

// ---- world entities (edicts page) ----
extern bool  g_espWorldEnable;
extern bool  g_espAmmoPacks;
extern bool  g_espDroppedWeapons;
extern bool  g_espProjectiles;
extern bool  g_espProjPath;
extern float g_espProjPathColor[4];
extern float g_espItemColor[4];
extern float g_espProjColor[4];

// shared with sound chams (botox m_sound_esp_min_volume / m_sound_esp_max_distance)
extern float g_chamsSoundMinVolume;
extern float g_chamsSoundMaxDistance;

// botox m_player_flags (e_player_flags, globals/config/variables.h): money, armor, helmet, kit
// (has defuser), defusing, walking, scoped, flashed, bomb (carrying C4), hostage (grabbing one),
// immunity (sm_god), wallbangable. Menu order == bit order == botox's own.
// "walking" (m_bIsWalking) is IMPOSSIBLE in CS:S: grepped css-community-master, no IsWalking /
// m_bIsWalking anywhere -- CS:S (2004-era) never shipped a walk-toggle key, that is CS:GO-only.
// Dropped; every other bit index below shifts up by one from botox's own enum to close the gap.
enum EspFlagBit
{
    EF_MONEY, EF_ARMOR, EF_HELMET, EF_KIT, EF_DEFUSING, EF_SCOPED, EF_FLASHED, EF_BOMB,
    EF_HOSTAGE, EF_IMMUNITY, EF_WALLBANGABLE, EF_COUNT
};
inline const char* const* EspFlagNames()
{
    static const char* const k[EF_COUNT] = { "money", "armor", "helmet", "kit", "defusing", "scoped", "flashed", "bomb",
        "hostage", "immune", "wallbangable" };
    return k;
}
