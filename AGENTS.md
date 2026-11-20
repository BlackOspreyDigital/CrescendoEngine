# Crescendo Engine Coding Guidance

## Repository scope

Crescendo is a C++17 engine/editor with a native SDL2/Vulkan backend and an Emscripten/WebGPU backend.

Primary targets:

- `CrescendoCore`: engine library
- `Spectra`: editor/runtime executable
- `Lemur`: ECS data editor
- `CrescendoPacker`: project VFS packer

Preserve the existing directory casing, especially `src/IO`, `src/servers`, and `modules`.

## Architecture rules

- Keep engine orchestration in `src/core/Engine.*`.
- Keep renderer-independent APIs in `IRenderer.hpp`.
- Do not make gameplay, scene, or asset code depend on Vulkan-only types when the operation belongs in `IRenderer`.
- Native Vulkan implementation belongs in `RenderingServer`.
- Maintain the Emscripten split:
  - native builds use `RenderingServer`
  - Emscripten builds use `WebGPURenderer`
- Scene entities are currently owned by `Scene::entities` as raw pointers. `Scene::DeleteEntity()` and `Scene::Clear()` must remain the only deletion paths unless ownership is deliberately redesigned.
- Components are owned by entities through `std::unique_ptr`.
- Preserve `Scene` cache invalidation through `RefreshCaches()` after entity changes.
- Prefer existing subsystem boundaries over adding global managers or parallel abstractions.

## Build and validation

Use the existing build flow:

```bash
./dev.sh
```

or:

```bash
mkdir build && cd build
CC=clang CXX=clang++ cmake ..
make -j$(nproc)
```

Desktop shader sources under `src/shaders` are compiled with `glslc` as part of the `Spectra` build.

There is currently no first-party test suite or CTest registration. For changes to runtime behavior, build and launch `Spectra`. For renderer changes, run a non-`NDEBUG` build with `VK_LAYER_KHRONOS_validation` available and resolve validation errors rather than suppressing them.

## Vulkan synchronization invariants

Do not change submission or destruction code without checking all consumers of the affected resource.

The current frame protocol is:

1. Wait for `inFlightFences[currentFrame]`.
2. Acquire a swapchain image using `imageAvailableSemaphores[currentFrame]`.
3. Reset the current fence and command buffer.
4. Record commands into the current-frame command buffer.
5. Submit waiting on the image-available semaphore.
6. Signal `renderFinishedSemaphores[imageIndex]`.
7. Present and advance `currentFrame`.

Preserve the distinction between `currentFrame` indexing and `imageIndex` indexing. Do not casually replace semaphore vectors, add frames in flight, or reuse a fence without proving the ownership model.

Any new queue submission must:

- use the correct queue-family command pool;
- serialize `vkQueueSubmit` with `queueMutex`;
- provide a fence or semaphore that tracks completion;
- keep every referenced buffer, image, descriptor, pipeline, and command pool alive until GPU completion;
- wait for completion before freeing temporary command buffers or pools.

Never destroy or rebuild GPU resources while work may still reference them. Operations such as shutdown, swapchain recreation, MSAA rebuilds, and allocator replacement must wait for device idle under the existing queue lock.

Per-frame mapped UBO/SSBO memory may be overwritten only after that frame's fence has completed.

## Image layout and barrier rules

Every image transition must specify:

- correct old and new layouts;
- correct access masks;
- correct source and destination pipeline stages;
- correct aspect mask, mip count, and array-layer count.

Compute writes followed by shader reads require an explicit compute-to-read dependency. Transfer uploads require transfer-write to shader-read synchronization.

Do not add a fallback transition that hides an unsupported layout sequence. Track the actual layout lifecycle of every image.

## Resource lifetime rules

- `VulkanBuffer` and `VulkanImage` are move-only. Do not copy them.
- Preserve move semantics and null the source handle and allocator/device context.
- `VulkanImage` destroys its image view before its VMA image allocation.
- Manually created image views, samplers, framebuffers, pipelines, render passes, descriptor pools, semaphores, fences, and command pools remain the caller's responsibility.
- Never destroy a VMA resource after the allocator has been destroyed.
- Never use mapped pointers after their owning buffer is destroyed.
- Destroy custom views and samplers before their backing images.
- Destroy swapchain-dependent framebuffers, views, images, and render passes before destroying the swapchain.
- Destroy the VMA allocator before the Vulkan device only after all VMA-backed resources are gone.
- Destroy the device before the Vulkan instance.
- Keep shutdown idempotent; `Engine` may call renderer shutdown from both explicit shutdown and destructor paths.

## Swapchain and rebuild changes

When changing swapchain-dependent resources, verify all of these together:

- swapchain images and image views;
- framebuffers;
- command-buffer count;
- image-available and render-finished semaphore counts;
- descriptor image views;
- ImGui viewport texture;
- offscreen viewport, SSR, bloom, final, depth, and MSAA images;
- dependent render passes and pipelines.

If the swapchain image count can change, reconcile command buffers and per-image synchronization objects instead of assuming the previous count.

## Coding style

- Use `#pragma once` and the `Crescendo` namespace.
- Match nearby naming and formatting rather than introducing a new convention.
- Prefer RAII and `std::unique_ptr` for new owning C++ objects.
- Use forward declarations where existing headers do so.
- Keep Vulkan implementation details out of renderer-independent interfaces.
- Avoid broad refactors while fixing a synchronization or lifetime issue.
- Preserve existing logging style and error propagation unless the change specifically improves the failure path.

## Change checklist

Before submitting a renderer/resource change:

- Identify the owning object and destruction path.
- Identify every queue submission that can reference the resource.
- Check fence/semaphore indexing and command-pool ownership.
- Check image layout transitions and descriptor layouts.
- Check swapchain recreation and shutdown paths.
- Build `Spectra`.
- Run the affected runtime path with Vulkan validation enabled.
- Report any validation error or unverified runtime path explicitly.

