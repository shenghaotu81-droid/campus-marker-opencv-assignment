#include "pose/tag_config_validation.hpp"
#include "vision/config/pose_config.hpp"
#include <cmath>
namespace mark::pose::detail
{
    bool validateTagConfig(const TagConfig &c)
    {
        for (int id:c.allowed_ids_)
            if (id < 0 || id >= 587)
                return false;

        for (auto [id, side]:c.side_m_)
            if (!c.allowed_ids_.count(id) || !std::isfinite(side) || side <= 0)
                return false;

        return true;
    }

}
