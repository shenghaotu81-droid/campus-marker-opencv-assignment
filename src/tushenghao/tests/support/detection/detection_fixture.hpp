#pragma once
#include "detector/config.hpp"
#include "block3_fixture.hpp"
namespace detection_fixture {
inline mark::AppConfig app() {
    return mark::loadConfig(std::filesystem::path(VISION_SOURCE_ROOT) / "config/detector.yaml");
}
inline bool same(const mark::Detection &a, const mark::Detection &b) {
    return a.corners == b.corners && a.bbox == b.bbox && a.attributes.orientation == b.attributes.orientation &&
        a.confidence == b.confidence && a.category == b.category && a.quality_flags == b.quality_flags &&
        a.attributes.marker_code.has_value() == b.attributes.marker_code.has_value();
}
}
