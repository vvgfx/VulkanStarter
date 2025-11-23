// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

// #include "SDL_stdinc.h"
#include <camera.h>
#include <cstdint>
#include <vector>
#include <vk_descriptors.h>
#include <vk_loader.h>
#include <vk_types.h>

#include <GPUResourceAllocator.h>

struct EngineStats
{
    float frametime;
    int triangle_count;
    int drawcall_count;
    float scene_update_time;
    float mesh_draw_time;
};

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
    VkSemaphore _renderSemaphore; //, _renderSemaphore;
    VkFence _renderFence;
    DeletionQueue _deletionQueue;
    DescriptorAllocatorGrowable _frameDescriptors;
};

struct SyncStructures
{
    VkSemaphore _presentSemaphore;
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

struct GPUSceneData
{
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 viewproj;
    glm::vec4 ambientColor;
    glm::vec4 sunlightDirection; // w for sun power
    glm::vec4 sunlightColor;
};

// {{{ SCENEGRAPHS --------------------------

struct RenderObject
{
    uint32_t indexCount;
    uint32_t firstIndex;
    VkBuffer indexBuffer;

    MaterialInstance *material;
    Bounds bounds;
    glm::mat4 transform;
    VkDeviceAddress vertexBufferAddress;
};

struct DrawContext
{
    std::vector<RenderObject> OpaqueSurfaces;
    std::vector<RenderObject> TransparentSurfaces;
};

// }}} SCENEGRAPHS end -----------------------

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
    DescriptorAllocatorGrowable globalDescriptorAllocator;
    GPUResourceAllocator _gpuResourceAllocator;

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
    int currentBackgroundEffect{1};

    // mesh pipelines
    VkPipelineLayout _meshPipelineLayout;
    VkPipeline _meshPipeline;

    // mesh data
    // std::vector<std::shared_ptr<MeshAsset>> testMeshes;

    bool resize_requested;
    float renderScale = 1.f;

    // scene data
    GPUSceneData sceneData;
    VkDescriptorSetLayout _gpuSceneDataDescriptorLayout;

    // default images
    AllocatedImage _whiteImage;
    AllocatedImage _blackImage;
    AllocatedImage _greyImage;
    AllocatedImage _errorCheckerboardImage;

    VkSampler _defaultSamplerLinear;
    VkSampler _defaultSamplerNearest;

    // default materials
    MaterialInstance defaultData;
    // GLTFMetallic_Roughness metalRoughMaterial;

    // sync structure - removed from frameData
    std::vector<SyncStructures> swapchainSyncStructures;

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
    virtual void init();

    // shuts down the engine
    void cleanup();

    // draw loop
    virtual void draw();

    // run main loop
    void run();

    // scenegraph stuff

    DrawContext mainDrawContext;
    std::unordered_map<std::string, std::shared_ptr<sgraph::Node>> loadedNodes;

    virtual void update_scene();

    // camera
    Camera mainCamera;

    // statistics
    EngineStats stats;

    // refactor
    GPUResourceAllocator *getGPUResourceAllocator();

  protected:
    void init_vulkan();
    void init_swapchain();
    void init_commands();
    void init_sync_structures();

    void create_swapchain(uint32_t width, uint32_t height);
    void destroy_swapchain();
    void draw_background(VkCommandBuffer cmd);

    void init_descriptors();

    // pipeline stuff
    virtual void init_pipelines();
    void init_background_pipelines();

    // imgui
    void init_imgui();
    void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView);

    void draw_geometry(VkCommandBuffer cmd);

    virtual void init_default_data();

    void resize_swapchain();

    // single cleanup override method for all children
    virtual void cleanupOnChildren();
};
