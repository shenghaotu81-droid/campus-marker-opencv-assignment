// 配置解析/标定加载/模型转换
#include "mark/pose_config.hpp"

#include <cmath>
#include <stdexcept>
#include <vector>

namespace mark::pose
{
namespace
{
// 过去缺字段易被 OpenCV 转成 0；新配置逐项检查类型，空字符串只用于明确未知值。
cv::FileNode required(const cv::FileNode &node, const char *key)
{
    auto value = node[key];
    if (value.empty())
        throw std::runtime_error(std::string("G2 配置缺字段: ") + key);
    return value;
}

std::string text(const cv::FileNode &node, const char *key)
{
    auto value = required(node, key);
    if (!value.isString())
        throw std::runtime_error(std::string("G2 应为字符串: ") + key);
    return static_cast<std::string>(value);
}

double number(const cv::FileNode &value)
{
    if (!value.isInt() && !value.isReal())
        throw std::runtime_error("G2 应为有限数值");
    const double result = static_cast<double>(value);
    if (!std::isfinite(result))
        throw std::runtime_error("G2 数值非有限");
    return result;
}

int integer(const cv::FileNode &node, const char *key)
{
    auto value = required(node, key);
    if (!value.isInt())
        throw std::runtime_error(std::string("G2 应为整数: ") + key);
    return static_cast<int>(value);
}

bool boolean(const cv::FileNode &node, const char *key)
{
    int value = integer(node, key);
    if (value != 0 && value != 1)
        throw std::runtime_error(std::string("G2 开关必须是 0/1: ") + key);
    return value == 1;
}

// 空字符串→nullopt，就是"未标定"
std::optional<double> optional_number(const cv::FileNode &node, const char *key)
{
    auto value = required(node, key);
    if (value.isString() && static_cast<std::string>(value).empty())
        return std::nullopt;
    return number(value);
}

// 原模型单位未记录；只能显式声明 mm 或 m，不能按目标大小猜单位。
// mm→m 转换，按 L0.v0/M1.v1/L2.v0/L3.v0 取点
std::array<cv::Point3d, 4> load_points(const std::filesystem::path &path,
                                      const std::string &unit)
{
    if (unit != "mm" && unit != "m")
        throw std::runtime_error("G2 模型单位只接受 mm/m");
    cv::FileStorage fs(path.string(), cv::FileStorage::READ);
    if (!fs.isOpened() || !fs["polygons"].isSeq())
        throw std::runtime_error("G2 模型打不开或 polygons 非序列: " + path.string());
    const std::array<std::string, 4> ids{{"L0", "M1", "L2", "L3"}};
    const std::array<int, 4> vertex{{0, 1, 0, 0}};
    std::array<cv::Point3d, 4> out{};
    std::array<bool, 4> found{};
    const double scale = unit == "mm" ? 0.001 : 1.0;
    for (const auto &poly : fs["polygons"])
    {
        const auto id_ = text(poly, "id");
        for (std::size_t p = 0; p < ids.size(); ++p)
        {
            if (id_ != ids[p])
                continue;
            const auto vertices = required(poly, "vertices");
            if (found[p] || !vertices.isSeq() ||
                vertices.size() <= static_cast<std::size_t>(vertex[p]))
                throw std::runtime_error("G2 模型角部重复或缺顶点: " + id_);
            const auto v = vertices[vertex[p]];
            if (!v.isSeq() || v.size() != 2)
                throw std::runtime_error("G2 模型顶点必须为二维");
            out[p] = {number(v[0]) * scale, number(v[1]) * scale, 0};
            found[p] = true;
        }
    }
    for (bool value : found)
        if (!value)
            throw std::runtime_error("G2 模型缺 P0/P1/P2/P3 对应结构角");
    return out;
}

void positive(const std::optional<double> &value, const char *name)
{
    if (value && (!std::isfinite(*value) || *value <= 0))
        throw std::runtime_error(std::string("G2 必须为有限正数: ") + name);
}
} // namespace

// 加载只读参考标定、图纸模型和新配置；不写回旧 detector/calibration YAML。
// 逐段读 yaml
G2Config load_pose_config(const std::filesystem::path &path)
{
    cv::FileStorage fs(path.string(), cv::FileStorage::READ);
    if (!fs.isOpened() || integer(fs.root(), "schema_version") != 1)
        throw std::runtime_error("G2 配置打不开或版本不支持: " + path.string());
    auto camera_ = required(fs.root(), "camera");
    auto geometry = required(fs.root(), "geometry");
    auto budgets_ = required(fs.root(), "budgets");
    auto selection_ = required(fs.root(), "selection");
    auto tag_ = required(fs.root(), "tag");
    G2Config c;
    const auto base = path.parent_path();
    auto relative_resource = [&base](const std::string &name)
    {
        const std::filesystem::path resource(name);
        if (resource.empty() || resource.is_absolute())
            throw std::runtime_error("G2 配置资源必须为相对配置目录的路径");
        return (base / resource).lexically_normal();
    };
    cv::FileStorage calibration(relative_resource(text(camera_, "calibration")).string(),
                                cv::FileStorage::READ);
    if (!calibration.isOpened() || integer(calibration.root(), "schema_version") != 1)
        throw std::runtime_error("G2 标定文件打不开或版本不支持");
    required(calibration.root(), "camera_matrix") >> c.camera_.K_;
    required(calibration.root(), "distortion_coefficients") >> c.camera_.dist_;
    c.camera_.image_size_ = {integer(calibration.root(), "image_width"),
                           integer(calibration.root(), "image_height")};
    c.camera_.camera_id_ = text(camera_, "camera_id");
    c.camera_.hardware_verified_ = boolean(camera_, "hardware_verified");
    c.mark_points_m_ = load_points(relative_resource(text(geometry, "model")),
                                 text(geometry, "source_unit"));
    c.mark_target_id_ = text(geometry, "target_id");
    c.mark_geometry_verified_ = boolean(geometry, "hardware_verified");
    c.budgets_.max_rmse_px_ = optional_number(budgets_, "max_rmse_px");
    c.budgets_.max_corner_error_px_ = optional_number(budgets_, "max_corner_error_px");
    c.budgets_.min_quad_area_px2_ = optional_number(budgets_, "min_quad_area_px2");
    c.budgets_.max_time_uncertainty_s_ = optional_number(budgets_, "max_time_uncertainty_s");
    c.budgets_.hardware_verified_ = boolean(budgets_, "hardware_verified");
    (void)text(budgets_, "evidence");
    c.selection_.min_z_m_ = optional_number(selection_, "min_z_m");
    c.selection_.max_z_m_ = optional_number(selection_, "max_z_m");
    c.selection_.min_normal_z_ = optional_number(selection_, "min_normal_z");
    c.selection_.max_normal_z_ = optional_number(selection_, "max_normal_z");
    c.selection_.constraints_verified_ = boolean(selection_, "constraints_verified");
    c.selection_.refine_lm_ = boolean(selection_, "refine_lm");
    c.selection_.prior_enabled_ = boolean(selection_, "prior_enabled");
    c.selection_.prior_max_age_s_ = optional_number(selection_, "prior_max_age_s");
    c.selection_.prior_max_translation_m_ = optional_number(selection_, "prior_max_translation_m");
    c.selection_.prior_max_rotation_rad_ = optional_number(selection_, "prior_max_rotation_rad");
    if (text(tag_, "family") != "tag36h11")
        throw std::runtime_error("G2 只允许 tag36h11");
    c.tag_.nthreads_ = integer(tag_, "nthreads");
    c.tag_.quad_decimate_ = number(required(tag_, "quad_decimate"));
    c.tag_.quad_sigma_ = number(required(tag_, "quad_sigma"));
    c.tag_.refine_edges_ = boolean(tag_, "refine_edges");
    c.tag_.decode_sharpening_ = number(required(tag_, "decode_sharpening"));
    c.tag_.max_hamming_ = integer(tag_, "max_hamming");
    c.tag_.min_decision_margin_ = optional_number(tag_, "min_decision_margin");
    c.tag_.hardware_verified_ = boolean(tag_, "hardware_verified");
    auto ids = required(tag_, "allowed_ids");
    auto sizes = required(tag_, "sizes");
    if (!ids.isSeq() || !sizes.isSeq())
        throw std::runtime_error("G2 allowed_ids/sizes 必须为序列");
    for (const auto &id_ : ids)
    {
        if (!id_.isInt() || !c.tag_.allowed_ids_.insert(static_cast<int>(id_)).second)
            throw std::runtime_error("G2 Tag ID 类型错误或重复");
    }
    for (const auto &size : sizes)
        if (!c.tag_.side_m_.emplace(integer(size, "id"), number(required(size, "side_m"))).second)
            throw std::runtime_error("G2 Tag 尺寸 ID 重复");
    validate_pose_config(c);
    return c;
}

// 配置即使来自测试构造也必须验证，避免绕开 YAML 检查污染解算输入。
// 校验：K 3x3、dist 维度、模型四点是平行四边形
void validate_pose_config(const G2Config &c)
{
    const auto &k = c.camera_.K_;
    const auto &d = c.camera_.dist_;
    const auto count = d.total();
    if (k.rows != 3 || k.cols != 3 || k.type() != CV_64F || !cv::checkRange(k) ||
        k.at<double>(0, 0) <= 0 || k.at<double>(1, 1) <= 0 ||
        k.at<double>(0, 1) != 0 || k.at<double>(1, 0) != 0 ||
        k.at<double>(2, 0) != 0 || k.at<double>(2, 1) != 0 || k.at<double>(2, 2) != 1 ||
        d.type() != CV_64F || (d.rows != 1 && d.cols != 1) || !cv::checkRange(d) ||
        (count != 4 && count != 5 && count != 8 && count != 12 && count != 14) ||
        c.camera_.image_size_.width <= 0 || c.camera_.image_size_.height <= 0 ||
        c.camera_.camera_id_.empty() || c.mark_target_id_.empty())
        throw std::runtime_error("G2 相机矩阵/畸变/尺寸/身份无效");
    for (const auto &p : c.mark_points_m_)
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.z != 0)
            throw std::runtime_error("G2 模型必须为有限 z=0 平面点");
    const auto &p = c.mark_points_m_;
    const auto e1 = p[1] - p[0], e2 = p[3] - p[0];
    if (cv::norm(e1.cross(e2)) <= 0 || cv::norm(p[2] - p[1] - p[3] + p[0]) > 1e-12)
        throw std::runtime_error("G2 模型不是非退化 P0-P1-P2-P3 平行四边形");
    positive(c.budgets_.max_rmse_px_, "max_rmse_px");
    positive(c.budgets_.max_corner_error_px_, "max_corner_error_px");
    positive(c.budgets_.min_quad_area_px2_, "min_quad_area_px2");
    positive(c.budgets_.max_time_uncertainty_s_, "max_time_uncertainty_s");
    positive(c.selection_.min_z_m_, "min_z_m");
    positive(c.selection_.max_z_m_, "max_z_m");
    positive(c.selection_.prior_max_age_s_, "prior_max_age_s");
    positive(c.selection_.prior_max_translation_m_, "prior_max_translation_m");
    positive(c.selection_.prior_max_rotation_rad_, "prior_max_rotation_rad");
    const auto &s = c.selection_;
    if (s.min_z_m_.has_value() != s.max_z_m_.has_value() ||
        (s.min_z_m_ && *s.min_z_m_ >= *s.max_z_m_) ||
        s.min_normal_z_.has_value() != s.max_normal_z_.has_value() ||
        (s.min_normal_z_ && (!std::isfinite(*s.min_normal_z_) ||
                           !std::isfinite(*s.max_normal_z_) || *s.min_normal_z_ < -1 ||
                           *s.max_normal_z_ > 1 || *s.min_normal_z_ >= *s.max_normal_z_)) ||
        (s.prior_enabled_ && (!s.prior_max_age_s_ || !s.prior_max_translation_m_ ||
                             !s.prior_max_rotation_rad_)))
        throw std::runtime_error("G2 安装/历史约束缺字段或区间无效");
    if (c.tag_.nthreads_ < 1 || !std::isfinite(c.tag_.quad_decimate_) ||
        c.tag_.quad_decimate_ < 1 || !std::isfinite(c.tag_.quad_sigma_) || c.tag_.quad_sigma_ < 0 ||
        !std::isfinite(c.tag_.decode_sharpening_) || c.tag_.decode_sharpening_ < 0 ||
        c.tag_.max_hamming_ < 0 || c.tag_.max_hamming_ > 2)
        throw std::runtime_error("G2 Tag 算法配置不合法");
    positive(c.tag_.min_decision_margin_, "min_decision_margin");
    for (int id_ : c.tag_.allowed_ids_)
        if (id_ < 0 || id_ >= 587)
            throw std::runtime_error("G2 tag36h11 ID 越界");
    for (const auto &[id_, side] : c.tag_.side_m_)
        if (!c.tag_.allowed_ids_.count(id_) || !std::isfinite(side) || side <= 0)
            throw std::runtime_error("G2 Tag 尺寸必须为白名单 ID 的有限正米值");
}
} // namespace mark::pose