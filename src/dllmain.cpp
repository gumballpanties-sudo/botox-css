// dih -- debloated TF2 visual overlay
// DX9 Present hook + ImGui menu. INSERT = menu, F11 = eject.
// Global definitions live here; features in src/hacks/*/*.cpp.

#include "globals/common.h"
#include "globals/globals.h"

// -- Core state ------------------------------------------------------------------
ICvar*             g_cvar             = nullptr;
HANDLE             g_hPollThread      = nullptr;
IEngineClient*     g_engine           = nullptr;
void*              g_gameEventManager = nullptr;
void*              g_matSystem        = nullptr;
void*              g_modelRender      = nullptr;
void*              g_modelInfo        = nullptr;
void*              g_renderView       = nullptr;
HWND               g_hwnd             = nullptr;
bool               g_imguiInit        = false;
IDirect3DDevice9*  g_d3dDev           = nullptr;
std::atomic<bool>  g_eject  {false};
std::atomic<bool>  g_ejectRequest {false};
std::atomic<int>   g_hookDepth     {0};
std::atomic<int>   g_miscHookDepth {0};

// -- Menu ------------------------------------------------------------------------
bool  g_showMenu       = false;
int   g_menuKey        = VK_INSERT;
ImFont* g_fontBold     = nullptr;
ImFont* g_fontReg      = nullptr;   // Tahoma 13 regular -- io.FontDefault used to be this;
                                    // now verdana 11 (g_menuReg) is the default (botox menu
                                    // spec section 6/9), so every HUD caller that used to get
                                    // the default font with NO g_fontBold-style fallback reads
                                    // this instead, and keeps its old size unchanged.
ImFont* g_fontMi       = nullptr;
ImFont* g_fontIcon13   = nullptr;   // botox font_name_icon_13 (cog glyph U+F013, OptionPopup)
ImFont* g_fontClarityInter = nullptr;
ImFont* g_fontClarityIcon  = nullptr;
ImFont* g_menuReg      = nullptr;
ImFont* g_menuBold     = nullptr;
ImFont* g_fontTahoma12 = nullptr, *g_fontTahomaBd12 = nullptr, *g_fontChillware13 = nullptr;
ImFont* g_fontLobotomy13 = nullptr, *g_fontLobotomyBold13 = nullptr;
ImFont* g_fontKamibebra13 = nullptr, *g_fontKamibebraBold13 = nullptr;
ImFont* g_fontCumhacck35 = nullptr, *g_fontAirplaneTitle = nullptr, *g_fontAirplaneRows = nullptr;
ImFont* g_fontInterBold15 = nullptr, *g_fontInterMedium15 = nullptr, *g_fontSunflower30 = nullptr;
ImFont* g_fontInterwebzCalibri = nullptr, *g_fontHavocTahoma13 = nullptr, *g_fontAirflow14 = nullptr;
ImFont* g_fontEvolve16 = nullptr, *g_fontEvolveBold16 = nullptr;
ImFont* g_fontLegendwareVerdana12 = nullptr, *g_fontLegendwareLucida10 = nullptr;
ImFont* g_fontInteriumDroid24 = nullptr, *g_fontSkebobNunito14 = nullptr;
ImFont* g_fontSkebobArial12 = nullptr, *g_fontSkebobArial13 = nullptr;
ImFont* g_fontCumidereTahoma16 = nullptr, *g_fontIllusoryTahoma13 = nullptr;
ImFont* g_fontLumiRubik16 = nullptr, *g_fontLumiProggy13 = nullptr, *g_fontInbaTahoma13 = nullptr;
ImFont* g_fontDnaMontserrat15 = nullptr, *g_fontDnaPtRootBold15 = nullptr, *g_fontDnaClarityIcon50 = nullptr;
ImFont* g_fontCucumberTahoma14 = nullptr, *g_fontCucumberVerdana14 = nullptr;
ImFont* g_fontHowewewareMinecraft14 = nullptr, *g_fontGamesenseSfBold15 = nullptr;
ImFont* g_fontArialBd16 = nullptr;
ImFont* g_fontMenuTahoma9 = nullptr, *g_fontMenuTahomaBd9 = nullptr;
ImFont* g_fontMs19 = nullptr;

// fpsoverlay minesweeper (hacks/misc/minesweeper.cpp): diff 0..2 presets, 3 = custom
bool  g_msOpen   = false;
bool  g_msNight  = true;
int   g_msDiff   = 1;
int   g_msWidth  = 20;
int   g_msHeight = 20;
int   g_msMines  = 60;

int   g_menuStyle      = 0;
float g_accentColor[4] = { 160.f / 255.f, 170.f / 255.f, 220.f / 255.f, 1.f };   // matches render.cpp's old fixed kAccent
float g_dpiScale       = 100.f;
bool  g_dpiScalePanels = false;
bool  g_safeMode       = false;

// -- Watermark -------------------------------------------------------------------
// Defaults = botox csgo-sdk (m_watermark true/tickrate/fps/time, ping/velocity off, text "botox").
bool  g_wmEnable       = true;
bool  g_wmShowLabel    = true;
char  g_wmLabel[64]    = "botox";
bool  g_wmShowFPS      = true;
bool  g_wmShowPing     = false;
bool  g_wmShowLoss     = false;
bool  g_wmShowTime     = true;
int   g_wmPos          = WM_TOP_RIGHT;
// Style 0 box is the botox dark fill (25,25,25) + grey outline (50,50,50) now; brand is
// always white, info parts always the menu accent, so this stays unused by style 0.
float g_wmTextColor[4] = { 0.86f, 0.86f, 0.90f, 1.f };

// -- Media player ------------------------------------------------------------------
bool  g_mpEnable       = false;
bool  g_mpCoverArt     = true;
bool  g_mpBackground   = true;
bool  g_mpProgressBar  = true;
bool  g_mpSimple       = false;
bool  g_mpLumi         = false;
bool  g_mpTime         = false;
int   g_mpPos          = WM_TOP_RIGHT;   // same corner as the watermark = stacked under it

// -- Unit E: misc HUD ---------------------------------------------------------------
bool  g_hmEnable        = false;
float g_hmColor[4]      = { 1.f, 1.f, 1.f, 1.f };
float g_hmSize          = 10.f;   // botox m_hit_marker_size default

bool  g_dpEnable        = false;
int   g_dpStyle         = 0;
float g_dpColor[4]      = { 140.f / 255.f, 200.f / 255.f, 1.f, 1.f };   // botox m_death_particles_* defaults
bool  g_dpMulticolor    = false;
int   g_dpAmount        = 8;
float g_dpSize          = 1.f;
float g_dpLifetime      = 3.f;
int   g_dpSoulStyle     = 0;
int   g_dpMcMode        = 0;
float g_dpMcXpTime      = 10.f;

bool  g_ptsEnable       = false;

bool  g_specEnable      = false;
bool  g_specAlwaysShow  = false;
int   g_specStyle       = 0;
int   g_specPos         = WM_TOP_LEFT;
float g_specOffX        = 0.f;   // drag offset on top of g_specPos (menu open, LMB drag / RMB reset)
float g_specOffY        = 0.f;
bool  g_specShowTarget    = true;
bool  g_specDelusionalLeft = false;
int   g_specListWidth     = 230;                          // botox m_spectators_list_width (slider 195..500)
float g_specColorOne[4]   = { 1.f, 1.f, 1.f, 1.f };       // botox text_color_one: rows watching the local player
float g_specColorTwo[4]   = { 1.f, 1.f, 1.f, 1.f };       // botox text_color_two: everyone else

int   g_wmStyle         = 0;

bool  g_discordEnable   = false;
char  g_discordAppId[32]     = "";
char  g_discordState[128]    = "In-game";
char  g_discordDetails[128]  = "Counter-Strike: Source";
char  g_discordImageKey[128] = "";

