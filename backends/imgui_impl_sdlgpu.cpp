#include "imgui.h"
#ifndef IMGUI_DISABLE
#include "imgui_impl_sdlgpu.h"


#if !SDL_VERSION_ATLEAST(3, 0, 0)
#error This backend requires SDL 3.0.0+
#endif


struct ImGui_ImplSDLGpu_Data {
    ImGui_ImplSDLGpu_InitInfo SDLGpuInitInfo = {};
    SDL_GpuGraphicsPipeline *Pipeline = NULL;
    SDL_GpuGraphicsPipeline *SecondaryPipeline = NULL;
    SDL_GpuShader *VertexShader = NULL;
    SDL_GpuShader *FragmentShader = NULL;
    SDL_GpuBuffer *VertexBuffer = NULL;
    SDL_GpuBuffer *IndexBuffer = NULL;
    uint32_t VertexBufferSize = 0;
    uint32_t IndexBufferSize = 0;
    SDL_GpuSampler *FontSampler = NULL;
    SDL_GpuTexture *FontImage = NULL;
    SDL_GpuTextureSamplerBinding FontTextureSamplerBindings = {};
};

struct ImGui_ImplSDLGpu_ViewportData {
    SDL_Window *Window;
    bool WindowOwned;
};

static void ImGui_ImplSDLGpu_InitPlatformInterface();

static void ImGui_ImplSDLGpu_ShutdownPlatformInterface();

static ImGui_ImplSDLGpu_Data *ImGui_ImplSDLGpu_GetBackendData() {
    return ImGui::GetCurrentContext() ? (ImGui_ImplSDLGpu_Data *) ImGui::GetIO().BackendRendererUserData : nullptr;
}

static void ImGui_ImplSDLGpu_SetupRenderState(ImDrawData *draw_data, SDL_GpuRenderPass *renderPass, int fb_width,
                                              int fb_height, SDL_GpuGraphicsPipeline **pipeline = nullptr) {
    ImGui_ImplSDLGpu_Data *bd = ImGui_ImplSDLGpu_GetBackendData();

    if (!pipeline) {
        SDL_GpuBindGraphicsPipeline(renderPass, bd->Pipeline);
    } else {
        SDL_GpuBindGraphicsPipeline(renderPass, *pipeline);
    }

    if (draw_data->TotalVtxCount > 0) {
        SDL_GpuBufferBinding vertexBufferBinding[1] = {SDL_GpuBufferBinding{bd->VertexBuffer, 0}};
        SDL_GpuBufferBinding indexBufferBinding = {bd->IndexBuffer, 0};
        SDL_GpuBindVertexBuffers(renderPass, 0, vertexBufferBinding, 1);
        SDL_GpuBindIndexBuffer(renderPass, &indexBufferBinding,
                               sizeof(ImDrawIdx) == 2
                                   ? SDL_GPU_INDEXELEMENTSIZE_16BIT
                                   : SDL_GPU_INDEXELEMENTSIZE_32BIT);
    }

    SDL_GpuViewport viewport;
    viewport.x = 0;
    viewport.y = 0;
    viewport.w = fb_width;
    viewport.h = fb_height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    SDL_GpuSetViewport(renderPass, &viewport);

    struct UniformBuffer {
        float scale[2];
        float translation[2];
    } ubo;

    ubo.scale[0] = 2.0f / draw_data->DisplaySize.x;
    ubo.scale[1] = 2.0f / draw_data->DisplaySize.y;
    ubo.translation[0] = -1.0f - draw_data->DisplayPos.x * ubo.scale[0];
    ubo.translation[1] = -1.0f - draw_data->DisplayPos.y * ubo.scale[1];
    SDL_GpuPushVertexUniformData(renderPass, 0, (void *) &ubo, sizeof(ubo));
}

