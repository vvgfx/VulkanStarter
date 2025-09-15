#include <vulkan_pch.h>

#include <iostream>
#include <stdexcept>
#include <algorithm>

constexpr uint32_t WIDTH = 1280;
constexpr uint32_t HEIGHT = 720;

class HelloTriangleApplication
{
public:
    void run()
    {
        initWindow();
        initVulkan();
        mainLoop();
        cleanup();
    }

private:
    void initVulkan()
    {
        createInstance();
        setupDebugMessenger();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
    }

    void mainLoop()
    {
        while (!glfwWindowShouldClose(window))
        {
            glfwPollEvents();
        }
    }

    void cleanup()
    {
        glfwDestroyWindow(window);
        glfwTerminate();
    }

    void initWindow()
    {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

        window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
    }

    void createSurface()
    {
    }

    void createInstance()
    {
        constexpr vk::ApplicationInfo appInfo { 
            .pApplicationName   = "Hello Triangle",
            .applicationVersion = VK_MAKE_VERSION( 1, 0, 0 ),
            .pEngineName        = "VV Engine",
            .engineVersion      = VK_MAKE_VERSION( 1, 0, 0 ),
            .apiVersion         = vk::ApiVersion14 
        };

        // get required instance extensions from GLFW
        uint32_t glfwExtensionCount = 0;
        auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);


        // check if required extensions are supported
        auto extensionProperties = context.enumerateInstanceExtensionProperties();
        for(uint32_t i = 0; i < glfwExtensionCount; i++)
        {
            if(std::ranges::none_of(extensionProperties,
                                    [glfwExtension = glfwExtensions[i]](auto const& extensionProperty)
                                    { return strcmp(extensionProperty.extensionName, glfwExtension) == 0; }))
            {
                throw std::runtime_error("Required GLFW extension is not supported: " + std::string(glfwExtensions[i]));
            }
        }

        vk::InstanceCreateInfo createInfo {
            .pApplicationInfo = &appInfo,
            .enabledExtensionCount = glfwExtensionCount,
            .ppEnabledExtensionNames = glfwExtensions
        };

        instance = vk::raii::Instance(context, createInfo);
    }

    void setupDebugMessenger()
    {
    }

    void pickPhysicalDevice()
    {
    }

    void createLogicalDevice()
    {
    }

    //-----------------------------------------------------
    // class variables here
    GLFWwindow *window = nullptr;
    vk::raii::Context  context;
    vk::raii::Instance instance = nullptr;
    vk::raii::SurfaceKHR surface = nullptr;
};

int main()
{
    HelloTriangleApplication app;

    try
    {
        app.run();
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}