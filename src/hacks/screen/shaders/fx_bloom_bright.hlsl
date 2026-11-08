// Bloom pass one: the bright part of the frame, boxed down to the bloom buffer's size.
// SOURCE OF RECORD for fx_bloom_bright.h (the compiled bytecode).
//
// Rebuild after ANY edit here:
//   fxc /nologo /T ps_2_0 /E main /Fh src/hacks/screen/shaders/fx_bloom_bright.h /Vn k_fxBloomBrightBytes src/hacks/screen/shaders/fx_bloom_bright.hlsl
//
// ps_2_0, not 3_0, on purpose: the quad is drawn through fixed-function vertex processing
// (XYZRHW), and ps_3_0 is the one target that will not pair with it.
//
// c0 = (1/sceneWidth, 1/sceneHeight, threshold, unused)

sampler2D sScene : register(s0);
float4 c0 : register(c0);

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    // Four bilinear taps, each one sitting on a texel corner of the full-res copy, so each
    // averages a 2x2 and the four together are a clean 4x4 box. A single tap instead would
    // throw away three quarters of the pixels and make the glow crawl when the camera moves.
    float2 o = c0.xy;
    float3 c  = tex2D(sScene, uv + float2(-o.x, -o.y)).rgb;
    c += tex2D(sScene, uv + float2( o.x, -o.y)).rgb;
    c += tex2D(sScene, uv + float2(-o.x,  o.y)).rgb;
    c += tex2D(sScene, uv + float2( o.x,  o.y)).rgb;
    c *= 0.25;

    // Soft knee. Scaling the colour by how far its LUMA is over the threshold keeps the hue
    // of what is glowing -- a per-channel subtract would drag every highlight towards white.
    float lum = dot(c, float3(0.2126, 0.7152, 0.0722));
    float k   = max(lum - c0.z, 0.0) / max(lum, 0.0001);
    return float4(c * k, 1.0);
}