void ImGui_ImplSDLGpu_RenderDrawData(ImDrawData *draw_data, SDL_GpuRenderPass *renderPass,
                                     SDL_GpuGraphicsPipeline **pipeline) {
    int fb_width = (int) (draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    int fb_height = (int) (draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
    if (fb_width <= 0 || fb_height <= 0) {
        return;
    }

    ImGui_ImplSDLGpu_Data *bd = ImGui_ImplSDLGpu_GetBackendData();
    ImGui_ImplSDLGpu_InitInfo *v = &bd->SDLGpuInitInfo;

    if (draw_data->TotalVtxCount > 0) {
        uint32_t vertexBufferSize = draw_data->TotalVtxCount * sizeof(ImDrawVert);
        uint32_t indexBufferSize = draw_data->TotalIdxCount * sizeof(ImDrawIdx);

        if (bd->VertexBuffer == NULL) {
            bd->VertexBuffer = SDL_GpuCreateBuffer(v->Device, SDL_GPU_BUFFERUSAGE_VERTEX_BIT, vertexBufferSize);
            bd->VertexBufferSize = vertexBufferSize;
        }
        if (bd->IndexBuffer == NULL) {
            bd->IndexBuffer = SDL_GpuCreateBuffer(v->Device, SDL_GPU_BUFFERUSAGE_INDEX_BIT, indexBufferSize);
            bd->IndexBufferSize = indexBufferSize;
        }

        if (bd->VertexBuffer != NULL || bd->VertexBufferSize < vertexBufferSize) {
            SDL_GpuReleaseBuffer(v->Device, bd->VertexBuffer);
            SDL_GpuWait(v->Device);
            bd->VertexBuffer = SDL_GpuCreateBuffer(v->Device, SDL_GPU_BUFFERUSAGE_VERTEX_BIT, vertexBufferSize);
            bd->VertexBufferSize = vertexBufferSize;
        }
        if (bd->IndexBuffer != NULL || bd->IndexBufferSize < indexBufferSize) {
            SDL_GpuReleaseBuffer(v->Device, bd->IndexBuffer);
            SDL_GpuWait(v->Device);
            bd->IndexBuffer = SDL_GpuCreateBuffer(v->Device, SDL_GPU_BUFFERUSAGE_INDEX_BIT, indexBufferSize);
            bd->IndexBufferSize = indexBufferSize;
        }


        // Upload vertex/index data into a single contiguous GPU buffer
        ImDrawVert *vtx_dst = nullptr;
        ImDrawIdx *idx_dst = nullptr;

        SDL_GpuCommandBuffer *copyCommandBuffer = SDL_GpuAcquireCommandBuffer(v->Device);

        SDL_GpuCopyPass *vertexCopyPass = SDL_GpuBeginCopyPass(copyCommandBuffer);
        SDL_GpuBufferCopy vertexBufferCopy;
        vertexBufferCopy.size = vertexBufferSize;
        vertexBufferCopy.srcOffset = 0;
        vertexBufferCopy.dstOffset = 0;


        SDL_GpuTransferBuffer *vertexTransferBuffer = SDL_GpuCreateTransferBuffer(v->Device,
            SDL_GPU_TRANSFERUSAGE_BUFFER,
            SDL_GPU_TRANSFER_MAP_WRITE,
            vertexBufferSize);

        SDL_GpuMapTransferBuffer(v->Device, vertexTransferBuffer, SDL_FALSE, (void **) &vtx_dst);
        for (int n = 0; n < draw_data->CmdListsCount; n++) {
            const ImDrawList *cmd_list = draw_data->CmdLists[n];
            memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
            vtx_dst += cmd_list->VtxBuffer.Size;
        }
        SDL_GpuUnmapTransferBuffer(v->Device, vertexTransferBuffer);
        SDL_GpuUploadToBuffer(vertexCopyPass, vertexTransferBuffer, bd->VertexBuffer, &vertexBufferCopy, SDL_FALSE);
        SDL_GpuEndCopyPass(vertexCopyPass);

        SDL_GpuCopyPass *indexCopyPass = SDL_GpuBeginCopyPass(copyCommandBuffer);

        SDL_GpuBufferCopy indexBufferCopy = {};
        indexBufferCopy.size = indexBufferSize;
        indexBufferCopy.srcOffset = 0;
        indexBufferCopy.dstOffset = 0;

        SDL_GpuTransferBuffer *indexTransferBuffer = SDL_GpuCreateTransferBuffer(v->Device,
            SDL_GPU_TRANSFERUSAGE_BUFFER,
            SDL_GPU_TRANSFER_MAP_WRITE,
            indexBufferSize);

        SDL_GpuMapTransferBuffer(v->Device, indexTransferBuffer, SDL_FALSE, (void **) &idx_dst);
        for (int n = 0; n < draw_data->CmdListsCount; n++) {
            const ImDrawList *cmd_list = draw_data->CmdLists[n];
            memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
            idx_dst += cmd_list->IdxBuffer.Size;
        }
        SDL_GpuUnmapTransferBuffer(v->Device, indexTransferBuffer);
        SDL_GpuUploadToBuffer(indexCopyPass, indexTransferBuffer, bd->IndexBuffer, &indexBufferCopy, SDL_FALSE);
        SDL_GpuEndCopyPass(indexCopyPass);

        SDL_GpuSubmit(copyCommandBuffer);
        SDL_GpuReleaseTransferBuffer(v->Device, vertexTransferBuffer);
        SDL_GpuReleaseTransferBuffer(v->Device, indexTransferBuffer);
    }
    if (!pipeline) {
        ImGui_ImplSDLGpu_SetupRenderState(draw_data, renderPass, fb_width, fb_height, &bd->Pipeline);
    } else {
        ImGui_ImplSDLGpu_SetupRenderState(draw_data, renderPass, fb_width, fb_height, pipeline);
    }

    // Will project scissor/clipping rectangles into framebuffer space
    ImVec2 clip_off = draw_data->DisplayPos; // (0,0) unless using multi-viewports
    ImVec2 clip_scale = draw_data->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

    int global_vtx_offset = 0;
    int global_idx_offset = 0;
    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList *cmd_list = draw_data->CmdLists[n];
        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
            const ImDrawCmd *pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != nullptr) {
                // User callback, registered via ImDrawList::AddCallback()
                // (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
                if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
                    ImGui_ImplSDLGpu_SetupRenderState(draw_data, renderPass, fb_width, fb_height);
                else
                    pcmd->UserCallback(cmd_list, pcmd);
            } else {
                // Project scissor/clipping rectangles into framebuffer space
                ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x,
                                (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
                ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x,
                                (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);


                if (clip_min.x < 0.0f) { clip_min.x = 0.0f; }
                if (clip_min.y < 0.0f) { clip_min.y = 0.0f; }
                if (clip_max.x > fb_width) { clip_max.x = (float) fb_width; }
                if (clip_max.y > fb_height) { clip_max.y = (float) fb_height; }
                if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y) {
                    continue;
                }

                SDL_GpuRect scissor;
                scissor.x = (int32_t) clip_min.x;
                scissor.y = (int32_t) clip_min.y;
                scissor.w = (uint32_t) (clip_max.x - clip_min.x);
                scissor.h = (uint32_t) (clip_max.y - clip_min.y);

                SDL_GpuSetScissor(renderPass, &scissor);
                SDL_GpuBindFragmentSamplers(renderPass, 0, (SDL_GpuTextureSamplerBinding *) pcmd->TextureId, 1);
                SDL_GpuDrawIndexedPrimitives(renderPass, pcmd->VtxOffset + global_vtx_offset,pcmd->IdxOffset + global_idx_offset, pcmd->ElemCount / 3, 1);
            }
        }
        global_idx_offset += cmd_list->IdxBuffer.Size;
        global_vtx_offset += cmd_list->VtxBuffer.Size;
    }

    SDL_GpuRect scissor = {0, 0, fb_width, fb_height};
    SDL_GpuSetScissor(renderPass, &scissor);
}

