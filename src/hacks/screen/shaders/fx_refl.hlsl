// Screen-space reflections, botox reflections.cpp 1:1: five ps_3_0 passes (normals, smooth x/y,
// trace with cubemap fallback, filter, composite). SOURCE OF RECORD for fx_refl.h.
//
// Rebuild after ANY edit here (from the project root):
//   bash src/hacks/screen/shaders/fx_refl_build.sh
//
// botox compiles k_common + one pass body at runtime; we ship one bytecode blob per PASS.
// Drawn through screenfx.cpp's vs_3_0 quad. Constants exactly as botox's, plus c7:
//   c0 = 2 tan(fovx/2), 2 tan(fovy/2), 1/trace w, 1/trace h
//   c1 = distance, intensity, angle falloff, blur
//   c2 = blur / smooth axis (1/w, 0) or (0, 1/h)
//   c3 = view right (w = have a cubemap), c4 = view down (w = crossfade), c5 = view forward
//   c6 = march: steps per run, growth per step, refines (qUINT 16 / 1.6 / 3 at "high")
//   c7 = zNear, zFar of this frame (TF2 only, see lin())
// s0 depth (point), s1 normals (point), s2 frame copy, s3 reflection, s4/s5 cubemaps (linear)

sampler2D s_depth : register(s0);
sampler2D s_normal : register(s1);
sampler2D s_frame : register(s2);
sampler2D s_reflection : register(s3);
samplerCUBE s_cube0 : register(s4);
samplerCUBE s_cube1 : register(s5);
float4 c0 : register(c0);
float4 c1 : register(c1);
float4 c2 : register(c2);
float4 c3 : register(c3);
float4 c4 : register(c4);
float4 c5 : register(c5);
float4 c6 : register(c6);
float4 c7 : register(c7);

// botox lin() is reshade's (far plane 1000) of CS:GO raw depth. TF2 raw depth moves with each
// map's zFar, so it is first re-projected through TF2's DEFAULT projection (VIEW_NEARZ 7,
// 16384*sqrt3 = 28377.9) -- the same remap fx_depthview.hlsl uses. The sky (raw 1) stays far.
float lin(float raw)
{
    if (raw >= 0.9999) return 1.0;
    float z = c7.x * c7.y / (c7.y - raw * (c7.y - c7.x));
    float r = saturate(28377.9 * (z - 7.0) / (z * 28370.9));
    return r / (1000.0 - r * 999.0);
}
float depth_at(float2 uv) { return lin(tex2Dlod(s_depth, float4(uv, 0, 0)).r); }
float3 view_pos(float2 uv, float l) { return float3((uv - 0.5) * c0.xy, 1.0) * l * 1000.0; }
float3 at(float2 uv) { return view_pos(uv, depth_at(uv)); }
float4 blur(float2 uv, float4 ck, float4 cm)
{
    if (c1.w <= 0.0) return ck;
    float kk = -2.0 / (c1.w * c1.w + 1e-3);
    float4 s = 0.0;
    float sw = 1e-3;
    float ext = floor(c1.w);
    [loop] for (float j = -ext; j <= ext; j++) {
        float2 t = uv + c2.xy * (2.0 * j - 0.5);
        float4 tk = tex2Dlod(s_reflection, float4(t, 0, 0));
        float4 tm = tex2Dlod(s_normal, float4(t, 0, 0));
        float dw = saturate(1.0 - abs(tm.w - cm.w) * 50.0);
        float nw = saturate((dot(tm.xyz, cm.xyz) - 0.8660254) * (1.0 / (1.0 - 0.8660254 + 1e-3)));
        float w = dw * nw * exp(j * j * kk) * tk.w;
        s += tk * w;
        sw += w;
    }
    s /= sw;
    return lerp(ck, s, saturate(sw * 2.0));
}

