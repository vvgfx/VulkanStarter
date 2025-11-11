#include "vk_engine.h"
#include "vk_initializers.h"
#include "vk_pipelines.h"
#include <PBREngine.h>

// {{{ GLTFMETALLIC_ROUGHNESS METHODS

void GLTFMetallic_Roughness::build_pipelines(VulkanEngine *engine)
{
    VkShaderModule meshFragShader;
    if (!vkutil::load_shader_module("../shaders/mesh.frag.spv", engine->_device, &meshFragShader))
        fmt::println("Error when building the triangle fragment shader module\n");

    VkShaderModule meshVertexShader;
    if (!vkutil::load_shader_module("../shaders/mesh.vert.spv", engine->_device, &meshVertexShader))
        fmt::println("Error when building the triangle vertex shader module\n");

    VkPushConstantRange matrixRange{};
    matrixRange.offset = 0;
    matrixRange.size = sizeof(GPUDrawPushConstants);
    matrixRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    DescriptorLayoutBuilder layoutBuilder;
    layoutBuilder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    layoutBuilder.add_binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    layoutBuilder.add_binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    materialLayout = layoutBuilder.build(engine->_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

    VkDescriptorSetLayout layouts[] = {engine->_gpuSceneDataDescriptorLayout, materialLayout};

    VkPipelineLayoutCreateInfo mesh_layout_info = vkinit::pipeline_layout_create_info();
    mesh_layout_info.setLayoutCount = 2;
    mesh_layout_info.pSetLayouts = layouts;
    mesh_layout_info.pPushConstantRanges = &matrixRange;
    mesh_layout_info.pushConstantRangeCount = 1;

    VkPipelineLayout newLayout;
    VK_CHECK(vkCreatePipelineLayout(engine->_device, &mesh_layout_info, nullptr, &newLayout));

    opaquePipeline.layout = newLayout;
    transparentPipeline.layout = newLayout;

    // build the stage-create-info for both vertex and fragment stages. This lets
    // the pipeline know the shader modules per stage
    PipelineBuilder pipelineBuilder;
    pipelineBuilder.set_shaders(meshVertexShader, meshFragShader);
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.set_multisampling_none();
    pipelineBuilder.disable_blending();
    pipelineBuilder.enable_depthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);

    // render format
    pipelineBuilder.set_color_attachment_format(engine->_drawImage.imageFormat);
    pipelineBuilder.set_depth_format(engine->_depthImage.imageFormat);

    // use the triangle layout we created
    pipelineBuilder._pipelineLayout = newLayout;

    // finally build the pipeline
    opaquePipeline.pipeline = pipelineBuilder.build_pipeline(engine->_device);

    // create the transparent variant
    pipelineBuilder.enable_blending_additive();

    pipelineBuilder.enable_depthtest(false, VK_COMPARE_OP_GREATER_OR_EQUAL);

    transparentPipeline.pipeline = pipelineBuilder.build_pipeline(engine->_device);

    vkDestroyShaderModule(engine->_device, meshFragShader, nullptr);
    vkDestroyShaderModule(engine->_device, meshVertexShader, nullptr);
}

MaterialInstance GLTFMetallic_Roughness::write_material(VkDevice device, MaterialPass pass,
                                                        const MaterialResources &resources,
                                                        DescriptorAllocatorGrowable &descriptorAllocator)
{
    MaterialInstance matData;
    matData.passType = pass;
    if (pass == MaterialPass::Transparent)
        matData.pipeline = &transparentPipeline;
    else
        matData.pipeline = &opaquePipeline;

    matData.materialSet = descriptorAllocator.allocate(device, materialLayout);

    writer.clear();
    writer.write_buffer(0, resources.dataBuffer, sizeof(MaterialConstants), resources.dataBufferOffset,
                        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.write_image(1, resources.colorImage.imageView, resources.colorSampler,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.write_image(2, resources.metalRoughImage.imageView, resources.metalRoughSampler,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    writer.update_set(device, matData.materialSet);

    return matData;
}

void GLTFMetallic_Roughness::clear_resources(VkDevice device)
{
    vkDestroyDescriptorSetLayout(device, materialLayout, nullptr);
    vkDestroyPipelineLayout(device, transparentPipeline.layout, nullptr);

    vkDestroyPipeline(device, transparentPipeline.pipeline, nullptr);
    vkDestroyPipeline(device, opaquePipeline.pipeline, nullptr);
}

// }}} END GLTFMETALLIC_ROUGHNESS METHODS

void PBREngine::init()
{

    VulkanEngine::init();

    std::string structurePath = {"..\\assets\\structure.glb"};
    auto structureFile = loadGltf(this, structurePath);

    assert(structureFile.has_value());

    loadedScenes["structure"] = *structureFile;
}

void PBREngine::init_pipelines()
{
    VulkanEngine::init_pipelines();

    metalRoughMaterial.build_pipelines(this);
}

void PBREngine::init_default_data()
{
    VulkanEngine::init_default_data();

    GLTFMetallic_Roughness::MaterialResources materialResources;
    // default the material textures
    materialResources.colorImage = _whiteImage;
    materialResources.colorSampler = _defaultSamplerLinear;
    materialResources.metalRoughImage = _whiteImage;
    materialResources.metalRoughSampler = _defaultSamplerLinear;

    // set the uniform buffer for the material data
    AllocatedBuffer materialConstants =
        _gpuResourceAllocator.create_buffer(sizeof(GLTFMetallic_Roughness::MaterialConstants),
                                            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    // write the buffer
    GLTFMetallic_Roughness::MaterialConstants *sceneUniformData =
        (GLTFMetallic_Roughness::MaterialConstants *)materialConstants.info.pMappedData;
    sceneUniformData->colorFactors = glm::vec4{1, 1, 1, 1};
    sceneUniformData->metal_rough_factors = glm::vec4{1, 0.5, 0, 0};

    _mainDeletionQueue.push_function([=, this]() { _gpuResourceAllocator.destroy_buffer(materialConstants); });

    materialResources.dataBuffer = materialConstants.buffer;
    materialResources.dataBufferOffset = 0;

    defaultData = metalRoughMaterial.write_material(_device, MaterialPass::MainColor, materialResources,
                                                    globalDescriptorAllocator);

    for (auto &m : testMeshes)
    {
        std::shared_ptr<GLTFMeshNode> newNode = std::make_shared<GLTFMeshNode>();
        newNode->mesh = m;

        newNode->localTransform = glm::mat4{1.f};
        newNode->worldTransform = glm::mat4{1.f};

        for (auto &s : newNode->mesh->surfaces)
            s.material = std::make_shared<GLTFMaterial>(defaultData);

        loadedNodes[m->name] = std::move(newNode);
    }
}

void PBREngine::cleanupOnChildren()
{

    loadedScenes.clear(); // UGLY!
    metalRoughMaterial.clear_resources(_device);
}

void PBREngine::update_scene()
{
    auto start = std::chrono::system_clock::now();

    VulkanEngine::update_scene();

    loadedScenes["structure"]->Draw(glm::mat4{1.f}, mainDrawContext);

    auto end = std::chrono::system_clock::now();

    // convert to microseconds (integer), and then come back to miliseconds
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    stats.scene_update_time = elapsed.count() / 1000.f;
}