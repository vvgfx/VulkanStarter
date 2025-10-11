//> includes
#include "vk_engine.h"
#include "SDL_events.h"
#include "SDL_scancode.h"
#include "fmt/base.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>

// ---- other includes ----
#include "VkBootstrap.h"
#include <vk_initializers.h>
#include <vk_types.h>

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

    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);

    _window = SDL_CreateWindow("Vulkan Engine", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, _windowExtent.width,
                               _windowExtent.height, window_flags);

    initVulkan();

    initSwapChain();

    initCommands();

    initSyncStructures();

    // everything went fine
    _isInitialized = true;
}

void VulkanEngine::initVulkan()
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
}

void VulkanEngine::createSwapchain(uint32_t width, uint32_t height)
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

void VulkanEngine::initSwapChain()
{
    createSwapchain(_windowExtent.width, _windowExtent.height);
}

void VulkanEngine::destroySwapchain()
{
    vkDestroySwapchainKHR(_device, _swapchain, nullptr);

    for (int i = 0; i < _swapchainImageViews.size(); i++)
        vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
}

void VulkanEngine::initCommands()
{
}

void VulkanEngine::initSyncStructures()
{
}

void VulkanEngine::cleanup()
{
    if (_isInitialized)
    {
        destroySwapchain();

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
    // nothing yet
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
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym < 128)
                fmt::println("Key pressed : {}", (char)e.key.keysym.sym);
        }

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