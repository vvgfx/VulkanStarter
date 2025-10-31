﻿#include "vk_engine.h"
#include "SDL_events.h"
#include "SDL_scancode.h"
#include "fmt/base.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <chrono>
#include <cmath>
// #include <cstddef>
#include <cstdint>
// #include <cstdio>
// #include <string>
#include <thread>

// ---- other includes ----
#include "VkBootstrap.h"
#include "vk_images.h"
#include "vk_loader.h"
#include <glm/gtx/transform.hpp>
#include <vk_images.h>
#include <vk_initializers.h>
#include <vk_types.h>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-completeness"
#endif
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <vk_pipelines.h>
// imgui
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"

VulkanEngine *loadedEngine = nullptr;

VulkanEngine &VulkanEngine::Get()
{
    return *loadedEngine;
}
void VulkanEngine::init()
{
    // only one engine initialization is allowed with the application.
    assert(loadedEngine == nullptr);
    loadedEngine = this;

    // We initialize SDL and create a window with it.
    SDL_Init(SDL_INIT_VIDEO);

    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

    _window = SDL_CreateWindow("Vulkan Engine", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, _windowExtent.width,
                               _windowExtent.height, window_flags);

    init_vulkan();

    init_swapchain();

    init_commands();

    init_sync_structures();

    init_descriptors();

    init_pipelines();

    init_default_data();

    init_imgui();

    // everything went fine
    _isInitialized = true;
}

void VulkanEngine::init_vulkan()
{
    vkb::InstanceBuilder builder;

    auto instRet = builder.set_app_name("Vulkan Engine")
                       .request_validation_layers(true)
                       .use_default_debug_messenger()
                       .require_api_version(1, 3, 0)
                       .build();

    vkb::Instance vkbInst = instRet.value();
    _instance = vkbInst.instance;
    _debugMessenger = vkbInst.debug_messenger;

    SDL_Vulkan_CreateSurface(_window, _instance, &_surface);

    // vulkan 1.3 features
    VkPhysicalDeviceVulkan13Features features{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
                                              .synchronization2 = true,
                                              .dynamicRendering = true};

    VkPhysicalDeviceVulkan12Features features12{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
                                                .descriptorIndexing = true,
                                                .bufferDeviceAddress = true};

    vkb::PhysicalDeviceSelector selector{vkbInst};

    vkb::PhysicalDevice PhysicalDevice = selector.set_minimum_version(1, 3)
                                             .set_required_features_13(features)
                                             .set_required_features_12(features12)
                                             .set_surface(_surface)
                                             .select()
                                             .value();

    vkb::DeviceBuilder DeviceBuilder{PhysicalDevice};

    vkb::Device vkbDevice = DeviceBuilder.build().value();

    _device = vkbDevice.device;
    _chosenGPU = PhysicalDevice.physical_device;
    _graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    _graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = _chosenGPU;
    allocatorInfo.device = _device;
    allocatorInfo.instance = _instance;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    vmaCreateAllocator(&allocatorInfo, &_allocator);

    _mainDeletionQueue.push_function([&]() { vmaDestroyAllocator(_allocator); });
}

void VulkanEngine::init_descriptors()
{
    // create a descriptor pool that will hold 10 sets with 1 image each
    std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes = {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
                                                                     {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};

    globalDescriptorAllocator.init(_device, 10, sizes);

    // make the descriptor set layout for our compute draw
    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        _drawImageDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT);
    }

    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        _gpuSceneDataDescriptorLayout =
            builder.build(_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    }

    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        _singleImageDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_FRAGMENT_BIT);
    }

    // allocate a descriptor set for our draw image
    _drawImageDescriptors = globalDescriptorAllocator.allocate(_device, _drawImageDescriptorLayout);

    // old way of creating descriptor sets ----------------
    // VkDescriptorImageInfo imgInfo{};
    // imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    // imgInfo.imageView = _drawImage.imageView;

    // VkWriteDescriptorSet drawImageWrite = {};
    // drawImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    // drawImageWrite.pNext = nullptr;

    // drawImageWrite.dstBinding = 0;
    // drawImageWrite.dstSet = _drawImageDescriptors;
    // drawImageWrite.descriptorCount = 1;
    // drawImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    // drawImageWrite.pImageInfo = &imgInfo;

    // vkUpdateDescriptorSets(_device, 1, &drawImageWrite, 0, nullptr);
    // old way end ----------------

    DescriptorWriter writer;
    writer.write_image(0, _drawImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL,
                       VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

    writer.update_set(_device, _drawImageDescriptors);

    // global descriptor sets
    for (int i = 0; i < FRAME_OVERLAP; i++)
    {
        // create a descriptor pool
        std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> frame_sizes = {
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4},
        };

        _frames[i]._frameDescriptors = DescriptorAllocatorGrowable{};
        _frames[i]._frameDescriptors.init(_device, 1000, frame_sizes);

        _mainDeletionQueue.push_function([&, i]() { _frames[i]._frameDescriptors.destroy_pools(_device); });
    }

    // make sure both the descriptor allocator and the new layout get cleaned up properly
    _mainDeletionQueue.push_function(
        [&]()
        {
            globalDescriptorAllocator.destroy_pools(_device);

            vkDestroyDescriptorSetLayout(_device, _drawImageDescriptorLayout, nullptr);
        });
}

void VulkanEngine::init_pipelines()
{
    // compute
    init_background_pipelines();

    // graphics
    init_mesh_pipeline();

    // build materials
    metalRoughMaterial.build_pipelines(this);
}