float g_consoleColor[4] = { 0.f, 1.f, 1.f, 1.f };   // matches the hardcoded cyan it replaces

// -- Viewmodel ---------------------------------------------------------------------
bool  g_vmSwayEnable   = false;
float g_vmSwayScale    = 0.f;
float g_vmSwayInterp   = 0.f;
bool  g_vmOffsetEnable = false;
float g_vmOffsetX      = 0.f;
float g_vmOffsetY      = 0.f;
float g_vmOffsetZ      = 0.f;
bool  g_vmRemoveBob    = false;
// CS:GO viewmodel (vm_csgo.h) -- defaults = CS:GO cvar defaults
bool  g_vmCsBob            = false;
float g_vmCsBobCycle       = 0.98f;  // cl_bobcycle
float g_vmCsBobUp          = 0.5f;   // cl_bobup
float g_vmCsBobVert        = 0.25f;  // cl_bobamt_vert
float g_vmCsBobLat         = 0.4f;   // cl_bobamt_lat
float g_vmCsBobLower       = 21.f;   // cl_bob_lower_amt
bool  g_vmCsGunLower       = false;
float g_vmCsGunLowerAngle  = 2.f;    // cl_gunlowerangle
float g_vmCsGunLowerSpeed  = 0.1f;   // cl_gunlowerspeed
bool  g_vmCsLandDip        = false;
float g_vmCsLandDipAmt     = 4.f;    // cl_headbob_land_dip_amt
float g_vmCsLandDipTime    = 0.25f;  // seconds to the bottom (CS:GO 0.25)
bool  g_vmAlphaEnable  = false;
float g_vmAlpha        = 0.5f;   // 1 = opaque (no-op), 0 = invisible viewmodel
bool  g_vmFlatAlpha    = true;   // one alpha over the whole weapon, not alpha x phong mask
bool  g_vmLowFps       = false;  // low fps viewmodel: gun anim stepped at g_vmLowFpsRate, camera live
float g_vmLowFpsRate   = 24.f;   // botox default 24, slider 15..60

// -- Debug log --------------------------------------------------------------------
bool  g_debugLogEnable = false;

// -- Fog -----------------------------------------------------------------------------
bool  g_fogEnable    = false;
float g_fogColor[4]  = { 0.5f, 0.5f, 0.5f, 1.f };
float g_fogStart     = 0.f;
float g_fogEnd       = 0.f;
float g_fogDensity   = 0.f;

// -- Screen effects (depth source -- hacks/screen/screenfx.cpp) ------------------------------
bool  g_fxDepthProbe  = false;
bool  g_fxForceQueueMode = true;
bool  g_fxUseCallQueue = true;
float g_fxParticleGuard = 1.0f;

// Ambient occlusion (hacks/screen/ssao.cpp). Defaults are coldtires' own, unchanged.
bool  g_aoEnable      = false;
float g_aoIntensity   = 1.6f;
float g_aoRadius      = 40.f;
int   g_aoSamples     = 12;
float g_aoBias        = 0.2f;
float g_aoBlur        = 1.f;
float g_aoScale       = 0.75f;
float g_aoFadeStart   = 1200.f;
float g_aoFadeEnd     = 3000.f;
bool  g_aoViewmodel   = true;
bool  g_aoFogFade     = true;
bool  g_aoDebug       = false;

// Depth of field (hacks/screen/dof.cpp). Defaults are coldtires' own, unchanged.
bool  g_dofEnable        = false;
bool  g_dofAutoFocus     = true;
float g_dofDistance      = 400.f;
float g_dofFocusSpeed    = 8.f;
float g_dofFocusLimit    = 4000.f;
float g_dofRange         = 300.f;
float g_dofRamp          = 600.f;
float g_dofMaxBlur       = 20.f;
float g_dofTapDensity    = 1.25f;
float g_dofBokehBoost    = 4.f;
float g_dofBokehThreshold= 0.6f;
bool  g_dofNearBlur      = true;
bool  g_dofViewmodelSharp= true;
bool  g_dofSmooth        = true;

// Frame blending (hacks/screen/frameblend.cpp). 8ms is a 1/125s shutter, which is about what a
// camera shooting 60fps footage of a game runs at -- long enough to see, short enough that the
// picture never stops being readable.
bool  g_fbEnable     = false;
float g_fbShutter    = 8.f;
float g_fbAmount     = 1.f;
bool  g_fbPlayers    = true;
bool  g_fbViewmodel  = true;
float g_fbVmAmount   = 1.f;
int   g_fbSamples    = 24;
float g_fbMaxStreak  = 0.06f;

// -- Bloom (hacks/screen/bloom.cpp) ----------------------------------------------------------
bool  g_bloomEnable    = false;
float g_bloomIntensity = 0.6f;
float g_bloomThreshold = 0.7f;
float g_bloomRadius    = 0.5f;
float g_bloomSat       = 1.2f;

// -- Color correction --------------------------------------------------------------------
bool  g_ccEnable      = false;
float g_ccGainR       = 1.f;
float g_ccGainG       = 1.f;
float g_ccGainB       = 1.f;
float g_ccExposure    = 0.f;
float g_ccTemp        = 0.f;
float g_ccHue         = 0.f;
float g_ccSat         = 1.f;
float g_ccContrast    = 1.f;
float g_ccLevInBlack  = 0.f;
float g_ccLevInWhite  = 1.f;
float g_ccGamma       = 1.f;
float g_ccLevOutBlack = 0.f;
float g_ccLevOutWhite = 1.f;
float g_ccHighlights  = 0.f;
float g_ccShadows     = 0.f;
// Identity: two points on the diagonal. CcIsNeutral() tests for exactly this, and the
// whole pass is skipped while all four curves match it.
CcCurve g_ccCurve[4] = {
    { 2, { 0.f, 1.f }, { 0.f, 1.f } },
    { 2, { 0.f, 1.f }, { 0.f, 1.f } },
    { 2, { 0.f, 1.f }, { 0.f, 1.f } },
    { 2, { 0.f, 1.f }, { 0.f, 1.f } },
};

