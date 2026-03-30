cmake_minimum_required(VERSION 3.10)
project(CrescendoEngine)

# --- 1. Global Settings ---
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Silence Jolt Warnings
if(POLICY CMP0177)
  cmake_policy(SET CMP0177 NEW)
endif()

if (NOT EMSCRIPTEN)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
endif()
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")

# --- 2. WEBGPU / EMSCRIPTEN TARGETS ---
if (EMSCRIPTEN)
    message(STATUS "Configuring for WebAssembly (Emscripten) + WebGPU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --use-port=emdawnwebgpu -s USE_SDL=2")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --use-port=emdawnwebgpu -s USE_SDL=2 -s ALLOW_MEMORY_GROWTH=1 -s ASYNCIFY")
endif()

# --- 3. External Dependencies ---
find_package(Threads REQUIRED)

if (NOT EMSCRIPTEN)
    find_package(SDL2 REQUIRED)
    find_package(Vulkan REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LUA REQUIRED lua5.4>=5.4)
    find_package(Ktx REQUIRED)
endif()

# Jolt Physics via FetchContent (Using Archive to bypass Git errors)
include(FetchContent)
FetchContent_Declare(
  JoltPhysics
  URL https://github.com/jrouwe/JoltPhysics/archive/refs/tags/v5.0.0.tar.gz
  SOURCE_SUBDIR  Build 
)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "Disable Jolt unit tests" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "Disable Jolt hello world" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "Disable Jolt performance test" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "Disable Jolt samples" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "Disable Jolt viewer" FORCE)
FetchContent_MakeAvailable(JoltPhysics)

# ENet via FetchContent (Using the active zpl-c archive)
FetchContent_Declare(
  enet
  URL https://github.com/zpl-c/enet/archive/refs/heads/master.tar.gz
)
FetchContent_MakeAvailable(enet)

# Local Subdirectories
if (EXISTS "${CMAKE_SOURCE_DIR}/deps/ozz-animation/CMakeLists.txt")
    add_subdirectory(deps/ozz-animation)
endif()

# --- 4. Source Files ---
file(GLOB_RECURSE ENGINE_SOURCES "src/*.cpp" "src/*.c")
file(GLOB_RECURSE MODULE_SOURCES "modules/gltf/*.cpp" "modules/ktx/*.cpp" "modules/image/*.cpp" "modules/terrain/*.cpp")

list(APPEND ENGINE_SOURCES 
    "src/controllers/VehicleController.cpp"
    "src/IO/ConfigManager.cpp"
    "src/IO/SceneManager.cpp"
    "src/servers/audio/AudioServer.cpp"
    "src/servers/interface/SymbolServer.cpp"
    "deps/xatlas.cpp"
)

set(IMGUI_SOURCES 
    deps/imgui/imgui.cpp 
    deps/imgui/imgui_draw.cpp
    deps/imgui/imgui_tables.cpp 
    deps/imgui/imgui_widgets.cpp
    deps/imgui/imgui_demo.cpp
    deps/imgui/ImGuizmo.cpp
    deps/imgui/backends/imgui_impl_sdl2.cpp
)

if (EMSCRIPTEN)
    list(APPEND IMGUI_SOURCES deps/imgui/backends/imgui_impl_wgpu.cpp)
else()
    list(APPEND IMGUI_SOURCES deps/imgui/backends/imgui_impl_vulkan.cpp)
endif()

# --- 5. Shader Compilation ---
if (NOT EMSCRIPTEN)
    find_program(GLSLC_EXECUTABLE glslc REQUIRED)
    file(GLOB SHADER_FILES "assets/shaders/*.vert" "assets/shaders/*.frag" "assets/shaders/*.comp")
    set(SHADER_SPV_FILES "")
    foreach(SHADER ${SHADER_FILES})
        get_filename_component(SHADER_NAME ${SHADER} NAME)
        set(SPV_FILE "${CMAKE_SOURCE_DIR}/assets/shaders/${SHADER_NAME}.spv")
        add_custom_command(
            OUTPUT ${SPV_FILE}
            COMMAND ${GLSLC_EXECUTABLE} ${SHADER} -o ${SPV_FILE}
            DEPENDS ${SHADER}
            COMMENT "Compiling shader ${SHADER_NAME}"
        )
        list(APPEND SHADER_SPV_FILES ${SPV_FILE})
    endforeach()
endif()

# --- 6. Executable Target ---
add_executable(crescendo_engine ${ENGINE_SOURCES} ${MODULE_SOURCES} ${IMGUI_SOURCES} ${SHADER_SPV_FILES})

# --- 7. Include Directories ---
target_include_directories(crescendo_engine PRIVATE 
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src 
    ${CMAKE_SOURCE_DIR}/include
    SYSTEM ${CMAKE_SOURCE_DIR}/deps/sol/include # SYSTEM tag fixes sol.hpp diagnostic errors
    ${CMAKE_SOURCE_DIR}/deps/tinygltf
    ${CMAKE_SOURCE_DIR}/deps/json
    ${CMAKE_SOURCE_DIR}/deps/stb
    ${CMAKE_SOURCE_DIR}/deps/imgui
    ${CMAKE_SOURCE_DIR}/deps/imgui/backends
    ${CMAKE_SOURCE_DIR}/modules/gltf 
    ${CMAKE_SOURCE_DIR}/modules/image
    ${CMAKE_SOURCE_DIR}/modules/ktx
    ${CMAKE_SOURCE_DIR}/deps/gli 
    ${enet_SOURCE_DIR}/include
    ${joltphysics_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/deps/ozz-animation/include
)

if (NOT EMSCRIPTEN)
    target_include_directories(crescendo_engine PRIVATE 
        ${SDL2_INCLUDE_DIRS}
        ${Vulkan_INCLUDE_DIRS}
        ${LUA_INCLUDE_DIRS}
    )
endif()

# --- 8. Link Libraries & Definitions ---
target_compile_definitions(crescendo_engine PRIVATE 
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    SOL_ALL_SAFETIES_ON=1
    SOL_COMPILER_CLANG=1
)

# Desktop uses standard C linkage for Lua; Emscripten uses C++ linkage
if (EMSCRIPTEN)
    target_compile_definitions(crescendo_engine PRIVATE SOL_USING_CXX_LUA=1)
else()
    target_link_libraries(crescendo_engine PRIVATE ${LUA_LIBRARIES} SDL2::SDL2 SDL2_image Vulkan::Vulkan dl)
    target_link_libraries(crescendo_engine PRIVATE ${LUA_LIBRARIES} SDL2::SDL2 SDL2_image Vulkan::Vulkan dl ktx)
    if (LUA_VERSION MATCHES "^5\\.5")
        target_compile_definitions(crescendo_engine PRIVATE SOL_LUA_VERSION=505)
    else()
        target_compile_definitions(crescendo_engine PRIVATE SOL_LUA_VERSION=504)
    endif()
endif()

target_link_libraries(crescendo_engine PRIVATE Threads::Threads Jolt enet_static ozz_animation ozz_base)