#include "websurface.h"
#include "websurf_math.h"
#include "websurf_js.h"
#include "../aim/css_bridge.h"
#include "webview2host.h"
#include "steamhtml.h"
#include "../../render/render.h"                     // Render_W2SMatrix, RenderFadedGradientLine, g_showMenu/g_hwnd/g_d3dDev/g_eject (globals.h)
#include "../../dependencies/imgui/imgui.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

// INCLUDE RULE (docs/port/AGENT_BRIEF.md, 11-03 fix): files outside src/hacks/aim/ never
// include aim/css.h, aim/defs.h or aim/sdk.h -- that SDK layer's ConVar/ICvar/RecvTable/
// ClientClass/IClientEntityList/INetChannelInfo redefine the classes src/globals/* already
// declares (confirmed: both layers in one TU is a hard C2011 redefinition error). This file
// uses only css_bridge.h (unit SDK2, plain types + free functions over entity indices), its
// own math (websurf_math.h's WsVec3), and src/globals/*'s own types (globals.h, pulled in
// through render.h) -- never aim/defs.h's Vec3/CBasePlayer/I::.
//
// botox web-on-surface, full CSS x64 port of csgo-sdk/hacks/web/websurface.cpp (11-03 WEB3
// pass). WEB2 had already ported the dual HTML backend (WebView2 primary, ISteamHTMLSurface
// fallback) and the botox-network peer screens -- that infrastructure is kept as-is below.
// This pass adds everything the port was still missing against the 2697-line original:
//   - the named render bug: WebDrawQuad used D3DFVF_XYZ fixed-function with NO transform set
//     (world/view/proj were whatever the material system's shader state last left them --
//     garbage under CSS's pipeline) -- replaced by WsDrawTris: full D3D state save/restore
//     (WsSavedState/WsSave/WsRestore, botox ~1202-1281) and a real projection built from
//     Render_W2SMatrix() (render.h, the same per-frame engine world-to-screen matrix the ESP
//     overlay uses), transposed via WsTranspose4 exactly as botox's get_world_to_screen_matrix
///    path did (botox ~2131-2136). Pulled in Web_Present (render thread, scene already open),
//     NOT in Web_OnPostScreenSpaceEffects -- CSS's screenfx.cpp hook is not guaranteed to be
//     the Present thread (multicore), Render_W2SMatrix's render-thread-safe snapshot is.
//   - mesh baking with rounded corners (WsBuildMesh/WsRoundedRectOutline, both already in
//     websurf_math.h, byte-identical to botox's) instead of a flat 2-triangle quad; depth
//     test modes (g_webDepthMode: 0 occluded by world / 1 always on top, same draw call /
//     2 always on top, drawn last, after the panel -- botox's g_webDepthMode 0/1/2) and
//     front/back face handling (g_webBackMode).
//   - Web_RestoreOverColorCorr: NOT ported, removed from the header. See websurface.h's own
//     comment and the report -- ColorCorr_Frame runs strictly before BuildImGuiFrame on every
//     CSS path, so the quad is never graded and botox's stencil-mark/restore-over dance has
//     nothing to undo here. This also means the stencil-marking half of WsDrawTris (botox's
//     s_markDs/s_marked/k_wsStencilBit) is dropped too -- same reason, real CSS pipeline
//     difference, not a simplification.
//   - the world-screen call-queue marshaling (botox's n_render_queue::submit / s_cqRan/
//     s_cqRefused/s_cqQueued) is dropped outright: that existed because CS:GO's Web_MainTick
//     ran on the main thread and had to hop to the render thread to issue raw D3D. In CSS,
//     Web_Present is already called by the orchestrator ON the render thread, inside
//     BuildImGuiFrame, with the scene open -- there is no thread to marshal to.
//   - input: crosshair-driven mouse ray onto the placed quad (WsRayPlane/WsQuadUV/WsInQuad,
//     occlusion trace via CssBridge_TraceWorld), click/right-click/place/remove key binds,
//     WndProc scroll + arrow keys (Web_OnInput), and page keyboard input while the panel has
//     focus (WebPanelKeys, ImGui's InputQueueCharacters).
//   - the address-bar/controls panel (self-contained ImGui window drawn by Web_Present, same
//     as botox draws its own -- no src/hacks/menu/* edit needed) and the chromeless image
//     mode (g_webPanelAlways) for when the menu is closed.
//   - page-side adblock/guard/UA+codec-spoof JS (k_adCss/k_adJs/k_guardJs/k_uaJs/k_codecJs,
//     verbatim botox strings -- pure content, no SDK dependency) and the Steam-backend cache
//     wipe sequence (WebWipeNext/WebWipeTick, cookie/storage wipe per visited origin then a
//     fresh browser).
//   - distance-based volume attenuation (g_webVolDistance/near/far) against the local
//     player's real eye position: WebDoPlace reads Css_ViewOffset (css_bridge.h/.cpp, backed
//     by netvars.cpp's g_offViewOffset = m_vecViewOffset) instead of a hardcoded +64 -- CSS
//     standing/crouched eye height differ (64 / ~47, cs_gamemovement.cpp's g_CSViewVectors)
//     and botox's own +64 was CS:GO's constant, never verified against CSS's.
//   - follow-up pass (11-03): the codec/DRM capability probe (k_probeTag/k_probeJs, verbatim
//     botox ~555-610) and websurf_js.h's k_wsSmoothJs (already in that header, unused until
//     now) wired at the exact botox injection points -- Steam's OnFinishedRequest (every
//     finished request), wv2's EV_READY (start scripts) + EV_FINISHED (probe once, guarded by
//     s_probeLine), and peers' EV_READY. Needed WebHandleTitle, a new shared helper read by
//     both the Steam backend's real HTML_ChangedTitle_t callback and wv2's EV_TITLE event --
//     wv2 title changes were not observed at all before this pass, which also meant
//     g_webAutoRemove (the k_wsEndedJs "ended" tag) never reached Web_Unplace() on WebView2,
//     only on the Steam fallback. Fixed as part of wiring the probe, not a separate item.

bool  g_webEnable    = false;
bool  g_webBlockFire = true;
float g_webScale     = 1.f;
int   g_webAspect    = 0;

bool  g_webPanel            = false;
float g_webPanelOpacity     = 0.92f;
bool  g_webPanelAlways      = false;
int   g_webResH             = 512;
float g_webVolume           = 1.f;
std::string g_webUrl        = "https://www.youtube.com";

float g_webWorldOpacity     = 1.f;
int   g_webDepthMode        = 0;
int   g_webBackMode         = 1;
float g_webRound            = 0.f;
bool  g_webVolDistance      = false;
float g_webVolNear          = 200.f;
float g_webVolFar           = 1200.f;
bool  g_webCrosshairPointer = true;
bool  g_webClickOnly        = false;
bool  g_webScrollInput      = true;
bool  g_webAutoPlay         = true;
bool  g_webPlayFromStart    = false;
bool  g_webAutoRemove       = false;
bool  g_webAdblock          = true;

int g_webPlaceKey       = 0;
int g_webClickKey       = 0;
int g_webRightClickKey  = 0;
int g_webRemoveKey      = 0;

namespace
{
	// ---- page-side JS content (adblock/guard/UA+codec spoof) -- verbatim botox strings ----
	// Pure content: no SDK call, no vtable, nothing that can crash -- every apply site is
	// already inside a __try (WebExecJs_SEH). botox csgo-sdk/hacks/web/websurface.cpp ~226-610.
	const char* k_adCss =
		"ins.adsbygoogle,.adsbygoogle,[id^='google_ads'],[id^='div-gpt-ad'],iframe[src*='doubleclick'],"
		"iframe[src*='googlesyndication'],iframe[src*='adservice'],.ad-container,.ad-banner,.advertisement,"
		"[class*='sponsored-ad'],[data-ad-slot],ytd-promoted-video-renderer,ytd-display-ad-renderer,"
		"ytd-ad-slot-renderer,.ytp-ad-overlay-container,#player-ads,#masthead-ad,"
		"ytd-banner-promo-renderer,ytd-in-feed-ad-layout-renderer,ytd-companion-slot-renderer,"
		"ytd-statement-banner-renderer,ytd-merch-shelf-renderer,ytd-rich-item-renderer:has(ytd-ad-slot-renderer),"
		"ytd-rich-section-renderer:has(ytd-statement-banner-renderer),ytm-promoted-sparkles-web-renderer"
		"{display:none!important;visibility:hidden!important;height:0!important}";

	const char* k_adJsTpl = R"JS((function(){try{
if(window.__fpsAd)return;window.__fpsAd=1;
var YT=/(^|\.)youtube(-nocookie)?\.com$/.test(location.hostname);
if(YT){
  var AD=['adPlacements','playerAds','adSlots'];
  var prune=function(o){try{
    if(!o||typeof o!=='object')return o;
    var roots=[o,o.playerResponse];
    if(Array.isArray(o))o.forEach(function(x){if(x)roots.push(x,x.playerResponse);});
    roots.forEach(function(r){if(r&&typeof r==='object')AD.forEach(function(k){if(k in r)delete r[k];});});
    if(Array.isArray(o.entries))o.entries=o.entries.filter(function(e){try{return !e.command.reelWatchEndpoint.adClientParams.isAd;}catch(x){return true;}});
  }catch(e){}return o;};
  var hit=function(u){return /\/youtubei\/v1\/(player|next|get_watch|reel\/reel_watch_sequence)|\/playlist\?/.test(String(u||''));};
  var scrub=function(t){return typeof t==='string'?t.replace(/"adPlacements"/g,'"no_ads"').replace(/"adSlots"/g,'"no_ads"').replace(/"playerAds"/g,'"no_ads"'):t;};
  var jp=JSON.parse;JSON.parse=function(){return prune(jp.apply(this,arguments));};
  var f=window.fetch;
  if(f)window.fetch=function(input){
    var u=input&&input.url?input.url:input;
    var p=f.apply(this,arguments);
    if(!hit(u))return p;
    return p.then(function(r){return r.clone().text().then(function(t){
      return new Response(scrub(t),{status:r.status,statusText:r.statusText,headers:r.headers});
    },function(){return r;});});
  };
  var X=XMLHttpRequest.prototype,xo=X.open;
  X.open=function(m,u){this.__fpsU=u;return xo.apply(this,arguments);};
  ['responseText','response'].forEach(function(k){
    var d=Object.getOwnPropertyDescriptor(X,k);if(!d||!d.get)return;
    Object.defineProperty(X,k,{configurable:true,enumerable:d.enumerable,get:function(){
      var v=d.get.call(this);if(!hit(this.__fpsU))return v;
      if(typeof v==='string')return scrub(v);
      return prune(v);}});
  });
}
var css=document.createElement('style');css.id='__fpsAdCss';
css.textContent="%CSS%";
(document.head||document.documentElement).appendChild(css);
var sped=false;
if(YT)setInterval(function(){try{
  if(!document.getElementById('__fpsAdCss'))(document.head||document.documentElement).appendChild(css);
  var p=document.querySelector('.html5-video-player'),v=p&&p.querySelector('video');
  if(p)p.querySelectorAll('.ytp-ad-skip-button,.ytp-skip-ad-button,.ytp-ad-skip-button-modern,.ytp-ad-skip-button-container button,.ytp-ad-overlay-close-button').forEach(function(b){b.click();});
  if(p&&v&&p.classList.contains('ad-showing')){
    v.muted=true;v.playbackRate=16;sped=true;
    if(isFinite(v.duration)&&v.duration>0)v.currentTime=v.duration;
  }else if(sped&&v){v.playbackRate=1;sped=false;}
  document.querySelectorAll('ytd-enforcement-message-view-model').forEach(function(e){
    var d=e.closest('tp-yt-paper-dialog');(d||e).remove();
    var w=document.querySelector('video');if(w&&w.paused)w.play();});
}catch(e){}},250);
}catch(e){}})())JS";

	const std::string& WebAdJs()
	{
		static std::string s;
		if (s.empty()) {
			std::string css;
			for (const char* c = k_adCss; *c; ++c) { if (*c == '"' || *c == '\\') css += '\\'; css += *c; }
			s = k_adJsTpl;
			s.replace(s.find("%CSS%"), 5, css);
		}
		return s;
	}

	const char* k_guardJs =
		"(function(){try{if(window.__fpsGuard)return;window.__fpsGuard=1;"
		"window.alert=function(){};window.confirm=function(){return true;};window.prompt=function(){return null;};"
		"window.open=function(u){try{if(u)location.href=u;}catch(e){}return null;};"
		"document.addEventListener('click',function(e){try{var t=e.target;"
		"var a=t&&t.closest&&t.closest('a[target]');if(a&&a.target!=='_self')a.target='_self';"
		"var f=t&&t.closest&&t.closest('input[type=file]');if(f)e.preventDefault();"
		"}catch(x){}},true);"
		"var pc=HTMLInputElement.prototype.click;HTMLInputElement.prototype.click=function(){"
		"if(this.type==='file')return;return pc.apply(this,arguments);};"
		"if(HTMLInputElement.prototype.showPicker)HTMLInputElement.prototype.showPicker=function(){};"
		"}catch(e){}})()";

	const char* k_uaJs = R"JS((function(){try{
