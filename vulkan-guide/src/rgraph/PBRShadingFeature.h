#pragma once
#include "IFeature.h"

namespace rgraph
{

    /**
     * @brief An implementation of IFeature that does simple PBR Shading
     *
     */
    class PBRShadingFeature : public IFeature
    {
        void Register(RendergraphBuilder *builder) override;
    };
} // namespace rgraph
