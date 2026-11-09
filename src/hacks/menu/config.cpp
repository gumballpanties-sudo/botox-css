#include "config.h"
#include "../hud/indicators.h"   // botox indicators vars (rows in cfg/u_ind.inc)
#include "../screen/colorcorrect.h"   // CcResetCurve -- a curve that fails to parse goes back to identity
#include "../aim/aim.h"   // the aimbot's vars ride in this same file
#include "../visuals/esp_preview.h"   // g_espPreview
#include "../misc/names.h"        // and custom names' cycle lists + aliases
#include "../movement/detections.h"    // unit B
#include "../movement/ps_lines.h"      // unit B
#include "../movement/recorder.h"      // unit B
#include "../movement/movement.h"      // ladder / fireman vars
#include "cfg/u_movx.h"    // unit movx vars (air freeze, ps wall strafe, edgebug lob)
#include "../screen/resolution_spoof.h"  // resolution spoofer cfg (cfg/u_screen.inc)
#include "../web/websurface.h"           // web on surface cfg
#include "../botoxnet/botoxnet.h"        // botox network cfg
#include "fonts.h"         // fonts tab
#include "visual_keys.h"   // settings tab "visuals config"
#include <cfloat>      // FLT_MAX -- the "this end was never bounded" sentinel below
#include <algorithm>   // std::sort -- ListConfigs

// Tiny key=value config. One entry table drives save + load.

struct CfgEntry {
    const char* key;
    enum { BOOL, INT, FLOAT, FLOAT4, STR, CURVE } type;
    void*  ptr;
    size_t size; // STR only
};

// CURVE: "n;x,y;x,y;..." -- a colour correction curve, whose point count varies. Anything
// malformed, out of order or out of range is refused whole and the curve resets to the
// identity diagonal: a half-parsed curve would be baked straight into the lookup table.
static bool ParseCurve(const char* val, CcCurve& out)
{
    const int n = atoi(val);
    if (n < 2 || n > k_ccMaxPts) return false;

    CcCurve c{};
    c.n = n;
    const char* p = val;
    for (int i = 0; i < n; ++i)
    {
        p = strchr(p, ';');
        if (!p) return false;
        ++p;
        if (sscanf(p, "%f,%f", &c.x[i], &c.y[i]) != 2) return false;
        if (c.y[i] < 0.f) c.y[i] = 0.f;
        if (c.y[i] > 1.f) c.y[i] = 1.f;
        if (i > 0 && !(c.x[i] > c.x[i - 1])) return false;   // must be strictly increasing
    }
    c.x[0] = 0.f;          // the endpoints are pinned; only their height is the user's
    c.x[n - 1] = 1.f;
    if (n > 2 && !(c.x[1] > 0.f && c.x[n - 2] < 1.f)) return false;

    out = c;
    return true;
}

