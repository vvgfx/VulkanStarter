#include "RendergraphBuilder.h"
#include "IFeature.h"
#include "vk_types.h"
#include <memory>
#include <string>

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
    setup(pass);

    passData.emplace_back(pass);

    executionLambdas.emplace_back(run);
}

void rgraph::RendergraphBuilder::AddGraphicsPass(const std::string name, std::function<void(Pass &)> setup,
                                                 std::function<void(PassExecution &)> run)
{
    rgraph::Pass pass;
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
    // create a queue of functions?

    // need to insert image transitions between the features.

    // so this loops through the different required IFeatures, then calls the setup lambdas, then finally inserts the
    // transitions.
    /// currently this depends on the order in which features are added.
    for (auto &feature : features)
        feature.lock()->Register(this);

    // all the AddXPass would be called above.

    //  figure out the transitions here.
}

void rgraph::RendergraphBuilder::Run()
{
    // actually call the execution lambdas here.
    // Ideally this should already contain the transition stuff.
}

void rgraph::RendergraphBuilder::AddFeature(std::weak_ptr<IFeature> feature)
{
    features.emplace_back(feature);
}