bool ImGui_ImplSDLGpu_CreateFontsTexture() {
    ImGuiIO &io = ImGui::GetIO();
    ImGui_ImplSDLGpu_Data *bd = ImGui_ImplSDLGpu_GetBackendData();
    ImGui_ImplSDLGpu_InitInfo *v = &bd->SDLGpuInitInfo;

    unsigned char *pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    SDL_GpuTextureCreateInfo textureInfo = {};
    textureInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8;
    textureInfo.width = width;
    textureInfo.height = height;
    textureInfo.depth = 1;
    textureInfo.isCube = SDL_FALSE;
    textureInfo.layerCount = 1;
    textureInfo.levelCount = 1;
    textureInfo.usageFlags = SDL_GPU_TEXTUREUSAGE_SAMPLER_BIT;
    bd->FontImage = SDL_GpuCreateTexture(v->Device, &textureInfo);


    SDL_GpuTransferBuffer *imageDataTransferBuffer = SDL_GpuCreateTransferBuffer(
        v->Device,
        SDL_GPU_TRANSFERUSAGE_TEXTURE,
        SDL_GPU_TRANSFER_MAP_READ,
        sizeof(unsigned char) * 4 * width * height
    );

    SDL_GpuBufferCopy copy;
    copy.size = sizeof(unsigned char) * 4 * width * height;
    copy.dstOffset = 0;
    copy.srcOffset = 0;
    SDL_GpuSetTransferData(
        v->Device,
        pixels,
        imageDataTransferBuffer,
        &copy,
        SDL_FALSE
    );

    SDL_GpuTextureRegion region = {};
    region.w = width;
    region.h = height;
    region.d = 1;
    region.textureSlice.texture = bd->FontImage;
    SDL_GpuBufferImageCopy imageCopy = {};
    imageCopy.bufferOffset = 0;

    SDL_GpuCommandBuffer *uploadCmdBuf = SDL_GpuAcquireCommandBuffer(v->Device);
    SDL_GpuCopyPass *copyPass = SDL_GpuBeginCopyPass(uploadCmdBuf);

    SDL_GpuUploadToTexture(copyPass, imageDataTransferBuffer, &region, &imageCopy, SDL_FALSE);
    SDL_GpuEndCopyPass(copyPass);
    SDL_GpuSubmit(uploadCmdBuf);
    bd->FontTextureSamplerBindings.texture = bd->FontImage;
    SDL_GpuReleaseTransferBuffer(v->Device, imageDataTransferBuffer);
    io.Fonts->SetTexID((ImTextureID) &bd->FontTextureSamplerBindings);

    return true;
}

