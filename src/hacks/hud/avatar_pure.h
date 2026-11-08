#pragma once
#include <cstdint>
#include <cstddef>

// Unit E5: avatar_cache's pure arithmetic, split out so it is testable without the Steam
// SDK or d3d9 (same shape as gif_frame.h / mp_art.h).
// Gate test: tests/avatar_cache_test.cpp.
namespace n_avatar {

// IVEngineClient::GetPlayerInfo's friendsID (32-bit account id) -> a full steamid64.
// 0x0110000100000000 == 76561197960265728: individual account, public universe, desktop
// instance -- the same base scoreboard.cpp's AccountIDToSteam64 already uses.
inline uint64_t FriendIdToSteam64(uint32_t friendsID)
{
    return 0x0110000100000000ULL | (uint64_t)friendsID;
}

// ISteamUtils::GetImageRGBA hands back straight RGBA8; a d3d9 D3DFMT_A8R8G8B8 texture wants
// BGRA8 in memory (byte order B,G,R,A on a little-endian upload) -- swap R and B in place,
// leave G and A alone.
inline void RgbaToBgraInPlace(uint8_t* px, size_t pixelCount)
{
    for (size_t i = 0; i < pixelCount; ++i)
    {
        uint8_t* p = px + i * 4;
        const uint8_t r = p[0];
        p[0] = p[2];
        p[2] = r;
    }
}

} // namespace n_avatar
