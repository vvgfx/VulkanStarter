#include "RendergraphBuilder.h"
#include "GPUResourceAllocator.h"
#include "IFeature.h"
#include "fmt/base.h"
#include "vk_engine.h"
#include "vk_images.h"
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
    // actually call the execution lambdas here.
    // Ideally this should already contain the transition stuff.
    for (size_t i = 0; i < passData.size(); i++)
    {

        fmt::println("New pass");
        const Pass &pass = passData[i];

        // Insert transitions for this pass
        auto transitionsIt = transitionData.find(pass.name);
        if (transitionsIt != transitionData.end())
        {
            for (const auto &transition : transitionsIt->second)
            {
                AllocatedImage img = images[transition.imageName];
                // vkutil::transition_image(cmd, img->image, transition.currentLayout, transition.newLayout);
                // std::string deb = "Create a transition once for " + transition.imageName;
                fmt::println("Create a transition once for {}", transition.imageName);
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
                fmt::println("creating a new buffer! {}", bufferCreateInfo.name);
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
        exec.allocatedBuffers = buffers;
        exec.delQueue = &(frameData._deletionQueue);
        exec.frameDescriptor = &(frameData._frameDescriptors);

        // Execute the pass with its own context
        fmt::println("Execute once.");
        // executionLambdas[i](exec);
    }
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