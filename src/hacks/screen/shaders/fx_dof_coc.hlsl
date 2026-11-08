// Depth of field: the near circle of confusion, blurred. SOURCE OF RECORD for fx_dof_coc.h.
//
// Rebuild after ANY edit here:
//   fxc /T ps_3_0 /E main /Fh src/hacks/screen/shaders/fx_dof_coc.h /Vn k_fxDofCocBytes src/hacks/screen/shaders/fx_dof_coc.hlsl
//
// CinematicDOF's PS_CoCGaussian1/2, and it is the one thing a plain gather never has: a
// field that says "a blurred foreground reaches THIS far out", including over pixels that
// are perfectly sharp themselves. Without it the near plane can only paint inside its own
// silhouette and that silhouette stays a razor line however hard the interior is blurred.
//
// THE FIELD IS A MAX, NOT AN AVERAGE, and that is the difference between a soft edge and a
// slightly less hard one. Blurring the coc itself halves it at the silhouette and it is gone
// a few pixels out. This instead asks "what is the biggest coc whose own radius still
// REACHES me": every tap is worth its coc minus the distance it travelled, in units of the
// max blur. Inside the foreground that is a plateau at its own coc, and outside it decays
// linearly to nothing exactly where its disc ends. A max is separable the way an average is,
// so it is still two half-res draws.
//
//   c1 = half the sharp band, max blur radius in pixels, 1/width, 1/height
//   c4 = direction x, direction y, reading the prepared alpha, -

sampler2D sSrc : register(s0);
float4 c1 : register(c1);
float4 c4 : register(c4);

// CinematicDOF's own 18-tap table (Ioxa's gaussian). The offsets are half a texel short of
// whole ones so every tap sits between two and reaches twice as far for free.
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

float read(float2 t)
{
    // the prepared alpha is the SIGNED coc, 0.5 sharp -- the near side is its negative half
    float4 s = tex2Dlod(sSrc, float4(t, 0, 0));
    return (c4.z > 0.5) ? max(-(s.a * 2.0 - 1.0), 0.0) : s.r;
}

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    // THE FIELD IS FIVE TIMES WIDER THAN THE DISC, and that is the whole transition.
    // CinematicDOF blurs its near coc over 33.4 texels x NearPlaneMaxBlur x 0.8 -- about
    // 50 px -- while its near disc is only about 9: the alpha ramp is what you see fading,
    // not the gather. A field capped at the disc radius gives an 8 px blur an 8 px
    // transition, and that reads as a cut.
    float2 axis = c4.xy * c1.zw * max(c1.y, 1.0) * 0.1645;

    float coc = read(uv) * k_wgt[0];

    [unroll] for (int i = 1; i < 18; i++)
    {
        float2 o = axis * k_off[i];
        coc += (read(uv + o) + read(uv - o)) * k_wgt[i];
    }

    return float4(saturate(coc), 0.0, 0.0, 1.0);
}
