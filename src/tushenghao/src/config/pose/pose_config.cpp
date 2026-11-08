#include "vision/config/pose_config.hpp"
#include "reader/value_reader.hpp"
#include "pose/tag_config_validation.hpp"
#include "vision/types/marker_geometry.hpp"
#include <stdexcept>

namespace mark::pose
{
    namespace
    {
        void bad(config_value::Error)
        {
            throw std::runtime_error("POSE_CONFIG_MISSING_OR_INVALID");
        }

        cv::FileNode required(const cv::FileNode &n, const char *k)
        {
            return config_value::required(n, k, bad);
        }

        std::string text(const cv::FileNode &n, const char *k)
        {
            return config_value::text(required(n, k), bad);
        }

        int integer(const cv::FileNode &n, const char *k)
        {
            return config_value::integer(required(n, k), bad);
        }

        double number(const cv::FileNode &n)
        {
            return config_value::number(n, config_value::NumericPolicy::NodeNumber, bad);
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
            const auto &bindings = model_corner_contract;
            std::array<cv::Point3d, 4> out{};
            std::array<bool, 4> found{};
            const double scale = unit == "mm" ? 0.001 : 1.0;
            for (const auto &poly : fs["polygons"])
            {
                const auto id_ = text(poly, "id");
                for (std::size_t p = 0; p < bindings.size(); ++p)
                {
                    if (id_ != bindings[p].part)
                        continue;
                    const auto vertices = required(poly, "vertices");
                    if (found[p] || !vertices.isSeq() ||
                        vertices.size() <= static_cast<std::size_t>(bindings[p].vertex))
                        throw std::runtime_error("G2 模型角部重复或缺顶点: " + id_);
                    const auto v = vertices[bindings[p].vertex];
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

    }

    G2Config load_pose_config(const std::filesystem::path &path)
    {
        cv::FileStorage fs(path.string(), cv::FileStorage::READ);
        if (!fs.isOpened() || integer(fs.root(), "schema_version") != 2)
            throw std::runtime_error("POSE_CONFIG_SCHEMA");
        for (const auto &n : fs.root())
            if (n.name() != "schema_version" && n.name() != "camera" && n.name() != "geometry" &&
                n.name() != "tag")
                throw std::runtime_error("POSE_CONFIG_UNKNOWN_KEY:" + n.name());

        const auto base = path.parent_path();
        G2Config c;
        auto camera = required(fs.root(), "camera"), geometry = required(fs.root(), "geometry"),
             tag = required(fs.root(), "tag");
        cv::FileStorage cal((base / text(camera, "calibration")).string(), cv::FileStorage::READ);
        if (!cal.isOpened() || integer(cal.root(), "schema_version") != 1)
            throw std::runtime_error("POSE_CALIBRATION_SCHEMA");
        required(cal.root(), "camera_matrix") >> c.camera_.K_;
        required(cal.root(), "distortion_coefficients") >> c.camera_.dist_;
        c.camera_.image_size_ = {integer(cal.root(), "image_width"),
                                 integer(cal.root(), "image_height")};
        c.camera_.camera_id_ = text(camera, "camera_id");
        c.mark_points_m_ =
            load_points(base / text(geometry, "model"), text(geometry, "source_unit"));
        c.mark_target_id_ = text(geometry, "target_id");
        auto ids = required(tag, "allowed_ids"), sizes = required(tag, "sizes");
        if (!ids.isSeq() || !sizes.isSeq())
            throw std::runtime_error("TAG_CONFIG_SEQUENCE");
        for (const auto &id : ids)
            if (!id.isInt() || !c.tag_.allowed_ids_.insert(int(id)).second)
                throw std::runtime_error("TAG_CONFIG_ID");

        for (const auto &s : sizes)
            if (!c.tag_.side_m_.emplace(integer(s, "id"), number(required(s, "side_m"))).second)
                throw std::runtime_error("TAG_CONFIG_SIZE_DUPLICATE");

        validate_pose_config(c);
        return c;
    }

    void validate_pose_config(const G2Config &c)
    {
        const auto &k = c.camera_.K_, &d = c.camera_.dist_;
        auto n = d.total();
        if (k.rows != 3 || k.cols != 3 || k.type() != CV_64F || !cv::checkRange(k) ||
            k.at<double>(0, 0) <= 0 || k.at<double>(1, 1) <= 0 || k.at<double>(0, 1) != 0 ||
            k.at<double>(1, 0) != 0 || k.at<double>(2, 0) != 0 || k.at<double>(2, 1) != 0 ||
            k.at<double>(2, 2) != 1 || d.type() != CV_64F || (d.rows != 1 && d.cols != 1) ||
            !cv::checkRange(d) || (n != 4 && n != 5 && n != 8 && n != 12 && n != 14) ||
            c.camera_.image_size_.width <= 0 || c.camera_.image_size_.height <= 0 ||
            c.camera_.camera_id_.empty() || c.mark_target_id_.empty())
            throw std::runtime_error("POSE_CAMERA_INVALID");
        for (auto p : c.mark_points_m_)
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.z != 0)
                throw std::runtime_error("POSE_MODEL_INVALID");

        const auto &p = c.mark_points_m_;
        if (cv::norm((p[1] - p[0]).cross(p[3] - p[0])) == 0 ||
            cv::norm(p[2] - p[1] - p[3] + p[0]) > 1e-12)
            throw std::runtime_error("POSE_MODEL_DEGENERATE");
        if (!detail::validateTagConfig(c.tag_))
            throw std::runtime_error("TAG_CONFIG_INVALID");
    }

}