void ImGui_ImplSDLGpu_DestroyFontsTexture() {
    ImGuiIO &io = ImGui::GetIO();
    ImGui_ImplSDLGpu_Data *bd = ImGui_ImplSDLGpu_GetBackendData();
    ImGui_ImplSDLGpu_InitInfo *v = &bd->SDLGpuInitInfo;

    if (bd->FontImage) { SDL_GpuReleaseTexture(v->Device, bd->FontImage); }
}

static void ImGui_ImplSDLGpu_CreateShaderModules(SDL_GpuDevice *device) {
    ImGui_ImplSDLGpu_Data *bd = ImGui_ImplSDLGpu_GetBackendData();
    if (!bd->VertexShader || !bd->FragmentShader) {
        SDL_GpuShaderCreateInfo vertexShaderInfo;
        SDL_GpuShaderCreateInfo fragmentShaderInfo;

        switch (SDL_GpuGetBackend(bd->SDLGpuInitInfo.Device)) {
            case SDL_GPU_BACKEND_D3D11:
                vertexShaderInfo.code = shader_vert_dxcb;
                vertexShaderInfo.codeSize = sizeof(shader_vert_dxcb);
                vertexShaderInfo.format = SDL_GPU_SHADERFORMAT_DXBC;
                vertexShaderInfo.entryPointName = "main";
                fragmentShaderInfo.code = shader_frag_dxcb;
                fragmentShaderInfo.codeSize = sizeof(shader_frag_dxcb);
                fragmentShaderInfo.format = SDL_GPU_SHADERFORMAT_DXBC;
                 fragmentShaderInfo.entryPointName = "main";
                break;
            case SDL_GPU_BACKEND_VULKAN:
                vertexShaderInfo.code = shader_vert_spv;
                vertexShaderInfo.codeSize = sizeof(shader_vert_spv);
                vertexShaderInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
                vertexShaderInfo.entryPointName = "main";
                fragmentShaderInfo.code = shader_frag_spv;
                fragmentShaderInfo.codeSize = sizeof(shader_frag_spv);
                fragmentShaderInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
                fragmentShaderInfo.entryPointName = "main";
                break;
            case SDL_GPU_BACKEND_METAL:
                vertexShaderInfo.code = shader_vert_metallib;
                vertexShaderInfo.codeSize = sizeof(shader_vert_metallib);
                vertexShaderInfo.format = SDL_GPU_SHADERFORMAT_METALLIB;
                vertexShaderInfo.entryPointName = "main0";
                fragmentShaderInfo.code = shader_frag_metallib;
                fragmentShaderInfo.codeSize = sizeof(shader_frag_metallib);
                fragmentShaderInfo.format = SDL_GPU_SHADERFORMAT_METALLIB;
                fragmentShaderInfo.entryPointName = "main0";
                break;
        }


        vertexShaderInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        bd->VertexShader = SDL_GpuCreateShader(device, &vertexShaderInfo);
        fragmentShaderInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        bd->FragmentShader = SDL_GpuCreateShader(device, &fragmentShaderInfo);
    }
}

