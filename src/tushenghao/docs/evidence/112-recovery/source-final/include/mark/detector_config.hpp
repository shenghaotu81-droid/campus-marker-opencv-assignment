// Detector 自己需要的配置结构（只包含检测算法配置）14 个 YAML 字段（冻结文档，配置文件语言） → C++ 成员（映射，程序语言）
// Detector 模块的配置结构体定义（定义 外部怎么配置 Detector）（配置怎么传递）（公共接口层组分）
#pragma once

#include <string>
#include <cstddef>
#include <optional>

namespace mark
{
    // C3/C4/C5必须显式fixture或获批配置，缺预算不能沿用三L宽松门限。
    struct AssignmentCompletionConfig
    {
        double max_boundary_distance_work_px;
        double approximation_epsilon_work_px; // 简化上限；保留五比例并增加1/4及至多两额外顶点有界候选，不读真值
        double max_direction_diff_deg;
        double max_relative_area_error;
        double boundary_sample_step_work_px;
        size_t max_candidates;
        size_t max_expansions;
        size_t max_output_branches;
    };
    // 把 YAML 里的配置，映射成 C++ 结构，让 Detector 使用

    // 检测器
    // YAML detector.mode uses "skeleton"; enum limits valid states.
    // YAML detector.mode 使用字符串 "skeleton"，enum 用于限制合法状态空间。
    enum class DetectorMode
    {
        Skeleton
    };          // 现在是骨架模式（啥也不干，返回 NOT_READY）

    // 输入相关配置
    // Keep C++ structure aligned with YAML hierarchy for field-path validation.
    // 保持 C++ 结构与 YAML 层级一致，便于错误字段路径对应。
    struct InputConfig
    {
        std::string pixel_format = "BGR8";        // 图像格式（OpenCV 默认彩色格式）
        std::string timestamp_unit = "us";        //时间戳单位是微秒
    };

    // 预处理参数（原图->resize->thershold->findCountours）
    struct PreprocessConfig
    {
        int work_width = 960;
        int work_height = 720;             // 图像统一缩到这个尺寸再算（快）
        int threshold = 200;               // 二值化阈值，灰度超 200 变白——用来找白色角标
    };

    // 几何观察相关配置。
    // Block2 使用这些参数从白色区域中提取几何信息。
    // 具体阈值由后续合成数据测量确定。// TODO: 以下参数为占位值，待合成数据测量后调优（校内赛前）。
    struct GeometryConfig
    {
        // 白色区域二值化阈值。
        // 为什么需要：
        // extractWhiteComponents 需要把白色 MARK 区域从背景中分离出来。
        // 当前值只是占位，后续通过数据测量调整。
        int white_threshold_ = 200;

        // approxPolyDP 多边形简化精度。（简化误差）
        // 为什么需要：
        // 原始 contour 点数量可能过多，需要压缩成几何顶点。
        // 当前值只是占位。
        double approximation_epsilon_ = 2.0;

        // 白色组件面积过滤下限。
        // 为什么需要：
        // 去除噪声产生的小连通区域。
        // 当前值只是占位。
        double min_area_ = 0.0;

        // 几何假设搜索最大数量。
        // 防止组合爆炸导致资源不可控。
        std::size_t max_hypothesis_count_ = 1000;

        // Block2 Step 7 几何验证最大允许残差。
        // 用于比较模型投影轮廓与实际观测轮廓。
        double max_validation_residual_ = 5.0;

        // Step 7 面积比例验证范围。
        // ratio = 投影面积 / 观测面积。
        double min_area_ratio_ = 0.5;

        double max_area_ratio_ = 2.0;
    };

    /**
     * @brief Block 3 角点恢复配置（Block 3 角点恢复的 6 个可调参数（局部搜索范围、最小点数、拟合误差、最小夹角、交点误差、截断拒绝））
     *
     * 控制：
     * - 局部边搜索；
     * - 直线拟合；
     * - 交点稳定性检查。
     *
     * 不负责：
     * - 屏幕排序；
     * - 语义归并；
     * - 时序稳定。
     */
    // G4 原图边关联预算没有生产默认值；显式fixture可赋值，正式值待报告审批。
    struct CornerObservationBudget
    {
        double max_edge_direction_diff_deg;
        double max_edge_position_distance_px;
        double max_component_mapping_distance_px;
        double turn_trim_distance_px;
        double max_turn_connection_length_px;
        double max_support_extension_px;
        double min_support_span_px;
    };

    struct CornerConfig
    {
        /**
         * @brief 局部搜索区域扩大比例
         *
         * 根据模型对应区域扩大搜索范围。
         *
         * 只用于寻找真实观测边，
         * 不生成预测点。
         */
        double local_search_margin_ratio_{0.2};

