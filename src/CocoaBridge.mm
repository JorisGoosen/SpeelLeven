#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>

#import <imgui.h>
#import <imgui_impl_metal.h>

extern "C" {
void* objc_autoreleasePoolPush(void);
void objc_autoreleasePoolPop(void* pool);
}

static void* g_poolToken = nullptr;

void sl_attach_metal_layer(void* nsview, void* caMetalLayer) {
    NSView* v = (__bridge NSView*)nsview;
    v.wantsLayer = YES;
    v.layer = (__bridge CALayer*)caMetalLayer;
}

void sl_view_pixel_size(void* nsview, double* w, double* h) {
    NSView* v = (__bridge NSView*)nsview;
    double s = (v.window != nil) ? (double)v.window.backingScaleFactor : 2.0;
    *w = (double)v.bounds.size.width * s;
    *h = (double)v.bounds.size.height * s;
}

void sl_autorelease_push() {
    g_poolToken = objc_autoreleasePoolPush();
}

void sl_autorelease_pop() {
    if (g_poolToken) {
        objc_autoreleasePoolPop(g_poolToken);
        g_poolToken = nullptr;
    }
}

bool sl_imgui_metal_init(void* device) {
    return ImGui_ImplMetal_Init((__bridge id<MTLDevice>)device);
}

void sl_imgui_metal_new_frame(void* renderPassDescriptor) {
    ImGui_ImplMetal_NewFrame((__bridge MTLRenderPassDescriptor*)renderPassDescriptor);
}

void sl_imgui_metal_render_draw_data(void* drawData, void* cmdBuffer, void* encoder) {
    ImGui_ImplMetal_RenderDrawData((ImDrawData*)drawData,
                                   (__bridge id<MTLCommandBuffer>)cmdBuffer,
                                   (__bridge id<MTLRenderCommandEncoder>)encoder);
}
