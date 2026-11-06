#include "corners/measurement/corner_resolver.hpp"
#include "corners/observation/corner_observation.hpp"
#include "corners/fitting/corner_edge_fit.hpp"
#include <set>
namespace mark
{
    CornerResolution resolveObservedCorners(const CornerPoints &physical, OriginalContourIndex &index)
    {
        CornerPoints measured;
        std::set<size_t> used;
        for (size_t i = 0; i < 4; ++i)
        {
            auto source = index.atCorner(physical[i]);
            if (!source.contour)
                return {{}, source.reason};
            if (!used.insert(source.component_id).second)
                return {{}, "REUSED_CORNER_COMPONENT"};
            std::string reason;
            auto corner = fitObservedCorner(*source.contour, physical[i], {physical[(i + 3) % 4], physical[(i + 1) % 4]}, reason);
            if (!corner)
                return {{}, reason};
            measured[i] = *corner;
        }
        return {measured, {}};
    }

}
