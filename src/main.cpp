#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>

#include "CocoaBridge.hpp"
#include "Camera.hpp"
#include "Renderer.hpp"

#include <QuartzCore/QuartzCore.hpp>
#include <cmath>
#include <cstdio>
#include <algorithm>

struct UIState {
    bool paused = false;
    float tickRate = 20.0f;
    float density = 0.28f;
    float hueCycle = 256.0f;
    float opacity = 0.9f;
};

static UIState g_ui;
static OrbitCamera g_cam;
static Renderer g_renderer;
static bool g_drag = false;
static double g_lastX = 0.0, g_lastY = 0.0;

static void keyCallback(GLFWwindow*, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    if (ImGui::GetIO().WantCaptureKeyboard) return;
    if (key == GLFW_KEY_SPACE) g_ui.paused = !g_ui.paused;
    else if (key == GLFW_KEY_R) g_renderer.requestReseed(g_ui.density);
}

static void mouseButtonCallback(GLFWwindow* window, int button, int action, int) {
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    if (action == GLFW_PRESS && !ImGui::GetIO().WantCaptureMouse) {
        g_drag = true;
        glfwGetCursorPos(window, &g_lastX, &g_lastY);
    } else if (action == GLFW_RELEASE) {
        g_drag = false;
    }
}

static void cursorPosCallback(GLFWwindow*, double x, double y) {
    if (!g_drag) return;
    g_cam.yaw += (float)(x - g_lastX) * 0.006f;
    g_cam.pitch += (float)(y - g_lastY) * 0.006f;
    g_cam.pitch = std::clamp(g_cam.pitch, -1.45f, 1.45f);
    g_lastX = x;
    g_lastY = y;
}

static void scrollCallback(GLFWwindow*, double, double yoff) {
    if (ImGui::GetIO().WantCaptureMouse) return;
    g_cam.dist = std::clamp(g_cam.dist * std::exp(-(float)yoff * 0.08f), 0.7f, 6.0f);
}

static simd::float4 toSimd(const glm::vec3& v) {
    return simd_make_float4(v.x, v.y, v.z, 0.0f);
}

static void buildCam(CamUniforms& cam, float aspect) {
    g_cam.update();
    cam.pos = toSimd(g_cam.pos);
    cam.fwd = toSimd(g_cam.fwd);
    cam.right = toSimd(g_cam.right);
    cam.up = toSimd(g_cam.up);
    cam.tanHalfFovY = std::tan(g_cam.fovY * 0.5f);
    cam.aspect = aspect;
    cam.hueCycle = g_ui.hueCycle;
    cam.opacity = g_ui.opacity;
    cam.head = 0;
}

static void drawPanel(Renderer& renderer, float fps) {
    ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(330, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("SpeelLeven");
    ImGui::Text("%.1f fps", fps);
    ImGui::Separator();
    ImGui::Checkbox("Paused (Space)", &g_ui.paused);
    ImGui::SliderFloat("Sim speed (Hz)", &g_ui.tickRate, 1.0f, 60.0f, "%.0f");
    if (ImGui::Button("Reseed (R)")) renderer.requestReseed(g_ui.density);
    ImGui::SliderFloat("Seed density", &g_ui.density, 0.05f, 0.60f, "%.2f");
    ImGui::Separator();
    ImGui::SliderFloat("Hue cycle (gens)", &g_ui.hueCycle, 16.0f, 4096.0f, "%.0f",
                       ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Cell opacity", &g_ui.opacity, 0.30f, 1.0f, "%.2f");
    ImGui::Separator();
    ImGui::Text("generation %llu", (unsigned long long)renderer.generation);
    ImGui::Text("population %u", renderer.population);
    ImGui::Spacing();
    ImGui::TextDisabled("LMB drag: orbit · scroll: zoom");
    ImGui::End();
}

int main() {
    if (!glfwInit()) {
        fprintf(stderr, "glfwInit failed\n");
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1440, 900, "SpeelLeven", nullptr, nullptr);
    if (!window) {
        fprintf(stderr, "Window creation failed\n");
        glfwTerminate();
        return 1;
    }
    void* nsview = (void*)glfwGetCocoaView(window);

    ImGui::CreateContext();
    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);
    ImGui_ImplGlfw_InitForOther(window, true);

    CA::MetalLayer* layer = CA::MetalLayer::layer();
    if (!g_renderer.init(nsview, layer)) {
        fprintf(stderr, "Renderer init failed\n");
        return 1;
    }
    sl_attach_metal_layer(nsview, layer);
    sl_imgui_metal_init((void*)g_renderer.device());
    g_renderer.requestReseed(g_ui.density);

    double last = glfwGetTime();
    double acc = 0.0;
    float fps = 60.0f;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        double now = glfwGetTime();
        float dt = (float)(now - last);
        last = now;
        dt = std::clamp(dt, 1e-5f, 0.1f);
        fps = fps * 0.9f + (1.0f / dt) * 0.1f;

        uint32_t ticks = 0;
        if (!g_ui.paused) {
            acc += dt * (double)g_ui.tickRate;
            ticks = (uint32_t)acc;
            if (ticks > 8) {
                ticks = 8;
                acc = 0.0;
            } else {
                acc -= ticks;
            }
        }

        if (g_renderer.beginFrame()) {
            drawPanel(g_renderer, fps);
            CamUniforms cam{};
            buildCam(cam, g_renderer.aspect());
            g_renderer.endFrame(cam, ticks);
        }
    }

    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
