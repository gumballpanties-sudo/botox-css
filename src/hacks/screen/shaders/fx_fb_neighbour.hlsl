// Frame blending, pass four: the longest streak within 5x5 tiles, which is how far outside
// itself a moving thing is allowed to reach. SOURCE OF RECORD for fx_fb_neighbour.h.
//
// Rebuild after ANY edit here:
//   fxc /T ps_3_0 /E main /Fh src/hacks/screen/shaders/fx_fb_neighbour.h /Vn k_fxFbNeighbourBytes src/hacks/screen/shaders/fx_fb_neighbour.hlsl
//
// A DIAGONAL neighbour only counts when its motion points roughly at us: a tile moving along
// the diagonal cannot have crossed the corner, and letting it in is the square halo.
//
//   c7 = tileW, tileH, 1/tileW, 1/tileH

sampler2D sTile : register(s0);

float4 c7 : register(c7);

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float  best = 0.0;
    float2 pick = 0.0;

    [loop] for (int y = -2; y <= 2; y++)
    {
        [loop] for (int x = -2; x <= 2; x++)
        {
            float2 offs = float2((float)x, (float)y);

            float4 t = tex2Dlod(sTile, float4(uv + offs * c7.zw, 0, 0));

            float  diag    = abs(offs.x * offs.y);
            float2 towards = -offs * rsqrt(max(1e-15, dot(offs, offs)));

            float sq = dot(t.xy, t.xy);

            if (sq > best)
            {
                bool ok = true;

                if (diag > 0.0)
                {
                    float a = acos(clamp(dot(towards, t.xy * rsqrt(max(1e-15, sq))), -1.0, 1.0));
                    ok = a < 0.7854 || a > 2.3561;
                }

                if (ok) { best = sq; pick = t.xy; }
            }
        }
    }

    return float4(pick, 0.0, 0.0);
}
