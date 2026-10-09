// 接口契约:给四个物理角发"身份证"+ 证据 + 信封(BLOCK3)
// 角是什么、证据是什么、结果怎么包装
#pragma once

#include <array>
#include <string>
#include <optional>
#include <vector>
#include <limits>
#include <cstdint>

#include <opencv2/core.hpp>

namespace mark
{

    // 单线遗漏扫描行奇偶结构；内部模型类型明确区分估计目标，不改公开 Detection。
    enum class LineFitModel
    {
        Legacy = 0,
        PeriodicLeastSquares = 1,
        PeriodicPhaseMedian = 3,
        PeriodicL1 = 4,
        PeriodicZeroSlope = 5
    };

    // P0~P3 是谁（物理身份，不随屏幕转）
    /**
     * @brief 目标物理角编号
     *
     * P0~P3 是图纸中规定的固定角身份。
     * 它们表示“目标自身哪个角”，不表示屏幕中的左上、右上等位置。
     *
     * 即使目标在图像中旋转：
     * - P0 仍然是模型左上完整 L 的外侧角；
     * - P1 仍然是模型右上 M 分段角；
     * - P2 仍然是模型右下完整 L 的外侧角；
     * - P3 仍然是模型左下完整 L 的外侧角。
     *
     * 后续屏幕排序必须先知道物理身份，再建立屏幕位置关系。
     */
    enum class PhysicalCorner
    {
        P0, // 模型左上完整 L 的外侧上边与外侧左边交汇凸角
        P1, // 模型右上分段 M 的外侧上边与外侧右边交汇凸角
        P2, // 模型右下完整 L 的外侧右边与外侧下边交汇凸角
        P3  // 模型左下完整 L 的外侧下边与外侧左边交汇凸角
    };

    // P0 去 L0 模型的 v0 顶点找，由 e5/e0 两条边组成（只存"去哪查"，不存坐标）
    /**
     * @brief 物理角到图纸模型的绑定关系:记录"这个物理角去模型哪里找"，不存坐标(坐标在 YAML 里，运行时去 MarkerGeometry 查)
     *
     * 这里只记录：
     * “这个物理角应该去模型哪里找”。
     *
     * 不保存实际坐标：
     * - 坐标属于 MarkerGeometry；
     * - YAML 模型修改后，这里不用同步修改；
     * - 运行时通过 model_piece_id_ 和顶点编号查询模型数据。
     *
     * 不负责：
     * - 图像检测；
     * - 当前角点计算；
     * - 屏幕排序；
     * - 方向判断。
     */
    struct CornerBinding
    {
        /**
         * @brief 物理角编号
         *
         * 表示这个绑定对应 P0~P3 中哪一个固定角。
         *
         * 它保存的是目标语义身份，不是屏幕位置。
         */
        PhysicalCorner physical_corner_{static_cast<PhysicalCorner>(-1)};

        /**
         * @brief 对应的模型片段编号
         *
         * 用来找到 MarkerGeometry 中具体哪个几何片段。
         *
         * 例如：
         * - L0
         * - L2
         * - L3
         * - M1
         *
         * 不直接存坐标，避免模型数据出现两份来源。
         */
        std::string model_piece_id_;

        /**
         * @brief 该物理角对应模型顶点编号
         *
         * 编号来自 marker_geometry.yaml 中 vertices 数组下标。
         *
         * 例如：
         * vertices[0] 对应 v0。
         *
         * 这里保存编号，不保存 Point 坐标。
         */
        int vertex_index_{-1};

        /**
         * @brief 构成该凸角的两条模型邻边编号
         *
         * 边编号来自模型顶点顺序：
         * e0 = v0 -> v1，
         * e1 = v1 -> v2，
         * 依次类推，
         * 最后一条边返回 v0。
         *
         * 后续求角时，需要知道是哪两条边形成这个物理角。
         */
        std::array<int, 2> adjacent_edge_indices_{{-1, -1}};
    };

