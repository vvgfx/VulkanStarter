#include "RendergraphBuilder.h"

namespace rgraph
{
    /**
     * @brief This class represents a feature for the rendergraph
     *
     */
    class IFeature
    {
        /**
         * @brief Register the feature with the rendergraph.
         *
         */
        virtual void Register(RendergraphBuilder *builder) = 0;
    };
} // namespace rgraph