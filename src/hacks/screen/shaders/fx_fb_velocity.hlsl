// Frame blending, pass one: THE VELOCITY BUFFER. SOURCE OF RECORD for fx_fb_velocity.h.
//
// Rebuild after ANY edit here:
//   fxc /T ps_3_0 /E main /Fh src/hacks/screen/shaders/fx_fb_velocity.h /Vn k_fxFbVelocityBytes src/hacks/screen/shaders/fx_fb_velocity.hlsl
//
// x = the streak in pixels, y = view depth, zw = the unit direction. Everything downstream
// reads this and nothing re-derives it.
//
// THE FRAME IS BLENDED IN TWO RUNS, and c8.w says which this is:
//   world run (0)  the game's own post slot. The viewmodel is not drawn yet
//                  (viewrender.cpp:2160 comes after), so every pixel here is world: the depth
//                  is unprojected, put back through LAST frame's camera, and the difference is
//                  where that pixel slid to. Players get their own reprojection, see below.
//   gun run (1)    ViewDrawFade, after DrawViewModels. The world half is already finished and
//                  is left alone (velocity zero, real depth so the gun can smear OVER it); the
//                  gun's pixels -- the raw depth range 0..0.1 the engine squeezes it into --
//                  get their velocity from the BONES.
//
// THE GUN IS PARKED under half the world's near plane either way, so one compare tells the two
// apart with no depth-range test anywhere downstream.
//
//   c0  = zNear, zFar, tan(fovX/2), tan(fovY/2)
//   c1  = prev tanX, prev tanY, viewmodel raw-depth cutoff, exposure (frames of travel)
//   c2  = tap cap, max streak, tile size, frame noise
//   c3..c5 = the reprojection: prev_view^T * cur_view, the eye's own step in .w
//   c6  = width, height, 1/width, 1/height
//   c8  = depth scale, the gun's fence, player count, gun run
//   c9..c11 = view space -> world space (the camera's axes as columns, the eye in .w)
//   c12 = viewmodel zNear, zFar, tanX, tanY -- its own projection, not the world's
//   c13 = bone count, viewmodel amount, -, -
//   c16.. = players, five registers each: mins, maxs, three rows of his own reprojection
//   c66.. = bones, four registers each: view-space centre, three rows of its transform

sampler2D sDepth : register(s0);

float4 c0 : register(c0);
float4 c1 : register(c1);
float4 c2 : register(c2);
float4 c3 : register(c3);
float4 c4 : register(c4);
float4 c5 : register(c5);
float4 c6 : register(c6);
float4 c8 : register(c8);
float4 c12 : register(c12);
float4 c13 : register(c13);

float4 viewToWorld[3] : register(c9);
float4 player[50] : register(c16);
float4 bone[64] : register(c66);

float linear_z(float d) { return (c0.x * c0.y) / (c0.y - d * (c0.y - c0.x)); }

float3 view_pos(float2 uv, float z)
{
    return float3((uv.x * 2.0 - 1.0) * c0.z, (1.0 - uv.y * 2.0) * c0.w, 1.0) * z;
}

