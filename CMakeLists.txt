cmake_minimum_required(VERSION 3.10)
project(CrescendoEngine)

set(CMAKE_CXX_STANDARD 17)

# --- DEPENDENCIES ---
find_package(SDL2 REQUIRED)
find_package(Vulkan REQUIRED)
# You would need to manually point to Jolt/Lua on Windows or use vcpkg
# include_directories(deps/imgui deps/json deps/tinygltf ...)

# --- SOURCES ---
file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.c")
# Add ImGui sources manually as they are in deps
set(IMGUI_SOURCES 
    deps/imgui/imgui.cpp 
    deps/imgui/imgui_draw.cpp
    deps/imgui/imgui_tables.cpp 
    deps/imgui/imgui_widgets.cpp
    deps/imgui/backends/imgui_impl_sdl2.cpp
    deps/imgui/backends/imgui_impl_vulkan.cpp
)

# --- EXECUTABLE ---
add_executable(Crescendo ${SOURCES} ${IMGUI_SOURCES})

# --- INCLUDES ---
target_include_directories(Crescendo PRIVATE 
    ${CMAKE_SOURCE_DIR}/src 
    ${CMAKE_SOURCE_DIR}/include
    ${SDL2_INCLUDE_DIRS}
    ${Vulkan_INCLUDE_DIRS}
)

# --- LINKING ---
target_link_libraries(Crescendo 
    ${SDL2_LIBRARIES} 
    ${Vulkan_LIBRARIES}
    # On Windows you'd link Jolt.lib and Lua.lib here
)

# Copy assets to build folder automatically
file(COPY ${CMAKE_SOURCE_DIR}/assets DESTINATION ${CMAKE_BINARY_DIR})