void VulkanEngine::init_background_pipelines()
{
    VkPipelineLayoutCreateInfo computeLayout{};
    computeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    computeLayout.pNext = nullptr;
    computeLayout.pSetLayouts = &_drawImageDescriptorLayout;
    computeLayout.setLayoutCount = 1;

    VkPushConstantRange pushConstant{};
    pushConstant.offset = 0;
    pushConstant.size = sizeof(ComputePushConstants);
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    computeLayout.pPushConstantRanges = &pushConstant;
    computeLayout.pushConstantRangeCount = 1;

    VK_CHECK(vkCreatePipelineLayout(_device, &computeLayout, nullptr, &_gradientPipelineLayout));

    VkShaderModule gradientShader;
    if (!vkutil::load_shader_module("../shaders/gradient_color.comp.spv", _device, &gradientShader))
        fmt::print("Error when building the compute shader \n");

    VkShaderModule skyShader;
    if (!vkutil::load_shader_module("../shaders/sky.comp.spv", _device, &skyShader))
        fmt::print("Error when building the compute shader \n");

    VkPipelineShaderStageCreateInfo stageinfo{};
    stageinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageinfo.pNext = nullptr;
    stageinfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageinfo.module = gradientShader;
    stageinfo.pName = "main";

    VkComputePipelineCreateInfo computePipelineCreateInfo{};
    computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    computePipelineCreateInfo.pNext = nullptr;
    computePipelineCreateInfo.layout = _gradientPipelineLayout;
    computePipelineCreateInfo.stage = stageinfo;

    ComputeEffect gradient;
    gradient.layout = _gradientPipelineLayout;
    gradient.name = "gradient";
    gradient.data = {};

    gradient.data.data1 = glm::vec4(1, 0, 0, 1);
    gradient.data.data2 = glm::vec4(0, 0, 1, 1);

    VK_CHECK(
        vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &gradient.pipeline));

    // change the shader module only to create the sky shader
    computePipelineCreateInfo.stage.module = skyShader;

    ComputeEffect sky;
    sky.layout = _gradientPipelineLayout;
    sky.name = "sky";
    sky.data = {};
    // default sky parameters
    sky.data.data1 = glm::vec4(0.1, 0.2, 0.4, 0.97);

    VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &sky.pipeline));

    // add the 2 background effects into the array
    backgroundEffects.push_back(gradient);
    backgroundEffects.push_back(sky);

    // destroy structures properly
    vkDestroyShaderModule(_device, gradientShader, nullptr);
    vkDestroyShaderModule(_device, skyShader, nullptr);
    _mainDeletionQueue.push_function(
        [=, this]()
        {
            vkDestroyPipelineLayout(_device, _gradientPipelineLayout, nullptr);
            vkDestroyPipeline(_device, sky.pipeline, nullptr);
            vkDestroyPipeline(_device, gradient.pipeline, nullptr);
        });
}

void VulkanEngine::create_swapchain(uint32_t width, uint32_t height)
{
    vkb::SwapchainBuilder SwapchainBuilder{_chosenGPU, _device, _surface};

    _swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

    vkb::Swapchain vkbSwapchain =
        SwapchainBuilder
            .set_desired_format(
                VkSurfaceFormatKHR{.format = _swapchainImageFormat, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
            .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
            .set_desired_extent(width, height)
            .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
            .build()
            .value();

    _swapchainExtent = vkbSwapchain.extent;
    _swapchain = vkbSwapchain.swapchain;
    _swapchainImages = vkbSwapchain.get_images().value();
    _swapchainImageViews = vkbSwapchain.get_image_views().value();
}

void VulkanEngine::init_swapchain()
{
    create_swapchain(_windowExtent.width, _windowExtent.height);

    // draw image size will match the window
    VkExtent3D drawImageExtent = {_windowExtent.width, _windowExtent.height, 1};

    // hardcoding the draw format to 32 bit float
    _drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    _drawImage.imageExtent = drawImageExtent;

    VkImageUsageFlags drawImageUsages{};
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    VkImageCreateInfo rimg_info = vkinit::image_create_info(_drawImage.imageFormat, drawImageUsages, drawImageExtent);

    // for the draw image, we want to allocate it from gpu local memory
    VmaAllocationCreateInfo rimg_allocinfo = {};
    rimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    rimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // allocate and create the image
    vmaCreateImage(_allocator, &rimg_info, &rimg_allocinfo, &_drawImage.image, &_drawImage.allocation, nullptr);

    // build a image-view for the draw image to use for rendering
    VkImageViewCreateInfo rview_info =
        vkinit::imageview_create_info(_drawImage.imageFormat, _drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);

    VK_CHECK(vkCreateImageView(_device, &rview_info, nullptr, &_drawImage.imageView));

    // similarly, create the depth image

    _depthImage.imageFormat = VK_FORMAT_D32_SFLOAT;
    _depthImage.imageExtent = drawImageExtent;
    VkImageUsageFlags depthImageUsages{};
    depthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    VkImageCreateInfo dimg_info = vkinit::image_create_info(_depthImage.imageFormat, depthImageUsages, drawImageExtent);

    // allocate and create the image
    vmaCreateImage(_allocator, &dimg_info, &rimg_allocinfo, &_depthImage.image, &_depthImage.allocation, nullptr);

    // build a image-view for the draw image to use for rendering
    VkImageViewCreateInfo dview_info =
        vkinit::imageview_create_info(_depthImage.imageFormat, _depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);

    VK_CHECK(vkCreateImageView(_device, &dview_info, nullptr, &_depthImage.imageView));

    // add to deletion queues
    _mainDeletionQueue.push_function(
        [=, this]()
        {
            vkDestroyImageView(_device, _drawImage.imageView, nullptr);
            vmaDestroyImage(_allocator, _drawImage.image, _drawImage.allocation);

            vkDestroyImageView(_device, _depthImage.imageView, nullptr);
            vmaDestroyImage(_allocator, _depthImage.image, _depthImage.allocation);
        });
}

void VulkanEngine::destroy_swapchain()
{
    vkDestroySwapchainKHR(_device, _swapchain, nullptr);

    for (int i = 0; i < _swapchainImageViews.size(); i++)
        vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
}

void VulkanEngine::init_commands()
{

    // create command pool
    VkCommandPoolCreateInfo commandPoolInfo =
        vkinit::command_pool_create_info(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

    for (int i = 0; i < FRAME_OVERLAP; i++)
    {
        VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frames[i]._commandPool));

        // allocate the default command buffer that we will use for rendering
        VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_frames[i]._commandPool, 1);

        VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i]._mainCommandBuffer));
    }

    // imgui
    VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_immCommandPool));

    // allocate the command buffer for immediate submits
    VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_immCommandPool, 1);

    VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_immCommandBuffer));

    _mainDeletionQueue.push_function([=, this]() { vkDestroyCommandPool(_device, _immCommandPool, nullptr); });
}

