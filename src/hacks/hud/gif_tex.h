#pragma once
#include "../../globals/globals.h"
#include <cstdint>
#include <cstddef>

// Unit E5: cache of decoded embedded GIFs (spectator_list/airflow icons), one reused
// IDirect3DTexture9 per GIF re-uploaded on frame change -- same approach spectator_list.cpp
// already uses, not a texture per frame (a 1.4MB donor GIF can run 50+ frames; keeping all
// of them resident would be hundreds of MB for an animation nobody looks at closely).
// ponytail: single reused texture, not "true" per-frame textures; bump to a small ring of
// pre-decoded textures only if a profile shows the re-upload cost actually matters.
struct GifTex;   // opaque, render-thread owned

// id is the cache key -- pass a stable string per GIF (e.g. "kamibebra_original"). data/size
// only matter the first time an id is seen.
GifTex* GifTex_Get(const char* id, const uint8_t* data, size_t size);

// RENDER THREAD: current frame for this GIF at timeSec of playback (decodes + uploads
// lazily on first call / frame change). Returns nullptr-equivalent (0) on any failure.
ImTextureID GifTex_Frame(GifTex* gif, double timeSec);

void GifTex_ReleaseAll();   // device Reset: the DEFAULT-pool frame texture is gone, GDI+ bitmap survives

// pixel size of the decoded GIF (frame 0 = every frame). false until it decoded.
bool GifTex_Size(GifTex* gif, float& w, float& h);

// botox spectator_list.cpp file_image_frame / url_image_path: a local path as is, an http(s) link
// (imgur page / .gifv / extensionless -> i.imgur.com raw file) downloaded once off the render
// thread to C:\botox-css\images\<fnv1a>. nullptr until the file exists and decodes.
GifTex* GifTex_GetFile(const char* setting);

// botox menu_tab_settings.cpp image_path_input( ..., upload = false ): text box + "browse"
// button (file dialog on its own thread, result lands in buf a frame later).
void GifTex_PathInput(const char* label, const char* hint, char* buf, size_t bufSize);
