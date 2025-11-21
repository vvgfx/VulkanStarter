#pragma once
#include "GPUResourceAllocator.h"
#include "IFeature.h"
#include "vk_descriptors.h"
#include "vk_engine.h"

namespace rgraph
{

    /**
     * @brief An implementation of IFeature that does simple PBR Shading
     *
     */
    class PBRShadingFeature : public IFeature
    {
      public:
        PBRShadingFeature(DrawContext &drwCtx, VkDevice _device, DeletionQueue &delQueue);
        void Register(RendergraphBuilder *builder) override;

      private:
        void InitPipeline(VkDevice _device, DeletionQueue &delQueue);

        VkPipeline pipeline;
        VkPipelineLayout pipelineLayout;
        VkDescriptorSetLayout descriptorLayout;
        DescriptorAllocatorGrowable descriptorAllocator;
        DrawContext &drawContext;
        GPUResourceAllocator *gpuResourceAllocator;

        // frame deletion queue?
    };
} // namespace rgraph
