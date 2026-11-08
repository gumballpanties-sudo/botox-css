// Depth of field: the near plane, over the finished far plane. SOURCE OF RECORD for
// fx_dof_near.h.
//
// Rebuild after ANY edit here:
//   fxc /T ps_3_0 /E main /Fh src/hacks/screen/shaders/fx_dof_near.h /Vn k_fxDofNearBytes src/hacks/screen/shaders/fx_dof_near.hlsl
//
// CinematicDOF's PS_NearBokehBlur. It runs over the finished far-plane image and its radius
// comes from the blurred field, not from this pixel's own depth -- which is the whole trick:
// a sharp wall pixel a few pixels outside a blurred player still gets a radius, still
// gathers, and is then blended in by an alpha that falls off with the field. That fade IS
// the soft edge.
//
// No occlusion test anywhere in the loop. A foreground is in front of everything by
// definition, so it is allowed to bleed over all of it -- the far gather already refused
// every near tap for the same reason.
//
//   s0 the far-plane result, s1 depth (read for ONE thing: telling the viewmodel apart),
//   s2 the blurred near coc field
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
    float  nb  = tex2Dlod(sCoc, float4(uv, 0, 0)).r;

    // CINEMATICDOF'S ALPHA, VERBATIM: (min(2.5, NearPlaneMaxBlur) + 0.4) x (near plane
    // itself ? 2 : 1) x the blurred coc, and where the field has thinned out the pixel's own
    // coc holds it up instead. It is a BOOST, not a coverage -- it saturates well outside the
    // silhouette and the fade is the tail of the wide field.
    float alpha = saturate(1.4 * ((nb > 0.1) ? ((own > 0.0) ? 2.0 : 1.0) * nb : max(nb, own)));

    // botox "show near bleed": the field, its alpha, the max blur (botox k_near_body, c5.y there)
    if (c4.w > 0.5)
        return float4(nb, alpha, saturate(c1.y * 0.0166667), 1.0);

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

    // NOTHING THIS PIXEL WOULD SEE. The old test was nb <= 0 && own <= 0, and the field this
    // reads is deliberately five times wider than the disc -- so out in its tail nb is a
    // thousandth, alpha rounds to nothing, and the pixel still paid for eight taps and a
    // lerp that could not move it by one of 255 levels. Most of a frame with any foreground
    // in it is that tail.
    if (alpha < 0.004)
        return mid;

    // The disc is the field's, with a one pixel floor: out in the tail the field is small but
    // the alpha there is not, and an early out would cut the fade off again.
    float radius = max(nb * c1.y, 1.0);
    float n      = clamp(ceil(radius * c5.y), 8.0, min(c4.x, (float)TAPS));
    float invN   = rcp(n);

    float2 px  = uv / c1.zw;
    float  jit = frac(52.9829189 * frac(dot(px, float2(0.06711056, 0.00583715)))) * 6.2831853;

    float sa, ca;
    sincos(GOLDEN_HALF + jit, sa, ca);
    float2 dir = float2(ca, sa);

    float2 scale = radius * c1.zw;

    // The centre the way CinematicDOF weights it: its COLOUR is scaled by the field but its
    // weight is the busy factor alone, so where the field is weak the centre stops dominating.
    float4 sum = float4(unpack_hdr(mid.rgb) * nb * 0.5, 0.5);

    [loop] for (int i = 0; i < TAPS; i++)
    {
        if ((float)i >= n) break;

        float fi = (float)i + 0.5;
        float rr = pow(fi * invN, 0.35);

        float2 off = dir * (rr * scale);
        float4 s   = tex2Dlod(sFar, float4(uv + off, 0, 0));
        float3 t   = s.rgb;

        dir = float2(dir.x * k_rot.x - dir.y * k_rot.y, dir.x * k_rot.y + dir.y * k_rot.x);

        // NO OCCLUSION TEST, NO SCATTER TEST -- CinematicDOF has none in its near loop and
        // says so: "no further weight needed, bleed all you want". The ring weight is his,
        // lerp(ring/rings, 1, smoothstep(0,1,busy)) at the 0.5 busy default.
        float w = lerp(rr, 1.0, 0.5);
        w *= 1.0 + c3.x * saturate(dot(t, float3(0.2126, 0.7152, 0.0722)) - c3.y);
        t  = unpack_hdr(t);

        sum += float4(t * w, w);
    }

    float3 outc = pack_hdr(sum.rgb / max(sum.a, 0.0001));

    // The coc goes out blended too: where the bleed took over, the smoothing pass has to be
    // told this pixel is blurred now, or it hands the edge straight back.
    float sc = lerp(cs, -max(nb, own), alpha);

    return float4(lerp(mid.rgb, outc, alpha), sc * 0.5 + 0.5);
}
