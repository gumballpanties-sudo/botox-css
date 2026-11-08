// Depth of field, pass one: the frame plus its signed circle of confusion.
// SOURCE OF RECORD for fx_dof_prepare.h.
//
// Rebuild after ANY edit here:
//   fxc /T ps_3_0 /E main /Fh src/hacks/screen/shaders/fx_dof_prepare.h /Vn k_fxDofPrepareBytes src/hacks/screen/shaders/fx_dof_prepare.hlsl
//
// One fetch of the frame, one of the depth; out comes the frame again with the SIGNED
// circle of confusion in alpha: 0.5 sharp, below that in front of the focal plane, above it
// behind. Signed because the gather still has to know which taps are nearer than the pixel
// they are reaching, and the sign of the coc is monotonic in depth wherever it is not
// saturated. It costs two fetches per pixel once and saves one fetch per TAP in the gather.
//
//   c0 = zNear, zFar, focal distance, 1/blur ramp
//   c1 = half the sharp band, max blur radius in pixels, 1/width, 1/height
//   c2 = viewmodel cutoff (RAW depth, 0.1), near blur on, VIEWMODEL PASS, particle guard
//   c3 = -, -, -, 1/near ramp
//   c5 = -, -, the gun's zNear, the gun's zFar

sampler2D sFrame : register(s0);
sampler2D sDepth : register(s1);
sampler2D sPre   : register(s2);   // the frame before one translucent thing drew
float4 c0 : register(c0);
float4 c1 : register(c1);
float4 c2 : register(c2);
float4 c3 : register(c3);
float4 c5 : register(c5);

float linear_z(float d) { return (c0.x * c0.y) / (c0.y - d * (c0.y - c0.x)); }

// The gun is drawn through a projection of its own (CViewSetup zNearViewmodel/zFarViewmodel)
// AND squeezed into the first tenth of the depth range, so its raw value has to be stretched
// back out before it means anything.
float linear_vm(float d) { return (c5.z * c5.w) / (c5.w - d * (c5.w - c5.z)); }

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float3 c = tex2Dlod(sFrame, float4(uv, 0, 0)).rgb;

    // The RAW sample is kept as well as the unprojected one: the viewmodel is told apart by
    // the depth range it was drawn into, and that is a fact about the raw value which
    // unprojecting destroys.
    float raw = tex2Dlod(sDepth, float4(uv, 0, 0)).r;

    // THE VIEWMODEL PASS. It runs after DrawViewModels, over a frame the world pass already
    // finished, so the ONLY thing allowed a circle of confusion here is the gun -- everything
    // else goes out at 0.5, which is sharp, and every pass downstream leaves it alone.
    if (c2.z > 0.5)
    {
        if (raw >= c2.x)
            return float4(c, 0.5);

        float zv = linear_vm(raw / max(c2.x, 1e-4));
        float nv = saturate((c0.z - c1.x - zv) * c3.w);
        return float4(c, -nv * 0.5 + 0.5);   // the gun is foreground by definition
    }

    float z = linear_z(raw);

    float f = saturate((z - c0.z - c1.x) * c0.w);

    // The near side gets its OWN ramp (c3.w), because it only has focus minus half the sharp
    // band of room to use: share the far ramp and a focus of 400 units can never blur
    // anything in front of it by more than a third, which reads as the near blur being
    // broken whenever the focus is close.
    float n = saturate((c0.z - c1.x - z) * c3.w) * c2.y;

    // THE VIEWMODEL, AND ONLY THE VIEWMODEL. It is drawn through DepthRange(0, 0.1)
    // (viewrender.cpp:1109), so every pixel of it is a raw depth below 0.1 and no world
    // pixel past 7.8 units ever is -- a test in world units throws the near 24 of them away,
    // wall, railing and crate with it.
    float k = (raw < c2.x) ? 0.0 : max(f, n);
    float s = (n > f) ? -1.0 : 1.0;

    // THE PARTICLE GUARD. A crit spark, a muzzle flash, a rocket trail and every other
    // particle draws with z writes off, so the depth this pixel's blur was just computed from
    // belongs to whatever is BEHIND the spark -- and the spark comes out as blurred as a wall
    // thirty metres away. sPre is the same frame photographed the instant opaque geometry
    // finished, so anything that differs from it was painted by something with no depth at
    // all. Those pixels keep their colour and lose their circle of confusion, which does two
    // things at once: they are not blurred, and the gather's scatter test then refuses to let
    // them smear into the background either (a sharp tap reaches nothing).
    //
    // The floor is what stops msaa resolve noise and dithering from reading as a particle,
    // and the slope reaches full guard about a tenth of a level above it.
    if (c2.w > 0.001)
    {
        float3 d3 = abs(c - tex2Dlod(sPre, float4(uv, 0, 0)).rgb);
        float  d  = max(max(d3.r, d3.g), d3.b);
        k *= 1.0 - saturate((d - 0.02) * 12.0) * c2.w;
    }

    return float4(c, k * s * 0.5 + 0.5);
}