static const CfgEntry kEntries[] = {
    { "menuKey",          CfgEntry::INT,    &g_menuKey, 0 },

    { "wmEnable",         CfgEntry::BOOL,   &g_wmEnable, 0 },
    { "wmShowLabel",      CfgEntry::BOOL,   &g_wmShowLabel, 0 },
    { "wmLabel",          CfgEntry::STR,    g_wmLabel, sizeof(g_wmLabel) },
    { "wmShowFPS",        CfgEntry::BOOL,   &g_wmShowFPS, 0 },
    { "wmShowPing",       CfgEntry::BOOL,   &g_wmShowPing, 0 },
    { "wmShowLoss",       CfgEntry::BOOL,   &g_wmShowLoss, 0 },
    { "wmShowTime",       CfgEntry::BOOL,   &g_wmShowTime, 0 },
    { "wmPos",            CfgEntry::INT,    &g_wmPos, 0 },
    { "wmTextColor",      CfgEntry::FLOAT4, g_wmTextColor, 0 },
    { "mpEnable",         CfgEntry::BOOL,   &g_mpEnable, 0 },
    { "mpCoverArt",       CfgEntry::BOOL,   &g_mpCoverArt, 0 },
    { "mpBackground",     CfgEntry::BOOL,   &g_mpBackground, 0 },
    { "mpProgressBar",    CfgEntry::BOOL,   &g_mpProgressBar, 0 },
    { "mpSimple",         CfgEntry::BOOL,   &g_mpSimple, 0 },
    { "mpLumi",           CfgEntry::BOOL,   &g_mpLumi, 0 },
    { "mpTime",           CfgEntry::BOOL,   &g_mpTime, 0 },
    { "mpPos",            CfgEntry::INT,    &g_mpPos, 0 },

    { "hmEnable",         CfgEntry::BOOL,   &g_hmEnable, 0 },
    { "hmColor",          CfgEntry::FLOAT4, g_hmColor, 0 },
    { "hmSize",           CfgEntry::FLOAT,  &g_hmSize, 0 },

    { "dpEnable",         CfgEntry::BOOL,   &g_dpEnable, 0 },
    { "dpStyle",          CfgEntry::INT,    &g_dpStyle, 0 },
    { "dpColor",          CfgEntry::FLOAT4, g_dpColor, 0 },
    { "dpMulticolor",     CfgEntry::BOOL,   &g_dpMulticolor, 0 },
    { "dpAmount",         CfgEntry::INT,    &g_dpAmount, 0 },
    { "dpSize",           CfgEntry::FLOAT,  &g_dpSize, 0 },
    { "dpLifetime",       CfgEntry::FLOAT,  &g_dpLifetime, 0 },
    { "dpSoulStyle",      CfgEntry::INT,    &g_dpSoulStyle, 0 },
    { "dpMcMode",         CfgEntry::INT,    &g_dpMcMode, 0 },
    { "dpMcXpTime",       CfgEntry::FLOAT,  &g_dpMcXpTime, 0 },

    { "ptsEnable",        CfgEntry::BOOL,   &g_ptsEnable, 0 },

    { "specEnable",       CfgEntry::BOOL,   &g_specEnable, 0 },
    { "specAlwaysShow",   CfgEntry::BOOL,   &g_specAlwaysShow, 0 },
    { "specStyle",        CfgEntry::INT,    &g_specStyle, 0 },
    { "specPos",          CfgEntry::INT,    &g_specPos, 0 },
    { "specOffX",         CfgEntry::FLOAT,  &g_specOffX, 0 },
    { "specOffY",         CfgEntry::FLOAT,  &g_specOffY, 0 },
    { "specShowTarget",   CfgEntry::BOOL,   &g_specShowTarget, 0 },
    { "specDelusionalLeft", CfgEntry::BOOL, &g_specDelusionalLeft, 0 },
    { "specListWidth",    CfgEntry::INT,    &g_specListWidth, 0 },
    { "specColorOne",     CfgEntry::FLOAT4, g_specColorOne, 0 },
    { "specColorTwo",     CfgEntry::FLOAT4, g_specColorTwo, 0 },

    { "wmStyle",          CfgEntry::INT,    &g_wmStyle, 0 },

    { "discordEnable",    CfgEntry::BOOL,   &g_discordEnable, 0 },
    { "discordAppId",     CfgEntry::STR,    g_discordAppId, sizeof(g_discordAppId) },
    { "discordState",     CfgEntry::STR,    g_discordState, sizeof(g_discordState) },
    { "discordDetails",   CfgEntry::STR,    g_discordDetails, sizeof(g_discordDetails) },
    { "discordImageKey",  CfgEntry::STR,    g_discordImageKey, sizeof(g_discordImageKey) },

    { "consoleColor",     CfgEntry::FLOAT4, g_consoleColor, 0 },

    { "vmSwayEnable",     CfgEntry::BOOL,   &g_vmSwayEnable, 0 },
    { "vmSwayScale",      CfgEntry::FLOAT,  &g_vmSwayScale, 0 },
    { "vmSwayInterp",     CfgEntry::FLOAT,  &g_vmSwayInterp, 0 },
    { "vmOffsetEnable",   CfgEntry::BOOL,   &g_vmOffsetEnable, 0 },
    { "vmOffsetX",        CfgEntry::FLOAT,  &g_vmOffsetX, 0 },
    { "vmOffsetY",        CfgEntry::FLOAT,  &g_vmOffsetY, 0 },
    { "vmOffsetZ",        CfgEntry::FLOAT,  &g_vmOffsetZ, 0 },
    { "vmRemoveBob",      CfgEntry::BOOL,   &g_vmRemoveBob, 0 },
    { "vmCsBob",          CfgEntry::BOOL,   &g_vmCsBob, 0 },
    { "vmCsBobCycle",     CfgEntry::FLOAT,  &g_vmCsBobCycle, 0 },
    { "vmCsBobUp",        CfgEntry::FLOAT,  &g_vmCsBobUp, 0 },
    { "vmCsBobVert",      CfgEntry::FLOAT,  &g_vmCsBobVert, 0 },
    { "vmCsBobLat",       CfgEntry::FLOAT,  &g_vmCsBobLat, 0 },
    { "vmCsBobLower",     CfgEntry::FLOAT,  &g_vmCsBobLower, 0 },
    { "vmCsGunLower",     CfgEntry::BOOL,   &g_vmCsGunLower, 0 },
    { "vmCsGunLowerAngle",CfgEntry::FLOAT,  &g_vmCsGunLowerAngle, 0 },
    { "vmCsGunLowerSpeed",CfgEntry::FLOAT,  &g_vmCsGunLowerSpeed, 0 },
    { "vmCsLandDip",      CfgEntry::BOOL,   &g_vmCsLandDip, 0 },
    { "vmCsLandDipAmt",   CfgEntry::FLOAT,  &g_vmCsLandDipAmt, 0 },
    { "vmCsLandDipTime",  CfgEntry::FLOAT,  &g_vmCsLandDipTime, 0 },
    { "vmAlphaEnable",    CfgEntry::BOOL,   &g_vmAlphaEnable, 0 },
    { "vmAlpha",          CfgEntry::FLOAT,  &g_vmAlpha, 0 },
    { "vmFlatAlpha",      CfgEntry::BOOL,   &g_vmFlatAlpha, 0 },
    { "vmLowFps",         CfgEntry::BOOL,   &g_vmLowFps, 0 },
    { "vmLowFpsRate",     CfgEntry::FLOAT,  &g_vmLowFpsRate, 0 },

    { "debugLog",         CfgEntry::BOOL,   &g_debugLogEnable, 0 },
    { "menuStyle",        CfgEntry::INT,    &g_menuStyle, 0 },
    { "accentColor",      CfgEntry::FLOAT4, g_accentColor, 0 },
    { "dpiScale",         CfgEntry::FLOAT,  &g_dpiScale, 0 },
    { "dpiScalePanels",   CfgEntry::BOOL,   &g_dpiScalePanels, 0 },
    { "safeMode",         CfgEntry::BOOL,   &g_safeMode, 0 },

    { "fogEnable",        CfgEntry::BOOL,   &g_fogEnable, 0 },
    { "fogColor",         CfgEntry::FLOAT4, g_fogColor, 0 },
    { "fogStart",         CfgEntry::FLOAT,  &g_fogStart, 0 },
    { "fogEnd",           CfgEntry::FLOAT,  &g_fogEnd, 0 },
    { "fogDensity",       CfgEntry::FLOAT,  &g_fogDensity, 0 },

    { "fxDepthProbe",     CfgEntry::BOOL,   &g_fxDepthProbe, 0 },
    { "fxForceQueueMode", CfgEntry::BOOL,   &g_fxForceQueueMode, 0 },
    { "fxUseCallQueue",   CfgEntry::BOOL,   &g_fxUseCallQueue, 0 },

    { "aoEnable",         CfgEntry::BOOL,   &g_aoEnable, 0 },
    { "aoIntensity",      CfgEntry::FLOAT,  &g_aoIntensity, 0 },
    { "aoRadius",         CfgEntry::FLOAT,  &g_aoRadius, 0 },
    { "aoSamples",        CfgEntry::INT,    &g_aoSamples, 0 },
    { "aoBias",           CfgEntry::FLOAT,  &g_aoBias, 0 },
    { "aoBlur",           CfgEntry::FLOAT,  &g_aoBlur, 0 },
    { "aoScale",          CfgEntry::FLOAT,  &g_aoScale, 0 },
    { "aoFadeStart",      CfgEntry::FLOAT,  &g_aoFadeStart, 0 },
    { "aoFadeEnd",        CfgEntry::FLOAT,  &g_aoFadeEnd, 0 },
    { "aoViewmodel",      CfgEntry::BOOL,   &g_aoViewmodel, 0 },
    { "aoFogFade",        CfgEntry::BOOL,   &g_aoFogFade, 0 },
    { "aoDebug",          CfgEntry::BOOL,   &g_aoDebug, 0 },

    { "dofEnable",        CfgEntry::BOOL,   &g_dofEnable, 0 },
    { "dofAutoFocus",     CfgEntry::BOOL,   &g_dofAutoFocus, 0 },
    { "dofDistance",      CfgEntry::FLOAT,  &g_dofDistance, 0 },
    { "dofFocusSpeed",    CfgEntry::FLOAT,  &g_dofFocusSpeed, 0 },
    { "dofFocusLimit",    CfgEntry::FLOAT,  &g_dofFocusLimit, 0 },
    { "dofRange",         CfgEntry::FLOAT,  &g_dofRange, 0 },
    { "dofRamp",          CfgEntry::FLOAT,  &g_dofRamp, 0 },
    { "dofMaxBlur",       CfgEntry::FLOAT,  &g_dofMaxBlur, 0 },
    { "dofTapDensity",    CfgEntry::FLOAT,  &g_dofTapDensity, 0 },
    { "dofBokehBoost",    CfgEntry::FLOAT,  &g_dofBokehBoost, 0 },
    { "dofBokehThresh",   CfgEntry::FLOAT,  &g_dofBokehThreshold, 0 },
    { "dofNearBlur",      CfgEntry::BOOL,   &g_dofNearBlur, 0 },
    { "dofViewmodelSharp",CfgEntry::BOOL,   &g_dofViewmodelSharp, 0 },
    { "fxParticleGuard",  CfgEntry::FLOAT,  &g_fxParticleGuard, 0 },
    { "dofSmooth",        CfgEntry::BOOL,   &g_dofSmooth, 0 },

    { "fbEnable",         CfgEntry::BOOL,   &g_fbEnable, 0 },
    { "fbShutter",        CfgEntry::FLOAT,  &g_fbShutter, 0 },
    { "fbAmount",         CfgEntry::FLOAT,  &g_fbAmount, 0 },
    { "fbPlayers",        CfgEntry::BOOL,   &g_fbPlayers, 0 },
    { "fbViewmodel",      CfgEntry::BOOL,   &g_fbViewmodel, 0 },
    { "fbVmAmount",       CfgEntry::FLOAT,  &g_fbVmAmount, 0 },
    { "fbSamples",        CfgEntry::INT,    &g_fbSamples, 0 },
    { "fbMaxStreak",      CfgEntry::FLOAT,  &g_fbMaxStreak, 0 },

    { "bloomEnable",      CfgEntry::BOOL,   &g_bloomEnable, 0 },
    { "bloomIntensity",   CfgEntry::FLOAT,  &g_bloomIntensity, 0 },
    { "bloomThreshold",   CfgEntry::FLOAT,  &g_bloomThreshold, 0 },
    { "bloomRadius",      CfgEntry::FLOAT,  &g_bloomRadius, 0 },
    { "bloomSat",         CfgEntry::FLOAT,  &g_bloomSat, 0 },

    { "ccEnable",         CfgEntry::BOOL,   &g_ccEnable, 0 },
    { "ccGainR",          CfgEntry::FLOAT,  &g_ccGainR, 0 },
    { "ccGainG",          CfgEntry::FLOAT,  &g_ccGainG, 0 },
    { "ccGainB",          CfgEntry::FLOAT,  &g_ccGainB, 0 },
    { "ccExposure",       CfgEntry::FLOAT,  &g_ccExposure, 0 },
    { "ccTemp",           CfgEntry::FLOAT,  &g_ccTemp, 0 },
    { "ccHue",            CfgEntry::FLOAT,  &g_ccHue, 0 },
    { "ccSat",            CfgEntry::FLOAT,  &g_ccSat, 0 },
    { "ccContrast",       CfgEntry::FLOAT,  &g_ccContrast, 0 },
    { "ccLevInBlack",     CfgEntry::FLOAT,  &g_ccLevInBlack, 0 },
    { "ccLevInWhite",     CfgEntry::FLOAT,  &g_ccLevInWhite, 0 },
    { "ccGamma",          CfgEntry::FLOAT,  &g_ccGamma, 0 },
    { "ccLevOutBlack",    CfgEntry::FLOAT,  &g_ccLevOutBlack, 0 },
    { "ccLevOutWhite",    CfgEntry::FLOAT,  &g_ccLevOutWhite, 0 },
    { "ccHighlights",     CfgEntry::FLOAT,  &g_ccHighlights, 0 },
    { "ccShadows",        CfgEntry::FLOAT,  &g_ccShadows, 0 },
    { "ccCurveM",         CfgEntry::CURVE,  &g_ccCurve[0], 0 },
    { "ccCurveR",         CfgEntry::CURVE,  &g_ccCurve[1], 0 },
    { "ccCurveG",         CfgEntry::CURVE,  &g_ccCurve[2], 0 },
    { "ccCurveB",         CfgEntry::CURVE,  &g_ccCurve[3], 0 },

    // -- Unit D: simple screen post-fx --------------------------------------------------
    { "sharpenEnable",     CfgEntry::BOOL,   &g_sharpenEnable, 0 },
    { "sharpenAmount",     CfgEntry::FLOAT,  &g_sharpenAmount, 0 },
    { "filmGrainEnable",   CfgEntry::BOOL,   &g_filmGrainEnable, 0 },
    { "filmGrainAmount",   CfgEntry::FLOAT,  &g_filmGrainAmount, 0 },
    { "vignetteEnable",    CfgEntry::BOOL,   &g_vignetteEnable, 0 },
    { "vignetteAmount",    CfgEntry::FLOAT,  &g_vignetteAmount, 0 },
    { "vignetteInvert",    CfgEntry::BOOL,   &g_vignetteInvert, 0 },
    { "invertFilterEnable",CfgEntry::BOOL,   &g_invertFilterEnable, 0 },
    { "posterizeEnable",   CfgEntry::BOOL,   &g_posterizeEnable, 0 },
    { "posterizeLevels",   CfgEntry::FLOAT,  &g_posterizeLevels, 0 },
    { "debandEnable",      CfgEntry::BOOL,   &g_debandEnable, 0 },
    { "debandAmount",      CfgEntry::FLOAT,  &g_debandAmount, 0 },
    { "channelShiftEnable",CfgEntry::BOOL,   &g_channelShiftEnable, 0 },
    { "channelShiftAmount",CfgEntry::FLOAT,  &g_channelShiftAmount, 0 },
    { "channelShiftDepth",     CfgEntry::BOOL,  &g_channelShiftDepth, 0 },
    { "channelShiftDepthMode", CfgEntry::INT,   &g_channelShiftDepthMode, 0 },
    { "channelShiftDepthDist", CfgEntry::FLOAT, &g_channelShiftDepthDist, 0 },
    { "channelShiftDepthRange",CfgEntry::FLOAT, &g_channelShiftDepthRange, 0 },
    { "channelShiftDepthRamp", CfgEntry::FLOAT, &g_channelShiftDepthRamp, 0 },
    { "warpEnable",          CfgEntry::BOOL,   &g_warpEnable, 0 },
    { "warpEffect",          CfgEntry::INT,    &g_warpEffect, 0 },
    { "warpAnimate",         CfgEntry::INT,    &g_warpAnimate, 0 },
    { "warpAnimSpeed",       CfgEntry::FLOAT,  &g_warpAnimSpeed, 0 },
    { "warpCenterX",         CfgEntry::FLOAT,  &g_warpCenter[0], 0 },
    { "warpCenterY",         CfgEntry::FLOAT,  &g_warpCenter[1], 0 },
    { "warpUseOffset",       CfgEntry::BOOL,   &g_warpUseOffset, 0 },
    { "warpOffsetX",         CfgEntry::FLOAT,  &g_warpOffset[0], 0 },
    { "warpOffsetY",         CfgEntry::FLOAT,  &g_warpOffset[1], 0 },
    { "warpRadius",          CfgEntry::FLOAT,  &g_warpRadius, 0 },
    { "warpTension",         CfgEntry::FLOAT,  &g_warpTension, 0 },
    { "warpMagnitude",       CfgEntry::FLOAT,  &g_warpMagnitude, 0 },
    { "warpAngle",           CfgEntry::FLOAT,  &g_warpAngle, 0 },
    { "warpInverse",         CfgEntry::BOOL,   &g_warpInverse, 0 },
    { "warpInnerRadius",     CfgEntry::FLOAT,  &g_warpInnerRadius, 0 },
    { "warpSplices",         CfgEntry::INT,    &g_warpSplices, 0 },
    { "warpAmplitude",       CfgEntry::FLOAT,  &g_warpAmplitude, 0 },
    { "warpPeriod",          CfgEntry::FLOAT,  &g_warpPeriod, 0 },
    { "warpPhase",           CfgEntry::FLOAT,  &g_warpPhase, 0 },
    { "warpWaveType",        CfgEntry::INT,    &g_warpWaveType, 0 },
    { "warpWaveAngle",       CfgEntry::FLOAT,  &g_warpWaveAngle, 0 },
    { "warpWavePeriod",      CfgEntry::FLOAT,  &g_warpWavePeriod, 0 },
    { "warpWaveAmplitude",   CfgEntry::FLOAT,  &g_warpWaveAmplitude, 0 },
    { "warpDrunkStrengthX",  CfgEntry::FLOAT,  &g_warpDrunkStrength[0], 0 },
    { "warpDrunkStrengthY",  CfgEntry::FLOAT,  &g_warpDrunkStrength[1], 0 },
    { "warpDrunkSpeedX",     CfgEntry::FLOAT,  &g_warpDrunkSpeed[0], 0 },
    { "warpDrunkSpeedY",     CfgEntry::FLOAT,  &g_warpDrunkSpeed[1], 0 },
    { "warpDrunkAngle",      CfgEntry::FLOAT,  &g_warpDrunkAngle, 0 },
    { "warpDrunkAngleSpeed", CfgEntry::FLOAT,  &g_warpDrunkAngleSpeed, 0 },
    { "warpPlanetRotX",      CfgEntry::FLOAT,  &g_warpPlanetRot[0], 0 },
    { "warpPlanetRotY",      CfgEntry::FLOAT,  &g_warpPlanetRot[1], 0 },
    { "warpPlanetRotZ",      CfgEntry::FLOAT,  &g_warpPlanetRot[2], 0 },
    { "warpPlanetScale",     CfgEntry::FLOAT,  &g_warpPlanetScale, 0 },
    { "warpPlanetOffsetX",   CfgEntry::FLOAT,  &g_warpPlanetOffset[0], 0 },
    { "warpPlanetOffsetY",   CfgEntry::FLOAT,  &g_warpPlanetOffset[1], 0 },
    { "warpPlanetSeam",      CfgEntry::FLOAT,  &g_warpPlanetSeam, 0 },
    { "warpSlitPos",         CfgEntry::FLOAT,  &g_warpSlitPos, 0 },
    { "warpSlitSpeed",       CfgEntry::FLOAT,  &g_warpSlitSpeed, 0 },
    { "warpSlitDir",         CfgEntry::INT,    &g_warpSlitDir, 0 },
    { "warpBorderColor",     CfgEntry::FLOAT4, g_warpBorderColor, 0 },
    { "warpAspect",          CfgEntry::FLOAT,  &g_warpAspect, 0 },
    { "warpAspectAngle",     CfgEntry::FLOAT,  &g_warpAspectAngle, 0 },
    { "warpBlend",           CfgEntry::INT,    &g_warpBlend, 0 },
    { "warpOpacity",         CfgEntry::FLOAT,  &g_warpOpacity, 0 },
    { "warpDepthMode",       CfgEntry::INT,    &g_warpDepthMode, 0 },
    { "warpDepthDist",       CfgEntry::FLOAT,  &g_warpDepthDist, 0 },
    { "warpDepthRange",      CfgEntry::FLOAT,  &g_warpDepthRange, 0 },
    { "warpDepthRamp",       CfgEntry::FLOAT,  &g_warpDepthRamp, 0 },
    { "warpDepthProtect",    CfgEntry::BOOL,   &g_warpDepthProtect, 0 },
    { "warpFade",            CfgEntry::BOOL,   &g_warpFade, 0 },
    { "emphasizeEnable",   CfgEntry::BOOL,   &g_emphasizeEnable, 0 },
    { "emphasizeAmount",   CfgEntry::FLOAT,  &g_emphasizeAmount, 0 },
    { "emphasizeSpherical",CfgEntry::BOOL,   &g_emphasizeSpherical, 0 },
    { "showDepthEnable",   CfgEntry::BOOL,   &g_showDepthEnable, 0 },
    { "emphasizeFocusDepth",  CfgEntry::FLOAT,  &g_emphasizeFocusDepth, 0 },
    { "emphasizeFocusRange",  CfgEntry::FLOAT,  &g_emphasizeFocusRange, 0 },
    { "emphasizeFocusEdge",   CfgEntry::FLOAT,  &g_emphasizeFocusEdge, 0 },
    { "emphasizeSphereFov",   CfgEntry::INT,    &g_emphasizeSphereFov, 0 },
    { "emphasizeSphereX",     CfgEntry::FLOAT,  &g_emphasizeSphereX, 0 },
    { "emphasizeSphereY",     CfgEntry::FLOAT,  &g_emphasizeSphereY, 0 },
    { "emphasizeBlendColor",  CfgEntry::FLOAT4, g_emphasizeBlendColor, 0 },
    { "emphasizeBlendFactor", CfgEntry::FLOAT,  &g_emphasizeBlendFactor, 0 },
    { "showDepthType",        CfgEntry::INT,    &g_showDepthType, 0 },
    { "showDepthBlend",       CfgEntry::BOOL,   &g_showDepthBlend, 0 },
    { "showDepthLive",        CfgEntry::BOOL,   &g_showDepthLive, 0 },
    { "showDepthUpsideDown",  CfgEntry::BOOL,   &g_showDepthUpsideDown, 0 },
    { "showDepthReversed",    CfgEntry::BOOL,   &g_showDepthReversed, 0 },
    { "showDepthLog",         CfgEntry::BOOL,   &g_showDepthLog, 0 },
    { "showDepthScaleX",      CfgEntry::FLOAT,  &g_showDepthScaleX, 0 },
    { "showDepthScaleY",      CfgEntry::FLOAT,  &g_showDepthScaleY, 0 },
    { "showDepthOffsetX",     CfgEntry::INT,    &g_showDepthOffsetX, 0 },
    { "showDepthOffsetY",     CfgEntry::INT,    &g_showDepthOffsetY, 0 },
    { "showDepthFarPlane",    CfgEntry::FLOAT,  &g_showDepthFarPlane, 0 },
    { "showDepthMultiplier",  CfgEntry::FLOAT,  &g_showDepthMultiplier, 0 },

    // -- Unit D: world fx ----------------------------------------------------------------
    { "fullbrightEnable",    CfgEntry::BOOL,  &g_fullbrightEnable, 0 },
    { "disableInterpEnable", CfgEntry::BOOL,  &g_disableInterpEnable, 0 },
    { "aspectRatioEnable",   CfgEntry::BOOL,  &g_aspectRatioEnable, 0 },
    { "aspectRatioValue",    CfgEntry::FLOAT, &g_aspectRatioValue, 0 },
    { "worldFovEnable",      CfgEntry::BOOL,  &g_worldFovEnable, 0 },
    { "worldFovAmount",      CfgEntry::FLOAT, &g_worldFovAmount, 0 },
    { "thirdPersonEnable",   CfgEntry::BOOL,  &g_thirdPersonEnable, 0 },
    { "thirdPersonDistance", CfgEntry::FLOAT, &g_thirdPersonDistance, 0 },

    // -- Unit D2: world fx 2 ---------------------------------------------------------------
    { "removeVisualRecoilEnable", CfgEntry::BOOL,   &g_removeVisualRecoilEnable, 0 },
    { "dlightEnable",             CfgEntry::BOOL,   &g_dlightEnable, 0 },
    { "dlightRadius",             CfgEntry::FLOAT,  &g_dlightRadius, 0 },
    { "dlightColor",              CfgEntry::FLOAT4, g_dlightColor, 0 },
    { "worldTintEnable",          CfgEntry::BOOL,   &g_worldTintEnable, 0 },
    { "worldTintColor",           CfgEntry::FLOAT4, g_worldTintColor, 0 },
    { "propTintEnable",           CfgEntry::BOOL,   &g_propTintEnable, 0 },
    { "propTintColor",            CfgEntry::FLOAT4, g_propTintColor, 0 },
    { "weatherTintEnable",        CfgEntry::BOOL,   &g_weatherTintEnable, 0 },
    { "weatherTintColor",         CfgEntry::FLOAT4, g_weatherTintColor, 0 },

    { "skyEnable",        CfgEntry::BOOL,   &g_skyEnable, 0 },
    { "skyName",          CfgEntry::STR,    g_skyName, sizeof(g_skyName) },

    // -- Unit D3: reflections + minecraft sky/clouds -------------------------------------
    { "reflEnable",       CfgEntry::BOOL,   &g_reflEnable, 0 },
    { "reflIntensity",    CfgEntry::FLOAT,  &g_reflIntensity, 0 },
    { "reflDistance",     CfgEntry::FLOAT,  &g_reflDistance, 0 },
    { "mcWorldEnable",    CfgEntry::BOOL,   &g_mcWorldEnable, 0 },

    { "weatherEnable",    CfgEntry::BOOL,   &g_weatherEnable, 0 },
    { "weatherMode",      CfgEntry::INT,    &g_weatherMode, 0 },
    { "rainAlpha",        CfgEntry::FLOAT,  &g_rainAlpha, 0 },
    { "rainSpeed",        CfgEntry::FLOAT,  &g_rainSpeed, 0 },
    { "rainWidth",        CfgEntry::FLOAT,  &g_rainWidth, 0 },
    { "rainLength",       CfgEntry::FLOAT,  &g_rainLength, 0 },
    { "rainRadius",       CfgEntry::FLOAT,  &g_rainRadius, 0 },
    { "rainSideVel",      CfgEntry::FLOAT,  &g_rainSideVel, 0 },
    { "snowAlpha",        CfgEntry::FLOAT,  &g_snowAlpha, 0 },
    { "snowSpeed",        CfgEntry::FLOAT,  &g_snowSpeed, 0 },
    { "snowWidth",        CfgEntry::FLOAT,  &g_snowWidth, 0 },
    { "snowLength",       CfgEntry::FLOAT,  &g_snowLength, 0 },
    { "snowRadius",       CfgEntry::FLOAT,  &g_snowRadius, 0 },
    { "snowSideVel",      CfgEntry::FLOAT,  &g_snowSideVel, 0 },
    { "snowDensity",      CfgEntry::FLOAT,  &g_snowDensity, 0 },

    // "noHats"/"noUnusual" removed: TF2-only cosmetics, no CSS counterpart
    // sheenNormal/Alpha/Env removed round-4 (VISTAB): TF2 killstreak sheen, see dllmain.cpp.
    { "selfKillfeedOnly", CfgEntry::BOOL,   &g_selfKillfeedOnly, 0 },
    // "revealScoreboard"/"steamHistory" removed 11-03: TF2-only / N/A on CS:S (scoreboard.h)
    { "removeRagdolls",   CfgEntry::BOOL,   &g_removeRagdolls, 0 },
    { "removePropFade",   CfgEntry::BOOL,   &g_removePropFade, 0 },
    { "fpsBoost",         CfgEntry::BOOL,   &g_fpsBoost, 0 },
    { "removeScreenEffects", CfgEntry::BOOL, &g_removeScreenEffects, 0 },
    { "flipWorld",        CfgEntry::BOOL,   &g_flipWorld, 0 },

    { "lagResolver",      CfgEntry::BOOL,   &g_lagResolver, 0 },
    { "lrYaw",            CfgEntry::BOOL,   &g_lrYaw, 0 },
    { "lrSmoothMs",       CfgEntry::FLOAT,  &g_lrSmoothMs, 0 },
    { "lrExtrapMs",       CfgEntry::FLOAT,  &g_lrExtrapMs, 0 },
    { "lrMaxErr",         CfgEntry::FLOAT,  &g_lrMaxErr, 0 },


    // lowLatency / hiPriority keys deleted 2026-09-05 -- an old cfg carrying them is simply
    // ignored by the parser, which is what should happen: both features are gone.
    { "hiTimerRes",       CfgEntry::BOOL,   &g_hiTimerRes, 0 },
    { "perfEnable",       CfgEntry::BOOL,   &g_perfEnable, 0 },
    // misc convenience buttons, one title/command pair per slot
    { "convTitle0", CfgEntry::STR, g_convTitle[0], sizeof(g_convTitle[0]) }, { "convCmd0", CfgEntry::STR, g_convCmd[0], sizeof(g_convCmd[0]) },
    { "convTitle1", CfgEntry::STR, g_convTitle[1], sizeof(g_convTitle[1]) }, { "convCmd1", CfgEntry::STR, g_convCmd[1], sizeof(g_convCmd[1]) },
    { "convTitle2", CfgEntry::STR, g_convTitle[2], sizeof(g_convTitle[2]) }, { "convCmd2", CfgEntry::STR, g_convCmd[2], sizeof(g_convCmd[2]) },
    { "convTitle3", CfgEntry::STR, g_convTitle[3], sizeof(g_convTitle[3]) }, { "convCmd3", CfgEntry::STR, g_convCmd[3], sizeof(g_convCmd[3]) },
    { "convTitle4", CfgEntry::STR, g_convTitle[4], sizeof(g_convTitle[4]) }, { "convCmd4", CfgEntry::STR, g_convCmd[4], sizeof(g_convCmd[4]) },
    { "convTitle5", CfgEntry::STR, g_convTitle[5], sizeof(g_convTitle[5]) }, { "convCmd5", CfgEntry::STR, g_convCmd[5], sizeof(g_convCmd[5]) },
    { "convTitle6", CfgEntry::STR, g_convTitle[6], sizeof(g_convTitle[6]) }, { "convCmd6", CfgEntry::STR, g_convCmd[6], sizeof(g_convCmd[6]) },
    { "convTitle7", CfgEntry::STR, g_convTitle[7], sizeof(g_convTitle[7]) }, { "convCmd7", CfgEntry::STR, g_convCmd[7], sizeof(g_convCmd[7]) },
    // fonts tab (fonts.h)
    { "fontIndName",      CfgEntry::STR,    g_fontIndName, sizeof(g_fontIndName) },
    { "fontIndSize",      CfgEntry::INT,    &g_fontIndSize, 0 },
    { "fontIndFlags",     CfgEntry::INT,    &g_fontIndFlags, 0 },
    { "fontEspName",      CfgEntry::STR,    g_fontEspName, sizeof(g_fontEspName) },
    { "fontEspSize",      CfgEntry::INT,    &g_fontEspSize, 0 },
    { "fontEspFlags",     CfgEntry::INT,    &g_fontEspFlags, 0 },


    { "bhop",             CfgEntry::BOOL,   &g_bhop, 0 },
    { "jumpbug",          CfgEntry::BOOL,   &g_jumpbug, 0 },
    { "jumpbugKey",       CfgEntry::INT,    &g_jumpbugKey, 0 },
    { "jumpbugMode",      CfgEntry::INT,    &g_jumpbugMode, 0 },
    { "jbYieldSurf",      CfgEntry::BOOL,   &g_jbYieldSurf, 0 },
    // jbPredict removed (11-03): renamed g_jumpbugOnly, cfg row now in cfg/u_mvcore.inc
    { "autoStrafe",       CfgEntry::BOOL,   &g_autoStrafe, 0 },
    { "autoStrafeKey",    CfgEntry::INT,    &g_autoStrafeKey, 0 },
    { "autoStrafeMode",   CfgEntry::INT,    &g_autoStrafeMode, 0 },
    { "autoStrafeAvoidWalls", CfgEntry::BOOL, &g_autoStrafeAvoidWalls, 0 },
    { "autoStrafeAvoidDist",  CfgEntry::FLOAT,&g_autoStrafeAvoidDist, 0 },
    { "strafeOpt",        CfgEntry::BOOL,   &g_strafeOpt, 0 },
    { "strafeOptGain",    CfgEntry::FLOAT,  &g_strafeOptGain, 0 },
    { "strafeOptMinSpeed",CfgEntry::FLOAT,  &g_strafeOptMinSpeed, 0 },
    { "edgeJump",         CfgEntry::BOOL,   &g_edgeJump, 0 },
    { "edgeJumpKey",      CfgEntry::INT,    &g_edgeJumpKey, 0 },
    { "edgeJumpMode",     CfgEntry::INT,    &g_edgeJumpMode, 0 },
    { "longJump",         CfgEntry::BOOL,   &g_longJump, 0 },
    { "longJumpKey",      CfgEntry::INT,    &g_longJumpKey, 0 },
    { "longJumpMode",     CfgEntry::INT,    &g_longJumpMode, 0 },
    { "longJumpEdge",     CfgEntry::BOOL,   &g_longJumpEdge, 0 },
    { "longJumpAdaptive",       CfgEntry::BOOL,  &g_longJumpAdaptive, 0 },
    // 3 stop-reason bools removed (11-03): g_longJumpAdaptiveStopMask, cfg row in cfg/u_mvcore.inc
    { "longJumpAdaptiveTime",   CfgEntry::FLOAT, &g_longJumpAdaptiveTime, 0 },
    { "miniJump",         CfgEntry::BOOL,   &g_miniJump, 0 },
    { "miniJumpKey",      CfgEntry::INT,    &g_miniJumpKey, 0 },
    { "miniJumpMode",     CfgEntry::INT,    &g_miniJumpMode, 0 },
    { "miniJumpHoldDuck", CfgEntry::BOOL,   &g_miniJumpHoldDuck, 0 },
    { "miniJumpEdge",     CfgEntry::BOOL,   &g_miniJumpEdge, 0 },
    { "fastStop",         CfgEntry::BOOL,   &g_fastStop, 0 },
    // fastAccel / noPush removed (11-03): no botox csgo equivalent (TF2-port invention)

    // RESERVED, do not re-use: autoBackstab / backstabKey / backstabMode / backstabSilent /
    // backstabFOV / backstabIgnoreInvuln / backstabIgnoreCloaked / cleanPovDemos. Unknown
    // keys are ignored on load, so an old cfg is harmless -- unless a name is recycled,
    // and then its stale value lands in whatever took the slot.

    { "edgeBug",          CfgEntry::BOOL,   &g_edgeBug, 0 },
    { "edgeBugKey",       CfgEntry::INT,    &g_edgeBugKey, 0 },
    { "edgeBugMode",      CfgEntry::INT,    &g_edgeBugMode, 0 },
    { "edgeBugPredictTime", CfgEntry::FLOAT, &g_edgeBugPredictTime, 0 },
    { "edgeBugDetectMode",  CfgEntry::INT,   &g_edgeBugDetectMode, 0 },
    { "edgeBugMaxBend", CfgEntry::FLOAT, &g_edgeBugStrafeAngleMax, 0 },
    { "edgeBugStance",    CfgEntry::INT,    &g_edgeBugStance, 0 },
    { "edgeBugToEdge",    CfgEntry::BOOL,   &g_edgeBugToEdge, 0 },
    { "edgeBugMouseSteer",CfgEntry::BOOL,   &g_edgeBugMouseSteer, 0 },
    { "edgeBugPaths",     CfgEntry::INT,    &g_edgeBugPaths, 0 },
    { "edgeBugLockType",  CfgEntry::INT,    &g_edgeBugLockType, 0 },
    { "edgeBugLockStrength", CfgEntry::FLOAT, &g_edgeBugLockStrength, 0 },
    { "edgeBugDelTicks",  CfgEntry::INT,    &g_edgeBugDelTicks, 0 },
    { "edgeBugDelAdvanced", CfgEntry::BOOL, &g_edgeBugDelAdvanced, 0 },
    { "edgeBugDelAngleLimit", CfgEntry::FLOAT, &g_edgeBugDelAngleLimit, 0 },
    { "edgeBugDelSearch", CfgEntry::INT,    &g_edgeBugDelSearch, 0 },
    { "edgeBugDelSilent", CfgEntry::BOOL,   &g_edgeBugDelSilent, 0 },
    { "edgeBugDelMouseFix", CfgEntry::BOOL, &g_edgeBugDelMouseFix, 0 },
    { "edgeBugDnaScan",   CfgEntry::FLOAT,  &g_edgeBugDnaScan, 0 },
    { "edgeBugDnaAdvanced", CfgEntry::BOOL, &g_edgeBugDnaAdvanced, 0 },
    { "edgeBugDnaRange",  CfgEntry::INT,    &g_edgeBugDnaRange, 0 },
    { "edgeBugDnaCustomAngle", CfgEntry::BOOL, &g_edgeBugDnaCustomAngle, 0 },
    { "edgeBugDnaAngleLimit", CfgEntry::FLOAT, &g_edgeBugDnaAngleLimit, 0 },
    { "edgeBugHwTicks",   CfgEntry::INT,    &g_edgeBugHwTicks, 0 },
    { "edgeBugHwLockAmt", CfgEntry::FLOAT,  &g_edgeBugHwLockAmt, 0 },
    { "edgeBugHwAdvanced", CfgEntry::BOOL,  &g_edgeBugHwAdvanced, 0 },
    { "edgeBugHwStrafeDeltaMax", CfgEntry::FLOAT, &g_edgeBugHwStrafeDeltaMax, 0 },
    { "edgeBugViz",       CfgEntry::BOOL,   &g_edgeBugViz, 0 },
    { "edgeBugVizSize",   CfgEntry::FLOAT,  &g_edgeBugVizSize, 0 },
    { "pixelSurf",        CfgEntry::BOOL,   &g_pixelSurf, 0 },
    { "pixelSurfKey",     CfgEntry::INT,    &g_pixelSurfKey, 0 },
    { "pixelSurfMode",    CfgEntry::INT,    &g_pixelSurfMode, 0 },
{ "pixelSurfPredictTicks", CfgEntry::INT, &g_pixelSurfPredictTicks, 0 },
    { "autoAlign",        CfgEntry::BOOL,   &g_autoAlign, 0 },
    { "aaHoldSurf",       CfgEntry::BOOL,   &g_aaHoldSurf, 0 },
    { "textureBug",       CfgEntry::BOOL,   &g_textureBug, 0 },
    { "textureBugKey",    CfgEntry::INT,    &g_textureBugKey, 0 },
    { "textureBugMode",   CfgEntry::INT,    &g_textureBugMode, 0 },
    // "tbStyle"/"tbcAlign" removed: cumidere style engine dropped (texturebug.inl now 1:1 botox)
    { "tbReach",          CfgEntry::FLOAT,  &g_tbReach, 0 },
    { "tbScanStep",       CfgEntry::FLOAT,  &g_tbScanStep, 0 },   // shared WC/AS wall-scan infra (MV_DetectWallLines), not texture-bug-only -- kept
    // tbRampNz/tbHugRange/tbSolveForward/tbNoOffgridStick/tbParkAtEnd/tbParkTicks/tbParkSpeed/
    // tbParkBrake/tbParkLead/tbPredict/tbPredictTicks/tbPredictReach deleted 11-03: TF2-port-only
    // settings botox never had (texturebug.inl folded them to fixed constants at their old
    // defaults -- zero behavior change, just no longer user-exposed). See MVTB report.
    { "wallClimb",        CfgEntry::BOOL,   &g_wallClimb, 0 },
    { "wallClimbKey",     CfgEntry::INT,    &g_wallClimbKey, 0 },
    { "wallClimbMode",    CfgEntry::INT,    &g_wallClimbMode, 0 },
    { "fireMan",          CfgEntry::BOOL,   &g_fireMan, 0 },
    { "fireManKey",       CfgEntry::INT,    &g_fireManKey, 0 },
    { "fireManMode",      CfgEntry::INT,    &g_fireManMode, 0 },
    { "firemanApproach",   CfgEntry::BOOL,  &g_firemanApproach, 0 },
    { "firemanReach",      CfgEntry::FLOAT, &g_firemanReach, 0 },
    { "firemanCrouchJump", CfgEntry::BOOL,  &g_firemanCrouchJump, 0 },
    { "firemanDropIn",     CfgEntry::BOOL,  &g_firemanDropIn, 0 },
    { "firemanDropInLock", CfgEntry::BOOL,  &g_firemanDropInLock, 0 },
    { "firemanSideOnly",   CfgEntry::BOOL,  &g_firemanSideOnly, 0 },
    { "firemanSideOnlyKey",  CfgEntry::INT, &g_firemanSideOnlyKey, 0 },
    { "firemanSideOnlyMode", CfgEntry::INT, &g_firemanSideOnlyMode, 0 },
    { "antiLadder",       CfgEntry::BOOL,   &g_antiLadder, 0 },
    { "antiLadderKey",    CfgEntry::INT,    &g_antiLadderKey, 0 },
    { "antiLadderMode",   CfgEntry::INT,    &g_antiLadderMode, 0 },
    { "ladderGlide",      CfgEntry::BOOL,   &g_ladderGlide, 0 },
    { "ladderGlideKey",   CfgEntry::INT,    &g_ladderGlideKey, 0 },
    { "ladderGlideMode",  CfgEntry::INT,    &g_ladderGlideMode, 0 },
    { "ladderFreelook",     CfgEntry::BOOL, &g_ladderFreelookClimb, 0 },
    { "ladderFreelookKey",  CfgEntry::INT,  &g_ladderFreelookClimbKey, 0 },
    { "ladderFreelookMode", CfgEntry::INT,  &g_ladderFreelookClimbMode, 0 },
    { "fastLadder",       CfgEntry::BOOL,   &g_fastLadder, 0 },
    { "fastLadderKey",    CfgEntry::INT,    &g_fastLadderKey, 0 },
    { "fastLadderMode",   CfgEntry::INT,    &g_fastLadderMode, 0 },
    { "autoOneHop",       CfgEntry::BOOL,   &g_autoOneHopLadder, 0 },
    { "autoOneHopKey",    CfgEntry::INT,    &g_autoOneHopLadderKey, 0 },
    { "autoOneHopMode",   CfgEntry::INT,    &g_autoOneHopLadderMode, 0 },
    { "wallClimbPreventSlow", CfgEntry::BOOL, &g_wcPreventSlow, 0 },
    { "wallClimbDuck",    CfgEntry::BOOL,   &g_wcDuck, 0 },
    { "wallClimbSeamSteer", CfgEntry::BOOL, &g_wallClimbSeamSteer, 0 },
    { "wallClimbSteerScale", CfgEntry::FLOAT, &g_wallClimbSteerScale, 0 },
    { "showWallSlope",      CfgEntry::BOOL, &g_showWallSlope, 0 },
    { "showWallSlopeKey",   CfgEntry::INT,  &g_showWallSlopeKey, 0 },
    { "showWallSlopeMode",  CfgEntry::INT,  &g_showWallSlopeMode, 0 },
    { "wallClimbViz",     CfgEntry::BOOL,   &g_wcViz, 0 },
    { "wallClimbVizColor", CfgEntry::FLOAT4, g_wcVizColor, 0 },
    { "airStuck",         CfgEntry::BOOL,   &g_airStuck, 0 },
    { "asExtraReach",     CfgEntry::FLOAT,  &g_asExtraReach, 0 },
    { "asBudget",         CfgEntry::FLOAT,  &g_asBudget, 0 },
    // "asStyle"/"asa*" removed: cumidere/other style engines dropped (airstuck.inl now 1:1 botox)
    { "airStuckKey",      CfgEntry::INT,    &g_airStuckKey, 0 },
    { "airStuckMode",     CfgEntry::INT,    &g_airStuckMode, 0 },
    { "pixelHop",         CfgEntry::BOOL,   &g_pixelHop, 0 },
    { "pixelHopKey",      CfgEntry::INT,    &g_pixelHopKey, 0 },
    { "pixelHopMode",     CfgEntry::INT,    &g_pixelHopMode, 0 },
    { "phReach",          CfgEntry::BOOL,   &g_phReach, 0 },
    { "phReachTicks",     CfgEntry::INT,    &g_phReachTicks, 0 },
    { "phReachAngle",     CfgEntry::FLOAT,  &g_phReachAngle, 0 },

    // -- Unit A port: small tricks from botox csgo-sdk --
    { "nulls",            CfgEntry::BOOL,   &g_nulls, 0 },
    { "hsw",               CfgEntry::BOOL,  &g_hsw, 0 },
    { "hswKey",            CfgEntry::INT,   &g_hswKey, 0 },
    { "hswMode",           CfgEntry::INT,   &g_hswMode, 0 },
    { "hswSide",           CfgEntry::INT,   &g_hswSide, 0 },
    { "hswAllowWS",        CfgEntry::BOOL,  &g_hswAllowWS, 0 },
    { "hswAllowAD",        CfgEntry::BOOL,  &g_hswAllowAD, 0 },
    { "hswPreGround",      CfgEntry::BOOL,  &g_hswPreGround, 0 },
    { "delayHop",          CfgEntry::BOOL,  &g_delayHop, 0 },
    { "delayHopKey",       CfgEntry::INT,   &g_delayHopKey, 0 },
    { "delayHopMode",      CfgEntry::INT,   &g_delayHopMode, 0 },
    { "delayHopTicks",     CfgEntry::INT,   &g_delayHopTicks, 0 },
    { "autoCrouch",        CfgEntry::BOOL,  &g_autoCrouch, 0 },
    { "autoCrouchKey",     CfgEntry::INT,   &g_autoCrouchKey, 0 },
    { "autoCrouchMode",    CfgEntry::INT,   &g_autoCrouchMode, 0 },
    { "autoCrouchTicks",   CfgEntry::INT,   &g_autoCrouchTicks, 0 },
    { "silentView",        CfgEntry::BOOL,  &g_silentView, 0 },
    { "jumpstats",         CfgEntry::BOOL,  &g_jumpstats, 0 },

    // -- Unit A3 port: bounce/pf modes/eb presets/ps fix --
    { "autoBounce",        CfgEntry::BOOL,  &g_autoBounce, 0 },
    { "autoBounceKey",     CfgEntry::INT,   &g_autoBounceKey, 0 },
    { "autoBounceMode",    CfgEntry::INT,   &g_autoBounceMode, 0 },
    { "abPlanMode",        CfgEntry::INT,   &g_abMode, 0 },
    { "abMinGain",         CfgEntry::FLOAT, &g_abMinGain, 0 },
    { "abReach",           CfgEntry::FLOAT, &g_abReach, 0 },
    { "abJump",            CfgEntry::BOOL,  &g_abJump, 0 },
    { "abCrouch",          CfgEntry::BOOL,  &g_abCrouch, 0 },
    { "abAlign",           CfgEntry::BOOL,  &g_abAlign, 0 },
    { "abViz",             CfgEntry::BOOL,  &g_abViz, 0 },
    { "abVizColor",        CfgEntry::FLOAT4, g_abVizColor, 0 },
    { "abLumiRays",        CfgEntry::INT,   &g_abLumiRays, 0 },
    { "abLumiRadius",      CfgEntry::FLOAT, &g_abLumiRadius, 0 },
    { "abLumiDepth",       CfgEntry::FLOAT, &g_abLumiDepth, 0 },
    { "abLumiMinAngle",    CfgEntry::FLOAT, &g_abLumiMinAngle, 0 },
    { "abLumiMaxAngle",    CfgEntry::FLOAT, &g_abLumiMaxAngle, 0 },
    { "abLumiSmooth",      CfgEntry::FLOAT, &g_abLumiSmooth, 0 },
    { "psFinderHeadbounce",CfgEntry::BOOL,  &g_psFinderHeadbounce, 0 },
    { "psFinderPixeljump", CfgEntry::BOOL,  &g_psFinderPixeljump, 0 },
    { "psFinderJumpSurf",  CfgEntry::BOOL,  &g_psFinderJumpSurf, 0 },
    { "psSurfFix",         CfgEntry::BOOL,  &g_psSurfFix, 0 },
    { "edgeBugStyle",      CfgEntry::INT,   &g_edgeBugStyle, 0 },
    { "edgeBugMouseLock",  CfgEntry::BOOL,  &g_edgeBugMouseLock, 0 },
    // -- Unit A2 port: edge skip + blockbot from botox csgo-sdk --
    { "edgeSkip",          CfgEntry::BOOL,  &g_edgeSkip, 0 },
    { "edgeSkipKey",       CfgEntry::INT,   &g_edgeSkipKey, 0 },
    { "edgeSkipMode",      CfgEntry::INT,   &g_edgeSkipMode, 0 },
    { "edgeSkipDepth",     CfgEntry::INT,   &g_edgeSkipDepth, 0 },
    { "edgeSkipMaxBend",   CfgEntry::FLOAT, &g_edgeSkipMaxBend, 0 },
    { "blockBot",          CfgEntry::BOOL,  &g_blockBot, 0 },
    { "blockBotKey",       CfgEntry::INT,   &g_blockBotKey, 0 },
    { "blockBotMode",      CfgEntry::INT,   &g_blockBotMode, 0 },

    // fake movement (fm*) rows removed (11-03/11-04): no botox csgo equivalent, see globals.h
    { "psFinder",         CfgEntry::BOOL,   &g_psFinder, 0 },
    { "psFinderKey",      CfgEntry::INT,    &g_psFinderKey, 0 },
    { "psFinderMode",     CfgEntry::INT,    &g_psFinderMode, 0 },
    { "psFinderMargin",   CfgEntry::FLOAT,  &g_psFinderMargin, 0 },
    { "psFinderBudget",   CfgEntry::FLOAT,  &g_psFinderBudget, 0 },
    { "psFinderStrict",   CfgEntry::BOOL,   &g_psFinderStrict, 0 },
    { "psFinderTb",       CfgEntry::BOOL,   &g_psFinderTb, 0 },
    { "psFinderNoProps",  CfgEntry::BOOL,   &g_psFinderNoProps, 0 },
    { "psFinderLength",   CfgEntry::BOOL,   &g_psFinderLength, 0 },
    { "psMapLines",       CfgEntry::BOOL,   &g_psMapLines, 0 },
    { "psMapMaxDist",     CfgEntry::FLOAT,  &g_psMapMaxDist, 0 },
    { "psAssist",         CfgEntry::BOOL,   &g_psAssist, 0 },
    { "psAssistKey",      CfgEntry::INT,    &g_psAssistKey, 0 },
    { "psAssistMode",     CfgEntry::INT,    &g_psAssistMode, 0 },
    { "psSetPointKey",    CfgEntry::INT,    &g_psSetPointKey, 0 },
    { "psDeletePointKey", CfgEntry::INT,    &g_psDeletePointKey, 0 },
    // psAssistRadius/psAssistMaxPoints/psAssistReuseDelay/psAssistJumpBox/psGroundHelpAngle/
    // psLaunchZSlack rows removed -- PSPORT round 2 11-04, those globals are gone (the old
    // design's own tuning knobs; botox's real pixelsurf_assist.cpp has no equivalents). Their
    // botox-equivalent vars (g_psAssistTicks/_Type/_BrokeHop/g_psGroundAngleLimit/
    // g_psAssistRenderHeight) are new `inline` globals in cfg/u_mvps.h/.inc, not here.
    { "psAssistSnap",     CfgEntry::FLOAT,  &g_psAssistSnap, 0 },
    { "psAssistRender",   CfgEntry::BOOL,   &g_psAssistRender, 0 },
    { "psGroundHelp",     CfgEntry::BOOL,   &g_psGroundHelp, 0 },

    { "routeCalc",        CfgEntry::BOOL,   &g_routeCalc, 0 },
    { "rcAddKey",         CfgEntry::INT,    &g_rcAddKey, 0 },
    { "rcDeleteKey",      CfgEntry::INT,    &g_rcDeleteKey, 0 },
    { "rcClearKey",       CfgEntry::INT,    &g_rcClearKey, 0 },
    { "rcSolveKey",       CfgEntry::INT,    &g_rcSolveKey, 0 },
    // the move list is 6 bits with new meanings, so the key is new too -- an old rcMoves
    // (13 bits) would read as the wrong moves. Unmatched old keys are skipped on load.
    // rcBudget removed (dihrc worker thread): the solve runs off the game thread now, nothing
    // left to budget per cmd. An old rcBudget line in a saved cfg is just skipped.
    { "rcMoveBits",       CfgEntry::INT,    &g_rcMoveMask, 0 },   // was rcMoveSet (6 bits): renamed so the delay hop bit (7th) starts on
    { "rcMaxShown",       CfgEntry::INT,    &g_rcMaxShown, 0 },
    { "rcStartHere",      CfgEntry::BOOL,   &g_rcStartHere, 0 },
    // rcShowMarkers removed: botox always draws the points (an old line is skipped on load)
    { "rcShowPath",       CfgEntry::BOOL,   &g_rcShowPath, 0 },
    { "rcShowPanel",      CfgEntry::BOOL,   &g_rcShowPanel, 0 },
    { "rcPopupTime",      CfgEntry::INT,    &g_rcPopupTime, 0 },
    { "rcStartMoves",     CfgEntry::INT,    &g_rcStartMoves, 0 },
    { "rcDelayTicks",     CfgEntry::INT,    &g_rcDelayTicks, 0 },
    { "rcShowKeys",       CfgEntry::BOOL,   &g_rcShowKeys, 0 },
    { "rcPathAlpha",      CfgEntry::INT,    &g_rcPathAlpha, 0 },
    { "rcPathThick",      CfgEntry::FLOAT,  &g_rcPathThick, 0 },
    { "rcAllowInvalid",   CfgEntry::BOOL,   &g_rcAllowInvalid, 0 },
    { "rcSnapEdge",       CfgEntry::BOOL,   &g_rcSnapEdge, 0 },
    { "rcAdvanced",       CfgEntry::BOOL,   &g_rcAdvanced, 0 },
    { "pixelCalc",        CfgEntry::BOOL,   &g_pixelCalc, 0 },
    { "pcPlaceKey",       CfgEntry::INT,    &g_pcPlaceKey, 0 },
    { "pcSolveKey",       CfgEntry::INT,    &g_pcSolveKey, 0 },
    { "pcShowMarker",     CfgEntry::BOOL,   &g_pcShowMarker, 0 },
    { "pcShowPanel",      CfgEntry::BOOL,   &g_pcShowPanel, 0 },
    // "pcBuildings"/"pcBuildMask" removed: no buildings in CSS (TF2 sentry/dispenser/teleporter)

    // prInfAmmo/prInfHealth/prInfTime/prNoclipKey removed 11-03: no botox csgo equivalent
    // (botox's practice window is checkpoint/teleport/show window only -- see cfg/u_mvcore.inc)
    { "prSaveKey",        CfgEntry::INT,    &g_prSaveKey, 0 },
    { "prTpKey",          CfgEntry::INT,    &g_prTpKey, 0 },
    { "prBotEnemyKey",    CfgEntry::INT,    &g_prBotEnemyKey, 0 },
    { "prBotMateKey",     CfgEntry::INT,    &g_prBotMateKey, 0 },
    { "distCalc",         CfgEntry::BOOL,   &g_distCalc, 0 },
    { "distCalcKey",      CfgEntry::INT,    &g_distCalcKey, 0 },
    { "distCalcMode",     CfgEntry::INT,    &g_distCalcMode, 0 },
    { "floorLevel",       CfgEntry::BOOL,   &g_floorLevel, 0 },
    { "floorLevelKey",    CfgEntry::INT,    &g_floorLevelKey, 0 },
    { "floorLevelMode",   CfgEntry::INT,    &g_floorLevelMode, 0 },
    { "floorLevelSize",   CfgEntry::FLOAT,  &g_floorLevelSize, 0 },
    // sentry/dispenser/teleporter spawn keys + bot class removed: TF2 building/class bot
    // spawner, no csgo counterpart. prBotEnemyKey/prBotMateKey (bot-at-crosshair, either team)
    // kept -- real CS feature, still used by practice.inl PrBot(), see report KEPT EXTRAS.

    { "espEnable",               CfgEntry::BOOL,   &g_espEnable, 0 },
    { "espPreview",              CfgEntry::BOOL,   &g_espPreview, 0 },
    { "espTeammates",            CfgEntry::BOOL,   &g_espTeammates, 0 },
    { "espMaxDistance",          CfgEntry::FLOAT,  &g_espMaxDistance, 0 },
    { "espDistanceFade",         CfgEntry::BOOL,   &g_espDistanceFade, 0 },
    { "espSoundOnly",            CfgEntry::BOOL,   &g_espSoundOnly, 0 },
    { "espSoundDuration",        CfgEntry::FLOAT,  &g_espSoundDuration, 0 },
    { "espSoundFade",            CfgEntry::BOOL,   &g_espSoundFade, 0 },
    { "espBox",                  CfgEntry::BOOL,   &g_espBox, 0 },
    { "espBoxVisColors",         CfgEntry::BOOL,   &g_espBoxVisColors, 0 },
    { "espBoxVisibleColor",      CfgEntry::FLOAT4, g_espBoxVisibleColor, 0 },
    { "espBoxInvisibleColor",    CfgEntry::FLOAT4, g_espBoxInvisibleColor, 0 },
    { "espBoxColor",             CfgEntry::FLOAT4, g_espBoxColor, 0 },
    { "espBox3d",                CfgEntry::BOOL,   &g_espBox3d, 0 },
    { "espBoxCorner",            CfgEntry::BOOL,   &g_espBoxCorner, 0 },
    { "espBoxThickness",         CfgEntry::FLOAT,  &g_espBoxThickness, 0 },
    { "espBoxRounding",          CfgEntry::FLOAT,  &g_espBoxRounding, 0 },
    { "espBoxOutline",           CfgEntry::BOOL,   &g_espBoxOutline, 0 },
    { "espBoxOutlineColor",      CfgEntry::FLOAT4, g_espBoxOutlineColor, 0 },
    { "espBoxOutlineThickness",  CfgEntry::FLOAT,  &g_espBoxOutlineThickness, 0 },
    { "espBoxOutlineSide",       CfgEntry::INT,    &g_espBoxOutlineSide, 0 },
    { "espBoxFill",              CfgEntry::BOOL,   &g_espBoxFill, 0 },
    { "espBoxFillColor",         CfgEntry::FLOAT4, g_espBoxFillColor, 0 },
    { "espBoxFillGradient",      CfgEntry::BOOL,   &g_espBoxFillGradient, 0 },
    { "espBoxFillBottomColor",   CfgEntry::FLOAT4, g_espBoxFillBottomColor, 0 },
    { "espName",                 CfgEntry::BOOL,   &g_espName, 0 },
    { "espNamePosition",         CfgEntry::INT,    &g_espNamePosition, 0 },
    { "espNameVisColors",        CfgEntry::BOOL,   &g_espNameVisColors, 0 },
    { "espNameVisibleColor",     CfgEntry::FLOAT4, g_espNameVisibleColor, 0 },
    { "espNameInvisibleColor",   CfgEntry::FLOAT4, g_espNameInvisibleColor, 0 },
    { "espNameColor",            CfgEntry::FLOAT4, g_espNameColor, 0 },
    { "espHealthBar",            CfgEntry::BOOL,   &g_espHealthBar, 0 },
    { "espHealthBarSide",        CfgEntry::INT,    &g_espHealthBarSide, 0 },
    { "espHealthBarCornerPick",  CfgEntry::INT,    &g_espHealthBarCornerPick, 0 },
    { "espHealthBarCornerRounding", CfgEntry::FLOAT,  &g_espHealthBarCornerRounding, 0 },
    { "espHealthBarCornerSize",  CfgEntry::FLOAT,  &g_espHealthBarCornerSize, 0 },
    { "espHealthBarThickness",   CfgEntry::FLOAT,  &g_espHealthBarThickness, 0 },
    { "espHealthBarOutline",     CfgEntry::BOOL,   &g_espHealthBarOutline, 0 },
    { "espHealthBarCustomColor", CfgEntry::BOOL,   &g_espHealthBarCustomColor, 0 },
    { "espHealthBarColor",       CfgEntry::FLOAT4, g_espHealthBarColor, 0 },
    { "espHealthBarGradient",    CfgEntry::BOOL,   &g_espHealthBarGradient, 0 },
    { "espHealthBarBottomColor", CfgEntry::FLOAT4, g_espHealthBarBottomColor, 0 },
    { "espHealthBarBgColor",     CfgEntry::FLOAT4, g_espHealthBarBgColor, 0 },
    { "espHealthText",           CfgEntry::BOOL,   &g_espHealthText, 0 },
    { "espHealthSuffix",         CfgEntry::BOOL,   &g_espHealthSuffix, 0 },
    { "espHealthTextStyle",      CfgEntry::INT,    &g_espHealthTextStyle, 0 },
    { "espHealthTextCustomColor", CfgEntry::BOOL,   &g_espHealthTextCustomColor, 0 },
    { "espHealthTextColor",      CfgEntry::FLOAT4, g_espHealthTextColor, 0 },
    { "espWeaponName",           CfgEntry::BOOL,   &g_espWeaponName, 0 },
    { "espWeaponNameColor",      CfgEntry::FLOAT4, g_espWeaponNameColor, 0 },
    { "espAmmoBar",              CfgEntry::BOOL,   &g_espAmmoBar, 0 },
    { "espAmmoBarColor",         CfgEntry::FLOAT4, g_espAmmoBarColor, 0 },
    { "espDistance",             CfgEntry::BOOL,   &g_espDistance, 0 },
    { "espDistanceUnit",         CfgEntry::INT,    &g_espDistanceUnit, 0 },
    { "espDistanceColor",        CfgEntry::FLOAT4, g_espDistanceColor, 0 },
    { "espFlags",                CfgEntry::BOOL,   &g_espFlags, 0 },
    { "espFlagList",             CfgEntry::INT,    &g_espFlagList, 0 },
    { "espFlagsColor",           CfgEntry::FLOAT4, g_espFlagsColor, 0 },
    { "espFlagsAlertColor",      CfgEntry::FLOAT4, g_espFlagsAlertColor, 0 },
    { "espSkeleton",             CfgEntry::BOOL,   &g_espSkeleton, 0 },
    { "espSkeletonVisColors",    CfgEntry::BOOL,   &g_espSkeletonVisColors, 0 },
    { "espSkeletonVisibleColor", CfgEntry::FLOAT4, g_espSkeletonVisibleColor, 0 },
    { "espSkeletonInvisibleColor", CfgEntry::FLOAT4, g_espSkeletonInvisibleColor, 0 },
    { "espSkeletonColor",        CfgEntry::FLOAT4, g_espSkeletonColor, 0 },
    { "espSkeletonType",         CfgEntry::INT,    &g_espSkeletonType, 0 },
    { "espSkeletonThickness",    CfgEntry::FLOAT,  &g_espSkeletonThickness, 0 },
    { "espHitboxes",             CfgEntry::BOOL,   &g_espHitboxes, 0 },
    { "espHitboxesMode",         CfgEntry::INT,    &g_espHitboxesMode, 0 },
    { "espHitboxColor",          CfgEntry::FLOAT4, g_espHitboxColor, 0 },
    { "espHitboxesThickness",    CfgEntry::FLOAT,  &g_espHitboxesThickness, 0 },
    { "espHitboxesDuration",     CfgEntry::FLOAT,  &g_espHitboxesDuration, 0 },
    { "espAvatar",               CfgEntry::BOOL,   &g_espAvatar, 0 },
    { "espAvatarPosition",       CfgEntry::INT,    &g_espAvatarPosition, 0 },
    { "espAvatarSize",           CfgEntry::FLOAT,  &g_espAvatarSize, 0 },
    { "espAvatarHeadScale",      CfgEntry::FLOAT,  &g_espAvatarHeadScale, 0 },
    { "espDamageNumbers",        CfgEntry::BOOL,   &g_espDamageNumbers, 0 },
    { "espDmgColor",             CfgEntry::FLOAT4, g_espDmgColor, 0 },
    { "espDmgDuration",          CfgEntry::FLOAT,  &g_espDmgDuration, 0 },
    { "espDmgSpeed",             CfgEntry::FLOAT,  &g_espDmgSpeed, 0 },
    { "espDmgStack",             CfgEntry::BOOL,   &g_espDmgStack, 0 },
    { "espOofArrows",            CfgEntry::BOOL,   &g_espOofArrows, 0 },
    { "espOofType",              CfgEntry::INT,    &g_espOofType, 0 },
    { "espOofColor",             CfgEntry::FLOAT4, g_espOofColor, 0 },
    { "espOofWallColor",         CfgEntry::FLOAT4, g_espOofWallColor, 0 },
    { "espOofSoundColor",        CfgEntry::FLOAT4, g_espOofSoundColor, 0 },
    { "espOofSoundDuration",     CfgEntry::FLOAT,  &g_espOofSoundDuration, 0 },
    { "espOofSoundFade",         CfgEntry::BOOL,   &g_espOofSoundFade, 0 },
    { "espOofStyle",             CfgEntry::INT,    &g_espOofStyle, 0 },
    { "espOofWidth",             CfgEntry::FLOAT,  &g_espOofWidth, 0 },
    { "espOofHeight",            CfgEntry::FLOAT,  &g_espOofHeight, 0 },
    { "espOofDistance",          CfgEntry::INT,    &g_espOofDistance, 0 },
    { "espOofAvatar",            CfgEntry::BOOL,   &g_espOofAvatar, 0 },
    { "espOofAspect",            CfgEntry::BOOL,   &g_espOofAspect, 0 },
    { "espSoundPings",           CfgEntry::BOOL,   &g_espSoundPings, 0 },
    { "espSoundColor",           CfgEntry::FLOAT4, g_espSoundColor, 0 },
    { "espC4",                   CfgEntry::BOOL,   &g_espC4, 0 },
    { "espC4Icon",               CfgEntry::BOOL,   &g_espC4Icon, 0 },
    { "espC4Timer",              CfgEntry::BOOL,   &g_espC4Timer, 0 },
    { "espC4Color",              CfgEntry::FLOAT4, g_espC4Color, 0 },
    { "espC4DefuseColor",        CfgEntry::FLOAT4, g_espC4DefuseColor, 0 },
    { "espC4FailColor",          CfgEntry::FLOAT4, g_espC4FailColor, 0 },
    { "espC4Bar",                CfgEntry::BOOL,   &g_espC4Bar, 0 },
    { "espC4BarPosition",        CfgEntry::INT,    &g_espC4BarPosition, 0 },
    { "espWorldEnable",          CfgEntry::BOOL,   &g_espWorldEnable, 0 },
    { "espAmmoPacks",            CfgEntry::BOOL,   &g_espAmmoPacks, 0 },
    { "espDroppedWeapons",       CfgEntry::BOOL,   &g_espDroppedWeapons, 0 },
    { "espProjectiles",          CfgEntry::BOOL,   &g_espProjectiles, 0 },
    { "espProjPath",             CfgEntry::BOOL,   &g_espProjPath, 0 },
    { "espProjPathColor",        CfgEntry::FLOAT4, g_espProjPathColor, 0 },
    { "espItemColor",            CfgEntry::FLOAT4, g_espItemColor, 0 },
    { "espProjColor",            CfgEntry::FLOAT4, g_espProjColor, 0 },

    { "chamsEnable",       CfgEntry::BOOL,   &g_chamsEnable, 0 },
    { "chamsEnemy",        CfgEntry::BOOL,   &g_chamsEnemy, 0 },
    { "chamsTeam",         CfgEntry::BOOL,   &g_chamsTeam, 0 },
    { "chamsEnemyColor",   CfgEntry::FLOAT4, g_chamsEnemyColor, 0 },
    { "chamsTeamColor",    CfgEntry::FLOAT4, g_chamsTeamColor, 0 },

    { "glowEnable",        CfgEntry::BOOL,   &g_glowEnable, 0 },
    { "glowEnemy",         CfgEntry::BOOL,   &g_glowEnemy, 0 },
    { "glowTeam",          CfgEntry::BOOL,   &g_glowTeam, 0 },
    { "glowEnemyColor",    CfgEntry::FLOAT4, g_glowEnemyColor, 0 },
    { "glowTeamColor",     CfgEntry::FLOAT4, g_glowTeamColor, 0 },
    // "glowBuildings"/"glowBuildingColor" removed: no buildings in CSS
    { "glowAlpha",         CfgEntry::FLOAT,  &g_glowAlpha, 0 },

    // 10-31 chams/glow rework (aim/chams.cpp)
    { "chamsLocalEnable",       CfgEntry::BOOL,   &g_chamsLocalEnable, 0 },
    { "chamsRagdolls",          CfgEntry::BOOL,   &g_chamsRagdolls, 0 },
    { "chamsSoundDuration",     CfgEntry::FLOAT,  &g_chamsSoundDuration, 0 },
    { "chamsSoundFade",         CfgEntry::BOOL,   &g_chamsSoundFade, 0 },
    { "chamsSoundMinVolume",    CfgEntry::FLOAT,  &g_chamsSoundMinVolume, 0 },
    { "chamsSoundMaxDistance",  CfgEntry::FLOAT,  &g_chamsSoundMaxDistance, 0 },
    { "chamsTwoToneThreshold",  CfgEntry::FLOAT,  &g_chamsTwoToneThreshold, 0 },
    { "chamsTwoToneEdge",       CfgEntry::FLOAT,  &g_chamsTwoToneEdge, 0 },
    { "chamsVisible",          CfgEntry::STR,    g_chamsVisibleCfg, sizeof(g_chamsVisibleCfg) },
    { "chamsOccluded",          CfgEntry::STR,    g_chamsOccludedCfg, sizeof(g_chamsOccludedCfg) },
    { "chamsSound",             CfgEntry::STR,    g_chamsSoundCfg, sizeof(g_chamsSoundCfg) },
    { "chamsTeamVisible",       CfgEntry::STR,    g_chamsTeamVisibleCfg, sizeof(g_chamsTeamVisibleCfg) },
    { "chamsTeamOccluded",      CfgEntry::STR,    g_chamsTeamOccludedCfg, sizeof(g_chamsTeamOccludedCfg) },
    { "chamsWeapon",            CfgEntry::STR,    g_chamsWeaponCfg, sizeof(g_chamsWeaponCfg) },
    // "chamsUber"/"chamsCloak"/"chamsTeamUber"/"chamsTeamCloak" removed: TF2 uber/cloak chams
    { "glowVisColor",           CfgEntry::FLOAT4, g_glowVisColor, 0 },
    { "glowInvisColor",         CfgEntry::FLOAT4, g_glowInvisColor, 0 },
    { "glowLegacy",             CfgEntry::BOOL,   &g_glowLegacy, 0 },
    { "glowLegacyStyle",        CfgEntry::INT,    &g_glowLegacyStyle, 0 },
    { "glowNormalBlend",        CfgEntry::BOOL,   &g_glowNormalBlend, 0 },
    { "glowThickness",          CfgEntry::FLOAT,  &g_glowThickness, 0 },
    { "glowGradient",           CfgEntry::BOOL,   &g_glowGradient, 0 },
    { "glowVisOuterColor",      CfgEntry::FLOAT4, g_glowVisOuterColor, 0 },
    { "glowInvisOuterColor",    CfgEntry::FLOAT4, g_glowInvisOuterColor, 0 },
    { "glowGradientInner",      CfgEntry::FLOAT,  &g_glowGradientInner, 0 },
    { "glowWave",               CfgEntry::BOOL,   &g_glowWave, 0 },
    { "glowWaveColor",          CfgEntry::FLOAT4, g_glowWaveColor, 0 },
    { "glowWaveSpeed",          CfgEntry::FLOAT,  &g_glowWaveSpeed, 0 },
    { "glowWaveSize",           CfgEntry::FLOAT,  &g_glowWaveSize, 0 },
    { "glowWaveStrength",       CfgEntry::FLOAT,  &g_glowWaveStrength, 0 },
    { "glowPulse",              CfgEntry::BOOL,   &g_glowPulse, 0 },
    { "glowPulseSpeed",         CfgEntry::FLOAT,  &g_glowPulseSpeed, 0 },
    { "glowPulseMin",           CfgEntry::FLOAT,  &g_glowPulseMin, 0 },

    { "chEnable",         CfgEntry::BOOL,   &g_chEnable, 0 },
    { "chAntialias",      CfgEntry::BOOL,   &g_chAntialias, 0 },
    { "chAdditive",       CfgEntry::BOOL,   &g_chAdditive, 0 },   // kept: old cfgs' additive
    { "chBlendMode",      CfgEntry::INT,    &g_chBlendMode, 0 },
    { "chColor",          CfgEntry::FLOAT4, g_chColor, 0 },
    { "chThickness",      CfgEntry::FLOAT,  &g_chThickness, 0 },
    { "chLength",         CfgEntry::FLOAT,  &g_chLength, 0 },
    { "chGap",            CfgEntry::FLOAT,  &g_chGap, 0 },
    { "chAspect",         CfgEntry::FLOAT,  &g_chAspect, 0 },
    { "chGapAspect",      CfgEntry::FLOAT,  &g_chGapAspect, 0 },
    { "chDot",            CfgEntry::BOOL,   &g_chDot, 0 },
    { "chDotCircle",      CfgEntry::BOOL,   &g_chDotCircle, 0 },
    { "chOutline",        CfgEntry::BOOL,   &g_chOutline, 0 },
    { "chOutlineColor",   CfgEntry::FLOAT4, g_chOutlineColor, 0 },
    { "chOutlineThick",   CfgEntry::FLOAT,  &g_chOutlineThick, 0 },
    { "chSidesOnly",       CfgEntry::BOOL,  &g_chSidesOnly, 0 },
    { "chSideL",           CfgEntry::BOOL,  &g_chSideL, 0 },
    { "chSideR",           CfgEntry::BOOL,  &g_chSideR, 0 },
    { "chSideT",           CfgEntry::BOOL,  &g_chSideT, 0 },
    { "chSideB",           CfgEntry::BOOL,  &g_chSideB, 0 },
    { "chOutlineOnLines",  CfgEntry::BOOL,  &g_chOutlineOnLines, 0 },
    { "chOutlineOnDot",    CfgEntry::BOOL,  &g_chOutlineOnDot, 0 },
    { "chOutlineSidesOnly",CfgEntry::BOOL,  &g_chOutlineSidesOnly, 0 },
    { "chOutlineSideL",    CfgEntry::BOOL,  &g_chOutlineSideL, 0 },
    { "chOutlineSideR",    CfgEntry::BOOL,  &g_chOutlineSideR, 0 },
    { "chOutlineSideT",    CfgEntry::BOOL,  &g_chOutlineSideT, 0 },
    { "chOutlineSideB",    CfgEntry::BOOL,  &g_chOutlineSideB, 0 },
    { "chOutlineBlur",       CfgEntry::BOOL,  &g_chOutlineBlur, 0 },
    { "chOutlineBlurRadius", CfgEntry::FLOAT, &g_chOutlineBlurRadius, 0 },
    { "chDynamic",    CfgEntry::BOOL,  &g_chDynamic, 0 },
    { "chDynamicMin", CfgEntry::FLOAT, &g_chDynamicMin, 0 },
    { "chDynamicMax", CfgEntry::FLOAT, &g_chDynamicMax, 0 },
    { "chDynamicRef", CfgEntry::FLOAT, &g_chDynamicRef, 0 },
    { "chGradient",    CfgEntry::BOOL,   &g_chGradient, 0 },
    { "chGradCenter",  CfgEntry::FLOAT4, g_chGradCenter, 0 },
    { "chGradOuter",   CfgEntry::FLOAT4, g_chGradOuter, 0 },
    { "chGradStart",   CfgEntry::FLOAT,  &g_chGradStart, 0 },
    { "chGradEnd",     CfgEntry::FLOAT,  &g_chGradEnd, 0 },
    { "chTaper",      CfgEntry::BOOL,  &g_chTaper, 0 },
    { "chTaperNear",  CfgEntry::FLOAT, &g_chTaperNear, 0 },
    { "chTaperFar",   CfgEntry::FLOAT, &g_chTaperFar, 0 },
    { "chDotCustomColor", CfgEntry::BOOL,   &g_chDotCustomColor, 0 },
    { "chDotColor",       CfgEntry::FLOAT4, g_chDotColor, 0 },
    { "chRotation",       CfgEntry::FLOAT,  &g_chRotation, 0 },
    { "forceCrosshair",   CfgEntry::BOOL,   &g_forceCrosshair, 0 },

    { "sniperChEnable",    CfgEntry::BOOL,   &g_sniperChEnable, 0 },
    { "sniperChStyle",     CfgEntry::INT,    &g_sniperChStyle, 0 },
    { "sniperChColor",        CfgEntry::FLOAT4, g_sniperChColor, 0 },
    { "sniperChOutlineColor", CfgEntry::FLOAT4, g_sniperChOutlineColor, 0 },
    { "sniperChDotColor",     CfgEntry::FLOAT4, g_sniperChDotColor, 0 },
    { "sniperChSize",      CfgEntry::FLOAT,  &g_sniperChSize, 0 },
    { "sniperChThickness", CfgEntry::FLOAT,  &g_sniperChThickness, 0 },
    { "sniperChGap",       CfgEntry::FLOAT,  &g_sniperChGap, 0 },
    { "sniperChDot",       CfgEntry::BOOL,   &g_sniperChDot, 0 },
    { "sniperChT",         CfgEntry::BOOL,   &g_sniperChT, 0 },
    { "sniperChOutline",            CfgEntry::BOOL,  &g_sniperChOutline, 0 },
    { "sniperChOutlineThickness",   CfgEntry::FLOAT, &g_sniperChOutlineThickness, 0 },
    { "sniperChInterwebzSize",        CfgEntry::FLOAT, &g_sniperChInterwebzSize, 0 },
    { "sniperChInterwebzGap",         CfgEntry::INT,   &g_sniperChInterwebzGap, 0 },
    { "sniperChInterwebzThickness",   CfgEntry::INT,   &g_sniperChInterwebzThickness, 0 },
    { "sniperChInterwebzArmCorners",  CfgEntry::BOOL,  &g_sniperChInterwebzArmCorners, 0 },
    { "sniperChInterwebzDotCorners",  CfgEntry::BOOL,  &g_sniperChInterwebzDotCorners, 0 },

    { "chCsgoEnable",     CfgEntry::BOOL,   &g_chCsgoEnable, 0 },
    { "chCsgoStyle",      CfgEntry::INT,    &g_chCsgoStyle, 0 },
    { "chCsgoColor",      CfgEntry::FLOAT4, g_chCsgoColor, 0 },
    { "chCsgoGap",        CfgEntry::FLOAT,  &g_chCsgoGap, 0 },
    { "chCsgoScale",      CfgEntry::FLOAT,  &g_chCsgoScale, 0 },
    { "chCsgoCrisp",      CfgEntry::BOOL,   &g_chCsgoCrisp, 0 },
    { "chCsgoLinear",     CfgEntry::BOOL,   &g_chCsgoLinear, 0 },
    { "chCsgoAdditive",   CfgEntry::BOOL,   &g_chCsgoAdditive, 0 },
    { "chCsgoBlack",      CfgEntry::FLOAT,  &g_chCsgoBlack, 0 },
    { "chCsgoVelSens",    CfgEntry::FLOAT,  &g_chCsgoVelSens, 0 },
    { "chCsgoSmooth",     CfgEntry::FLOAT,  &g_chCsgoSmooth, 0 },
    { "chCsgoArcs",       CfgEntry::BOOL,   &g_chCsgoArcs, 0 },
    { "chCsgoPips",       CfgEntry::BOOL,   &g_chCsgoPips, 0 },
    { "chCsgoAspect",     CfgEntry::FLOAT,  &g_chCsgoAspect, 0 },
    { "chCsgoGapAspect",  CfgEntry::FLOAT,  &g_chCsgoGapAspect, 0 },
    { "csXSize",          CfgEntry::FLOAT,  &g_csXSize, 0 },
    { "csXThick",         CfgEntry::FLOAT,  &g_csXThick, 0 },
    { "csXGap",           CfgEntry::FLOAT,  &g_csXGap, 0 },
    { "csXDot",           CfgEntry::BOOL,   &g_csXDot, 0 },
    { "csXOutline",       CfgEntry::BOOL,   &g_csXOutline, 0 },
    { "csXOutlineTh",     CfgEntry::FLOAT,  &g_csXOutlineTh, 0 },
    { "csXT",             CfgEntry::BOOL,   &g_csXT, 0 },
    { "csXAlpha",         CfgEntry::INT,    &g_csXAlpha, 0 },
    { "csXUseAlpha",      CfgEntry::BOOL,   &g_csXUseAlpha, 0 },
    { "csXSplitDist",     CfgEntry::FLOAT,  &g_csXSplitDist, 0 },
    { "csXSplitRatio",    CfgEntry::FLOAT,  &g_csXSplitRatio, 0 },
    { "csXInnerMod",      CfgEntry::FLOAT,  &g_csXInnerMod, 0 },
    { "csXOuterMod",      CfgEntry::FLOAT,  &g_csXOuterMod, 0 },

    { "pureBypass",       CfgEntry::BOOL,   &g_pureBypass, 0 },
    { "cheatsBypass",     CfgEntry::BOOL,   &g_cheatsBypass, 0 },
    // "f2pChatBypass" removed: TF2 GC-only, mmfix.cpp (its reader) already gone (globals.h:560)

    // "regionEnable"/"regionMask" (TF2 GC matchmaking datacenter select), "netSettings*"/"ng*"
    // (engine CNetGraphPanel hooks, no botox equivalent) removed: see netregion.h.

    { "equipRegionUnlock", CfgEntry::BOOL,  &g_equipRegionUnlock, 0 },

    // ---- unit B: detections ----
    { "detections",         CfgEntry::BOOL,   &g_detections, 0 },
    { "detectionStackEb",   CfgEntry::BOOL,   &g_detectionStackEb, 0 },
    { "detectionTypes",     CfgEntry::INT,    &g_detectionTypes, 0 },
    { "detectionLabel",     CfgEntry::STR,    g_detectionLabel, sizeof(g_detectionLabel) },

    // ---- unit B: pixelsurf lines ----
    { "psLinesEnable",     CfgEntry::BOOL,   &g_psLinesEnable, 0 },
    { "psLinesColor",      CfgEntry::FLOAT4, g_psLinesColor, 0 },
    { "psLinesStyle",      CfgEntry::INT,    &g_psLinesStyle, 0 },
    { "psLinesThickness",  CfgEntry::FLOAT,  &g_psLinesThickness, 0 },

    // ---- unit B: movement recorder ----
    { "recEnable",          CfgEntry::BOOL, &g_recEnable, 0 },
    { "recShowLine",        CfgEntry::BOOL, &g_recShowLine, 0 },
    { "recPreviewLine",     CfgEntry::BOOL, &g_recPreviewLine, 0 },
    { "recRender",          CfgEntry::BOOL, &g_recRender, 0 },
    { "recPosition",        CfgEntry::INT,  &g_recPosition, 0 },
    { "recLockVa",          CfgEntry::BOOL, &g_recLockVa, 0 },
    { "recLockGoingToStart",CfgEntry::BOOL, &g_recLockGoingToStart, 0 },
    { "recClipper",         CfgEntry::BOOL, &g_recClipper, 0 },
    { "recClipperBox",      CfgEntry::BOOL, &g_recClipperBox, 0 },
    { "recClipSeconds",     CfgEntry::FLOAT,&g_recClipSeconds, 0 },
    { "recStopOnMove",      CfgEntry::BOOL, &g_recStopOnMove, 0 },
    { "recForceWeapon",     CfgEntry::BOOL, &g_recForceWeapon, 0 },
    { "recYaw",             CfgEntry::INT,  &g_recYaw, 0 },
    { "recSpinSpeed",       CfgEntry::FLOAT,&g_recSpinSpeed, 0 },
    { "recSmoothStart",     CfgEntry::FLOAT,&g_recSmoothStart, 0 },
    { "recSmoothEnd",       CfgEntry::FLOAT,&g_recSmoothEnd, 0 },
    { "recKeyStartRec",     CfgEntry::INT,  &g_recKeyStartRec, 0 },
    { "recKeyStopRec",      CfgEntry::INT,  &g_recKeyStopRec, 0 },
    { "recKeySave",         CfgEntry::INT,  &g_recKeySave, 0 },
    { "recKeyStartPlay",    CfgEntry::INT,  &g_recKeyStartPlay, 0 },
    { "recKeyStopPlay",     CfgEntry::INT,  &g_recKeyStopPlay, 0 },
    { "recKeyClear",        CfgEntry::INT,  &g_recKeyClear, 0 },
    { "recKeyClip",         CfgEntry::INT,  &g_recKeyClip, 0 },
    // 11-01 port round: one row file per unit (cfg/units.h declares the vars)
#include "cfg/u_ind.inc"
#include "cfg/u_esp.inc"
#include "cfg/u_world.inc"
#include "cfg/u_screen.inc"
#include "cfg/u_post.inc"
#include "cfg/u_hud.inc"
#include "cfg/u_wm.inc"
#include "cfg/u_spec.inc"
#include "cfg/u_misc.inc"
#include "cfg/u_game.inc"
#include "cfg/u_btchams.inc"
#include "cfg/u_points.inc"
#include "cfg/u_mvcore.inc"
#include "cfg/u_mveb.inc"
#include "cfg/u_mvps.inc"
#include "cfg/u_mvtb.inc"
#include "cfg/u_mvtools.inc"
#include "cfg/u_movx.inc"
#include "cfg/u_vistab.inc"
#include "cfg/u_misctab.inc"
#include "cfg/u_visx.inc"
};

