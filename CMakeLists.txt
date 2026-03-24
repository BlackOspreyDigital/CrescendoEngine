cmake_minimum_required(VERSION 3.10)
project(CrescendoEngine)

# --- 1. Global Settings ---
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native -Wall")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -g")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -g")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")

# --- 2. External Dependencies ---
find_package(SDL2 REQUIRED)
find_package(Vulkan REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LUA REQUIRED lua5.4)
find_package(Ktx REQUIRED)

# Jolt Physics via FetchContent
include(FetchContent)
FetchContent_Declare(
  JoltPhysics
  GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
  GIT_TAG        v5.0.0 
  SOURCE_SUBDIR  Build 
)
# Disable Jolt's extra build targets so we only compile the core library
set(TARGET_UNIT_TESTS OFF CACHE BOOL "Disable Jolt unit tests" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "Disable Jolt hello world" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "Disable Jolt performance test" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "Disable Jolt samples" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "Disable Jolt viewer" FORCE)
FetchContent_MakeAvailable(JoltPhysics)

# Local Subdirectories
add_subdirectory(deps/ozz-animation)
add_subdirectory(deps/enet)


# --- 3. Source Files ---
# Core Engine & Modules
file(GLOB_RECURSE ENGINE_SOURCES "src/*.cpp" "src/*.c")
file(GLOB_RECURSE MODULE_SOURCES "modules/gltf/*.cpp" "modules/ktx/*.cpp" "modules/image/*.cpp" "modules/terrain/*.cpp")

# Explicit overrides for files CMake might miss during rapid creation
list(APPEND ENGINE_SOURCES 
    "src/controllers/VehicleController.cpp"
    "src/IO/ConfigManager.cpp"
    "src/IO/SceneManager.cpp"
    "src/servers/audio/AudioServer.cpp"
    "src/servers/interface/SymbolServer.cpp"
    "deps/xatlas.cpp"
)

# ImGui
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


# --- 4. Shader Compilation ---
find_program(GLSLC_EXECUTABLE glslc REQUIRED)

# Automatically find ALL shaders (No need to manually list them anymore!)
file(GLOB SHADER_FILES 
    "assets/shaders/*.vert" 
    "assets/shaders/*.frag" 
    "assets/shaders/*.comp"
)

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


# --- 5. Executable Target ---
# (Notice we completely removed the redundant JOLT_SOURCES here)
add_executable(crescendo_engine 
    ${ENGINE_SOURCES} 
    ${MODULE_SOURCES} 
    ${IMGUI_SOURCES} 
    ${SHADER_SPV_FILES}
)


# --- 6. Include Directories ---
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
    ${CMAKE_SOURCE_DIR}/modules/gltf 
    ${CMAKE_SOURCE_DIR}/modules/image
    ${CMAKE_SOURCE_DIR}/modules/ktx
    ${CMAKE_SOURCE_DIR}/deps/gli 
    ${CMAKE_SOURCE_DIR}/deps/enet/include
    ${joltphysics_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/deps/ozz-animation/include
    ${SDL2_INCLUDE_DIRS}
    ${Vulkan_INCLUDE_DIRS}
    ${LUA_INCLUDE_DIRS}
)


# --- 7. Link Libraries & Definitions ---
target_compile_definitions(crescendo_engine PRIVATE 
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
)

target_link_libraries(crescendo_engine PRIVATE 
    SDL2::SDL2 
    SDL2_image 
    Vulkan::Vulkan 
    ${LUA_LIBRARIES} 
    Threads::Threads 
    dl 
    Jolt 
    KTX::ktx 
    enet 
    ozz_animation 
    ozz_base
)