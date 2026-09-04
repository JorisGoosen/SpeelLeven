#pragma once

static const char* kShaderSource = R"METAL(
#include <metal_stdlib>
#include <metal_atomic>
using namespace metal;

constant uint kDim = 256u;

struct Cam {
    float4 pos;
    float4 fwd;
    float4 right;
    float4 up;
    float tanHalfFovY;
    float aspect;
    float hueCycle;
    float opacity;
    float ior;
    float absorption;
    float reflStrength;
    float metallic;
    float faceting;
    uint head;
};

struct DrawParams {
    int2 start;
    int2 end;
    int radius;
    int steps;
    uint value;
    uint head;
};

kernel void drawKernel(texture3d<uint, access::write> vol [[texture(0)]],
                       constant DrawParams& dp [[buffer(0)]],
                       uint3 tid [[thread_position_in_grid]])
{
    int r = dp.radius;
    int oy = (int)tid.y - r;
    int ox = (int)tid.z - r;
    if (ox * ox + oy * oy > r * r) return;
    float f = (dp.steps > 1) ? (float)tid.x / (float)(dp.steps - 1) : 0.0f;
    float2 c = mix(float2(dp.start), float2(dp.end), f);
    int2 cell = int2(round(c)) + int2(ox, oy);
    if (cell.x < 0 || cell.x > 255 || cell.y < 0 || cell.y > 255) return;
    vol.write(uint4(dp.value, 0u, 0u, 0u), uint3((uint)cell.x, (uint)cell.y, dp.head));
}

inline uint hashCoord(uint3 v) {
    uint h = v.x * 73856093u ^ v.y * 19349663u ^ v.z * 83492791u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    h ^= h >> 16u;
    return h;
}

kernel void seedKernel(texture3d<uint, access::write> vol [[texture(0)]],
                       constant float& density [[buffer(0)]],
                       uint3 gid [[thread_position_in_grid]])
{
    if (gid.x >= kDim || gid.y >= kDim || gid.z >= kDim) return;
    float r = (float)hashCoord(gid) * (1.0 / 4294967295.0);
    uint alive = (gid.z == 0u && r < density) ? 1u : 0u;
    vol.write(uint4(alive, 0u, 0u, 0u), gid);
}

kernel void lifeKernel(texture3d<uint, access::read_write> vol [[texture(0)]],
                       device atomic_uint* population [[buffer(0)]],
                       constant uint& head [[buffer(1)]],
                       uint3 gid [[thread_position_in_grid]])
{
    if (gid.x >= kDim || gid.y >= kDim) return;
    uint self = vol.read(uint3(gid.x, gid.y, head)).r;
    uint n = 0u;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            int x = ((int)gid.x + dx + 256) % 256;
            int y = ((int)gid.y + dy + 256) % 256;
            if (vol.read(uint3((uint)x, (uint)y, head)).r > 0u) ++n;
        }
    }
    bool alive = self > 0u;
    bool next = alive ? (n == 2u || n == 3u) : (n == 3u);
    uint age = next ? (self >= 0xFFFFFFFFu ? 0xFFFFFFFFu : self + 1u) : 0u;
    vol.write(uint4(age, 0u, 0u, 0u), uint3(gid.x, gid.y, (head + 1u) % kDim));
    if (next) atomic_fetch_add_explicit(population, 1u, memory_order_relaxed);
}

struct VSOut {
    float4 pos [[position]];
    float2 uv;
};

vertex VSOut fullscreenVS(uint vid [[vertex_id]]) {
    float2 p = float2(float((vid << 1u) & 2u), float(vid & 2u));
    VSOut o;
    o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0);
    o.uv = p;
    return o;
}

float3 env(float3 rd) {
    float3 base = mix(float3(0.012, 0.013, 0.020), float3(0.050, 0.055, 0.080), 0.5 + 0.5 * rd.y);
    float3 key = normalize(float3(0.45, 0.85, 0.30));
    base += float3(1.00, 0.97, 0.90) * pow(max(dot(rd, key), 0.0), 80.0) * 1.6;
    base += float3(0.30, 0.45, 0.65) * pow(max(dot(rd, normalize(float3(-0.6, 0.15, 0.5))), 0.0), 8.0) * 0.35;
    base += float3(0.20, 0.25, 0.30) * smoothstep(0.55, 0.95, abs(rd.y)) * 0.6;
    return base;
}