if(window.__fpsUA)return;
if(/(^|\.)tiktok\.com$/.test(location.hostname)){window.__fpsUA='skip';return;}
window.__fpsUA=document.readyState;
var V='150',F=V+'.0.0.0';
var ua='Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/'+F+' Safari/537.36';
var b=[{brand:'Chromium',version:V},{brand:'Google Chrome',version:V},{brand:'Not.A/Brand',version:'99'}];
var fb=[{brand:'Chromium',version:F},{brand:'Google Chrome',version:F},{brand:'Not.A/Brand',version:'99.0.0.0'}];
var hi={brands:b,fullVersionList:fb,mobile:false,platform:'Windows',platformVersion:'15.0.0',
  architecture:'x86',bitness:'64',model:'',uaFullVersion:F,wow64:false,formFactors:['Desktop']};
var uad={brands:b,mobile:false,platform:'Windows',
  getHighEntropyValues:function(h){var o={brands:b,mobile:false,platform:'Windows'};
    (h||[]).forEach(function(k){if(k in hi)o[k]=hi[k];});return Promise.resolve(o);},
  toJSON:function(){return {brands:b,mobile:false,platform:'Windows'};}};
var P=Navigator.prototype,d=function(k,v){try{Object.defineProperty(P,k,{get:function(){return v;},configurable:true});}catch(e){}};
d('userAgent',ua);d('appVersion',ua.slice(8));d('userAgentData',uad);d('vendor','Google Inc.');d('webdriver',false);
}catch(e){}})())JS";

	const char* k_codecJs = R"JS((function(){try{
if(window.__fpsCodec)return;window.__fpsCodec=1;
var bad=/hvc1|hev1|hevc|bytevc1|h265/i;
[window.MediaSource,window.ManagedMediaSource].forEach(function(M){
  if(!M||!M.isTypeSupported)return;var o=M.isTypeSupported;
  M.isTypeSupported=function(t){return bad.test(String(t))?false:o.call(M,t);};});
if(window.HTMLMediaElement){var cp=window.HTMLMediaElement.prototype.canPlayType;
  window.HTMLMediaElement.prototype.canPlayType=function(t){return bad.test(String(t))?'':cp.call(this,t);};}
var mc=navigator.mediaCapabilities;
if(mc&&mc.decodingInfo){var d=mc.decodingInfo.bind(mc);
  mc.decodingInfo=function(c){try{var v=c&&c.video;if(v&&bad.test(String(v.contentType)))
    return Promise.resolve({supported:false,smooth:false,powerEfficient:false,keySystemAccess:null});}catch(e){}return d(c);};}
var VD=window.VideoDecoder;
if(VD&&VD.isConfigSupported){var vi=VD.isConfigSupported.bind(VD);
  VD.isConfigSupported=function(c){return c&&bad.test(String(c.codec))?Promise.resolve({supported:false,config:c}):vi(c);};}
}catch(e){}})())JS";

	const char* k_wipeJs = R"JS((function(){try{
var h=location.hostname.split('.'),D=[''],P=['/'],s=location.protocol==='https:'?';secure':'';
for(var i=0;i<h.length-1;i++){var d=h.slice(i).join('.');D.push(d,'.'+d);}
var p=location.pathname.split('/');for(var j=2;j<p.length;j++)P.push(p.slice(0,j).join('/'));
document.cookie.split(';').forEach(function(c){var n=c.split('=')[0].trim();if(!n)return;
  D.forEach(function(d){P.forEach(function(q){document.cookie=n+'=;expires=Thu, 01 Jan 1970 00:00:00 GMT;max-age=0;path='+q+(d?';domain='+d:'')+s;});});});
