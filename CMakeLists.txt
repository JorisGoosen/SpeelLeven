cmake_minimum_required(VERSION 3.24)
project(SpeelLeven LANGUAGES C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0)

include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9b
    GIT_SHALLOW TRUE)
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE)
FetchContent_Declare(metal-cpp
    URL https://developer.apple.com/metal/cpp/files/metal-cpp_macOS26_iOS26.zip
        https://developer.apple.com/metal/cpp/files/metal-cpp_macOS15_iOS18.zip)

FetchContent_MakeAvailable(glfw glm)

FetchContent_GetProperties(metal-cpp)
if(NOT metal-cpp_POPULATED)
    FetchContent_Populate(metal-cpp)
endif()
add_library(metal-cpp INTERFACE)
target_include_directories(metal-cpp INTERFACE
    "${metal-cpp_SOURCE_DIR}"
    "${metal-cpp_SOURCE_DIR}/metal-cpp")
target_link_libraries(metal-cpp INTERFACE
    "-framework Foundation"
    "-framework Metal"
    "-framework QuartzCore"
    "-framework CoreFoundation")

FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_metal.mm)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(imgui PUBLIC GLFW_INCLUDE_NONE)
target_link_libraries(imgui PUBLIC glfw metal-cpp)

add_executable(SpeelLeven
    src/main.cpp
    src/Renderer.cpp
    src/metalcpp_impl.cpp
    src/CocoaBridge.mm)
target_include_directories(SpeelLeven PRIVATE src)
target_link_libraries(SpeelLeven PRIVATE imgui glfw glm::glm metal-cpp)
target_link_libraries(SpeelLeven PRIVATE
    "-framework Metal"
    "-framework QuartzCore"
    "-framework AppKit"
    "-framework CoreFoundation"
    "-framework IOKit")
set_source_files_properties(src/main.cpp src/Renderer.cpp src/CocoaBridge.mm
    PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
target_compile_options(SpeelLeven PRIVATE -Wall -Wextra)
