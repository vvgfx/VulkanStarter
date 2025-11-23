#pragma once

#include "MaterialSystem.h"
#include "rgraph/ComputeBackgroundFeature.h"
#include "rgraph/PBRShadingFeature.h"
#include "rgraph/RendergraphBuilder.h"
#include <memory>
#include <vk_descriptors.h>
#include <vk_engine.h>
#include <vk_types.h>

class PBREngine : public VulkanEngine
{
  public:
    GLTFMRMaterialSystem materialSystemInstance;
    void init() override;

  protected:
    // functions
    void init_pipelines() override;

    void init_default_data() override;

    void cleanupOnChildren() override;

    void update_scene() override;

    // gltf data
    std::unordered_map<std::string, std::shared_ptr<sgraph::GLTFScene>> loadedScenes;

    rgraph::RendergraphBuilder builder;
    std::shared_ptr<rgraph::ComputeBackgroundFeature> testComputeFeature;
    std::shared_ptr<rgraph::PBRShadingFeature> testPBRFeature;

    void testRendergraph();
};