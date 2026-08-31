cmake_minimum_required(VERSION 3.24)
project("Crescendo SDK")

# --- 1. Global Settings & Policies ---
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(POLICY CMP0177)
  cmake_policy(SET CMP0177 NEW)
endif()

# --- 2. Dependencies ---
find_package(Threads REQUIRED)

if (EMSCRIPTEN)
    message(STATUS "Configuring WebAssembly (Emscripten) + WebGPU")
    include(FetchContent)
    
    # Fetch Web-Specific System Headers
    FetchContent_Declare(glm GIT_REPOSITORY https://github.com/g-truc/glm.git GIT_TAG 0.9.9.8 SOURCE_SUBDIR "disable_build_trick")
    FetchContent_Declare(vulkan_headers GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git GIT_TAG v1.3.280 SOURCE_SUBDIR "disable_build_trick")
    FetchContent_Declare(lua GIT_REPOSITORY https://github.com/lua/lua.git GIT_TAG v5.4.6 SOURCE_SUBDIR "disable_build_trick")
    
    FetchContent_MakeAvailable(glm vulkan_headers lua)
else()
    # Rely on native system packages (e.g., via pacman) for fast desktop builds
    find_package(SDL2 REQUIRED)
    find_package(Vulkan REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LUA REQUIRED lua5.4>=5.4)
    find_package(Ktx REQUIRED)
endif()

# Core Physics & Networking
include(FetchContent)
FetchContent_Declare(JoltPhysics URL https://github.com/jrouwe/JoltPhysics/archive/refs/tags/v5.0.0.tar.gz SOURCE_SUBDIR Build)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(JoltPhysics)

FetchContent_Declare(enet URL https://github.com/zpl-c/enet/archive/refs/heads/master.tar.gz)
set(ENET_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(enet)

if (EXISTS "${CMAKE_SOURCE_DIR}/deps/ozz-animation/CMakeLists.txt")
    set(ozz_build_samples OFF CACHE BOOL "" FORCE)
    set(ozz_build_howtos OFF CACHE BOOL "" FORCE)
    add_subdirectory(deps/ozz-animation)
endif()

# --- 3. CRESCENDO CORE LIBRARY ---
# We compile the engine as a library so multiple tools can link it without recompiling.
file(GLOB_RECURSE CORE_SOURCES 
    "src/core/*.cpp" 
    "src/scene/*.cpp" 
    "src/IO/*.cpp" 
    "src/servers/*.cpp" 
    "src/controllers/*.cpp"
    "modules/gltf/*.cpp" 
    "modules/image/*.cpp"
    "modules/terrain/*.cpp" 
    "modules/voxel/*.cpp"    
    "modules/atmosphere/*.cpp"
    "deps/xatlas.cpp"
)

# Remove the dead OS/UI weight and standalone tool entry points
list(FILTER CORE_SOURCES EXCLUDE REGEX "src/tools/.*")
list(FILTER CORE_SOURCES EXCLUDE REGEX "src/core/CrescendoOS.*")
list(FILTER CORE_SOURCES EXCLUDE REGEX "src/main.cpp")

if (EMSCRIPTEN)
    list(FILTER CORE_SOURCES EXCLUDE REGEX "src/servers/rendering/RenderingServer.cpp")
    
    # Lua WebAssembly compilation
    file(GLOB LUA_SOURCES "${lua_SOURCE_DIR}/*.c")
    list(FILTER LUA_SOURCES EXCLUDE REGEX "lua.c|luac.c|onelua.c")
    set_source_files_properties(${LUA_SOURCES} PROPERTIES LANGUAGE CXX)
    list(APPEND CORE_SOURCES ${LUA_SOURCES})
else()
    file(GLOB_RECURSE KTX_SOURCES "modules/ktx/*.cpp")
    list(APPEND CORE_SOURCES ${KTX_SOURCES})
    list(FILTER CORE_SOURCES EXCLUDE REGEX "src/servers/rendering/Webgpu/.*")
endif()

add_library(CrescendoCore STATIC ${CORE_SOURCES})

# Target-specific includes (No more global includes leaking)
target_include_directories(CrescendoCore PUBLIC 
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src 
    ${CMAKE_SOURCE_DIR}/include
    SYSTEM ${CMAKE_SOURCE_DIR}/deps/sol/include
    ${CMAKE_SOURCE_DIR}/deps/tinygltf
    ${CMAKE_SOURCE_DIR}/deps/json
    ${CMAKE_SOURCE_DIR}/deps/stb
    ${CMAKE_SOURCE_DIR}/modules/gltf 
    ${CMAKE_SOURCE_DIR}/modules/image
    ${enet_SOURCE_DIR}/include
    ${joltphysics_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/deps/ozz-animation/include
    ${CMAKE_SOURCE_DIR}/deps/imgui 
    ${CMAKE_SOURCE_DIR}/deps/imgui/backends
)

target_compile_definitions(CrescendoCore PUBLIC 
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    SOL_ALL_SAFETIES_ON=1
    SOL_COMPILER_CLANG=1
    SOL_LUA_VERSION=504
)

if (EMSCRIPTEN)
    target_compile_options(CrescendoCore PRIVATE -pthread -sUSE_PTHREADS=1 -sDEFAULT_TO_CXX=1 -Wno-nontrivial-memcall)
    target_compile_definitions(CrescendoCore PUBLIC SOL_USING_CXX_LUA=1)
    target_include_directories(CrescendoCore PUBLIC ${glm_SOURCE_DIR} ${vulkan_headers_SOURCE_DIR}/include ${lua_SOURCE_DIR})
else()
    target_compile_options(CrescendoCore PRIVATE -march=native -Wall)
    target_include_directories(CrescendoCore PUBLIC ${SDL2_INCLUDE_DIRS} ${Vulkan_INCLUDE_DIRS} ${LUA_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/modules/ktx)
    target_link_libraries(CrescendoCore PUBLIC ${LUA_LIBRARIES} SDL2::SDL2 SDL2_image Vulkan::Vulkan dl ktx)
endif()

target_link_libraries(CrescendoCore PUBLIC Threads::Threads Jolt enet_static ozz_animation ozz_base)


# --- 4. EXECUTABLE TARGETS (The SDK Suite) ---

# Place all desktop binaries directly in the project root (H3EK Style)
if (NOT EMSCRIPTEN)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()

set(IMGUI_SOURCES 
    deps/imgui/imgui.cpp 
    deps/imgui/imgui_draw.cpp 
    deps/imgui/imgui_tables.cpp 
    deps/imgui/imgui_widgets.cpp
    deps/imgui/ImGuizmo.cpp
    deps/imgui/backends/imgui_impl_sdl2.cpp
)

if (EMSCRIPTEN)
    list(APPEND IMGUI_SOURCES deps/imgui/backends/imgui_impl_wgpu.cpp)
else()
    list(APPEND IMGUI_SOURCES deps/imgui/backends/imgui_impl_vulkan.cpp)
endif()

# [ SPECTRA - 3D WORLD EDITOR ] (Runs the full game engine runtime)
add_executable(Spectra src/main.cpp ${IMGUI_SOURCES})
target_link_libraries(Spectra PRIVATE CrescendoCore)
target_include_directories(Spectra PRIVATE ${CMAKE_SOURCE_DIR}/deps/imgui ${CMAKE_SOURCE_DIR}/deps/imgui/backends)

if (EMSCRIPTEN)
    set_target_properties(Spectra PROPERTIES SUFFIX ".html")
    target_link_options(Spectra PRIVATE 
        "-pthread" "-sUSE_PTHREADS=1" "-sPTHREAD_POOL_SIZE=8"
        "--use-port=emdawnwebgpu" "-sUSE_SDL=2" "-sDEFAULT_TO_CXX=1"
        "--shell-file" "${CMAKE_SOURCE_DIR}/deps/imgui/examples/libs/emscripten/shell_minimal.html"
        "-sALLOW_MEMORY_GROWTH=1" "-sINITIAL_MEMORY=134217728"
        "-sSTACK_SIZE=5242880" "-sEXIT_RUNTIME=0" "-sASYNCIFY" "-g3"
    )
else()
    # Shader Compilation (Desktop Only)
    find_program(GLSLC_EXECUTABLE glslc REQUIRED)
    file(GLOB SHADER_FILES "assets/shaders/*.vert" "assets/shaders/*.frag" "assets/shaders/*.comp")
    foreach(SHADER ${SHADER_FILES})
        get_filename_component(SHADER_NAME ${SHADER} NAME)
        set(SPV_FILE "${CMAKE_SOURCE_DIR}/assets/shaders/${SHADER_NAME}.spv")
        add_custom_command(TARGET Spectra PRE_BUILD COMMAND ${GLSLC_EXECUTABLE} ${SHADER} -o ${SPV_FILE} DEPENDS ${SHADER})
    endforeach()
endif()

# [ DESKTOP ONLY SDK TOOLS ]
if (NOT EMSCRIPTEN)
    # [ LEMUR - STANDALONE TAG EDITOR ]
    # Lemur uses SDL_Renderer for a lightweight Guerilla workflow (Zero Vulkan/Jolt overhead)
    set(LEMUR_IMGUI_SOURCES
        deps/imgui/imgui.cpp
        deps/imgui/imgui_draw.cpp
        deps/imgui/imgui_tables.cpp
        deps/imgui/imgui_widgets.cpp
        deps/imgui/backends/imgui_impl_sdl2.cpp
        deps/imgui/backends/imgui_impl_sdlrenderer2.cpp
    )

    add_executable(Lemur src/tools/lemur/main.cpp ${LEMUR_IMGUI_SOURCES})
    target_include_directories(Lemur PRIVATE 
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/deps/imgui 
        ${CMAKE_SOURCE_DIR}/deps/imgui/backends
        ${CMAKE_SOURCE_DIR}/deps/json
        ${SDL2_INCLUDE_DIRS}
    )
    target_link_libraries(Lemur PRIVATE SDL2::SDL2)

    # [ CRESCENDO PACKER - VFS COMPILER ]
    add_executable(CrescendoPacker src/tools/CrescendoPacker.cpp)
    target_link_libraries(CrescendoPacker PRIVATE CrescendoCore)
endif()