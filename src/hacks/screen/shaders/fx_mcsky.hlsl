// Minecraft 1.21.11 overworld sky + fancy clouds, one ps_3_0 pass drawn right behind the
// engine's own 2D sky (R_DrawSkyBox hook), quad at the depth clear value so only sky pixels
// pass the z test. SOURCE OF RECORD for fx_mcsky.h.
//
// Rebuild after ANY edit here:
//   fxc /nologo /T ps_3_0 /E main /Fh src/hacks/screen/shaders/fx_mcsky.h /Vn k_fxMcSkyBytes src/hacks/screen/shaders/fx_mcsky.hlsl
//
// Sky = botox n_mc_sky::shade (mc_sky.h) evaluated per pixel instead of baked into skybox VTFs:
// same sky disc, fog colour, sun quad and sun.png texels, so the result is the bake without the
// 1024px face texels. Clouds = botox n_mc_clouds::k_shader (mc_clouds_math.h), unchanged.
//
//   TEXCOORD0 = screen uv of the shared screenfx quad (fx_quad_vs)
//   s0 = cloud cells 256x256 (a = cloud), point + wrap      s1 = sun.png 32x32, point + clamp
//   c0 = cloud offset x, z (mc blocks), gamma light scale (ps c30.w), unused
//   c1 = sky colour     c2 = fog colour     c3 = sin(sun angle), cos(sun angle)
//   c4..c7 = world rays through the quad corners tl tr bl br (CornerRays, from vs c8 cViewProj)

sampler2D sCells : register(s0);
sampler2D sSun   : register(s1);
float4 c0 : register(c0);
float4 c1 : register(c1);
float4 c2 : register(c2);
float4 c3 : register(c3);
float4 c4 : register(c4);
float4 c5 : register(c5);
float4 c6 : register(c6);
float4 c7 : register(c7);

// botox mc_clouds_math.h k_shader: first 12x4x12 cell face the ray meets. xyz = face shade, w = alpha
float4 clouds(float3 d)
{
    if (d.y <= 0.0) return float4(0, 0, 0, 0);
    float h = 192.33 - 65.62;
    float t = h / d.y;
    float top = (h + 4.0) / d.y;
    if (t >= 2048.0) return float4(0, 0, 0, 0);
    float2 p = c0.xy + d.xz * t;
    float2 c = floor(p / 12.0);
    float2 home = floor(c0.xy / 12.0);
    float2 st = float2(d.x > 0.0 ? 1.0 : -1.0, d.z > 0.0 ? 1.0 : -1.0);
    float2 dt = float2(d.x != 0.0 ? 12.0 / abs(d.x) : 1e30, d.z != 0.0 ? 12.0 / abs(d.z) : 1e30);
    float2 nx = float2(d.x != 0.0 ? t + ((c.x + (st.x > 0.0 ? 1.0 : 0.0)) * 12.0 - p.x) / d.x : 1e30,
                       d.z != 0.0 ? t + ((c.y + (st.y > 0.0 ? 1.0 : 0.0)) * 12.0 - p.y) / d.z : 1e30);
    float face = 0.7;
    bool hit = false;
    [loop] for (int i = 0; i < 32; i++)
    {
        if (t >= top || t >= 2048.0) break;
        float2 r = c - home;
        if (dot(r, r) <= 29241.0 && tex2Dlod(sCells, float4((c + 0.5) / 256.0, 0, 0)).a > 0.5) { hit = true; break; }
        if (nx.x < nx.y) { t = nx.x; nx.x += dt.x; c.x += st.x; face = 0.9; }
        else             { t = nx.y; nx.y += dt.y; c.y += st.y; face = 0.8; }
    }
    if (!hit) return float4(0, 0, 0, 0);
    return float4(face, face, face, 0.8 * (1.0 - min(t / 2048.0, 1.0)));
}

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float3 ray = lerp(lerp(c4.xyz, c5.xyz, uv.x), lerp(c6.xyz, c7.xyz, uv.x), uv.y);
    float3 n = normalize(ray);
    // source ( x fwd, y left, z up ) -> minecraft ( x east, y up, z south )
    float3 d = float3(n.x, n.z, -n.y);

    // n_mc_sky::shade -- clear = fog colour, sky disc fan (octagon r 512 at y 16) fogged per vertex
    float3 col = c2.rgb;
    if (d.y > 0.0)
    {
        const float k_pi = 3.14159265, k_wedge = 3.14159265 / 4.0;
        float px = d.x * 16.0 / d.y, pz = d.z * 16.0 / d.y;
        float a = atan2(pz, px);
        float mid = floor((a + k_pi) / k_wedge) * k_wedge - k_pi + k_wedge * 0.5;
        float ring = (px * cos(mid) + pz * sin(mid)) / (512.0 * cos(k_wedge * 0.5));
        if (ring <= 1.0)
        {
            float spherical   = 16.0 + ring * (sqrt(512.0 * 512.0 + 16.0 * 16.0) - 16.0);
            float cylindrical = 16.0 + ring * (512.0 - 16.0);
            float f = max(min(spherical / 192.0, 1.0), cylindrical >= 192.0 ? 1.0 : 0.0);
            col = lerp(c1.rgb, c2.rgb, f);
        }
    }

    // sun: quad +-30 at 100 up, posed YP( -90 ) * XP( angle ), additive, nearest texel
    float sinA = c3.x, cosA = c3.y;
    float facing = -d.x * sinA + d.y * cosA;
    if (facing > 0.0)
    {
        float t = 100.0 / facing;
        float su = (d.z * t) / 30.0;
        float sv = (-d.x * cosA - d.y * sinA) * t / 30.0;
        if (abs(su) < 1.0 && abs(sv) < 1.0)
        {
            float4 s = tex2Dlod(sSun, float4((su + 1.0) * 0.5, (sv + 1.0) * 0.5, 0, 0));
            col = min(1.0, col + s.rgb * s.a);
        }
    }

    // clouds over the sky, the blend botox's SRCALPHA cloud pass did over its baked faces
    float4 cl = clouds(d);
    col = lerp(col, cl.rgb, cl.a);

    return float4(col * c0.z, 1.0);
}
