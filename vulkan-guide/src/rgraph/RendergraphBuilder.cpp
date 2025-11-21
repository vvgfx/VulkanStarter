#include "RendergraphBuilder.h"
#include "IFeature.h"
#include "fmt/base.h"
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

// void rgraph::Pass::AddColorAttachment(const std::string name, bool store, VkClearValue *clear)
// {
//     // if store, write to color attachment, and if clearvalue is null, do not clear the value beforehand.
//     PassImageWrite imageWrite = {};
//     imageWrite.clear = clear;
//     imageWrite.store = store;
//     imageWrite.name = name;

//     colorAttachments.emplace_back(imageWrite);
// }

// void rgraph::Pass::AddDepthStencilAttachment(const std::string name, bool store, VkClearValue *clear)
// {
//     PassImageWrite imageWrite = {};
//     imageWrite.clear = clear;
//     imageWrite.store = store;
//     imageWrite.name = name;
// }

void rgraph::RendergraphBuilder::AddComputePass(const std::string name, std::function<void(Pass &)> setup,
                                                std::function<void(PassExecution &)> run)
{
    rgraph::Pass pass;
    pass.name = name;
    setup(pass);

    passData.emplace_back(pass);

    executionLambdas.emplace_back(run);
}

void rgraph::RendergraphBuilder::AddGraphicsPass(const std::string name, std::function<void(Pass &)> setup,
                                                 std::function<void(PassExecution &)> run)
{
    rgraph::Pass pass;
    pass.name = name;
    setup(pass);

    passData.emplace_back(pass);

    executionLambdas.emplace_back(run);
}

void rgraph::RendergraphBuilder::AddTrackedImage(const std::string name, VkImageLayout startLayout,
                                                 AllocatedImage *image)
{
    // I dont know why the startLayout is required, so ignoring it for now.
    images[name] = image;
}

void rgraph::RendergraphBuilder::AddTrackedBuffer(const std::string name, AllocatedBuffer *buffer)
{
    buffers[name] = buffer;
}

void rgraph::RendergraphBuilder::Build()
{

    // need to insert image transitions between the features.

    // so this loops through the different required IFeatures, then calls the setup lambdas, then finally inserts the
    // transitions.
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
    }
}

void rgraph::RendergraphBuilder::Run()
{
    // actually call the execution lambdas here.
    // Ideally this should already contain the transition stuff.
    for (size_t i = 0; i < passData.size(); i++)
    {
        const Pass &pass = passData[i];

        // Insert transitions for this pass
        auto transitionsIt = transitionData.find(pass.name);
        if (transitionsIt != transitionData.end())
        {
            for (const auto &transition : transitionsIt->second)
            {
                AllocatedImage *img = images[transition.imageName];
                // vkutil::transition_image(cmd, img->image, transition.currentLayout, transition.newLayout);
                fmt::println("Create a transition once");
            }
        }

        // Create unique PassExecution for this pass
        PassExecution exec;
        exec.cmd = cmd;
        exec._device = _device;
        exec._drawExtent = _extent;
        exec.allocatedImages = images;
        exec.allocatedBuffers = buffers;

        // Execute the pass with its own context
        fmt::println("Execute once.");
        // executionLambdas[i](exec);
    }
}

void rgraph::RendergraphBuilder::AddFeature(std::weak_ptr<IFeature> feature)
{
    features.emplace_back(feature);
}

void rgraph::RendergraphBuilder::setReqData(VkCommandBuffer cmd, VkDevice _device, VkExtent2D _extent)
{
    this->cmd = cmd;
    this->_device = _device;
    this->_extent = _extent;
}