float3 ageColor(float hn) {
    return 0.55 + 0.45 * cos(6.28318530718 * (hn + float3(0.0, 0.33, 0.67)));
}

uint sliceOf(uint ageIdx, uint head) {
    return (head + 256u - ageIdx) & 255u;
}

struct MarchOut {
    float3 rgb;
    float a;
};

MarchOut marchVolume(float3 ro, float3 rd, uint steps,
                     texture3d<uint, access::read> vol, constant Cam& cam)
{
    MarchOut o;
    o.rgb = float3(0.0);
    o.a = 0.0;

    float3 rdn = normalize(rd + float3(1e-6));
    float3 inv = 1.0 / rdn;
    float3 t0 = (float3(-0.5) - ro) * inv;
    float3 t1 = (float3(0.5) - ro) * inv;
    float3 tmin3 = min(t0, t1);
    float3 tmax3 = max(t0, t1);
    float tEnter = max(max(tmin3.x, tmin3.y), tmin3.z);
    float tExit = min(min(tmax3.x, tmax3.y), tmax3.z);
    float tStart = max(tEnter, 0.0);
    if (tExit <= tStart) return o;

    float ds = (tExit - tStart) / float(steps);
    float hueNorm = log2(1.0 + cam.hueCycle);

    for (uint i = 0u; i < steps; ++i) {
        float t = tStart + (float(i) + 0.5) * ds;
        float3 p = ro + rdn * t;
        float3 uvw = p + 0.5;
        float ageF = 1.0 - uvw.y;
        if (ageF < 0.0 || ageF >= 1.0) continue;
        uint xc = (uint)clamp(uvw.x * 256.0, 0.0, 255.0);
        uint yc = (uint)clamp(uvw.z * 256.0, 0.0, 255.0);
        uint ageIdx = min((uint)(ageF * 256.0), 255u);
        uint v = vol.read(uint3(xc, yc, sliceOf(ageIdx, cam.head))).r;
        if (v == 0u) continue;
        float hn = clamp(log2(1.0 + (float)v) / hueNorm, 0.0, 1.0);
        float3 c = ageColor(hn) * (1.0 + 0.35 * (1.0 - hn));
        float fade = pow(1.0 - ageF, 1.4);
        float a = fade * cam.opacity;
        o.rgb += (1.0 - o.a) * a * c;
        o.a += (1.0 - o.a) * a;
        if (o.a > 0.992) break;
    }
    return o;
}

struct DdaHit {
    bool hit;
    float3 pos;
    float3 n;
    uint3 cell;
    uint age;
    float ageF;
};

float ddaTmax(float cc, int ic, float dc) {
    if (dc > 0.0) return ((float)ic + 1.0 - cc) / dc;
    if (dc < 0.0) return ((float)ic - cc) / dc;
    return 1e30;
}

