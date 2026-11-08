// botox color_correction.cpp k_shader, 1:1: color correction + sharpen / film grain / vignette /
// invert / posterize / deband / channel shift in ONE pass over a copy of the finished frame.
// SOURCE OF RECORD for fx_grade.h (the compiled bytecode).
//
// Rebuild after ANY edit here:
//   fxc /nologo /T ps_3_0 /E main /Fh src/hacks/screen/shaders/fx_grade.h /Vn k_fxGradeBytes src/hacks/screen/shaders/fx_grade.hlsl
//
// botox compiles one variant per enabled-stage mask at runtime (D3DXCompileShader, #if STAGE).
// We ship bytecode, so the #ifs became bool registers b0..b12 = static flow control: same
// stages, same order, same math, one shader. Drawn through screenfx.cpp's vs_3_0 quad
// (ps_3_0 cannot pair with fixed-function XYZRHW).
//
// b1 LEVELS b3 CURVES b6 SHARPEN b7 GRAIN b8 VIGNETTE b9 INVERT b10 POSTERIZE b11 DEBAND (botox's
// grade_* bit numbers). TEMPERATURE / HUE / TONE / BASIC / SHIFT are not branched: the cpu sends
// identity constants while they are off. Constants c0..c18 laid out exactly as botox's.

sampler2D s0 : register(s0);   // frame copy
sampler2D s1 : register(s1);   // 256x1 curves table
float4 c0 : register(c0);
float4 c1 : register(c1);
float4 c2 : register(c2);
float4 c3 : register(c3);
float4 c4 : register(c4);
float4 c5 : register(c5);
float4 c6 : register(c6);
float4 c7 : register(c7);
float4 c8 : register(c8);
float4 c9 : register(c9);
float4 c10 : register(c10);
float4 c11 : register(c11);
float4 c12 : register(c12);
float4 c13 : register(c13);
float4 c14 : register(c14);
float4 c15 : register(c15);
float4 c16 : register(c16);
float4 c17 : register(c17);
float4 c18 : register(c18);

bool LEVELS      : register(b1);
bool CURVES      : register(b3);
bool SHARPEN     : register(b6);
bool GRAIN       : register(b7);
bool VIGNETTE    : register(b8);
bool INVERT      : register(b9);
bool POSTERIZE   : register(b10);
bool DEBAND      : register(b11);
// SHIFT: c10/c11.xy = 0 while off, see src()

// src = shifted read for sharpen / blur taps. deband taps read raw (botox's note: where shift
// shows the step test keeps c, where deband acts shift barely changes the average)
float3 raw(float2 t) { return tex2Dlod(s0, float4(t, 0, 0)).rgb; }
// Always the 3-fetch form: c10/c11.xy are zero while shift is off (cpu side), so it reads the
// same texel three times. A branch here gets inlined at every call site and pushed the shader
// past ps_3_0's 512 guaranteed slots.
float3 src(float2 t)
{
    float4 q = t.xyxy + c10;
    return float3(tex2Dlod(s0, float4(q.xy, 0, 0)).r, tex2Dlod(s0, float4(q.zw, 0, 0)).g,
                  tex2Dlod(s0, float4(t + c11.xy, 0, 0)).b);
}

