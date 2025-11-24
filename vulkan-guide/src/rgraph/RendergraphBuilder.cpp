#include "RendergraphBuilder.h"
#include "GPUResourceAllocator.h"
#include "IFeature.h"
#include "fmt/base.h"
#include "vk_engine.h"
#include "vk_images.h"
#include "vk_initializers.h"
#include "vk_types.h"
#include <memory>
#include <string>
#include <unordered_map>

void rgraph::Pass::ReadsImage(const std::string name, VkImageLayout layout)
{
    PassImageRead imageRead = {};
    imageRead.name = name;
    imageRead.startingLayout = layout;

    imageReads.emplace_back(imageRead);
}

void rgraph::Pass::WritesImage(const std::string name)
{
    PassImageWrite imageWrite = {};
    imageWrite.name = name;

    imageWrites.emplace_back(imageWrite);
}

void rgraph::Pass::AddColorAttachment(const std::string name, bool store, VkClearValue *clear)
{
    // if store, write to color attachment, and if clearvalue is null, do not clear the value beforehand.
    PassImageWrite imageWrite = {};
    imageWrite.clear = clear;
    imageWrite.store = store;
    imageWrite.name = name;

    colorAttachments.emplace_back(imageWrite);
}

void rgraph::Pass::AddDepthStencilAttachment(const std::string name, bool store, VkClearValue *clear)
{
    depthAttachment.clear = clear;
    depthAttachment.store = store;
    depthAttachment.name = name;
}

void rgraph::RendergraphBuilder::AddComputePass(const std::string name, std::function<void(Pass &)> setup,
                                                std::function<void(PassExecution &)> run)
{
    rgraph::Pass pass;
    pass.type = PassType::Compute;
    pass.name = name;
    setup(pass);

    passData.emplace_back(pass);

    executionLambdas.emplace_back(run);
}

void rgraph::RendergraphBuilder::AddGraphicsPass(const std::string name, std::function<void(Pass &)> setup,
                                                 std::function<void(PassExecution &)> run)
{
    rgraph::Pass pass;
    pass.type = PassType::Graphics;
    pass.name = name;
    setup(pass);

    passData.emplace_back(pass);

    executionLambdas.emplace_back(run);
}

void rgraph::RendergraphBuilder::AddTrackedImage(const std::string name, VkImageLayout startLayout,
                                                 AllocatedImage image)
{
    // I dont know why the startLayout is required, so ignoring it for now.
    images[name] = image;
}

void rgraph::RendergraphBuilder::AddTrackedBuffer(const std::string name, AllocatedBuffer buffer)
{
    buffers[name] = buffer;
}

