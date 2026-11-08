// Ambient occlusion, pass two: the gather. SOURCE OF RECORD for fx_ao_occlusion.h.
//
// Rebuild after ANY edit here:
//   fxc /T ps_3_0 /E main /Fh src/hacks/screen/shaders/fx_ao_occlusion.h /Vn k_fxAoOcclusionBytes src/hacks/screen/shaders/fx_ao_occlusion.hlsl
//
// Every tap asks one question: is the surface at that pixel in FRONT of the plane this
// pixel's normal defines, and close enough to matter.
//
//   c0 = zNear, zFar, tan(fovX/2), tan(fovY/2)
//   c1 = aspect, sample count, world radius, -1/radius^2
//   c2 = angle bias, viewmodel raw-depth cutoff, max screen radius, self-occlusion bias
//   c3 = target width, target height, fade end, -

// one sincos for the whole disc, then the golden angle applied as a rotation
#define GOLDEN_HALF 1.19998162
static const float2 k_rot = float2(-0.73736888, 0.67549029);

sampler2D sNormal : register(s0);
sampler2D sDepth  : register(s1);
float4 c0 : register(c0);
float4 c1 : register(c1);
float4 c2 : register(c2);
float4 c3 : register(c3);

float linear_z(float d) { return (c0.x * c0.y) / (c0.y - d * (c0.y - c0.x)); }

float3 view_pos(float2 uv, float z)
{
    return float3((uv.x * 2.0 - 1.0) * c0.z, (1.0 - uv.y * 2.0) * c0.w, 1.0) * z;
}

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float raw = tex2Dlod(sDepth, float4(uv, 0, 0)).r;
    float z   = linear_z(raw);

    // The sky is at the far plane, and the viewmodel is the raw depth range 0 to 0.1 the
    // engine compresses it into (viewrender.cpp:1109) -- a RAW test, because in world units
    // it is indistinguishable from the wall in front of you. Neither is geometry this pass
    // can shade. Past the fade end the composite multiplies the term by zero, so gathering
    // it is paid for and thrown away -- on an open map that is most of the screen.
    if (z >= c0.y * 0.98 || raw < c2.y || z >= c3.z)
        return 0.0;

    float3 p = view_pos(uv, z);
    float3 n = normalize(tex2Dlod(sNormal, float4(uv, 0, 0)).xyz * 2.0 - 1.0);

    // Pushed off its own surface by a fraction of the distance to it. Without this every
    // flat wall occludes itself out of pure depth quantisation and the whole screen greys.
    p += n * (z * c2.w);

    // The world-space radius, projected. A length r at depth z covers r/(2*tan*z) of the
    // screen; the clamp stops a wall pressed against the camera asking for half the frame.
    float rad = min(c1.z / (2.0 * c0.z * z), c2.z);

    // Interleaved gradient noise. Without a per-pixel rotation every pixel samples the SAME
    // directions and the error is structured -- bands and ghosts. With it, it is grain the
    // blur eats.
    float jit = frac(52.9829189 * frac(dot(uv * c3.xy, float2(0.06711056, 0.00583715)))) * 6.2831853;

    float ao = 0.0;
    int count = (int)c1.y;

    float  invN = rcp(c1.y);
    // the y offset is scaled by the aspect ratio or the disc comes out an ellipse
    float2 ell  = rad * float2(1.0, c1.x);

    float sa, ca;
    sincos(GOLDEN_HALF + jit, sa, ca);
    float2 dir = float2(ca, sa);

    // Golden-angle spiral: an even disc for ANY sample count, no poisson table to feed.
    // sqrt spreads the taps over the area rather than the radius, so the middle is not
    // oversampled.
    [loop] for (int s = 0; s < count; s++)
    {
        float i  = (float)s + 0.5;
        float rr = sqrt(i * invN);
        float2 t = uv + dir * (rr * ell);

        dir = float2(dir.x * k_rot.x - dir.y * k_rot.y, dir.x * k_rot.y + dir.y * k_rot.x);

        float traw = tex2Dlod(sDepth, float4(t, 0, 0)).r;
        float tz   = linear_z(traw);
        float3 d   = view_pos(t, tz) - p;
        float v2   = dot(d, d);
        // how far above the surface plane the tap sits, and how much the falloff still cares
        float vn   = dot(d, n) * rsqrt(v2);

        // AND THE TAP HAS TO BE WORLD. Unprojecting the viewmodel with the world's near
        // plane puts it a few units in front of the eye -- a huge occluder hanging over
        // everything. Skipping viewmodel PIXELS is not enough: a wall pixel beside the gun
        // still reaches taps that land on it, and that is the dark cloud hugging the
        // weapon's silhouette.
        ao += saturate(1.0 + c1.w * v2) * saturate(vn - c2.x) * step(c2.y, traw);
    }

    ao = saturate(ao / ((1.0 - c2.x) * c1.y) * 2.0);

    // square rooted on the way into 8 bits, and squared again after the blur
    return sqrt(ao).xxxx;
}
