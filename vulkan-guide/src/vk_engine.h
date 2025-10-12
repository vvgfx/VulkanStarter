﻿// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

// #include "SDL_stdinc.h"
#include <cstdint>
#include <vector>
#include <vk_types.h>

struct FrameData
{
    VkCommandPool _commandPool;
    VkCommandBuffer _mainCommandBuffer;
    VkSemaphore _swapchainSemaphore, _renderSemaphore;
    VkFence _renderFence;
};

constexpr unsigned int FRAME_OVERLAP = 2;

class VulkanEngine
{
  public:
    bool _isInitialized{false};
    int _frameNumber{0};
    bool stop_rendering{false};
    VkExtent2D _windowExtent{1700, 900};
    VkInstance _instance;
    VkDebugUtilsMessengerEXT _debugMessenger;
    VkPhysicalDevice _chosenGPU;
    VkDevice _device;
    VkSurfaceKHR _surface;
    VkSwapchainKHR _swapchain;
    VkFormat _swapchainImageFormat;
    FrameData _frames[FRAME_OVERLAP];
    VkQueue _graphicsQueue;
    uint32_t _graphicsQueueFamily;

    FrameData &get_current_frame()
    {
        return _frames[_frameNumber % FRAME_OVERLAP];
    };

    std::vector<VkImage> _swapchainImages;
    std::vector<VkImageView> _swapchainImageViews;
    VkExtent2D _swapchainExtent;

    struct SDL_Window *_window{nullptr};

    static VulkanEngine &Get();

    // initializes everything in the engine
    void init();

    // shuts down the engine
    void cleanup();

    // draw loop
    void draw();

    // run main loop
    void run();

  private:
    void initVulkan();
    void initSwapChain();
    void initCommands();
    void initSyncStructures();

    void createSwapchain(uint32_t width, uint32_t height);
    void destroySwapchain();
};