// -- Unit D: simple screen post-fx (hacks/screen/screenfx_passes.cpp) -----------------------
bool  g_sharpenEnable      = false;
float g_sharpenAmount      = 0.5f;
bool  g_filmGrainEnable    = false;
float g_filmGrainAmount    = 0.08f;
bool  g_vignetteEnable     = false;
float g_vignetteAmount     = 0.4f;
bool  g_vignetteInvert     = false;
bool  g_invertFilterEnable = false;
bool  g_posterizeEnable    = false;
float g_posterizeLevels    = 8.f;
bool  g_debandEnable       = false;
float g_debandAmount       = 8.f;
bool  g_channelShiftEnable = false;
float g_channelShiftAmount = 2.f;
bool  g_channelShiftDepth      = false;
int   g_channelShiftDepthMode  = 0;
float g_channelShiftDepthDist  = 200.f;
float g_channelShiftDepthRange = 0.f;
float g_channelShiftDepthRamp  = 600.f;
// depth distort, Warp-FX defaults
bool  g_warpEnable          = false;
int   g_warpEffect          = 1;
int   g_warpAnimate         = 0;
float g_warpAnimSpeed       = 1.f;
float g_warpCenter[2]       = { 0.5f, 0.5f };
bool  g_warpUseOffset       = false;
float g_warpOffset[2]       = { 0.5f, 0.5f };
float g_warpRadius          = 0.5f;
float g_warpTension         = 1.f;
float g_warpMagnitude       = -0.5f;
float g_warpAngle           = 180.f;
bool  g_warpInverse         = false;
float g_warpInnerRadius     = 0.f;
int   g_warpSplices         = 10;
float g_warpAmplitude       = 1.f;
float g_warpPeriod          = 0.25f;
float g_warpPhase           = 0.f;
int   g_warpWaveType        = 1;
float g_warpWaveAngle       = 0.f;
float g_warpWavePeriod      = 3.f;
float g_warpWaveAmplitude   = 0.075f;
float g_warpDrunkStrength[2] = { 25.f, 25.f };
float g_warpDrunkSpeed[2]   = { 2.f, 2.f };
float g_warpDrunkAngle      = 180.f;
float g_warpDrunkAngleSpeed = 1.f;
float g_warpPlanetRot[3]    = { 0.5f, 0.5f, 0.5f };
float g_warpPlanetScale     = 10.f;
float g_warpPlanetOffset[2] = { 0.f, 0.f };
float g_warpPlanetSeam      = 0.5f;
float g_warpSlitPos         = 0.5f;
float g_warpSlitSpeed       = 1.f;
int   g_warpSlitDir         = 0;
float g_warpBorderColor[4]  = { 1.f, 0.f, 0.f, 1.f };
float g_warpAspect          = 0.f;
float g_warpAspectAngle     = 0.f;
int   g_warpBlend           = 0;
float g_warpOpacity         = 1.f;
int   g_warpDepthMode       = 0;
float g_warpDepthDist       = 200.f;
float g_warpDepthRange      = 0.f;
float g_warpDepthRamp       = 600.f;
bool  g_warpDepthProtect    = true;
bool  g_warpFade            = false;
bool  g_emphasizeEnable    = false;
float g_emphasizeAmount    = 1.f;     // botox m_emphasize_effect_factor
bool  g_emphasizeSpherical = true;
bool  g_showDepthEnable    = false;
// botox depth_view (emphasize = reshade Emphasize.fx, show depth = DisplayDepth.fx), botox defaults
float g_emphasizeFocusDepth  = 0.f;
float g_emphasizeFocusRange  = 0.f;
float g_emphasizeFocusEdge   = 0.001f;
int   g_emphasizeSphereFov   = 180;
float g_emphasizeSphereX     = 0.537f;
float g_emphasizeSphereY     = 0.5f;
float g_emphasizeBlendColor[4] = { 0.f, 0.f, 0.f, 1.f };
float g_emphasizeBlendFactor = 0.2f;
int   g_showDepthType        = 0;       // 0 depth map, 1 normal map, 2 both
bool  g_showDepthBlend       = false;
bool  g_showDepthLive        = false;
bool  g_showDepthUpsideDown  = false;
bool  g_showDepthReversed    = false;
bool  g_showDepthLog         = false;
float g_showDepthScaleX      = 1.f;
float g_showDepthScaleY      = 1.f;
int   g_showDepthOffsetX     = 0;
int   g_showDepthOffsetY     = 0;
float g_showDepthFarPlane    = 1000.f;
float g_showDepthMultiplier  = 1.f;

// -- Unit D: world fx (hacks/visuals/world_fx.cpp) --------------------------------------------
bool  g_fullbrightEnable    = false;
bool  g_disableInterpEnable = false;
bool  g_aspectRatioEnable   = false;
float g_aspectRatioValue    = 1.777778f; // botox m_aspect_ratio (16:9)
bool  g_worldFovEnable      = false;
float g_worldFovAmount      = 90.f;
bool  g_thirdPersonEnable   = false;
float g_thirdPersonDistance = 100.f;

// -- Unit D3: reflections + minecraft sky/clouds -----------------------------------------
bool  g_reflEnable    = false;
float g_reflIntensity = 1.f;    // botox m_reflections_intensity
float g_reflDistance  = 0.8f;   // botox m_reflections_distance
bool  g_mcWorldEnable = false;
// -- Unit D2: world fx 2 (hacks/visuals/world_fx2.cpp) ----------------------------------------
bool  g_removeVisualRecoilEnable = false;

bool  g_dlightEnable  = false;
float g_dlightRadius  = 150.f;
float g_dlightColor[4]= { 1.f, 0.6f, 0.2f, 1.f };


bool  g_worldTintEnable   = false;
float g_worldTintColor[4] = { 1.f, 1.f, 1.f, 1.f };
bool  g_propTintEnable    = false;
float g_propTintColor[4]  = { 1.f, 1.f, 1.f, 1.f };
bool  g_weatherTintEnable   = false;
float g_weatherTintColor[4] = { 1.f, 1.f, 1.f, 1.f };

// -- Skybox ----------------------------------------------------------------------------
bool g_skyEnable     = false;
char g_skyName[64]   = "sky_day01_01";

// -- Weather ------------------------------------------------------------------------------
bool  g_weatherEnable = false;
int   g_weatherMode   = 0;    // 0=off, 1=rain, 2=snow
// Defaults are the engine's own (r_rain* convar defaults / SNOW_SPEED), NOT zero. Every one of
// these feeds a formula that collapses at 0: speed divides ComputeEmissionArea's fallTime,
// length scales the tracer delta to nothing, radius is the emission box edge.
float g_rainAlpha     = 0.40f;
float g_rainSpeed     = 600.f;
float g_rainWidth     = 0.50f;
float g_rainLength    = 0.10f;
float g_rainRadius    = 1500.f;
float g_rainSideVel   = 130.f;
float g_snowAlpha     = 0.60f;
float g_snowSpeed     = 80.f;   // SNOW_SPEED
float g_snowWidth     = 0.70f;  // SNOW_TRACER_WIDTH
float g_snowLength    = 0.05f;  // half the rain streak -- flake, not a line
float g_snowRadius    = 1500.f;
float g_snowSideVel   = 30.f;
float g_snowDensity   = 1.f;

// -- No hats ---------------------------------------------------------------------------------

// -- Weapon sheen blend: deleted round-4 (VISTAB). TF2 killstreak-sheen DIP intercept
// (g_sheenNormal/Alpha/Env) never fired on CS:S; real weapon sheen is world_fx2.cpp's
// Fx2_SheenTick ($envmaptint), cfg: u_world.h g_wpnSheen*.

// -- Killfeed ------------------------------------------------------------------------------------
bool g_selfKillfeedOnly = false;

// -- Crosshair ----------------------------------------------------------------------------------------
bool  g_chEnable        = false;
// defaults = SimpleVisual's C:\dih\default.cfg -- at 0 thickness/length enabling drew nothing
bool  g_chAntialias     = true;
bool  g_chAdditive      = false;   // arms + dot blend ONE instead of INVSRCALPHA
int   g_chBlendMode     = 0;       // kChBlend id (ch_blend_modes.h); 0 falls back to g_chAdditive
float g_chColor[4]      = { 1.f, 1.f, 1.f, 1.f };
float g_chThickness     = 1.1f;
float g_chLength        = 3.f;
float g_chGap           = 1.6f;
float g_chAspect        = 1.f;     // arm length w:h. 1 = square, >1 wider, <1 taller
float g_chGapAspect     = 1.f;     // gap w:h, same rule
bool  g_chDot           = false;
bool  g_chDotCircle     = false;
bool  g_chOutline       = true;
float g_chOutlineColor[4] = { 0.f, 0.f, 0.f, 1.f };
float g_chOutlineThick  = 0.5f;
// botox custom_crosshair.cpp full-port fields -- defaults match botox csgo-sdk defaults
// (every side on, no taper/gradient/dynamic/blur/rotation = identical look to the old simplified build)
bool  g_chSidesOnly     = false;
bool  g_chSideL = true, g_chSideR = true, g_chSideT = true, g_chSideB = true;
bool  g_chOutlineOnLines = true;
bool  g_chOutlineOnDot   = true;
bool  g_chOutlineSidesOnly = false;
bool  g_chOutlineSideL = true, g_chOutlineSideR = true, g_chOutlineSideT = true, g_chOutlineSideB = true;
bool  g_chOutlineBlur       = false;
float g_chOutlineBlurRadius = 2.f;
bool  g_chDynamic    = false;
float g_chDynamicMin = 0.f;
float g_chDynamicMax = 2.f;
float g_chDynamicRef = 300.f;
bool  g_chGradient    = false;
float g_chGradCenter[4] = { 1.f, 1.f, 1.f, 1.f };
float g_chGradOuter[4]  = { 1.f, 1.f, 1.f, 1.f };
float g_chGradStart   = 0.f;
float g_chGradEnd     = 1.f;
bool  g_chTaper     = false;
float g_chTaperNear = 1.f;
float g_chTaperFar  = 1.f;
bool  g_chDotCustomColor = false;
float g_chDotColor[4]    = { 1.f, 1.f, 1.f, 1.f };
float g_chRotation = 0.f;
bool  g_forceCrosshair = false;