void rgraph::RendergraphBuilder::Build(FrameData &frameData)
{
    // fmt::println("Build called");
    transitionData.clear();
    passData.clear();
    executionLambdas.clear();
    // buffers.clear();
    // need to insert image transitions between the features.

    // so this loops through the different required IFeatures, then calls the setup lambdas, then finally inserts
    // the transitions.
    /// currently this depends on the order in which features are added.
    for (auto &feature : features)
        feature.lock()->Register(this);

    // all the AddXPass would be called above.

    std::unordered_map<std::string, VkImageLayout> imgLayoutMap;

    for (auto &image : images)
        imgLayoutMap[image.first] = VK_IMAGE_LAYOUT_UNDEFINED;

    for (auto &pass : passData)
    {
        transitionData[pass.name] = {};
        // get the image writes.
        for (auto &writeImage : pass.imageWrites)
        {
            // all these will be in general.
            if (imgLayoutMap[writeImage.name] != VK_IMAGE_LAYOUT_GENERAL)
            {
                // add a transition here.
                transitionData[pass.name].push_back(
                    {writeImage.name, imgLayoutMap[writeImage.name], VK_IMAGE_LAYOUT_GENERAL});
                imgLayoutMap[writeImage.name] = VK_IMAGE_LAYOUT_GENERAL;
            }
        }

        // get the image reads.
        for (auto &readImage : pass.imageReads)
        {
            // need to check if current layout and new layout are same
            if (imgLayoutMap[readImage.name] != readImage.startingLayout)
            {
                // add a transition here.
                transitionData[pass.name].push_back(
                    {readImage.name, imgLayoutMap[readImage.name], readImage.startingLayout});
                imgLayoutMap[readImage.name] = readImage.startingLayout;
            }
        }

        // get the color and depth attachments later, they should be in color/depth _optimal

        for (auto &colorImage : pass.colorAttachments)
        {
            if (imgLayoutMap[colorImage.name] != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
            {
                // transition to color layout here.
                transitionData[pass.name].push_back(
                    {colorImage.name, imgLayoutMap[colorImage.name], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
                imgLayoutMap[colorImage.name] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            }
        }

        // similarly, now do the same for depth image. Don't need a loop because the depth image will always be
        // singular.
        if (imgLayoutMap.contains(pass.depthAttachment.name) &&
            imgLayoutMap[pass.depthAttachment.name] != VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
        {
            // transition data.
            transitionData[pass.name].push_back({pass.depthAttachment.name, imgLayoutMap[pass.depthAttachment.name],
                                                 VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL});
            imgLayoutMap[pass.depthAttachment.name] = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        }
    }
}

void rgraph::RendergraphBuilder::Run(FrameData &frameData)
{
    // fmt::println("run called");
    // actually call the execution lambdas here.
    // Ideally this should already contain the transition stuff.
    VkCommandBuffer cmd = frameData._mainCommandBuffer;
    VkQueryPool queryPool = frameData.timestampQueryPool;

    VK_CHECK(vkResetCommandBuffer(cmd, 0));

    VkCommandBufferBeginInfo cmdBeginInfo =
        vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    // start command buffer recording ---------------------
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    uint32_t timestampCount = passData.size() * 2 + 2;
    vkCmdResetQueryPool(cmd, queryPool, 0, timestampCount);

    std::vector<std::pair<std::string, uint32_t>> passIndices;
    uint32_t queryIndex = 0;

    uint32_t totalStartQuery = queryIndex++;
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queryPool, totalStartQuery);

    for (size_t i = 0; i < passData.size(); i++)
    {

        // fmt::println("New pass");
        const Pass &pass = passData[i];

        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queryPool, queryIndex);
        uint32_t startQuery = queryIndex++;

        // Insert transitions for this pass
        auto transitionsIt = transitionData.find(pass.name);
        if (transitionsIt != transitionData.end())
        {
            for (const auto &transition : transitionsIt->second)
            {
                AllocatedImage img = images[transition.imageName];
                vkutil::transition_image(frameData._mainCommandBuffer, img.image, transition.currentLayout,
                                         transition.newLayout);
                // fmt::println("Create a transition once for {} from {} to {}", transition.imageName,
                //  static_cast<int>(transition.currentLayout), static_cast<int>(transition.newLayout));
            }
        }

        // Create buffers if required.
        PassExecution exec;
        // potential for memory aliasing here.
        if (pass.bufferCreations.size() > 0)
        {
            // create the buffers.
            for (auto &bufferCreateInfo : pass.bufferCreations)
            {
                // fmt::println("creating a new buffer! {}", bufferCreateInfo.name);
                AllocatedBuffer newBuffer = gpuResourceAllocator->create_buffer(
                    bufferCreateInfo.size, bufferCreateInfo.usageFlags, VMA_MEMORY_USAGE_CPU_TO_GPU);
                exec.allocatedBuffers[bufferCreateInfo.name] = newBuffer;
                frameData._deletionQueue.push_function([=, this]()
                                                       { gpuResourceAllocator->destroy_buffer(newBuffer); });
            }
        }

        // Create unique PassExecution for this pass
        // PassExecution exec;
        exec.cmd = frameData._mainCommandBuffer;
        exec._device = _device;
        exec._drawExtent = _extent;
        exec.allocatedImages = images;
        // exec.allocatedBuffers = buffers;
        exec.delQueue = &(frameData._deletionQueue);
        exec.frameDescriptor = &(frameData._frameDescriptors);

        // Execute the pass with its own context
        // fmt::println("Execute once.");
        if (pass.type == PassType::Graphics)
        {
            // TODO: Support multiple color attachments

            AllocatedImage drawImage = images[pass.colorAttachments[0].name];
            AllocatedImage depthImage = images[pass.depthAttachment.name];

            VkRenderingAttachmentInfo colorAttachment =
                vkinit::attachment_info(drawImage.imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

            // setup depth attachment similarly
            VkRenderingAttachmentInfo depthAttachment =
                vkinit::depth_attachment_info(depthImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

            VkRenderingInfo renderInfo =
                vkinit::rendering_info({_extent.width, _extent.height}, &colorAttachment, &depthAttachment);
            vkCmdBeginRendering(cmd, &renderInfo);
        }
        executionLambdas[i](exec);

        if (pass.type == PassType::Graphics)
            vkCmdEndRendering(cmd);

        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool, queryIndex);
        queryIndex++;

        passIndices.push_back({pass.name, startQuery});
    }

    uint32_t totalEndQuery = queryIndex++;
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool, totalEndQuery);

    frameData.timestampCount = timestampCount;
    frameData.passIndices = passIndices;
    // fmt::println("frame done.");
    // commenting this out for now, will change later
    // TODO: move swapchain transitions into the rendergraph.
    // VK_CHECK(vkEndCommandBuffer(cmd));
}

void rgraph::RendergraphBuilder::AddFeature(std::weak_ptr<IFeature> feature)
{
    features.emplace_back(feature);
}

void rgraph::RendergraphBuilder::setReqData(VkDevice _device, VkExtent3D _extent, GPUResourceAllocator *gpuAllocator)
{
    this->_device = _device;
    this->_extent = _extent;
    this->gpuResourceAllocator = gpuAllocator;
}

void rgraph::Pass::CreatesBuffer(const std::string name, size_t size, VkBufferUsageFlags usages)
{
    PassBufferCreationInfo bufferCreateInfo = {};
    bufferCreateInfo.name = name;
    bufferCreateInfo.size = size;
    bufferCreateInfo.usageFlags = usages;

    bufferCreations.emplace_back(bufferCreateInfo);
}

void rgraph::Pass::ReadsBuffer(const std::string name)
{
    // do nothing right now, not sure where these are used yet.
}

void rgraph::Pass::WritesBuffer(const std::string name)
{
    // do nothing right now, not sure where these are used yet.
}

void rgraph::RendergraphBuilder::ReadTimestamps(VkDevice device, VkQueryPool queryPool, uint32_t queryCount,
                                                const std::vector<std::pair<std::string, uint32_t>> &passIndices,
                                                const std::pair<uint32_t, uint32_t> &totalIndices)
{
    if (queryCount == 0)
        return;

    timestampBuffer.resize(queryCount);

    VkResult result = vkGetQueryPoolResults(device, queryPool, 0, queryCount, timestampBuffer.size() * sizeof(uint64_t),
                                            timestampBuffer.data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);

    if (result != VK_SUCCESS)
        return;

    lastFrameTimings.clear();

    // Calculate total GPU time
    uint64_t totalStart = timestampBuffer[totalIndices.first];
    uint64_t totalEnd = timestampBuffer[totalIndices.second];
    uint64_t totalDuration = (totalEnd >= totalStart) ? (totalEnd - totalStart) : (UINT64_MAX - totalStart + totalEnd);

    totalGpuMs = totalDuration * timestampPeriod / 1000000.0f;

    // Calculate per-pass times
    for (const auto &[passName, startIdx] : passIndices)
    {
        uint64_t startTime = timestampBuffer[startIdx];
        uint64_t endTime = timestampBuffer[startIdx + 1];
        uint64_t duration = (endTime >= startTime) ? (endTime - startTime) : (UINT64_MAX - startTime + endTime);

        float gpuMs = duration * timestampPeriod / 1000000.0f;

        PassTiming timing;
        timing.name = passName;
        timing.gpuMs = gpuMs;
        lastFrameTimings.push_back(timing);
    }
}