float4 rnm(float2 tc)
{
    float n = sin(dot(tc, float2(12.9898, 78.233))) * 43758.5453;
    return float4(frac(n), frac(n * 1.2154), frac(n * 1.3453), frac(n * 1.3647)) * 2.0 - 1.0;
}
float2 grad(float perm, float z) { return rnm(float2(perm, z)).rg * 4.0 - 1.0; }
float pnoise(float2 p, float z)
{
    float2 pi = 0.00390625 * floor(p) + 0.001953125;
    float2 pf = frac(p);
    float n00 = dot(grad(rnm(pi).a, z), pf);
    float n01 = dot(grad(rnm(pi + float2(0.0, 0.00390625)).a, z), pf - float2(0.0, 1.0));
    float n10 = dot(grad(rnm(pi + float2(0.00390625, 0.0)).a, z), pf - float2(1.0, 0.0));
    float n11 = dot(grad(rnm(pi + 0.00390625).a, z), pf - 1.0);
    float2 f = pf * pf * pf * (pf * (pf * 6.0 - 15.0) + 10.0);
    return lerp(lerp(n00, n01, f.y), lerp(n10, n11, f.y), f.x);
}

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float3 c = src(uv);

    // deband = vegas boris dft DeBand, botox's single pass form (see color_correction.cpp)
    float2 dq = uv * c11.zw;
    float dn = 0.0;
    [branch] if (DEBAND) {
        float2 dh = c13.xy * 0.5;
        dn = frac(52.9829189 * frac(dot(dq + c16.w, float2(0.06711056, 0.00583715))));
        float2 dd; sincos(6.2831853 * dn, dd.y, dd.x);
        float3 d0 = raw(uv), da = 0.0;
        [loop] for (float di = 0.5; di < 16.0; di += 1.0) {
            if (di > c17.x) break;
            float3 dp = d0, dj = 0.0; float dk = 1.0, dl = 0.0;
            [loop] for (float dm = 0.0; dm < 12.0; dm += 1.0) {
                float2 df = dq + dd * dk;
                float3 ds = raw((df - frac(df)) * c13.xy + dh);
                dj += clamp(ds - dp, -c16.x, c16.x); dp = ds;
                da += dj * (sqrt(dk) * exp2(-dk * c16.y) * (dk - dl)); dl = dk; dk = max(dk + 1.0, dk * c18.z);
            }
            dd = float2(dd.x * c9.z - dd.y * c9.w, dd.x * c9.w + dd.y * c9.z);
        }
        c += da * c18.w;
    }

    float vm = 0.0;
    [branch] if (VIGNETTE) {
        float2 vt = uv - 0.5;
        vt.x *= c7.z;
        vm = smoothstep(c7.x, c7.y, length(vt));
        vm = lerp(vm, 1.0 - vm, c7.w);
        // blur: 24 tap vogel disk, radius = mask * blur px, under half a px skipped
        [branch] if (vm * c9.x > 0.5) {
            float2 vp = uv * c11.zw;
            float2 vd;
            sincos(6.2831853 * frac(52.9829189 * frac(dot(vp, float2(0.06711056, 0.00583715)))), vd.y, vd.x);
            float2 vs = vm * c9.x * c13.xy;
            float3 vb = c;
            [loop] for (float i = 0.5; i < 24.0; i += 1.0) {
                vb += src(uv + vd * sqrt(i / 24.0) * vs);
                vd = float2(vd.x * -0.73736888 - vd.y * 0.67549029, vd.x * 0.67549029 + vd.y * -0.73736888);
            }
            c = vb / 25.0;
        }
    }

    [branch] if (SHARPEN) {
        float3 b = src(uv + float2(c13.x, -c13.y) * 0.5);
        b += src(uv - c13.xy * 0.5);
        b += src(uv + c13.xy * 0.5);
        b += src(uv - float2(c13.x, -c13.y) * 0.5);
        float d = dot(c - b * 0.25, float3(0.2126, 0.7152, 0.0722));
        d = sign(d) * max(abs(d) - c13.w, 0.0);
        d *= 1.0 - vm * c9.y;   // vm = 0 while vignette is off
        float hc = 0.1 * max(c13.z, 1.0);
        c = saturate(c + clamp(d * c13.z, -hc, hc));
    }

    // BASIC / TEMPERATURE / HUE / TONE run unconditionally: the cpu sends their identity
    // constants while a stage is off (gain 1, temp 0, identity matrix, tone 0, levels out 0..1,
    // contrast 1), which is exactly what botox's #if-off variant computes -- and branching them
    // pushed the shader past ps_3_0's 512 guaranteed slots.
    c *= c0.rgb * c1.z;
    c.r = saturate(c.r + c1.y * 0.15);
    c.g = saturate(c.g + c1.y * 0.05);
    c.b = saturate(c.b - c1.y * 0.15);

    [branch] if (LEVELS) {
        float3 li = saturate((c - c5.x) / (c5.y - c5.x));
        c = pow(li, c5.z);
    }

    float luma = dot(c, float3(0.2126, 0.7152, 0.0722));
    c = luma + (c - luma) * c1.x;

    float3 h;
    h.r = dot(c, c2.xyz);
    h.g = dot(c, c3.xyz);
    h.b = dot(c, c4.xyz);
    c = saturate(h);

    [branch] if (CURVES) {
        float3 cu = c * (255.0 / 256.0) + (0.5 / 256.0);
        c = float3(tex2Dlod(s1, float4(cu.r, 0.5, 0, 0)).r, tex2Dlod(s1, float4(cu.g, 0.5, 0, 0)).g,
                   tex2Dlod(s1, float4(cu.b, 0.5, 0, 0)).b);
    }

    c = saturate(c6.x + c * c6.y);

    float tl = dot(c, float3(0.2126, 0.7152, 0.0722));
    float hw = smoothstep(0.5, 1.0, tl);
    float sw = 1.0 - smoothstep(0.0, 0.5, tl);
    c = saturate(c + c6.z * hw + c6.w * sw);

    c = saturate((c - 0.5) * c0.w + 0.5);

    // posterize: n levels, optional ign dither of one step before rounding (round, not floor)
    [branch] if (POSTERIZE) {
        float pn = frac(52.9829189 * frac(dot(uv * c11.zw, float2(0.06711056, 0.00583715)))) - 0.5;
        float3 pq = floor(saturate(c) * c17.y + 0.5 + pn * c17.z) / c17.y;
        c = lerp(c, saturate(pq), c17.w);
    }

    // invert: rgb = 1 - c, luma = flip brightness keep hue (c + 1 - 2 * luma)
    [branch] if (INVERT) {
        float3 iv = c18.y > 0.5 ? c + 1.0 - 2.0 * dot(c, float3(0.2126, 0.7152, 0.0722)) : 1.0 - c;
        c = lerp(c, saturate(iv), c18.x);
    }

    if (VIGNETTE) c = lerp(c, c8.rgb, vm * c8.a);

    [branch] if (GRAIN) {
        float2 q = uv * 2.0 - 1.0;
        q.x *= c15.z;
        float2 r = float2(q.x * c14.x - q.y * c14.y, q.y * c14.x + q.x * c14.y);
        r.x /= c15.z;
        float n = pnoise((r * 0.5 + 0.5) * c15.xy, c14.z);
        float gl = saturate(abs(dot(c, float3(0.299, 0.587, 0.114)) - c12.x) * c12.y);
        gl *= gl;
        c = saturate(c + n * lerp(1.0 - gl * gl, 1.0, c12.z) * c14.w);
    }

    // deband dither last: the 8 bit back buffer rounds the smooth ramp straight back into bands
    if (DEBAND)
        c += c16.z * (dn + frac(52.9829189 * frac(dot(dq + c16.w * 1.7 + 37.0, float2(0.00583715, 0.06711056)))) - 1.0);

    return float4(c, 1);
}
