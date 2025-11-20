#pragma once
#include "vk_types.h"
#include <functional>
#include <memory>
#include <string>
namespace rgraph
{

    class IFeature;

    enum PassType
    {
        Compute,
        Graphics
    };

    struct PassImageRead
    {
        std::string name;
        VkImageLayout startingLayout;
    };

    struct PassImageWrite
    {
        std::string name;

        // These are used for color and depth attachments
        bool store;
        VkClearValue *clear;
    };

    struct Pass
    {
        friend class RendergraphBuilder;

        void ReadsImage(const std::string name, VkImageLayout layout);
        void WritesImage(const std::string name);

        // Add these back as I require.
        // void AddColorAttachment(const std::string name, bool store, VkClearValue *clear = nullptr);
        // void AddDepthStencilAttachment(const std::string name, bool store, VkClearValue *clear = nullptr);

        // void CreatesBuffer(const std::string name, size_t size, VkBufferUsageFlags usages);

        // void ReadsBuffer(const std::string name);
        // void WritesBuffer(const std::string name);

        PassType type;

      private:
        // add imageRead vector
        std::vector<PassImageRead> imageReads;
        // add imageWrite vector
        std::vector<PassImageWrite> imageWrites;

        // add imageWrite vector for colorAttachments
        // std::vector<PassImageWrite> colorAttachments;

        // add PassBufferCreationInfo vector for buffer creations
        // add string vector for buffer dependencies

        // add depth attachment read, bool storeDepth, and a reference to the creating builder itself.
        // PassImageRead depthAttachment
    };

    struct PassExecution
    {
        VkCommandBuffer cmd;
        VkDevice _device;
        std::unordered_map<std::string, AllocatedBuffer *> allocatedBuffers;
        std::unordered_map<std::string, AllocatedImage *> allocatedImages;

        // temporary, need to change later.
        VkExtent2D _drawExtent;
    };

    /**
     * @brief This class builds the rendergraph, and is expected to be called every frame.
     *
     */
    class RendergraphBuilder
    {

      public:
        void AddComputePass(const std::string name, std::function<void(Pass &)> setup,
                            std::function<void(PassExecution &)> run);
        void AddGraphicsPass(const std::string name, std::function<void(Pass &)> setup,
                             std::function<void(PassExecution &)> run);

        void AddTrackedImage(const std::string name, VkImageLayout startLayout, AllocatedImage *image);
        void AddTrackedBuffer(const std::string name, AllocatedBuffer *buffer);

        void Build();
        void Run();

        void AddFeature(std::weak_ptr<IFeature> feature);

      private:
        std::vector<Pass> passData;

        std::vector<std::function<void(PassExecution &)>> executionLambdas;

        std::unordered_map<std::string, AllocatedImage *> images;
        std::unordered_map<std::string, AllocatedBuffer *> buffers;

        std::vector<std::weak_ptr<IFeature>> features;
    };
} // namespace rgraph