    // 每个角的"破案卷宗"（用了哪两条边、拟合直线、交点、误差、截断没）
    /**
     * @brief 角点证据
     *
     * 保存一个物理角为什么可以被测量出来。
     *
     * 不只保存最后结果点：
     * - 保存用了哪些观测片段；
     * - 保存原图中的边段；
     * - 保存拟合直线；
     * - 保存交点；
     * - 保存误差；
     * - 保存是否截断。
     *
     * 这样失败时可以回查：
     * 是没有找到证据，
     * 还是证据质量不满足要求。
     */
    struct CornerEvidence
    {
        /**
         * @brief 当前证据对应的物理角
         *
         * 表示这份证据属于 P0~P3 哪个固定角。
         *
         * 不表示屏幕中的位置。
         */
        PhysicalCorner physical_corner_{static_cast<PhysicalCorner>(-1)};

        // G5：帧/组件/两条连续弧的来源；未填写时绝不表示已取得原图证据。
        uint64_t frame_id_{0};
        size_t component_id_{std::numeric_limits<size_t>::max()};
        std::string stable_id_;
        std::array<int, 2> model_edge_ids_{{-1, -1}};
        int model_vertex_id_{-1};
        std::array<std::vector<cv::Point2d>, 2> original_support_arcs_;
        std::vector<cv::Point2d> original_turn_arc_;
        bool original_observation_{false};
        // 每线原图px残差/延伸；corner_error为交点到实测连接弧的距离。
        std::array<double, 2> line_mean_residual_px_{
            {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()}};
        std::array<double, 2> line_max_residual_px_{
            {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()}};
        std::array<double, 2> support_extension_px_{
            {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()}};
        double corner_error_px_{std::numeric_limits<double>::infinity()};

        // 原残差字段仍是全部原像素到物理直线的垂距；校正误差必须另存，不能偷换口径。
        // axis=1 对 y 扫描、拟合 x；axis=0 对 x 扫描、拟合 y。c 为正交坐标奇偶位移。
        std::array<LineFitModel, 2> line_model_kinds_{{LineFitModel::Legacy, LineFitModel::Legacy}};
        std::array<int, 2> periodic_axes_{{0, 0}};
        std::array<double, 2> periodic_coefficients_{{0, 0}};
        std::array<double, 2> model_mean_residual_px_{
            {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()}};
        std::array<double, 2> model_max_residual_px_{
            {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()}};

        /**
         * @brief 参与测量的观测片段编号
         *
         * 保存观测来源编号。
         *
         * 不复制整份观测数据，
         * 避免产生第二份状态。
         */
        std::vector<int> observed_segment_ids_;
        std::array<int, 2> observed_segment_end_ids_{{-1, -1}}; // 连续弧终点索引，合并弧仍可追溯

        /**
         * @brief 第一条原图观测边段
         *
         * 保存当前角点使用的真实图像证据。
         *
         * 不是模型预测边。
         */
        std::array<cv::Point2d, 2> edge_segment_a_{};

        /**
         * @brief 第二条原图观测边段
         *
         * 两条边共同确定一个角。
         */
        std::array<cv::Point2d, 2> edge_segment_b_{};

        /**
         * @brief 第一条拟合直线
         *
         * 保存边拟合结果，
         * 用于之后检查角点来源。
         */
        cv::Vec4d line_a_{};

        /**
         * @brief 第二条拟合直线
         */
        cv::Vec4d line_b_{};

        /**
         * @brief 两条观测直线交点
         *
         * 这是当前观测计算出的角点。
         *
         * 不是预测点，
         * 不是历史点，
         * 不是补出来的点。
         */
        cv::Point2d intersection_{std::numeric_limits<double>::infinity(),
                                  std::numeric_limits<double>::infinity()};

        /**
         * @brief 当前测量误差
         *
         * 表示边拟合和交点计算的不确定程度。
         */
        // 两线原图平均垂距之和；未测量使用无穷，避免默认零冒充完美证据。
        double error_{std::numeric_limits<double>::infinity()};

