// Depth of field, pass two: the far-plane gather. SOURCE OF RECORD for fx_dof_gather.h.
//
// Rebuild after ANY edit here:
//   fxc /T ps_3_0 /E main /Fh src/hacks/screen/shaders/fx_dof_gather.h /Vn k_fxDofGatherBytes src/hacks/screen/shaders/fx_dof_gather.hlsl
//
// Reads only the prepared texture: rgb the picture, alpha the signed circle of confusion the
// pass before worked out. Everything below runs off that alpha, so the tap loop never reads
// depth at all.
//
// DIH: coldtires compiles one of twelve variants (three shapes x four tap ceilings) and one
// more for its debug views. This is the bokeh shape with the 64-tap ceiling and no debug
// path -- the shape rows were already out of its menu, and the tap count is dynamic anyway.
//
//   c0 = zNear, zFar, focal distance, 1/blur ramp
//   c1 = half the sharp band, max blur radius in pixels, 1/width, 1/height
//   c3 = bokeh highlight boost, bokeh threshold, edge softness, 1/near ramp
//   c4 = tap ceiling (quality: 16/32/64/128), show depth, its white point (units), -
//   c5 = near bleed chain on, taps per pixel of radius, -, -

// the loop's hard bound; c4.x is the ceiling the "quality" combo picks under it
#define TAPS 128

// The golden angle, and the SAME angle as a rotation. Advancing the tap direction by one
// complex multiply is what the loop does instead of sincos per tap: on ps_3_0 sincos is a
// macro of about eight instructions and this is two mads, for the identical directions.
#define GOLDEN_HALF 1.19998162       // 0.5 * 2.39996323, the first tap's angle
static const float2 k_rot = float2(-0.73736888, 0.67549029);   // cos/sin of 2.39996323

sampler2D sPrepared : register(s0);
sampler2D sDepth    : register(s1);   // read by the "show depth" view only
float4 c0 : register(c0);
float4 c1 : register(c1);
float4 c3 : register(c3);
float4 c4 : register(c4);
float4 c5 : register(c5);

float linear_z(float d) { return (c0.x * c0.y) / (c0.y - d * (c0.y - c0.x)); }