// botox g_config.folder(visuals): visuals files live one folder down
static constexpr const char* kVisualsDir = "C:\\botox-css\\visuals";

const char* ConfigFolder(bool visuals) { return visuals ? kVisualsDir : CONFIG_DIR; }

void SaveConfig(const char* path, bool visuals)
{
    _mkdir(CONFIG_DIR);
    if (visuals) _mkdir(kVisualsDir);
    FILE* f = fopen(path, "w");
    if (!f) return;

    for (const auto& e : kEntries)
    {
        if (visuals && !IsVisualCfgKey(e.key)) continue;
        switch (e.type)
        {
        case CfgEntry::BOOL:
            fprintf(f, "%s=%d\n", e.key, *(bool*)e.ptr ? 1 : 0);
            break;
        case CfgEntry::INT:
            fprintf(f, "%s=%d\n", e.key, *(int*)e.ptr);
            break;
        case CfgEntry::FLOAT:
            fprintf(f, "%s=%.4f\n", e.key, *(float*)e.ptr);
            break;
        case CfgEntry::FLOAT4: {
            float* c = (float*)e.ptr;
            fprintf(f, "%s=%.4f %.4f %.4f %.4f\n", e.key, c[0], c[1], c[2], c[3]);
            break;
        }
        case CfgEntry::STR:
            fprintf(f, "%s=%s\n", e.key, (char*)e.ptr);
            break;
        case CfgEntry::CURVE: {
            const CcCurve* c = (const CcCurve*)e.ptr;
            const int n = (c->n < 2) ? 2 : (c->n > k_ccMaxPts ? k_ccMaxPts : c->n);
            fprintf(f, "%s=%d", e.key, n);
            for (int i = 0; i < n; ++i) fprintf(f, ";%.4f,%.4f", c->x[i], c->y[i]);
            fputc('\n', f);
            break;
        }
        }
    }

    // botox: aimbot vars are never in a visuals file. skinchanger removed entirely (11-03).
    if (!visuals)
    {
        Names_SaveCfg(f);       // custom names: cycle lists + aliases

        // Aimbot vars (src/hacks/aim/aim.cpp). Appended after dih's own entries;
        // they are keyed by their registered "Vars::..." name so they cannot collide with kEntries.
        Aim_SaveVars(f);
    }

    fclose(f);
    DebugLog("config saved%s: %s", visuals ? " (visuals)" : "", path);
}

