cmake_minimum_required(VERSION 3.10)
project(Crescendo)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native -Wall")

# --- 1. Dependencies ---
find_package(SDL2 REQUIRED)
find_package(Vulkan REQUIRED)
find_package(Threads REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LUA REQUIRED lua5.4)

# --- 2. Sources ---
file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.c")
file(GLOB_RECURSE JOLT_SOURCES "JoltPhysics/Jolt/*.cpp")

set(IMGUI_SOURCES 
    deps/imgui/imgui.cpp 
    deps/imgui/imgui_draw.cpp
    deps/imgui/imgui_tables.cpp 
    deps/imgui/imgui_widgets.cpp
    deps/imgui/imgui_demo.cpp
    deps/imgui/ImGuizmo.cpp
    deps/imgui/backends/imgui_impl_sdl2.cpp
    deps/imgui/backends/imgui_impl_vulkan.cpp
)

# --- 3. Shader Compilation ---
find_program(GLSLC_EXECUTABLE glslc)

# [FIX] Check your 'ls' output. If you don't have fullscreen.vert, 
# remove it from this list or rename it to match your file!
set(SHADER_FILES
    assets/shaders/shader.vert
    assets/shaders/shader.frag
    assets/shaders/sky.vert
    assets/shaders/sky.frag
    assets/shaders/water.vert
    assets/shaders/water.frag
    assets/shaders/bloom_bright.frag
    assets/shaders/bloom_composite.frag
    assets/shaders/fullscreen_vert.vert
)

foreach(SHADER ${SHADER_FILES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPV_FILE "${CMAKE_SOURCE_DIR}/assets/shaders/${SHADER_NAME}.spv")
    add_custom_command(
        OUTPUT ${SPV_FILE}
        COMMAND ${GLSLC_EXECUTABLE} ${CMAKE_SOURCE_DIR}/${SHADER} -o ${SPV_FILE}
        DEPENDS ${CMAKE_SOURCE_DIR}/${SHADER}
        COMMENT "Compiling shader ${SHADER}"
    )
    list(APPEND SHADER_SPV_FILES ${SPV_FILE})
endforeach()

# --- 4. Executable ---
# Named crescendo_engine to match your previous setup
add_executable(crescendo_engine ${SOURCES} ${JOLT_SOURCES} ${IMGUI_SOURCES} ${SHADER_SPV_FILES})

# --- 5. Includes ---
target_include_directories(crescendo_engine PRIVATE 
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src 
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/deps/sol/include
    ${CMAKE_SOURCE_DIR}/deps/tinygltf
    ${CMAKE_SOURCE_DIR}/deps/json
    ${CMAKE_SOURCE_DIR}/deps/stb
    ${CMAKE_SOURCE_DIR}/deps/imgui
    ${CMAKE_SOURCE_DIR}/deps/imgui/backends
    ${CMAKE_SOURCE_DIR}/JoltPhysics
    ${SDL2_INCLUDE_DIRS}
    ${Vulkan_INCLUDE_DIRS}
    ${LUA_INCLUDE_DIRS}
)

target_compile_definitions(crescendo_engine PRIVATE JPH_PROFILE_ENABLED GLM_ENABLE_EXPERIMENTAL)

target_link_libraries(crescendo_engine PRIVATE 
    SDL2::SDL2 SDL2_image Vulkan::Vulkan ${LUA_LIBRARIES} Threads::Threads dl
)