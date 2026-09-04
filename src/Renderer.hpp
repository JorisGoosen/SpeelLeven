#pragma once

#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <simd/simd.h>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <cstdlib>

struct CamUniforms {
    simd::float4 pos;
    simd::float4 fwd;
    simd::float4 right;
    simd::float4 up;
    float tanHalfFovY;
    float aspect;
    float hueCycle;
    float opacity;
    float ior;
    float absorption;
    float reflStrength;
    float metallic;
    float faceting;
    uint32_t head;
};

struct DrawParams {
    int32_t startX, startY;
    int32_t endX, endY;
    int32_t radius;
    int32_t steps;
    uint32_t value;
    uint32_t head;
};

class Renderer {
public:
    bool init(void* nsview, void* caLayer);
    MTL::Device* device() const { return device_; }
    float aspect() const { return aspect_; }
    uint64_t generation = 0;
    uint32_t population = 0;

    void requestReseed(float density) { reseedPending_ = true; reseedDensity_ = density; }
    void requestClear() { clearPending_ = true; }
    void requestDraw(int startX, int startY, int endX, int endY, int radius, uint32_t value) {
        DrawParams d;
        d.startX = startX;
        d.startY = startY;
        d.endX = endX;
        d.endY = endY;
        d.radius = radius;
        d.steps = std::max(std::max(std::abs(endX - startX), std::abs(endY - startY)) + 1, 1);
        d.value = value;
        d.head = head_;
        pendingStrokes_.push_back(d);
    }
    bool beginFrame();
    void endFrame(CamUniforms& cam, uint32_t ticks);

private:
    void encodeSeed(float density, MTL::CommandBuffer* cb);
    void encodeDraw(const DrawParams& d, MTL::CommandBuffer* cb);
    void encodeTicks(uint32_t ticks, MTL::CommandBuffer* cb);
    void readbackPopulation();

    void* nsview_ = nullptr;
    CA::MetalLayer* layer_ = nullptr;
    MTL::Device* device_ = nullptr;
    MTL::CommandQueue* queue_ = nullptr;
    MTL::Texture* vol_ = nullptr;
    MTL::ComputePipelineState* seedPSO_ = nullptr;
    MTL::ComputePipelineState* lifePSO_ = nullptr;
    MTL::ComputePipelineState* drawPSO_ = nullptr;
    MTL::RenderPipelineState* renderPSO_ = nullptr;
    MTL::RenderPassDescriptor* rpd_ = nullptr;
    CA::MetalDrawable* drawable_ = nullptr;
    MTL::Buffer* popCounter_ = nullptr;
    MTL::Buffer* ring_[3] = {nullptr, nullptr, nullptr};
    uint32_t head_ = 0;
    uint32_t copyIndex_ = 0;
    uint32_t reseedPending_ = 0;
    uint32_t clearPending_ = 0;
    float reseedDensity_ = 0.28f;
    std::vector<DrawParams> pendingStrokes_;
    float aspect_ = 1.0f;
};
