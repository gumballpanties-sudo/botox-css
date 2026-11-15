// Depth of field: the near circle of confusion, blurred. SOURCE OF RECORD for fx_dof_coc.h.
//
// Rebuild after ANY edit here:
//   fxc /T ps_3_0 /E main /Fh src/hacks/screen/shaders/fx_dof_coc.h /Vn k_fxDofCocBytes src/hacks/screen/shaders/fx_dof_coc.hlsl
//
// CinematicDOF's PS_CoCGaussian1/2, two channels:
//   r = the near coc, blurred (his field: alpha tail, smoothing)
//   g = the near MASK, blurred. g is 0.5 on any silhouette whatever the coc, and r / g is the
//       coc of the foreground around this pixel, thin or wide -- the near pass's halo radius.
//
// The first pass MUST be point sampled (dof.cpp): a bilinear fetch blends a coc across the
// silhouette and saturate(own * blur) turns that half texel into a full mask, so r / g lies.
// Its target is full height for the same reason: at half height a point fetch skips a row.
//
// Each tap is two fetches a quarter step either side: at 20 px max blur the taps are 6.6 px
// apart, and one fetch per tap turned the field into a staircase that banded the halo.
//
//   c1 = half the sharp band, max blur radius in pixels, 1/width, 1/height
//   c4 = direction x, direction y, 1 = first pass (reads the prepared alpha), -

sampler2D sSrc : register(s0);
float4 c1 : register(c1);
float4 c4 : register(c4);

static const float k_off[18] = {
    0.0, 1.4953705027, 3.4891992113, 5.4830312105, 7.4768683759, 9.4707125766, 11.4645656736,
    13.4584295168, 15.4523059431, 17.4461967743, 19.4661974725, 21.4627427973, 23.4592916956,
    25.455844494, 27.4524015179, 29.4489630909, 31.445529535, 33.4421011704
};
static const float k_wgt[18] = {
    0.033245, 0.0659162217, 0.0636705814, 0.0598194658, 0.0546642566, 0.0485871646,
    0.0420045997, 0.0353207015, 0.0288880982, 0.0229808311, 0.0177815511, 0.013382297,
    0.0097960001, 0.0069746748, 0.0048301008, 0.0032534598, 0.0021315311, 0.0013582974
};

// first pass: (saturate(1 - 2a), saturate((1 - 2a) * blur)) off the signed alpha; second: rg
float2 read(float2 t, float2 ka, float2 kb, float2 k0)
{
    float4 s = tex2Dlod(sSrc, float4(t, 0, 0));
    return saturate(s.a * ka + (s.rg * k0 + kb));
}

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float2 k1 = c4.z * float2(1.0, c1.y);
    float2 ka = -2.0 * k1;
    float2 k0 = 1.0 - c4.z;

    // the field is about five times wider than the disc: that tail is CinematicDOF's fade
    float2 axis = c4.xy * c1.zw * max(c1.y, 1.0) * 0.1645;
    float2 hs   = axis * 0.5;

    float2 f = read(uv, ka, k1, k0) * k_wgt[0];

    [unroll] for (int i = 1; i < 18; i++)
    {
        float2 p = uv + axis * k_off[i];
        float2 q = uv - axis * k_off[i];
        f += (read(p - hs, ka, k1, k0) + read(p + hs, ka, k1, k0) +
              read(q - hs, ka, k1, k0) + read(q + hs, ka, k1, k0)) * (0.5 * k_wgt[i]);
    }

    return float4(saturate(f), 0.0, 1.0);
}
