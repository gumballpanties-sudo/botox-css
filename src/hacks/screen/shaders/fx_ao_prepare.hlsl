// Ambient occlusion, pass one: view-space normals rebuilt from depth.
// SOURCE OF RECORD for fx_ao_prepare.h.
//
// Rebuild after ANY edit here:
//   fxc /T ps_3_0 /E main /Fh src/hacks/screen/shaders/fx_ao_prepare.h /Vn k_fxAoPrepareBytes src/hacks/screen/shaders/fx_ao_prepare.hlsl
//
// There is no g-buffer to read normals out of -- the engine's normals never leave the
// shaders that made them -- so depth is the only place an occlusion term can learn which
// way a surface faces.
//
//   c0 = zNear, zFar, tan(fovX/2), tan(fovY/2)
//   c1 = 1/width, 1/height, -, -
//
// THE ALPHA CHANNEL CARRIES THE DEPTH, and it is here purely so the composite's bilateral
// blur does not have to fetch the z buffer again beside every normal it reads. Eight taps x
// one fewer texture is eight fetches a pixel at full frame size.
//
// z/(z+k) and not z/zFar: a linear 8-bit encode over a 28000 unit far plane is a 110 unit
// step, and the blur's own depth tolerance is 5% of z -- eleven units at the near end. The
// hyperbolic one holds its RELATIVE precision instead, about 2% over the whole range the
// term is alive in, which sits comfortably inside that tolerance.

#define AO_ZK 256.0

sampler2D sDepth : register(s0);
float4 c0 : register(c0);
float4 c1 : register(c1);

// The z buffer holds a projected depth that is nearly all precision up close. This is the
// standard d3d unprojection back to view-space units (= source units in front of the eye).
float linear_z(float d) { return (c0.x * c0.y) / (c0.y - d * (c0.y - c0.x)); }

float3 view_pos(float2 uv, float z)
{
    return float3((uv.x * 2.0 - 1.0) * c0.z, (1.0 - uv.y * 2.0) * c0.w, 1.0) * z;
}

float3 at(float2 uv) { return view_pos(uv, linear_z(tex2Dlod(sDepth, float4(uv, 0, 0)).r)); }

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float3 p = at(uv);

    // The sky has no surface to rebuild, and the composite throws those pixels out before
    // it ever reads a normal -- the four neighbour fetches below would go straight in the
    // bin. A zero normal (0.5 packed) fails every bilateral weight, which is what a sky tap
    // did anyway. Alpha 1 decodes to an enormous depth, so a sky tap fails the blur's depth
    // weight as well as its normal one.
    if (p.z >= c0.y * 0.98)
        return float4(0.5, 0.5, 0.5, 1);

    float3 xr = at(uv + float2(c1.x, 0)) - p;
    float3 xl = p - at(uv - float2(c1.x, 0));
    float3 yd = at(uv + float2(0, c1.y)) - p;
    float3 yu = p - at(uv - float2(0, c1.y));

    // The neighbour with the SMALLER depth step is the one still on this surface. Taking
    // both would bend every normal along a silhouette towards whatever is behind it, and
    // those bent normals are what draw a dark halo around every railing and player.
    float3 dx = (abs(xr.z) < abs(xl.z)) ? xr : xl;
    float3 dy = (abs(yd.z) < abs(yu.z)) ? yd : yu;

    float3 n = normalize(cross(dy, dx));
    // and it has to face the eye whichever way the cross product came out
    n *= (dot(n, p) > 0.0) ? -1.0 : 1.0;

    return float4(n * 0.5 + 0.5, p.z * rcp(p.z + AO_ZK));
}