static void ImGui_ImplSDLGpu_CreatePipeline(SDL_GpuDevice *device, SDL_GpuSampleCount MSAASamples,
                                            SDL_GpuGraphicsPipeline **pipeline) {
    ImGui_ImplSDLGpu_Data *bd = ImGui_ImplSDLGpu_GetBackendData();
    ImGui_ImplSDLGpu_CreateShaderModules(device);
    SDL_GpuVertexBinding vertexBinding[1];
    vertexBinding[0].binding = 0;
    vertexBinding[0].inputRate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vertexBinding[0].stride = sizeof(ImDrawVert);
    vertexBinding[0].stepRate = 0;

    SDL_GpuVertexAttribute vertexAttributes[3];
    vertexAttributes[0].binding = 0;
    vertexAttributes[0].location = 0;
    vertexAttributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_VECTOR2;
    vertexAttributes[0].offset = offsetof(ImDrawVert, pos);

    vertexAttributes[1].binding = 0;
    vertexAttributes[1].location = 1;
    vertexAttributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_VECTOR2;
    vertexAttributes[1].offset = offsetof(ImDrawVert, uv);

    vertexAttributes[2].binding = 0;
    vertexAttributes[2].location = 2;
    vertexAttributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_COLOR;
    vertexAttributes[2].offset = offsetof(ImDrawVert, col);


    SDL_GpuVertexInputState vertexInputState;
    vertexInputState.vertexAttributeCount = 3;
    vertexInputState.vertexAttributes = vertexAttributes;
    vertexInputState.vertexBindingCount = 1;
    vertexInputState.vertexBindings = vertexBinding;

    SDL_GpuRasterizerState rasterizerState = {};
    rasterizerState.cullMode = SDL_GPU_CULLMODE_NONE;
    rasterizerState.frontFace = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    rasterizerState.depthBiasEnable = SDL_FALSE;

    SDL_GpuMultisampleState multisampleState = {};
    multisampleState.multisampleCount = (MSAASamples != 0) ? MSAASamples : SDL_GPU_SAMPLECOUNT_1;
    multisampleState.sampleMask = UINT32_MAX;

    SDL_GpuColorAttachmentBlendState blendState = {};
    blendState.blendEnable = SDL_TRUE;
    blendState.srcColorBlendFactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    blendState.dstColorBlendFactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blendState.colorBlendOp = SDL_GPU_BLENDOP_ADD;
    blendState.srcAlphaBlendFactor = SDL_GPU_BLENDFACTOR_ONE;
    blendState.dstAlphaBlendFactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blendState.alphaBlendOp = SDL_GPU_BLENDOP_ADD;
    blendState.colorWriteMask = SDL_GPU_COLORCOMPONENT_R_BIT | SDL_GPU_COLORCOMPONENT_G_BIT |
                                SDL_GPU_COLORCOMPONENT_B_BIT | SDL_GPU_COLORCOMPONENT_A_BIT;

    SDL_GpuColorAttachmentDescription attachmentDesc[1];
    attachmentDesc[0].format = SDL_GpuGetSwapchainTextureFormat(device, bd->SDLGpuInitInfo.Window);
    attachmentDesc[0].blendState = blendState;


    SDL_GpuGraphicsPipelineAttachmentInfo attachmentInfo = {};
    attachmentInfo.colorAttachmentCount = 1;
    attachmentInfo.colorAttachmentDescriptions = attachmentDesc;
    attachmentInfo.hasDepthStencilAttachment = SDL_FALSE;


    SDL_GpuGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.vertexShader = bd->VertexShader;
    pipelineInfo.fragmentShader = bd->FragmentShader;
    pipelineInfo.vertexInputState = vertexInputState;
    pipelineInfo.primitiveType = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipelineInfo.rasterizerState = rasterizerState;
    pipelineInfo.multisampleState = multisampleState;
    pipelineInfo.depthStencilState = {};
    pipelineInfo.attachmentInfo = attachmentInfo;

    pipelineInfo.vertexResourceInfo.samplerCount = 0;
    pipelineInfo.vertexResourceInfo.storageTextureCount = 0;
    pipelineInfo.vertexResourceInfo.storageBufferCount = 0;
    pipelineInfo.vertexResourceInfo.uniformBufferCount = 1;

    pipelineInfo.fragmentResourceInfo.samplerCount = 1;
    pipelineInfo.fragmentResourceInfo.storageTextureCount = 0;
    pipelineInfo.fragmentResourceInfo.storageBufferCount = 0;
    pipelineInfo.fragmentResourceInfo.uniformBufferCount = 0;

    pipelineInfo.blendConstants[0] = 0;
    pipelineInfo.blendConstants[1] = 0;
    pipelineInfo.blendConstants[2] = 0;
    pipelineInfo.blendConstants[3] = 0;

    *pipeline = SDL_GpuCreateGraphicsPipeline(device, &pipelineInfo);
}

