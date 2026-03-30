cmake_minimum_required(VERSION 3.10)
project(CrescendoEngine)

# --- 1. Global Settings ---
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Only optimize for the local CPU if we are NOT building for the web
if (NOT EMSCRIPTEN)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
endif()

# Everyone gets warnings!
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")

# --- WEBGPU / EMSCRIPTEN TARGETS ---
if (EMSCRIPTEN)
    message(STATUS "Configuring for WebAssembly (Emscripten) + WebGPU")
    # Added -Wno-absolute-value to kill the Jolt "abs" warnings
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --use-port=emdawnwebgpu -s USE_SDL=2 -Wno-absolute-value")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --use-port=emdawnwebgpu -s USE_SDL=2 -s ALLOW_MEMORY_GROWTH=1 -s ASYNCIFY")

    # --- THE MATH MACRO SHIELD & SAFE INCLUDES --- 
    # Intercepts <math.h> from C libraries and neutralizes the macros before they break C++
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/math_fix)
    file(WRITE ${CMAKE_BINARY_DIR}/math_fix/math.h "#include_next <math.h>\n#ifdef __cplusplus\n#undef isnan\n#undef isinf\n#undef isfinite\n#undef signbit\n#endif\n")

    
    # Create a safe proxy directory to let GLM and Vulkan through without poisoning the stdlib
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/safe_includes)
    execute_process(COMMAND ln -sfn /usr/include/glm ${CMAKE_BINARY_DIR}/safe_includes/glm)
    execute_process(COMMAND ln -sfn /usr/include/vulkan ${CMAKE_BINARY_DIR}/safe_includes/vulkan)
    execute_process(COMMAND ln -sfn /usr/include/vk_video ${CMAKE_BINARY_DIR}/safe_includes/vk_video)
    execute_process(COMMAND bash -c "ln -sfn /usr/include/lua5.4/* ${CMAKE_BINARY_DIR}/safe_includes/")

    include_directories(BEFORE ${CMAKE_BINARY_DIR}/math_fix ${CMAKE_BINARY_DIR}/safe_includes)
endif() 

# --- 2. External Dependencies ---
# Only look for native system libraries if we are NOT building for the web!
if (NOT EMSCRIPTEN)
    find_package(SDL2 REQUIRED)
    find_package(Vulkan REQUIRED)
    find_package(Threads REQUIRED)
    
    # MOVED THESE INSIDE THE SHIELD:
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LUA REQUIRED lua5.4)
    find_package(Ktx REQUIRED)
endif()

# Jolt Physics via FetchContent
include(FetchContent)
FetchContent_Declare(
  JoltPhysics
  GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
  GIT_TAG        v5.0.0 
  SOURCE_SUBDIR  Build 
)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "Disable Jolt unit tests" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "Disable Jolt hello world" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "Disable Jolt performance test" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "Disable Jolt samples" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "Disable Jolt viewer" FORCE)

# ADD THIS: If we are on WebAssembly, tell Jolt to ignore the memcpy warning!
if (EMSCRIPTEN)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-nontrivial-memcall")
endif()

FetchContent_MakeAvailable(JoltPhysics)



# Disable Ozz Animation tests and samples so they don't break the web build
set(ozz_build_samples OFF CACHE BOOL "Disable ozz samples" FORCE)
set(ozz_build_howtos OFF CACHE BOOL "Disable ozz howtos" FORCE)
set(ozz_build_tests OFF CACHE BOOL "Disable ozz tests" FORCE)

# Local Subdirectories
add_subdirectory(deps/ozz-animation)
add_subdirectory(deps/enet)


# --- 3. Source Files ---
file(GLOB_RECURSE ENGINE_SOURCES "src/*.cpp" "src/*.c")
file(GLOB_RECURSE MODULE_SOURCES "modules/gltf/*.cpp" "modules/ktx/*.cpp" "modules/image/*.cpp" "modules/terrain/*.cpp")

# Filter out the Vulkan RenderingServer if we are building for the web!
if (EMSCRIPTEN)
    # Remove RenderingServer.cpp from the web build so it doesn't throw Vulkan errors
    list(FILTER ENGINE_SOURCES EXCLUDE REGEX "src/servers/rendering/RenderingServer.cpp")
endif()

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
)

# Only compile ImGui Vulkan backend if we are on Desktop
if (NOT EMSCRIPTEN)
    list(APPEND IMGUI_SOURCES deps/imgui/backends/imgui_impl_vulkan.cpp)
endif()


# --- 4. Shader Compilation ---
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


# --- 5. Executable Target ---
add_executable(crescendo_engine 
    ${ENGINE_SOURCES} 
    ${MODULE_SOURCES} 
    ${IMGUI_SOURCES} 
    ${SHADER_SPV_FILES}
)

# Tell Emscripten to output an HTML file instead of an executable!
if (EMSCRIPTEN)
    set_target_properties(crescendo_engine PROPERTIES SUFFIX ".html")
endif()


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
    ${LUA_INCLUDE_DIRS}

    /usr/include/lua5.4
    
)

# Only include native SDL and Vulkan headers on Desktop
if (NOT EMSCRIPTEN)
    target_include_directories(crescendo_engine PRIVATE ${SDL2_INCLUDE_DIRS} ${Vulkan_INCLUDE_DIRS})
endif()


# --- 7. Link Libraries & Definitions ---
# Added GLM_FORCE_CXX17 to prevent libc++ namespace conflicts
# Added SOL_LUA_VERSION to fix the sol2/Lua 5.4 detection
target_compile_definitions(crescendo_engine PRIVATE 
    GLM_FORCE_RADIANS 
    GLM_FORCE_DEPTH_ZERO_TO_ONE 
    GLM_FORCE_CXX17
    SOL_LUA_VERSION=504
)

# Base libraries everyone needs (Removed LUA and KTX from here!)
target_link_libraries(crescendo_engine PRIVATE 
    Jolt 
    enet 
    ozz_animation 
    ozz_base
)

# Platform specific linking
if (EMSCRIPTEN)
    # Fix ENet typedef clash on WebAssembly
    target_compile_definitions(enet PRIVATE HAS_SOCKLEN_T=1)
    
    set_target_properties(crescendo_engine PROPERTIES LINK_FLAGS "-s ERROR_ON_UNDEFINED_SYMBOLS=0")
else()
    # Desktop gets the native binaries!
    target_link_libraries(crescendo_engine PRIVATE ${LUA_LIBRARIES} KTX::ktx Threads::Threads SDL2::SDL2 SDL2_image Vulkan::Vulkan dl)
endif()