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
    bool drawMode = false;
    int tool = 0;
    float tickRate = 20.0f;
    float density = 0.28f;
    float hueCycle = 256.0f;
    float opacity = 0.9f;
    float ior = 1.45f;
    float absorption = 0.8f;
    float reflStrength = 1.0f;
    float metallic = 0.15f;
    float faceting = 1.0f;
    int brushWidth = 4;
};

static UIState g_ui;
static OrbitCamera g_cam;
static Renderer g_renderer;
static GLFWwindow* g_window = nullptr;
static bool g_drag = false;
static bool g_pan = false;
static bool g_drawDrag = false;
static bool g_lineDrag = false;
static int g_lineStartX = 0, g_lineStartY = 0;
static bool g_lastCellValid = false;
static int g_lastCellX = 0, g_lastCellY = 0;
static double g_lastX = 0.0, g_lastY = 0.0;

static bool pickTopCell(int* cx, int* cy) {
    double mx, my;
    glfwGetCursorPos(g_window, &mx, &my);
    int ww, wh;
    glfwGetWindowSize(g_window, &ww, &wh);
    if (ww <= 0 || wh <= 0) return false;
    float ndcX = 2.0f * (float)mx / (float)ww - 1.0f;
    float ndcY = 1.0f - 2.0f * (float)my / (float)wh;

    g_cam.update();
    float tanY = std::tan(g_cam.fovY * 0.5f);
    float aspect = g_renderer.aspect();
    glm::vec3 rd = g_cam.fwd
                 + g_cam.right * (ndcX * tanY * aspect)
                 + g_cam.up * (ndcY * tanY);
    float len = glm::length(rd);
    if (len < 1e-6f) return false;
    rd /= len;
    if (std::fabs(rd.y) < 1e-5f) return false;
    float t = (0.5f - g_cam.pos.y) / rd.y;
    if (t <= 0.0f) return false;
    glm::vec3 p = g_cam.pos + rd * t;
    if (std::fabs(p.x) > 0.5f || std::fabs(p.z) > 0.5f) return false;
    *cx = std::clamp((int)((p.x + 0.5f) * 256.0f), 0, 255);
    *cy = std::clamp((int)((p.z + 0.5f) * 256.0f), 0, 255);
    return true;
}

static void queueStroke(int sx, int sy, int ex, int ey) {
    bool erase =
        glfwGetKey(g_window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
        glfwGetKey(g_window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
    g_renderer.requestDraw(sx, sy, ex, ey, g_ui.brushWidth, erase ? 0u : 1u);
}

static void snapLine(int sx, int sy, int* ex, int* ey) {
    int dx = *ex - sx;
    int dy = *ey - sy;
    if (std::abs(dx) > 2 * std::abs(dy)) {
        dy = 0;
    } else if (std::abs(dy) > 2 * std::abs(dx)) {
        dx = 0;
    } else {
        int m = std::min(std::abs(dx), std::abs(dy));
        dx = dx < 0 ? -m : m;
        dy = dy < 0 ? -m : m;
    }
    *ex = std::clamp(sx + dx, 0, 255);
    *ey = std::clamp(sy + dy, 0, 255);
}

static void keyCallback(GLFWwindow*, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    if (ImGui::GetIO().WantCaptureKeyboard) return;
    if (key == GLFW_KEY_SPACE) g_ui.paused = !g_ui.paused;
    else if (key == GLFW_KEY_R) g_renderer.requestReseed(g_ui.density);
    else if (key == GLFW_KEY_B) g_ui.drawMode = !g_ui.drawMode;
    else if (key == GLFW_KEY_L) g_ui.tool = g_ui.tool == 0 ? 1 : 0;
}

static void mouseButtonCallback(GLFWwindow* window, int button, int action, int) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS && !ImGui::GetIO().WantCaptureMouse) {
            if (g_ui.drawMode) {
                int cx, cy;
                if (pickTopCell(&cx, &cy)) {
                    g_lastCellValid = true;
                    g_lastCellX = cx;
                    g_lastCellY = cy;
                } else {
                    g_lastCellValid = false;
                }
                if (g_ui.tool == 1) {
                    g_lineDrag = true;
                    g_lineStartX = g_lastCellX;
                    g_lineStartY = g_lastCellY;
                } else {
                    g_drawDrag = true;
                    if (g_lastCellValid) {
                        queueStroke(g_lastCellX, g_lastCellY, g_lastCellX, g_lastCellY);
                    }
                }
            } else {
                g_drag = true;
                glfwGetCursorPos(window, &g_lastX, &g_lastY);
            }
        } else if (action == GLFW_RELEASE) {
            if (g_lineDrag) {
                g_lineDrag = false;
                if (g_lastCellValid) {
                    int ex = g_lastCellX, ey = g_lastCellY;
                    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                        glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
                        snapLine(g_lineStartX, g_lineStartY, &ex, &ey);
                    }
                    queueStroke(g_lineStartX, g_lineStartY, ex, ey);
                }
            }
            g_drag = false;
            g_drawDrag = false;
        }
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT || button == GLFW_MOUSE_BUTTON_MIDDLE) {
        if (action == GLFW_PRESS && !ImGui::GetIO().WantCaptureMouse) {
            g_pan = true;
            glfwGetCursorPos(window, &g_lastX, &g_lastY);
        } else if (action == GLFW_RELEASE) {
            g_pan = false;
        }
    }
}

