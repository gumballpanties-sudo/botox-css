// Frame blending, pass three: the same down the COLUMN, and now the tile holds the longest
// streak in its whole block. SOURCE OF RECORD for fx_fb_tile_v.h.
//
// Rebuild after ANY edit here:
//   fxc /T ps_3_0 /E main /Fh src/hacks/screen/shaders/fx_fb_tile_v.h /Vn k_fxFbTileVBytes src/hacks/screen/shaders/fx_fb_tile_v.hlsl

sampler2D sRow : register(s0);

float4 c2 : register(c2);
float4 c6 : register(c6);
float4 c7 : register(c7);

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float base = floor(uv.y * c7.y) * c2.z;

    float  best = 0.0;
    float2 pick = 0.0;

    int k = (int)c2.z;

    [loop] for (int i = 0; i < k; i++)
    {
        float4 row = tex2Dlod(sRow, float4(uv.x, (base + 0.5 + (float)i) * c6.w, 0, 0));

        float sq = dot(row.xy, row.xy);

        if (sq > best) { best = sq; pick = row.xy; }
    }

    return float4(pick, 0.0, 0.0);
}