void VulkanEngine::init_sync_structures()
{
    VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
    VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();

    for (int i = 0; i < FRAME_OVERLAP; i++)
    {
        VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i]._renderFence));

        VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._swapchainSemaphore));
        VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._renderSemaphore));
    }

    // imgui
    VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_immFence));
    _mainDeletionQueue.push_function([=, this]() { vkDestroyFence(_device, _immFence, nullptr); });
}

void VulkanEngine::immediate_submit(std::function<void(VkCommandBuffer cmd)> &&function)
{
    VK_CHECK(vkResetFences(_device, 1, &_immFence));
    VK_CHECK(vkResetCommandBuffer(_immCommandBuffer, 0));

    VkCommandBuffer cmd = _immCommandBuffer;

    VkCommandBufferBeginInfo cmdBeginInfo =
        vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    function(cmd);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cmdinfo = vkinit::command_buffer_submit_info(cmd);
    VkSubmitInfo2 submit = vkinit::submit_info(&cmdinfo, nullptr, nullptr);

    // submit command buffer to the queue and execute it.
    //  _renderFence will now block until the graphic commands finish execution
    VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, _immFence));

    VK_CHECK(vkWaitForFences(_device, 1, &_immFence, true, 9999999999));
}

void VulkanEngine::draw_background(VkCommandBuffer cmd)
{
    // // make a clear-color from frame number. This will flash with a 120 frame period.
    // VkClearColorValue clearValue;
    // float flash = std::abs(std::sin(_frameNumber / 120.f));
    // clearValue = {{0.0f, 0.0f, flash, 1.0f}};

    // VkImageSubresourceRange clearRange = vkinit::image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);

    // // clear image
    // vkCmdClearColorImage(cmd, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &clearRange);

    ComputeEffect &effect = backgroundEffects[currentBackgroundEffect];

    // bind the background compute pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);

    // bind the descriptor set containing the draw image for the compute pipeline
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _gradientPipelineLayout, 0, 1, &_drawImageDescriptors,
                            0, nullptr);

    vkCmdPushConstants(cmd, _gradientPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants),
                       &effect.data);

    // execute the compute pipeline dispatch. We are using 16x16 workgroup size so we need to divide by it
    vkCmdDispatch(cmd, std::ceil(_drawExtent.width / 16.0), std::ceil(_drawExtent.height / 16.0), 1);
}

