// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

// #include "SDL_stdinc.h"
#include <cstdint>
#include <vector>
#include <vk_descriptors.h>
#include <vk_loader.h>
#include <vk_types.h>

struct DeletionQueue
{
    std::deque<std::function<void()>> deletors;

    void push_function(std::function<void()> &&function)
    {
        deletors.push_back(function);
    }

    void flush()
    {
        // reverse iterate the deletion queue to execute all the functions
        for (auto it = deletors.rbegin(); it != deletors.rend(); it++)
            (*it)(); // call functors

        deletors.clear();
    }
};

struct FrameData
{
    VkCommandPool _commandPool;
    VkCommandBuffer _mainCommandBuffer;
    VkSemaphore _swapchainSemaphore, _renderSemaphore;
    VkFence _renderFence;
    DeletionQueue _deletionQueue;
};

struct ComputePushConstants
{
    glm::vec4 data1;
    glm::vec4 data2;
    glm::vec4 data3;
    glm::vec4 data4;
};

struct ComputeEffect
{
    const char *name;
    VkPipeline pipeline;
    VkPipelineLayout layout;
    ComputePushConstants data;
};

constexpr unsigned int FRAME_OVERLAP = 3;

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
    DeletionQueue _mainDeletionQueue;
    VmaAllocator _allocator;
    AllocatedImage _drawImage;
    AllocatedImage _depthImage; // depth testing
    VkExtent2D _drawExtent;
    DescriptorAllocator globalDescriptorAllocator;

    VkDescriptorSet _drawImageDescriptors;
    VkDescriptorSetLayout _drawImageDescriptorLayout;

    // pipeline and shader modules
    VkPipeline _gradientPipeline;
    VkPipelineLayout _gradientPipelineLayout;

    // imgui stuff
    VkFence _immFence;
    VkCommandBuffer _immCommandBuffer;
    VkCommandPool _immCommandPool;
    void immediate_submit(std::function<void(VkCommandBuffer cmd)> &&function);

    // multiple compute pipelines
    std::vector<ComputeEffect> backgroundEffects;
    int currentBackgroundEffect{0};

    // mesh pipelines
    VkPipelineLayout _meshPipelineLayout;
    VkPipeline _meshPipeline;
    GPUMeshBuffers uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices);

    // mesh data
    std::vector<std::shared_ptr<MeshAsset>> testMeshes;

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
    void init_vulkan();
    void init_swapchain();
    void init_commands();
    void init_sync_structures();

    void create_swapchain(uint32_t width, uint32_t height);
    void destroy_swapchain();
    void draw_background(VkCommandBuffer cmd);

    void init_descriptors();

    // pipeline stuff
    void init_pipelines();
    void init_background_pipelines();

    // imgui
    void init_imgui();
    void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView);

    void draw_geometry(VkCommandBuffer cmd);

    // buffers and mesh pipeline
    AllocatedBuffer create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
    void destroy_buffer(const AllocatedBuffer &buffer);

    void init_mesh_pipeline();
    void init_default_data();
};
