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
    uint head;
    uint pad[3];
};

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

float aliveAt(int3 c, texture3d<uint, access::read> vol) {
    if (c.x < 0 || c.x > 255 || c.y < 0 || c.y > 255 || c.z < 0 || c.z > 255) return 0.0;
    return vol.read(uint3((uint)c.x, (uint)c.y, (uint)c.z)).r > 0u ? 1.0 : 0.0;
}

float3 gradNormal(int3 c, texture3d<uint, access::read> vol) {
    float gx = aliveAt(c + int3(1, 0, 0), vol) - aliveAt(c + int3(-1, 0, 0), vol);
    float gy = aliveAt(c + int3(0, 1, 0), vol) - aliveAt(c + int3(0, -1, 0), vol);
    float gz = aliveAt(c + int3(0, 0, 1), vol) - aliveAt(c + int3(0, 0, -1), vol);
    float3 g = float3(gx, gz, gy);
    if (dot(g, g) < 1e-6) return float3(0.0, 1.0, 0.0);
    return normalize(g);
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

    float3 inv = 1.0 / rd;
    float3 t0 = (float3(-0.5) - ro) * inv;
    float3 t1 = (float3(0.5) - ro) * inv;
    float3 tmin3 = min(t0, t1);
    float3 tmax3 = max(t0, t1);
    float tEnter = max(max(tmin3.x, tmin3.y), tmin3.z);
    float tExit = min(min(tmax3.x, tmax3.y), tmax3.z);

    float3 bg = mix(float3(0.012, 0.013, 0.020), float3(0.050, 0.055, 0.080), 0.5 + 0.5 * rd.y);
    float4 outCol = float4(bg, 1.0);

    float tStart0 = max(tEnter, 0.0);
    if (tExit > tStart0) {
        float3 pe = ro + rd * tStart0;
        float bx = 0.5 - abs(pe.x);
        float by = 0.5 - abs(pe.y);
        float bz = 0.5 - abs(pe.z);
        float bw = 0.006;
        float dv = max(bx, bz);
        float dh = (by < bw) ? min(bx, bz) : 1.0;
        float av = 1.0 - smoothstep(bw * 0.3, bw, dv);
        float ah = 1.0 - smoothstep(bw * 0.3, bw, dh);
        float rim = max(av * 0.55, ah * 0.9);

        const uint kSteps = 384u;
        float ds = (tExit - tStart0) / float(kSteps);
        float3 acc = float3(0.0);
        float accA = 0.0;
        bool firstHit = true;
        float hueNorm = log2(1.0 + cam.hueCycle);

        for (uint i = 0u; i < kSteps; ++i) {
            float t = tStart0 + (float(i) + 0.5) * ds;
            float3 p = ro + rd * t;
            float3 uvw = p + 0.5;
            float ageF = 1.0 - uvw.y;
            if (ageF < 0.0 || ageF >= 1.0) continue;
            uint xc = (uint)clamp(uvw.x * 256.0, 0.0, 255.0);
            uint yc = (uint)clamp(uvw.z * 256.0, 0.0, 255.0);
            uint ageIdx = min((uint)(ageF * 256.0), 255u);
            uint slice = (cam.head + 256u - ageIdx) & 255u;
            uint v = vol.read(uint3(xc, yc, slice)).r;
            if (v == 0u) continue;

            float hn = clamp(log2(1.0 + (float)v) / hueNorm, 0.0, 1.0);
            float3 c = 0.55 + 0.45 * cos(6.28318530718 * (hn + float3(0.0, 0.33, 0.67)));
            if (firstHit) {
                float3 n = gradNormal(int3(xc, yc, slice), vol);
                if (dot(n, rd) > 0.0) n = -n;
                float3 L = normalize(float3(0.45, 0.85, 0.30));
                float diff = max(dot(n, L), 0.0);
                float spec = pow(max(dot(reflect(-L, n), -rd), 0.0), 28.0);
                c = c * (0.30 + 0.70 * diff) + spec * float3(0.9, 0.95, 1.0) * 0.45;
                firstHit = false;
            }
            c *= 1.0 + 0.35 * (1.0 - hn);
            float fade = pow(1.0 - ageF, 1.4);
            float a = fade * cam.opacity;
            acc += (1.0 - accA) * a * c;
            accA += (1.0 - accA) * a;
            if (accA > 0.992) break;
        }
        outCol.rgb = acc + bg * (1.0 - accA) + rim * float3(0.25, 0.6, 0.75);
    }
    return outCol;
}
)METAL";
