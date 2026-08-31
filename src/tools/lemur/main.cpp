#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>

#include <SDL2/SDL.h>
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"
#include "deps/json/json.hpp"

namespace fs = std::filesystem;

static std::string activeTagPath = "";
static nlohmann::json activeTagData;

std::string GetCommandLineArg(int argc, char* argv[], const std::string& flag) {
    for (int i = 0; i < argc - 1; ++i) {
        if (std::string(argv[i]) == flag) return std::string(argv[i + 1]);
    }
    return "";
}

void SetLemurStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    ImVec4 ashGreyDark   = ImVec4(0.10f, 0.10f, 0.11f, 1.00f); 
    ImVec4 ashGreyMedium = ImVec4(0.15f, 0.15f, 0.16f, 1.00f);
    ImVec4 ashGreyLight  = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    ImVec4 goldOrange    = ImVec4(1.00f, 0.65f, 0.00f, 1.00f);
    ImVec4 goldHover     = ImVec4(1.00f, 0.80f, 0.30f, 1.00f);

    colors[ImGuiCol_WindowBg]             = ashGreyDark;   
    colors[ImGuiCol_ChildBg]              = ashGreyDark;
    colors[ImGuiCol_PopupBg]              = ashGreyDark;
    colors[ImGuiCol_MenuBarBg]            = ashGreyMedium;
    colors[ImGuiCol_TitleBg]              = ashGreyDark;
    colors[ImGuiCol_TitleBgActive]        = ashGreyMedium;
    colors[ImGuiCol_TitleBgCollapsed]     = ashGreyDark;
    colors[ImGuiCol_Header]               = ashGreyMedium;
    colors[ImGuiCol_HeaderHovered]        = goldOrange;
    colors[ImGuiCol_HeaderActive]         = goldOrange;
    colors[ImGuiCol_Text]                 = goldOrange;    
    colors[ImGuiCol_TextSelectedBg]       = ImVec4(1.00f, 0.65f, 0.00f, 0.35f);
    colors[ImGuiCol_FrameBg]              = ashGreyMedium;
    colors[ImGuiCol_FrameBgHovered]       = ashGreyLight;
    colors[ImGuiCol_FrameBgActive]        = ashGreyLight;
    colors[ImGuiCol_Button]               = ashGreyMedium;
    colors[ImGuiCol_ButtonHovered]        = goldHover;
    colors[ImGuiCol_ButtonActive]         = goldOrange;
    colors[ImGuiCol_SliderGrab]           = goldOrange;
    colors[ImGuiCol_SliderGrabActive]     = goldHover;
    colors[ImGuiCol_CheckMark]            = goldOrange;
    colors[ImGuiCol_Tab]                  = ashGreyDark;
    colors[ImGuiCol_TabHovered]           = goldHover;
    colors[ImGuiCol_TabActive]            = ashGreyMedium;
    colors[ImGuiCol_TabUnfocused]         = ashGreyDark;
    colors[ImGuiCol_TabUnfocusedActive]   = ashGreyMedium;
    colors[ImGuiCol_DockingPreview]       = ImVec4(1.00f, 0.65f, 0.00f, 0.70f);
    colors[ImGuiCol_Border]               = ashGreyMedium;
    colors[ImGuiCol_Separator]            = ashGreyMedium;

    style.WindowRounding = 5.0f;
    style.FrameRounding  = 3.0f;
    style.PopupRounding  = 5.0f;
}

void DrawTagBrowser(const std::string& tagsPath) {
    ImGui::Begin("Tag Directory");
    if (fs::exists(tagsPath)) {
        for (const auto& entry : fs::recursive_directory_iterator(tagsPath)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                std::string relPath = fs::relative(entry.path(), tagsPath).string();
                bool isSelected = (activeTagPath == entry.path().string());
                
                if (ImGui::Selectable(relPath.c_str(), isSelected)) {
                    activeTagPath = entry.path().string();
                    std::ifstream f(activeTagPath);
                    if (f.is_open()) {
                        activeTagData.clear();
                        f >> activeTagData;
                    }
                }
            }
        }
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Missing 'tags/' directory at: %s", tagsPath.c_str());
    }
    ImGui::End();
}

void DrawTagInspector() {
    ImGui::Begin("Tag Inspector");
    if (!activeTagPath.empty() && !activeTagData.is_null()) {
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.0f, 1.0f), "Editing: %s", activeTagPath.c_str());
        ImGui::Separator();
        ImGui::Spacing();

        for (auto& [key, value] : activeTagData.items()) {
            if (value.is_number_float()) {
                float v = value.get<float>();
                if (ImGui::DragFloat(key.c_str(), &v, 0.05f)) value = v;
            } else if (value.is_number_integer()) {
                int v = value.get<int>();
                if (ImGui::InputInt(key.c_str(), &v)) value = v;
            } else if (value.is_boolean()) {
                bool v = value.get<bool>();
                if (ImGui::Checkbox(key.c_str(), &v)) value = v;
            } else if (value.is_string()) {
                std::string v = value.get<std::string>();
                char buffer[512];
                strncpy(buffer, v.c_str(), sizeof(buffer));
                buffer[sizeof(buffer) - 1] = '\0';
                if (ImGui::InputText(key.c_str(), buffer, sizeof(buffer))) value = std::string(buffer);
            } else if (value.is_array() && value.size() == 3 && value[0].is_number()) {
                float vec[3] = { value[0].get<float>(), value[1].get<float>(), value[2].get<float>() };
                if (ImGui::ColorEdit3(key.c_str(), vec)) value = { vec[0], vec[1], vec[2] };
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Save Tag (Ctrl+S)", ImVec2(-1, 30)) || 
           (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))) {
            std::ofstream out(activeTagPath);
            if (out.is_open()) {
                out << activeTagData.dump(4);
                std::cout << "[Lemur] Saved: " << activeTagPath << std::endl;
            }
        }
    } else {
        ImGui::TextDisabled("Select a .json tag to inspect properties.");
    }
    ImGui::End();
}

int main(int argc, char* argv[]) {
    std::cout << "========================================================\n";
    std::cout << " CRESCENDO SDK - LEMUR TAG EDITOR (Standalone v1.0)\n";
    std::cout << "========================================================\n";

    std::string projectPath = GetCommandLineArg(argc, argv, "-project");
    if (projectPath.empty()) projectPath = "./";
    if (projectPath.back() == '/' || projectPath.back() == '\\') projectPath.pop_back();

    std::string tagsPath = projectPath + "/tags";
    if (!fs::exists(tagsPath)) {
        std::cerr << "[Lemur] WARNING: Missing 'tags/' directory at: " << tagsPath << "\n";
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::cerr << "SDL_Init Error: " << SDL_GetError() << std::endl;
        return -1;
    }

    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow("Lemur - Tag Editor", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
    if (!window) {
        std::cerr << "SDL_CreateWindow Error: " << SDL_GetError() << std::endl;
        return -1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        std::cerr << "SDL_CreateRenderer Error: " << SDL_GetError() << std::endl;
        return -1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    SetLemurStyle();

    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window)) {
                running = false;
            }
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGuiID dockSpaceId = ImGui::GetID("LemurDockSpace");
        ImGui::DockSpaceOverViewport(dockSpaceId, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

        DrawTagBrowser(tagsPath);
        DrawTagInspector();

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 25, 25, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}