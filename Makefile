CXX = clang++

LUA_CFLAGS  := $(shell pkg-config --cflags lua 2>/dev/null || pkg-config --cflags lua5.4)
LUA_LDFLAGS := $(shell pkg-config --libs lua 2>/dev/null || pkg-config --libs lua5.4)

CXXFLAGS = -std=c++17 -O3 -DNDEBUG -march=native -Wall \
           -I. -I./src -I./include \
           -I./deps/sol/include \
           -I./deps/tinygltf -I./deps/json -I./deps/stb -I./deps/imgui -I./deps/imgui/backends \
           -I./JoltPhysics \
           $(LUA_CFLAGS) \
           -DJPH_PROFILE_ENABLED \
           `sdl2-config --cflags`

# Linker Flags
LDFLAGS = `sdl2-config --libs` -L./JoltPhysics/Build/Output -lSDL2_image -lvulkan -lGL -ldl -lJolt $(LUA_LDFLAGS)

TARGET = crescendo_engine
OBJ_DIR = obj

# --- SHADERS ---
# 1. Standard Object Shaders
VERT_SHADER = assets/shaders/shader.vert
FRAG_SHADER = assets/shaders/shader.frag
VERT_SPV = assets/shaders/vert.spv
FRAG_SPV = assets/shaders/frag.spv

# 2. Grid Shader
GRID_FRAG = assets/shaders/grid.frag
GRID_SPV = assets/shaders/grid.spv

# 3. Sky Shaders (FIX: Added these variables)
SKY_VERT = assets/shaders/sky.vert
SKY_FRAG = assets/shaders/sky.frag
SKY_VERT_SPV = assets/shaders/sky_vert.spv
SKY_FRAG_SPV = assets/shaders/sky_frag.spv
WATER_VERT = assets/shaders/water.vert
WATER_FRAG = assets/shaders/water.frag
WATER_VERT_SPV = assets/shaders/water_vert.spv
WATER_FRAG_SPV = assets/shaders/water_frag.spv

# Source Files
SOURCES = $(shell find src -name '*.cpp')

# Dependencies
SOURCES += deps/imgui/imgui.cpp \
           deps/imgui/imgui_draw.cpp \
           deps/imgui/imgui_widgets.cpp \
           deps/imgui/imgui_tables.cpp \
           deps/imgui/imgui_demo.cpp \
           deps/imgui/backends/imgui_impl_sdl2.cpp \
           deps/imgui/backends/imgui_impl_vulkan.cpp \
           deps/imgui/ImGuizmo.cpp

# Jolt Physics
JOLT_SOURCES = $(shell find JoltPhysics/Jolt -name '*.cpp')
SOURCES += $(JOLT_SOURCES)

OBJECTS = $(SOURCES:%.cpp=$(OBJ_DIR)/%.o)

# --- BUILD RULES ---
all: $(VERT_SPV) $(FRAG_SPV) $(GRID_SPV) $(SKY_VERT_SPV) $(SKY_FRAG_SPV) $(WATER_VERT_SPV) $(WATER_FRAG_SPV) $(TARGET)

$(TARGET): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Shader Compilation Rules
$(VERT_SPV): $(VERT_SHADER)
	glslc $(VERT_SHADER) -o $(VERT_SPV)

$(FRAG_SPV): $(FRAG_SHADER)
	glslc $(FRAG_SHADER) -o $(FRAG_SPV)

$(GRID_SPV): $(GRID_FRAG)
	glslc $(GRID_FRAG) -o $(GRID_SPV)

# FIX: Added rules for Sky Shaders
$(SKY_VERT_SPV): $(SKY_VERT)
	glslc $(SKY_VERT) -o $(SKY_VERT_SPV)

$(SKY_FRAG_SPV): $(SKY_FRAG)
	glslc $(SKY_FRAG) -o $(SKY_FRAG_SPV)

$(WATER_VERT_SPV): $(WATER_VERT)
	glslc $(WATER_VERT) -o $(WATER_VERT_SPV)

$(WATER_FRAG_SPV): $(WATER_FRAG)
	glslc $(WATER_FRAG) -o $(WATER_FRAG_SPV)

clean:
	rm -rf $(OBJ_DIR) $(TARGET) assets/shaders/*.spv

.PHONY: all clean