bool ImGui_ImplSDLGpu_CreateDeviceObjects() {
    ImGui_ImplSDLGpu_Data *bd = ImGui_ImplSDLGpu_GetBackendData();
    ImGui_ImplSDLGpu_InitInfo *v = &bd->SDLGpuInitInfo;

    SDL_GpuSamplerCreateInfo samplerInfo = {};
    samplerInfo.magFilter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.minFilter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.addressModeU = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    samplerInfo.addressModeV = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    samplerInfo.addressModeW = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    samplerInfo.minLod = -1000;
    samplerInfo.maxLod = 1000;
    samplerInfo.maxAnisotropy = 1.0f;
    bd->FontSampler = SDL_GpuCreateSampler(v->Device, &samplerInfo);
    bd->FontTextureSamplerBindings.sampler = bd->FontSampler;
    ImGui_ImplSDLGpu_CreatePipeline(v->Device, v->MSAASamples, &bd->Pipeline);
    return true;
}

void ImGui_ImplSDLGpu_DestroyDeviceObjects() {
    ImGui_ImplSDLGpu_Data *bd = ImGui_ImplSDLGpu_GetBackendData();
    ImGui_ImplSDLGpu_InitInfo *v = &bd->SDLGpuInitInfo;
    ImGui_ImplSDLGpu_DestroyFontsTexture();

    if (bd->Pipeline) { SDL_GpuReleaseGraphicsPipeline(v->Device, bd->Pipeline); }
    if (bd->SecondaryPipeline) { SDL_GpuReleaseGraphicsPipeline(v->Device, bd->SecondaryPipeline); }
    if (bd->VertexShader) { SDL_GpuReleaseShader(v->Device, bd->VertexShader); }
    if (bd->FragmentShader) { SDL_GpuReleaseShader(v->Device, bd->FragmentShader); }
    if (bd->FontSampler) { SDL_GpuReleaseSampler(v->Device, bd->FontSampler); }
    if (bd->VertexBuffer) { SDL_GpuReleaseBuffer(v->Device, bd->VertexBuffer); }
    if (bd->IndexBuffer) { SDL_GpuReleaseBuffer(v->Device, bd->IndexBuffer); }
}

