#pragma once
#include "../../globals/globals.h"

// Detections, botox csgo 1:1: a performed trick prints "<label> | edgebugged" to chat, label in
// the accent color. Types bitmask in botox e_detection_types order (all off by default, as botox).
enum DetType { DET_EB, DET_TB, DET_AS, DET_PS, DET_WC, DET_ESURF, DET_MAX };   // edge surf appended: cfg bits stay
inline bool g_detections       = false;
inline int  g_detectionTypes   = 0;        // bit i = DetType i
inline bool g_detectionStackEb = false;
inline char g_detectionLabel[64] = "botox";
// line = label, separator, prefix + ' ', trick ( "edgebugged" ), each its own color
inline char  g_detectionSeparator[32]     = " | ";   // empty = " | "
inline float g_detectionSeparatorColor[4] = { 190.f / 255.f, 190.f / 255.f, 190.f / 255.f, 1.f };
inline float g_detectionLabelColor[4]     = { 190.f / 255.f, 190.f / 255.f, 190.f / 255.f, 1.f };
inline int   g_detectionPrefixMode        = 0;       // 0 off, 1 static g_detectionPrefix, 2 random g_detectionPrefixList ( names.h )
inline float g_detectionPrefixColor[4]    = { 1.f, 1.f, 1.f, 1.f };
inline char  g_detectionPrefix[64]        = "";

// cmd start state + this cmd's trick flags. Movement thread (CreateMove), so chat is safe here.
struct DetTick
{
    bool  onGround = false, walk = false, jumpPressed = false;
    float vz = 0.f, speed = 0.f, gravity = 800.f, interval = 0.015f;
    int   tick = 0;
    bool  tb = false, as = false, ps = false, wc = false;
    bool  tbOwns = false;   // TB steered the cmd without a hit (park/hold) -- EB veto only
    // edge surf ( botox es_phys ): esPhys = START pinned + last cmd sent a proven pin, esHeld = it owned the last cmd,
    // esVeto = an edge surf pin this cmd or the last: never an edgebug too
    bool  esPhys = false, esHeld = false, esVeto = false;

};
void Detections_Tick(const DetTick& t);

// botox push_notice CHAT_TOKEN_JUMPSTATS: "<label> | J: 245.00 units | 5 strafes | ..." (not
// gated on g_detections -- botox's own token, jumpstats toggle only)
struct JsResult;
void Detections_Jumpstats(const JsResult& r);
