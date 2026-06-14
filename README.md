# Crescendo Engine

A custom-built, low-level Vulkan graphics engine designed for developers who want absolute authority over their rendering pipeline. 

Crescendo is built on a strict "from-scratch" philosophy. It rejects the modern industry trend of relying on temporal upscaling, DLSS, and "half-measures" to salvage noisy, brute-forced rendering. Instead, the focus is on clever optimization, clean rasterization, and delivering crisp, native-resolution performance reminiscent of the golden era of graphics programming.

## Current State: Renderer Proof-of-Concept

**Status: Highly Experimental**

Crescendo is currently in an active, heavy development phase. 
* **The Core Renderer:** Functions as a stable proof-of-concept demonstrating our custom Vulkan pipeline.
* **Modules & Subsystems:** All other features (such as Jolt physics integration, voxel terrain handling, and advanced memory allocators) are treated as highly experimental, modular plugins. Expect frequent breaking changes as these systems are rapidly prototyped and refactored.

## Core Architecture

* **Graphics API:** Vulkan / WebASM / DX ( Coming Soon )
* **Language:** C++
* **Design Philosophy:** Modular, code-first, and natively performant. 

## Compiling from Source

*(Note to users: Crescendo is not a monolithic drag-and-drop editor. Utilizing this engine requires an understanding of source compilation and low-level system architecture.)*

### Dependencies
* Vulkan SDK
* CMake
* Sol2
* Ktx
* SDL2

### Compiling from Source

*(Note to users: Crescendo is not a monolithic drag-and-drop editor. Utilizing this engine requires an understanding of source compilation and low-level system architecture.)*

### Prerequisites (Arch Linux)

Crescendo targets C++17 and relies on system-level libraries for windowing, rendering, and scripting. Jolt Physics and ENet are fetched automatically during the build process.

Install the required core dependencies, including Clang and the Vulkan toolchain:

```bash
sudo pacman -S base-devel cmake clang pkgconf sdl2 sdl2_image vulkan-headers vulkan-icd-loader shaderc lua ktx
```

### Prerequisites (Windows)
Direct X support coming soon!

### Prerequisites (MacOs)
Metal support coming soon!

Thanks for checking out the repo, if you would like to get involved please check out our website at htttps://www.theospreylibrary.com 
