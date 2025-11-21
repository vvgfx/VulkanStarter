#pragma once
#include "GPUResourceAllocator.h"
#include "IFeature.h"
#include "MaterialSystem.h"
#include "vk_descriptors.h"
#include "vk_engine.h"
#include <memory>

namespace rgraph
{

    /**
     * @brief An implementation of IFeature that does simple PBR Shading
     *
     */
    class PBRShadingFeature : public IFeature
    {
      public:
        PBRShadingFeature(DrawContext &drwCtx, VkDevice _device, DeletionQueue &delQueue,
                          GLTFMRMaterialSystemCreateInfo &materialSystemCreateInfo, GPUSceneData &sceneData);
        void Register(RendergraphBuilder *builder) override;

        std::shared_ptr<GLTFMRMaterialSystem> getMaterialSystemReference();

      private:
        // execution lambdas for run.
        void renderScene(PassExecution &passExec);

        std::shared_ptr<GLTFMRMaterialSystem> materialSystem;

        VkPipeline pipeline;
        VkPipelineLayout pipelineLayout;
        VkDescriptorSetLayout descriptorLayout;
        DescriptorAllocatorGrowable descriptorAllocator;
        DrawContext &drawContext;
        GPUResourceAllocator *gpuResourceAllocator;
        EngineStats stats;
        GPUSceneData &sceneData;

        // frame deletion queue?
    };
} // namespace rgraph
