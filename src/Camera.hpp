#pragma once

#include <glm/vec3.hpp>
#include <glm/geometric.hpp>
#include <cmath>

struct OrbitCamera {
    float yaw = 2.3f;
    float pitch = 0.45f;
    float dist = 1.7f;
    float fovY = 45.0f * 3.14159265358979f / 180.0f;
    glm::vec3 target{0.0f};
    glm::vec3 pos{0.0f};
    glm::vec3 fwd{0.0f, 0.0f, -1.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};

    void update() {
        float cp = std::cos(pitch);
        float sp = std::sin(pitch);
        glm::vec3 dir{cp * std::sin(yaw), sp, cp * std::cos(yaw)};
        pos = target + dir * dist;
        fwd = glm::normalize(target - pos);
        right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));
        up = glm::cross(right, fwd);
    }
};
