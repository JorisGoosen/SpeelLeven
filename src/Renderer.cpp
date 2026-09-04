#include "Renderer.hpp"
#include "Shaders.hpp"
#include "CocoaBridge.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <CoreGraphics/CoreGraphics.h>
#include <cstdio>
#include <cmath>

static constexpr uint32_t kDim = 256;

static MTL::Function* getFn(MTL::Library* lib, const char* name) {
    return lib->newFunction(NS::String::string(name, NS::UTF8StringEncoding));
}

bool Renderer::init(void* nsview, void* caLayer) {
    nsview_ = nsview;
    layer_ = (CA::MetalLayer*)caLayer;
    device_ = MTL::CreateSystemDefaultDevice();
    if (!device_) {
        fprintf(stderr, "No Metal device\n");
        return false;
    }
    layer_->setDevice(device_);
    layer_->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    queue_ = device_->newCommandQueue();

    NS::Error* err = nullptr;
    MTL::Library* lib = device_->newLibrary(
        NS::String::string(kShaderSource, NS::UTF8StringEncoding), nullptr, &err);
    if (!lib) {
        fprintf(stderr, "Shader compile failed:\n%s\n",
                err->localizedDescription()->utf8String());
        return false;
    }

    MTL::TextureDescriptor* td = MTL::TextureDescriptor::alloc()->init();
    td->setTextureType(MTL::TextureType3D);
    td->setPixelFormat(MTL::PixelFormatR32Uint);
    td->setWidth(kDim);
    td->setHeight(kDim);
    td->setDepth(kDim);
    td->setUsage(MTL::TextureUsageShaderRead | MTL::TextureUsageShaderWrite);
    td->setStorageMode(MTL::StorageModeManaged);
    vol_ = device_->newTexture(td);
    td->release();

    seedPSO_ = device_->newComputePipelineState(getFn(lib, "seedKernel"), &err);
    if (!seedPSO_) goto fail;
    lifePSO_ = device_->newComputePipelineState(getFn(lib, "lifeKernel"), &err);
    if (!lifePSO_) goto fail;
    drawPSO_ = device_->newComputePipelineState(getFn(lib, "drawKernel"), &err);
    if (!drawPSO_) goto fail;

    {
        MTL::RenderPipelineDescriptor* rpd = MTL::RenderPipelineDescriptor::alloc()->init();
        rpd->setVertexFunction(getFn(lib, "fullscreenVS"));
        rpd->setFragmentFunction(getFn(lib, "raymarchFS"));
        rpd->colorAttachments()->object(0)->setPixelFormat(layer_->pixelFormat());
        renderPSO_ = device_->newRenderPipelineState(rpd, &err);
        rpd->release();
        if (!renderPSO_) goto fail;
    }

    popCounter_ = device_->newBuffer(4, MTL::ResourceStorageModeManaged);
    for (auto& b : ring_) b = device_->newBuffer(4, MTL::ResourceStorageModeShared);

    lib->release();
    return true;

fail:
    if (err) fprintf(stderr, "Pipeline creation failed: %s\n",
                     err->localizedDescription()->utf8String());
    return false;
}

bool Renderer::beginFrame() {
    double w = 0, h = 0;
    sl_view_pixel_size(nsview_, &w, &h);
    if (w > 0 && h > 0) {
        CGSize cur = layer_->drawableSize();
        if (std::fabs(cur.width - w) > 0.5 || std::fabs(cur.height - h) > 0.5) {
            layer_->setDrawableSize(CGSizeMake(w, h));
        }
        aspect_ = (float)(w / h);
    }

    drawable_ = layer_->nextDrawable();
    if (!drawable_) return false;

    sl_autorelease_push();
    rpd_ = MTL::RenderPassDescriptor::renderPassDescriptor();
    auto* ca = rpd_->colorAttachments()->object(0);
    ca->setTexture(drawable_->texture());
    ca->setLoadAction(MTL::LoadActionClear);
    ca->setStoreAction(MTL::StoreActionStore);
    ca->setClearColor(MTL::ClearColor(0.012, 0.013, 0.020, 1.0));

    sl_imgui_metal_new_frame(rpd_);
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    return true;
}

void Renderer::encodeSeed(float density, MTL::CommandBuffer* cb) {
    head_ = 0;
    generation = 0;
    auto* enc = cb->computeCommandEncoder();
    enc->setComputePipelineState(seedPSO_);
    enc->setTexture(vol_, 0);
    enc->setBytes(&density, sizeof(float), 0);
    enc->dispatchThreadgroups(MTL::Size(16, 16, 16), MTL::Size(16, 16, 1));
    enc->endEncoding();
}

void Renderer::encodeDraw(const DrawParams& d, MTL::CommandBuffer* cb) {
    auto* enc = cb->computeCommandEncoder();
    enc->setComputePipelineState(drawPSO_);
    enc->setTexture(vol_, 0);
    enc->setBytes(&d, sizeof(d), 0);
    enc->dispatchThreads(MTL::Size((NS::UInteger)d.steps,
                                   (NS::UInteger)d.extent,
                                   (NS::UInteger)d.extent),
                         MTL::Size(1, 16, 16));
    enc->endEncoding();
}

void Renderer::encodeTicks(uint32_t ticks, MTL::CommandBuffer* cb) {
    for (uint32_t i = 0; i < ticks; ++i) {
        MTL::BlitCommandEncoder* blit = cb->blitCommandEncoder();
        blit->fillBuffer(popCounter_, NS::Range(0, 4), 0);
        blit->endEncoding();

        auto* enc = cb->computeCommandEncoder();
        enc->setComputePipelineState(lifePSO_);
        enc->setTexture(vol_, 0);
        enc->setBuffer(popCounter_, 0, 0);
        enc->setBytes(&head_, sizeof(uint32_t), 1);
        enc->dispatchThreadgroups(MTL::Size(16, 16, 1), MTL::Size(16, 16, 1));
        enc->endEncoding();

        blit = cb->blitCommandEncoder();
        blit->copyFromBuffer(popCounter_, 0, ring_[copyIndex_ % 3], 0, 4);
        blit->endEncoding();

        head_ = (head_ + 1) % kDim;
        ++generation;
    }
    if (ticks > 0) ++copyIndex_;
}

void Renderer::readbackPopulation() {
    if (copyIndex_ >= 2) {
        population = *(const uint32_t*)ring_[(copyIndex_ + 1) % 3]->contents();
    }
}

void Renderer::endFrame(CamUniforms& cam, uint32_t ticks) {
    MTL::CommandBuffer* cb = queue_->commandBuffer();

    if (clearPending_) {
        encodeSeed(0.0f, cb);
        clearPending_ = false;
    } else if (reseedPending_) {
        encodeSeed(reseedDensity_, cb);
        reseedPending_ = false;
    }
    for (const DrawParams& d : pendingStrokes_) encodeDraw(d, cb);
    pendingStrokes_.clear();
    encodeTicks(ticks, cb);
    cam.head = head_;

    auto* enc = cb->renderCommandEncoder(rpd_);
    enc->setRenderPipelineState(renderPSO_);
    enc->setFragmentBytes(&cam, sizeof(cam), 0);
    enc->setFragmentTexture(vol_, 0);
    enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), 3);

    ImGui::Render();
    sl_imgui_metal_render_draw_data(ImGui::GetDrawData(), cb, enc);
    enc->endEncoding();

    cb->presentDrawable(drawable_);
    cb->commit();
    readbackPopulation();
    sl_autorelease_pop();
}
