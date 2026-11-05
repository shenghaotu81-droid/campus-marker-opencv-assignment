// 检测配置；离线输出由 AppConfig 持有，G2/G4各自拥有自己的配置。
#pragma once
#include <string>
#include <array>
#include <optional>
namespace mark {
enum class DetectorMode { SegmentedMark };
struct InputConfig {
    std::string pixel_format="BGR8";
    std::string timestamp_unit="us";
};
struct PreprocessConfig {
    int work_width=960,work_height=720;
    int threshold=200; // 当前原图白片分割阈值；工作图另读 geometry.white_threshold。
};
struct GeometryConfig {
    int white_threshold_=200;
    double approximation_epsilon_=2.; // 工作图轮廓简化尺度，非拟合质量门。
    double min_area_=0.;
};
struct CornerObservationBudget {
    double max_edge_direction_diff_deg;
    double max_edge_position_distance_px;
    double turn_trim_distance_px;
    double max_turn_connection_length_px;
    double max_support_extension_px; // 质量标签阈值，不拒绝交点。
    double min_support_span_px;
};
struct CornerConfig {
    // 安装契约：P0/P1/P2/P3 -> 屏幕LT/RT/RB/LB槽位；不逐帧推断身份。
    std::array<int,4> physical_to_screen_{{0,1,2,3}};
    int min_line_points_{5};
    double max_line_fit_error_{2.}; // 残差质量标签阈值。
    double min_intersection_angle_deg_{10.}; // 数值求交的原有病态角限制。
    double max_corner_error_{5.}; // 交点到实测转折弧的距离标签阈值。
    double approximation_epsilon_{2.}; // 原图支持候选，与工作图观察独立。
    // 原 semantic_geometry_threshold 的拟合内部用途；数值保持64 px²。
    // 同一角的两个可行边对若产生冲突交点，仍不能强选唯一角。
    double max_pair_disagreement_px2_{64.};
    std::optional<CornerObservationBudget> observation_budget_;
};
struct DebugConfig { bool timing_enabled=false; };
struct DetectorConfig {
    int schema_version=1;
    InputConfig input;
    PreprocessConfig preprocess;
    GeometryConfig geometry_;
    CornerConfig corner_;
    DetectorMode mode=DetectorMode::SegmentedMark;
    DebugConfig debug;
};
}
