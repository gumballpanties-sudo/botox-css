// Ambient occlusion, pass three: bilateral blur, then straight onto the frame through the
// blend unit. SOURCE OF RECORD for fx_ao_composite.h.
//
// Rebuild after ANY edit here:
//   fxc /T ps_3_0 /E main /Fh src/hacks/screen/shaders/fx_ao_composite.h /Vn k_fxAoCompositeBytes src/hacks/screen/shaders/fx_ao_composite.hlsl
//
// WHERE THE FRAME COPY WENT: nowhere -- there is none. This writes 1-occlusion and the
// blend unit multiplies it into the frame (SRCBLEND ZERO, DESTBLEND SRCCOLOR), so unlike a
// blur pass it never has to read the frame back. One less fullscreen copy per frame, and
// msaa safe for free.
//
// The taps are MXAO's own eight -- a cross that covers two and a half texels without ever
// sampling the same one twice.
//
// DIH: THE PER-TAP DEPTH FETCH IS GONE. The prepare pass already writes a normal per pixel
// and its alpha was doing nothing, so the depth the bilateral weight needs rides along in
// it -- one fetch a tap instead of two, eight fewer full-res reads and eight fewer
// unprojections per pixel. The encode is z/(z+AO_ZK), matching fx_ao_prepare.hlsl.
//
//   c0 = zNear, zFar, 1/occlusionWidth, 1/occlusionHeight
//   c1 = blur radius, fade end, tan(fovX/2), tan(fovY/2)
//   c2 = intensity, fade start, 1/(fadeEnd-fadeStart), viewmodel raw-depth cutoff
//   c3 = fog start, 1/(fogEnd-fogStart), fog density, -
//   c4 = smoke centre, camera-local (same frame fx_ao_prepare.hlsl's view_pos builds: x =
//        world delta . camera right, y = world delta . camera up, z = world delta . camera
//        forward, all world units) -- 0,0,0,0 when no smoke is live
//   c5 = smoke radius, smoke band (fade width outside the radius), -, -
//
// 11-04: CS:S HAS smoke grenades (unlike TF2, whose removal is the reason coldtires' original
// c4..c10 smoke-cloud block was cut from this pass). A smoke cloud writes no depth, so the
// gather shades creases nobody can see through it -- ported back in as one sphere test: the
// pixel's own view-space position (already reconstructable here with c0/c1's zNear/zFar/
// tanX/tanY, same formula as fx_ao_prepare.hlsl's view_pos) against the smoke centre c4 sent
// in that same camera-local frame by hud_aim.cpp's CollectSmoke (world delta dotted with the
// camera's right/up/forward -- done on the C++ side, once a frame, not per pixel).

#define AO_ZK 256.0

sampler2D sOcclusion : register(s0);
sampler2D sDepth     : register(s1);
sampler2D sNormal    : register(s2);
float4 c0 : register(c0);
float4 c1 : register(c1);
float4 c2 : register(c2);
float4 c3 : register(c3);
float4 c4 : register(c4);
float4 c5 : register(c5);

float linear_z(float d) { return (c0.x * c0.y) / (c0.y - d * (c0.y - c0.x)); }

// camera-local position of this pixel, in world units -- same formula as fx_ao_prepare.hlsl's
// view_pos, needed here only for the smoke sphere test below.
float3 view_pos(float2 uv, float z) { return float3((uv.x * 2.0 - 1.0) * c1.z, (1.0 - uv.y * 2.0) * c1.w, 1.0) * z; }

// the prepare pass's alpha, back to view-space units. a = 1 (sky) comes out enormous, which
// is exactly the tap the depth weight below should throw away.
float unpack_z(float a) { return AO_ZK * a * rcp(max(1.0 - a, 1e-4)); }

static const float2 k_taps[8] = {
    float2( 1.5,  0.5), float2(-1.5, -0.5), float2(-0.5,  1.5), float2( 0.5, -1.5),
    float2( 1.5,  2.5), float2(-1.5, -2.5), float2(-2.5,  1.5), float2( 2.5, -1.5)
};

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float raw = tex2Dlod(sDepth, float4(uv, 0, 0)).r;
    float z   = linear_z(raw);

    // SKY, VIEWMODEL AND ANYTHING PAST THE FADE END FIRST. These pixels end at ao 0 whatever
    // the blur says -- the fade below multiplies them out and the gather never even filled
    // them in -- and the blur is 24 fetches. White is what the blend unit reads as "leave
    // this pixel alone".
    if (z >= c0.y * 0.98 || raw < c2.w || z >= c1.y)
        return float4(1, 1, 1, 1);

    float3 n  = tex2Dlod(sNormal, float4(uv, 0, 0)).xyz * 2.0 - 1.0;
    float  ao = tex2Dlod(sOcclusion, float4(uv, 0, 0)).r;
    float sum = 1.0;

    // A blur radius of zero still ran all eight taps, and every one of them landed on this
    // pixel's own texel -- 16 fetches to arrive back where it started. The register is
    // uniform over the draw, so the branch costs nothing.
    if (c1.x > 0.001)
    {
        float2 texel = c0.zw * c1.x;
        float  tol  = rcp(z * 0.05 + 1.0);

        [unroll] for (int i = 0; i < 8; i++)
        {
            float2 t   = uv + k_taps[i] * texel;
            float4 tn4 = tex2Dlod(sNormal, float4(t, 0, 0));
            float  tz  = unpack_z(tn4.a);
            float3 tn  = tn4.xyz * 2.0 - 1.0;

            // A tap only counts if it is on the same surface: within a hair of this depth AND
            // facing nearly the same way. That is what keeps the blur from dragging a corner's
            // shadow out over the floor. The depth tolerance grows with distance because so
            // does the depth buffer's own step.
            float dw = saturate(1.0 - abs(tz - z) * tol);
            float nw = saturate(dot(tn, n) * 16.0 - 15.0);
            float w  = dw * nw;

            ao  += tex2Dlod(sOcclusion, float4(t, 0, 0)).r * w;
            sum += w;
        }
    }

    ao /= sum;
    // the square root the gather stored, undone
    ao *= ao;
    // intensity as an exponent rather than a multiply: it cannot push a pixel past fully occluded
    ao = 1.0 - pow(saturate(1.0 - ao), c2.x);
    // the plain distance fade, and then the fog one on top of it
    ao *= 1.0 - saturate((z - c2.y) * c2.z);

    // FOG. What the fog has already turned into flat colour must not get a crease drawn on
    // it -- this is the whole "underneath the fog" trick. A density of 0 makes the term a
    // no-op, which is what a map with no fog and the fade switched off both give.
    ao *= 1.0 - saturate((z - c3.x) * c3.y) * c3.z;

    // SMOKE. Same trick as fog, by sphere instead of by plane: a smoke cloud writes no depth
    // of its own, so a crease shaded behind it is a crease nobody can see. c5.x = 0 (no smoke
    // live, or the feature is off) makes this a no-op -- band is never 0 then, so no div-by-zero.
    if (c5.x > 0.0)
    {
        float3 p = view_pos(uv, z);
        float  dist = length(p - c4.xyz);
        ao *= saturate((dist - c5.x) / c5.y);
    }

    // 1 - occlusion, and the blend unit multiplies it into the frame. With blending switched
    // off by the caller the exact same shader draws the debug view instead.
    return float4(1.0 - ao, 1.0 - ao, 1.0 - ao, 1.0);
}
