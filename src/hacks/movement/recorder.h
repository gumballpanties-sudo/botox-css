#pragma once
#include "../../globals/globals.h"
#include "movement.h"
#include "recorder_ser.h"

// Movement recorder: botox hacks/movement/movement_recorder.cpp 1:1 on this pipeline.
// Record a route (or clip the last N seconds), walk onto its start, replay it tick for tick.
// Playback keeps the recorded WORLD wish dir under any sent view (CorrectMovement), so the
// camera can be free, original, turned (left/right/back) or spinning.
// Not ported: CS:GO jetpack replay, round freeze events (TF2 preround: the 2 s approach
// give-up covers it), botox's detection-window toasts (messages go to chat instead).
// Clips live under kRecorderDir, one ".rec" file per clip (map + tickrate inside the file).
static constexpr const char* kRecorderDir = "C:\\botox-css\\recorder";

inline bool  g_recEnable           = false;
inline bool  g_recShowLine         = true;
inline bool  g_recPreviewLine      = false;   // route lines near start (botox m_movement_rec_preview_line)
inline bool  g_recRender           = true;    // timer box
inline int   g_recPosition         = 0;       // timer: top left / bottom left / bottom right
inline bool  g_recLockVa           = true;    // original playback viewangles
inline bool  g_recLockGoingToStart = true;    // freeze the mouse while walking to the start
inline bool  g_recClipper          = false;   // rolling buffer of the last g_recClipSeconds
inline bool  g_recClipperBox       = true;
inline float g_recClipSeconds      = 15.f;
inline bool  g_recStopOnMove       = false;   // fresh WASD/jump/duck press = take over
inline bool  g_recForceWeapon      = true;
inline int   g_recYaw              = 0;       // original / left / right / backwards / spinning
inline float g_recSpinSpeed        = 360.f;   // deg/s
inline float g_recSmoothStart      = 0.f;     // s
inline float g_recSmoothEnd        = 0.f;     // s
// press actions (no style: always on / toggle would fire them on every menu close)
inline int g_recKeyStartRec = 0, g_recKeyStopRec = 0, g_recKeySave = 0, g_recKeyStartPlay = 0,
           g_recKeyStopPlay = 0, g_recKeyClear = 0, g_recKeyClip = 0;

enum RecAction : unsigned
{
    REC_START_REC  = 1u << 0,
    REC_STOP_REC   = 1u << 1,
    REC_SAVE       = 1u << 2,
    REC_START_PLAY = 1u << 3,
    REC_STOP_PLAY  = 1u << 4,
    REC_CLEAR      = 1u << 5,
    REC_CLIP       = 1u << 6,
};

// ---- game thread (Movement_RunImpl / Movement_Run) ----
// Start: pLocal null = dead (resets). Captures the player's own input, then lays the replay
// frame on the cmd so every feature + prediction sees the recorded input.
void Recorder_CmdStart(void* pLocal, UserCmd_t* pCmd);
void Recorder_CmdPrePred(UserCmd_t* pCmd);   // re-lay it: pre features may have rewritten it
void Recorder_CmdEnd(UserCmd_t* pCmd);       // keys/actions, approach, step, record the final cmd
bool Recorder_OwnsCmd();                     // playing or walking to the start
void Recorder_FrameStart();                  // FrameStageNotify FRAME_START: per-frame camera
void Recorder_MouseInput(float* x, float* y);// OverrideMouseInput: mouse lock

// ---- render thread ----
void Recorder_Request(unsigned actions);     // menu buttons, consumed on the next cmd
void Recorder_Render();                      // path, route starts, timer, clipper box, editor
void Recorder_MenuRoutes();                  // "routes" group body (list, edit, delete, refresh)
void Recorder_Refresh();
bool Recorder_EditorOpen();                  // imgui owns input while true
bool Recorder_EditorKey(unsigned vk);        // WndProc fresh keydown: ENTER opens/closes, ESC closes
