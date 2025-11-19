#include "vk_types.h"
#include <functional>
namespace rgraph
{

    enum PassType
    {
        Compute,
        Graphics
    };

    struct Pass
    {
        void ReadsImage(const std::string name, VkImageLayout layout);
        void WritesImage(const std::string name);

        void AddColorAttachment(const std::string name, bool store, VkClearValue *clear);
        void AddDepthStencilAttachment(const std::string name, bool store, VkClearValue *clear);

        void CreatesBuffer(const std::string name, size_t size, VkBufferUsageFlags usages);

        void ReadsBuffer(const std::string name);
        void WritesBuffer(const std::string name);

        PassType type;

        // add imageRead vector
        // add imageWrite vector

        // add imageWrite vector for colorAttachments

        // add PassBufferCreationInfo vector for buffer creations
        // add string vector for buffer dependencies
    };

    struct PassExecution
    {
        VkCommandBuffer cmd;
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

      private:
    };
} // namespace rgraph