DdaHit ddaHit(float3 ro, float3 rd,
              texture3d<uint, access::read> vol, constant Cam& cam)
{
    DdaHit h;
    h.hit = false;
    h.pos = float3(0.0);
    h.n = float3(0.0, 1.0, 0.0);
    h.cell = uint3(0u);
    h.age = 0u;
    h.ageF = 0.0;

    float3 rdn = normalize(rd + float3(1e-6));
    float3 inv = 1.0 / rdn;
    float3 t0 = (float3(-0.5) - ro) * inv;
    float3 t1 = (float3(0.5) - ro) * inv;
    float3 tmin3 = min(t0, t1);
    float3 tmax3 = max(t0, t1);
    float tEnter = max(max(tmin3.x, tmin3.y), tmin3.z);
    float tExit = min(min(tmax3.x, tmax3.y), tmax3.z);
    if (tExit <= max(tEnter, 0.0)) return h;

    float t = max(tEnter, 0.0) + 1e-5;
    float3 p = ro + rdn * t;
    float3 cc = float3((p.x + 0.5) * 256.0, (0.5 - p.y) * 256.0, (p.z + 0.5) * 256.0);
    int3 ic = int3(clamp(floor(cc), 0.0, 255.0));
    float3 dc = float3(rdn.x, -rdn.y, rdn.z) * 256.0;
    int3 stp = int3(dc.x > 0.0 ? 1 : (dc.x < 0.0 ? -1 : 0),
                    dc.y > 0.0 ? 1 : (dc.y < 0.0 ? -1 : 0),
                    dc.z > 0.0 ? 1 : (dc.z < 0.0 ? -1 : 0));
    float3 tMax = float3(ddaTmax(cc.x, ic.x, dc.x),
                         ddaTmax(cc.y, ic.y, dc.y),
                         ddaTmax(cc.z, ic.z, dc.z));
    float3 tDelta = float3(dc.x != 0.0 ? abs(1.0 / dc.x) : 1e30,
                           dc.y != 0.0 ? abs(1.0 / dc.y) : 1e30,
                           dc.z != 0.0 ? abs(1.0 / dc.z) : 1e30);
    int axis = -1;

    for (int iter = 0; iter < 800; ++iter) {
        if (ic.x < 0 || ic.x > 255 || ic.y < 0 || ic.y > 255 || ic.z < 0 || ic.z > 255) return h;
        uint slice = sliceOf((uint)ic.y, cam.head);
        uint v = vol.read(uint3((uint)ic.x, (uint)ic.z, slice)).r;
        if (v > 0u) {
            h.hit = true;
            h.age = v;
            h.ageF = clamp(((float)ic.y + 0.5) / 256.0, 0.0, 1.0);
            h.cell = uint3((uint)ic.x, (uint)ic.z, slice);
            if (axis == 0) h.n = float3(-sign(rdn.x), 0.0, 0.0);
            else if (axis == 1) h.n = float3(0.0, -sign(rdn.y), 0.0);
            else if (axis == 2) h.n = float3(0.0, 0.0, -sign(rdn.z));
            h.pos = ro + rdn * t;
            return h;
        }
        if (tMax.x < tMax.y && tMax.x < tMax.z) {
            t = tMax.x; ic.x += stp.x; tMax.x += tDelta.x; axis = 0;
        } else if (tMax.y < tMax.z) {
            t = tMax.y; ic.y += stp.y; tMax.y += tDelta.y; axis = 1;
        } else {
            t = tMax.z; ic.z += stp.z; tMax.z += tDelta.z; axis = 2;
        }
    }
    return h;
}