// rows of the coldtires-era indicator port whose shape changed in the botox port. Never written
// now; read once so an old cfg keeps its picks. Old show / label slots were TB AIR WC PS AST EB JB
// ST EJ LJ MJ (bit / index 0..10)
static bool Ind_LoadLegacy(const char* key, const char* val)
{
    static constexpr int kShow[11]  = { KI_TB, KI_AS, KI_WC, KI_PS, KI_PSA, KI_EB, KI_JB, KI_ST, KI_EJ, KI_LJ, KI_MJ };
    static constexpr int kLabel[11] = { LBL_TB, LBL_AIR, LBL_WC, LBL_PS, LBL_AST, LBL_EB, LBL_JB, LBL_AS, LBL_EJ, LBL_LJ, LBL_MJ };
    const auto f4 = [&](float* c) { sscanf(val, "%f %f %f %f", &c[0], &c[1], &c[2], &c[3]); };

    if (strcmp(key, "miMitosis") == 0) { if (atoi(val)) g_kiStyle = IND_STYLE_MITOSIS; return true; }
    if (strcmp(key, "miOffsetY") == 0) { g_kiPosition = (int)atof(val); return true; }
    if (strcmp(key, "miScale") == 0)   { g_kiScale = (float)atof(val); return true; }
    if (strcmp(key, "miIdleColor") == 0)   { f4(g_kiColor); return true; }
    if (strcmp(key, "miActiveColor") == 0) { f4(g_kiColorSuccess); return true; }
    if (strcmp(key, "viOffsetY") == 0) { g_viPadding = (int)atof(val); return true; }
    if (strcmp(key, "miShowMask") == 0)
    {
        const int old = atoi(val);
        g_kiShow = 0;
        for (int i = 0; i < 11; ++i)
            if (old & (1 << i)) g_kiShow |= 1 << kShow[i];
        return true;
    }
    if (strncmp(key, "miLabel", 7) == 0)
    {
        const int i = atoi(key + 7);
        if (i >= 0 && i < 11) strncpy_s(g_kiLabels[kLabel[i]], val, _TRUNCATE);
        return true;
    }
    return strcmp(key, "miOnlyActive") == 0;
}

