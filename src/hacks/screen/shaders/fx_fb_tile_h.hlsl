// Frame blending, pass two: the longest streak in each ROW of a tile. SOURCE OF RECORD for
// fx_fb_tile_h.h.
//
// Rebuild after ANY edit here:
//   fxc /T ps_3_0 /E main /Fh src/hacks/screen/shaders/fx_fb_tile_h.h /Vn k_fxFbTileHBytes src/hacks/screen/shaders/fx_fb_tile_h.hlsl
//
// 1d, so the KxK search over a tile costs K and not K*K -- pass three does the column.
//
// WHY TILES AT ALL: a moving thing has to blur PAST ITS OWN EDGE, and a pixel that is not
// moving has no idea anything is coming. The tile is how it finds out to go looking.
//
//   c2 = -, -, tile size, -
//   c6 = width, height, 1/width, 1/height

sampler2D sInfo : register(s0);

float4 c2 : register(c2);
float4 c6 : register(c6);
float4 c7 : register(c7);

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float base = floor(uv.x * c7.x) * c2.z;

    float  best = 0.0;
    float2 pick = 0.0;

    int k = (int)c2.z;

    [loop] for (int i = 0; i < k; i++)
    {
        float4 info = tex2Dlod(sInfo, float4((base + 0.5 + (float)i) * c6.z, uv.y, 0, 0));

        float2 m  = info.zw * info.x;
        float  sq = dot(m, m);

        if (sq > best) { best = sq; pick = m; }
    }

    return float4(pick, 0.0, 0.0);
}
