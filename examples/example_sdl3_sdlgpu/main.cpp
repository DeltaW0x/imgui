#define STB_IMAGE_IMPLEMENTATION
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu.h"

#include <stdio.h>
#include <SDL3/SDL.h>



int main() {
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMEPAD) != 0)
	{
		printf("Error: SDL_Init(): %s\n", SDL_GetError());
		return -1;
	}

	SDL_Window* window = SDL_CreateWindow("SDL_Gpu ImGui", 1280, 720, 0);
	SDL_GpuDevice* device = SDL_GpuCreateDevice(SDL_GPU_BACKEND_ALL, SDL_TRUE);
	SDL_GpuClaimWindow(device, window, SDL_GPU_COLORSPACE_NONLINEAR_SRGB, SDL_GPU_PRESENTMODE_VSYNC);

	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
	ImGui::StyleColorsDark();

	ImGui_ImplSDLGpu_InitInfo info;
	info.Device = device;
	info.Window = window;
	info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
	info.WindowColorspace = SDL_GPU_COLORSPACE_NONLINEAR_SRGB;
	info.WindowPresentMode = SDL_GPU_PRESENTMODE_VSYNC;

	ImGui_ImplSDL3_InitForOther(window);
	ImGui_ImplSDLGpu_Init(&info);

	bool quit = false;
	bool show_demo_window = true;
	bool show_another_window = false;
	ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	while (!quit)
	{
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			ImGui_ImplSDL3_ProcessEvent(&e);
			switch (e.type)
			{
			case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
				quit = true;
				break;
			}
		}

		ImGui_ImplSDLGpu_NewFrame();
		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();
		// 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
		if (show_demo_window)
			ImGui::ShowDemoWindow(&show_demo_window);

		// 2. Show a simple window that we create ourselves. We use a Begin/End pair to create a named window.
		{
			static float f = 0.0f;
			static int counter = 0;

			ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.

			ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
			ImGui::Checkbox("Demo Window", &show_demo_window);      // Edit bools storing our window open/close state
			ImGui::Checkbox("Another Window", &show_another_window);

			ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
			ImGui::ColorEdit3("clear color", (float*)&clear_color); // Edit 3 floats representing a color

			if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
				counter++;
			ImGui::SameLine();
			ImGui::Text("counter = %d", counter);

			ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
			ImGui::End();
		}

		// 3. Show another simple window.
		if (show_another_window)
		{
			ImGui::Begin("Another Window", &show_another_window);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
			ImGui::Text("Hello from another window!");
			if (ImGui::Button("Close Me"))
				show_another_window = false;
			ImGui::End();
		}

		ImGui::Render();

		SDL_GpuCommandBuffer* cmdbuf = SDL_GpuAcquireCommandBuffer(device);
		Uint32 w, h;
		SDL_GpuTexture* swapchainTexture = SDL_GpuAcquireSwapchainTexture(cmdbuf, window, &w, &h);

		if (swapchainTexture != NULL)
		{
			SDL_GpuColorAttachmentInfo colorAttachmentInfo = { 0 };
			colorAttachmentInfo.textureSlice.texture = swapchainTexture;
			colorAttachmentInfo.clearColor = SDL_GpuColor{ clear_color.x, clear_color.y, clear_color.z, clear_color.w };
			colorAttachmentInfo.loadOp = SDL_GPU_LOADOP_CLEAR;
			colorAttachmentInfo.storeOp = SDL_GPU_STOREOP_STORE;

			SDL_GpuRenderPass* renderPass = SDL_GpuBeginRenderPass(cmdbuf, &colorAttachmentInfo, 1, NULL);
			ImDrawData* draw_data = ImGui::GetDrawData();
			ImGui_ImplSDLGpu_RenderDrawData(draw_data, renderPass);
			SDL_GpuEndRenderPass(renderPass);

		}
		SDL_GpuSubmit(cmdbuf);
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
		}
	}

	SDL_GpuWait(device);
	ImGui_ImplSDLGpu_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	SDL_GpuUnclaimWindow(device, window);
	SDL_GpuDestroyDevice(device);
	SDL_Quit();
	return 0;
}