// Bloom pass two: one direction of a 5-tap gaussian, run x then y, ping-ponged.
// SOURCE OF RECORD for fx_bloom_blur.h (the compiled bytecode).
//
// Rebuild after ANY edit here:
//   fxc /nologo /T ps_2_0 /E main /Fh src/hacks/screen/shaders/fx_bloom_blur.h /Vn k_fxBloomBlurBytes src/hacks/screen/shaders/fx_bloom_blur.hlsl
//
// c0 = (stepU, stepV, 0, 0)       one texel of the SOURCE, times the pass's widening
// c1 = (tintR, tintG, tintB, sat) 1,1,1,1 on every pass but the last
//
// The last y pass draws onto the frame itself with an additive blend, and it is the only
// one given real numbers in c1. A gaussian is linear, so scaling there is the same picture
// as scaling at the start -- except the buffers are 8 bit, and scaling at the start would
// clip every glow past 1.0 into flat white.

sampler2D sSrc : register(s0);
float4 c0 : register(c0);
float4 c1 : register(c1);

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    // Nine gaussian taps' worth of reach out of five fetches: the offsets sit between texels
    // so the hardware's own bilinear filter does the pairing.
    float2 s = c0.xy;
    float3 c  = tex2D(sSrc, uv).rgb * 0.2270270;
    c += tex2D(sSrc, uv + s * 1.3846154).rgb * 0.3162162;
    c += tex2D(sSrc, uv - s * 1.3846154).rgb * 0.3162162;
    c += tex2D(sSrc, uv + s * 3.2307692).rgb * 0.0702702;
    c += tex2D(sSrc, uv - s * 3.2307692).rgb * 0.0702702;

    float lum = dot(c, float3(0.2126, 0.7152, 0.0722));
    c = lerp(lum.xxx, c, c1.w) * c1.rgb;
    return float4(c, 1.0);
}
