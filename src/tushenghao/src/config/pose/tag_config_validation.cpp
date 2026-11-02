#include "pose/tag_config_validation.hpp"
#include "vision/config/pose_config.hpp"
#include <cmath>
namespace mark::pose
{
namespace detail {
// Q03/P03：纯 Tag 算法参数契约；原生 family 范围与尺寸校验留在各消费者。
bool validateTagConfig(const TagConfig &c) {
    return c.nthreads_ >= 1 && std::isfinite(c.quad_decimate_) && c.quad_decimate_ >= 1 &&
           std::isfinite(c.quad_sigma_) && c.quad_sigma_ >= 0 &&
           std::isfinite(c.decode_sharpening_) && c.decode_sharpening_ >= 0 &&
           c.max_hamming_ >= 0 && c.max_hamming_ <= 2;
}
}

}