        /**
         * @brief 是否存在结构截断
         *
         * true 表示当前角附近结构可能被图像边界切断。
         *
         * 截断情况下不能假设角完整存在。
         */
        bool truncated_{true};
    };

    // 一次测量的四个角 + 四份证据
    /**
     * @brief 单个几何假设下的四物理角测量
     *
     * 保存：
     * - P0~P3 四个物理角；
     * - 每个角对应的观测证据。
     *
     * 顺序固定：
     *
     * [0] = P0
     * [1] = P1
     * [2] = P2
     * [3] = P3
     *
     * 这里保持物理身份顺序，
     * 不转换成屏幕 LT/RT/RB/LB。
     */
    struct CornerMeasurement
    {
        /**
         * @brief 四个物理角测量坐标
         *
         * 数组位置固定对应 P0~P3。
         */
        std::array<cv::Point2d, 4> physical_corners_{};

        /**
         * @brief 四个物理角对应证据
         *
         * 每个角必须能追溯到自己的观测来源。
         */
        std::array<CornerEvidence, 4> evidence_;
    };

    /**
     * @brief CornerResolution 的结果状态
     *
     * 明确区分：
     * - 成功得到四角测量；
     * - 当前无法得到可信测量。
     *
     * 不使用特殊坐标表示失败。
     */
    enum class CornerResolutionStatus
    {
        SUCCESS,
        FAILED
    };

    // 三个"信封"——成功就装结果，失败就装拒绝原因，不许用 (0,0) 蒙混(1)
    /**
     * @brief resolveObservedCorners 的结果包装
     *
     * 成功：
     * - measurement_ 存在。
     *
     * 失败：
     * - measurement_ 为空；
     * - rejection_reason_ 说明原因。
     */
    struct CornerResolution
    {
        /**
         * @brief 当前解析状态
         */
        CornerResolutionStatus status_{CornerResolutionStatus::FAILED};

        /**
         * @brief 可选的四角测量结果
         *
         * 成功时必须存在。
         *
         * 失败时为空，
         * 不创建假的四点。
         */
        std::optional<CornerMeasurement> measurement_;

        /**
         * @brief 拒绝原因
         *
         * 失败时说明为什么不能输出测量。
         */
        std::string rejection_reason_;
    };

    /**
     * @brief 屏幕排序结果
     *
     * 保存：
     * - 屏幕点序；
     * - 物理角到屏幕位置的映射；
     * - 排序平局信息。
     *
     * 不重新解释 P0~P3 的物理身份。
     */
    struct ScreenOrder
    {
        /**
         * @brief 屏幕点序
         *
         * 保存排序后的四个屏幕点。
         *
         * 这里表示屏幕标签顺序，
         * 不表示物理编号。
         */
        std::array<cv::Point2d, 4>
            screen_points_{}; // [0]=LT, [1]=RT, [2]=RB, [3]=LB（按3.md §4.2固定顺序） 顺序写死

        /**
         * @brief physical_to_screen 映射
         *
         * 保存：
         *
         * physical_to_screen_[0] -> P0 在 screen_points_ 中的位置
         * physical_to_screen_[1] -> P1 在 screen_points_ 中的位置
         * physical_to_screen_[2] -> P2 在 screen_points_ 中的位置
         * physical_to_screen_[3] -> P3 在 screen_points_ 中的位置
         *
         * 排序必须保留该映射，
         * 不能根据屏幕位置重新猜物理身份。
         */
        std::array<int, 4> physical_to_screen_{
            {-1, -1, -1, -1}}; // P0~P3 分别在 screen_points_ 里的位置

        /**
         * @brief 是否存在排序平局
         *
         * true 表示存在满足条件的平局情况。
         *
         * 平局必须记录，
         * 不能隐藏。
         */
        bool screen_order_tie_{false};
    };

    /**
     * @brief ScreenOrder 的结果状态
     *
     * 与 CornerResolution 使用同一种状态表达方式。
     *
     * 明确区分：
     * - 排序成功；
     * - 排序失败。
     */
    enum class ScreenOrderStatus
    {
        SUCCESS,
        FAILED
    };

