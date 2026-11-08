#include "reflections.h"
#include "../screen/shaders/fx_refl.h"   // k_fxRefl* -- fx_refl_build.sh from fx_refl.hlsl
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Screen-space reflections, botox hacks/visuals/screen/reflections.cpp 1:1: normals, two
// smoothing passes, the qUINT march with the map's own env_cubemap under it, a vertical filter
// and the composite (horizontal blur + edge aa) blended over the frame. TF2 changes:
//   * bytecode from fx_refl.hlsl (no d3dx9), drawn through screenfx.cpp's vs_3_0 quad
//   * lin() re-projects TF2 raw depth first (c7 = this frame's z range, see the .hlsl)
//   * the cubemap pick runs on the main thread from screenfx.cpp's view-setup read
//     (Reflections_UpdateView), the draw on the render thread -- botox's frame_t split

// ==== cubemaps (main thread) ========================================================
namespace {

struct CubeData {
    int size = 0;
    D3DFORMAT format = D3DFMT_UNKNOWN;
    int pitch = 0, rows = 0;
    std::vector<unsigned char> faces[6];
};
using CubePtr = std::shared_ptr<const CubeData>;

struct ReflFrame {
    CubePtr cube[2];
    float   blend = 1.f;
    float   right[3]{}, down[3]{}, forward[3]{};
    unsigned generation = 0;
};

std::string           s_level;
std::vector<int>      s_samples;      // 3 ints per dcubemapsample_t origin
std::vector<CubePtr>  s_cubeData;
std::vector<bool>     s_cubeTried;
std::vector<int>      s_cubeRecent;
int   s_cubeCurrent = -1, s_cubePrevious = -1;
float s_cubeSwitch = 0.f;
unsigned s_generation = 0;

std::mutex s_frameLock;   // main thread writes, render thread copies
ReflFrame  s_frame;

// IBaseFileSystem (sdk public/filesystem.h, VBaseFileSystem011 in TF2 x64 filesystem_stdio.dll):
// Read 0, Open 2, Close 3, Seek 4, Tell 5 -- the slots botox's read_game_file calls.
void* FileSystem()
{
    static void* fs = GetInterface("filesystem_stdio.dll", "VBaseFileSystem011");
    return fs;
}
void* FsOpen(void* fs, const char* path)
{
    __try { return VCall<void*>(fs, 2, path, "rb", "GAME"); } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
void FsClose(void* fs, void* h) { __try { VCall<void>(fs, 3, h); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
void FsSeek(void* fs, void* h, int pos, int whence) { __try { VCall<void>(fs, 4, h, pos, whence); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
unsigned FsTell(void* fs, void* h) { __try { return VCall<unsigned>(fs, 5, h); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; } }
int FsRead(void* fs, void* h, void* out, int n) { __try { return VCall<int>(fs, 0, out, n, h); } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; } }

bool ReadGameFile(const char* path, std::vector<unsigned char>& out, unsigned offset = 0, unsigned length = 0)
{
    void* fs = FileSystem();
    if (!fs) return false;
    void* h = FsOpen(fs, path);
    if (!h) return false;
    FsSeek(fs, h, 0, 2 /* FILESYSTEM_SEEK_TAIL */);
    const unsigned size = FsTell(fs, h);
    if (!length && size > offset) length = size - offset;
    bool ok = length > 0 && length < (256u << 20) && offset <= size && length <= size - offset;   // no wrap on a junk lump
    if (ok) {
        FsSeek(fs, h, (int)offset, 0 /* FILESYSTEM_SEEK_HEAD */);
        out.resize(length);
        ok = FsRead(fs, h, out.data(), (int)length) == (int)length;
    }
    FsClose(fs, h);
    return ok;
}

// IVEngineClient::GetLevelName = slot 51 (psassist.inl EngineLevelName): "maps/x.bsp"
const char* LevelName()
{
    if (!g_engine) return nullptr;
    __try { return VCall<const char*>(g_engine, 51); } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

float HalfToFloat(unsigned short half)
{
    const int e = (half >> 10) & 31, m = half & 1023;
    const float v = e == 0 ? std::ldexp((float)m, -24) : (e == 31 ? 65504.f : std::ldexp((float)(m | 1024), e - 25));
    return (half & 0x8000) ? -v : v;
}

// botox parse_cube_vtf: VTF 7.x env map, top mip of all 6 faces of the first frame.
CubePtr ParseCubeVtf(const std::vector<unsigned char>& file)
{
    const auto u16 = [&](size_t at) { return (unsigned)(file[at] | (file[at + 1] << 8)); };
    const auto u32 = [&](size_t at) { return (unsigned)(u16(at) | (u16(at + 2) << 16)); };

    if (file.size() < 80 || memcmp(file.data(), "VTF", 4) != 0 || u32(4) != 7) return nullptr;

    const unsigned minor  = u32(8);
    const unsigned header = u32(12);
    const int size   = (int)u16(16);
    const int frames = (std::max)((int)u16(24), 1);
    const int format = (int)u32(52);
    const int mips   = (std::max)((int)file[56], 1);

    if (size < 4 || u16(18) != (unsigned)size || !(u32(20) & 0x4000 /* TEXTUREFLAGS_ENVMAP */)) return nullptr;

    const auto mipBytes = [format](int width) -> size_t {
        const size_t blocks = (size_t)(std::max)((width + 3) / 4, 1);
        switch (format) {
        case 13: return blocks * blocks * 8;                 // DXT1
        case 14: case 15: return blocks * blocks * 16;       // DXT3 / DXT5
        case 24: return (size_t)width * width * 8;           // RGBA16161616F
        case 0: case 12: case 16: return (size_t)width * width * 4;   // RGBA8888 / BGRA8888 / BGRX8888
        default: return 0;
        }
    };
    if (!mipBytes(size)) return nullptr;

    size_t image = 0;
    if (minor >= 3) {
        const unsigned count = (std::min)(u32(68), 32u);
        for (unsigned i = 0; i < count && 88 + i * 8 <= file.size(); ++i)
            if ((u32(80 + i * 8) & 0x00ffffff) == 0x30) image = u32(84 + i * 8);   // high res image resource
    } else {
        const int lowFormat = (int)u32(57);
        const size_t lowW = (size_t)(std::max)((file[61] + 3) / 4, 1);
        const size_t lowH = (size_t)(std::max)((file[62] + 3) / 4, 1);
        image = header + (lowFormat == 13 ? lowW * lowH * 8 : 0);
    }
    if (!image || image >= file.size()) return nullptr;

    size_t faceChain = 0, belowTop = 0;
    for (int mip = 0; mip < mips; ++mip) {
        const size_t bytes = mipBytes((std::max)(size >> mip, 1));
        faceChain += bytes;
        belowTop += mip ? bytes : 0;
    }
    const size_t faces = (file.size() - image) / (faceChain * frames);
    if (faces < 6) return nullptr;

    const size_t top   = mipBytes(size);
    const size_t first = image + belowTop * frames * faces;   // mips are stored smallest first
    if (first + 6 * top > file.size()) return nullptr;

    auto data = std::make_shared<CubeData>();
    data->size = size;
    const bool compressed = format == 13 || format == 14 || format == 15;
    data->format = format == 13 ? D3DFMT_DXT1 : format == 14 ? D3DFMT_DXT3 : format == 15 ? D3DFMT_DXT5 : D3DFMT_X8R8G8B8;
    data->pitch  = compressed ? (int)top / (std::max)((size + 3) / 4, 1) : size * 4;
    data->rows   = compressed ? (std::max)((size + 3) / 4, 1) : size;

    for (int face = 0; face < 6; ++face) {
        const unsigned char* source = file.data() + first + face * top;
        std::vector<unsigned char>& out = data->faces[face];

        if (compressed || format == 12 || format == 16) {
            out.assign(source, source + top);
            if (compressed && face == 5) {   // an all-black dxt cube is a failed bake: try the next file
                size_t sum = 0;
                for (const auto& any : data->faces) for (unsigned char b : any) sum += b;
                if (sum < top * 6) return nullptr;
            }
            continue;
        }

        const size_t pixels = (size_t)size * size;
        out.resize(pixels * 4);
        for (size_t p = 0; p < pixels; ++p) {
            unsigned char rgb[3];
            for (int c = 0; c < 3; ++c) {
                if (format == 24) {
                    const float linear = HalfToFloat((unsigned short)(source[p * 8 + c * 2] | (source[p * 8 + c * 2 + 1] << 8)));
                    rgb[c] = (unsigned char)(powf(std::clamp(linear, 0.f, 1.f), 1.f / 2.2f) * 255.f + 0.5f);
                } else
                    rgb[c] = source[p * 4 + c];
            }
            out[p * 4 + 0] = rgb[2];
            out[p * 4 + 1] = rgb[1];
            out[p * 4 + 2] = rgb[0];
            out[p * 4 + 3] = 255;
        }
    }
    return data;
}

void LoadCubeSamples()
{
    s_samples.clear();
    s_cubeData.clear();
    s_cubeTried.clear();
    s_cubeRecent.clear();
    s_cubeCurrent = s_cubePrevious = -1;

    // dheader_t: ident, version, lump_t[64] (16 bytes each); LUMP_CUBEMAPS = 42 (sdk bspfile.h)
    std::vector<unsigned char> header;
    if (!ReadGameFile(s_level.c_str(), header, 0, 8 + 64 * 16) || memcmp(header.data(), "VBSP", 4) != 0) {
        DebugLog("reflections: can't read %s, no cubemaps", s_level.c_str());
        return;
    }
    unsigned lump[2]{};
    memcpy(lump, header.data() + 8 + 42 * 16, sizeof(lump));

    std::vector<unsigned char> samples;
    if (lump[1] < 16 || !ReadGameFile(s_level.c_str(), samples, lump[0], lump[1] - lump[1] % 16)) {
        DebugLog("reflections: %s has no cubemap lump", s_level.c_str());
        return;
    }
    for (size_t i = 0; i + 16 <= samples.size(); i += 16) {   // dcubemapsample_t: int origin[3], int size
        int origin[3];
        memcpy(origin, samples.data() + i, sizeof(origin));
        s_samples.insert(s_samples.end(), origin, origin + 3);
    }
    const size_t count = s_samples.size() / 3;
    s_cubeData.resize(count);
    s_cubeTried.resize(count, false);
    DebugLog("reflections: %d cubemaps in %s", (int)count, s_level.c_str());
}

CubePtr LoadCube(int index)
{
    if (index < 0 || index >= (int)s_cubeData.size()) return nullptr;
    if (s_cubeTried[index]) return s_cubeData[index];
    s_cubeTried[index] = true;

    const std::string folder = "materials/" + s_level.substr(0, s_level.size() - 4) + "/";
    const int* o = &s_samples[index * 3];
    char name[64];
    snprintf(name, sizeof(name), "c%d_%d_%d", o[0], o[1], o[2]);

    for (const std::string& file : { std::string(name) + ".hdr.vtf", std::string(name) + ".vtf",
                                     std::string("cubemapdefault.hdr.vtf"), std::string("cubemapdefault.vtf") }) {
        std::vector<unsigned char> bytes;
        if (!ReadGameFile((folder + file).c_str(), bytes)) continue;
        if (CubePtr data = ParseCubeVtf(bytes)) {
            s_cubeData[index] = data;
            return data;
        }
    }
    DebugLog("reflections: no usable cubemap for %s%s", folder.c_str(), name);
    return nullptr;
}

void AngleBasis(const float ang[3], float fwd[3], float right[3], float up[3])
{
    const float d2r = 3.14159265f / 180.f;
    const float sp = sinf(ang[0] * d2r), cp = cosf(ang[0] * d2r);
    const float sy = sinf(ang[1] * d2r), cy = cosf(ang[1] * d2r);
    const float sr = sinf(ang[2] * d2r), cr = cosf(ang[2] * d2r);
    fwd[0] = cp * cy; fwd[1] = cp * sy; fwd[2] = -sp;
    right[0] = -sr * sp * cy + cr * sy; right[1] = -sr * sp * sy - cr * cy; right[2] = -sr * cp;
    up[0] = cr * sp * cy + sr * sy; up[1] = cr * sp * sy - sr * cy; up[2] = cr * cp;
}

} // namespace

// botox on_post_screen_space_effects + pick_cubemap. Main thread (screenfx.cpp FxReadViewSetup).
void Reflections_UpdateView(const float origin[3], const float angles[3])
{
    if (!g_reflEnable) return;

    ReflFrame frame{};
    float up[3];
    AngleBasis(angles, frame.forward, frame.right, up);
    for (int i = 0; i < 3; ++i) frame.down[i] = -up[i];

    const char* level = IsInGame() ? LevelName() : nullptr;
    if (!level || strlen(level) < 5) {
        if (!s_level.empty()) {
            s_level.clear();
            s_samples.clear();
            s_cubeData.clear();
            s_cubeTried.clear();
            s_cubeRecent.clear();
            ++s_generation;
        }
    } else {
        if (s_level != level) {
            s_level = level;
            ++s_generation;
            LoadCubeSamples();
        }

        int nearest = -1;
        float closest = 0.f;
        for (int i = 0; i < (int)(s_samples.size() / 3); ++i) {
            const float dx = (float)s_samples[i * 3] - origin[0];
            const float dy = (float)s_samples[i * 3 + 1] - origin[1];
            const float dz = (float)s_samples[i * 3 + 2] - origin[2];
            const float dist = dx * dx + dy * dy + dz * dz;
            if (nearest < 0 || dist < closest) { nearest = i; closest = dist; }
        }

        if (nearest >= 0) {
            const float now = (float)(GetTickCount64() % 100000000ull) / 1000.f;
            if (nearest != s_cubeCurrent) {
                s_cubePrevious = s_cubeCurrent;
                s_cubeCurrent  = nearest;
                s_cubeSwitch   = now;

                constexpr size_t k_cubeKeep = 8;
                s_cubeRecent.erase(std::remove(s_cubeRecent.begin(), s_cubeRecent.end(), nearest), s_cubeRecent.end());
                s_cubeRecent.insert(s_cubeRecent.begin(), nearest);
                while (s_cubeRecent.size() > k_cubeKeep) {
                    const int old = s_cubeRecent.back();
                    s_cubeRecent.pop_back();
                    s_cubeData[old].reset();
                    s_cubeTried[old] = false;
                }
            }

            frame.cube[0] = LoadCube(s_cubeCurrent);
            frame.blend = std::clamp((now - s_cubeSwitch) / 0.5f, 0.f, 1.f);
            if (frame.blend < 1.f) frame.cube[1] = LoadCube(s_cubePrevious);
            if (!frame.cube[1]) frame.blend = 1.f;
        }
    }
    frame.generation = s_generation;

    std::lock_guard<std::mutex> lock(s_frameLock);
    s_frame = std::move(frame);
}

// ==== device side (render thread) ===================================================
enum { k_shNormals, k_shSmooth, k_shTrace, k_shFilter, k_shComposite, k_shMax };

static IDirect3DPixelShader9* s_shaders[k_shMax]{};
static bool s_failed = false;
static IDirect3DTexture9* s_frameTex = nullptr;
static IDirect3DSurface9* s_frameSurf = nullptr;
static IDirect3DTexture9* s_normalTex[2]{};
static IDirect3DSurface9* s_normalSurf[2]{};
static IDirect3DTexture9* s_reflTex[2]{};
static IDirect3DSurface9* s_reflSurf[2]{};
static int s_w = 0, s_h = 0, s_tw = 0, s_th = 0;
static D3DFORMAT s_fmt = D3DFMT_UNKNOWN;

struct CubeTexture { CubePtr data; IDirect3DCubeTexture9* tex; };
static std::vector<CubeTexture> s_cubeTex;
static unsigned s_cubeGeneration = 0;

static void ReleaseTargets()
{
    const auto drop = [](auto*& r) { if (r) { r->Release(); r = nullptr; } };
    drop(s_frameSurf); drop(s_frameTex);
    for (int i = 0; i < 2; ++i) { drop(s_normalSurf[i]); drop(s_normalTex[i]); drop(s_reflSurf[i]); drop(s_reflTex[i]); }
    s_w = s_h = s_tw = s_th = 0;
    s_fmt = D3DFMT_UNKNOWN;
}

static void ReleaseCubes()
{
    for (CubeTexture& e : s_cubeTex) if (e.tex) e.tex->Release();
    s_cubeTex.clear();
}

void Reflections_Destroy()
{
    ReleaseTargets();
    ReleaseCubes();
    for (auto& s : s_shaders) if (s) { s->Release(); s = nullptr; }
}

static bool ShadersReady(IDirect3DDevice9* dev)
{
    if (s_failed) return false;
    static const unsigned char* const k_src[k_shMax] = { k_fxReflNormals, k_fxReflSmooth, k_fxReflTrace, k_fxReflFilter, k_fxReflComposite };
    static const size_t k_len[k_shMax] = { sizeof(k_fxReflNormals), sizeof(k_fxReflSmooth), sizeof(k_fxReflTrace),
                                           sizeof(k_fxReflFilter), sizeof(k_fxReflComposite) };
    for (int i = 0; i < k_shMax; ++i) {
        if (s_shaders[i]) continue;
        std::vector<DWORD> code((k_len[i] + 3) / 4);
        memcpy(code.data(), k_src[i], k_len[i]);
        if (FAILED(dev->CreatePixelShader(code.data(), &s_shaders[i])) || !s_shaders[i]) {
            s_shaders[i] = nullptr;
            s_failed = true;
            DebugLog("reflections: the card refused shader %d -- feature is off", i);
            return false;
        }
        DebugLog("reflections: shader %d built", i);
    }
    return true;
}

// botox build(): frame copy at full size, normals fp16 + reflection rgba8 x2 at render size
static bool Build(IDirect3DDevice9* dev, const D3DSURFACE_DESC& desc)
{
    const int w = (int)desc.Width, h = (int)desc.Height;
    const float scale = std::clamp(g_sfxReflScale, 0.25f, 1.f);
    const int tw = (std::max)((int)(w * scale + 0.5f), 16);
    const int th = (std::max)((int)(h * scale + 0.5f), 16);
    if (s_frameTex && s_w == w && s_h == h && s_fmt == desc.Format && s_tw == tw && s_th == th) return true;

    ReleaseTargets();
    const auto fail = [](const char* what) { DebugLog("%s", what); ReleaseTargets(); return false; };

    HRESULT hr = dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, desc.Format, D3DPOOL_DEFAULT, &s_frameTex, nullptr);
    if (FAILED(hr))
        hr = dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &s_frameTex, nullptr);
    if (FAILED(hr) || FAILED(s_frameTex->GetSurfaceLevel(0, &s_frameSurf)))
        return fail("reflections: frame copy creation failed");

    for (int i = 0; i < 2; ++i) {
        if (FAILED(dev->CreateTexture(tw, th, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &s_normalTex[i], nullptr)) ||
            FAILED(s_normalTex[i]->GetSurfaceLevel(0, &s_normalSurf[i])))
            return fail("reflections: normal target creation failed (fp16)");
        if (FAILED(dev->CreateTexture(tw, th, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &s_reflTex[i], nullptr)) ||
            FAILED(s_reflTex[i]->GetSurfaceLevel(0, &s_reflSurf[i])))
            return fail("reflections: reflection target creation failed");
    }

    s_w = w; s_h = h; s_tw = tw; s_th = th; s_fmt = desc.Format;
    DebugLog("reflections: built %dx%d, trace %dx%d", w, h, tw, th);
    return true;
}

// botox cube_texture(): upload once per data, staging (sysmem) -> default pool
static IDirect3DCubeTexture9* CubeTex(IDirect3DDevice9* dev, const CubePtr& data)
{
    if (!data) return nullptr;
    for (const CubeTexture& e : s_cubeTex) if (e.data == data) return e.tex;

    // use_count 1 = only this cache holds it: the main thread dropped it, nobody asks again
    s_cubeTex.erase(std::remove_if(s_cubeTex.begin(), s_cubeTex.end(), [](const CubeTexture& e) {
        if (e.data.use_count() != 1) return false;
        if (e.tex) e.tex->Release();
        return true;
    }), s_cubeTex.end());

    IDirect3DCubeTexture9* staging = nullptr;
    IDirect3DCubeTexture9* tex = nullptr;
    if (SUCCEEDED(dev->CreateCubeTexture(data->size, 1, 0, data->format, D3DPOOL_SYSTEMMEM, &staging, nullptr))) {
        bool filled = true;
        for (int face = 0; face < 6 && filled; ++face) {
            D3DLOCKED_RECT lr{};
            if (FAILED(staging->LockRect((D3DCUBEMAP_FACES)face, 0, &lr, nullptr, 0))) { filled = false; break; }
            for (int row = 0; row < data->rows; ++row)
                memcpy((unsigned char*)lr.pBits + row * lr.Pitch, data->faces[face].data() + row * data->pitch, (size_t)data->pitch);
            staging->UnlockRect((D3DCUBEMAP_FACES)face, 0);
        }
        if (filled && SUCCEEDED(dev->CreateCubeTexture(data->size, 1, 0, data->format, D3DPOOL_DEFAULT, &tex, nullptr)) &&
            FAILED(dev->UpdateTexture(staging, tex))) {
            tex->Release();
            tex = nullptr;
        }
        staging->Release();
    }
    if (!tex) DebugLog("reflections: cubemap upload failed");
    else      DebugLog("reflections: cubemap %d px uploaded", data->size);
    s_cubeTex.push_back({ data, tex });
    return tex;
}

void Reflections_Draw(IDirect3DDevice9* dev, IDirect3DSurface9* target,
                       const D3DSURFACE_DESC& desc, IDirect3DTexture9* depth,
                       const FxViewSetup& view)
{
    if (!dev || !target || !depth || s_failed) return;
    if (!ScreenFx_QuadReady(dev)) return;   // ps_3_0 needs the shared passthrough vs, not FF
    if (!ShadersReady(dev) || !Build(dev, desc)) return;

    // must be frame sized, else it's not the scene's depth
    D3DSURFACE_DESC dd{};
    if (FAILED(depth->GetLevelDesc(0, &dd)) || dd.Width != desc.Width || dd.Height != desc.Height) return;

    ReflFrame frame;
    { std::lock_guard<std::mutex> lock(s_frameLock); frame = s_frame; }
    if (frame.generation != s_cubeGeneration) { ReleaseCubes(); s_cubeGeneration = frame.generation; }

    if (FAILED(dev->StretchRect(target, nullptr, s_frameSurf, nullptr, D3DTEXF_NONE))) {
        static bool logged = false;
        if (!logged) { logged = true; DebugLog("reflections: frame copy failed"); }
        return;
    }

    // ---- save: botox's list + what the shaderapi caches and the vs quad changes
    constexpr int k_stages = 6;
    static const D3DRENDERSTATETYPE k_rs[] = {
        D3DRS_STENCILENABLE, D3DRS_COLORWRITEENABLE, D3DRS_CLIPPLANEENABLE, D3DRS_FILLMODE, D3DRS_ZENABLE,
        D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE, D3DRS_SRCBLEND, D3DRS_DESTBLEND,
        D3DRS_BLENDOP, D3DRS_CULLMODE, D3DRS_SCISSORTESTENABLE, D3DRS_SRGBWRITEENABLE, D3DRS_SEPARATEALPHABLENDENABLE,
    };
    static const D3DSAMPLERSTATETYPE k_samp[] = {
        D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_ADDRESSW, D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER,
        D3DSAMP_MIPFILTER, D3DSAMP_SRGBTEXTURE,
    };
    constexpr int kRs = (int)(sizeof(k_rs) / sizeof(k_rs[0]));
    constexpr int kSamp = (int)(sizeof(k_samp) / sizeof(k_samp[0]));

    DWORD oldRs[kRs]{}, oldSamp[k_stages][kSamp]{}, oldFvf = 0;
    float oldC[8][4]{};
    IDirect3DSurface9* oldDs = nullptr;
    IDirect3DPixelShader9* oldPs = nullptr;
    IDirect3DVertexShader9* oldVs = nullptr;
    IDirect3DVertexDeclaration9* oldDecl = nullptr;
    IDirect3DBaseTexture9* oldTex[k_stages]{};
    IDirect3DVertexBuffer9* oldStream = nullptr;
    IDirect3DIndexBuffer9* oldIdx = nullptr;
    UINT oldOff = 0, oldStride = 0;
    D3DVIEWPORT9 oldVp{};

    if (FAILED(dev->GetDepthStencilSurface(&oldDs))) oldDs = nullptr;
    for (int i = 0; i < kRs; ++i) dev->GetRenderState(k_rs[i], &oldRs[i]);
    for (int u = 0; u < k_stages; ++u) {
        dev->GetTexture(u, &oldTex[u]);
        for (int i = 0; i < kSamp; ++i) dev->GetSamplerState(u, k_samp[i], &oldSamp[u][i]);
    }
    dev->GetPixelShader(&oldPs);
    dev->GetVertexShader(&oldVs);
    dev->GetVertexDeclaration(&oldDecl);
    dev->GetFVF(&oldFvf);
    dev->GetStreamSource(0, &oldStream, &oldOff, &oldStride);
    dev->GetIndices(&oldIdx);
    dev->GetViewport(&oldVp);
    dev->GetPixelShaderConstantF(0, oldC[0], 8);

    // proof for the water-map bug: what the engine left on that would cut or mask the quad
    static bool s_leftLogged = false;
    if (!s_leftLogged && (oldRs[2] || oldRs[0] || (oldRs[1] & 7) != 7 || oldRs[3] != D3DFILL_SOLID)) {
        s_leftLogged = true;
        DebugLog("reflections: engine left clipplanes=0x%lx stencil=%lu colorwrite=0x%lx fill=%lu at the post slot -- reset for the pass",
                 oldRs[2], oldRs[0], oldRs[1], oldRs[3]);
    }

    // ---- state for the passes
    dev->SetDepthStencilSurface(nullptr);
    ScreenFx_BindQuad(dev);
    dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    dev->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0f);
    for (int u = 0; u < k_stages; ++u) {
        const DWORD filter = u >= 2 ? D3DTEXF_LINEAR : D3DTEXF_POINT;   // depth + normals point
        dev->SetSamplerState(u, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(u, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(u, D3DSAMP_ADDRESSW, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(u, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        dev->SetSamplerState(u, D3DSAMP_SRGBTEXTURE, 0);
        dev->SetSamplerState(u, D3DSAMP_MINFILTER, filter);
        dev->SetSamplerState(u, D3DSAMP_MAGFILTER, filter);
    }

    // ---- constants, botox execute() 1:1 (+ c7 z range)
    const float width = (float)s_w, height = (float)s_h;
    const float aspect = height > 0.f ? width / height : 1.777778f;
    const float tanX = tanf(std::clamp(view.fov, 1.f, 170.f) * 0.5f * 3.14159265f / 180.f);
    const float tanY = tanX / aspect;
    const float c0[4] = { 2.f * tanX, 2.f * tanY, 1.f / (float)s_tw, 1.f / (float)s_th };
    const float c1[4] = { std::clamp(g_reflDistance, 0.001f, 1.f), std::clamp(g_reflIntensity, 0.f, 1.f),
                          std::clamp(g_sfxReflFalloff, 0.f, 1.f), std::clamp(g_sfxReflBlur, 0.f, 5.f) };
    static const float k_march[3][4] = { { 6.f, 3.505f, 1.f, 0.f }, { 10.f, 2.123f, 2.f, 0.f }, { 16.f, 1.6f, 3.f, 0.f } };
    const float c7[4] = { view.zNear, view.zFar, 0.f, 0.f };
    dev->SetPixelShaderConstantF(0, c0, 1);
    dev->SetPixelShaderConstantF(1, c1, 1);
    dev->SetPixelShaderConstantF(6, k_march[std::clamp(g_sfxReflQuality, 0, 2)], 1);
    dev->SetPixelShaderConstantF(7, c7, 1);

    const float axisX[4] = { 1.f / width, 0.f, 0.f, 0.f };
    const float axisY[4] = { 0.f, 1.f / height, 0.f, 0.f };
    const D3DVIEWPORT9 traceVp{ 0, 0, (DWORD)s_tw, (DWORD)s_th, 0.f, 1.f };
    const D3DVIEWPORT9 fullVp{ 0, 0, (DWORD)s_w, (DWORD)s_h, 0.f, 1.f };

    const auto pass = [&](int shader, IDirect3DSurface9* surf, const float* axis) {
        if (axis) dev->SetPixelShaderConstantF(2, axis, 1);
        dev->SetRenderTarget(0, surf);
        dev->SetViewport(&traceVp);
        dev->SetPixelShader(s_shaders[shader]);
        ScreenFx_DrawQuad(dev, s_tw, s_th);
    };

    // cubemaps: c3 right (w = have one), c4 down (w = crossfade), c5 forward. No cube = plain ssr.
    IDirect3DCubeTexture9* cubeNew = CubeTex(dev, frame.cube[0]);
    IDirect3DCubeTexture9* cubeOld = CubeTex(dev, frame.cube[1]);
    const float cube[3][4] = {
        { frame.right[0], frame.right[1], frame.right[2], cubeNew ? 1.f : 0.f },
        { frame.down[0], frame.down[1], frame.down[2], cubeOld ? frame.blend : 1.f },
        { frame.forward[0], frame.forward[1], frame.forward[2], 0.f },
    };
    dev->SetPixelShaderConstantF(3, cube[0], 3);
    dev->SetTexture(4, cubeNew);
    dev->SetTexture(5, cubeOld ? cubeOld : cubeNew);

    dev->SetTexture(0, depth);
    dev->SetTexture(2, s_frameTex);

    pass(k_shNormals, s_normalSurf[0], nullptr);

    if (g_sfxReflSmoothNormals) {
        dev->SetTexture(1, s_normalTex[0]);
        pass(k_shSmooth, s_normalSurf[1], axisX);
        dev->SetTexture(1, s_normalTex[1]);
        pass(k_shSmooth, s_normalSurf[0], axisY);
    }

    dev->SetTexture(1, s_normalTex[0]);
    pass(k_shTrace, s_reflSurf[0], nullptr);

    dev->SetTexture(3, s_reflTex[0]);
    pass(k_shFilter, s_reflSurf[1], axisY);

    dev->SetTexture(3, s_reflTex[1]);
    dev->SetPixelShaderConstantF(2, axisX, 1);
    dev->SetRenderTarget(0, target);
    dev->SetViewport(&fullVp);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
    dev->SetPixelShader(s_shaders[k_shComposite]);
    ScreenFx_DrawQuad(dev, s_w, s_h);

    // ---- restore
    dev->SetPixelShaderConstantF(0, oldC[0], 8);
    dev->SetPixelShader(oldPs);
    dev->SetVertexShader(oldVs);
    if (oldDecl) dev->SetVertexDeclaration(oldDecl); else dev->SetFVF(oldFvf);
    dev->SetStreamSource(0, oldStream, oldOff, oldStride);
    dev->SetIndices(oldIdx);
    for (int u = 0; u < k_stages; ++u) {
        dev->SetTexture(u, oldTex[u]);
        for (int i = 0; i < kSamp; ++i) dev->SetSamplerState(u, k_samp[i], oldSamp[u][i]);
    }
    for (int i = 0; i < kRs; ++i) dev->SetRenderState(k_rs[i], oldRs[i]);
    dev->SetViewport(&oldVp);
    dev->SetDepthStencilSurface(oldDs);

    for (IUnknown* held : { (IUnknown*)oldDs, (IUnknown*)oldPs, (IUnknown*)oldVs, (IUnknown*)oldDecl,
                            (IUnknown*)oldStream, (IUnknown*)oldIdx })
        if (held) held->Release();
    for (IDirect3DBaseTexture9* t : oldTex) if (t) t->Release();

    static bool logged = false;
    if (!logged) {
        logged = true;
        DebugLog("reflections: drew %dx%d trace %dx%d at the post slot (cube %d, quality %d, smooth %d)",
                 s_w, s_h, s_tw, s_th, (int)(cubeNew != nullptr), g_sfxReflQuality, (int)g_sfxReflSmoothNormals);
    }
}