// where this pixel sat on screen last frame: one 3x4 multiply and a divide by the PREVIOUS
// frame's half angles, because a zoom between two frames moves pixels as surely as a turn does
float2 prev_uv(float3 q)
{
    return float2((q.x / (q.z * c1.x)) * 0.5 + 0.5, 0.5 - (q.y / (q.z * c1.y)) * 0.5);
}

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float raw = tex2Dlod(sDepth, float4(uv, 0, 0)).r;
    float2 vel = 0.0;
    float  z   = c8.y * 0.5;

    if (raw >= c1.z)
    {
        // ---- the world -------------------------------------------------------------
        z = linear_z(raw);

        if (c8.w < 0.5)
        {
            float3 p = view_pos(uv, z);

            // THE ROWS ARE A CHOICE, not a constant. c3..c5 say where a pixel of the STILL
            // world was last frame, which is right for a wall and wrong for a man -- he was
            // somewhere else on his own account. So the pixel is put back into the world (the
            // depth buffer already knows where it is) and tested against the players' boxes,
            // and a pixel inside one is reprojected through THAT player's transform: his walk
            // and the camera's, in one 3x4.
            //
            // The boxes go up FARTHEST FIRST, so where two overlap the nearer man wins -- which
            // is whose pixels these are. An empty slot is parked inside out and never matches.
            float3 r0 = c3.xyz, r1 = c4.xyz, r2 = c5.xyz;
            float3 tw = float3(c3.w, c4.w, c5.w);

            if (c8.z > 0.5)
            {
                float3 w = float3(dot(viewToWorld[0].xyz, p) + viewToWorld[0].w,
                                  dot(viewToWorld[1].xyz, p) + viewToWorld[1].w,
                                  dot(viewToWorld[2].xyz, p) + viewToWorld[2].w);

                // unrolled: ps_3_0 cannot index a constant register, so a branch per player
                // would cost more than the four compares it skipped
                [unroll] for (int k = 0; k < 10; k++)
                {
                    float3 hit = step(player[k * 5].xyz, w) * step(w, player[k * 5 + 1].xyz);
                    float  ins = hit.x * hit.y * hit.z;

                    r0 = lerp(r0, player[k * 5 + 2].xyz, ins);
                    r1 = lerp(r1, player[k * 5 + 3].xyz, ins);
                    r2 = lerp(r2, player[k * 5 + 4].xyz, ins);
                    tw = lerp(tw, float3(player[k * 5 + 2].w, player[k * 5 + 3].w, player[k * 5 + 4].w), ins);
                }
            }

            float3 q = float3(dot(r0, p) + tw.x, dot(r1, p) + tw.y, dot(r2, p) + tw.z);

            // behind the eye last frame: there is no screen position to have come from
            if (q.z >= c0.x) vel = (uv - prev_uv(q)) * c6.xy * c1.w;
        }
    }
    else
    {
        // ---- the gun ---------------------------------------------------------------
        // Its own near / far and its own fov: the world's would hand a gun pixel the velocity
        // of a point three units from the lens. Back out of the 0..0.1 squeeze first.
        float d  = raw / c1.z;
        float vz = (c12.x * c12.y) / (c12.y - d * (c12.y - c12.x));

        float3 p = float3((uv.x * 2.0 - 1.0) * c12.z, (1.0 - uv.y * 2.0) * c12.w, 1.0) * vz;

        if (c8.w > 0.5 && c13.x > 0.5)
        {
            // THE BONES ARE THE WHOLE OF IT. Every proxy is a transform with the gun's rigid
            // ride already divided out (see fbbones.h), so a bone that merely went where the
            // gun went returns the point it was handed and writes no streak -- walking,
            // turning and the bob they drive all fall out, and what is left is the animation.
            //
            // The blend is a skinning with distance standing in for the weights we cannot
            // read: 1 / (d^2 + e)^2, which hands a pixel the bone NEAREST it however far the
            // mesh reaches past the skeleton.
            float3 acc = 0.0;
            float  wsum = 0.0;

            [unroll] for (int i = 0; i < 16; i++)
            {
                float3 dv = p - bone[i * 4].xyz;

                // DEPTH COUNTS FOR THREE. A bone centre sits INSIDE the mesh it drives, so the
                // bone a surface pixel belongs to is a unit or two behind it -- while the hand
                // behind the weapon is five or ten. A plain 3d distance lets that hand win on a
                // pixel of the GUN, which is a weapon blurring because the hand moved.
                dv.z *= 3.0;

                // INVERSE FOURTH POWER, not inverse square: a wide kernel averages the whole
                // skeleton's motion into every pixel, and a reload then looks like one soft
                // mush instead of a hand going one way and a magazine another.
                float dd = dot(dv, dv);
                float w  = 1.0 / (dd * dd + 0.02);

                acc += w * float3(dot(bone[i * 4 + 1].xyz, p) + bone[i * 4 + 1].w,
                                  dot(bone[i * 4 + 2].xyz, p) + bone[i * 4 + 2].w,
                                  dot(bone[i * 4 + 3].xyz, p) + bone[i * 4 + 3].w);
                wsum += w;
            }

            float3 q = acc / max(wsum, 1e-15);

            // c12.zw and NOT the previous frame's: a gun-local transform lands in THIS frame's
            // gun space, so it is this frame's fov that turns it into pixels. c13.y is the
            // amount, and there is no c1.w on it -- the transform was already RUN for the
            // length of the shutter (power_transform on the cpu side).
            if (q.z >= c12.x)
                vel = (uv - float2((q.x / (q.z * c12.z)) * 0.5 + 0.5, 0.5 - (q.y / (q.z * c12.w)) * 0.5)) * c6.xy * c13.y;
        }

        // pushed in front of the world, under half its near plane. The scale is flat because
        // the depth test downstream is a RATIO: every gun pixel moving together changes nothing
        // between two of them, and a wall you stand against can never be ruled the nearer.
        z = min(vz * 0.05, c8.y);
    }

    float l = length(vel);
    float h = l * 0.5;   // the gather walks both ways out of the pixel: a shutter is centred

    // A SOFT KNEE, NOT min(). Every streak past the cap coming back exactly the cap long draws
    // a visible straight edge across the smear where a whole fast object stops on one line.
    // Linear to half the cap, bending into it after, value and slope both continuous.
    float mx = max(c2.y, 1e-3);
    float n  = (h < 0.5 * mx) ? h : (mx - 0.25 * mx * mx / max(h, 1e-3));

    // and the sub-pixel end is a ramp: 0.99px sitting dead next to a 1.01px neighbour at full
    // length draws the START of the blur as a line the same way
    n *= saturate((l - 0.5) * 2.0);

    float2 m = vel * (n / max(1e-15, l));

    return float4(n, min(z, 60000.0), m / max(1e-15, n));
}