// The frame arrives already tonemapped, so a bright pixel is worth barely more than a grey
// one and averaging 40 of them gives grey mush. These two undo / redo a reinhard curve
// around the gather (qUINT ADoF does the same) so the sum happens in something like linear
// light and a highlight still reads as a highlight after it is spread over a disc.
float3 unpack_hdr(float3 c) { return c * rcp(1.2 - saturate(c)); }
float3 pack_hdr(float3 c)   { return 1.2 * c * rcp(c + 1.0); }

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float4 mid    = tex2Dlod(sPrepared, float4(uv, 0, 0));
    float3 centre = mid.rgb;
    float  cs     = mid.a * 2.0 - 1.0;

    // botox "show depth": linear depth, white at c4.z units. tex2Dlod, so legal in the branch.
    if (c4.y > 0.5)
        return float4(saturate(linear_z(tex2Dlod(sDepth, float4(uv, 0, 0)).r) / c4.z).xxx, mid.a);

    // THE NEAR PLANE IS NOT THIS PASS'S when the bleed chain is on: a foreground blurred here
    // can only ever fill its own silhouette, because the sharp pixels beside it gather
    // nothing at all. The near pass blurs it off the BLURRED circle of confusion instead,
    // which reaches past the edge.
    if (c5.x > 0.5 && cs < 0.0)
        return float4(centre, mid.a);

    float radius = abs(cs) * c1.y;

    // A sharp pixel is most of the screen most of the time, and it costs 2 samples instead
    // of 34. Alpha goes back out untouched: the smoothing pass runs off the SAME signed coc
    // and has no depth texture of its own to work it out again.
    if (radius < 0.5)
        return float4(centre, mid.a);

    // THE TAP COUNT FOLLOWS THE DISC. A fixed count is what made big radii look cheap:
    // spread 16 taps over a 30 pixel disc and the gaps between them are 8 pixels wide, which
    // reads as rings and dots. What has to stay constant is the SPACING, so the count rises
    // with the radius and TAPS is only the ceiling.
    //
    // DIH: the density is c5.y and not the 2.5 it was hardcoded at. 2.5 taps per pixel of
    // radius means a 20px blur costs fifty fetches on every blurred pixel of the frame, and
    // the smoothing pass that runs after this exists precisely to close the gaps a sparser
    // spiral leaves -- so the taps were being paid for twice.
    float n    = clamp(ceil(radius * c5.y), 8.0, min(c4.x, (float)TAPS));
    float invN = rcp(n);

    // The whole disc is rotated by a per-pixel angle (interleaved gradient noise). Without
    // it every pixel samples the SAME directions and the result is that many ghost copies of
    // the image -- which is exactly what reads as chopping.
    float2 px  = uv / c1.zw;
    float  jit = frac(52.9829189 * frac(dot(px, float2(0.06711056, 0.00583715)))) * 6.2831853;

    // ONE sincos for the whole disc; k_rot walks it round the spiral from here.
    float sa, ca;
    sincos(GOLDEN_HALF + jit, sa, ca);
    float2 dir = float2(ca, sa);

    // Everything the loop used to recompute per tap.
    float featherInv = rcp(max(1.0, radius * 0.35));

    float4 sum = float4(unpack_hdr(centre), 1.0);

    // [loop], not [unroll]: the count is dynamic, and 64 taps unrolled would blow the ps_3_0
    // instruction limit anyway. The loop body carries no gradient reads, so the branch is legal.
    [loop] for (int i = 0; i < TAPS; i++)
    {
        if ((float)i >= n) break;

        // The golden-angle spiral: every tap lands on an even disc for any count, no table
        // needed. The bokeh power pushes them out to the rim instead of spreading them evenly.
        float fi  = (float)i + 0.5;
        // how far out this tap is, in PIXELS -- the scatter test below wants it anyway, so
        // the offset is built from it rather than the other way round
        float rpx = pow(fi * invN, 0.35) * radius;

        float2 off = dir * (rpx * c1.zw);

        // ONE fetch. Colour and the tap's own signed coc arrive together.
        float4 s  = tex2Dlod(sPrepared, float4(uv + off, 0, 0));
        float3 t  = s.rgb;
        float  ts = s.a * 2.0 - 1.0;

        // the next tap's direction, two mads
        dir = float2(dir.x * k_rot.x - dir.y * k_rot.y, dir.x * k_rot.y + dir.y * k_rot.x);

        // SCATTER AS GATHER. A tap only reaches this pixel if its own circle of confusion is
        // wide enough to cover the distance between them: a sharp foreground reaches nothing
        // and still cannot smear, but a BLURRED foreground spreads out past its own
        // silhouette the way it physically should.
        float trad = abs(ts) * c1.y;

        // TWO OUT OF FOCUS SURFACES BEHIND THE FOCAL PLANE BARELY OCCLUDE EACH OTHER. The
        // strict rule is right for a foreground against a background, but on a far building
        // against a far sky it keeps the silhouette at full contrast while everything inside
        // it turns to mush -- the edge ends up the sharpest thing on screen. When the tap AND
        // this pixel are both on the far side the reach is relaxed toward the full disc by
        // c3.z; 0 there is the strict behaviour exactly.
        float relax = (ts > 0.0 && cs > 0.0) ? c3.z : 0.0;
        float reach = (ts < cs) ? lerp(trad, radius, relax) : radius;

        // The feather has to scale with the disc. A fixed one is about a pixel and a half
        // wide, so on a twenty pixel blur the accept/reject boundary is a hard line drawn
        // inside the soft image.
        float w = saturate((reach - rpx) * featherInv + 1.0);

        // A near plane tap is worth NOTHING here for the same reason -- it is unblurred in
        // this buffer, so letting it in would smear a sharp foreground colour over the
        // background behind it.
        w *= (c5.x > 0.5 && ts < 0.0) ? 0.0 : 1.0;

        // Bokeh: bright pixels count for more, which is what turns a highlight into a disc of
        // its own instead of a smear. The weight is read off the TONEMAPPED luma (the
        // threshold is in screen values), then the colour goes into the sum with the curve
        // undone.
        w *= 1.0 + c3.x * saturate(dot(t, float3(0.2126, 0.7152, 0.0722)) - c3.y);
        t  = unpack_hdr(t);

        sum += float4(t * w, w);
    }

    float3 outc = pack_hdr(sum.rgb / sum.a);
    return float4(outc, mid.a);
}