// name -> "<ConfigFolder>\<name>.cfg". Path chars stripped so a name can't leave the folder.
// Returns false if nothing usable left.
static bool CfgPathFromName(const char* name, char* out, size_t outSz, bool visuals)
{
    char clean[128]; size_t n = 0;
    for (const char* p = name; *p && n + 1 < sizeof(clean); ++p)
        if (!strchr("\\/:*?\"<>|", *p) && (unsigned char)*p >= 32) clean[n++] = *p;
    while (n && (clean[n-1] == ' ' || clean[n-1] == '.')) --n;   // Windows drops these anyway
    clean[n] = '\0';
    if (n > 4 && _stricmp(clean + n - 4, ".cfg") == 0) clean[n -= 4] = '\0';
    if (!n) return false;
    snprintf(out, outSz, "%s\\%s.cfg", ConfigFolder(visuals), clean);
    return true;
}

std::vector<std::string> ListConfigs(bool visuals)
{
    std::vector<std::string> out;
    char pat[MAX_PATH];
    snprintf(pat, sizeof(pat), "%s\\*.cfg", ConfigFolder(visuals));
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back(fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end());
    return out;
}

void SaveConfigNamed(const char* name, bool visuals)
{
    char path[MAX_PATH];
    if (CfgPathFromName(name, path, sizeof(path), visuals)) SaveConfig(path, visuals);
}

