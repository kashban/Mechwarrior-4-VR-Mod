// Replacement for MW4Engine's VS ad8cee09478ddb12: pre-transformed (screen space) vertices of the
// game's CPU path (MLR meshes the engine does not draw on the GPU). Same work as the original, plus
// the eye offset: a constant added to clip.x, which is -P00 * e / w on the screen - the parallax of
// a camera moved sideways by e. Only perspective vertices (rhw > 0) are moved.
// Output signature must stay register-for-register identical to the original's.

cbuffer Frame : register(b0) {
    float4 g_screen;   // xy: 2/width, 2/height; z > 0: reversed depth and TAA jitter on
    float4 g_rtJitter; // zw: jitter in clip units
    float4 g_rtOrigin;
};

cbuffer Eye : register(b12) {
    row_major float4x4 g_K; // this frame's clip-space transform (head pose / eye): clip' = K clip
    float4 g_eyeN;          // x: n of the projection (clip.z of a point = n); y: only nearer than this (view z); z > 0: what is nearer than z is not drawn (our own cockpit, mw5.inc)
};

struct VSIn {
    float4 pos : POSITION; // x, y in pixels, z, rhw
    float4 c0 : COLOR0;
    float4 c1 : COLOR1;
    float2 t0 : TEXCOORD0;
    float4 t1 : TEXCOORD1;
};

struct VSOut {
    float4 pos : SV_Position;
    float4 c0 : COLOR0;
    float3 c1 : COLOR1;
    float t1 : TEXCOORD1; // packed into o2.w
    float2 t0 : TEXCOORD0;
    float t2 : TEXCOORD2; // o3.z
    float4 t3 : TEXCOORD3;
};

VSOut main(VSIn v) {
    VSOut o;
    float2 p = v.pos.xy + 0.5;
    float2 mn = min(p, 0.0);
    float2 xy;
    xy.x = (v.pos.x > 0.0) ? p.x : mn.x;
    xy.y = (v.pos.y > 0.0) ? p.y : mn.y;
    xy *= g_screen.xy;
    xy = xy * float2(1.0, -1.0) + float2(-1.0, 1.0);
    bool persp = v.pos.w > 0.0;
    float w = persp ? 1.0 / v.pos.w : 1.0;
    xy *= w;
    bool modern = g_screen.z > 0.0;
    float2 jittered = g_rtJitter.zw * w + xy;
    o.pos.xy = modern ? jittered : xy;
    float z = modern ? (1.0 - v.pos.z) : v.pos.z;
    o.pos.z = w * z;
    o.pos.w = w;
    if (persp && g_eyeN.z > 0.0 && w < g_eyeN.z) {
        // not a number: the whole triangle is dropped. (A point outside the view would leave what remains of a
        // triangle with a corner beyond the range - a sliver from there to the middle of the picture.)
        o.pos = asfloat(0x7fc00000).xxxx;
    } else if (persp && w < g_eyeN.y) {
        float4 c = mul(g_K, float4(o.pos.x, o.pos.y, g_eyeN.x, w));
        o.pos.xy = c.xy;
        o.pos.z = c.w * z;
        o.pos.w = c.w;
    }
    o.c0 = v.c0;
    o.c1 = v.c1.xyz;
    o.t1 = v.c1.w;
    o.t0 = v.t0;
    o.t2 = 0.0;
    o.t3 = v.t1;
    return o;
}