        /**
         * @brief 拟合直线所需最少点数
         *
         * 点数不足时，
         * 当前边没有足够观测证据。
         */
        int min_line_points_{5};

        /**
         * @brief 最大直线拟合误差
         *
         * 超过该值说明当前边段不满足直线假设。
         */
        double max_line_fit_error_{2.0};

        /**
         * @brief 两条边允许的最小夹角
         *
         * 防止近平行直线产生不稳定交点。
         */
        double min_intersection_angle_deg_{10.0};

        /**
         * @brief 最大角点交点误差
         *
         * 检查交点是否仍位于实际转折附近。
         */
        double max_corner_error_{5.0};

        /**
         * @brief 是否拒绝截断角
         *
         * true：
         * 图像边界切断结构时，
         * 不输出有效角。
         */
        bool reject_truncated_corner_{true};

        /**
         * @brief Block 3 轮廓近似参数
         *
         * 用于 corner_resolver.cpp 内部 approxPolyDP。
         *
         * 与 Block 2 的 GeometryConfig::approximation_epsilon_
         * 分开维护。
         *
         * Block 2 负责形状观察，
         * Block 3 负责角点边定位，
         * 两者调参目的不同。
         */
        double approximation_epsilon_{2.0};

        /**
         * @brief 点到候选边的距离阈值
         *
         * 用于 find_matching_edge 收集 contour 点。
         * 跟 approximation_epsilon_ 分开：
         * epsilon 控制轮廓简化强度，
         * 这个控制点归属判定。
         */
        double edge_point_distance_threshold_{3.0};

        /**
         * @brief 假设间几何一致性阈值
         *
         * 用于 Step 5 语义归并时，
         * 判断两个 CornerMeasurement 是否描述同一个几何对象。
         *
         * 比较公式：
         *
         * Σ ||pi - qi||²
         *
         * 单位：
         * pixel²
         *
         * 它不是单角误差阈值，
         * 不参与直线拟合和角点检测。
         */
        double semantic_geometry_threshold_{64.0};

        // 缺预算表示阶段未就绪，不沿用旧占位参数偷偷开始正式定位。
        std::optional<CornerObservationBudget> observation_budget_;
    };

    // 时序相关
    struct TemporalConfig
    {
        // YAML 0/1 -> C++ bool: configuration switch becomes semantic state.
        // YAML 0/1 → C++ bool：配置开关直接表达启用状态。
        // 旧接口只有开关，无法描述时间权重；冻结起点兼容schema=1，预算无默认值。
        bool stabilization_enabled = true;          // 是否开启稳定

        bool display_hold_enabled = false;           // 是否允许显示保持状态                     // 开关都是 0（关）

        double reference_dt_ms = 14.0;
        double reference_alpha = 0.7;
        double history_max_gap_ms = 50.0;
        double max_center_distance_diagonal_ratio = 0.5;
        double min_area_ratio = 0.5;
        double max_area_ratio = 2.0;
        std::optional<double> correspondence_uncertainty_px;
        std::optional<double> max_smoothing_deviation_px;
        int max_hold_frames = 5;                     // 最多保持5帧
    };

    // 输出/显示相关
    struct OutputConfig
    {
        bool show_window = false;            //控制cv::imshow()是否打开

        bool show_held_state = false;        //历史保持状态是否显示                          // 都是 0（不弹窗）
    };         // 它属于 App 行为。但这里先放在 DetectorConfig 里，是冻结结构决定的。

    // 调试开关
    struct DebugConfig
    {
        bool timing_enabled = false;         // 打开处理耗时统计

        bool draw_candidates = false;        // 显示亮斑候选，轮廓，角点方便调试           // 都是 0（关，板块5才开）
    };

    // DetectorConfig 总结构
    struct DetectorConfig
    {
        int schema_version = 1;           // 配置文件版本号，以后加字段就升版

        InputConfig input;

        PreprocessConfig preprocess;

        GeometryConfig geometry_;

        /**
         * @brief Block 3 角点恢复配置
         *
         * 控制物理角恢复阶段的搜索和误差判断。
         */
        CornerConfig corner_;   // 从 detector.yaml 的 corner: 节点读值，填进 config.corner_（后面调用）
        std::optional<AssignmentCompletionConfig> assignment_completion_;

        // MARK 几何模型 YAML 路径
        // 用于 Block 2 geometry matcher 加载模型几何描述。
        std::string marker_geometry_path_ = "config/marker_geometry.yaml";

        DetectorMode mode = DetectorMode::Skeleton;

        TemporalConfig temporal;

        OutputConfig output;

        DebugConfig debug;
    };              // 和 YAML 配置层级一致！参数  给定默认值再覆盖（如果 YAML 没写，使用默认值）

} // namespace mark

/*
读取 app.yaml

        ↓

AppConfig

        ↓

DetectorConfig

        ↓

创建 Detector（使用配置进行检测）
*/
