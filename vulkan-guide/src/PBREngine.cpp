#include "MaterialSystem.h"
#include "fmt/base.h"
#include "glm/ext/matrix_float4x4.hpp"
#include "rgraph/ComputeBackgroundFeature.h"
#include "rgraph/PBRShadingFeature.h"
#include "sgraph/Scenegraph.h"
#include "sgraph/ScenegraphImporter.h"
#include "sgraph/ScenegraphStructs.h"
#include "vk_engine.h"
#include "vk_initializers.h"
#include "vk_loader.h"
#include "vk_pipelines.h"
#include "vk_types.h"
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
    testRendergraph();
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

void PBREngine::testRendergraph()
{

    // need test images for drawImage and depthImage.
    AllocatedImage testDrawImage, testDepthImage;

    VkExtent3D drawImageExtent = {_windowExtent.width, _windowExtent.height, 1};

    // hardcoding the draw format to 32 bit float
    testDrawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    testDrawImage.imageExtent = drawImageExtent;

    VkImageUsageFlags drawImageUsages{};
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    VkImageCreateInfo rimg_info =
        vkinit::image_create_info(testDrawImage.imageFormat, drawImageUsages, drawImageExtent);

    // for the draw image, we want to allocate it from gpu local memory
    VmaAllocationCreateInfo rimg_allocinfo = {};
    rimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    rimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // allocate and create the image
    _gpuResourceAllocator.create_image(&rimg_info, &rimg_allocinfo, &testDrawImage.image, &testDrawImage.allocation,
                                       nullptr);

    // build a image-view for the draw image to use for rendering
    VkImageViewCreateInfo rview_info =
        vkinit::imageview_create_info(testDrawImage.imageFormat, testDrawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);

    VK_CHECK(vkCreateImageView(_device, &rview_info, nullptr, &testDrawImage.imageView));

    // similarly, create the depth image

    testDepthImage.imageFormat = VK_FORMAT_D32_SFLOAT;
    testDepthImage.imageExtent = drawImageExtent;
    VkImageUsageFlags depthImageUsages{};
    depthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    VkImageCreateInfo dimg_info =
        vkinit::image_create_info(testDepthImage.imageFormat, depthImageUsages, drawImageExtent);

    // allocate and create the image
    _gpuResourceAllocator.create_image(&dimg_info, &rimg_allocinfo, &testDepthImage.image, &testDepthImage.allocation,
                                       nullptr);

    // build a image-view for the draw image to use for rendering
    VkImageViewCreateInfo dview_info =
        vkinit::imageview_create_info(testDepthImage.imageFormat, testDepthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);

    VK_CHECK(vkCreateImageView(_device, &dview_info, nullptr, &testDepthImage.imageView));

    // test image creation end ----------------------------------------------------------------------------------

    VkExtent3D extent = {_windowExtent.width, _windowExtent.height, 1};
    testComputeFeature = make_shared<rgraph::ComputeBackgroundFeature>(_device, _mainDeletionQueue, extent);
    GLTFMRMaterialSystemCreateInfo msCreateInfo = {_device, testDrawImage.imageFormat, testDepthImage.imageFormat,
                                                   _gpuSceneDataDescriptorLayout};
    testPBRFeature = make_shared<rgraph::PBRShadingFeature>(mainDrawContext, _device, msCreateInfo, sceneData,
                                                            _gpuSceneDataDescriptorLayout);
    builder.AddTrackedImage("drawImage", VK_IMAGE_LAYOUT_UNDEFINED, testDrawImage);
    builder.AddTrackedImage("depthImage", VK_IMAGE_LAYOUT_UNDEFINED, testDepthImage);
    builder.setReqData(_device, testDrawImage.imageExtent, getGPUResourceAllocator());
    builder.AddFeature(testComputeFeature);
    builder.AddFeature(testPBRFeature);
    builder.Build(get_current_frame());
    builder.Run(get_current_frame());
    get_current_frame()._deletionQueue.flush();
    testPBRFeature.get()->getMaterialSystemReference()->clear_resources(_device);

    // destroy temp resources ----------------------------------------------------------------------------------------

    vkDestroyImageView(_device, testDrawImage.imageView, nullptr);
    _gpuResourceAllocator.destroy_image(testDrawImage.image, testDrawImage.allocation);

    vkDestroyImageView(_device, testDepthImage.imageView, nullptr);
    _gpuResourceAllocator.destroy_image(testDepthImage.image, testDepthImage.allocation);
}