bool ImGui_ImplSDLGpu_Init(ImGui_ImplSDLGpu_InitInfo *info) {
    ImGuiIO &io = ImGui::GetIO();
    IMGUI_CHECKVERSION();
    IM_ASSERT(io.BackendRendererUserData == nullptr && "Already initialized a renderer backend!");

    // Setup backend capabilities flags
    ImGui_ImplSDLGpu_Data *bd = IM_NEW(ImGui_ImplSDLGpu_Data)();
    io.BackendRendererUserData = (void *) bd;
    io.BackendRendererName = "imgui_impl_sdlgpu";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;
    IM_ASSERT(info->Device != NULL);
    IM_ASSERT(info->Window != NULL);

    bd->SDLGpuInitInfo = *info;

    ImGui_ImplSDLGpu_CreateDeviceObjects();

    ImGuiViewport *main_viewport = ImGui::GetMainViewport();
    main_viewport->RendererUserData = IM_NEW(ImGui_ImplSDLGpu_ViewportData)();

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui_ImplSDLGpu_InitPlatformInterface();
    }


    return true;
}

void ImGui_ImplSDLGpu_Shutdown() {
    ImGui_ImplSDLGpu_Data *bd = ImGui_ImplSDLGpu_GetBackendData();
    IM_ASSERT(bd != nullptr && "No renderer backend to shutdown, or already shutdown?");
    ImGuiIO &io = ImGui::GetIO();

    ImGui_ImplSDLGpu_DestroyDeviceObjects();

    ImGuiViewport *main_viewport = ImGui::GetMainViewport();
    if (ImGui_ImplSDLGpu_ViewportData *vd = (ImGui_ImplSDLGpu_ViewportData *) main_viewport->RendererUserData) {
        IM_DELETE(vd);
    }
    main_viewport->RendererUserData = nullptr;

    ImGui_ImplSDLGpu_ShutdownPlatformInterface();

    io.BackendRendererName = nullptr;
    io.BackendRendererUserData = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasViewports);

    IM_DELETE(bd);
}

void ImGui_ImplSDLGpu_NewFrame() {
    ImGui_ImplSDLGpu_Data *bd = ImGui_ImplSDLGpu_GetBackendData();
    if (!bd->FontImage) {
        ImGui_ImplSDLGpu_CreateFontsTexture();
    }
}


