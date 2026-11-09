#pragma once
#include "../../globals/globals.h"

// Detections, botox csgo 1:1: a performed trick prints "<label> | edgebugged" to chat, label in
// the accent color. Types bitmask in botox e_detection_types order (all off by default, as botox).
enum DetType { DET_EB, DET_TB, DET_AS, DET_PS, DET_WC, DET_MAX };
inline bool g_detections       = false;
inline int  g_detectionTypes   = 0;        // bit i = DetType i
inline bool g_detectionStackEb = false;
inline char g_detectionLabel[64] = "botox";

// cmd start state + this cmd's trick flags. Movement thread (CreateMove), so chat is safe here.
struct DetTick
{
    bool  onGround = false, walk = false, jumpPressed = false;
    float vz = 0.f, speed = 0.f, gravity = 800.f, interval = 0.015f;
    int   tick = 0;
    bool  tb = false, as = false, ps = false, wc = false;
    bool  tbOwns = false;   // TB steered the cmd without a hit (park/hold) -- EB veto only

};
void Detections_Tick(const DetTick& t);

// botox push_notice CHAT_TOKEN_JUMPSTATS: "<label> | J: 245.00 units | 5 strafes | ..." (not
// gated on g_detections -- botox's own token, jumpstats toggle only)
struct JsResult;
void Detections_Jumpstats(const JsResult& r);
