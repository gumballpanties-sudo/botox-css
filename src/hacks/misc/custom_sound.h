#pragma once
// custom trick/hit sounds: wav files dropped in C:\botox-css\sounds\ play off disk (winmm), not
// through the game's sound/ tree. Name = file name in that folder (or an absolute path). Not found
// there -> caller falls back to the old "playvol <game path>" so old cfgs still work.
#include <windows.h>
#include <mmsystem.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>
#pragma comment(lib, "winmm.lib")
#undef PlaySound   // aim/defs.h ISurface::PlaySound vfunc; we call PlaySoundA by name

static constexpr const char* SOUNDS_DIR = "C:\\botox-css\\sounds";

namespace wavvol {
// scale PCM samples in a RIFF/WAVE buffer by vol (0..2, clipped). 8/16-bit PCM only; other
// formats play unscaled. false = not a wav.
inline bool Scale(std::vector<char>& w, float vol)
{
    if (w.size() < 12 || memcmp(w.data(), "RIFF", 4) || memcmp(w.data() + 8, "WAVE", 4)) return false;
    uint16_t fmt = 0, bits = 0;
    for (size_t p = 12; p + 8 <= w.size();)
    {
        uint32_t len; memcpy(&len, w.data() + p + 4, 4);
        const size_t body = p + 8, end = (len > w.size() - body) ? w.size() : body + len;
        if (!memcmp(w.data() + p, "fmt ", 4) && end - body >= 16)
        { memcpy(&fmt, w.data() + body, 2); memcpy(&bits, w.data() + body + 14, 2); }
        else if (!memcmp(w.data() + p, "data", 4))
        {
            if (fmt != 1 || vol == 1.f) return true;
            if (bits == 16)
                for (size_t i = body; i + 2 <= end; i += 2)
                {
                    int16_t s; memcpy(&s, w.data() + i, 2);
                    float v = s * vol; v = v > 32767.f ? 32767.f : v < -32768.f ? -32768.f : v;
                    s = (int16_t)v; memcpy(w.data() + i, &s, 2);
                }
            else if (bits == 8)
                for (size_t i = body; i < end; ++i)
                {
                    float v = ((uint8_t)w[i] - 128) * vol + 128.f; v = v > 255.f ? 255.f : v < 0.f ? 0.f : v;
                    w[i] = (char)(uint8_t)v;
                }
            return true;
        }
        p = body + len + (len & 1);   // chunks are word aligned
    }
    return false;
}
}

// "hit.wav" -> C:\botox-css\sounds\hit.wav; absolute paths kept. Empty when the file is missing.
inline bool CustomSound_Resolve(const char* name, char* out, size_t sz)
{
    if (!name || !name[0]) return false;
    const bool abs = strchr(name, ':') || name[0] == '\\' || name[0] == '/';
    snprintf(out, sz, abs ? "%s" : "%s\\%s", abs ? name : SOUNDS_DIR, name);
    const DWORD a = GetFileAttributesA(out);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// true = played (or muted) from disk. One sound at a time (PlaySound limit): a new one cuts the last.
inline bool CustomSound_Play(const char* name, float vol)
{
    char path[MAX_PATH];
    if (!CustomSound_Resolve(name, path, sizeof(path))) return false;
    static std::vector<char> s_buf;   // must outlive the async play
    PlaySoundA(nullptr, nullptr, 0);  // stop before touching the buffer
    if (vol <= 0.001f) return true;
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") || !f) return false;
    fseek(f, 0, SEEK_END); const long n = ftell(f); fseek(f, 0, SEEK_SET);
    s_buf.resize(n > 0 ? (size_t)n : 0);
    const bool ok = n > 0 && fread(s_buf.data(), 1, s_buf.size(), f) == s_buf.size();
    fclose(f);
    if (!ok || !wavvol::Scale(s_buf, vol > 2.f ? 2.f : vol)) return false;
    return PlaySoundA(s_buf.data(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT) != FALSE;
}
