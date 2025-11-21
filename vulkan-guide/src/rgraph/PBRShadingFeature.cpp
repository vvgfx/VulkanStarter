#include "PBRShadingFeature.h"
#include "RendergraphBuilder.h"
#include "vk_engine.h"

rgraph::PBRShadingFeature::PBRShadingFeature(DrawContext &drwCtx, VkDevice _device, DeletionQueue &delQueue)
    : drawContext(drwCtx)
{
}

void rgraph::PBRShadingFeature::Register(rgraph::RendergraphBuilder *builder)
{
}

void rgraph::PBRShadingFeature::InitPipeline(VkDevice _device, DeletionQueue &delQueue)
{
}