void LoadConfigNamed(const char* name, bool visuals)
{
    char path[MAX_PATH];
    if (CfgPathFromName(name, path, sizeof(path), visuals)) LoadConfig(path, visuals);
}

bool DeleteConfigNamed(const char* name, bool visuals)
{
    char path[MAX_PATH];
    if (!CfgPathFromName(name, path, sizeof(path), visuals)) return false;
    const bool ok = DeleteFileA(path) != 0;
    DebugLog("config %s: %s (err %lu)", ok ? "removed" : "remove FAILED", path, ok ? 0ul : GetLastError());
    return ok;
}

void LoadConfig(const char* path, bool visuals)
{
    FILE* f = fopen(path, "r");
    if (!f) return;

    char line[512];
    while (fgets(line, sizeof(line), f))
    {
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = line;
        char* val = eq + 1;
        // strip trailing newline
        size_t vl = strlen(val);
        while (vl > 0 && (val[vl-1] == '\n' || val[vl-1] == '\r')) val[--vl] = '\0';

        // botox: a full cfg dropped in the visuals folder still only touches visuals
        if (visuals)
        {
            if (!IsVisualCfgKey(key)) continue;
        }

        // Aimbot var? Its key is namespaced ("Vars::..."), so it can never match one of
        // dih's own keys, and consuming it here keeps it out of the kEntries walk.
        if (Aim_LoadVar(key, val)) continue;
        // skinchanger ("enableSkins" / "skin_<def>_<field>" / shareNet/Send/Recv) removed 11-03
        if (Names_LoadCfg(key, val)) continue;
        if (Ind_LoadLegacy(key, val)) continue;
        // esp rows from before the botox players port: one box combo (0 off, 1 corner, 2 full)
        // and one "health" row that drew bar + number. Never written now, read once into the new rows
        if (strcmp(key, "espBoxStyle") == 0) { const int v = atoi(val); g_espBox = v != 0; g_espBoxCorner = v == 1; continue; }
        if (strcmp(key, "espHealth") == 0)   { g_espHealthBar = g_espHealthText = atoi(val) != 0; continue; }

        for (const auto& e : kEntries)
        {
            if (strcmp(key, e.key) != 0) continue;
            switch (e.type)
            {
            case CfgEntry::BOOL:  *(bool*)e.ptr  = (atoi(val) != 0); break;
            case CfgEntry::INT:   *(int*)e.ptr   = atoi(val);        break;
            case CfgEntry::FLOAT: *(float*)e.ptr = (float)atof(val); break;
            case CfgEntry::FLOAT4: {
                float* c = (float*)e.ptr;
                sscanf(val, "%f %f %f %f", &c[0], &c[1], &c[2], &c[3]);
                break;
            }
            case CfgEntry::STR:
                strncpy((char*)e.ptr, val, e.size - 1);
                ((char*)e.ptr)[e.size - 1] = '\0';
                break;
            case CfgEntry::CURVE:
                if (!ParseCurve(val, *(CcCurve*)e.ptr))
                {
                    CcResetCurve(*(CcCurve*)e.ptr);
                    DebugLog("config: %s did not parse -- curve reset to identity", e.key);
                }
                break;
            }
            break;
        }
    }
    fclose(f);

    // ---- post-load range clamps -------------------------------------------------------
    // a saved value below a slider floor survives forever -- load is a plain atof
    // round-trip and every later save writes it straight back. That is how tbEngageDist sat
    // at 8 for four builds while its floor moved to 32, which read as a code bug. Only knobs
    // whose out-of-range value DISABLES a feature are listed; a plain preference is left
    // exactly as saved. whyLo / whyHi name what that extreme breaks, and a clamp that has one
    // logs it -- a silently corrected cfg is how the case above went unnoticed for so long.
    // kNoLo / kNoHi = that end was never bounded. Do not invent a bound for it.
    constexpr float kNoLo = -FLT_MAX, kNoHi = FLT_MAX;

    // dpiScale was a fraction (0.5-2) before the botox port made it a percent (25-200). A
    // percent that small cannot be saved by the slider, so <= 2 is always an old cfg.
    if (g_dpiScale > 0.f && g_dpiScale <= 2.f)
    {
        DebugLog("config: dpiScale=%.3f is the old fraction -- read as %.0f %%", g_dpiScale, g_dpiScale * 100.f);
        g_dpiScale *= 100.f;
    }

    struct FloatClamp { float* p; float lo, hi; const char* name; const char* whyLo; const char* whyHi; };
    static const FloatClamp kFloatClamps[] = {
        // tbHugRange/tbRampNz/tbParkSpeed/tbParkLead/tbPredictReach clamps deleted 11-03 with
        // their globals (TF2-port-only settings, folded to fixed constants -- see MVTB report).
        { &g_tbReach,      0.f,   48.f,   "tbReach",      nullptr, nullptr },   // botox range 0-48, was 1-kNoHi
        { &g_rcPathThick,  1.f,   8.f,    "rcPathThick",  nullptr, nullptr },
        { &g_tbScanStep,   1.f,   kNoHi,  "tbScanStep",   nullptr, nullptr },
        // botox air_stuck: the scan reach is clamped 0-32 in the feature too; the clock is a
        // share of one tick, 90 is where botox's own c_sim_budget clamps
        { &g_asExtraReach, 0.f,   32.f,   "asExtraReach", nullptr, nullptr },
        { &g_asBudget,     5.f,   90.f,   "asBudget",     nullptr, nullptr },   // botox slider 5-90, was 10-90
        // psAssistRadius/psAssistReuseDelay/psGroundHelpAngle/psLaunchZSlack rows removed --
        // PSPORT round 2 11-04, those globals are gone (botox's own pixelsurf_assist.cpp has no
        // such tuning: points carry their own radius, ground help's angle limit is
        // g_psGroundAngleLimit 0-45 def 2.5, no launch-z-slack guard exists in botox at all)
        // the pad is the drawn footprint of the bug tick, not a hitbox -- both ends are taste
        { &g_edgeBugVizSize, 2.f, 64.f, "edgeBugVizSize", nullptr, nullptr },
        { &g_abReach, 16.f, 512.f, "abReach", nullptr, nullptr },
        { &g_abMinGain, 0.f, 500.f, "abMinGain", nullptr, nullptr },
        { &g_edgeBugPredictTime, 0.02f, 2.f, "edgeBugPredictTime", nullptr, nullptr },
        { &g_edgeBugStrafeAngleMax, 0.f, 180.f, "edgeBugMaxBend", nullptr, nullptr },
        { &g_edgeBugLockStrength, 0.f, 100.f, "edgeBugLockStrength", nullptr, nullptr },
        { &g_edgeBugDelAngleLimit, 0.f, 180.f, "edgeBugDelAngleLimit", nullptr, nullptr },
        { &g_edgeBugDnaScan, 0.f, 1.f, "edgeBugDnaScan", nullptr, nullptr },
        { &g_edgeBugDnaAngleLimit, 0.f, 45.f, "edgeBugDnaAngleLimit", nullptr, nullptr },
        { &g_edgeBugHwLockAmt, 0.f, 1.f, "edgeBugHwLockAmt", nullptr, nullptr },
        { &g_edgeBugHwStrafeDeltaMax, 0.1f, 100.f, "edgeBugHwStrafeDeltaMax", nullptr, nullptr },
        // Transparent viewmodel: 0 is a real choice (invisible), but outside 0..1 is a blend
        // the engine would either clip or read as garbage.
        { &g_vmAlpha,      0.f,   1.f,    "vmAlpha",      nullptr, nullptr },
        // Lag resolver: each of the three has an extreme that turns the feature off while the
        // checkbox still reads on.
        { &g_lrSmoothMs,   15.f,  200.f,  "lrSmoothMs",   "there is no smoothing left", nullptr },
        { &g_lrExtrapMs,   40.f,  400.f,  "lrExtrapMs",   "stalls re-freeze", nullptr },
        { &g_lrMaxErr,     8.f,   128.f,  "lrMaxErr",     "it snaps every frame", nullptr },
        // Colour correction: only the values whose extreme is a BROKEN picture, not a taste.
        // Gains, exposure, contrast and the tone lifts are left exactly as saved.
        { &g_ccGamma,      0.01f, 10.f,   "ccGamma",      "the shader would divide by zero", nullptr },
        { &g_ccLevInBlack, 0.f,   0.99f,  "ccLevInBlack", nullptr, nullptr },
        { &g_ccLevInWhite, 0.f,   1.f,    "ccLevInWhite", nullptr, nullptr },
        { &g_ccLevOutBlack,0.f,   1.f,    "ccLevOutBlack",nullptr, nullptr },
        { &g_ccLevOutWhite,0.f,   1.f,    "ccLevOutWhite",nullptr, nullptr },
        { &g_ccSat,        0.f,   kNoHi,  "ccSat",        nullptr, nullptr },
        { &g_ccGainR,      0.f,   kNoHi,  "ccGainR",      nullptr, nullptr },
        { &g_ccGainG,      0.f,   kNoHi,  "ccGainG",      nullptr, nullptr },
        { &g_ccGainB,      0.f,   kNoHi,  "ccGainB",      nullptr, nullptr },
        // Weather. Every cfg written before the defaults were fixed holds 0 in all six snow
        // keys -- the whole "snow does nothing" report -- and three of those zeros kill it on
        // their own: speed divides ComputeEmissionArea's fallTime (NaN box, every particle
        // culled), length scales the tracer delta to nothing (Tracer_ComputeVerts drops every
        // flake on sqLength < 1e-3), radius is the emission box edge (0 area = 0 particles).
        // alpha 0 is legal on purpose, but 0 for BOTH modes only came from those zero defaults.
        { &g_rainAlpha,    0.05f, 1.f,    "rainAlpha",    "weather draws nothing", nullptr },
        { &g_rainSpeed,    50.f,  2000.f, "rainSpeed",    "weather draws nothing", nullptr },
        { &g_rainWidth,    0.05f, 5.f,    "rainWidth",    "weather draws nothing", nullptr },
        { &g_rainLength,   0.01f, 1.f,    "rainLength",   "weather draws nothing", nullptr },
        { &g_rainRadius,   256.f, 5000.f, "rainRadius",   "weather draws nothing", nullptr },
        { &g_rainSideVel,  0.f,   500.f,  "rainSideVel",  nullptr, nullptr },  // 0 = straight down, still rains
        { &g_snowAlpha,    0.05f, 1.f,    "snowAlpha",    "weather draws nothing", nullptr },
        { &g_snowSpeed,    20.f,  500.f,  "snowSpeed",    "weather draws nothing", nullptr },
        { &g_snowWidth,    0.05f, 10.f,   "snowWidth",    "weather draws nothing", nullptr },
        { &g_snowLength,   0.01f, 1.f,    "snowLength",   "weather draws nothing", nullptr },
        { &g_snowRadius,   256.f, 5000.f, "snowRadius",   "weather draws nothing", nullptr },
        { &g_snowSideVel,  0.f,   100.f,  "snowSideVel",  nullptr, nullptr },
        { &g_snowDensity,  0.05f, 1.f,    "snowDensity",  "weather draws nothing", nullptr },
    };

    struct IntClamp { int* p; int lo, hi; const char* name; const char* whyLo; const char* whyHi; };
    static const IntClamp kIntClamps[] = {
        // tbParkTicks/tbPredictTicks int-clamps deleted 11-03 with their globals (TF2-port-only
        // settings) -- see MVTB report.
        // the EB search array is 132 entries (2.0s at TF2's 66 tick, where the old
        // 128 was 2.0s at CSGO's 64). Above it the search clamps silently and the menu lies.
        // bitmask: 0 both / 1 ducked / 2 standing / 3 both picked. Anything else matches no run
        { &g_edgeBugStance,      0,  3,   "edgeBugStance",
          "not a stance, so every attempt would be refused",
          "not a stance, so every attempt would be refused" },
        { &g_edgeBugDetectMode,  0,  2,   "edgeBugDetectMode",  nullptr, nullptr },
        { &g_edgeBugPaths,       1,  8,   "edgeBugPaths",       nullptr, nullptr },
        { &g_edgeBugLockType,    0,  1,   "edgeBugLockType",    nullptr, nullptr },
        { &g_edgeBugStyle,       0,  4,   "edgeBugStyle",       nullptr, nullptr },   // 4 = lobotomy v2 (u_movx)
        { &g_edgeBugLobDeltaType, 0, 3,   "edgeBugLobDeltaType", nullptr, nullptr },
        { &g_edgeBugLobTicks,    0,  126, "edgeBugLobTicks",    nullptr, nullptr },
        { &g_edgeBugHwTicks,     0,  32,  "edgeBugHwTicks",     nullptr, nullptr },
        { &g_edgeBugDelTicks,    0,  128, "edgeBugDelTicks",    nullptr, nullptr },
        { &g_edgeBugDelSearch,   1,  15,  "edgeBugDelSearch",   nullptr, nullptr },
        { &g_edgeBugDnaRange,    1,  10,  "edgeBugDnaRange",    nullptr, nullptr },
        // 0 look-ahead runs no sims at all, so the slider's own floor kills the hunt.
        // Past 32 it only spends the command sim pool on flight free fall already describes.
        { &g_pixelSurfPredictTicks, 1, 32, "pixelSurfPredictTicks",
          "no look-ahead runs, so pixel surf could never latch", nullptr },
        // psAssistMaxPoints row removed -- PSPORT round 2, that global is gone (botox caps
        // candidates at a fixed 4, pixelsurf_assist.cpp's own ps_max_points, not a user slider)
        // the mask is what a route is ALLOWED to press -- an empty one solves nothing
        // and blames the markers
        { &g_rcMoveMask,         1,  0x7F, "rcMoveBits",
          "no move is allowed, so every route is empty", nullptr },
        { &g_rcMaxShown,         1,  64,  "rcMaxShown",         nullptr, nullptr },
        { &g_rcStartMoves,       1,  0x7F, "rcStartMoves",
          "the starting jump may press nothing, so every route is empty", nullptr },
        { &g_rcDelayTicks,       0,  8,   "rcDelayTicks",       nullptr, nullptr },
        { &g_rcPathAlpha,        0,  255, "rcPathAlpha",        nullptr, nullptr },
    };

    for (const auto& c : kFloatClamps)
    {
        // NaN fails both comparisons below, so it would survive the clamp and poison every
        // formula downstream. atof("nan") in a hand-edited cfg is all it takes.
        if (!(*c.p == *c.p)) {
            const float safe = (c.lo > kNoLo) ? c.lo : ((c.hi < kNoHi) ? c.hi : 0.f);
            DebugLog("config: %s is not a number -- reset to %.3f", c.name, safe);
            *c.p = safe;
        }
        else if (*c.p < c.lo) {
            if (c.whyLo) DebugLog("config: %s=%.3f -- %s. clamped to %.3f", c.name, *c.p, c.whyLo, c.lo);
            *c.p = c.lo;
        } else if (*c.p > c.hi) {
            if (c.whyHi) DebugLog("config: %s=%.3f -- %s. clamped to %.3f", c.name, *c.p, c.whyHi, c.hi);
            *c.p = c.hi;
        }
    }
    for (const auto& c : kIntClamps)
    {
        if (*c.p < c.lo) {
            if (c.whyLo) DebugLog("config: %s=%d -- %s. clamped to %d", c.name, *c.p, c.whyLo, c.lo);
            *c.p = c.lo;
        } else if (*c.p > c.hi) {
            if (c.whyHi) DebugLog("config: %s=%d -- %s. clamped to %d", c.name, *c.p, c.whyHi, c.hi);
            *c.p = c.hi;
        }
    }

    // Cross-check, run on the values the table just settled: an input white at or under the
    // input black turns the whole frame into two colours.
    if (g_ccLevInWhite < g_ccLevInBlack + 0.01f)
    {
        DebugLog("config: ccLevInWhite=%.4f is at or under ccLevInBlack=%.4f (frame would be "
                 "two colours) -- lifted to %.4f", g_ccLevInWhite, g_ccLevInBlack,
                 g_ccLevInBlack + 0.01f);
        g_ccLevInWhite = g_ccLevInBlack + 0.01f;
    }

    DebugLog("config loaded%s: %s", visuals ? " (visuals)" : "", path);
}

