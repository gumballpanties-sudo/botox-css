// Frame blending, pass six: THE TRAIL. SOURCE OF RECORD for fx_fb_trail.h.
//
// Rebuild after ANY edit here:
//   fxc /T ps_3_0 /E main /Fh src/hacks/screen/shaders/fx_fb_trail.h /Vn k_fxFbTrailBytes src/hacks/screen/shaders/fx_fb_trail.hlsl
//
// botox true_motion_blur.cpp k_accumulate_body, on our two runs:
//   world run   "player trail". Last frame's history reprojected through the camera: a still
//               wall lands on itself and stays as it is, whatever moved on its own (a player,
//               a rocket) leaves an exponential ghost behind it.
//   gun run     "animation trail (old)". The gun is bolted to the lens, so its history is read
//               in place; alpha carries how much gun the history pixel holds, so the ghost fades
//               off the world behind it instead of staining it.
//
//   s0 = this frame (after the gather), s1 = the history, s2 = depth (INTZ, point)
//   c0  = zNear, zFar, tan(fovX/2), tan(fovY/2)
//   c1  = prev tanX, prev tanY, viewmodel raw-depth cutoff, -
//   c3..c5 = the camera reprojection, the eye's own step in .w
//   c8  = -, -, -, gun run
//   c14 = -, -, history blend (0 = history empty), -

sampler2D sCur   : register(s0);
sampler2D sHist  : register(s1);
sampler2D sDepth : register(s2);

float4 c0 : register(c0);
float4 c1 : register(c1);
float4 c3 : register(c3);
float4 c4 : register(c4);
float4 c5 : register(c5);
float4 c8 : register(c8);
float4 c14 : register(c14);

float linear_z(float d) { return (c0.x * c0.y) / (c0.y - d * (c0.y - c0.x)); }

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float3 cur = tex2Dlod(sCur, float4(uv, 0, 0)).rgb;
    float  raw = tex2Dlod(sDepth, float4(uv, 0, 0)).r;
    float  b   = c14.z;

    if (c8.w < 0.5)
    {
        // the gun is not in this run's frame; the sky reprojects to nowhere useful either way
        if (b <= 0.0 || raw < c1.z) return float4(cur, 0.0);

        float  z = linear_z(raw);
        float3 p = float3((uv.x * 2.0 - 1.0) * c0.z, (1.0 - uv.y * 2.0) * c0.w, 1.0) * z;
        float3 q = float3(dot(c3.xyz, p) + c3.w, dot(c4.xyz, p) + c4.w, dot(c5.xyz, p) + c5.w);

        // behind the eye last frame: nothing to have come from
        if (q.z < c0.x) return float4(cur, 0.0);

        float2 g = float2((q.x / (q.z * c1.x)) * 0.5 + 0.5, 0.5 - (q.y / (q.z * c1.y)) * 0.5);
        if (g.x < 0.0 || g.x > 1.0 || g.y < 0.0 || g.y > 1.0) return float4(cur, 0.0);

        float3 h = tex2Dlod(sHist, float4(g, 0, 0)).rgb;
        return float4(lerp(cur, h, b), 0.0);
    }

    // ---- the gun run ----------------------------------------------------------------------
    float4 self = tex2Dlod(sHist, float4(uv, 0, 0));
    float  gun  = saturate(self.a);

    // a gun pixel keeps the full mask; off the gun the mask decays with the ghost itself
    if (raw < c1.z) return float4(lerp(cur, self.rgb, b * gun), 1.0);
    return float4(lerp(cur, self.rgb, b * gun * gun), gun * b);
}
