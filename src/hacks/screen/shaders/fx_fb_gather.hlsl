// Frame blending, pass five: THE RECONSTRUCTION. SOURCE OF RECORD for fx_fb_gather.h.
//
// Rebuild after ANY edit here:
//   fxc /T ps_3_0 /E main /Fh src/hacks/screen/shaders/fx_fb_gather.h /Vn k_fxFbGatherBytes src/hacks/screen/shaders/fx_fb_gather.hlsl
//
// McGuire et al. 2012 "A Reconstruction Filter for Plausible Motion Blur", with Guertin et
// al. 2014's feature-aware weighting. It is a RESAMPLE, not a smear: every tap is asked three
// questions and the answers multiply.
//
//   depth      a tap in front of this pixel can only paint over it, a tap behind it can only
//              be revealed by it. This is the whole reason the gun does not drag across the
//              wall and the wall does not bleed into the gun, with no depth-range test.
//   spread     did the tap's own streak actually reach this far during the shutter? A still
//              tap answers no at every distance, which keeps anything static out of the blur.
//   direction  how parallel the tap's motion is to the line being walked. A tap sliding across
//              the scan is not part of this streak.
//
// Two accumulators, the paper's: foreground taps are added on top, background taps fill in what
// the moving thing uncovered, and whatever weight is left at the end is filled with the
// background rather than the centre -- which is what stops the hard edge around a fast object.
//
// Blurring is done in ~linear light with the highlights pushed out by an invertible reinhard:
// a tracer smeared in gamma space goes grey, in linear it stays a streak of light.
//
// THE FENCE. Pass one parks every gun pixel under c8.y, so:
//   world run   a gun pixel keeps its own colour outright -- the world reaching INTO the gun
//               is the smear that drags a wall across your weapon.
//   gun run     a gun pixel takes gun taps only; a world pixel takes whatever reaches it,
//               which in that run is the gun smearing onto the wall behind it. That is what an
//               exposure does, and it is the half people actually see.
//
//   c2 = tap cap, max streak, tile size, frame noise
//   c6 = width, height, 1/width, 1/height
//   c8 = depth scale, the gun's fence, player count, gun run
//   c14 = blur gamma, 1 / gamma, -, -   (botox "blur gamma", f0e average_bright; 2 = c*c / sqrt)

sampler2D sColor : register(s0);
sampler2D sInfo  : register(s1);
sampler2D sTile  : register(s2);

float4 c2 : register(c2);
float4 c6 : register(c6);
float4 c8 : register(c8);
float4 c14 : register(c14);

float3 in_color(float3 c)  { c = pow(saturate(c), c14.x); return c / (1.5 - max(c.r, max(c.g, c.b))); }
float3 out_color(float3 c) { return pow(saturate((1.5 * c) / (1.0 + max(c.r, max(c.g, c.b)))), c14.y); }

// interleaved gradient noise, animated on the frame: the taps land between each other every frame
float ign(float2 p)
{
    return frac(52.9829189 * frac(dot(p + 5.588238 * c2.w, float2(0.06711056, 0.00583715))));
}

