#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <opencv2/core.hpp>
namespace mark {
// 两个估计器共用支持提取、求交和证据输出；L2为主，周期相位中位数为回退。
enum class LineFitModel { L2=0,PeriodicPhaseMedian=1 };
// 图纸身份，随目标旋转；P1为短M，其余为长L。
enum class PhysicalCorner { P0,P1,P2,P3 };
struct CornerEvidence {
    PhysicalCorner physical_corner_{static_cast<PhysicalCorner>(-1)};
    uint64_t frame_id_{0};
    size_t component_id_{std::numeric_limits<size_t>::max()}; // 当前原图轮廓索引。
    std::array<std::vector<cv::Point2d>,2> original_support_arcs_;
    std::vector<cv::Point2d> original_turn_arc_;
    bool original_observation_{false};
    // 原像素到物理直线的垂距，绝不被周期校正残差替换，单位均为原图px。
    std::array<double,2> line_mean_residual_px_{{INFINITY,INFINITY}};
    std::array<double,2> line_max_residual_px_{{INFINITY,INFINITY}};
    std::array<double,2> support_extension_px_{{INFINITY,INFINITY}};
    double corner_error_px_{INFINITY}; // 交点到实际连接弧的距离。
    std::array<LineFitModel,2> line_model_kinds_{{LineFitModel::L2,LineFitModel::L2}};
    // axis=1沿y扫描拟合x，axis=0沿x扫描拟合y；coefficient为正交坐标奇偶位移px。
    std::array<int,2> periodic_axes_{{0,0}};
    std::array<double,2> periodic_coefficients_{{0,0}};
    std::array<double,2> model_mean_residual_px_{{INFINITY,INFINITY}};
    std::array<double,2> model_max_residual_px_{{INFINITY,INFINITY}};
    std::vector<int> observed_segment_ids_;
    std::array<int,2> observed_segment_end_ids_{{-1,-1}}; // 完整连续弧的原始起止索引。
    std::array<cv::Point2d,2> edge_segment_a_{},edge_segment_b_{};
    cv::Vec4d line_a_{},line_b_{};
    cv::Point2d intersection_{INFINITY,INFINITY}; // 两实测直线交点，不是粗框角。
    double error_{INFINITY}; // 两边原始平均垂距之和；排序分数不冒充测量误差。
    bool truncated_{true};
};
struct CornerMeasurement {
    std::array<cv::Point2d,4> physical_corners_{}; // double，固定P0…P3。
    std::array<CornerEvidence,4> evidence_;
};
enum class CornerResolutionStatus { SUCCESS,FAILED };
struct CornerResolution {
    CornerResolutionStatus status_{CornerResolutionStatus::FAILED};
    std::optional<CornerMeasurement> measurement_;
    std::string rejection_reason_;
};
struct ScreenOrder {
    std::array<cv::Point2d,4> screen_points_{}; // LT/RT/RB/LB。
    std::array<int,4> physical_to_screen_{{-1,-1,-1,-1}};
    bool screen_order_tie_{false};
};
enum class ScreenOrderStatus { SUCCESS,FAILED };
struct ScreenOrderResult {
    ScreenOrderStatus status_{ScreenOrderStatus::FAILED};
    std::optional<ScreenOrder> screen_order_;
    std::string rejection_reason_;
};
struct DetectionValidation { bool valid_{false};std::string rejection_reason_; };
struct CornerConfig;
ScreenOrderResult orderScreenCorners(const std::array<cv::Point2d,4>&,const CornerConfig&);
inline CornerResolution cornerFailure(std::string reason){return {CornerResolutionStatus::FAILED,std::nullopt,std::move(reason)};}
inline CornerResolution cornerSuccess(CornerMeasurement value){return {CornerResolutionStatus::SUCCESS,std::move(value),{}};}
inline ScreenOrderResult screenFailure(std::string reason){return {ScreenOrderStatus::FAILED,std::nullopt,std::move(reason)};}
inline ScreenOrderResult screenSuccess(ScreenOrder value){return {ScreenOrderStatus::SUCCESS,std::move(value),{}};}
}