// -- Sniper crosshair (new unit, botox indicators.cpp sniper_crosshair) ------------------------
bool  g_sniperChEnable = false;
int   g_sniperChStyle  = 0;
float g_sniperChColor[4]        = { 1.f, 1.f, 1.f, 1.f };
float g_sniperChOutlineColor[4] = { 0.f, 0.f, 0.f, 1.f };
float g_sniperChDotColor[4]     = { 1.f, 1.f, 1.f, 1.f };
float g_sniperChSize      = 5.f;
float g_sniperChThickness = 0.5f;
float g_sniperChGap       = 3.f;
bool  g_sniperChDot     = true;
bool  g_sniperChT       = false;
bool  g_sniperChOutline = true;
float g_sniperChOutlineThickness = 1.f;
float g_sniperChInterwebzSize      = 1.f;
int   g_sniperChInterwebzGap       = 4;
int   g_sniperChInterwebzThickness = 1;
bool  g_sniperChInterwebzArmCorners = false;
bool  g_sniperChInterwebzDotCorners = false;

// CSGO default: cl_crosshaircolor 1 -> crosshair.css crosshairColor1 #82b116, gap offset 0
bool  g_chCsgoEnable    = false;
int   g_chCsgoStyle     = 0;
float g_chCsgoColor[4]  = { 130.f/255.f, 177.f/255.f, 22.f/255.f, 1.f };
float g_chCsgoGap       = 0.f;
float g_chCsgoScale     = 1.f;
bool  g_chCsgoCrisp     = true;
bool  g_chCsgoLinear    = true;
bool  g_chCsgoAdditive  = true;     // csgo's SRGBadditive on pips/arcs/dot
float g_chCsgoBlack     = 1.0f;     // 1 = csgo's own wash strength
float g_chCsgoVelSens   = 1.f;      // 1 = csgo's own step sizes, 0 = movement moves nothing
float g_chCsgoSmooth    = 0.f;      // seconds to ease into a new gap/spread, 0 = snap (csgo)
bool  g_chCsgoArcs      = true;     // the 4 thin spread lines
bool  g_chCsgoPips      = true;     // the 4 green gap arms
float g_chCsgoAspect    = 1.f;      // arm length w:h, both style families
float g_chCsgoGapAspect = 1.f;      // gap w:h, both style families
// classic styles 2/3/4 -- csgo convar defaults, one for one
float g_csXSize         = 5.f;      // cl_crosshairsize
float g_csXThick        = 0.5f;     // cl_crosshairthickness
float g_csXGap          = 1.f;      // cl_crosshairgap
bool  g_csXDot          = true;     // cl_crosshairdot
bool  g_csXOutline      = true;     // cl_crosshair_drawoutline
float g_csXOutlineTh    = 1.f;      // cl_crosshair_outlinethickness
bool  g_csXT            = false;    // cl_crosshair_t
int   g_csXAlpha        = 200;      // cl_crosshairalpha
bool  g_csXUseAlpha     = true;     // cl_crosshairusealpha
float g_csXSplitDist    = 7.f;      // cl_crosshair_dynamic_splitdist
float g_csXSplitRatio   = 0.35f;    // cl_crosshair_dynamic_maxdist_splitratio
float g_csXInnerMod     = 1.f;      // cl_crosshair_dynamic_splitalpha_innermod
float g_csXOuterMod     = 0.5f;     // cl_crosshair_dynamic_splitalpha_outermod

// Region selector / net settings presets / engine net_graph recolor+reposition hooks
// DELETED 11-03 (DE-TF2 pass): none exist in botox csgo, all TF2-GC or TF2-sig-only.
// See src/hacks/network/netregion.h for what and why. Dangling declarations in
// menu.cpp / config.cpp: see report NEEDS FROM OTHERS.

// -- Equip region unlock ---------------------------------------------------------------------
// Client-side loadout gate removal: cosmetics that share an equip_region (two hats, hat +
// whole_head, ...) stop greying each other out. Three client.dll hooks, see equipregion.cpp.
bool g_equipRegionUnlock  = false;

// -- Bypass ---------------------------------------------------------------------------------------------
bool g_pureBypass   = false;
bool g_cheatsBypass = false;

// -- Removals -----------------------------------------------------------------------------------------------
bool g_removeRagdolls = false;
bool g_removePropFade = false;
bool g_fpsBoost       = false; // bones optimization: cached SetupBones for non-local players
bool g_removeScreenEffects = false; // swallow Shake/Fade/Rumble user messages

// -- Flip world (flipworld.cpp) -- mirror the 3D frame, the HUD's world anchors, the hand + input --
bool g_flipWorld = false;

// -- Lag resolver (lagres.cpp) -- draw a laggy player smooth instead of frozen-then-jumped --
bool  g_lagResolver = false;  // off by default: it trades a little accuracy for no twitch
bool  g_lrYaw       = true;   // smooth the model's yaw too, not just his position
float g_lrSmoothMs  = 60.f;   // ease time constant, ms. bigger = smoother AND later
float g_lrExtrapMs  = 200.f;  // how long a stalled player is carried at his last velocity
float g_lrMaxErr    = 64.f;   // units of error we refuse to smear -- snap past this

// -- Performance / latency (default OFF -- opt in from the menu) ------------------------------------------
// "low latency (gpu flush)" and "high process priority" DELETED 2026-09-05, DO NOT RE-ADD.
// The flush spun on a D3DQUERYTYPE_EVENT after every Present, so frame time stopped being
// max(CPU, GPU) and became CPU + GPU -- measured as net_graph out packets/s falling 60 -> 50
// (that row is one send per rendered FRAME, so it reads as packet loss and is really fps).
// HIGH_PRIORITY_CLASS made it worse: the spin was a PAUSE loop with no yield to the OS,
// holding a core against the rest of hl2.exe for the whole GPU frame. Neither bought a
// measurable win. Only the 1ms timer survives -- it costs nothing and is not a spin.
bool g_hiTimerRes  = false; // timeBeginPeriod(1) -- engine fps_max sleeps get 1ms, not 15.6ms
bool g_perfEnable  = false; // botox m_performance default
// botox m_convenience_buttons defaults (its ak47/awp/csgo-server rows have none)
char g_convTitle[kConvButtons][32]  = { "kill all bots", "kick all bots" };
// bot_kill / bot_kick take "all" (css-community-master game/server/cstrike/bot/
// cs_bot_manager.cpp:667,703 -- same syntax CSS ships, not TF2's tf_bot_*)
char g_convCmd[kConvButtons][160]   = { "bot_kill all", "bot_kick all" };

// -- fpsopt.cpp DELETED: its nine convar groups only mirrored console commands the player can
// set themselves, so the DLL was holding settings, not running a feature. cfg keys went too.