// the tile is a block and its edge would be a visible seam, so every pixel reads its
// neighbourhood max from a point jittered up to an eighth of a tile away -- never diagonally,
// which would soften the corners into the halo pass four's diagonal rule just removed
float2 tile_offs(float2 p)
{
    float2 s    = floor(fmod(p, 2.0)) * 2.0 - 1.0;
    float2 axis = (s.x * s.y < 0.0) ? float2(1, 0) : float2(0, 1);
    return (ign(p + 17.0) - 0.5) * 0.25 * c2.z * c6.zw * axis;
}

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float2 vpos = uv * c6.xy;

    float3 cen  = in_color(tex2Dlod(sColor, float4(uv, 0, 0)).rgb);
    float4 info = tex2Dlod(sInfo, float4(uv, 0, 0));

    float cen_len = info.x;
    float cen_z   = info.y;
    float cen_gun = step(cen_z, c8.y);

    // the world run never touches the gun: it is bolted to the camera and nothing in that run
    // knows what its animation did
    if (cen_gun > 0.5 && c8.w < 0.5) return float4(out_color(cen), 1.0);

    float2 tile     = tex2Dlod(sTile, float4(uv + tile_offs(vpos), 0, 0)).xy;
    float  tile_len = length(tile);

    // nothing within reach of this pixel moved at all
    if (tile_len < 1.0) return float4(out_color(cen), 1.0);

    float2 tile_dir = tile / max(1e-15, tile_len);

    // THE COUNT IS THE SPACING, AND THE TWO WALKS SPLIT IT. The loop alternates the tile's
    // direction with the pixel's own, so where those two disagree each walk only gets every
    // OTHER sample and its taps stand twice as far apart -- which is a thing coming back as
    // separate copies with their own edges. So the count doubles exactly where they disagree.
    float agree  = (cen_len < 1.0) ? 1.0 : abs(dot(info.zw, tile_dir));
    float per_px = lerp(2.0, 1.0, saturate((agree - 0.85) * 6.6667));

    float half_n = clamp(ceil(tile_len * per_px), 3.0, max(3.0, c2.x));

    // odd, so the tile direction gets one sample more than the pixel's own
    if (fmod(half_n, 2.0) < 0.5) half_n += 1.0;

    float4 tile_main = float4(tile_dir, tile_len, cen_len < 1.0 ? 1.0 : abs(dot(info.zw, tile_dir)));

    // a still pixel next to a moving one has no direction of its own: walk the tile's twice
    float4 cen_main = cen_len < 1.0 ? tile_main : float4(info.zw, cen_len, 1.0);

    // negated on the way back so the two halves do not leave a gap down the middle
    float2 noise = (ign(vpos) - 0.5) * float2(1.0, -1.0);

    float inv     = 1.0 / half_n;
    float to_step = 1.0 / (inv * tile_len);

    float4 bg = 0.0;
    float4 fg = 0.0;

    int n = (int)half_n;

    [loop] for (int j = 0; j < n; j++)
    {
        float4 m = (fmod((float)j, 2.0) < 0.5) ? tile_main : cen_main;

        float2 st   = (float)j + 0.5 + noise;
        float2 offs = inv * (m.xy * m.z) * c6.zw;

        float2 t0 = uv - st.x * offs;
        float2 t1 = uv + st.y * offs;

        float4 i0 = tex2Dlod(sInfo, float4(t0, 0, 0));
        float4 i1 = tex2Dlod(sInfo, float4(t1, 0, 0));

        // RELATIVE depth, not absolute: a hand's width at the muzzle and a hand's width across
        // the map are the same fraction apart, and an absolute test would call the whole
        // distant half of the map coplanar. x = behind me, y = in front of me.
        float2 d0 = saturate(0.5 + c8.x * float2(1.0, -1.0) * (i0.y - cen_z) / max(cen_z, 1.0));
        float2 d1 = saturate(0.5 + c8.x * float2(1.0, -1.0) * (i1.y - cen_z) / max(cen_z, 1.0));

        float2 s0w = saturate(float2(cen_len, i0.x) * to_step - max(0.0, st.x - 1.0));
        float2 s1w = saturate(float2(cen_len, i1.x) * to_step - max(0.0, st.y - 1.0));

        // THE DIRECTION TEST HAS NO FLOOR, and a floor is the haze: letting every tap on the
        // line in at a fraction of its weight however it is moving pours off-axis colour over
        // every blurred pixel. It is a RAMP and not a cut, so a bone arc turning a few degrees
        // across one shutter (dot 0.99) never comes near it.
        float2 w0 = d0 * s0w * float2(m.w, smoothstep(0.10, 0.45, abs(dot(i0.zw, m.xy))));
        float2 w1 = d1 * s1w * float2(m.w, smoothstep(0.10, 0.45, abs(dot(i1.zw, m.xy))));

        // AND A GUN PIXEL ONLY EVER TAKES GUN. The depth test alone leaves the world a way in
        // (a tap behind the gun still fills whatever weight the streak did not), so the fence
        // is checked outright. The other way round is left alone.
        w0 *= max(1.0 - cen_gun, step(i0.y, c8.y));
        w1 *= max(1.0 - cen_gun, step(i1.y, c8.y));

        float3 c0v = in_color(tex2Dlod(sColor, float4(t0, 0, 0)).rgb);
        float3 c1v = in_color(tex2Dlod(sColor, float4(t1, 0, 0)).rgb);

        // mirror the pair when the far one is also the slower one, so the nearer streak wins
        // the overlap
        bool2 mir  = bool2(i0.y > i1.y, i1.x > i0.x);
        float2 keep = w0;

        w0 = (mir.x && mir.y) ? w1 : keep;
        w1 = (mir.x || mir.y) ? w1 : w0;

        bg += float4(c0v, 1.0) * w0.x;
        fg += float4(c0v, 1.0) * w0.y;
        bg += float4(c1v, 1.0) * w1.x;
        fg += float4(c1v, 1.0) * w1.y;
    }

    // divided by the SAMPLE COUNT and not the weight -- a normalised weight stretches whatever
    // few taps qualified over the whole pixel, and that is where the blotches come from. What
    // is missing is filled with the background instead, at the bottom.
    float total = half_n * 2.0 + 1.0;

    // THE FILL IS THE NEIGHBOURHOOD ALONE, so it is read before the centre goes in. When no tap
    // qualified at all (a still pixel a fast thing merely passed near) there is no background to
    // fill with and the answer is this pixel, untouched: the sharp half of a resample.
    float3 fill = (bg.w > 1e-4) ? bg.rgb / bg.w : cen;

    // AND THE CENTRE IS ONE WHOLE SAMPLE OF THE 2n+1, which is what it is in a resample: the
    // instant the frame was drawn is one of the instants the shutter was open for.
    bg += float4(cen, 1.0);

    float4 acc = (bg + fg) / total;
    acc.rgb += saturate(1.0 - acc.w) * fill;

    return float4(out_color(acc.rgb), 1.0);
}
