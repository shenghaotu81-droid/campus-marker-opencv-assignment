// A06：各步骤只读快照；只依赖值契约，不包含检测编排或拟合器实现。
#pragma once
#include "vision/types/detection/prepared_image.hpp"
#include "vision/types/detection/decode_result.hpp"
#include "vision/types/detection/temporal_types.hpp"
namespace mark {
    // 原阶段详情散落在各audit；直接复用已生成结果，选中帧才复制，不重算几何或取证。
    struct StageDetails
    {
        PreparedFrame prepared;
        std::vector<WhiteComponent> components;
        std::vector<ShapeObservation> observations;
        GeometryBatch generated, validated, completed;
        DecodeStageResult decoded;
        TemporalDiagnostics temporal;
        std::optional<std::string> experimental; // 工具编码的实验 JSON；算法与渲染均不消费。
    };

}
