// Depth of field: the smoothing pass. SOURCE OF RECORD for fx_dof_smooth.h.
//
// Rebuild after ANY edit here:
//   fxc /T ps_3_0 /E main /Fh src/hacks/screen/shaders/fx_dof_smooth.h /Vn k_fxDofSmoothBytes src/hacks/screen/shaders/fx_dof_smooth.hlsl
//
// The gather leaves a residual grain because its taps are a spiral and not a grid, so a
// separable gaussian runs after it -- but WHICH pixels it may pull in is the whole
// difference between a blur that dissolves a silhouette and one that draws it sharper than
// anything around it.
//
// This is qUINT ADoF's rule (PS_DoF_Gauss1/2): every tap is weighted by HOW MUCH BLURRIER IT
// IS than this pixel (its "bleed fix"), the centre tap is on exactly those same terms, and
// the result is lerped by the total weight -- a blurred pixel among blurrier ones is
// replaced by them, a sharp one keeps itself because nothing around it earns any weight. The
// x4 buildup is ADoF's too: the gaussian is at full width by a coc of a quarter, so the
// smoothing arrives WITH the blur instead of trailing it.
//
//   c1 = half the sharp band, max blur radius in pixels, 1/width, 1/height
//   c4 = direction x, direction y, bleed factor (ADoF's 0.25), smoothening amount

sampler2D sSrc : register(s0);
float4 c1 : register(c1);
float4 c4 : register(c4);

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float4 mid = tex2Dlod(sSrc, float4(uv, 0, 0));
    float  sgn = mid.a * 2.0 - 1.0;
    float  cs  = saturate(abs(sgn) * 4.0);

    // THE NEAR PLANE IS EXEMPT FROM THE WHOLE RULE BELOW, and this was a hard edge. The
    // weights are ADoF's "a neighbour must be blurrier than me", which is right for the far
    // plane -- but on a near plane pixel it scores every background neighbour at ZERO, so
    // this pass mixes the foreground with itself and draws its outline back on, in the last
    // draw of the chain, after the near pass just spent a full disc softening it.
    float nearpx = (sgn < 0.0) ? 1.0 : 0.0;

    float steps = floor(cs * c4.w);

    // in focus stays untouched -- with no steps there is nothing this pass could add
    if (steps < 1.0)
        return mid;

    // sigma follows the step count, so the curve is the same shape at every blur size
    float  expc = -2.0 * rcp(steps * steps + 1e-3);
    float2 axis = c4.xy * c1.zw;

    // THE CENTRE IS JUST ANOTHER TAP. Weighting it 1 is what pins every pixel to its own
    // colour and leaves the outline standing; here it earns its place by its own coc.
    float  w0   = max(nearpx, saturate(cs - cs * c4.z));
    float3 sum  = mid.rgb * w0;
    float  wsum = w0 + 1e-3;

    // The ceiling is fixed and the real bound is the break, which is how the gather's loop is
    // built too -- ps_3_0 takes its loop count from a constant register, not per pixel.
    [loop] for (int i = 1; i <= 8; i++)
    {
        if ((float)i > steps) break;

        float fi = (float)i;
        float w  = exp(fi * fi * expc);

        // half a texel short of a whole one, so every tap lands between two
        float4 a = tex2Dlod(sSrc, float4(uv + axis * (2.0 * fi - 0.5), 0, 0));
        float4 b = tex2Dlod(sSrc, float4(uv - axis * (2.0 * fi + 0.5), 0, 0));

        float wa = w * max(nearpx, saturate(saturate(abs(a.a * 2.0 - 1.0) * 4.0) - cs * c4.z));
        float wb = w * max(nearpx, saturate(saturate(abs(b.a * 2.0 - 1.0) * 4.0) - cs * c4.z));

        sum  += a.rgb * wa + b.rgb * wb;
        wsum += wa + wb;
    }

    sum /= wsum;

    // lerp and not a straight divide: where nothing around this pixel is blurred the weights
    // come to nothing and it keeps itself, which is what holds the sharp side of an edge
    // together. Alpha carries the circle of confusion through to the vertical pass.
    return float4(lerp(mid.rgb, sum, saturate(wsum)), mid.a);
}