static void cursorPosCallback(GLFWwindow*, double x, double y) {
    float dx = (float)(x - g_lastX);
    float dy = (float)(y - g_lastY);
    g_lastX = x;
    g_lastY = y;
    if (g_lineDrag) {
        int cx, cy;
        if (pickTopCell(&cx, &cy)) {
            g_lastCellValid = true;
            g_lastCellX = cx;
            g_lastCellY = cy;
        } else {
            g_lastCellValid = false;
        }
    } else if (g_drawDrag) {
        int cx, cy;
        if (pickTopCell(&cx, &cy)) {
            if (g_lastCellValid) {
                queueStroke(g_lastCellX, g_lastCellY, cx, cy);
            } else {
                queueStroke(cx, cy, cx, cy);
            }
            g_lastCellValid = true;
            g_lastCellX = cx;
            g_lastCellY = cy;
        } else {
            g_lastCellValid = false;
        }
    } else if (g_drag) {
        g_cam.yaw += dx * 0.006f;
        g_cam.pitch += dy * 0.006f;
        g_cam.pitch = std::clamp(g_cam.pitch, -1.45f, 1.45f);
    } else if (g_pan) {
        float k = g_cam.dist * 0.0016f;
        g_cam.target += (-dx * g_cam.right + dy * g_cam.up) * k;
    }
}

static void scrollCallback(GLFWwindow*, double, double yoff) {
    if (ImGui::GetIO().WantCaptureMouse) return;
    g_cam.dist = std::clamp(g_cam.dist * std::exp(-(float)yoff * 0.08f), 1.15f, 6.0f);
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
    cam.ior = g_ui.ior;
    cam.absorption = g_ui.absorption;
    cam.reflStrength = g_ui.reflStrength;
    cam.metallic = g_ui.metallic;
    cam.faceting = g_ui.faceting;
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
    if (ImGui::Button("Clear")) renderer.requestClear();
    ImGui::SameLine();
    if (ImGui::Button("Reseed (R)")) renderer.requestReseed(g_ui.density);
    ImGui::SliderFloat("Seed density", &g_ui.density, 0.05f, 0.60f, "%.2f");
    ImGui::Separator();
    ImGui::Checkbox("Draw (B) — LMB paints, Alt erases", &g_ui.drawMode);
    if (g_ui.drawMode) {
        ImGui::RadioButton("Freehand", &g_ui.tool, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Line (L) — Shift snaps", &g_ui.tool, 1);
        ImGui::SliderInt("Brush width", &g_ui.brushWidth, 1, 24);
    }
    ImGui::Separator();
    ImGui::SliderFloat("Hue cycle (gens)", &g_ui.hueCycle, 16.0f, 4096.0f, "%.0f",
                       ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Cell opacity", &g_ui.opacity, 0.30f, 1.0f, "%.2f");
    ImGui::Separator();
    ImGui::SliderFloat("IOR", &g_ui.ior, 1.0f, 2.0f, "%.2f");
    ImGui::SliderFloat("Absorption", &g_ui.absorption, 0.0f, 2.0f, "%.2f");
    ImGui::SliderFloat("Reflectivity", &g_ui.reflStrength, 0.0f, 1.5f, "%.2f");
    ImGui::SliderFloat("Metallic", &g_ui.metallic, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Faceting", &g_ui.faceting, 0.0f, 1.0f, "%.2f");
    ImGui::Separator();
    ImGui::Text("generation %llu", (unsigned long long)renderer.generation);
    ImGui::Text("population %u", renderer.population);
    ImGui::Spacing();
    ImGui::TextDisabled("LMB orbit · RMB pan · WASD pan · scroll zoom");
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
    g_window = window;

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

        if (!ImGui::GetIO().WantCaptureKeyboard) {
            float ms = g_cam.dist * 1.2f * dt;
            if (glfwGetKey(window, GLFW_KEY_A)) g_cam.target -= g_cam.right * ms;
            if (glfwGetKey(window, GLFW_KEY_D)) g_cam.target += g_cam.right * ms;
            if (glfwGetKey(window, GLFW_KEY_W)) g_cam.target += g_cam.up * ms;
            if (glfwGetKey(window, GLFW_KEY_S)) g_cam.target -= g_cam.up * ms;
        }

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
