#include "pipeline/decode/decode_stage.hpp"
#include "preprocess/preprocess.hpp"
#include "geometry/observation/geometry_observation.hpp"
#include "corners/measurement/corner_resolver.hpp"
#include "corners/observation/corner_observation.hpp"
#include "corners/publication/detection_publication.hpp"
namespace mark
{
    DecodeStageResult decodeStage(const PreparedFrame &frame, const std::vector<MatchQuad> &matches, const CornerConfig &config)
    {
        DecodeStageResult result;
        OriginalContourIndex index(frame);
        for (const auto &match:matches)
        {
            std::string reason;
            auto screen = orderScreenCorners(match, reason);
            if (!screen)
            {
                result.diagnostics.push_back(reason);
                continue;
            }
            CornerPoints physical;
            for (size_t p = 0; p < 4; ++p)
                physical[p] = (*screen)[config.physical_to_screen_[p]];
            auto measured = resolveObservedCorners(physical, index);
            if (!measured.corners)
            {
                result.diagnostics.push_back(measured.reason);
                continue;
            }
            auto d = publishFloatDetection(*measured.corners, frame.original_image_.size(), config, reason);
            if (d)
                result.detections.push_back(*d);
            else
                result.diagnostics.push_back(reason);
        }
        if (!result.detections.empty())
            result.status = Status::DETECTED;
        return result;
    }

    DecodeStageResult runDecodePipeline(const FrameInput &input, const DetectorConfig &config)
    {
        auto frame = preprocess(input, config.preprocess);
        auto components = extractWhiteComponents(frame, config.geometry_);
        return decodeStage(frame, matchQuads(frame, components, config.geometry_), config.corner_);
    }

}
