#pragma once
#include "GPUResourceAllocator.h"
#include "IFeature.h"
#include "MaterialSystem.h"
#include "vk_descriptors.h"
#include "vk_engine.h"
#include "vk_types.h"
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
        PBRShadingFeature(DrawContext &drwCtx, VkDevice _device,
                          GLTFMRMaterialSystemCreateInfo &materialSystemCreateInfo, GPUSceneData &sceneData,
                          VkDescriptorSetLayout gpuSceneLayout, DeletionQueue &delQueue);

        void Register(RendergraphBuilder *builder) override;

        std::shared_ptr<GLTFMRMaterialSystem> getMaterialSystemReference();

      private:
        void createPipelines(GLTFMRMaterialSystemCreateInfo &materialSystemCreateInfo);
        // execution lambdas for run.
        void renderScene(PassExecution &passExec);

        std::shared_ptr<GLTFMRMaterialSystem> materialSystem;

        MaterialPipeline opaquePipeline;
        MaterialPipeline transparentPipeline;

        VkDescriptorSetLayout descriptorLayout;
        VkDescriptorSetLayout _gpuSceneDataDescriptorLayout;
        DescriptorAllocatorGrowable descriptorAllocator;
        DrawContext &drawContext;
        GPUResourceAllocator *gpuResourceAllocator;
        GPUSceneData &sceneData;

        // frame deletion queue?
    };
} // namespace rgraph
