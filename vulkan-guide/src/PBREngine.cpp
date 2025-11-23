#include "MaterialSystem.h"
#include "fmt/base.h"
#include "glm/ext/matrix_float4x4.hpp"
#include "rgraph/ComputeBackgroundFeature.h"
#include "sgraph/Scenegraph.h"
#include "sgraph/ScenegraphImporter.h"
#include "sgraph/ScenegraphStructs.h"
#include "vk_engine.h"
#include "vk_initializers.h"
#include "vk_loader.h"
#include "vk_pipelines.h"
#include <PBREngine.h>
#include <fstream>
#include <iostream>
#include <memory>

void PBREngine::init()
{

    VulkanEngine::init();

    std::string structurePath = {"..\\assets\\outpostWithLights.glb"};

    GLTFCreatorData creatorData = {};

    creatorData._defaultSamplerLinear = _defaultSamplerLinear;
    creatorData.defaultImage = _whiteImage;
    creatorData.loadErrorImage = _errorCheckerboardImage;
    creatorData._device = _device;
    creatorData.gpuResourceAllocator = getGPUResourceAllocator();
    creatorData.materialSystemReference =
        &materialSystemInstance; // this would change to a reference from the material system in PBRShadingFeature.

    // this is called after the pipelines are initialzed.
    auto structureFile = loadGltf(creatorData, structurePath);

    assert(structureFile.has_value());

    loadedScenes["outpost"] = *structureFile;

    structureFile.value()->name = "outpost";

    // testing rendergraph build.
    VkExtent3D extent = {_windowExtent.width, _windowExtent.height, 1};
    testComputeFeature = make_shared<rgraph::ComputeBackgroundFeature>(_device, _mainDeletionQueue, extent);
    builder.AddTrackedImage("drawImage", VK_IMAGE_LAYOUT_UNDEFINED, _drawImage);
    builder.AddTrackedImage("depthImage", VK_IMAGE_LAYOUT_UNDEFINED, _depthImage);
    builder.AddFeature(testComputeFeature);
    builder.Build(get_current_frame());
    builder.Run(get_current_frame());
    get_current_frame()._deletionQueue.flush();
}

void PBREngine::init_pipelines()
{
    VulkanEngine::init_pipelines();

    GLTFMRMaterialSystemCreateInfo matSysCreateInfo = {};
    matSysCreateInfo._device = _device;
    matSysCreateInfo._gpuSceneDataDescriptorLayout = _gpuSceneDataDescriptorLayout;
    matSysCreateInfo.colorFormat = _drawImage.imageFormat;
    matSysCreateInfo.depthFormat = _depthImage.imageFormat;
    materialSystemInstance.build_pipelines(matSysCreateInfo);
}

void PBREngine::init_default_data()
{
    VulkanEngine::init_default_data();

    GLTFMRMaterialSystem::MaterialResources materialResources;
    // default the material textures
    materialResources.colorImage = _whiteImage;
    materialResources.colorSampler = _defaultSamplerLinear;
    materialResources.metalRoughImage = _whiteImage;
    materialResources.metalRoughSampler = _defaultSamplerLinear;

    // set the uniform buffer for the material data
    AllocatedBuffer materialConstants =
        _gpuResourceAllocator.create_buffer(sizeof(GLTFMRMaterialSystem::MaterialConstants),
                                            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    // write the buffer
    GLTFMRMaterialSystem::MaterialConstants *sceneUniformData =
        (GLTFMRMaterialSystem::MaterialConstants *)materialConstants.info.pMappedData;
    sceneUniformData->colorFactors = glm::vec4{1, 1, 1, 1};
    sceneUniformData->metal_rough_factors = glm::vec4{1, 0.5, 0, 0};

    _mainDeletionQueue.push_function([=, this]() { _gpuResourceAllocator.destroy_buffer(materialConstants); });

    materialResources.dataBuffer = materialConstants.buffer;
    materialResources.dataBufferOffset = 0;

    defaultData = materialSystemInstance.write_material(_device, MaterialPass::MainColor, materialResources,
                                                        globalDescriptorAllocator);
}

void PBREngine::cleanupOnChildren()
{

    loadedScenes.clear();
    materialSystemInstance.clear_resources(_device);
}

void PBREngine::update_scene()
{
    auto start = std::chrono::system_clock::now();

    VulkanEngine::update_scene();

    loadedScenes["outpost"]->Draw(glm::mat4{1.f}, mainDrawContext);

    auto end = std::chrono::system_clock::now();

    // convert to microseconds (integer), and then come back to miliseconds
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    stats.scene_update_time = elapsed.count() / 1000.f;
}