#pragma once
#ifndef IMGUI_DISABLE
#include "imgui.h"      // IMGUI_IMPL_API
#include "sdlgpu/sdlgpu_shaders.h"

enum SDL_GpuSampleCount;
struct SDL_GpuDevice;
struct SDL_GpuRenderPass;
struct SDL_Window;
struct SDL_GpuTextureSamplerBinding;
struct SDL_GpuSampler;
struct SDL_GpuTexture;

struct ImGui_ImplSDLGpu_InitInfo
{
    SDL_GpuDevice*                  Device;
    SDL_Window*                     Window;
    SDL_GpuSampleCount              MSAASamples;
};


IMGUI_IMPL_API bool         ImGui_ImplSDLGpu_Init(ImGui_ImplSDLGpu_InitInfo* info);
IMGUI_IMPL_API void         ImGui_ImplSDLGpu_Shutdown();
IMGUI_IMPL_API void         ImGui_ImplSDLGpu_NewFrame();
IMGUI_IMPL_API void         ImGui_ImplSDLGpu_RenderDrawData(ImDrawData* draw_data, SDL_GpuRenderPass* renderPass);
IMGUI_IMPL_API bool         ImGui_ImplSDLGpu_CreateFontsTexture();
IMGUI_IMPL_API void         ImGui_ImplSDLGpu_DestroyFontsTexture();

#endif