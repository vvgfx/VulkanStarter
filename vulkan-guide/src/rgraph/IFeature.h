#pragma once

#include "RendergraphBuilder.h"

namespace rgraph
{
    /**
     * @brief This class represents a feature for the rendergraph
     *
     */
    class IFeature
    {
      public:
        /**
         * @brief Register the feature with the rendergraph.
         *
         */
        virtual void Register(RendergraphBuilder *builder) = 0;

        /**
         * @brief Cleanup on close.
         *
         * @param _device VkDevice for any vulkan calls.
         */
        virtual void Cleanup(VkDevice _device) = 0;
    };
} // namespace rgraph