// -- Auto backstab: lives in the aim tree now (src/hacks/aim/aim_melee.cpp, knobs under
// Vars::Aimbot::Melee). dih's g_backstab* / g_cleanPovDemos globals are deleted -- their
// last reader went with backstab.cpp at bs11, and their cfg keys with it.

// -- Movement (all default OFF; mode 0 = always on, 1 = on hotkey, 2 = toggle) --------------------------------
bool g_bhop = false; // plain on/off, no bind (success rate = g_bhopChance, cfg/u_mvcore.h)
bool g_jumpbug = false;     int g_jumpbugKey = 0;     int g_jumpbugMode = 1;
bool g_jbYieldSurf = true;  // jumpbug hands the tick back while a pixel surf ride is live (kept: routecalc.inl reads it, see globals.h)
// g_jbPredict removed (11-03 tabs round): same analytic gate botox calls "jb only",
// renamed g_jumpbugOnly (cfg/u_mvcore.h) -- see globals.h note.
bool g_autoStrafe = false;  int g_autoStrafeKey = 0;  int g_autoStrafeMode = 1;
bool  g_autoStrafeAvoidWalls = false;
float g_autoStrafeAvoidDist  = 50.f;
bool g_strafeOpt = false;   // plain on/off, no bind
float g_strafeOptGain = 100.f;
float g_strafeOptMinSpeed = 30.f;
bool g_edgeJump = false;    int g_edgeJumpKey = 0;    int g_edgeJumpMode = 1;
bool g_longJump = false;    int g_longJumpKey = 0;    int g_longJumpMode = 1;
bool g_longJumpEdge = false;
bool  g_longJumpAdaptive = false;
// 3 stop-reason bools replaced by g_longJumpAdaptiveStopMask (cfg/u_mvcore.h, def 0x7 = all on,
// matching botox's vector<bool>[3] def true/true/true)
float g_longJumpAdaptiveTime = 0.3f;
bool g_miniJump = false;    int g_miniJumpKey = 0;    int g_miniJumpMode = 1;
bool g_miniJumpHoldDuck = false;
bool g_miniJumpEdge = false;
bool g_fastStop = false;    // plain on/off, no bind
// g_fastAccel / g_noPush removed (11-03 tabs round): no botox csgo equivalent
// wave 2 (ref): edge bug + pixel surf family
bool g_edgeBug = false;     int g_edgeBugKey = 0;     int g_edgeBugMode = 1;
// botox n_edgebug knobs (variables.h m_edgebug_*), same defaults
float g_edgeBugPredictTime = 0.35f;   // search window, seconds
int   g_edgeBugDetectMode = 0;        // 0 normal / 1 enhanced / 2 ebanat
bool  g_edgeBugToEdge = true;         // autostrafe to edge (also the autostrafe switch)
float g_edgeBugStrafeAngleMax = 180.f; // max path bend off course, 180 = off
bool  g_edgeBugMouseSteer = true;
int   g_edgeBugPaths = 3;
int   g_edgeBugLockType = 1;          // botox: smooth / full (donor: static / full)
float g_edgeBugLockStrength = 100.f;
// botox m_edgebug_types bitmask: bit 0 ducked, bit 1 standing, 0 = both (old 0/1/2 read the same)
int   g_edgeBugStance = 0;
bool  g_edgeBugCurStance = false;     // only the stance the duck key is in now (overrides stance)
// donor styles (botox m_edgebug_del_* / m_edgebug_dna_*)
int   g_edgeBugDelTicks = 64;
bool  g_edgeBugDelAdvanced = false;
float g_edgeBugDelAngleLimit = 50.f;
int   g_edgeBugDelSearch = 5;
bool  g_edgeBugDelSilent = false;
bool  g_edgeBugDelMouseFix = true;
float g_edgeBugDnaScan = 0.4f;
bool  g_edgeBugDnaAdvanced = false;
int   g_edgeBugDnaRange = 6;
bool  g_edgeBugDnaCustomAngle = false;
float g_edgeBugDnaAngleLimit = 3.f;
// style 3 hotwheels (botox m_edgebug_hw_*)
int   g_edgeBugHwTicks = 32;
float g_edgeBugHwLockAmt = 0.3f;
bool  g_edgeBugHwAdvanced = false;
float g_edgeBugHwStrafeDeltaMax = 10.f;
bool  g_edgeBugViz = false;         // draw the latched plan (path + landing pad)
float g_edgeBugVizSize = 16.f;      // pad half-width; 16 = the player hull's own footprint
bool g_pixelSurf = false;   int g_pixelSurfKey = 0;   int g_pixelSurfMode = 1;
int  g_pixelSurfPredictTicks = 8;
// g_psWallStrafe / g_psParkAtEnd (TF2-port extras, no botox var) removed 11-03.
// approach aids (both OFF by default -- see the memory note on the guide/speed fix)
bool g_autoAlign = false;   // plain on/off, no bind
bool g_aaHoldSurf = true;   // auto align: one pinned START latches, then hold-press into the face every tick (autoalign.inl)
// -- TB knob set rebuilt off coldtires. The -knobs (autocrouch, seam scan,
// aim frac, engage dist, xy keep, window keeper) went with the machinery they drove; autocrouch
// is SIM-DECIDED now (a duck pass runs only when the standing hull missed everywhere)
bool g_textureBug = false;  int g_textureBugKey = 0;  int g_textureBugMode = 1;
float g_tbReach = 8.f;         // how far out of the hull the wall scan looks (botox m_texture_bug_reach def 8, was 2)
float g_tbScanStep = 1.f;      // z resolution of the scan's line grid
// g_tbRampNz/g_tbHugRange/g_tbSolveForward/g_tbNoOffgridStick/g_tbParkAtEnd/g_tbParkTicks/
// g_tbParkSpeed/g_tbParkBrake/g_tbParkLead/g_tbPredict/g_tbPredictTicks/g_tbPredictReach deleted
// 11-03: TF2-port-only settings, not in botox csgo-sdk's texture bug menu at all. texturebug.inl
// folds each to a fixed constant at its old default (k_tbRampNzFixed etc) -- zero behavior change,
// just no longer a menu/cfg knob. See MVTB report.
bool g_wallClimb = false;   int g_wallClimbKey = 0;   int g_wallClimbMode = 1;
bool g_wcPreventSlow = true;  bool g_wcDuck = true;   // botox wall climb defaults
bool g_wcViz = false;  float g_wcVizColor[4] = { 1.f, 190 / 255.f, 90 / 255.f, 1.f };
bool g_airStuck = false;    int g_airStuckKey = 0;    int g_airStuckMode = 1;
bool g_pixelHop = false;    int g_pixelHopKey = 0;    int g_pixelHopMode = 1;
bool g_phReach = true;      int g_phReachTicks = 10;  float g_phReachAngle = 60.f;

// -- Unit A port (movement_ex.inl): small tricks from botox csgo-sdk, defaults OFF ---------------
bool g_nulls = false;
bool g_hsw = false;        int g_hswKey = 0;        int g_hswMode = 1;
int  g_hswSide = 0;
bool g_hswAllowWS = true;
bool g_hswAllowAD = true;
bool g_hswPreGround = true;
bool g_delayHop = false;   int g_delayHopKey = 0;   int g_delayHopMode = 1;
int  g_delayHopTicks = 6;
bool g_autoCrouch = false; int g_autoCrouchKey = 0; int g_autoCrouchMode = 1;
int  g_autoCrouchTicks = 6;
bool g_silentView = false;
bool g_jumpstats = false;

