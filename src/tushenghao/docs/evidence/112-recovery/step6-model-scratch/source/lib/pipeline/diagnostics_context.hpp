#pragma once
#include "core/timing.hpp"
#include "core/prepared_frame.hpp"
#include "pipeline/decode_stage.hpp"
#include "pipeline/temporal_types.hpp"

namespace mark
{
    // 仅工具合成实验填写真值/条件，Detector从不生成或消费它，不把实验真值作为准入证据。
    struct ExperimentalDetails
    {
        uint64_t segment{}, sample{};
        int dt_ms{}, mode{};
        double rate{}, r_px{}, deviation_px{};
        bool known{}, noisy{};
        std::array<cv::Point2d, 4> truth_physical{}, raw_physical{};
    };

    // 原阶段详情散落在各audit；直接复用已生成结果，选中帧才复制，不重算几何或取证。
    struct StageDetails
    {
        PreparedFrame prepared;
        std::vector<WhiteComponent> components;
        std::vector<ShapeObservation> observations;
        GeometryBatch generated, validated, completed;
        DecodeStageResult decoded;
        TemporalDiagnostics temporal;
        std::optional<ExperimentalDetails> experimental;
    };

    struct DiagnosticsRequest
    {
        std::string run_id;
        bool timing_enabled{false}, selected{false};
        ExecutionScope scope{ExecutionScope::Full};
        DiagnosticsLevel level{DiagnosticsLevel::Summary};
        std::string timestamp_recipe{"input"};
    };

    // 每个Detector实例拥有上下文；不使用全局/tls，旧公共消费无prepare仍能安全处理。
    class FrameDiagnosticsContext
    {
      public:
        DiagnosticsRequest request;
        FrameRecord record;
        FrameTiming timing;
        std::shared_ptr<StageDetails> details;
        void begin(const FrameInput &, const DiagnosticsRequest &);
        void event(Stage, ReasonCode, std::string,
                   std::optional<uint64_t> hypothesis = std::nullopt);
        void resets(const ResetSnapshot &, std::optional<uint64_t> source);
        void output(const FrameResult &);
        void finish() noexcept;
    };
}
