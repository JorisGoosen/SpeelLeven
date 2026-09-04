#pragma once

void sl_attach_metal_layer(void* nsview, void* caMetalLayer);
void sl_view_pixel_size(void* nsview, double* w, double* h);
void sl_autorelease_push();
void sl_autorelease_pop();
bool sl_imgui_metal_init(void* device);
void sl_imgui_metal_new_frame(void* renderPassDescriptor);
void sl_imgui_metal_render_draw_data(void* drawData, void* cmdBuffer, void* encoder);