// -- Unit A3 port (movement_tricks2.inl): bounce/pf modes/eb presets/ps fix ---------
bool g_autoBounce = false; int g_autoBounceKey = 0; int g_autoBounceMode = 1;
// auto bounce = botox auto_bounce.cpp (autobounce.inl), defaults = botox variables.h:1250-1257
int   g_abMode = 0;           // 0 drop in / 1 keep speed / 2 stop
float g_abMinGain = 0.f;
float g_abReach = 128.f;
bool  g_abJump = true;
bool  g_abCrouch = false;
bool  g_abAlign = false;
bool  g_abViz = false;
float g_abVizColor[4] = { 120 / 255.f, 200 / 255.f, 1.f, 1.f };
bool g_psFinderHeadbounce = true;   // botox m_pixel_finder_headbounces default (was false, PSPORT 11-04 fix)
bool g_psFinderPixeljump = true;    // botox m_pixel_finder_pixeljumps default
bool g_psFinderJumpSurf = false;
bool g_psSurfFix = false;
int  g_edgeBugStyle = 0;
bool g_edgeBugMouseLock = false;
// -- Unit A2 port (movement_edge.inl): edge skip + blockbot, defaults OFF ------------------------
bool g_edgeSkip = false;   int g_edgeSkipKey = 0;   int g_edgeSkipMode = 1;
int   g_edgeSkipDepth = 2;
float g_edgeSkipMaxBend = 180.f;
bool g_blockBot = false;   int g_blockBotKey = 0;   int g_blockBotMode = 1;

// fake movement DELETED (11-03/11-04 tabs round): no botox csgo equivalent, see globals.h note
bool g_fmWallbugFix = false;  // 11-11: local server only, CSS x64 ClipVelocity bandaid off (movement.cpp)
bool g_fmNoFallDamage = false; // 11-11: local server only, FlPlayerFallDamage -> 0 (movement.cpp)
float g_asExtraReach = 4.f;  // botox m_air_stuck_extra_reach
float g_asBudget = 50.f;     // botox m_air_stuck_budget (percent of a tick, botox slider 5-90)
bool g_asMovementFix = true;    // botox m_movement_fix (air_stuck.cpp:168 -- off forces view+move onto cmd)
bool g_asScreenMessage = true;  // botox m_air_stuck_display_screen (gates the stuck/drift popup text)
bool g_asFixStucks = false;     // botox m_fix_air_stucks -- consumed by auto_align.cpp:956, not this file (see report NEEDS FROM OTHERS)
// wave 3: pixel surf finder + assist
bool g_psFinder = false;    int g_psFinderKey = 0;    int g_psFinderMode = 1;
float g_psFinderMargin = 8.f;
bool  g_psFinderStrict = true;  // pfstrict: drop teleport-only pins + spots sealed off (clip in front)
bool  g_psFinderTb = true;      // run the texturebug seam pass too (the red rings)
bool  g_psFinderNoProps = true; // props are not map geometry -- finder traces refuse them
bool  g_psFinderLength = true;  // walk found surfs along the wall, draw their length
bool  g_psMapLines = true;      // draw the whole-map scan's lines (own switch, not the finder's)
float g_psMapMaxDist = 0.f;     // map-scan marks farther than this from you not drawn (0 = no limit)
float g_psFinderBudget = 16.f;  // ms per cmd the chunked finder scan may spend (;
                                // raised 2 -> 16: the scan no longer shares the TB clock, so this
                                // is the whole bound. A sim measures ~8.8us in the log, so
                                // a 150-step column is ~300ms of work = ~0.3s at this budget)
bool g_psAssist = false;    int g_psAssistKey = 0;    int g_psAssistMode = 1;
int  g_psSetPointKey = 0;
int  g_psDeletePointKey = 0;
float g_psAssistSnap = 48.f;
// botox m_pixel_surf_assist_render default is false (was true here) -- PSPORT round 2 11-04 fix
bool g_psAssistRender = false;
// g_psAssistRadius/MaxPoints/ReuseDelay/JumpBox/g_psGroundHelpAngle/g_psLaunchZSlack DELETED,
// see globals.h for why
bool  g_psGroundHelp = false;

// -- Route calculator -------------------------------------------------------------
bool g_routeCalc = false;
int  g_rcAddKey = 0;
int  g_rcDeleteKey = 0;
int  g_rcClearKey = 0;
int  g_rcSolveKey = 0;
int  g_rcMoveMask = 0x7F;     // rs::MoveBit: jump mini long jumpbug walk off / crouch walk off (start) delay hop
int  g_rcStartMoves = 0x37;   // botox m_route_calc_start_styles: jump mini long + walk off / crouch walk off
int  g_rcDelayTicks = 2;      // botox m_route_calc_delay_ticks (0-8)
bool g_rcShowKeys = true;     // botox m_route_calc_show_keys
int  g_rcPathAlpha = 255;     // botox m_route_calc_path_alpha
float g_rcPathThick = 2.f;    // botox m_route_calc_path_thickness (1-8 px)
bool g_rcAllowInvalid = false; // botox m_route_calc_allow_invalid
bool g_rcSnapEdge = false;    // botox m_route_calc_snap_edge
bool g_rcAdvanced = false;    // botox m_route_calc_advanced_readout
int  g_rcMaxShown = 8;
bool g_rcStartHere = true;
bool g_rcShowPath = true;
bool g_rcShowPanel = true;
int  g_rcPopupTime = 6;       // botox m_route_calc_popup_time

// -- Pixel calculator , split out of the route calculator --------------------------
bool g_pixelCalc = false;
int  g_pcPlaceKey = 0;
int  g_pcSolveKey = 0;
bool g_pcShowMarker = true;
bool g_pcShowPanel = true;

// -- Practice (practice.inl) -- console commands on a local server, sv_cheats 1 ------
// g_prInfAmmo/g_prInfHealth/g_prInfTime/g_prNoclipKey + TF2 building-placement keys removed
// 11-03 (no botox csgo equivalent); practice window vars now in cfg/u_mvcore.h.
int  g_prSaveKey = 0;
int  g_prTpKey = 0;
int  g_prBotEnemyKey = 0;
int  g_prBotMateKey = 0;
std::atomic<int> g_prMenuReq{0};