static void ImGui_ImplSDLGpu_CreateWindow(ImGuiViewport *viewport) {
    ImGui_ImplSDLGpu_Data *bd = ImGui_ImplSDLGpu_GetBackendData();
    ImGui_ImplSDLGpu_InitInfo *v = &bd->SDLGpuInitInfo;
    ImGui_ImplSDLGpu_ViewportData *vd = IM_NEW(ImGui_ImplSDLGpu_ViewportData)();
    vd->Window = (SDL_Window *) viewport->PlatformHandle;
    vd->WindowOwned = true;
    viewport->RendererUserData = vd;
    SDL_GpuClaimWindow(v->Device, vd->Window, v->SwapchainComposition, v->WindowPresentMode);

    if (!bd->SecondaryPipeline) {
        ImGui_ImplSDLGpu_CreatePipeline(v->Device, v->MSAASamples, &bd->SecondaryPipeline);
    }
}

static void ImGui_ImplSDLGpu_DestroyWindow(ImGuiViewport *viewport) {
    ImGui_ImplSDLGpu_Data *bd = ImGui_ImplSDLGpu_GetBackendData();

    if (ImGui_ImplSDLGpu_ViewportData *vd = (ImGui_ImplSDLGpu_ViewportData *) viewport->RendererUserData) {
        ImGui_ImplSDLGpu_InitInfo *v = &bd->SDLGpuInitInfo;
        if (vd->WindowOwned) {
            SDL_GpuWait(v->Device);
            SDL_GpuUnclaimWindow(v->Device, vd->Window);
        }
        IM_DELETE(vd);
    }
    viewport->RendererUserData = nullptr;
}

static void ImGui_ImplSDLGpu_RenderWindow(ImGuiViewport *viewport, void *) {
    ImGui_ImplSDLGpu_Data *bd = ImGui_ImplSDLGpu_GetBackendData();
    ImGui_ImplSDLGpu_ViewportData *vd = (ImGui_ImplSDLGpu_ViewportData *) viewport->RendererUserData;
    if (!vd->Window) {
        ImGui_ImplSDLGpu_CreateWindow(viewport);
    }

    ImGui_ImplSDLGpu_InitInfo *v = &bd->SDLGpuInitInfo;

    SDL_GpuCommandBuffer *cmdbuf = SDL_GpuAcquireCommandBuffer(v->Device);
    Uint32 w, h;
    SDL_GpuTexture *swapchainTexture = SDL_GpuAcquireSwapchainTexture(cmdbuf, vd->Window, &w, &h);

    if (swapchainTexture != NULL) {
        SDL_GpuColorAttachmentInfo colorAttachmentInfo = {};
        colorAttachmentInfo.textureSlice.texture = swapchainTexture;
        colorAttachmentInfo.clearColor = SDL_GpuColor{0.16f, 0.16f, 0.16f, 1.0f};
        colorAttachmentInfo.loadOp = SDL_GPU_LOADOP_CLEAR;
        colorAttachmentInfo.storeOp = SDL_GPU_STOREOP_STORE;

        SDL_GpuRenderPass *renderPass = SDL_GpuBeginRenderPass(cmdbuf, &colorAttachmentInfo, 1, NULL);
        ImGui_ImplSDLGpu_RenderDrawData(viewport->DrawData, renderPass, &bd->SecondaryPipeline);
        SDL_GpuEndRenderPass(renderPass);
    }
    SDL_GpuSubmit(cmdbuf);
}

void ImGui_ImplSDLGpu_InitPlatformInterface() {
    ImGuiPlatformIO &platform_io = ImGui::GetPlatformIO();
    platform_io.Renderer_CreateWindow = ImGui_ImplSDLGpu_CreateWindow;
    platform_io.Renderer_DestroyWindow = ImGui_ImplSDLGpu_DestroyWindow;
    platform_io.Renderer_RenderWindow = ImGui_ImplSDLGpu_RenderWindow;
}

void ImGui_ImplSDLGpu_ShutdownPlatformInterface() {
    ImGui::DestroyPlatformWindows();
}


#endif
