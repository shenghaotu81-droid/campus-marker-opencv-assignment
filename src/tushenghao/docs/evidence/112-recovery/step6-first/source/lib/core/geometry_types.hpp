// 从视觉证据推理出几何解释（数据流冻结）
/* 接口讲解：几何推理链——从白块到假设的数据流转（从像素到几何假设的完整链条）Block2核心（六个白块一一对应）
我们在解决什么问题？
图像里有一堆白块，我们要回答：这里有没有 MARK？如果有，哪个白块对应模型的哪个零件（L0、L2、M1...）？
注意：我们不知道答案，得从证据推理出来。这就是跟之前废掉的 matcher 最大的区别——那个假设 ID 已知，直接查表；现在是从零推理。
五个类型就是推理的五个阶段：
step5
1.WhiteComponent——"我发现一个白块"
从图像分割来的原始观测：轮廓、面积、外接框、碰没碰图像边。
好比拿笔把照片里的白色区域圈出来。

2.ShapeObservation——"这个白块长这样"
把圈出来的抖动轮廓简化成多边形，标出每个关键顶点是凸还是凹。
好比把圈出来的形状用直线描一遍，在拐角处标注"这里凸出去、这里凹进去"。
supported_classes 是说：这个形状可能像 L、M、S 中的哪几个，先保留不确定性。

step6(从多个白色几何观测中寻找一个能解释整套 MARK 的二维几何假设。)
3.ComponentAssignment——"这个白块可能是模型的那个零件"
一个猜测："3 号白块可能是模型的 L0"。注意这只是一个对应猜测，不是结论。

4.GeometryHypothesis——"这是一个完整的解释" （对应集合 + 仿射矩阵 + 验证残差 + 证据）
一组对应猜测合起来："1、3、5 号白块分别是 L0、L2、L3，模型到图像的仿射变换是这个矩阵"。
为什么是一组？单个白块说明不了 MARK，得凑齐一套才有意义。
affine_transform 是 2x3 的二维映射（模型坐标→图像坐标），不是相机位姿，Block 2 不碰 3D。

结果传递给block 3
5.GeometryBatch——"这一帧我所有的解释"
一帧可能有多个说得通的假设，都保留，不强行选唯一。外加诊断信息和"算力不够截断了"的标记。

三个关键设计为什么这样定：
1.TurnFeature 不用 vector<int>：[1,-1,1] 这种编码过两天自己都看不懂。改成 {vertex_index: 2, type: CONCAVE}，顶点下标和凸凹语义绑死，自解释。
2.完整性用两态枚举不用分数：Block 2 只分得清"明确不完整"（比如被图像边切掉了）和"待验证"（看着像，等 Block 3 细查）。打 0.85 分这种事留给后面，不在 Block 2 冒充。
3.ComponentAssignment 里没有 error 字段：单个对应不谈误差，误差是整个假设的事（validation_residual_），放 GeometryHypothesis 里。
*/
/* pipeline:数据流（像素 → WhiteComponent（分割）→ ShapeObservation（简化）→ ComponentAssignment（配对猜测）→ GeometryHypothesis（成组+仿射）→ GeometryBatch（收拢））
WhiteComponent
      |
      v
ShapeObservation
      |
      v
ComponentAssignment
      |
      v
GeometryHypothesis
      |
      v
GeometryBatch
*/
#pragma once

#include <cstddef>
#include <optional>
#include <limits>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

namespace mark
{
    // 前面三个是零件
    // 转折类型：显式区分简化多边形顶点是凸转折还是凹转折。
    enum class TurnType
    {
        CONVEX,   // 凸转折：顶点向外凸出，内角 < 180°。
        CONCAVE   // 凹转折：顶点向内凹入，内角 > 180°。
    };

    // 单个多边形转折特征：把顶点位置和凸凹语义绑定，避免使用含义不明的整数编码。
    struct TurnFeature
    {
        // 对应 simplified_polygon_ 中的顶点下标，用于把转折语义绑定到具体顶点。
        std::size_t vertex_index_{0};

        // 当前顶点的转折类型，取值仅为 CONVEX 或 CONCAVE。
        TurnType type_{TurnType::CONVEX};
    };

    // Block 2 早期完整性状态：这里只表达明确不完整或仍待后续验证。
    enum class GeometryCompleteness
    {
        CLEARLY_INCOMPLETE,
        PENDING_VALIDATION
    };

    // 对应数据流流转（见前面的五站：像素->分割 → 简化 → 配对猜测 → 成组+仿射 → 收拢）
    // Step 5 提取的输出
    // 单个白色连通区域：保存图像分割得到的原始白片观测。
    struct WhiteComponent
    {
        // 帧内唯一组件编号，用于后续 assignment 准确引用同一白片。
        std::size_t component_id_{0};