// -- Unit C: ESP (src/hacks/aim/esp.cpp, extern list hacks/esp/esp_vars.h) -- botox defaults --
bool  g_espEnable = false;
bool  g_espPreview = false;   // visuals > esp "esp preview" checkbox, src/hacks/visuals/esp_preview.h
bool  g_espTeammates = false;
float g_espMaxDistance = 0.f;
bool  g_espDistanceFade = false;
bool  g_espSoundOnly = false;
float g_espSoundDuration = 1.5f;
bool  g_espSoundFade = true;
bool  g_espBox = false;
bool  g_espBoxVisColors = false;
float g_espBoxVisibleColor[4] = { 1.f, 1.f, 1.f, 1.f };
float g_espBoxInvisibleColor[4] = { 1.f, 0.353f, 0.353f, 1.f };
float g_espBoxColor[4] = { 1.f, 1.f, 1.f, 1.f };
bool  g_espBox3d = false;
bool  g_espBoxCorner = false;
float g_espBoxThickness = 1.f;
float g_espBoxRounding = 0.f;
bool  g_espBoxOutline = false;
float g_espBoxOutlineColor[4] = { 0.f, 0.f, 0.f, 1.f };
float g_espBoxOutlineThickness = 1.f;
int   g_espBoxOutlineSide = 2;
bool  g_espBoxFill = false;
float g_espBoxFillColor[4] = { 0.f, 0.f, 0.f, 0.314f };
bool  g_espBoxFillGradient = false;
float g_espBoxFillBottomColor[4] = { 0.f, 0.f, 0.f, 0.f };
bool  g_espName = false;
int   g_espNamePosition = 0;
bool  g_espNameVisColors = false;
float g_espNameVisibleColor[4] = { 1.f, 1.f, 1.f, 1.f };
float g_espNameInvisibleColor[4] = { 1.f, 0.353f, 0.353f, 1.f };
float g_espNameColor[4] = { 1.f, 1.f, 1.f, 1.f };
bool  g_espHealthBar = false;
int   g_espHealthBarSide = 0;
int   g_espHealthBarCornerPick = 0;
float g_espHealthBarCornerRounding = 6.f;
float g_espHealthBarCornerSize = 0.33f;
float g_espHealthBarThickness = 2.f;
bool  g_espHealthBarOutline = true;
bool  g_espHealthBarCustomColor = false;
float g_espHealthBarColor[4] = { 1.f, 1.f, 1.f, 1.f };
bool  g_espHealthBarGradient = false;
float g_espHealthBarBottomColor[4] = { 1.f, 0.f, 0.f, 1.f };
float g_espHealthBarBgColor[4] = { 0.f, 0.f, 0.f, 1.f };
bool  g_espHealthText = false;
bool  g_espHealthSuffix = false;
int   g_espHealthTextStyle = 0;
bool  g_espHealthTextCustomColor = false;
float g_espHealthTextColor[4] = { 1.f, 1.f, 1.f, 1.f };
bool  g_espWeaponName = false;
float g_espWeaponNameColor[4] = { 1.f, 1.f, 1.f, 1.f };
bool  g_espAmmoBar = false;
float g_espAmmoBarColor[4] = { 1.f, 1.f, 1.f, 1.f };
bool  g_espDistance = false;
int   g_espDistanceUnit = 0;
float g_espDistanceColor[4] = { 1.f, 1.f, 1.f, 1.f };
bool  g_espFlags = false;
int   g_espFlagList = 0x1F;   // uber, crits, cloaked, disguised, zoomed: what the old [uber][cloak][spy] row covered
float g_espFlagsColor[4] = { 1.f, 1.f, 1.f, 1.f };
float g_espFlagsAlertColor[4] = { 1.f, 0.353f, 0.353f, 1.f };
bool  g_espSkeleton = false;
bool  g_espSkeletonVisColors = false;
float g_espSkeletonVisibleColor[4] = { 1.f, 1.f, 1.f, 1.f };
float g_espSkeletonInvisibleColor[4] = { 1.f, 0.353f, 0.353f, 1.f };
float g_espSkeletonColor[4] = { 1.f, 1.f, 1.f, 1.f };
int   g_espSkeletonType = 0;
float g_espSkeletonThickness = 1.f;
bool  g_espHitboxes = false;
int   g_espHitboxesMode = 0;
float g_espHitboxColor[4] = { 1.f, 1.f, 1.f, 0.588f };
float g_espHitboxesThickness = 1.f;
float g_espHitboxesDuration = 2.f;
bool  g_espAvatar = false;
int   g_espAvatarPosition = 1;
float g_espAvatarSize = 14.f;
float g_espAvatarHeadScale = 1.f;
bool  g_espDamageNumbers = false;
float g_espDmgColor[4] = { 1.f, 1.f, 1.f, 1.f };
float g_espDmgDuration = 1.5f;
float g_espDmgSpeed = 20.f;
bool  g_espDmgStack = true;
bool  g_espOofArrows = false;
int   g_espOofType = 0;
float g_espOofColor[4] = { 1.f, 1.f, 1.f, 1.f };
float g_espOofWallColor[4] = { 1.f, 0.353f, 0.353f, 1.f };
float g_espOofSoundColor[4] = { 1.f, 0.627f, 0.f, 1.f };
float g_espOofSoundDuration = 1.5f;
bool  g_espOofSoundFade = true;
int   g_espOofStyle = 0;
float g_espOofWidth = 18.f;
float g_espOofHeight = 22.f;
int   g_espOofDistance = 200;
bool  g_espOofAvatar = false;
bool  g_espOofAspect = false;
bool  g_espSoundPings = false;
float g_espSoundColor[4] = { 1.f, 0.9f, 0.47f, 0.86f };
// planted C4, botox variables.h m_bomb_esp* / m_bomb_timer_bar*
bool  g_espC4 = false;
bool  g_espC4Icon = true;
bool  g_espC4Timer = true;
float g_espC4Color[4]       = { 1.f, 1.f, 1.f, 1.f };
float g_espC4DefuseColor[4] = { 90.f / 255.f, 1.f, 130.f / 255.f, 1.f };
float g_espC4FailColor[4]   = { 1.f, 90.f / 255.f, 90.f / 255.f, 1.f };
bool  g_espC4Bar = false;
int   g_espC4BarPosition = 60;
bool  g_espWorldEnable = false;
bool  g_espAmmoPacks = true;
bool  g_espDroppedWeapons = true;
bool  g_espProjectiles = true;
bool  g_espProjPath = false;
float g_espProjPathColor[4] = { 1.f, 0.9f, 0.3f, 1.f };
float g_espItemColor[4] = { 1.f, 0.9f, 0.47f, 1.f };
float g_espProjColor[4] = { 1.f, 1.f, 1.f, 0.78f };

// -- Unit C2: chams (src/hacks/visuals/visuals.cpp) -- defaults OFF ------------------------------
bool  g_chamsEnable = false;
bool  g_chamsEnemy = true;
bool  g_chamsTeam = false;
float g_chamsEnemyColor[4] = { 1.f, 0.15f, 0.1f, 1.f };
float g_chamsTeamColor[4]  = { 0.1f, 0.5f, 1.f, 1.f };

// -- Unit C3: glow (src/hacks/visuals/visuals.cpp) -- defaults OFF --------------------------------
bool  g_glowEnable = false;
bool  g_glowEnemy = true;
bool  g_glowTeam = false;
float g_glowEnemyColor[4] = { 1.f, 0.05f, 0.05f, 0.6f };
float g_glowTeamColor[4]  = { 0.05f, 0.6f, 1.f, 0.6f };
bool  g_glowBuildings = false;
float g_glowBuildingColor[4] = { 1.f, 0.8f, 0.05f, 0.6f };
float g_glowAlpha = 1.f;

// -- 10-31 chams/glow rework (src/hacks/aim/chams.cpp), botox variables.h defaults --------
// Layer lists are config strings "mat:RRGGBBAA,..." (src/hacks/visuals/chams_layers.h). g_chamsTeam
// above is now the teammates group's own enable.
bool  g_chamsLocalEnable = true;
bool  g_chamsRagdolls = false;
float g_chamsSoundDuration = 1.f;
bool  g_chamsSoundFade = true;
float g_chamsSoundMinVolume = 0.f;
float g_chamsSoundMaxDistance = 0.f;
float g_chamsTwoToneThreshold = 0.5f;   // botox variables.h:604
float g_chamsTwoToneEdge = 0.f;         // botox variables.h:605
char  g_chamsVisibleCfg[160] = "";
char  g_chamsOccludedCfg[160] = "";
char  g_chamsSoundCfg[160] = "";
char  g_chamsTeamVisibleCfg[160] = "";
char  g_chamsTeamOccludedCfg[160] = "";
char  g_chamsWeaponCfg[160] = "";
char  g_chamsUberCfg[160] = "";        // enemy: replaces the visible list while ubered/bonked
char  g_chamsCloakCfg[160] = "";       // enemy: replaces the visible list while cloaked
char  g_chamsTeamUberCfg[160] = "";    // team: same pair for teammates
char  g_chamsTeamCloakCfg[160] = "";

