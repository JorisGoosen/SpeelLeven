#pragma once

#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <simd/simd.h>
#include <cstdint>

struct CamUniforms {
    simd::float4 pos;
    simd::float4 fwd;
    simd::float4 right;
    simd::float4 up;
    float tanHalfFovY;
    float aspect;
    float hueCycle;
    float opacity;
    uint32_t head;
    uint32_t pad[3];
};

class Renderer {
public:
    bool init(void* nsview, void* caLayer);
    MTL::Device* device() const { return device_; }
    float aspect() const { return aspect_; }
    uint64_t generation = 0;
    uint32_t population = 0;

    void requestReseed(float density) { reseedPending_ = true; reseedDensity_ = density; }
    bool beginFrame();
    void endFrame(CamUniforms& cam, uint32_t ticks);

private:
    void encodeSeed(float density, MTL::CommandBuffer* cb);
    void encodeTicks(uint32_t ticks, MTL::CommandBuffer* cb);
    void readbackPopulation();

    void* nsview_ = nullptr;
    CA::MetalLayer* layer_ = nullptr;
    MTL::Device* device_ = nullptr;
    MTL::CommandQueue* queue_ = nullptr;
    MTL::Texture* vol_ = nullptr;
    MTL::ComputePipelineState* seedPSO_ = nullptr;
    MTL::ComputePipelineState* lifePSO_ = nullptr;
    MTL::RenderPipelineState* renderPSO_ = nullptr;
    MTL::RenderPassDescriptor* rpd_ = nullptr;
    CA::MetalDrawable* drawable_ = nullptr;
    MTL::Buffer* popCounter_ = nullptr;
    MTL::Buffer* ring_[3] = {nullptr, nullptr, nullptr};
    uint32_t head_ = 0;
    uint32_t copyIndex_ = 0;
    uint32_t reseedPending_ = 0;
    float reseedDensity_ = 0.28f;
    float aspect_ = 1.0f;
};
