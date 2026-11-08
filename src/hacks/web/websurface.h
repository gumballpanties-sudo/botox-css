#pragma once
#include <cstdint>
#include <d3d9.h>
#include <string>

// botox: web-on-surface toggles. No menu wired yet (unit MENU owns it) -- see report MENU.
extern bool  g_webEnable;       // "web on surface"
extern bool  g_webBlockFire;    // "don't shoot at the screen"
extern float g_webScale;        // "size##web"
extern int   g_webAspect;       // "aspect##web"

// botox: web-on-surface config, plain globals (CSS-port style, no GET_VARIABLE indirection).
extern bool  g_webPanel;             // address-bar/controls panel visible
extern float g_webPanelOpacity;      // 0..1
extern bool  g_webPanelAlways;       // chromeless image shown even with the menu closed
extern int   g_webResH;              // browser's short-side resolution, px
extern float g_webVolume;            // 0..1
extern std::string g_webUrl;         // address bar default / last typed

extern float g_webWorldOpacity;      // 0..1, the placed quad's alpha
extern int   g_webDepthMode;         // 0 depth-tested (occluded by world) / 1 always on top queued /
                                      // 2 always on top, drawn last (after the panel)
extern int   g_webBackMode;          // 0 draw both sides / 1 hide back face / 2 mirror image on back face
extern float g_webRound;             // quad corner radius, world units
extern bool  g_webVolDistance;       // attenuate volume by distance to the local player
extern float g_webVolNear;           // full volume inside this distance
extern float g_webVolFar;            // silent past this distance
extern bool  g_webCrosshairPointer;  // crosshair on the quad drives the page's mouse pointer
extern bool  g_webClickOnly;         // only move/scroll the page's pointer while a button is held
extern bool  g_webScrollInput;       // WndProc: mouse wheel / up/down arrows reach the page
extern bool  g_webAutoPlay;          // try to play the main <video> on place / page load
extern bool  g_webPlayFromStart;     // auto play seeks to 0 first
extern bool  g_webAutoRemove;        // unplace when the main <video> fires 'ended'
extern bool  g_webAdblock;           // page-side ad CSS/JS (youtube/twitch skip, UA + codec spoof)

// Place/click/right-click/remove binds -- plain VK codes, 0 = unbound. Momentary (press-edge),
// unlike the movement binds there is no hold/toggle mode: botox's key_bind_t here is one action key.
extern int g_webPlaceKey;
extern int g_webClickKey;
extern int g_webRightClickKey;
extern int g_webRemoveKey;

// botox: botox network web share. m_seq bumps on own place / remove / url change
struct web_share_t {
    bool        m_placed = false;
    float       m_pos[3]{}, m_normal[3]{}, m_right[3]{};
    float       m_scale  = 1.f;
    int         m_aspect = 0;
    unsigned    m_seq    = 0;
    std::string m_url;
    bool        m_media  = false;   // main video seen in the last 3s
    float       m_time   = 0.f;
    bool        m_paused = false;
};
bool Web_ShareGet(web_share_t& out);                  // main thread, false = web off
bool Web_On();
// main thread. one screen + browser per sender, never touches our own screen
bool Web_PeerShow(int sender, const web_share_t& in); // false = not shown (web off, no WebView2, all slots busy)
void Web_PeerHide(int sender);
void Web_PeerMedia(int sender, float time, bool paused);

void Web_Present(IDirect3DDevice9* dev);             // render thread, inside BuildImGuiFrame (render.cpp)
void Web_MainTick();                                  // main thread, FSN stage 0 FRAME_START
void Web_OnPostScreenSpaceEffects(const void* setup); // screenfx.cpp DoPostScreenSpaceEffects hook
void Web_LevelShutdown();
void Web_Destroy();
void Web_InvalidateTexture();
bool Web_OnInput(unsigned msg, uintptr_t wParam);     // WndProc, menu closed only
bool Web_BlockFire();                                 // CreateMove, clears IN_ATTACK|IN_ATTACK2
void Web_PlaceAtCrosshair();
void Web_Navigate(const char* url);                   // menu / panel address bar, any thread
void Web_Unplace();
void Web_ClearCache();
void Web_ClearMemory();
const char* Web_Status();
// Web_RestoreOverColorCorr: NOT ported, dropped from the public API. CSS runs ColorCorr_Frame
// (and every other screen-fx pass) strictly BEFORE BuildImGuiFrame on every path (render.cpp
// h_Present/h_PaintTraverse/h_EndScene all call it ahead of BuildImGuiFrame) -- Web_Present
// runs inside that same BuildImGuiFrame, i.e. always AFTER the grade already landed. The quad
// is never graded, so botox's stencil-mark-then-blit-the-pre-grade-copy-back dance has nothing
// to undo here. See report.