        // 原始轮廓，用于保留未经多边形简化的观测证据。
        std::vector<cv::Point> contour_;

        // 原始轮廓面积，用于后续相对面积和尺度检查。
        double area_{0.0};

        // 白片在工作图中的外接范围，用于空间关系和边界检查。
        cv::Rect bounding_box_;

        // 是否接触工作图边界，用于识别可能被裁切的不完整结构。
        bool touches_border_{false};
    };

    // 一个明确六边L拓扑候选；点和凹点都来自完整工作轮廓，不生成虚拟锚点。
    struct LTopologyCandidate
    {
        std::vector<cv::Point2f> polygon_;
        std::vector<std::size_t> concave_vertex_indices_;
        double simplification_epsilon_{0};
    };

    // Step 5 观测的输出
    // 白片的结构化几何观测：保存简化后的形状证据及其不确定性。
    struct ShapeObservation
    {
        // 保存实际来源ID；未设置仅兼容直接构造旧内部fixture的调用者。
        std::size_t source_component_id_{std::numeric_limits<std::size_t>::max()};

        // 同一组件可有多个实际L拓扑，后续逐候选/逐凹点搜索，不能只挑第一个。
        std::vector<LTopologyCandidate> l_topology_candidates_;
        // 简化后的有序多边形，用于后续 L/M/S 结构分析。
        std::vector<cv::Point2f> simplified_polygon_;

        // 各关键顶点的凸凹转折信息，显式记录顶点下标和转折类型。
        std::vector<TurnFeature> turns_;

        // L/M/S 结构推理使用的候选锚点。
        // 保存 simplified_polygon_ 中凹转折顶点的索引。
        // 没有可靠凹点时保持 nullopt，不强行猜测。
        std::optional<std::size_t> anchor_vertex_index_;

        // 多边形简化相对原始轮廓的误差，用于判断结构观测是否仍可信。
        double simplification_error_{0.0};

        // 当前观测支持的候选类别，字符串只允许 "L"、"M"、"S"。
        std::vector<std::string> supported_classes_;
    };

    // 不是步骤输出，是 GeometryHypothesis 里的一块拼图（一个"白块可能是哪个零件"的猜测）
    // 模型片段与观测白片的对应关系：记录一个模型组件由哪一个帧内白片解释。
    struct ComponentAssignment
    {
        // 模型片段编号，例如 "L0"、"L2"、"L3"、"M1"、"S1a" 或 "S1b"。
        std::string model_part_id_;

        // 对应 WhiteComponent::component_id_，用于引用当前帧中的实际观测白片。
        std::size_t component_id_{0};
    };

    // Step 6 生成的输出
    // 几何假设的早期完整性采用 §4.7 状态，而不是用连续分数冒充最终完整性判断。
    struct GeometryHypothesis
    {
        // 当前假设采用的全部模型片段到观测白片对应，用于保留完整解释关系。
        std::vector<ComponentAssignment> assignments_;

        // 模型坐标到工作图坐标的 2x3、CV_64F 仿射矩阵，方向固定为 model -> working image。
        // 它只表示二维仿射对应，不是相机位姿。
        cv::Mat affine_transform_;

        // 工作图 px：已验证部件中，投影模型顶点到完整观测轮廓平均距离的最大值。
        // Step6 生成器显式写 -1 表示未计算；Step7 完整测量后回填，真实 0 可以成立。
        // 三L父验证后补M/S不扩展此统计的部件范围；不是评分或预测不确定度。
        double validation_residual_{0.0};

        // Block 2 只区分“明确不完整”和“待验证”，最终完整性留给 Block 3。
        GeometryCompleteness completeness_{
            GeometryCompleteness::PENDING_VALIDATION};

        // 记录该假设被保留或拒绝判断所依据的几何证据，便于后续 audit 定位原因。
        std::vector<std::string> evidence_;
    };

    // Step 7 验证后，整个 Block 2 的最终输出（打包合格的）
    // 一帧的几何推理结果：汇总全部合格假设以及搜索过程状态。
    struct GeometryBatch
    {
        // 只有窄范围补全模块验证六片之后才可进入 decode；不能拿三L猜测当完整对应。
        bool segmented_assignments_ready_{false};
        // 保存当前帧全部合格几何假设，因为 Block 2 不强行选择唯一解释。
        std::vector<GeometryHypothesis> hypotheses_;

        // 保存本帧几何阶段的诊断信息，用于定位提取、组合或验证失败原因。
        std::vector<std::string> diagnostics_;

        // 表示搜索是否因资源上限而被截断，因为截断后的结果不能宣称搜索完整。
        bool resource_truncated_{false};
    };

} // namespace mark
