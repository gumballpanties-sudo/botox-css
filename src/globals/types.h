#pragma once
#include "common.h"
#include <cstdint>

// -- Config paths --------------------------------------------------------------
static constexpr const char* CONFIG_DIR  = "C:\\botox-css";
static constexpr const char* CONFIG_PATH = "C:\\botox-css\\default.cfg";

// -- Watermark position enum ---------------------------------------------------
enum WatermarkPos : int {
    WM_TOP_LEFT = 0, WM_TOP_CENTER, WM_TOP_RIGHT,
    WM_BOTTOM_LEFT,  WM_BOTTOM_CENTER, WM_BOTTOM_RIGHT,
};

// -- Entity list stub ------------------------------------------------------------
struct IClientEntityList {};

// -- Color correction curve ---------------------------------------------------------
// A mapping, not an offset: the identity diagonal is neutral. Lives here rather than in
// colorcorrect.h because globals.h declares the four of them and colorcorrect.h includes
// globals.h. See colorcorrect.h for the interpolation and the rules on point order.
inline constexpr int k_ccMaxPts = 12;

struct CcCurve {
    int   n;                 // 2 .. k_ccMaxPts
    float x[k_ccMaxPts];
    float y[k_ccMaxPts];
};

// -- Client-side particle dispatch ------------------------------------------------------
// The Exorcism spell's ghosts are a SERVER effect, so an attribute write alone shows
// nothing off a Halloween server -- the particle has to be dispatched by hand.
struct CEffectData_Weather {
    float  m_vOrigin[3];
    float  m_vStart[3];
    float  m_vNormal[3];
    float  m_vAngles[3];
    int    m_fFlags;
    int    m_nEntIndex;
    float  m_flScale;
    float  m_flMagnitude;
    float  m_flRadius;
    int    m_nAttachmentIndex;
    short  m_nSurfaceProp;
    short  _pad4A;
    int    m_nMaterial;
    int    m_nDamageType;
    int    m_nHitBox;          // the particle system index goes here
    unsigned char m_nColor;
    bool   m_bCustomColors;
    char   _pad5A[2];
    float  m_CustomColors[6];
    bool   m_bControlPoint1;
    char   _pad75[3];
    int    m_eParticleAttachment;
    float  m_vecOffset[3];
};

using CTE_DispatchEffect_t      = int(__fastcall*)(const char*, const CEffectData_Weather&);
using Get_ParticleSystemIndex_t = int(__fastcall*)(const char*);
