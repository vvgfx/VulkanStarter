#include "PBRShadingFeature.h"
#include "MaterialSystem.h"
#include "RendergraphBuilder.h"
#include "vk_engine.h"
#include <algorithm>
#include <memory>

// forward declaration for now, TODO: come back and move this function here. remove from VKEngine.
bool is_visible(const RenderObject &obj, const glm::mat4 &viewproj);

rgraph::PBRShadingFeature::PBRShadingFeature(DrawContext &drwCtx, VkDevice _device, DeletionQueue &delQueue,
                                             GLTFMRMaterialSystemCreateInfo &materialSystemCreateInfo,
                                             GPUSceneData &scnData, VkDescriptorSetLayout gpuSceneLayout)
    : drawContext(drwCtx), sceneData(scnData)
{
    _gpuSceneDataDescriptorLayout = gpuSceneLayout;
    materialSystem = std::make_shared<GLTFMRMaterialSystem>();
    materialSystem->build_pipelines(materialSystemCreateInfo);
}

void rgraph::PBRShadingFeature::Register(rgraph::RendergraphBuilder *builder)
{
    builder->AddGraphicsPass(
        "renderPass",
        [](Pass &pass)
        {
            pass.AddColorAttachment("drawImage", true);
            pass.AddDepthStencilAttachment("depthImage", true);
            pass.CreatesBuffer("gpuSceneBuffer", sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        },
        [&](PassExecution &passExec) { renderScene(passExec); });
}

std::shared_ptr<GLTFMRMaterialSystem> rgraph::PBRShadingFeature::getMaterialSystemReference()
{
    return materialSystem;
}

void rgraph::PBRShadingFeature::renderScene(rgraph::PassExecution &passExec)
{

    // reset counters
    stats.drawcall_count = 0;
    stats.triangle_count = 0;

    std::vector<uint32_t> opaque_draws;
    opaque_draws.reserve(drawContext.OpaqueSurfaces.size());

    for (int i = 0; i < drawContext.OpaqueSurfaces.size(); i++)
        if (is_visible(drawContext.OpaqueSurfaces[i], sceneData.viewproj))
            opaque_draws.push_back(i);

    // sort the opaque surfaces by material and mesh
    std::sort(opaque_draws.begin(), opaque_draws.end(),
              [&](const auto &iA, const auto &iB)
              {
                  const RenderObject &A = drawContext.OpaqueSurfaces[iA];
                  const RenderObject &B = drawContext.OpaqueSurfaces[iB];
                  if (A.material == B.material)
                      return A.indexBuffer < B.indexBuffer;
                  else
                      return A.material < B.material;
              });

    // ALL THESE WILL BE DONE BY THE RENDERGRAPH NOW
    /**
     *
    VkRenderingAttachmentInfo colorAttachment =
        vkinit::attachment_info(_drawImage.imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    // setup depth attachment similarly
    VkRenderingAttachmentInfo depthAttachment =
        vkinit::depth_attachment_info(_depthImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    VkRenderingInfo renderInfo = vkinit::rendering_info(_drawExtent, &colorAttachment, &depthAttachment);
    vkCmdBeginRendering(cmd, &renderInfo);
    */
    // draw scene ----------------------
    // The rendergraph will now create the buffers for you.
    AllocatedBuffer gpuSceneDataBuffer = passExec.allocatedBuffers["gpuSceneDataBuffer"];

    // add it to the deletion queue of this frame so it gets deleted once its been used
    // TODO: Remember to flush this in the main rendergraph.
    // get_current_frame()._deletionQueue.push_function([=, this]()
    //                                                  { _gpuResourceAllocator.destroy_buffer(gpuSceneDataBuffer); });

    // write the buffer

    GPUSceneData *sceneUniformData = (GPUSceneData *)gpuSceneDataBuffer.info.pMappedData;
    *sceneUniformData = sceneData;

    // // create a descriptor set that binds that buffer and update it
    VkDescriptorSet globalDescriptor = passExec.frameDescriptor->allocate(
        passExec._device, _gpuSceneDataDescriptorLayout); // temporarily getting the layout through the constructor.
                                                          // Will need to figure out a better way later.

    DescriptorWriter writer;
    writer.write_buffer(0, gpuSceneDataBuffer.buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.update_set(passExec._device, globalDescriptor);

    // defined outside of the draw function, this is the state we will try to skip
    MaterialPipeline *lastPipeline = nullptr;
    MaterialInstance *lastMaterial = nullptr;
    VkBuffer lastIndexBuffer = VK_NULL_HANDLE;

    auto draw = [&](const RenderObject &r)
    {
        if (r.material != lastMaterial)
        {
            lastMaterial = r.material;
            // rebind pipeline and descriptors if the material changed
            if (r.material->pipeline != lastPipeline)
            {

                lastPipeline = r.material->pipeline;
                vkCmdBindPipeline(passExec.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.material->pipeline->pipeline);
                vkCmdBindDescriptorSets(passExec.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.material->pipeline->layout, 0,
                                        1, &globalDescriptor, 0, nullptr);

                VkViewport viewport = {};
                viewport.x = 0;
                viewport.y = 0;
                viewport.width = (float)passExec._drawExtent.width;
                viewport.height = (float)passExec._drawExtent.height;
                viewport.minDepth = 0.f;
                viewport.maxDepth = 1.f;

                vkCmdSetViewport(passExec.cmd, 0, 1, &viewport);

                VkRect2D scissor = {};
                scissor.offset.x = 0;
                scissor.offset.y = 0;
                scissor.extent.width = passExec._drawExtent.width;
                scissor.extent.height = passExec._drawExtent.height;

                vkCmdSetScissor(passExec.cmd, 0, 1, &scissor);
            }

            vkCmdBindDescriptorSets(passExec.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.material->pipeline->layout, 1, 1,
                                    &r.material->materialSet, 0, nullptr);
        }
        // rebind index buffer if needed
        if (r.indexBuffer != lastIndexBuffer)
        {
            lastIndexBuffer = r.indexBuffer;
            vkCmdBindIndexBuffer(passExec.cmd, r.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        }
        // calculate final mesh matrix
        GPUDrawPushConstants push_constants;
        push_constants.worldMatrix = r.transform;
        push_constants.vertexBuffer = r.vertexBufferAddress;

        vkCmdPushConstants(passExec.cmd, r.material->pipeline->layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(GPUDrawPushConstants), &push_constants);

        vkCmdDrawIndexed(passExec.cmd, r.indexCount, 1, r.firstIndex, 0, 0);
        // stats
        stats.drawcall_count++;
        stats.triangle_count += r.indexCount / 3;
    };

    for (auto &r : opaque_draws)
        draw(drawContext.OpaqueSurfaces[r]);

    for (auto &r : drawContext.TransparentSurfaces)
        draw(r);

    vkCmdEndRendering(passExec.cmd);
}