float g_glowVisColor[4]        = { 224 / 255.f, 175 / 255.f, 86 / 255.f, 153 / 255.f };
float g_glowInvisColor[4]      = { 114 / 255.f, 155 / 255.f, 221 / 255.f, 153 / 255.f };
bool  g_glowLegacy = false;
int   g_glowLegacyStyle = 0;
bool  g_glowNormalBlend = false;
float g_glowThickness = 15.f;
bool  g_glowGradient = false;
float g_glowVisOuterColor[4]   = { 0.f, 0.f, 0.f, 1.f };
float g_glowInvisOuterColor[4] = { 0.f, 0.f, 0.f, 1.f };
float g_glowGradientInner = 60.f;
bool  g_glowWave = false;
float g_glowWaveColor[4]       = { 1.f, 1.f, 1.f, 1.f };
float g_glowWaveSpeed = 0.5f;
float g_glowWaveSize = 0.35f;
float g_glowWaveStrength = 0.8f;
bool  g_glowPulse = false;
float g_glowPulseSpeed = 1.f;
float g_glowPulseMin = 35.f;

// -- Skybox --------------------------------------------------------------------------------
// Only the ConVar handle is shared (thread.cpp resolves it). The sky-name and fog-colour
// redirect buffers are file-local to visuals.cpp now -- they are that file's scratch, not
// settings, and having them here is what let the g_offMins double definition happen.
ConVar* g_cv_sv_skyname = nullptr;

// -- Weather entity state lives in visuals.cpp (file-local). g_offMins / g_offMaxs have ONE
// owner, netvars.cpp: defining them here too made two COMDATs of the same symbol that /Gw
// folded silently, so which initialiser survived (-1 vs 0) was the linker's pick.

// -- Net stats -----------------------------------------------------------------------------------------------------------
std::atomic<float> g_fps  {0.f};
std::atomic<int>   g_ping {0};
std::atomic<int>   g_loss {0};
std::atomic<bool>  g_conn {false};

// -- WndProc -------------------------------------------------------------------------------------------------------------------
WNDPROC g_origWndProc = nullptr;

// ==== ICvar::ConsoleColorPrintf (slot 23) =====================================================
struct ConsoleColor4 { uint8_t r, g, b, a; };
using ConsoleColorPrintf_t = void(__fastcall*)(void*, const ConsoleColor4*, const char*, ...);

void ConsoleColorPrint(uint8_t r, uint8_t g, uint8_t b, const char* msg)
{
    if (!g_cvar) return;
    __try {
        auto fn = reinterpret_cast<ConsoleColorPrintf_t>((*reinterpret_cast<void***>(g_cvar))[23]);
        ConsoleColor4 col{ r, g, b, 255 };
        fn(g_cvar, &col, "%s", msg);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// ==== Debug log (persistent handle -- avoids Defender rescan stalls) =========================
// Lifted out of DebugLog's body so DebugLog_Shutdown below can reach the same object. Still a
// function-local static: the C++11 magic-static guard is what makes first use thread-safe, and
// it must NOT become a namespace-scope object (static init order vs the first log line).
namespace {
struct LogState {
    HANDLE           h;
    CRITICAL_SECTION cs;
    LogState() {
        InitializeCriticalSection(&cs);
        _mkdir("C:\\botox-css");
        h = CreateFileA("C:\\botox-css\\debug.log", GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
};
LogState& LogGet() { static LogState s; return s; }

// Early-line buffer. Every install diag runs in PollThread, seconds before anyone can reach
// the menu toggle, so with the log off they went nowhere -- which is why scoreboard and
// lagres each grew a bespoke "replay my one line later" hack. This is that hack once, for
// every call site. CAPPED on purpose: the per-tick movement/TB diags would otherwise pay a
// format on every call forever. It fills, and the off-path is two bool tests again.
constexpr int kEarlyLines = 96;
constexpr int kEarlyLen   = 512;
char s_early[kEarlyLines][kEarlyLen];
int  s_earlyCount   = 0;
bool s_earlyFlushed = false;
}

void DebugLog(const char* fmt, ...)
{
    // Master gate, default off. Every call site stays compiled -- the per-tick movement / TB /
    // PS diags are still the only way to debug those features, and they come back the moment
    // the menu toggle flips. Off, this costs two bool tests and no formatting.
    const bool on = g_debugLogEnable;
    if (!on && (s_earlyFlushed || s_earlyCount >= kEarlyLines)) return;

    // DO NOT SHRINK THIS BUFFER. : at 512 the diag stopped mid-field at `dface=` and all
    // 1118 TB lines came out exactly 510 chars, so the six numbers that build shipped to measure
    // never reached the file -- an instrument that cannot answer is worse than none, because the
    // missing line reads as a fact. : the BUILD tag was over the ceiling for several builds
    // (~7800 chars) and every one landed cut off mid-sentence. One stack buffer, no hot path.
    char buf[8192];
    va_list ap; va_start(ap, fmt);
    int n = _vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    // -1 means it did not fit. `if (n <= 0) return` DROPPED those lines silently, and exactly
    // the longest ones -- the movement diags. Truncate and log instead of losing it.
    if (n < 0) n = (int)sizeof(buf) - 2;
    if (n == 0) return;
    buf[n]   = '\n';
    buf[n+1] = '\0';

    LogState& s_log = LogGet();
    if (s_log.h == INVALID_HANDLE_VALUE) return;

    EnterCriticalSection(&s_log.cs);
    if (!on) {
        // log is off: park the line for DebugLog_FlushEarly. Re-tested under the lock, another
        // thread can have filled the last slot between the gate above and here.
        if (!s_earlyFlushed && s_earlyCount < kEarlyLines) {
            int c = (n + 1 < kEarlyLen) ? n + 1 : kEarlyLen - 1;   // n+1 keeps the newline
            memcpy(s_early[s_earlyCount], buf, (size_t)c);
            s_early[s_earlyCount][c] = '\0';
            ++s_earlyCount;
        }
    }
    // Re-tested INSIDE the lock: DebugLog_Shutdown closes the handle under this same lock, so
    // the check above can be stale by the time we get here.
    else if (s_log.h != INVALID_HANDLE_VALUE) {
        DWORD w = 0;
        WriteFile(s_log.h, buf, (DWORD)(n + 1), &w, nullptr);
    }
    LeaveCriticalSection(&s_log.cs);
}

// Called the moment the menu toggle goes on. Dumps everything that happened while the log was
// off -- which is every hook install, every sig that did NOT resolve, and the whole startup
// order. Once is enough: after this the buffer stays closed and the off-path goes cheap again.
void DebugLog_FlushEarly()
{
    if (!g_debugLogEnable) return;

    LogState& s_log = LogGet();
    EnterCriticalSection(&s_log.cs);
    if (!s_earlyFlushed && s_log.h != INVALID_HANDLE_VALUE) {
        s_earlyFlushed = true;
        static const char hdr[] = "---- replay: logged before the log was on ----\n";
        DWORD w = 0;
        WriteFile(s_log.h, hdr, (DWORD)(sizeof(hdr) - 1), &w, nullptr);
        for (int i = 0; i < s_earlyCount; ++i)
            WriteFile(s_log.h, s_early[i], (DWORD)strlen(s_early[i]), &w, nullptr);
        static const char end[] = "---- replay end ----\n";
        WriteFile(s_log.h, end, (DWORD)(sizeof(end) - 1), &w, nullptr);
    }
    LeaveCriticalSection(&s_log.cs);
}

// The handle used to live for the life of the process. Under a soft eject that is a handle
// held on a file the user is about to read, for no reason. Closing it makes every later
// DebugLog a no-op, which is what we want once the hooks are down.
//
// The CRITICAL_SECTION is NOT deleted and must not be: a hook that slips past the drain and
// logs one last line would then enter a deleted lock, which is a worse fault than the ~40
// bytes it holds. The object is a function-local static, so it outlives everything anyway.
void DebugLog_Shutdown()
{
    LogState& s_log = LogGet();
    EnterCriticalSection(&s_log.cs);
    if (s_log.h != INVALID_HANDLE_VALUE) { CloseHandle(s_log.h); s_log.h = INVALID_HANDLE_VALUE; }
    LeaveCriticalSection(&s_log.cs);
}
