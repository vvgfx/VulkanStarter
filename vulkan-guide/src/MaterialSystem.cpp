#include "MaterialSystem.h"
#include "vk_initializers.h"
#include "vk_pipelines.h"

void GLTFMRMaterialSystem::build_pipelines(GLTFMRMaterialSystemCreateInfo &info)
{
    VkShaderModule meshFragShader;
    if (!vkutil::load_shader_module("../shaders/mesh.frag.spv", info._device, &meshFragShader))
        fmt::println("Error when building the triangle fragment shader module\n");

    VkShaderModule meshVertexShader;
    if (!vkutil::load_shader_module("../shaders/mesh.vert.spv", info._device, &meshVertexShader))
        fmt::println("Error when building the triangle vertex shader module\n");

    VkPushConstantRange matrixRange{};
    matrixRange.offset = 0;
    matrixRange.size = sizeof(GPUDrawPushConstants);
    matrixRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    DescriptorLayoutBuilder layoutBuilder;
    layoutBuilder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    layoutBuilder.add_binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    layoutBuilder.add_binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    materialLayout = layoutBuilder.build(info._device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

    VkDescriptorSetLayout layouts[] = {info._gpuSceneDataDescriptorLayout, materialLayout};

    VkPipelineLayoutCreateInfo mesh_layout_info = vkinit::pipeline_layout_create_info();
    mesh_layout_info.setLayoutCount = 2;
    mesh_layout_info.pSetLayouts = layouts;
    mesh_layout_info.pPushConstantRanges = &matrixRange;
    mesh_layout_info.pushConstantRangeCount = 1;

    VkPipelineLayout newLayout;
    VK_CHECK(vkCreatePipelineLayout(info._device, &mesh_layout_info, nullptr, &newLayout));

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
    pipelineBuilder.set_color_attachment_format(info.colorFormat);
    pipelineBuilder.set_depth_format(info.depthFormat);

    // use the triangle layout we created
    pipelineBuilder._pipelineLayout = newLayout;

    // finally build the pipeline
    opaquePipeline.pipeline = pipelineBuilder.build_pipeline(info._device);

    // create the transparent variant
    pipelineBuilder.enable_blending_additive();

    pipelineBuilder.enable_depthtest(false, VK_COMPARE_OP_GREATER_OR_EQUAL);

    transparentPipeline.pipeline = pipelineBuilder.build_pipeline(info._device);

    vkDestroyShaderModule(info._device, meshFragShader, nullptr);
    vkDestroyShaderModule(info._device, meshVertexShader, nullptr);
}

MaterialInstance GLTFMRMaterialSystem::write_material(VkDevice device, MaterialPass pass,
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

void GLTFMRMaterialSystem::clear_resources(VkDevice device)
{
    vkDestroyDescriptorSetLayout(device, materialLayout, nullptr);
    vkDestroyPipelineLayout(device, transparentPipeline.layout, nullptr);

    vkDestroyPipeline(device, transparentPipeline.pipeline, nullptr);
    vkDestroyPipeline(device, opaquePipeline.pipeline, nullptr);
}