// ==== generic access by key (lua port, config.h) ===========================================
static const CfgEntry* FindCfgEntry(const char* key)
{
    for (const auto& e : kEntries)
        if (strcmp(e.key, key) == 0)
            return &e;
    return nullptr;
}

const char* CfgEntry_TypeName(const char* key)
{
    const CfgEntry* e = FindCfgEntry(key);
    if (!e) return nullptr;
    switch (e->type) {
    case CfgEntry::BOOL:   return "bool";
    case CfgEntry::INT:    return "int";
    case CfgEntry::FLOAT:  return "float";
    case CfgEntry::FLOAT4: return "float4";
    case CfgEntry::STR:    return "string";
    case CfgEntry::CURVE:  return "curve";
    default:               return "unknown";
    }
}

bool CfgEntry_GetAsString(const char* key, std::string& out)
{
    const CfgEntry* e = FindCfgEntry(key);
    if (!e) return false;

    char buf[256];
    switch (e->type) {
    case CfgEntry::BOOL:   out = (*static_cast<bool*>(e->ptr)) ? "1" : "0"; return true;
    case CfgEntry::INT:    out = std::to_string(*static_cast<int*>(e->ptr)); return true;
    case CfgEntry::FLOAT:  _snprintf_s(buf, _TRUNCATE, "%g", *static_cast<float*>(e->ptr)); out = buf; return true;
    case CfgEntry::FLOAT4: {
        const float* f = static_cast<float*>(e->ptr);
        _snprintf_s(buf, _TRUNCATE, "%g,%g,%g,%g", f[0], f[1], f[2], f[3]);
        out = buf;
        return true;
    }
    case CfgEntry::STR:    out = static_cast<char*>(e->ptr); return true;
    default: return false;   // CURVE: no string form exposed to lua
    }
}

