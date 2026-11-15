// Depth of field: the near plane, over the finished far plane. SOURCE OF RECORD for
// fx_dof_near.h.
//
// Rebuild after ANY edit here:
//   fxc /T ps_3_0 /E main /Fh src/hacks/screen/shaders/fx_dof_near.h /Vn k_fxDofNearBytes src/hacks/screen/shaders/fx_dof_near.hlsl
//
// CinematicDOF's PS_NearBokehBlur, run over the finished far-plane image, radius off the field.
//
// THE OUTLINE RULE: alpha has to be 1 ON the silhouette, from both sides. Anywhere it is less,
// lerp(mid, ...) mixes the UNBLURRED frame back in, and mid is the one thing that jumps there
// (foreground colour on one side, background on the other) -- blurred inside, razor outline.
// Three terms, max'd:
//   gate  this pixel is foreground and visibly blurred
//   over  how deep inside some foreground tap's own disc this pixel sits: 1 for its inner
//         half, 0 at its rim -- the same for a 4 px pole as for a wall
//   tail  CinematicDOF's 1.4 x field, the wide soft fade past that
// gate and over fade by r^2 under a pixel of blur, or the 1 px disc floor softens near-focus.
// The tail never feeds the coc that goes out: it would mark the whole fade near and the
// smoothing pass would wipe the sharp background there.
// The disc is the foreground's own (field r / g), so the halo is as wide as its blur.
//
//   s0 the far-plane result, s1 depth (read for ONE thing: telling the viewmodel apart),
//   s2 the near field: r blurred coc, g blurred mask
//   c1 = half the sharp band, max blur radius in pixels, 1/width, 1/height
//   c2 = viewmodel cutoff (RAW depth), -, VIEWMODEL PASS, -
//   c3 = bokeh highlight boost, bokeh threshold, -, -
//   c4 = tap ceiling (quality), -, -, show near bleed
//   c5 = -, taps per pixel of radius, -, -

#define TAPS 128

// same trick as the far gather: one sincos, then walk the spiral by complex multiply
#define GOLDEN_HALF 1.19998162
static const float2 k_rot = float2(-0.73736888, 0.67549029);

sampler2D sFar   : register(s0);
sampler2D sDepth : register(s1);
sampler2D sCoc   : register(s2);
float4 c1 : register(c1);
float4 c2 : register(c2);
float4 c3 : register(c3);
float4 c4 : register(c4);
float4 c5 : register(c5);

float3 unpack_hdr(float3 c) { return c * rcp(1.2 - saturate(c)); }
float3 pack_hdr(float3 c)   { return 1.2 * c * rcp(c + 1.0); }

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float4 mid = tex2Dlod(sFar, float4(uv, 0, 0));
    float  cs  = mid.a * 2.0 - 1.0;
    float  own = max(-cs, 0.0);
    float2 fld = tex2Dlod(sCoc, float4(uv, 0, 0)).rg;
    float  nb  = fld.r;
    float  cl  = saturate(nb / max(fld.g, 1e-3));

    // botox "show near bleed": red the field, green the halo radius (x max blur), blue max blur
    if (c4.w > 0.5)
        return float4(nb, cl, saturate(c1.y * 0.0166667), 1.0);

    // A held-sharp viewmodel is IN FRONT of every near plane thing in the world, so nothing
    // is allowed to bleed over it -- its own DepthRange (raw < 0.1) is the test, same as the
    // prepare pass.
    //
    // THE TEST IS OFF IN THE VIEWMODEL PASS (c2.z), because there the gun is the subject and
    // not the thing being protected: rejecting raw < 0.1 would throw away every pixel the
    // pass exists to blur. Nothing else can be hurt by leaving it out -- the world's coc is
    // zero in that pass, so the only place the alpha above is non-zero is the gun and the
    // few pixels its own field bleeds over.
    if (c2.z < 0.5 && tex2Dlod(sDepth, float4(uv, 0, 0)).r < c2.x)
        return mid;

    float gate  = saturate(own * c1.y);
    gate *= gate;
    float alpha = max(gate, saturate(1.4 * nb));

    // no foreground anywhere near: the mask is a wide gaussian, so this is far past any disc
    if (alpha < 0.004 && fld.g < 0.002)
        return mid;

    float reach  = max(own, cl);
    float radius = max(reach * c1.y, 1.0);
    float n      = clamp(ceil(radius * c5.y), 8.0, min(c4.x, (float)TAPS));
    float invN   = rcp(n);

    float2 px  = uv / c1.zw;
    float  jit = frac(52.9829189 * frac(dot(px, float2(0.06711056, 0.00583715)))) * 6.2831853;

    float sa, ca;
    sincos(GOLDEN_HALF + jit, sa, ca);
    float2 dir = float2(ca, sa);

    float2 scale = radius * c1.zw;

    // centre colour NOT scaled by the field (CinematicDOF does): at 8 taps that darkened the fade
    float4 sum  = float4(unpack_hdr(mid.rgb) * 0.5, 0.5);
    float  over = 0.0;

    [loop] for (int i = 0; i < TAPS; i++)
    {
        if ((float)i >= n) break;

        float fi = (float)i + 0.5;
        // area-uniform like his rings; the far gather's rim-heavy spiral drew hard halo sides
        float rr  = sqrt(fi * invN);
        float rpx = rr * radius;

        float2 off = dir * (rr * scale);
        float4 s   = tex2Dlod(sFar, float4(uv + off, 0, 0));
        float3 t   = s.rgb;
        float  tr  = max(-(s.a * 2.0 - 1.0), 0.0) * c1.y;
        float  tf  = saturate(tr);

        dir = float2(dir.x * k_rot.x - dir.y * k_rot.y, dir.x * k_rot.y + dir.y * k_rot.x);

        over = max(over, saturate(2.0 - 2.0 * rpx / max(tr, 1e-3)) * tf * tf);

        // no occlusion test on the colour: "bleed all you want". Ring weight is his at busy 0.5.
        float w = lerp(rr, 1.0, 0.5);
        w *= 1.0 + c3.x * saturate(dot(t, float3(0.2126, 0.7152, 0.0722)) - c3.y);
        t  = unpack_hdr(t);

        sum += float4(t * w, w);
    }

    alpha = max(alpha, over);

    float3 outc = pack_hdr(sum.rgb / max(sum.a, 0.0001));

    // the coc goes out blended too, or the smoothing pass hands the edge straight back
    float sc = lerp(cs, -reach, max(gate, over));

    return float4(lerp(mid.rgb, outc, alpha), sc * 0.5 + 0.5);
}