    // 三个"信封"——成功就装结果，失败就装拒绝原因，不许用 (0,0) 蒙混(2)
    /**
     * @brief orderScreenCorners 的结果包装
     *
     * 成功：
     * - screen_order_ 存在；
     * - 保留 P0~P3 身份映射。
     *
     * 失败：
     * - screen_order_ 为空；
     * - rejection_reason_ 说明原因。
     */
    struct ScreenOrderResult
    {
        /**
         * @brief 当前排序状态
         */
        ScreenOrderStatus status_{ScreenOrderStatus::FAILED};

        /**
         * @brief 可选屏幕排序结果
         *
         * 失败时为空。
         */
        std::optional<ScreenOrder> screen_order_;

        /**
         * @brief 拒绝原因
         *
         * 失败时说明原因。
         */
        std::string rejection_reason_;
    };

    /* Block 3 三层结果包装顺序:
    角点检测结果
        ↓
    屏幕排序结果
        ↓
    语义归并结果
        ↓
    检测校验结果
    */
    /**
     * @brief 语义归并结果
     *
     * 用于处理多个 CornerMeasurement 之间的几何解释关系。
     *
     * 这里回答：
     *
     * - 多个测量是否描述同一个几何对象；
     * - 当前方向是否可以唯一确定；
     * - 最终保留哪些测量假设。
     *
     * 不负责：
     * - 单角检测；
     * - 屏幕排序；
     * - Detection 创建。
     */
    struct SemanticResolution
    {
        /**
         * @brief 几何一致性结果
         *
         * true：
         * 多个 CornerMeasurement 在无标签四点集合和循环对应意义下，
         * 可以解释为同一个几何对象。
         *
         * false：
         * 不同测量之间存在无法解释的几何冲突。
         */
        bool geometry_consistent_{false};

        /**
         * @brief 方向是否唯一
         *
         * true：
         * 当前证据支持唯一方向解释。
         *
         * false：
         * 几何可能有效，但存在方向歧义。
         *
         * 如果搜索过程被截断：
         * search_truncated=true 时，
         * 不能声明方向唯一。
         */
        bool orientation_unique_{false};

        /**
         * @brief 保留的几何假设
         *
         * 保存语义归并后保留的 CornerMeasurement。
         *
         * 不平均多个不同假设的角点：
         * - 不制造新的坐标来源；
         * - 几何一致时选择已有测量中的最佳结果。
         */
        std::vector<CornerMeasurement> retained_measurements_;

        /**
         * @brief 拒绝原因
         *
         * 当无法形成有效语义结果时，
         * 保存具体原因。
         *
         * 成功时保持为空。
         */
        std::string rejection_reason_;
    };

    // 三个"信封"——成功就装结果，失败就装拒绝原因，不许用 (0,0) 蒙混(3)
    /**
     * @brief Detection 几何校验结果
     *
     * 只负责回答：
     * 当前检测几何是否有效。
     *
     * 不保存另一份角点，
     * 不创建 Detection 对象。
     */
    struct DetectionValidation
    {
        /**
         * @brief 是否通过校验
         */
        bool valid_{false};

        /**
         * @brief 拒绝原因
         *
         * 通过时为空。
         *
         * 失败时必须说明原因。
         */
        std::string rejection_reason_;
    };

    struct CornerConfig; // 前向声明，定义在 detector_config.hpp(避免循环引用)

    /**
     * @brief 将物理角点转换成屏幕顺序（Block 3 Step 4）。
     *
     * 输入 physical_corners[0..3] 固定为 P0~P3 物理身份，
     * 输出屏幕顺序 LT/RT/RB/LB 及 physical_to_screen 映射。
     *
     * config 当前无排序参数，保留接口以保证 Block 3 API 一致。
     */
    ScreenOrderResult orderScreenCorners(const std::array<cv::Point2d, 4> &physical_corners,
                                         const CornerConfig &config);

} // namespace mark