bool CfgEntry_SetFromString(const char* key, const char* value)
{
    const CfgEntry* e = FindCfgEntry(key);
    if (!e) return false;

    switch (e->type) {
    case CfgEntry::BOOL:   *static_cast<bool*>(e->ptr) = atoi(value) != 0; return true;
    case CfgEntry::INT:    *static_cast<int*>(e->ptr) = atoi(value); return true;
    case CfgEntry::FLOAT:  *static_cast<float*>(e->ptr) = static_cast<float>(atof(value)); return true;
    case CfgEntry::FLOAT4: {
        float* f = static_cast<float*>(e->ptr);
        float parsed[4] = { f[0], f[1], f[2], f[3] };
        if (sscanf_s(value, "%f,%f,%f,%f", &parsed[0], &parsed[1], &parsed[2], &parsed[3]) != 4) return false;
        for (int i = 0; i < 4; i++) f[i] = parsed[i];
        return true;
    }
    case CfgEntry::STR: {
        strncpy_s(static_cast<char*>(e->ptr), e->size, value, _TRUNCATE);
        return true;
    }
    default: return false;   // CURVE: not settable from a plain string
    }
}

void CfgEntry_ListNames(std::vector<std::string>& out)
{
    out.clear();
    out.reserve(std::size(kEntries));
    for (const auto& e : kEntries)
        out.push_back(e.key);
}