void VulkanEngine::init_imgui()
{
    // 1: create descriptor pool for IMGUI
    //  the size of the pool is very oversize, but it's copied from imgui demo
    //  itself.
    VkDescriptorPoolSize pool_sizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
                                         {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
                                         {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
                                         {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
                                         {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
                                         {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
                                         {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
                                         {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
                                         {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
                                         {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
                                         {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000;
    pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    VkDescriptorPool imguiPool;
    VK_CHECK(vkCreateDescriptorPool(_device, &pool_info, nullptr, &imguiPool));

    // 2: initialize imgui library

    // this initializes the core structures of imgui
    ImGui::CreateContext();

    // this initializes imgui for SDL
    ImGui_ImplSDL2_InitForVulkan(_window);

    // this initializes imgui for Vulkan
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = _instance;
    init_info.PhysicalDevice = _chosenGPU;
    init_info.Device = _device;
    init_info.Queue = _graphicsQueue;
    init_info.DescriptorPool = imguiPool;
    init_info.MinImageCount = 3;
    init_info.ImageCount = 3;
    init_info.UseDynamicRendering = true;

    // dynamic rendering parameters for imgui to use
    init_info.PipelineRenderingCreateInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &_swapchainImageFormat;

    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&init_info);

    // ImGui_ImplVulkan_CreateFontsTexture(); // This seems to be no longer needed in the ImGUI API

    // add the destroy the imgui created structures
    _mainDeletionQueue.push_function(
        [=, this]()
        {
            ImGui_ImplVulkan_Shutdown();
            vkDestroyDescriptorPool(_device, imguiPool, nullptr);
        });
}

void VulkanEngine::cleanup()
{
    if (_isInitialized)
    {

        vkDeviceWaitIdle(_device);

        for (int i = 0; i < FRAME_OVERLAP; i++)
        {
            vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr);

            // destroy sync objects
            vkDestroyFence(_device, _frames[i]._renderFence, nullptr);
            vkDestroySemaphore(_device, _frames[i]._renderSemaphore, nullptr);
            vkDestroySemaphore(_device, _frames[i]._swapchainSemaphore, nullptr);

            _frames[i]._deletionQueue.flush();
        }

        for (auto &mesh : testMeshes)
        {
            destroy_buffer(mesh->meshBuffers.indexBuffer);
            destroy_buffer(mesh->meshBuffers.vertexBuffer);
        }

        _mainDeletionQueue.flush();

        destroy_swapchain();

        vkDestroySurfaceKHR(_instance, _surface, nullptr);
        vkDestroyDevice(_device, nullptr);
        vkb::destroy_debug_utils_messenger(_instance, _debugMessenger);
        vkDestroyInstance(_instance, nullptr);
        SDL_DestroyWindow(_window);
    }

    // clear engine pointer
    loadedEngine = nullptr;
}

void VulkanEngine::draw()
{
    update_scene();

    VK_CHECK(vkWaitForFences(_device, 1, &get_current_frame()._renderFence, true, 1000000000));
    get_current_frame()._deletionQueue.flush();
    get_current_frame()._frameDescriptors.clear_pools(_device);
    uint32_t swapchainImageIndex;
    VkResult e = vkAcquireNextImageKHR(_device, _swapchain, 1000000000, get_current_frame()._swapchainSemaphore,
                                       nullptr, &swapchainImageIndex);
    if (e == VK_ERROR_OUT_OF_DATE_KHR || e == VK_SUBOPTIMAL_KHR)
    {
        resize_requested = true;
        return;
    }

    _drawExtent.height = std::min(_swapchainExtent.height, _drawImage.imageExtent.height) * renderScale;
    _drawExtent.width = std::min(_swapchainExtent.width, _drawImage.imageExtent.width) * renderScale;

    VK_CHECK(vkResetFences(_device, 1, &get_current_frame()._renderFence));

    VkCommandBuffer cmd = get_current_frame()._mainCommandBuffer;
    VK_CHECK(vkResetCommandBuffer(cmd, 0));

    VkCommandBufferBeginInfo cmdBeginInfo =
        vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    // start command buffer recording ---------------------
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    // transition our main draw image into general layout so we can write into it
    // we will overwrite it all so we dont care about what was the older layout
    vkutil::transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    draw_background(cmd);

    vkutil::transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    // setup depth attachment
    vkutil::transition_image(cmd, _depthImage.image, VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    draw_geometry(cmd);

    // transition the draw image and the swapchain image into their correct transfer layouts
    vkutil::transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // execute a copy from the draw image into the swapchain
    vkutil::copy_image_to_image(cmd, _drawImage.image, _swapchainImages[swapchainImageIndex], _drawExtent,
                                _swapchainExtent);

    // set swapchain image layout to Attachment Optimal so we can draw it
    vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    // draw imgui into the swapchain image
    draw_imgui(cmd, _swapchainImageViews[swapchainImageIndex]);

    // set swapchain image layout to Present so we can draw it
    vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                             VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    // finalize the command buffer (we can no longer add commands, but it can now be executed)
    VK_CHECK(vkEndCommandBuffer(cmd));
    // end command buffer recording -----------------------

    // start submit queue -------------------------------------
    VkCommandBufferSubmitInfo cmdInfo = vkinit::command_buffer_submit_info(cmd);

    VkSemaphoreSubmitInfo waitInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
                                                                   get_current_frame()._swapchainSemaphore);
    VkSemaphoreSubmitInfo signalInfo =
        vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, get_current_frame()._renderSemaphore);
    VkSubmitInfo2 submit = vkinit::submit_info(&cmdInfo, &signalInfo, &waitInfo);
    VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, get_current_frame()._renderFence));

    // end submit queue ---------------------------------------

    // start present ---------------------------------

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext = nullptr;
    presentInfo.pSwapchains = &_swapchain;
    presentInfo.swapchainCount = 1;

    presentInfo.pWaitSemaphores = &get_current_frame()._renderSemaphore;
    presentInfo.waitSemaphoreCount = 1;

    presentInfo.pImageIndices = &swapchainImageIndex;

    VkResult presentResult = vkQueuePresentKHR(_graphicsQueue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
        resize_requested = true;

    // increase the number of frames drawn
    _frameNumber++;

    // end present -------------------------------------
}

void VulkanEngine::resize_swapchain()
{
    vkDeviceWaitIdle(_device);

    destroy_swapchain();

    int w, h;
    SDL_GetWindowSize(_window, &w, &h);
    _windowExtent.width = w;
    _windowExtent.height = h;

    create_swapchain(_windowExtent.width, _windowExtent.height);

    resize_requested = false;
}

void VulkanEngine::draw_geometry(VkCommandBuffer cmd)
{
    // begin a render pass  connected to our draw image
    VkRenderingAttachmentInfo colorAttachment =
        vkinit::attachment_info(_drawImage.imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    // setup depth attachment similarly
    VkRenderingAttachmentInfo depthAttachment =
        vkinit::depth_attachment_info(_depthImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    VkRenderingInfo renderInfo = vkinit::rendering_info(_drawExtent, &colorAttachment, &depthAttachment);
    vkCmdBeginRendering(cmd, &renderInfo);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _meshPipeline);

    // set dynamic viewport and scissor
    VkViewport viewport = {};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = _drawExtent.width;
    viewport.height = _drawExtent.height;
    viewport.minDepth = 0.f;
    viewport.maxDepth = 1.f;

    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = _drawExtent.width;
    scissor.extent.height = _drawExtent.height;

    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdDraw(cmd, 3, 1, 0, 0);
    // draw scene ----------------------

    // allocate a new uniform buffer for the scene data
    AllocatedBuffer gpuSceneDataBuffer =
        create_buffer(sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    // add it to the deletion queue of this frame so it gets deleted once its been used
    get_current_frame()._deletionQueue.push_function([=, this]() { destroy_buffer(gpuSceneDataBuffer); });

    // write the buffer
    GPUSceneData *sceneUniformData = (GPUSceneData *)gpuSceneDataBuffer.allocation->GetMappedData();
    *sceneUniformData = sceneData;

    // create a descriptor set that binds that buffer and update it
    VkDescriptorSet globalDescriptor = get_current_frame()._frameDescriptors.allocate(
        _device, _gpuSceneDataDescriptorLayout); // no need to destroy this because we destroy the entire pool at the
                                                 // start of the frame.

    DescriptorWriter writer;
    writer.write_buffer(0, gpuSceneDataBuffer.buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.update_set(_device, globalDescriptor);

    for (const RenderObject &draw : mainDrawContext.OpaqueSurfaces)
    {

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw.material->pipeline->pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw.material->pipeline->layout, 0, 1,
                                &globalDescriptor, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw.material->pipeline->layout, 1, 1,
                                &draw.material->materialSet, 0, nullptr);

        vkCmdBindIndexBuffer(cmd, draw.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        GPUDrawPushConstants pushConstants;
        pushConstants.vertexBuffer = draw.vertexBufferAddress;
        pushConstants.worldMatrix = draw.transform;
        vkCmdPushConstants(cmd, draw.material->pipeline->layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(GPUDrawPushConstants), &pushConstants);

        vkCmdDrawIndexed(cmd, draw.indexCount, 1, draw.firstIndex, 0, 0);
    }

    vkCmdEndRendering(cmd);
}

AllocatedBuffer VulkanEngine::create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
{
    // allocate buffer
    VkBufferCreateInfo bufferInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.pNext = nullptr;
    bufferInfo.size = allocSize;

    bufferInfo.usage = usage;

    VmaAllocationCreateInfo vmaallocInfo = {};
    vmaallocInfo.usage = memoryUsage;
    vmaallocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    AllocatedBuffer newBuffer;

    // allocate the buffer
    VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &vmaallocInfo, &newBuffer.buffer, &newBuffer.allocation,
                             &newBuffer.info));

    return newBuffer;
}

void VulkanEngine::destroy_buffer(const AllocatedBuffer &buffer)
{
    vmaDestroyBuffer(_allocator, buffer.buffer, buffer.allocation);
}

AllocatedImage VulkanEngine::create_image(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped)
{
    AllocatedImage newImage;
    newImage.imageFormat = format;
    newImage.imageExtent = size;

    VkImageCreateInfo img_info = vkinit::image_create_info(format, usage, size);
    if (mipmapped)
        img_info.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(size.width, size.height)))) + 1;

    // always allocate images on dedicated GPU memory
    VmaAllocationCreateInfo allocinfo = {};
    allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // allocate and create the image
    VK_CHECK(vmaCreateImage(_allocator, &img_info, &allocinfo, &newImage.image, &newImage.allocation, nullptr));

    // if the format is a depth format, we will need to have it use the correct
    // aspect flag
    VkImageAspectFlags aspectFlag = VK_IMAGE_ASPECT_COLOR_BIT;
    if (format == VK_FORMAT_D32_SFLOAT)
        aspectFlag = VK_IMAGE_ASPECT_DEPTH_BIT;

    // build a image-view for the image
    VkImageViewCreateInfo view_info = vkinit::imageview_create_info(format, newImage.image, aspectFlag);
    view_info.subresourceRange.levelCount = img_info.mipLevels;

    VK_CHECK(vkCreateImageView(_device, &view_info, nullptr, &newImage.imageView));

    return newImage;
}

AllocatedImage VulkanEngine::create_image(void *data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage,
                                          bool mipmapped)
{
    size_t data_size = size.depth * size.width * size.height * 4;
    AllocatedBuffer uploadbuffer =
        create_buffer(data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    memcpy(uploadbuffer.info.pMappedData, data, data_size);

    AllocatedImage new_image = create_image(
        size, format, usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, mipmapped);

    immediate_submit(
        [&](VkCommandBuffer cmd)
        {
            vkutil::transition_image(cmd, new_image.image, VK_IMAGE_LAYOUT_UNDEFINED,
                                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            VkBufferImageCopy copyRegion = {};
            copyRegion.bufferOffset = 0;
            copyRegion.bufferRowLength = 0;
            copyRegion.bufferImageHeight = 0;

            copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.imageSubresource.mipLevel = 0;
            copyRegion.imageSubresource.baseArrayLayer = 0;
            copyRegion.imageSubresource.layerCount = 1;
            copyRegion.imageExtent = size;

            // copy the buffer into the image
            vkCmdCopyBufferToImage(cmd, uploadbuffer.buffer, new_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                   &copyRegion);

            vkutil::transition_image(cmd, new_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        });

    destroy_buffer(uploadbuffer);

    return new_image;
}

void VulkanEngine::destroy_image(const AllocatedImage &img)
{
    vkDestroyImageView(_device, img.imageView, nullptr);
    vmaDestroyImage(_allocator, img.image, img.allocation);
}

GPUMeshBuffers VulkanEngine::uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices)
{
    const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
    const size_t indexBufferSize = indices.size() * sizeof(uint32_t);

    GPUMeshBuffers newSurface;

    // create vertex buffer
    newSurface.vertexBuffer = create_buffer(vertexBufferSize,
                                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                            VMA_MEMORY_USAGE_GPU_ONLY);

    // find the adress of the vertex buffer
    VkBufferDeviceAddressInfo deviceAdressInfo{.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
                                               .buffer = newSurface.vertexBuffer.buffer};
    newSurface.vertexBufferAddress = vkGetBufferDeviceAddress(_device, &deviceAdressInfo);

    // create index buffer
    newSurface.indexBuffer =
        create_buffer(indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VMA_MEMORY_USAGE_GPU_ONLY);

    AllocatedBuffer staging =
        create_buffer(vertexBufferSize + indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

    void *data = staging.allocation->GetMappedData();

    // copy vertex buffer
    memcpy(data, vertices.data(), vertexBufferSize);
    // copy index buffer
    memcpy((char *)data + vertexBufferSize, indices.data(), indexBufferSize);

    immediate_submit(
        [&](VkCommandBuffer cmd)
        {
            VkBufferCopy vertexCopy{0};
            vertexCopy.dstOffset = 0;
            vertexCopy.srcOffset = 0;
            vertexCopy.size = vertexBufferSize;

            vkCmdCopyBuffer(cmd, staging.buffer, newSurface.vertexBuffer.buffer, 1, &vertexCopy);

            VkBufferCopy indexCopy{0};
            indexCopy.dstOffset = 0;
            indexCopy.srcOffset = vertexBufferSize;
            indexCopy.size = indexBufferSize;

            vkCmdCopyBuffer(cmd, staging.buffer, newSurface.indexBuffer.buffer, 1, &indexCopy);
        });

    destroy_buffer(staging);

    return newSurface;
}

void VulkanEngine::init_default_data()
{
    // 3 default textures, white, grey, black. 1 pixel each
    uint32_t white = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
    _whiteImage =
        create_image((void *)&white, VkExtent3D{1, 1, 1}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

    uint32_t grey = glm::packUnorm4x8(glm::vec4(0.66f, 0.66f, 0.66f, 1));
    _greyImage = create_image((void *)&grey, VkExtent3D{1, 1, 1}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

    uint32_t black = glm::packUnorm4x8(glm::vec4(0, 0, 0, 0));
    _blackImage =
        create_image((void *)&black, VkExtent3D{1, 1, 1}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

    // checkerboard image
    uint32_t magenta = glm::packUnorm4x8(glm::vec4(1, 0, 1, 1));
    std::array<uint32_t, 16 * 16> pixels; // for 16x16 checkerboard texture
    for (int x = 0; x < 16; x++)
        for (int y = 0; y < 16; y++)
            pixels[y * 16 + x] = ((x % 2) ^ (y % 2)) ? magenta : black;
    _errorCheckerboardImage =
        create_image(pixels.data(), VkExtent3D{16, 16, 1}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

    VkSamplerCreateInfo sampl = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};

    sampl.magFilter = VK_FILTER_NEAREST;
    sampl.minFilter = VK_FILTER_NEAREST;

    vkCreateSampler(_device, &sampl, nullptr, &_defaultSamplerNearest);

    sampl.magFilter = VK_FILTER_LINEAR;
    sampl.minFilter = VK_FILTER_LINEAR;
    vkCreateSampler(_device, &sampl, nullptr, &_defaultSamplerLinear);

    _mainDeletionQueue.push_function(
        [&]()
        {
            vkDestroySampler(_device, _defaultSamplerNearest, nullptr);
            vkDestroySampler(_device, _defaultSamplerLinear, nullptr);

            destroy_image(_whiteImage);
            destroy_image(_greyImage);
            destroy_image(_blackImage);
            destroy_image(_errorCheckerboardImage);
        });

    testMeshes = loadGltfMeshes(this, "..\\assets\\basicmesh.glb").value();

    GLTFMetallic_Roughness::MaterialResources materialResources;
    // default the material textures
    materialResources.colorImage = _whiteImage;
    materialResources.colorSampler = _defaultSamplerLinear;
    materialResources.metalRoughImage = _whiteImage;
    materialResources.metalRoughSampler = _defaultSamplerLinear;

    // set the uniform buffer for the material data
    AllocatedBuffer materialConstants = create_buffer(sizeof(GLTFMetallic_Roughness::MaterialConstants),
                                                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    // write the buffer
    GLTFMetallic_Roughness::MaterialConstants *sceneUniformData =
        (GLTFMetallic_Roughness::MaterialConstants *)materialConstants.allocation->GetMappedData();
    sceneUniformData->colorFactors = glm::vec4{1, 1, 1, 1};
    sceneUniformData->metal_rough_factors = glm::vec4{1, 0.5, 0, 0};

    _mainDeletionQueue.push_function([=, this]() { destroy_buffer(materialConstants); });

    materialResources.dataBuffer = materialConstants.buffer;
    materialResources.dataBufferOffset = 0;

    defaultData = metalRoughMaterial.write_material(_device, MaterialPass::MainColor, materialResources,
                                                    globalDescriptorAllocator);

    for (auto &m : testMeshes)
    {
        std::shared_ptr<MeshNode> newNode = std::make_shared<MeshNode>();
        newNode->mesh = m;

        newNode->localTransform = glm::mat4{1.f};
        newNode->worldTransform = glm::mat4{1.f};

        for (auto &s : newNode->mesh->surfaces)
            s.material = std::make_shared<GLTFMaterial>(defaultData);

        loadedNodes[m->name] = std::move(newNode);
    }
}

void VulkanEngine::init_mesh_pipeline()
{
    VkShaderModule triangleFragShader;
    if (!vkutil::load_shader_module("../shaders/tex_image.frag.spv", _device, &triangleFragShader))
        fmt::println("Error when building the triangle fragment shader module");
    else
        fmt::println("Triangle fragment shader succesfully loaded");

    VkShaderModule triangleVertexShader;
    if (!vkutil::load_shader_module("../shaders/colored_triangle_mesh.vert.spv", _device, &triangleVertexShader))
        fmt::println("Error when building the triangle vertex shader module");
    else
        fmt::println("Triangle vertex shader succesfully loaded");

    VkPushConstantRange bufferRange{};
    bufferRange.offset = 0;
    bufferRange.size = sizeof(GPUDrawPushConstants);
    bufferRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();
    pipeline_layout_info.pPushConstantRanges = &bufferRange;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pSetLayouts = &_singleImageDescriptorLayout;
    pipeline_layout_info.setLayoutCount = 1;

    VK_CHECK(vkCreatePipelineLayout(_device, &pipeline_layout_info, nullptr, &_meshPipelineLayout));

    PipelineBuilder pipelineBuilder;

    // use the triangle layout we created
    pipelineBuilder._pipelineLayout = _meshPipelineLayout;
    // connecting the vertex and pixel shaders to the pipeline
    pipelineBuilder.set_shaders(triangleVertexShader, triangleFragShader);
    // it will draw triangles
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    // filled triangles
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    // no backface culling
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    // no multisampling
    pipelineBuilder.set_multisampling_none();
    // no blending
    pipelineBuilder.disable_blending();
    // pipelineBuilder.enable_blending_additive();

    // pipelineBuilder.disable_depthtest();
    pipelineBuilder.enable_depthtest(
        true,
        VK_COMPARE_OP_GREATER_OR_EQUAL); // greater or equal because depth is inverted. (for better depth testing)

    // connect the image format we will draw into, from draw image
    pipelineBuilder.set_color_attachment_format(_drawImage.imageFormat);
    pipelineBuilder.set_depth_format(_depthImage.imageFormat);

    // finally build the pipeline
    _meshPipeline = pipelineBuilder.build_pipeline(_device);

    // clean structures
    vkDestroyShaderModule(_device, triangleFragShader, nullptr);
    vkDestroyShaderModule(_device, triangleVertexShader, nullptr);

    _mainDeletionQueue.push_function(
        [&]()
        {
            vkDestroyPipelineLayout(_device, _meshPipelineLayout, nullptr);
            vkDestroyPipeline(_device, _meshPipeline, nullptr);
        });
}

void VulkanEngine::run()
{
    SDL_Event e;
    bool bQuit = false;

    SDL_Scancode scanCode;

    // main loop
    while (!bQuit)
    {
        // Handle events on queue
        while (SDL_PollEvent(&e) != 0)
        {
            // close the window when user alt-f4s or clicks the X button
            if (e.type == SDL_QUIT)
                bQuit = true;

            if (e.type == SDL_WINDOWEVENT)
            {
                if (e.window.event == SDL_WINDOWEVENT_MINIMIZED)
                    stop_rendering = true;
                if (e.window.event == SDL_WINDOWEVENT_RESTORED)
                    stop_rendering = false;
            }

            // get (if any) inputs from the keyboard
            // if (e.type == SDL_KEYDOWN && e.key.keysym.sym < 128)
            //     fmt::println("Key pressed : {}", (char)e.key.keysym.sym);

            ImGui_ImplSDL2_ProcessEvent(&e);
        }

        if (resize_requested)
            resize_swapchain();

        // imgui new frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        if (ImGui::Begin("background"))
        {
            ImGui::SliderFloat("Render Scale", &renderScale, 0.3f, 1.f);
            ComputeEffect &selected = backgroundEffects[currentBackgroundEffect];
            ImGui::Text("Selected effect: %s", selected.name);

            ImGui::SliderInt("Effect Index", &currentBackgroundEffect, 0, backgroundEffects.size() - 1);

            ImGui::InputFloat4("data1", (float *)&selected.data.data1);
            ImGui::InputFloat4("data2", (float *)&selected.data.data2);
            ImGui::InputFloat4("data3", (float *)&selected.data.data3);
            ImGui::InputFloat4("data4", (float *)&selected.data.data4);
        }
        ImGui::End();

        // make imgui calculate internal draw structures
        ImGui::Render();

        // do not draw if we are minimized
        if (stop_rendering)
        {
            // throttle the speed to avoid the endless spinning
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        draw();
    }
}

void VulkanEngine::draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView)
{
    VkRenderingAttachmentInfo colorAttachment =
        vkinit::attachment_info(targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingInfo renderInfo = vkinit::rendering_info(_swapchainExtent, &colorAttachment, nullptr);

    vkCmdBeginRendering(cmd, &renderInfo);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

    vkCmdEndRendering(cmd);
}

void VulkanEngine::update_scene()
{
    mainDrawContext.OpaqueSurfaces.clear();

    loadedNodes["Suzanne"]->Draw(glm::mat4{1.f}, mainDrawContext);

    sceneData.view = glm::translate(glm::vec3{0, 0, -5});
    // camera projection
    sceneData.proj =
        glm::perspective(glm::radians(70.f), (float)_windowExtent.width / (float)_windowExtent.height, 10000.f, 0.1f);

    // invert the Y direction on projection matrix so that we are more similar
    // to opengl and gltf axis
    sceneData.proj[1][1] *= -1;
    sceneData.viewproj = sceneData.proj * sceneData.view;

    // some default lighting parameters
    sceneData.ambientColor = glm::vec4(.1f);
    sceneData.sunlightColor = glm::vec4(1.f);
    sceneData.sunlightDirection = glm::vec4(0, 1, 0.5, 1.f);
}

void GLTFMetallic_Roughness::build_pipelines(VulkanEngine *engine)
{
    VkShaderModule meshFragShader;
    if (!vkutil::load_shader_module("../shaders/mesh.frag.spv", engine->_device, &meshFragShader))
        fmt::println("Error when building the triangle fragment shader module\n");

    VkShaderModule meshVertexShader;
    if (!vkutil::load_shader_module("../shaders/mesh.vert.spv", engine->_device, &meshVertexShader))
        fmt::println("Error when building the triangle vertex shader module\n");

    VkPushConstantRange matrixRange{};
    matrixRange.offset = 0;
    matrixRange.size = sizeof(GPUDrawPushConstants);
    matrixRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    DescriptorLayoutBuilder layoutBuilder;
    layoutBuilder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    layoutBuilder.add_binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    layoutBuilder.add_binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    materialLayout = layoutBuilder.build(engine->_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

    VkDescriptorSetLayout layouts[] = {engine->_gpuSceneDataDescriptorLayout, materialLayout};

    VkPipelineLayoutCreateInfo mesh_layout_info = vkinit::pipeline_layout_create_info();
    mesh_layout_info.setLayoutCount = 2;
    mesh_layout_info.pSetLayouts = layouts;
    mesh_layout_info.pPushConstantRanges = &matrixRange;
    mesh_layout_info.pushConstantRangeCount = 1;

    VkPipelineLayout newLayout;
    VK_CHECK(vkCreatePipelineLayout(engine->_device, &mesh_layout_info, nullptr, &newLayout));

    opaquePipeline.layout = newLayout;
    transparentPipeline.layout = newLayout;

    // build the stage-create-info for both vertex and fragment stages. This lets
    // the pipeline know the shader modules per stage
    PipelineBuilder pipelineBuilder;
    pipelineBuilder.set_shaders(meshVertexShader, meshFragShader);
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.set_multisampling_none();
    pipelineBuilder.disable_blending();
    pipelineBuilder.enable_depthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);

    // render format
    pipelineBuilder.set_color_attachment_format(engine->_drawImage.imageFormat);
    pipelineBuilder.set_depth_format(engine->_depthImage.imageFormat);

    // use the triangle layout we created
    pipelineBuilder._pipelineLayout = newLayout;

    // finally build the pipeline
    opaquePipeline.pipeline = pipelineBuilder.build_pipeline(engine->_device);

    // create the transparent variant
    pipelineBuilder.enable_blending_additive();

    pipelineBuilder.enable_depthtest(false, VK_COMPARE_OP_GREATER_OR_EQUAL);

    transparentPipeline.pipeline = pipelineBuilder.build_pipeline(engine->_device);

    vkDestroyShaderModule(engine->_device, meshFragShader, nullptr);
    vkDestroyShaderModule(engine->_device, meshVertexShader, nullptr);
}

MaterialInstance GLTFMetallic_Roughness::write_material(VkDevice device, MaterialPass pass,
                                                        const MaterialResources &resources,
                                                        DescriptorAllocatorGrowable &descriptorAllocator)
{
    MaterialInstance matData;
    matData.passType = pass;
    if (pass == MaterialPass::Transparent)
        matData.pipeline = &transparentPipeline;
    else
        matData.pipeline = &opaquePipeline;

    matData.materialSet = descriptorAllocator.allocate(device, materialLayout);

    writer.clear();
    writer.write_buffer(0, resources.dataBuffer, sizeof(MaterialConstants), resources.dataBufferOffset,
                        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.write_image(1, resources.colorImage.imageView, resources.colorSampler,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.write_image(2, resources.metalRoughImage.imageView, resources.metalRoughSampler,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    writer.update_set(device, matData.materialSet);

    return matData;
}

void MeshNode::Draw(const glm::mat4 &topMatrix, DrawContext &ctx)
{
    glm::mat4 nodeMatrix = topMatrix * worldTransform;

    for (auto &s : mesh->surfaces)
    {
        RenderObject def;
        def.indexCount = s.count;
        def.firstIndex = s.startIndex;
        def.indexBuffer = mesh->meshBuffers.indexBuffer.buffer;
        def.material = &s.material->data;

        def.transform = nodeMatrix;
        def.vertexBufferAddress = mesh->meshBuffers.vertexBufferAddress;

        ctx.OpaqueSurfaces.push_back(def);
    }

    // recurse down
    Node::Draw(topMatrix, ctx);
}