#if PASS == 0   // normals
float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float raw = tex2Dlod(s_depth, float4(uv, 0, 0)).r;
    float l = lin(raw);
    if (raw < 0.1) return float4(0, 0, 1, -1);   // the gun's own depth slice
    if (l >= c1.x) return float4(0, 0, 1, l);
    float3 p = view_pos(uv, l);
    float3 x1 = at(uv + float2(c0.z, 0)) - p;
    float3 x2 = p - at(uv - float2(c0.z, 0));
    float3 y1 = at(uv + float2(0, c0.w)) - p;
    float3 y2 = p - at(uv - float2(0, c0.w));
    x1 = (abs(x1.z) > abs(x2.z)) ? x2 : x1;
    y1 = (abs(y1.z) > abs(y2.z)) ? y2 : y1;
    return float4(normalize(cross(y1, x1)), l);
}
#elif PASS == 1   // smooth (run twice, c2 = x then y)
float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float4 c = tex2Dlod(s_normal, float4(uv, 0, 0));
    if (c.w < 0.0 || c.w >= c1.x) return c;
    float3 ns = c.xyz;
    float ws = 1.0;
    float tol = 1.0 / (c.w * 0.03 + 1e-6);
    [unroll] for (int j = 1; j <= 6; j++) {
        float2 off = c2.xy * (j * 1.2);
        float g = exp(-j * j * 0.15);
        float4 a = tex2Dlod(s_normal, float4(uv + off, 0, 0));
        float4 b = tex2Dlod(s_normal, float4(uv - off, 0, 0));
        float wa = g * saturate(1.0 - abs(a.w - c.w) * tol) * saturate(dot(a.xyz, c.xyz) * 4.0 - 2.8);
        float wb = g * saturate(1.0 - abs(b.w - c.w) * tol) * saturate(dot(b.xyz, c.xyz) * 4.0 - 2.8);
        ns += a.xyz * wa + b.xyz * wb;
        ws += wa + wb;
    }
    return float4(normalize(ns / ws), c.w);
}
#elif PASS == 2   // trace
float bayer(float2 v)
{
    float2 a = fmod(floor(v * 0.25), 2.0);
    float2 b = fmod(floor(v * 0.5), 2.0);
    float2 c = fmod(floor(v), 2.0);
    float3 x = float3(a.x, b.x, c.x) * 2.0;
    float3 r = lerp(x, 3.0 - x, float3(a.y, b.y, c.y)) / 3.0;
    return dot(r, float3(4.0, 16.0, 64.0)) / 84.0;
}
float4 main(float2 uv : TEXCOORD0, float2 vpos : VPOS) : COLOR0
{
    float4 nd = tex2Dlod(s_normal, float4(uv, 0, 0));
    if (nd.w < 0.0 || nd.w >= c1.x) return 0.0;
    float jitter = bayer(vpos) - 0.5;
    float3 e = normalize(view_pos(uv, nd.w));
    float3 d = reflect(e, nd.xyz);
    float st = (0.2 + 0.0125 * jitter) * sqrt(nd.w) / (1e-3 + saturate(1.0 - dot(d, e)));
    float3 rp = view_pos(uv, nd.w) + d * st;
    float2 t = uv;
    // c6 x steps per run, y growth per step, z refines. 16 / 1.6 / 3 = qUINT
    float j = 0.0;
    float k = 0.0;
    [loop] for (int it = 0; it < 80; it++) {
        if (j >= c6.x) break;
        j++;
        t = rp.xy / (rp.z * c0.xy) + 0.5;
        float ez = depth_at(t) * 1000.0 - rp.z;
        if (ez < 0.0 && ez > -2.5 * st) {
            j = 0.0;
            if (k < c6.z) {
                st /= c6.y;
                rp -= d * st;
                st *= c6.y / 16.0;
            } else {
                j += c6.x;
            }
            k++;
        }
        rp += d * st;
        st *= c6.y;
        float2 edge = saturate(t - t * t);
        if (edge.x <= 0.0 || edge.y <= 0.0) j += c6.x;
    }
    float nv = saturate(1.0 + dot(e, nd.xyz));
    // schlick (exponent 5) is ~0.04 looking straight down: c1.z = falloff, 0 = same strength at every angle
    float schlick = lerp(0.04, 1.0, pow(nv, 5.0));
    // hits in the outer 1/8 of the screen fade out instead of cutting hard
    float2 border = min(t, 1.0 - t);
    float hit = k > 0 ? saturate(min(border.x, border.y) * 8.0) : 0.0;
    float3 ssr = tex2Dlod(s_frame, float4(t, 0, 0)).rgb;
    float3 col;
    float a;
    if (c3.w > 0.5) {
        // map cubemap under the screen trace: misses, off screen and behind the camera still reflect
        float3 w = d.x * c3.xyz + d.y * c4.xyz + d.z * c5.xyz;
        float3 cube = lerp(texCUBElod(s_cube1, float4(w, 0)).rgb, texCUBElod(s_cube0, float4(w, 0)).rgb, c4.w);
        col = lerp(cube, ssr, hit);
        a = saturate(c1.y * lerp(1.0, schlick * nv, c1.z));
    } else {
        col = ssr;
        a = saturate(c1.y * lerp(1.0, schlick * saturate(dot(e, d)) * nv, c1.z)) * hit;
    }
    // distance fade on alpha only: the texture then has no hard cutoff for the edge aa
    return float4(col * a, a * saturate(1.0 - nd.w / c1.x));
}
#elif PASS == 3   // filter (vertical)
float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float4 ck = tex2Dlod(s_reflection, float4(uv, 0, 0));
    float4 cm = tex2Dlod(s_normal, float4(uv, 0, 0));
    if (cm.w < 0.0 || cm.w >= c1.x) return ck;
    return blur(uv, ck, cm);
}
#else   // composite (horizontal blur + edge aa), blended over the frame
float m(float4 v) { return dot(v, float4(0.299, 0.587, 0.114, 0.5)); }
float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float4 ck = tex2Dlod(s_reflection, float4(uv, 0, 0));
    float4 cm = tex2Dlod(s_normal, float4(uv, 0, 0));
    float4 r = ck;
    if (cm.w >= 0.0 && cm.w < c1.x) r = blur(uv, ck, cm);
    float2 h = c0.zw * 0.5;
    float4 a = tex2Dlod(s_reflection, float4(uv + float2(-h.x, -h.y), 0, 0));
    float4 b = tex2Dlod(s_reflection, float4(uv + float2(h.x, -h.y), 0, 0));
    float4 c = tex2Dlod(s_reflection, float4(uv + float2(-h.x, h.y), 0, 0));
    float4 d = tex2Dlod(s_reflection, float4(uv + float2(h.x, h.y), 0, 0));
    float ma = m(a), mb = m(b), mc = m(c), md = m(d);
    float hi = max(max(ma, mb), max(mc, md));
    float lo = min(min(ma, mb), min(mc, md));
    float e = saturate((hi - lo) / (hi + 0.05) * 2.0 - 0.5);
    return lerp(r, (a + b + c + d) * 0.25, e);
}
#endif