fragment float4 raymarchFS(VSOut in [[stage_in]],
                           constant Cam& cam [[buffer(0)]],
                           texture3d<uint, access::read> vol [[texture(0)]])
{
    float2 ndc = in.uv * 2.0 - 1.0;
    float tanX = cam.tanHalfFovY * cam.aspect;
    float3 rd = normalize(cam.fwd.xyz + cam.right.xyz * (ndc.x * tanX)
                                      + cam.up.xyz * (ndc.y * cam.tanHalfFovY));
    rd = normalize(rd + float3(1e-6));
    float3 ro = cam.pos.xyz;

    DdaHit h = ddaHit(ro, rd, vol, cam);

    float3 outCol;
    if (h.hit) {
        float3 cellCenter = float3((float(h.cell.x) + 0.5) / 256.0 - 0.5,
                                   0.5 - h.ageF,
                                   (float(h.cell.y) + 0.5) / 256.0 - 0.5);
        float3 org = mix(h.pos, cellCenter, cam.faceting);
        float3 vd = normalize(mix(rd, cam.fwd.xyz, cam.faceting));
        float3 n = h.n;
        if (dot(n, vd) > 0.0) n = -n;

        float cosi = clamp(dot(-vd, n), 0.0, 1.0);
        float f0 = (cam.ior - 1.0) / (cam.ior + 1.0);
        f0 *= f0;
        float hnHit = clamp(log2(1.0 + (float)h.age) / log2(1.0 + cam.hueCycle), 0.0, 1.0);
        float3 F0 = mix(float3(f0), ageColor(hnHit), cam.metallic);
        float3 Fr = F0 + (1.0 - F0) * pow(1.0 - cosi, 5.0);
        float R = f0 + (1.0 - f0) * pow(1.0 - cosi, 5.0);

        float3 rdir = reflect(vd, n);
        MarchOut rm = marchVolume(org + n * 0.008 + rdir * 0.004, rdir, 96u, vol, cam);
        float3 reflCol = rm.rgb + env(rdir) * (1.0 - rm.a);

        float3 tdir = refract(vd, n, 1.0 / cam.ior);
        float3 refrCol = float3(0.0);
        if (dot(tdir, tdir) < 1e-6) {
            R = 1.0;
        } else {
            float3 tp = org - n * 0.008 + tdir * 0.004;
            float3 absorb = float3(0.0);
            float stepIn = 0.0035;
            int emptyRun = 0;
            bool exited = false;
            float3 exitPos = tp;
            float3 exitNormal = float3(0.0);
            float hueNorm = log2(1.0 + cam.hueCycle);

            for (int i = 0; i < 200 && !exited; ++i) {
                tp += tdir * stepIn;
                if (abs(tp.x) > 0.5 || abs(tp.y) > 0.5 || abs(tp.z) > 0.5) {
                    float3 inv2 = 1.0 / (tdir + float3(1e-6));
                    float3 b0 = (float3(-0.5) - tp) * inv2;
                    float3 b1 = (float3(0.5) - tp) * inv2;
                    float3 tmx = max(b0, b1);
                    float tout = min(min(tmx.x, tmx.y), tmx.z);
                    exitPos = tp + tdir * tout;
                    float3 s = step(float3(0.999), abs(exitPos) * 2.0);
                    exitNormal = normalize(s * sign(exitPos) + float3(1e-6));
                    exited = true;
                    break;
                }
                float3 uvw2 = tp + 0.5;
                float ageF2 = 1.0 - uvw2.y;
                if (ageF2 < 0.0 || ageF2 >= 1.0) continue;
                uint xc2 = (uint)clamp(uvw2.x * 256.0, 0.0, 255.0);
                uint yc2 = (uint)clamp(uvw2.z * 256.0, 0.0, 255.0);
                uint slice2 = sliceOf(min((uint)(ageF2 * 256.0), 255u), cam.head);
                uint v2 = vol.read(uint3(xc2, yc2, slice2)).r;
                if (v2 > 0u) {
                    float hn2 = clamp(log2(1.0 + (float)v2) / hueNorm, 0.0, 1.0);
                    absorb += (1.0 - ageColor(hn2)) * cam.absorption * stepIn * 8.0;
                    emptyRun = 0;
                } else {
                    if (++emptyRun >= 10) {
                        exitPos = tp;
                        exited = true;
                    }
                }
            }

            float3 T = exp(-absorb);
            if (exited && dot(exitNormal, exitNormal) > 0.5) {
                float3 outDir = refract(tdir, -exitNormal, cam.ior);
                if (dot(outDir, outDir) < 1e-6) outDir = reflect(tdir, -exitNormal);
                MarchOut em = marchVolume(exitPos + outDir * 0.008, outDir, 64u, vol, cam);
                refrCol = T * (em.rgb + env(outDir) * (1.0 - em.a));
            } else {
                MarchOut em = marchVolume(tp, tdir, 64u, vol, cam);
                refrCol = T * (em.rgb + env(tdir) * (1.0 - em.a));
            }
        }

        float fadeH = pow(1.0 - h.ageF, 1.4);
        float3 behind = float3(0.0);
        if (fadeH < 0.999) {
            MarchOut cm = marchVolume(org + vd * 0.004, vd, 128u, vol, cam);
            behind = cm.rgb + env(vd) * (1.0 - cm.a);
        }
        float trans = (1.0 - R) * (1.0 - cam.metallic);
        outCol = fadeH * (refrCol * trans + reflCol * Fr * cam.reflStrength)
               + (1.0 - fadeH) * behind;
    } else {
        outCol = env(rd);
    }

    {
        float3 inv = 1.0 / rd;
        float3 t0 = (float3(-0.5) - ro) * inv;
        float3 t1 = (float3(0.5) - ro) * inv;
        float3 tmin3 = min(t0, t1);
        float3 tmax3 = max(t0, t1);
        float tE = max(max(tmin3.x, tmin3.y), tmin3.z);
        float tX = min(min(tmax3.x, tmax3.y), tmax3.z);
        if (tX > max(tE, 0.0)) {
            float3 pe = ro + rd * max(tE, 0.0);
            float bx = 0.5 - abs(pe.x);
            float by = 0.5 - abs(pe.y);
            float bz = 0.5 - abs(pe.z);
            float bw = 0.006;
            float dv = max(bx, bz);
            float dh = (by < bw) ? min(bx, bz) : 1.0;
            float av = 1.0 - smoothstep(bw * 0.3, bw, dv);
            float ah = 1.0 - smoothstep(bw * 0.3, bw, dh);
            float rim = max(av * 0.55, ah * 0.9);
            outCol += rim * float3(0.25, 0.6, 0.75);
        }
    }
    return float4(outCol, 1.0);
}
)METAL";