try{localStorage.clear();}catch(e){}try{sessionStorage.clear();}catch(e){}
try{indexedDB.databases().then(function(l){l.forEach(function(x){indexedDB.deleteDatabase(x.name);});});}catch(e){}
try{caches.keys().then(function(k){k.forEach(function(x){caches.delete(x);});});}catch(e){}
try{navigator.serviceWorker.getRegistrations().then(function(r){r.forEach(function(x){x.unregister();});});}catch(e){}
}catch(e){}})())JS";

	// Codec/DRM capability probe (follow-up, 11-03): verbatim botox csgo-sdk/hacks/web/
	// websurface.cpp ~555-610. Reports back over document.title (the only channel available
	// without a custom JS-bridge export) -- the real title is cached in window.__fpsT0 the
	// first time so two overlapping probes/diag sweeps never "restore" a stale __fps title,
	// then the probe's own title is reverted after 200ms. WebHandleTitle (below) is what reads
	// these back on both backends (Steam's real HTML_ChangedTitle_t callback, wv2's EV_TITLE).
	const char* k_probeTag = "__fpsprobe|";
	const char* k_probeJs =
		"(function(){try{var M=window.MediaSource,t=function(c){return M&&M.isTypeSupported(c)?1:0;};"
		"var s='h264='+t('video/mp4; codecs=\"avc1.4d401f\"')"
		"+'|hevc='+t('video/mp4; codecs=\"hvc1.1.6.L93.B0\"')"
		"+'|vp9='+t('video/webm; codecs=\"vp9\"')"
		"+'|av1='+t('video/mp4; codecs=\"av01.0.05M.08\"')"
		"+'|mse='+(M?1:0)+'|mms='+(window.ManagedMediaSource?1:0)"
		"+'|wasm='+(typeof WebAssembly==='object'?1:0)"
		"+'|webcodecs='+(typeof VideoDecoder==='function'?1:0)"
		"+'|webgl='+(function(){try{return document.createElement('canvas').getContext('webgl')?1:0;}catch(e){return 0;}})()"
		"+'|sab='+(typeof SharedArrayBuffer==='function'?1:0)"
		"+'|webdriver='+(navigator.webdriver?1:0)"
		"+'|chrome='+(window.chrome?1:0)"
		"+'|uad='+(navigator.userAgentData?navigator.userAgentData.brands.map(function(b){return b.brand+'/'+b.version;}).join(','):'none')"
		"+'|spoof='+(window.__fpsUA||'none')+'|host='+location.hostname;"
		"var t0=function(){var c=document.title;return /^__fps/.test(c)?(window.__fpsT0||''):(window.__fpsT0=c);};"
		"var send=function(w){var o=t0();document.title='__fpsprobe|'+s+'|widevine='+w+'|ua='+navigator.userAgent;"
		"setTimeout(function(){document.title=o;},200);};"
		"if(navigator.requestMediaKeySystemAccess)navigator.requestMediaKeySystemAccess('com.widevine.alpha',"
		"[{initDataTypes:['cenc'],videoCapabilities:[{contentType:'video/mp4; codecs=\"avc1.42E01E\"'}]}])"
		".then(function(){send(1);},function(){send(0);});else send(0);"
		"if(!window.__fpsDiagArmed&&!window.__fpsNoDiag){window.__fpsDiagArmed=1;"
		"var say=function(a){var o=t0(),i=0,n=function(){if(i>=a.length)return;document.title=a[i++];"
		"setTimeout(function(){document.title=o;setTimeout(n,200);},200);};n();};"
		"var tv=function(u){return new Promise(function(res){try{var v=document.createElement('video'),d=0,"
		"f=function(r){if(d)return;d=1;res(r);try{v.removeAttribute('src');v.load();}catch(e){}};"
		"v.muted=true;v.preload='auto';v.onloadeddata=function(){f('ok'+v.videoWidth);};"
		"v.onerror=function(){f('err'+(v.error?v.error.code+':'+String(v.error.message||'').slice(0,40):''));};"
		"setTimeout(function(){f('to'+v.readyState);},7000);v.src=u;}catch(e){res('x');}});};"
		"var M='https://interactive-examples.mdn.mozilla.net/media/cc0-videos/flower.';"
		"var diag=function(){try{"
		"var ST=window.__fpsST||(window.__fpsST=Promise.all([tv(M+'mp4'),tv(M+'webm')]));"
		"var ve=[].slice.call(document.querySelectorAll('video')).map(function(v){"
		"return (v.error?'err'+v.error.code+':'+String(v.error.message||'').slice(0,60):'ok')+'/rs'+v.readyState"
		"+'/'+String(v.currentSrc||'').slice(0,40);}).join(';')||'none';"
		"var v0=[].slice.call(document.querySelectorAll('video')).filter(function(v){return /^https?:/.test(v.currentSrc||'');})[0];"
		"var st=v0?fetch(v0.currentSrc,{headers:{Range:'bytes=0-7'},credentials:'include'}).then(function(r){"
		"return r.status+' '+(r.headers.get('content-type')||'');},function(){return 'fetchfail';}):Promise.resolve('-');"
		"var tx=document.body?document.body.innerText:'',i=tx.search(/#\\d{4}|not supported|unsupported|supported browser|isn.t supported|update your browser|obs.ugiwan/i),"
		"m=i<0?'':tx.substr(Math.max(0,i-80),160).replace(/\\s+/g,' ');"
		"var vc=function(c,hw){return window.VideoDecoder?VideoDecoder.isConfigSupported({codec:c,hardwareAcceleration:hw})"
		".then(function(r){return r.supported?1:0;},function(){return 'x';}):Promise.resolve('-');};"
		"var ac=window.AudioDecoder?AudioDecoder.isConfigSupported({codec:'mp4a.40.2',sampleRate:48000,numberOfChannels:2})"
		".then(function(r){return r.supported?1:0;},function(){return 'x';}):Promise.resolve('-');"
		"Promise.all([vc('avc1.4d401f','prefer-software'),vc('avc1.4d401f','prefer-hardware'),"
		"vc('av01.0.05M.08','prefer-software'),vc('vp09.00.10.08','prefer-software'),ac,st,ST]).then(function(d){"
		"say(['__fpsdiag|'+location.hostname+'|test h264mp4='+d[6][0]+' vp8webm='+d[6][1]"
		"+'|workers='+(window.__fpsWorkers||0)+'|mse='+(window.__fpsWk||'-')"
		"+'|ml='+((window.__fpsML||[]).join(' ')||'-'),"
		"'__fpsdiag2|'+location.hostname+'|video='+ve+'|src='+d[5]"
		"+'|dec h264sw='+d[0]+' h264hw='+d[1]+' av1sw='+d[2]+' vp9sw='+d[3]+' aac='+d[4]"
		"+'|hevcHw='+(window.__fpsHevcHw||'-')+'|msg='+m]);});}catch(e){}};"
		"setTimeout(diag,8000);setTimeout(diag,30000);}"
		"}catch(e){}})()";
	int  s_hasH264 = -1;
	char s_probeLine[256] = "";

	// ---- globals -----------------------------------------------------------------------
	bool        s_placed = false;
	WsVec3      s_pos{}, s_normal{ 0.f, 0.f, 1.f }, s_right{ 1.f, 0.f, 0.f }, s_up{ 0.f, 1.f, 0.f };
	bool        s_rebake = true;
	unsigned    s_seq    = 0;
	std::string s_url;
	DWORD       s_mediaSeenMs = 0;
	float       s_mediaTime = 0.f;
	bool        s_mediaPaused = false;

	bool        s_placeRequested = false;
	bool        s_wv2Started     = false;
	bool        s_wv2Failed      = false;

	// Current view, written by Web_OnPostScreenSpaceEffects (main/game thread, see screenfx.cpp's
	// own FxReadViewSetup -- collision traces off it are an established pattern there).
	WsVec3      s_viewOrigin, s_viewFwd;
	bool        s_viewValid = false;
	float       s_distAtten = 1.f;

	// Where the crosshair meets the placed screen (read by Web_OnInput on the WndProc thread).
	bool        s_crossInside = false;
	float       s_crossU = 0.5f, s_crossV = 0.5f;
	bool        s_mouseDownWorld = false, s_rDownWorld = false;
	bool        s_placeDown = false, s_removeDown = false;
	bool        s_ptrOnPage = false;

	// ---- mesh (rounded-rect quad, world units), rebuilt on place / resize / round change ----
	struct WsVert { float x, y, z; DWORD col; float u, v; };
	inline constexpr int k_wsCornerSeg = 8;
	const DWORD k_wsFvf = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;
	std::mutex              s_meshLock;
	std::vector<WsVert>     s_verts, s_vertsBack;
	std::vector<uint16_t>   s_idx;
	unsigned                s_meshEpoch = 0;

	bool WsBuildMesh(const WsVec3& pos, const WsVec3& right, const WsVec3& up, float ww, float wh,
	                 std::vector<WsVert>& verts, std::vector<WsVert>& vertsBack, std::vector<uint16_t>& idx)
	{
		const float w = ww > 1.f ? ww : 1.f;
		const float h = wh > 1.f ? wh : 1.f;
		float rad = g_webRound;
		const float maxRad = (w < h ? w : h) * 0.5f;
		if (rad < 0.f) rad = 0.f;
		if (rad > maxRad) rad = maxRad;

		float uv[4 * (k_wsCornerSeg + 1) * 2];
		const int n = WsRoundedRectOutline(rad / w, rad / h, rad > 0.01f ? k_wsCornerSeg : 0,
		                                   uv, 4 * (k_wsCornerSeg + 1));
		if (n < 3) return false;

		{
			const WsVec3 c = WsQuadPoint(0.5f, 0.5f, pos, right, up, ww, wh);
			WsVert vt{ c.x, c.y, c.z, 0xFFFFFFFF, 0.5f, 0.5f };
			verts.push_back(vt);
			vertsBack.push_back(vt);
		}
		for (int i = 0; i < n; ++i) {
			const float u = uv[i * 2], v = uv[i * 2 + 1];
			const WsVec3 p = WsQuadPoint(u, v, pos, right, up, ww, wh);
			WsVert vt{ p.x, p.y, p.z, 0xFFFFFFFF, u, v };
			verts.push_back(vt);
			vt.u = 1.f - u;
			vertsBack.push_back(vt);
		}
		for (int i = 0; i < n; ++i) {
			idx.push_back(0);
			idx.push_back((uint16_t)(1 + i));
			idx.push_back((uint16_t)(1 + ((i + 1) % n)));
		}
		return true;
	}

	float QuadWidth()  { float w, h; WsAspectDims(g_webAspect, g_webScale, w, h); return w; }
	float QuadHeight() { float w, h; WsAspectDims(g_webAspect, g_webScale, w, h); return h; }

	void WebBakeMesh()
	{
		std::vector<WsVert> verts, vertsBack;
		std::vector<uint16_t> idx;
		if (s_placed) {
			const float ww = QuadWidth(), wh = QuadHeight();
			WsBuildMesh(s_pos, s_right, s_up, ww, wh, verts, vertsBack, idx);
		}
		std::lock_guard<std::mutex> lk(s_meshLock);
		s_verts.swap(verts);
		s_vertsBack.swap(vertsBack);
		s_idx.swap(idx);
		++s_meshEpoch;
		s_rebake = false;
	}

	// ---- own-screen pixel sink (WebView2 capture thread) --------------------------------
	std::mutex s_pixLock;
	std::vector<uint8_t> s_pix;     // own screen, BGRA
	int         s_pixW = 0, s_pixH = 0;
	bool        s_pixDirty = false;

	IDirect3DTexture9* s_tex = nullptr;
	int                s_texW = 0, s_texH = 0;

	void WebWv2Sink(const uint8_t* bgra, int w, int h, int pitch)
	{
		std::lock_guard<std::mutex> lk(s_pixLock);
		s_pix.resize((size_t)w * h * 4);
		for (int y = 0; y < h; y++)
			memcpy(&s_pix[(size_t)y * w * 4], bgra + (size_t)y * pitch, (size_t)w * 4);
		s_pixW = w; s_pixH = h; s_pixDirty = true;
	}

	bool WebNavFilter(const char* url) { return !url || !*url; }   // adblock host-block is per-host, see WebUrlBlocked below

	const char* const k_adHosts[] = {
		"doubleclick.net", "googlesyndication.com", "googleadservices.com",
		"adservice.google.com", "popads.net", "popcash.net", "propellerads.com",
		"onclickads.net", "adnxs.com",
	};
	bool WebUrlBlocked(const char* url)
	{
		if (!g_webAdblock || !url) return false;
		for (const char* h : k_adHosts) if (strstr(url, h)) return true;
		return false;
	}
	bool WebWv2Filter(const char* url) { return WebUrlBlocked(url); }

	void WebWv2TryStart()
	{
		if (s_wv2Started || s_wv2Failed) return;
		int w, h; WsAspectRes(g_webAspect, g_webResH, w, h);
		if (!wv2::Start(w, h, WebWv2Sink, WebWv2Filter)) { s_wv2Failed = true; return; }
		s_wv2Started = true;
		if (!s_url.empty()) wv2::Navigate(s_url.c_str());
	}

	// ---- ISteamHTMLSurface fallback (own screen only) -- 1:1 with csgo-sdk's WebSteamInit/... ----
	using RegisterCb_t   = void (*)(CCallbackBase*, int);
	using UnregisterCb_t = void (*)(CCallbackBase*);
	using RegisterCr_t   = void (*)(CCallbackBase*, SteamAPICall_t);
	using UnregisterCr_t = void (*)(CCallbackBase*, SteamAPICall_t);
	ISteamHTMLSurface* s_surface     = nullptr;
	RegisterCb_t        s_pfnRegister = nullptr;
	UnregisterCb_t      s_pfnUnreg    = nullptr;
	RegisterCr_t        s_pfnRegCr    = nullptr;
	UnregisterCr_t      s_pfnUnregCr  = nullptr;
	SteamAPICall_t      s_createCall  = 0;
	HHTMLBrowser        s_browser     = INVALID_HTMLBROWSER;
	bool  s_steamTried  = false;
	bool  s_steamDead   = false;
	bool  s_steamCreated = false;
	bool  s_steamStarted = false;

	// Every Steam vtable call goes through its own noinline __try/__except -- C2712 (SEH cannot
	// share a function with an object that needs unwinding) forces the split.
	__declspec(noinline) void SEH_SetSize(HHTMLBrowser b, uint32_t w, uint32_t h) { __try { s_surface->SetSize(b, w, h); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
	__declspec(noinline) void SEH_LoadURL(HHTMLBrowser b, const char* url) { __try { s_surface->LoadURL(b, url, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
	__declspec(noinline) void SEH_AllowStart(HHTMLBrowser b, bool allow) { __try { s_surface->AllowStartRequest(b, allow); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
	__declspec(noinline) void SEH_JSDialog(HHTMLBrowser b, bool result) { __try { s_surface->JSDialogResponse(b, result); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
	__declspec(noinline) void SEH_FileDialog(HHTMLBrowser b) { __try { s_surface->FileLoadDialogResponse(b, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
	__declspec(noinline) void SEH_RemoveBrowser(HHTMLBrowser b) { __try { s_surface->RemoveBrowser(b); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
	__declspec(noinline) bool SEH_Init() { bool ok = false; __try { ok = s_surface->Init(); } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; } return ok; }
	__declspec(noinline) SteamAPICall_t SEH_CreateBrowser(const char* css) { SteamAPICall_t c = 0; __try { c = s_surface->CreateBrowser(nullptr, css); } __except (EXCEPTION_EXECUTE_HANDLER) { c = 0; } return c; }
	__declspec(noinline) void SEH_ExecJs(HHTMLBrowser b, const char* js) { __try { s_surface->ExecuteJavascript(b, js); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
	__declspec(noinline) void SEH_SetKeyFocus(HHTMLBrowser b, bool f) { __try { s_surface->SetKeyFocus(b, f); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
	__declspec(noinline) void SEH_MouseMove(HHTMLBrowser b, int x, int y) { __try { s_surface->MouseMove(b, x, y); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
	__declspec(noinline) void SEH_MouseWheel(HHTMLBrowser b, int32_t d) { __try { s_surface->MouseWheel(b, d); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
	__declspec(noinline) void SEH_MouseBtn(HHTMLBrowser b, bool down, bool right)
	{
		const ISteamHTMLSurface::EHTMLMouseButton btn = right ? ISteamHTMLSurface::eHTMLMouseButton_Right : ISteamHTMLSurface::eHTMLMouseButton_Left;
		__try { if (down) s_surface->MouseDown(b, btn); else s_surface->MouseUp(b, btn); } __except (EXCEPTION_EXECUTE_HANDLER) {}
	}
	__declspec(noinline) void SEH_KeyChar(HHTMLBrowser b, uint32_t c, int mods) { __try { s_surface->KeyChar(b, c, (ISteamHTMLSurface::EHTMLKeyModifiers)mods); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
	__declspec(noinline) void SEH_Key(HHTMLBrowser b, bool down, uint32_t vk, int mods)
	{
		__try { if (down) s_surface->KeyDown(b, vk, (ISteamHTMLSurface::EHTMLKeyModifiers)mods, false); else s_surface->KeyUp(b, vk, (ISteamHTMLSurface::EHTMLKeyModifiers)mods); } __except (EXCEPTION_EXECUTE_HANDLER) {}
	}
	__declspec(noinline) void SEH_Simple(HHTMLBrowser b, int which)
	{
		__try {
			if (which == 0) s_surface->GoBack(b);
			else if (which == 1) s_surface->GoForward(b);
			else s_surface->Reload(b);
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
	}

	void OnBrowserReady(HTML_BrowserReady_t* p)
	{
		if (s_browser != INVALID_HTMLBROWSER) return;
		s_browser = p->unBrowserHandle;
		int w, h; WsAspectRes(g_webAspect, g_webResH, w, h);
		SEH_SetSize(s_browser, (uint32_t)w, (uint32_t)h);
		SEH_SetKeyFocus(s_browser, true);
		if (!s_url.empty()) SEH_LoadURL(s_browser, s_url.c_str());
	}

	void OnNeedsPaint(HTML_NeedsPaint_t* p)
	{
		if (!p->pBGRA || p->unWide == 0 || p->unTall == 0) return;
		WebWv2Sink((const uint8_t*)p->pBGRA, (int)p->unWide, (int)p->unTall, (int)p->unWide * 4);
	}

	void OnStartRequest(HTML_StartRequest_t* p) { SEH_AllowStart(p->unBrowserHandle, !WebUrlBlocked(p->pchURL)); }

	// Steam-backend origin wipe sequence (WS cache clear), botox WebWipeNext/WebWipeTick/WebNoteOrigin.
	std::vector<std::string> s_origins;
	size_t s_wipeIdx   = 0;
	int    s_wipePhase = 0;
	DWORD  s_wipeAt    = 0;

	void WebNoteOrigin(const char* url)
	{
		if (!url || strncmp(url, "http", 4) || s_origins.size() >= 64) return;
		const char* h = strstr(url, "://");
		if (!h) return;
		const char* e = h + 3;
		while (*e && *e != '/' && *e != '?' && *e != '#') ++e;
		std::string o(url, e);
		for (const auto& x : s_origins) if (x == o) return;
		s_origins.push_back(std::move(o));
	}

	void OnURLChanged(HTML_URLChanged_t* p)
	{
		if (p->unBrowserHandle == s_browser && p->pchURL && !strncmp(p->pchURL, "http", 4)) {
			s_url = p->pchURL;
			WebNoteOrigin(p->pchURL);
			s_mediaSeenMs = 0;
		}
	}

	void WebApplyGuardSteam()
	{
		if (s_browser == INVALID_HTMLBROWSER) return;
		SEH_ExecJs(s_browser, k_guardJs);
		SEH_ExecJs(s_browser, k_uaJs);
		SEH_ExecJs(s_browser, k_codecJs);
	}
	void WebApplyAdblockSteam() { if (g_webAdblock && s_browser != INVALID_HTMLBROWSER) SEH_ExecJs(s_browser, WebAdJs().c_str()); }
	void WebApplyVolumeSteam()
	{
		if (s_browser == INVALID_HTMLBROWSER) return;
		float vol = g_webVolume; if (vol < 0.f) vol = 0.f; if (vol > 1.f) vol = 1.f;
		if (g_webVolDistance) vol *= s_distAtten;
		char js[256];
		snprintf(js, sizeof(js), "(function(){try{var v=%.3f;document.querySelectorAll('video,audio')"
		         ".forEach(function(e){e.volume=v;e.muted=(v<=0);});}catch(e){}})()", vol);
		SEH_ExecJs(s_browser, js);
	}

	void OnFinishedRequest(HTML_FinishedRequest_t* p)
	{
		if (s_wipePhase == 1 && p->unBrowserHandle == s_browser) {
			SEH_ExecJs(s_browser, k_wipeJs);
			s_wipePhase = 2; s_wipeAt = GetTickCount();
			return;
		}
		WebApplyGuardSteam();
		WebApplyAdblockSteam();
		SEH_ExecJs(s_browser, k_wsEndedJs);
		SEH_ExecJs(s_browser, k_wsSmoothJs);   // botox: g_webSmooth is a hardcoded `true`, not a real toggle
		WebApplyVolumeSteam();
		// botox: the Steam backend re-probes on EVERY finished request (cheap: a single JS
		// eval, guarded by __fpsDiagArmed inside the probe itself for the expensive diag part).
		SEH_ExecJs(s_browser, k_probeJs);
	}

	// Shared by the Steam backend's real HTML_ChangedTitle_t callback and wv2's EV_TITLE event
	// (Web_MainTick) -- botox csgo-sdk/hacks/web/websurface.cpp's OnChangedTitle, unchanged
	// logic, just split so wv2 (which has no HHTMLBrowser handle to compare) can call it too.
	void WebHandleTitle(const char* m)
	{
		if (!m) return;
		if (!strcmp(m, k_wsEndedTag)) {
			if (g_webAutoRemove && s_placed) Web_Unplace();
			return;
		}
		if (!strncmp(m, "__fpsdiag", 9)) {
			if (strstr(m, "h264mp4=ok"))                s_hasH264 = 1;
			else if (strstr(m, "h264mp4=err4:DECODER")) s_hasH264 = 0;
			return;
		}
		if (strncmp(m, k_probeTag, strlen(k_probeTag))) return;
		snprintf(s_probeLine, sizeof(s_probeLine), "%s", m + strlen(k_probeTag));
		if (char* ua = strstr(s_probeLine, "|ua=")) *ua = 0;
	}

	void OnChangedTitle(HTML_ChangedTitle_t* p) { if (p->unBrowserHandle == s_browser) WebHandleTitle(p->pchTitle); }

	void OnJSAlert(HTML_JSAlert_t* p) { SEH_JSDialog(p->unBrowserHandle, true); }
	void OnJSConfirm(HTML_JSConfirm_t* p) { SEH_JSDialog(p->unBrowserHandle, true); }
	void OnFileDialog(HTML_FileOpenDialog_t* p) { SEH_FileDialog(p->unBrowserHandle); }

	void OnNewWindow(HTML_NewWindow_t* p)
	{
		if (p->unNewWindow_BrowserHandle != INVALID_HTMLBROWSER) SEH_RemoveBrowser(p->unNewWindow_BrowserHandle);
		if (p->pchURL) SEH_LoadURL(s_browser, p->pchURL);
	}

	void OnCloseBrowser(HTML_CloseBrowser_t*) {}

	WsCallback<HTML_BrowserReady_t>    s_cbReady   (&OnBrowserReady);
	WsCallResult<HTML_BrowserReady_t>  s_crReady   (&OnBrowserReady);
	WsCallback<HTML_NeedsPaint_t>      s_cbPaint   (&OnNeedsPaint);
	WsCallback<HTML_StartRequest_t>    s_cbStart   (&OnStartRequest);
	WsCallback<HTML_FinishedRequest_t> s_cbFinished(&OnFinishedRequest);
	WsCallback<HTML_URLChanged_t>      s_cbUrl     (&OnURLChanged);
	WsCallback<HTML_ChangedTitle_t>    s_cbTitle   (&OnChangedTitle);
	WsCallback<HTML_JSAlert_t>         s_cbAlert   (&OnJSAlert);
	WsCallback<HTML_JSConfirm_t>       s_cbConfirm (&OnJSConfirm);
	WsCallback<HTML_FileOpenDialog_t>  s_cbFile    (&OnFileDialog);
	WsCallback<HTML_NewWindow_t>       s_cbNewWin  (&OnNewWindow);
	WsCallback<HTML_CloseBrowser_t>    s_cbClose   (&OnCloseBrowser);

	struct WsCbReg { CCallbackBase* cb; int id; };
	const WsCbReg k_cbs[] = {
		{ &s_cbReady,    HTML_BrowserReady_t::k_iCallback },
		{ &s_cbPaint,    HTML_NeedsPaint_t::k_iCallback },
		{ &s_cbStart,    HTML_StartRequest_t::k_iCallback },
		{ &s_cbFinished, HTML_FinishedRequest_t::k_iCallback },
		{ &s_cbUrl,      HTML_URLChanged_t::k_iCallback },
		{ &s_cbTitle,    HTML_ChangedTitle_t::k_iCallback },
		{ &s_cbAlert,    HTML_JSAlert_t::k_iCallback },
		{ &s_cbConfirm,  HTML_JSConfirm_t::k_iCallback },
		{ &s_cbFile,     HTML_FileOpenDialog_t::k_iCallback },
		{ &s_cbNewWin,   HTML_NewWindow_t::k_iCallback },
		{ &s_cbClose,    HTML_CloseBrowser_t::k_iCallback },
	};

	void WebSteamInit()
	{
		if (s_steamTried) return;
		s_steamTried = true;

		HMODULE h = GetModuleHandleA("steam_api64.dll");
		if (!h) { s_steamDead = true; return; }

		s_pfnRegister = (RegisterCb_t)  GetProcAddress(h, "SteamAPI_RegisterCallback");
		s_pfnUnreg    = (UnregisterCb_t)GetProcAddress(h, "SteamAPI_UnregisterCallback");
		s_pfnRegCr    = (RegisterCr_t)  GetProcAddress(h, "SteamAPI_RegisterCallResult");
		s_pfnUnregCr  = (UnregisterCr_t)GetProcAddress(h, "SteamAPI_UnregisterCallResult");

		if (auto flat = (void* (*)())GetProcAddress(h, "SteamAPI_SteamHTMLSurface_v005"))
			s_surface = (ISteamHTMLSurface*)flat();

		if (!s_surface || !s_pfnRegister) { s_steamDead = true; return; }
		if (!SEH_Init()) { s_steamDead = true; return; }

		for (const WsCbReg& r : k_cbs) s_pfnRegister(r.cb, r.id);
	}

	void WebSteamCreateBrowser()
	{
		if (s_steamCreated || s_steamDead) return;
		s_steamCreated = true;
		const SteamAPICall_t call = SEH_CreateBrowser(g_webAdblock ? k_adCss : nullptr);
		if (!call) { s_steamDead = true; return; }
		s_createCall = call;
		if (s_pfnRegCr) s_pfnRegCr(&s_crReady, call);
		s_steamStarted = true;
	}

	void WebSteamTryStart()
	{
		WebSteamInit();
		if (s_steamDead || !s_surface) return;
		WebSteamCreateBrowser();
	}

	void WebSteamNavigate(const char* url)
	{
		if (!s_steamStarted || s_browser == INVALID_HTMLBROWSER) return;
		SEH_LoadURL(s_browser, url);
	}

	__declspec(noinline) void SEH_UnregCr(SteamAPICall_t call) { __try { s_pfnUnregCr(&s_crReady, call); } __except (EXCEPTION_EXECUTE_HANDLER) {} }

	void WebSteamRecreate()
	{
		if (!s_surface) return;
		if (s_pfnUnregCr && s_createCall) SEH_UnregCr(s_createCall);
		if (s_browser != INVALID_HTMLBROWSER) SEH_RemoveBrowser(s_browser);
		s_browser = INVALID_HTMLBROWSER;
		s_createCall = 0;
		s_steamCreated = false;
		s_steamStarted = false;
		WebSteamCreateBrowser();
	}

	void WebWipeNext()
	{
		if (s_wipeIdx >= s_origins.size() || s_browser == INVALID_HTMLBROWSER) {
			s_wipePhase = 0;
			s_origins.clear();
			WebSteamRecreate();
			return;
		}
		s_wipePhase = 1;
		s_wipeAt = GetTickCount();
		SEH_LoadURL(s_browser, (s_origins[s_wipeIdx] + "/robots.txt").c_str());
	}

	void WebWipeTick()
	{
		if (!s_wipePhase) return;
		const DWORD dt = GetTickCount() - s_wipeAt;
		if (s_wipePhase == 1 && dt > 5000) { SEH_ExecJs(s_browser, k_wipeJs); s_wipePhase = 2; s_wipeAt = GetTickCount(); }
		else if (s_wipePhase == 2 && dt > 800) { ++s_wipeIdx; WebWipeNext(); }
	}

	void WebSteamDrop()
	{
		if (!s_steamCreated) return;
		if (s_pfnUnregCr && s_createCall) SEH_UnregCr(s_createCall);
		if (s_browser != INVALID_HTMLBROWSER && s_surface) SEH_RemoveBrowser(s_browser);
		s_browser = INVALID_HTMLBROWSER;
		s_createCall = 0;
		s_steamCreated = false;
		s_steamStarted = false;
		std::lock_guard<std::mutex> lk(s_pixLock);
		s_pix.clear(); s_pixW = s_pixH = 0;
	}

	void WebBackendTryStart()
	{
		if (s_wv2Started || s_steamStarted) return;
		if (!s_wv2Failed) { WebWv2TryStart(); if (s_wv2Started) return; }
		if (s_wv2Failed) WebSteamTryStart();
	}

	void WebPollMedia()
	{
		static DWORD s_lastAsk = 0;
		if (!s_placed || !s_wv2Started) return;
		const DWORD now = GetTickCount();
		if (now - s_lastAsk < 1000) return;
		s_lastAsk = now;
		wv2::ExecJsResult(k_wsMediaGetJs, k_wsMediaTag);
	}

	void WebWv2Drop()
	{
		if (!s_wv2Started) return;
		wv2::Stop();
		s_wv2Started = false;
		std::lock_guard<std::mutex> lk(s_pixLock);
		s_pix.clear(); s_pixW = s_pixH = 0;
	}

	void WebBackendDrop() { WebWv2Drop(); WebSteamDrop(); }

	bool WebAlive() { return s_wv2Started || (s_steamStarted && s_browser != INVALID_HTMLBROWSER); }

	// Generic exec, both backends. Used by the input helpers below and by WS_VOL etc.
	void WebExecJs(const char* js)
	{
		if (s_wv2Started) wv2::ExecJs(js);
		else if (s_browser != INVALID_HTMLBROWSER) SEH_ExecJs(s_browser, js);
	}

	void WebUploadPixels(IDirect3DDevice9* dev)
	{
		std::lock_guard<std::mutex> lk(s_pixLock);
		if (!s_pixDirty || s_pixW <= 0 || s_pixH <= 0) return;
		if (s_tex && (s_texW != s_pixW || s_texH != s_pixH)) { s_tex->Release(); s_tex = nullptr; }
		if (!s_tex) {
			if (FAILED(dev->CreateTexture(s_pixW, s_pixH, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &s_tex, nullptr)))
				return;
			s_texW = s_pixW; s_texH = s_pixH;
		}
		D3DLOCKED_RECT lr{};
		if (SUCCEEDED(s_tex->LockRect(0, &lr, nullptr, 0))) {
			for (int y = 0; y < s_pixH; y++)
				memcpy((uint8_t*)lr.pBits + (size_t)y * lr.Pitch, &s_pix[(size_t)y * s_pixW * 4], (size_t)s_pixW * 4);
			s_tex->UnlockRect(0);
		}
		s_pixDirty = false;
	}

	void WebReleaseTexture() { if (s_tex) { s_tex->Release(); s_tex = nullptr; } s_texW = s_texH = 0; }

	// ---- placement / occlusion trace: world+props only, local player skipped -----------
	void WebDoPlace()
	{
		float eye[3], angPitchYaw[2];
		if (!CssBridge_GetOrigin(CssBridge_LocalPlayerIndex(), eye) || !CssBridge_GetViewAngles(angPitchYaw))
			return;
		// Real view offset (m_vecViewOffset, DT_BasePlayer localdata, resolved in netvars.cpp
		// g_offViewOffset and read through Css_ViewOffset/css_bridge.cpp) -- 64 standing / ~47
		// crouched per cs_gamemovement.cpp's g_CSViewVectors, not a hardcoded 64. Falls back to
		// standing height only if the netvar read fails (e.g. not yet resolved this session).
		float viewOfs[3] = { 0.f, 0.f, 64.f };
		Css_ViewOffset(CssBridge_LocalPlayerIndex(), viewOfs);
		eye[0] += viewOfs[0]; eye[1] += viewOfs[1]; eye[2] += viewOfs[2];

		WsVec3 ang{ angPitchYaw[0], angPitchYaw[1], 0.f };
		WsVec3 fwd, viewRight, viewUp;
		WsAngleVectors(ang, fwd, viewRight, viewUp);

		float hit[3], normal[3];
		if (!CssBridge_TraceWorld(eye, &fwd.x, 8192.f, hit, normal)) return;
		if (WsLen(WsVec3(normal[0], normal[1], normal[2])) < 0.5f) return;

		s_normal = WsNorm(WsVec3(normal[0], normal[1], normal[2]));
		s_pos    = WsVec3(hit[0], hit[1], hit[2]) + s_normal * 0.5f;
		WsBasisFromNormal(s_normal, fwd, s_right, s_up);
		s_placed = true;
		s_rebake = true;
		++s_seq;
		s_mediaSeenMs = 0;
		WebBackendTryStart();
		if (g_webAutoPlay) {
			if (g_webPlayFromStart) WebExecJs("window.__fpsPlayFrom0=1");
			WebExecJs(k_wsPlayJs);
		}
	}

	// ---- peer screens (botox network senders), one WebView2 slot each -------------------
	struct WsPeer
	{
		bool active = false;
		int  sender = -1;
		WsVec3 pos{}, normal{ 0, 0, 1 }, right{ 1, 0, 0 }, up{ 0, 1, 0 };
		float scale = 1.f;
		int   aspect = 0;
		std::string url;
		std::mutex pixLock;
		std::vector<uint8_t> pix;
		int  pixW = 0, pixH = 0;
		bool pixDirty = false;
		IDirect3DTexture9* tex = nullptr;
		int texW = 0, texH = 0;
		float mediaTime = 0.f;
		bool  mediaPaused = false;
		bool  rebake = false;
		std::vector<WsVert> verts, vertsBack;
		std::vector<uint16_t> idx;
	};
	WsPeer s_peer[wv2::k_peers]{};

	int WebPeerFind(int sender)
	{
		for (int i = 0; i < wv2::k_peers; i++)
			if (s_peer[i].active && s_peer[i].sender == sender) return i;
		return -1;
	}

	void WebPeerSink(int slot, const uint8_t* bgra, int w, int h, int pitch)
	{
		if (slot < 0 || slot >= wv2::k_peers) return;
		WsPeer& p = s_peer[slot];
		std::lock_guard<std::mutex> lk(p.pixLock);
		p.pix.resize((size_t)w * h * 4);
		for (int y = 0; y < h; y++)
			memcpy(&p.pix[(size_t)y * w * 4], bgra + (size_t)y * pitch, (size_t)w * 4);
		p.pixW = w; p.pixH = h; p.pixDirty = true;
	}

	void WebPeerFree(int i)
	{
		WsPeer& p = s_peer[i];
		if (!p.active) return;
		wv2::PeerStop(i);
		if (p.tex) { p.tex->Release(); p.tex = nullptr; }
		p.active = false; p.sender = -1;
		p.pos = WsVec3(); p.normal = WsVec3(0, 0, 1); p.right = WsVec3(1, 0, 0); p.up = WsVec3(0, 1, 0);
		p.scale = 1.f; p.aspect = 0; p.url.clear();
		{ std::lock_guard<std::mutex> lk(p.pixLock); p.pix.clear(); }
		p.pixW = p.pixH = 0; p.pixDirty = false;
		p.texW = p.texH = 0; p.mediaTime = 0.f; p.mediaPaused = false;
		p.verts.clear(); p.vertsBack.clear(); p.idx.clear(); p.rebake = false;
	}

	void WebPeerUpload(IDirect3DDevice9* dev, WsPeer& p)
	{
		std::lock_guard<std::mutex> lk(p.pixLock);
		if (!p.pixDirty || p.pixW <= 0 || p.pixH <= 0) return;
		if (p.tex && (p.texW != p.pixW || p.texH != p.pixH)) { p.tex->Release(); p.tex = nullptr; }
		if (!p.tex) {
			if (FAILED(dev->CreateTexture(p.pixW, p.pixH, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &p.tex, nullptr)))
				return;
			p.texW = p.pixW; p.texH = p.pixH;
		}
		D3DLOCKED_RECT lr{};
		if (SUCCEEDED(p.tex->LockRect(0, &lr, nullptr, 0))) {
			for (int y = 0; y < p.pixH; y++)
				memcpy((uint8_t*)lr.pBits + (size_t)y * lr.Pitch, &p.pix[(size_t)y * p.pixW * 4], (size_t)p.pixW * 4);
			p.tex->UnlockRect(0);
		}
		p.pixDirty = false;
	}

	void WsMediaSyncJs(char* js, size_t n, float t, bool paused)
	{
		snprintf(js, n,
			"(function(){try{" WS_MAINVIDEO_JS
			"var v=M(),t=%.2f,s=%d,p=document.getElementById('movie_player');if(!v)return;"
			"if(Math.abs(v.currentTime-t)>2){if(p&&p.seekTo)p.seekTo(t,true);else v.currentTime=t;}"
			"if(s&&!v.paused){if(p&&p.pauseVideo)p.pauseVideo();else v.pause();}"
			"else if(!s&&v.paused){if(p&&p.playVideo)p.playVideo();var r=v.play();if(r&&r.catch)r.catch(function(){});}"
			"}catch(e){}})()", t, paused ? 1 : 0);
	}

	// ---- D3D state save/restore + mesh draw (the named render bug fix) -------------------
	struct WsSavedState
	{
		IDirect3DPixelShader9*       ps;
		IDirect3DVertexShader9*      vs;
		IDirect3DVertexDeclaration9* decl;
		IDirect3DBaseTexture9*       tex0;
		IDirect3DVertexBuffer9*      stream;
		IDirect3DIndexBuffer9*       indices;
		UINT   streamOffset, streamStride;
		DWORD  fvf;
		D3DMATRIX world, view, proj;
		DWORD  rs[14];
		DWORD  samp[6];
		DWORD  tss[8];
		DWORD  tss1[2];
	};
	const D3DRENDERSTATETYPE k_wsRS[14] = {
		D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ZFUNC, D3DRS_ALPHABLENDENABLE,
		D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_ALPHATESTENABLE, D3DRS_CULLMODE,
		D3DRS_LIGHTING, D3DRS_FOGENABLE, D3DRS_SCISSORTESTENABLE,
		D3DRS_COLORWRITEENABLE, D3DRS_SRGBWRITEENABLE, D3DRS_CLIPPLANEENABLE,
	};
	const D3DSAMPLERSTATETYPE k_wsSamp[6] = {
		D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_MAGFILTER,
		D3DSAMP_MINFILTER, D3DSAMP_MIPFILTER, D3DSAMP_SRGBTEXTURE,
	};
	const D3DTEXTURESTAGESTATETYPE k_wsTSS[8] = {
		D3DTSS_COLOROP, D3DTSS_COLORARG1, D3DTSS_COLORARG2,
		D3DTSS_ALPHAOP, D3DTSS_ALPHAARG1, D3DTSS_ALPHAARG2,
		D3DTSS_TEXCOORDINDEX, D3DTSS_TEXTURETRANSFORMFLAGS,
	};

	void WsSave(IDirect3DDevice9* dev, WsSavedState& s)
	{
		dev->GetPixelShader(&s.ps);
		dev->GetVertexShader(&s.vs);
		dev->GetVertexDeclaration(&s.decl);
		dev->GetTexture(0, &s.tex0);
		dev->GetStreamSource(0, &s.stream, &s.streamOffset, &s.streamStride);
		dev->GetIndices(&s.indices);
		dev->GetFVF(&s.fvf);
		dev->GetTransform(D3DTS_WORLD, &s.world);
		dev->GetTransform(D3DTS_VIEW, &s.view);
		dev->GetTransform(D3DTS_PROJECTION, &s.proj);
		for (int i = 0; i < 14; ++i) dev->GetRenderState(k_wsRS[i], &s.rs[i]);
		for (int i = 0; i < 6;  ++i) dev->GetSamplerState(0, k_wsSamp[i], &s.samp[i]);
		for (int i = 0; i < 8;  ++i) dev->GetTextureStageState(0, k_wsTSS[i], &s.tss[i]);
		dev->GetTextureStageState(1, D3DTSS_COLOROP, &s.tss1[0]);
		dev->GetTextureStageState(1, D3DTSS_ALPHAOP, &s.tss1[1]);
	}

	void WsRestore(IDirect3DDevice9* dev, WsSavedState& s)
	{
		for (int i = 0; i < 14; ++i) dev->SetRenderState(k_wsRS[i], s.rs[i]);
		for (int i = 0; i < 6;  ++i) dev->SetSamplerState(0, k_wsSamp[i], s.samp[i]);
		for (int i = 0; i < 8;  ++i) dev->SetTextureStageState(0, k_wsTSS[i], s.tss[i]);
		dev->SetTextureStageState(1, D3DTSS_COLOROP, s.tss1[0]);
		dev->SetTextureStageState(1, D3DTSS_ALPHAOP, s.tss1[1]);
		dev->SetTransform(D3DTS_WORLD, &s.world);
		dev->SetTransform(D3DTS_VIEW, &s.view);
		dev->SetTransform(D3DTS_PROJECTION, &s.proj);
		dev->SetTexture(0, s.tex0);
		dev->SetPixelShader(s.ps);
		dev->SetVertexShader(s.vs);
		if (s.decl) dev->SetVertexDeclaration(s.decl); else dev->SetFVF(s.fvf);
		dev->SetStreamSource(0, s.stream, s.streamOffset, s.streamStride);
		dev->SetIndices(s.indices);

		if (s.tex0)    s.tex0->Release();
		if (s.ps)      s.ps->Release();
		if (s.vs)      s.vs->Release();
		if (s.decl)    s.decl->Release();
		if (s.stream)  s.stream->Release();
		if (s.indices) s.indices->Release();
	}

	const D3DMATRIX k_wsIdentity = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };

	// depthTest false = D3DRS_ZENABLE off (always on top, no occlusion by world geometry).
	__declspec(noinline) void WsDrawTris(IDirect3DDevice9* dev, bool depthTest, bool mirrorU, IDirect3DTexture9* tex,
	                                     const WsVert* verts, UINT nv, const uint16_t* idx, UINT ntri, DWORD col)
	{
		if (!dev || !tex || !nv || !ntri) return;
		const float(*m)[4] = Render_W2SMatrix();
		if (!m) return;   // no frame matrix yet -- nothing safe to project with

		static std::vector<WsVert> s_scratch;
		s_scratch.assign(verts, verts + nv);
		for (WsVert& v : s_scratch) { v.col = col; if (mirrorU) v.u = 1.f - v.u; }

		WsSavedState st{};
		__try {
			WsSave(dev, st);

			dev->SetVertexShader(nullptr);
			dev->SetPixelShader(nullptr);
			dev->SetVertexDeclaration(nullptr);
			dev->SetFVF(k_wsFvf);

			// THE FIX: world/view = identity, projection = this frame's real world-to-screen
			// matrix (transposed, same as botox's get_world_to_screen_matrix path) -- botox's
			// WebDrawQuad never set any of these three, so the fixed-function pipeline drew
			// with whatever the material system's shader state last left in them.
			float vp[16];
			WsTranspose4(&m[0][0], vp);
			D3DMATRIX proj;
			memcpy(&proj, vp, sizeof(proj));
			dev->SetTransform(D3DTS_WORLD, &k_wsIdentity);
			dev->SetTransform(D3DTS_VIEW, &k_wsIdentity);
			dev->SetTransform(D3DTS_PROJECTION, &proj);

			dev->SetRenderState(D3DRS_ZENABLE, depthTest ? TRUE : FALSE);
			dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
			dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
			dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
			dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
			dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
			dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
			dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
			dev->SetRenderState(D3DRS_LIGHTING, FALSE);
			dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
			dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
			dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
			dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
			dev->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);

			dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
			dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
			dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
			dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);

			dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
			dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
			dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
			dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
			dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
			dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
			dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
			dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
			dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
			dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

			dev->SetTexture(0, tex);
			dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, nv, ntri, idx, D3DFMT_INDEX16, s_scratch.data(), sizeof(WsVert));
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
		__try { WsRestore(dev, st); } __except (EXCEPTION_EXECUTE_HANDLER) {}
	}

	// facing: true if the viewer is on the normal's side. Returns false = cull (back face, mode 1).
	bool WsPickFace(bool facing, bool& mirrorU)
	{
		mirrorU = false;
		if (facing) return true;
		if (g_webBackMode == 1) return false;        // hide back face
		if (g_webBackMode == 2) { mirrorU = true; return true; }  // mirror image on the back
		return true;                                  // draw both sides, same image
	}

	void WebDrawWorld(IDirect3DDevice9* dev)
	{
		if (!dev || !s_tex || !s_placed) return;
		float a = g_webWorldOpacity; if (a <= 0.003f) return; if (a > 1.f) a = 1.f;
		const DWORD col = D3DCOLOR_ARGB((int)(a * 255.f + 0.5f), 255, 255, 255);

		const bool facing = s_viewValid ? WsDot(s_viewOrigin - s_pos, s_normal) > 0.f : true;
		bool mirrorU;
		if (!WsPickFace(facing, mirrorU)) return;

		std::lock_guard<std::mutex> lk(s_meshLock);
		if (s_verts.empty() || s_idx.empty()) return;
		WsDrawTris(dev, g_webDepthMode == 0, mirrorU, s_tex, s_verts.data(), (UINT)s_verts.size(),
		           s_idx.data(), (UINT)(s_idx.size() / 3), col);
	}

	void WebPeerBake(WsPeer& p)
	{
		std::vector<WsVert> verts, vertsBack;
		std::vector<uint16_t> idx;
		float ww, wh; WsAspectDims(p.aspect, p.scale, ww, wh);
		WsBuildMesh(p.pos, p.right, p.up, ww, wh, verts, vertsBack, idx);
		p.verts.swap(verts); p.vertsBack.swap(vertsBack); p.idx.swap(idx);
		p.rebake = false;
	}

	void WebDrawPeers(IDirect3DDevice9* dev)
	{
		if (!dev) return;
		float a = g_webWorldOpacity; if (a <= 0.003f) return; if (a > 1.f) a = 1.f;
		const DWORD col = D3DCOLOR_ARGB((int)(a * 255.f + 0.5f), 255, 255, 255);
		for (WsPeer& p : s_peer) {
			if (!p.active || !p.tex) continue;
			if (p.rebake) WebPeerBake(p);
			if (p.verts.empty() || p.idx.empty()) continue;
			const bool facing = s_viewValid ? WsDot(s_viewOrigin - p.pos, p.normal) > 0.f : true;
			bool mirrorU;
			if (!WsPickFace(facing, mirrorU)) continue;
			WsDrawTris(dev, g_webDepthMode == 0, mirrorU, p.tex, p.verts.data(), (UINT)p.verts.size(),
			           p.idx.data(), (UINT)(p.idx.size() / 3), col);
		}
	}

	// ---- input: SEH-wrapped, dispatch to whichever backend is live ----------------------
	int s_mouseX = 0, s_mouseY = 0;

	int WebMods()
	{
		int m = 0;
		if (GetAsyncKeyState(VK_MENU)    & 0x8000) m |= wv2::KMOD_ALT;
		if (GetAsyncKeyState(VK_CONTROL) & 0x8000) m |= wv2::KMOD_CTRL;
		if (GetAsyncKeyState(VK_SHIFT)   & 0x8000) m |= wv2::KMOD_SHIFT;
		return m;
	}

	void WebResPx(int& w, int& h) { WsAspectRes(g_webAspect, g_webResH, w, h); }

	void WebMouseMovePx(int x, int y)
	{
		s_mouseX = x; s_mouseY = y;
		if (s_wv2Started) wv2::MouseInput(wv2::M_MOVE, x, y);
		else if (s_browser != INVALID_HTMLBROWSER) SEH_MouseMove(s_browser, x, y);
	}
	const int k_wsAway = -1;
	void WebMousePark() { if (WebAlive()) WebMouseMovePx(k_wsAway, k_wsAway); }

	void WebMouseAt(float u, float v)
	{
		if (!WebAlive()) return;
		int rw, rh; WebResPx(rw, rh);
		WebMouseMovePx((int)(u * (float)rw), (int)(v * (float)rh));
	}

	void WebMouseButton(bool down, bool right)
	{
		if (!WebAlive()) return;
		if (s_wv2Started) wv2::MouseInput(down ? (right ? wv2::M_RDOWN : wv2::M_LDOWN) : (right ? wv2::M_RUP : wv2::M_LUP), s_mouseX, s_mouseY);
		else if (s_browser != INVALID_HTMLBROWSER) SEH_MouseBtn(s_browser, down, right);
	}

	void WebWheel(int delta100)
	{
		if (!WebAlive()) return;
		if (s_wv2Started) wv2::MouseInput(wv2::M_WHEEL, s_mouseX, s_mouseY, delta100 * WHEEL_DELTA / 100);
		else if (s_browser != INVALID_HTMLBROWSER) SEH_MouseWheel(s_browser, (int32_t)delta100);
	}

	void WebKeyChar(uint32_t c)
	{
		if (!WebAlive()) return;
		if (s_wv2Started) wv2::KeyInput(wv2::K_CHAR, c, WebMods());
		else if (s_browser != INVALID_HTMLBROWSER) SEH_KeyChar(s_browser, c, WebMods());
	}

	void WebKey(bool down, uint32_t vk)
	{
		if (!WebAlive()) return;
		if (s_wv2Started) wv2::KeyInput(down ? wv2::K_DOWN : wv2::K_UP, vk, WebMods());
		else if (s_browser != INVALID_HTMLBROWSER) SEH_Key(s_browser, down, vk, WebMods());
	}

	bool WebGameFocused() { return g_hwnd && GetForegroundWindow() == g_hwnd; }

	// The keys that never arrive as characters -- WebPanelKeys forwards the rest via ImGui's
	// InputQueueCharacters.
	const int k_wsNavKeys[] = { VK_BACK, VK_RETURN, VK_TAB, VK_DELETE, VK_ESCAPE,
	                            VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN, VK_HOME, VK_END,
	                            VK_PRIOR, VK_NEXT };
	constexpr int k_wsNavCount = (int)(sizeof(k_wsNavKeys) / sizeof(k_wsNavKeys[0]));
	bool s_navDown[k_wsNavCount] = {};

	void WebPanelKeysRelease()
	{
		for (int i = 0; i < k_wsNavCount; ++i)
			if (s_navDown[i]) { s_navDown[i] = false; WebKey(false, k_wsNavKeys[i]); }
	}

	void WebPanelKeys()
	{
		if (!WebAlive()) return;
		ImGuiIO& io = ImGui::GetIO();
		// WantTextInput is true while the address bar (an ImGui InputText) itself has focus --
		// typing a URL must not also land in the page. !WebGameFocused(): alt-tabbed away,
		// GetAsyncKeyState would be reading a different window's keyboard state.
		if (io.WantTextInput || !WebGameFocused()) { WebPanelKeysRelease(); return; }

		for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
			const ImWchar c = io.InputQueueCharacters[i];
			if (c >= 32) WebKeyChar((uint32_t)c);
		}
		for (int i = 0; i < k_wsNavCount; ++i) {
			const bool down = (GetAsyncKeyState(k_wsNavKeys[i]) & 0x8000) != 0;
			if (down == s_navDown[i]) continue;
			s_navDown[i] = down;
			WebKey(down, k_wsNavKeys[i]);
		}
	}

	// ---- panel UI (self-contained ImGui window, no src/hacks/menu/* edit) ---------------
	constexpr float kWebRound = 5.f;
	ImVec2 s_imgPos(0.f, 0.f), s_imgSize(0.f, 0.f);
	bool   s_mouseDownPanel = false, s_rDownPanel = false, s_panelPtrIn = false;

	void WebPanelChromeless()
	{
		if (!s_tex || s_imgSize.x < 2.f || s_imgSize.y < 2.f) return;
		ImGui::SetNextWindowPos(s_imgPos);
		ImGui::SetNextWindowSize(s_imgSize);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
		                               ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
		                               ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
		if (ImGui::Begin("##websurf_bare", nullptr, flags)) {
			float pa = g_webPanelOpacity; if (pa < 0.f) pa = 0.f; if (pa > 1.f) pa = 1.f;
			ImGui::GetWindowDrawList()->AddImageRounded(
				(ImTextureID)s_tex, s_imgPos, ImVec2(s_imgPos.x + s_imgSize.x, s_imgPos.y + s_imgSize.y),
				ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, (int)(pa * 255.f + 0.5f)), kWebRound);
		}
		ImGui::End();
		ImGui::PopStyleVar();
	}

	bool WebBtn(const char* id, const char* label, ImVec2 size)
	{
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const bool pressed = ImGui::InvisibleButton(id, size);
		const bool hov = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 q(p.x + size.x, p.y + size.y);
		dl->AddRectFilled(p, q, IM_COL32(255, 255, 255, held ? 24 : (hov ? 16 : 10)), 5.f);
		dl->AddRect(p, q, IM_COL32(255, 255, 255, hov ? 32 : 18), 5.f, 0, 1.f);
		if (label) {
			const ImVec2 ts = ImGui::CalcTextSize(label);
			dl->AddText(ImVec2((p.x + q.x - ts.x) * 0.5f, (p.y + q.y - ts.y) * 0.5f),
			            IM_COL32(210, 210, 214, 255), label);
		}
		return pressed;
	}

	void WebPanel()
	{
		if (!g_webPanel) return;
		if (g_webPanelAlways && g_showMenu == false) WebPanelChromeless();
		if (!g_showMenu) return;

		float pa = g_webPanelOpacity; if (pa < 0.f) pa = 0.f; if (pa > 1.f) pa = 1.f;
		const float aspect = s_texH > 0 ? (float)s_texW / (float)s_texH : 16.f / 9.f;
		const float w = 480.f;

		ImGui::SetNextWindowSize(ImVec2(w, w / aspect + 110.f), ImGuiCond_FirstUseEver);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, kWebRound);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f, 10.f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(240.f, 160.f));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.f, 5.f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.f, 6.f));
		ImGui::PushStyleColor(ImGuiCol_FrameBg,        IM_COL32(255, 255, 255, 10));
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(255, 255, 255, 18));
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  IM_COL32(255, 255, 255, 20));
		ImGui::PushStyleColor(ImGuiCol_Border,         IM_COL32(255, 255, 255, 18));
		ImGui::PushStyleColor(ImGuiCol_Text,           IM_COL32(210, 210, 214, 255));
		auto popAll = [] { ImGui::End(); ImGui::PopStyleColor(5); ImGui::PopStyleVar(7); };

		if (!ImGui::Begin("Web##websurf", &g_webPanel, ImGuiWindowFlags_NoCollapse)) { popAll(); return; }

		static char s_urlBuf[512] = {};
		static bool s_urlInit = false;
		if (!s_urlInit) { strncpy_s(s_urlBuf, g_webUrl.c_str(), _TRUNCATE); s_urlInit = true; }

		const float fh = 17.f, gap = ImGui::GetStyle().ItemSpacing.x;
		if (WebBtn("##webback", "<", ImVec2(fh, fh))) WebExecJs("history.back()");
		ImGui::SameLine();
		if (WebBtn("##webfwd", ">", ImVec2(fh, fh))) WebExecJs("history.forward()");
		ImGui::SameLine();
		if (WebBtn("##webreload", "R", ImVec2(fh, fh))) WebExecJs("location.reload()");
		ImGui::SameLine();
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.f, 0.f));
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 44.f - gap);
		const bool entered = ImGui::InputTextWithHint("##weburl", "https://...", s_urlBuf, sizeof(s_urlBuf),
		                                              ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::PopStyleVar();
		ImGui::SameLine();
		if (WebBtn("##webgo", "Go", ImVec2(44.f, fh)) || entered) Web_Navigate(s_urlBuf);

		const float btnW = (ImGui::GetContentRegionAvail().x - gap * 3.f) / 4.f;
		if (WebBtn("##webplace", "Place", ImVec2(btnW, fh))) Web_PlaceAtCrosshair();
		ImGui::SameLine();
		if (WebBtn("##webremove", "Remove", ImVec2(btnW, fh))) Web_Unplace();
		ImGui::SameLine();
		if (WebBtn("##webclear", "Cache", ImVec2(btnW, fh))) Web_ClearCache();
		ImGui::SameLine();
		if (WebBtn("##webclrmem", "Memory", ImVec2(btnW, fh))) Web_ClearMemory();

		const ImVec2 avail = ImGui::GetContentRegionAvail();
		float dw = avail.x, dh = dw / aspect;
		if (dh > avail.y && avail.y > 1.f) { dh = avail.y; dw = dh * aspect; }
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		if (!s_tex) { ImGui::TextDisabled("%s", Web_Status()); popAll(); return; }

		ImGui::Dummy(ImVec2(dw, dh));
		ImGui::GetWindowDrawList()->AddImageRounded(
			(ImTextureID)s_tex, p0, ImVec2(p0.x + dw, p0.y + dh), ImVec2(0, 0), ImVec2(1, 1),
			IM_COL32(255, 255, 255, (int)(pa * 255.f + 0.5f)), kWebRound);
		s_imgPos = p0; s_imgSize = ImVec2(dw, dh);

		const bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
		const bool rdown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
		if (ImGui::IsItemHovered() && dw > 1.f && dh > 1.f) {
			const ImVec2 m = ImGui::GetIO().MousePos;
			const float u = (m.x - p0.x) / dw, v = (m.y - p0.y) / dh;
			int rw, rh; WebResPx(rw, rh);
			const float wheel = ImGui::GetIO().MouseWheel;
			if (!g_webClickOnly || down || rdown || s_mouseDownPanel || s_rDownPanel || wheel != 0.f) {
				WebMouseMovePx((int)(u * (float)rw), (int)(v * (float)rh));
				s_panelPtrIn = true;
			}
			if (down != s_mouseDownPanel) { WebMouseButton(down, false); s_mouseDownPanel = down; }
			if (rdown != s_rDownPanel)    { WebMouseButton(rdown, true); s_rDownPanel = rdown; }
			if (wheel != 0.f) WebWheel((int)(wheel * 100.f));
		} else {
			if (s_mouseDownPanel && !down)  { WebMouseButton(false, false); s_mouseDownPanel = false; }
			if (s_rDownPanel && !rdown)     { WebMouseButton(false, true);  s_rDownPanel = false; }
			if (s_panelPtrIn && !s_mouseDownPanel && !s_rDownPanel) { WebMousePark(); s_panelPtrIn = false; }
		}
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) WebPanelKeys();
		else WebPanelKeysRelease();

		popAll();
	}
}

// ================ public API (websurface.h) ================

bool Web_On() { return g_webEnable && !g_eject.load(std::memory_order_relaxed); }

bool Web_ShareGet(web_share_t& out)
{
	if (!Web_On() || !s_placed) return false;
	out.m_placed = true;
	out.m_pos[0] = s_pos.x; out.m_pos[1] = s_pos.y; out.m_pos[2] = s_pos.z;
	out.m_normal[0] = s_normal.x; out.m_normal[1] = s_normal.y; out.m_normal[2] = s_normal.z;
	out.m_right[0] = s_right.x; out.m_right[1] = s_right.y; out.m_right[2] = s_right.z;
	out.m_scale = g_webScale; out.m_aspect = g_webAspect; out.m_seq = s_seq;
	out.m_url = s_url;
	out.m_media = s_mediaSeenMs && GetTickCount() - s_mediaSeenMs < 3000;
	out.m_time = s_mediaTime; out.m_paused = s_mediaPaused;
	return true;
}

bool Web_PeerShow(int sender, const web_share_t& in)
{
	if (!Web_On()) return false;
	int slot = WebPeerFind(sender);
	if (slot < 0) {
		for (int i = 0; i < wv2::k_peers; i++) if (!s_peer[i].active) { slot = i; break; }
		if (slot < 0) return false;
		if (!wv2::PeerStart(slot, 512, 288, WebPeerSink, WebWv2Filter)) return false;
		s_peer[slot].active = true;
		s_peer[slot].sender  = sender;
	}
	WsPeer& p = s_peer[slot];
	p.pos    = WsVec3(in.m_pos[0], in.m_pos[1], in.m_pos[2]);
	p.normal = WsNorm(WsVec3(in.m_normal[0], in.m_normal[1], in.m_normal[2]));
	p.right  = WsNorm(WsVec3(in.m_right[0], in.m_right[1], in.m_right[2]));
	p.up     = WsNorm(WsCross(p.normal, p.right));
	p.scale  = in.m_scale; p.aspect = in.m_aspect;
	p.rebake = true;
	if (in.m_url != p.url) { p.url = in.m_url; wv2::PeerNavigate(slot, p.url.c_str()); }
	return true;
}

void Web_PeerHide(int sender)
{
	const int slot = WebPeerFind(sender);
	if (slot >= 0) WebPeerFree(slot);
}

void Web_PeerMedia(int sender, float time, bool paused)
{
	const int slot = WebPeerFind(sender);
	if (slot < 0) return;
	s_peer[slot].mediaTime = time; s_peer[slot].mediaPaused = paused;
	char js[256];
	WsMediaSyncJs(js, sizeof(js), time, paused);
	wv2::PeerExecJs(slot, js);
}

void Web_Present(IDirect3DDevice9* dev)
{
	if (!Web_On() || !dev) return;
	WebUploadPixels(dev);
	for (WsPeer& p : s_peer) if (p.active) WebPeerUpload(dev, p);

	if (g_webDepthMode != 2) { WebDrawWorld(dev); WebDrawPeers(dev); }

	WebPanel();

	if (g_webDepthMode == 2) { WebDrawWorld(dev); WebDrawPeers(dev); }   // drawn LAST, always on top of the panel too
}

void Web_MainTick()
{
	if (!Web_On()) {
		WebBackendDrop();
		for (int i = 0; i < wv2::k_peers; i++) WebPeerFree(i);
		return;
	}
	if (s_placeRequested) { s_placeRequested = false; WebDoPlace(); }
	if (s_placed) WebBackendTryStart();

	wv2::Event ev;
	while (s_wv2Started && wv2::PollEvent(ev)) {
		switch (ev.type) {
		case wv2::EV_READY:
			wv2::AddStartScript(k_guardJs);
			wv2::AddStartScript(k_uaJs);
			wv2::AddStartScript(k_codecJs);
			wv2::AddStartScript(k_wsEndedJs);
			wv2::AddStartScript(k_wsSmoothJs);   // botox: g_webSmooth is a hardcoded `true`
			if (g_webAdblock) wv2::AddStartScript(WebAdJs().c_str());
			break;
		case wv2::EV_URL:
			s_url = ev.a; s_mediaSeenMs = 0;
			break;
		case wv2::EV_FINISHED:
			if (g_webAutoPlay) wv2::ExecJs(k_wsPlayJs);
			// botox WebWv2Pump's EV_FINISHED -> OnFinishedRequest(&f) path: wv2 only ever
			// probes ONCE (s_probeLine starts empty), with __fpsNoDiag=1 first so the probe's
			// own 8s/30s diag sweep never arms on this backend.
			if (!s_probeLine[0]) { wv2::ExecJs("window.__fpsNoDiag=1"); wv2::ExecJs(k_probeJs); }
			break;
		case wv2::EV_TITLE:
			WebHandleTitle(ev.a.c_str());
			break;
		case wv2::EV_JS:
			if (ev.n == k_wsMediaTag) {
				const double v = strtod(ev.a.c_str(), nullptr);
				if (std::isfinite(v) && fabs(v) >= 1.0) {
					s_mediaPaused = v < 0.0;
					s_mediaTime   = (float)(fabs(v) - 1.0);
					s_mediaSeenMs = GetTickCount();
				}
			}
			break;
		default: break;
		}
	}
	WebPollMedia();
	WebWipeTick();

	for (int i = 0; i < wv2::k_peers; i++) {
		while (s_peer[i].active && wv2::PeerPollEvent(i, ev)) {
			if (ev.type == wv2::EV_READY) {
				wv2::PeerAddStartScript(i, k_guardJs);
				wv2::PeerAddStartScript(i, k_wsEndedJs);
				wv2::PeerAddStartScript(i, k_wsSmoothJs);   // botox: g_webSmooth is a hardcoded `true`
				if (g_webAdblock) wv2::PeerAddStartScript(i, WebAdJs().c_str());
			}
		}
	}
}

void Web_OnPostScreenSpaceEffects(const void* setup)
{
	if (g_eject.load(std::memory_order_relaxed)) return;
	if (!g_webEnable) { s_viewValid = false; return; }

	// Raw CViewSetup offsets, same verified layout screenfx.cpp's FxReadViewSetup uses
	// (public/view_shared.h: every member up to zFar is a 4-byte int/float/bool, so x64 keeps
	// the 32-bit SDK's layout) -- NOT botox's offsetof(c_view_setup,...), aim/defs.h is off
	// limits outside src/hacks/aim/ (INCLUDE RULE).
	if (setup) {
		constexpr size_t k_vsOrigin = 64, k_vsAngles = 76;
		__try {
			const uintptr_t p = reinterpret_cast<uintptr_t>(setup);
			const float* o = reinterpret_cast<const float*>(p + k_vsOrigin);
			const float* a = reinterpret_cast<const float*>(p + k_vsAngles);
			WsVec3 fwd, right, up;
			WsAngleVectors(WsVec3(a[0], a[1], a[2]), fwd, right, up);
			s_viewOrigin = WsVec3(o[0], o[1], o[2]);
			s_viewFwd    = fwd;
			s_viewValid  = true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { s_viewValid = false; }
	}

	// Place/remove binds -- momentary, edge-detected. Dead with the menu open; no shared
	// "is text being typed" gate is exposed to this file yet (NEEDS FROM OTHERS, see report),
	// so this checks the menu + foreground window only, same as botox's own g_showMenu check.
	if (!g_showMenu && WebGameFocused()) {
		const bool placeNow = g_webPlaceKey && (GetAsyncKeyState(g_webPlaceKey) & 0x8000) != 0;
		if (placeNow && !s_placeDown) s_placeRequested = true;
		s_placeDown = placeNow;
		const bool removeNow = g_webRemoveKey && (GetAsyncKeyState(g_webRemoveKey) & 0x8000) != 0;
		if (removeNow && !s_removeDown) Web_Unplace();
		s_removeDown = removeNow;
	} else {
		s_placeDown = s_removeDown = false;
	}
	if (s_placeRequested) { s_placeRequested = false; WebDoPlace(); }

	{
		static float s_lastW = -1.f, s_lastH = -1.f, s_lastRound = -1.f;
		const float ww = QuadWidth(), wh = QuadHeight();
		if (s_lastW != ww || s_lastH != wh || s_lastRound != g_webRound) {
			s_lastW = ww; s_lastH = wh; s_lastRound = g_webRound;
			s_rebake = true;
		}
	}
	if (s_rebake) WebBakeMesh();

	// Distance volume attenuation, local player's eye origin.
	if (g_webVolDistance && s_placed) {
		float eye[3];
		if (CssBridge_GetOrigin(CssBridge_LocalPlayerIndex(), eye)) {
			const WsVec3 eyeV(eye[0], eye[1], eye[2]);
			const float d  = WsLen(eyeV - s_pos);
			const float n0 = g_webVolNear;
			const float f0 = g_webVolFar > n0 + 1.f ? g_webVolFar : n0 + 1.f;
			s_distAtten = (d <= n0) ? 1.f : (d >= f0 ? 0.f : 1.f - (d - n0) / (f0 - n0));
		}
	} else {
		s_distAtten = 1.f;
	}
	if (WebAlive()) {
		static float s_lastVol = -1.f, s_lastAtten = -1.f;
		static DWORD s_lastVolMs = 0;
		const DWORD now = GetTickCount();
		const float atten = g_webVolDistance ? s_distAtten : 1.f;
		if (s_lastVol != g_webVolume || fabsf(atten - s_lastAtten) > 0.02f || now - s_lastVolMs > 1000) {
			s_lastVol = g_webVolume; s_lastAtten = atten; s_lastVolMs = now;
			if (s_wv2Started) {
				char js[256];
				snprintf(js, sizeof(js), "(function(){try{var v=%.3f;document.querySelectorAll('video,audio')"
				         ".forEach(function(e){e.volume=v;e.muted=(v<=0);});}catch(e){}})()", atten * (g_webVolume < 0.f ? 0.f : (g_webVolume > 1.f ? 1.f : g_webVolume)));
				wv2::ExecJs(js);
			} else {
				WebApplyVolumeSteam();
			}
		}
	}

	// Crosshair-on-quad detection, occlusion trace, mouse-pointer drive -- main thread (same
	// pattern screenfx.cpp's own focus trace uses off this hook).
	bool in = false;
	float t = 0.f;
	if (s_placed && s_viewValid && !g_showMenu && WsRayPlane(s_viewOrigin, s_viewFwd, s_pos, s_normal, t)) {
		float u, v;
		const float ww = QuadWidth(), wh = QuadHeight();
		WsQuadUV(s_viewOrigin + s_viewFwd * t, s_pos, s_right, s_up, ww, wh, u, v);
		in = WsInQuad(u, v);
		if (in && g_webDepthMode == 0 && t > 2.f) {
			float hit[3], normal[3];
			const WsVec3 end = s_viewOrigin + s_viewFwd * (t - 2.f);
			const WsVec3 dir = WsNorm(end - s_viewOrigin);
			if (CssBridge_TraceWorld(&s_viewOrigin.x, &dir.x, t - 2.f, hit, normal)) {
				const WsVec3 hitV(hit[0], hit[1], hit[2]);
				if (WsLen(hitV - s_viewOrigin) < t - 2.f - 1.f) in = false;   // something solid before the quad
			}
		}
		if (in) {
			const bool facing = WsDot(s_viewOrigin - s_pos, s_normal) > 0.f;
			s_crossU = (!facing && g_webBackMode == 2) ? 1.f - u : u;
			s_crossV = v;
		}
	}
	s_crossInside = in;

	if (s_placed && s_viewValid && g_webCrosshairPointer && !g_showMenu) {
		const bool inside  = s_crossInside;
		const bool focused = WebGameFocused();
		const bool clickNow = inside && focused && g_webClickKey && (GetAsyncKeyState(g_webClickKey) & 0x8000) != 0;
		const bool rNow     = inside && focused && g_webRightClickKey && (GetAsyncKeyState(g_webRightClickKey) & 0x8000) != 0;
		if (inside && (!g_webClickOnly || clickNow || rNow || s_mouseDownWorld || s_rDownWorld)) {
			WebMouseAt(s_crossU, s_crossV);
			s_ptrOnPage = true;
		} else if (s_ptrOnPage && !inside) {
			WebMousePark();
			s_ptrOnPage = false;
		}
		if (clickNow != s_mouseDownWorld) { WebMouseButton(clickNow, false); s_mouseDownWorld = clickNow; }
		if (rNow != s_rDownWorld)         { WebMouseButton(rNow, true);     s_rDownWorld = rNow; }
		if (g_webClickOnly && s_ptrOnPage && !clickNow && !rNow) { WebMousePark(); s_ptrOnPage = false; }
	} else {
		if (s_mouseDownWorld) { WebMouseButton(false, false); s_mouseDownWorld = false; }
		if (s_rDownWorld)     { WebMouseButton(false, true);  s_rDownWorld = false; }
		if (s_ptrOnPage)      { WebMousePark(); s_ptrOnPage = false; }
	}
}

void Web_PlaceAtCrosshair() { s_placeRequested = true; }

void Web_Unplace()
{
	if (s_placed) ++s_seq;
	s_placed = false;
	s_rebake = true;
	s_mediaSeenMs = 0;
	WebExecJs(k_wsPauseJs);
}

void Web_Navigate(const char* url)
{
	if (!url || !*url) return;
	s_url = WsNormalizeUrl(url);
	++s_seq;
	s_mediaSeenMs = 0;
	g_webUrl = s_url;
	if (s_wv2Started) wv2::Navigate(s_url.c_str());
	else if (s_steamStarted) WebSteamNavigate(s_url.c_str());
	else WebBackendTryStart();
}

bool Web_BlockFire() { return Web_On() && g_webBlockFire && g_webCrosshairPointer && s_placed && s_crossInside; }

bool Web_OnInput(unsigned msg, uintptr_t wParam)
{
	if (!g_webEnable || !g_webScrollInput || g_showMenu) return false;
	if (!WebAlive()) return false;

	float u, v;
	if (s_placed && s_crossInside)                                  { u = s_crossU; v = s_crossV; }
	else if (!s_placed && g_webPanel && g_webPanelAlways && s_tex)   { u = 0.5f; v = 0.5f; }
	else return false;

	const bool isArrow = wParam == VK_UP || wParam == VK_DOWN;
	switch (msg) {
	case WM_MOUSEWHEEL: {
		int rw, rh; WebResPx(rw, rh);
		WebMouseMovePx((int)(u * (float)rw), (int)(v * (float)rh));
		WebWheel((int)GET_WHEEL_DELTA_WPARAM(wParam) * 100 / WHEEL_DELTA);
		if (g_webClickOnly) WebMousePark();
		return true;
	}
	case WM_KEYDOWN: if (isArrow) { WebKey(true,  (uint32_t)wParam); return true; } break;
	case WM_KEYUP:   if (isArrow) { WebKey(false, (uint32_t)wParam); return true; } break;
	default: break;
	}
	return false;
}

void Web_LevelShutdown()
{
	s_placed = false;
	s_rebake = true;
	WebExecJs(k_wsPauseJs);
	for (int i = 0; i < wv2::k_peers; i++) WebPeerFree(i);
}

void Web_Destroy()
{
	WebBackendDrop();
	WebReleaseTexture();
	for (int i = 0; i < wv2::k_peers; i++) WebPeerFree(i);
}

void Web_InvalidateTexture()
{
	WebReleaseTexture();
	for (WsPeer& p : s_peer)
		if (p.tex) { p.tex->Release(); p.tex = nullptr; p.texW = p.texH = 0; }
}

void Web_ClearCache()
{
	if (s_wv2Started) wv2::ClearData();
	else if (s_steamStarted && !s_wipePhase) { s_wipeIdx = 0; WebWipeNext(); }
}

void Web_ClearMemory()
{
	if (s_wv2Started) { const std::string resume = s_url; WebWv2Drop(); s_wv2Failed = false; s_url = resume; }
	else WebSteamRecreate();
}

const char* Web_Status()
{
	static char buf[128];
	if (!Web_On()) snprintf(buf, sizeof(buf), "off");
	else if (!s_placed && !g_webPanel) snprintf(buf, sizeof(buf), "not placed");
	else if (s_wv2Started || s_steamStarted)
		snprintf(buf, sizeof(buf), "placed, %s (%s)", s_url.empty() ? "no url" : s_url.c_str(), s_wv2Started ? "WebView2" : "Steam");
	else if (s_steamDead && s_wv2Failed) snprintf(buf, sizeof(buf), "no browser available");
	else snprintf(buf, sizeof(buf), "starting...");
	return buf;
}
