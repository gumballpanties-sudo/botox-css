#include "frame_dump.h"
#include "../../globals/globals.h"   // DebugLog

#include <cstdio>
#include <cstring>
#include <vector>

// Own minimal PNG writer (uncompressed-store zlib stream, no external d3dx9/png lib) --
// botox's frame_dump.cpp:14-159, copied as-is, only g_console.print -> DebugLog.
namespace
{
    void PushBE(std::vector<unsigned char>& out, unsigned int value)
    {
        out.push_back((unsigned char)(value >> 24));
        out.push_back((unsigned char)(value >> 16));
        out.push_back((unsigned char)(value >> 8));
        out.push_back((unsigned char)value);
    }

    unsigned int Crc32(const unsigned char* data, size_t length)
    {
        static unsigned int table[256]{};
        static bool built = false;
        if (!built)
        {
            for (unsigned int i = 0; i < 256; ++i)
            {
                unsigned int c = i;
                for (int bit = 0; bit < 8; ++bit)
                    c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                table[i] = c;
            }
            built = true;
        }
        unsigned int crc = 0xFFFFFFFFu;
        for (size_t i = 0; i < length; ++i)
            crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
        return crc ^ 0xFFFFFFFFu;
    }

    unsigned int Adler32(const unsigned char* data, size_t length)
    {
        unsigned int a = 1, b = 0;
        for (size_t i = 0; i < length; ++i)
        {
            a = (a + data[i]) % 65521;
            b = (b + a) % 65521;
        }
        return (b << 16) | a;
    }

    void PushChunk(std::vector<unsigned char>& out, const char* type, const std::vector<unsigned char>& data)
    {
        PushBE(out, (unsigned int)data.size());
        std::vector<unsigned char> body{ type, type + 4 };
        body.insert(body.end(), data.begin(), data.end());
        out.insert(out.end(), body.begin(), body.end());
        PushBE(out, Crc32(body.data(), body.size()));
    }
}

bool FrameDump_Save(IDirect3DDevice9* device, IDirect3DSurface9* surface, const char* fileName)
{
    if (!device || !surface || !fileName) return false;

    D3DSURFACE_DESC desc{};
    if (FAILED(surface->GetDesc(&desc))) return false;

    if (desc.Format != D3DFMT_A8R8G8B8 && desc.Format != D3DFMT_X8R8G8B8)
    {
        DebugLog("frame dump: not a 32 bit surface, nothing written");
        return false;
    }

    IDirect3DSurface9* sysSurf = nullptr;
    if (FAILED(device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &sysSurf, nullptr)) || !sysSurf)
        return false;

    if (FAILED(device->GetRenderTargetData(surface, sysSurf)))
    {
        sysSurf->Release();
        DebugLog("frame dump: GetRenderTargetData refused");
        return false;
    }

    D3DLOCKED_RECT locked{};
    if (FAILED(sysSurf->LockRect(&locked, nullptr, D3DLOCK_READONLY)))
    {
        sysSurf->Release();
        return false;
    }

    const int width = (int)desc.Width, height = (int)desc.Height;
    std::vector<unsigned char> raw;
    raw.reserve((size_t)height * (1 + (size_t)width * 3));

    for (int y = 0; y < height; ++y)
    {
        const unsigned char* row = (const unsigned char*)locked.pBits + (size_t)y * locked.Pitch;
        raw.push_back(0);
        for (int x = 0; x < width; ++x)
        {
            raw.push_back(row[x * 4 + 2]);
            raw.push_back(row[x * 4 + 1]);
            raw.push_back(row[x * 4 + 0]);
        }
    }
    sysSurf->UnlockRect();
    sysSurf->Release();

    std::vector<unsigned char> stream{ 0x78, 0x01 };
    for (size_t offset = 0; offset < raw.size(); offset += 65535)
    {
        const size_t block = (raw.size() - offset) < 65535 ? (raw.size() - offset) : 65535;
        const bool last = (offset + block) >= raw.size();
        const unsigned int length = (unsigned int)block;
        stream.push_back(last ? 1 : 0);
        stream.push_back((unsigned char)length);
        stream.push_back((unsigned char)(length >> 8));
        stream.push_back((unsigned char)(~length));
        stream.push_back((unsigned char)(~length >> 8));
        stream.insert(stream.end(), raw.begin() + offset, raw.begin() + offset + block);
    }
    PushBE(stream, Adler32(raw.data(), raw.size()));

    std::vector<unsigned char> file{ 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    std::vector<unsigned char> header;
    PushBE(header, (unsigned int)width);
    PushBE(header, (unsigned int)height);
    header.push_back(8); header.push_back(2); header.push_back(0); header.push_back(0); header.push_back(0);
    PushChunk(file, "IHDR", header);
    PushChunk(file, "IDAT", stream);
    PushChunk(file, "IEND", {});

    char path[MAX_PATH] = {};
    HMODULE self = nullptr;
    static int marker = 0;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (const char*)&marker, &self) &&
        GetModuleFileNameA(self, path, sizeof(path)))
    {
        char* slash = strrchr(path, '\\');
        if (slash) *(slash + 1) = '\0';
    }
    else if (!GetTempPathA(sizeof(path), path))
        return false;

    strcat_s(path, fileName);

    std::FILE* handle = nullptr;
    if (fopen_s(&handle, path, "wb") != 0 || !handle)
    {
        DebugLog("frame dump: could not open the file");
        return false;
    }
    std::fwrite(file.data(), 1, file.size(), handle);
    std::fclose(handle);

    DebugLog("frame dump: wrote %s at %dx%d", path, width, height);
    return true;
}
