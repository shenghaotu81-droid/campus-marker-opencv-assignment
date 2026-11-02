// 独立adapter：复用sg连续变换、8x8覆盖渲染和hash，不改旧960x720 CLI/verify。
// 输入用户批准YAML模型和固定C/H profile；输出原图/名义真值/manifest，不运行Detector。
#include "vision/synth_ref/core.hpp"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <random>
#include <iostream>
#include <cmath>

namespace
{
    // 以YAML为权威模型；逐顶点读入，不使用旧未批准的JSON模型审批标记。
    std::vector<sg::Piece> model(const std::string &path)
    {
        cv::FileStorage fs(path, cv::FileStorage::READ);
        if (!fs.isOpened())
            throw std::runtime_error("cannot read approved YAML");
        std::vector<sg::Piece> pieces;
        int instance = 1;
        for (const auto &node : fs["polygons"])
        {
            sg::Piece p;
            p.id = std::string(node["id"]);
            p.instance = instance++;
            for (const auto &v : node["vertices"])
                p.source.emplace_back(double(v[0]), double(v[1]));
            if (!sg::valid_polygon(p.source) ||
                std::abs(sg::signed_area(p.source)) != double(node["area"]))
                throw std::runtime_error("model area mismatch " + p.id);
            pieces.push_back(p);
        }
        if (pieces.size() != 6)
            throw std::runtime_error("model must contain six parts");
        return pieces;
    }

    // 相切圆弧离散：每个直角16等角段，2px半径在旋转前构造；真值保留名义交点。
    // 凹角用同一相切规则，不能用blur/开运算冒充物理圆角。
    sg::Polygon rounded(const sg::Polygon &source, double radius, int subdivisions = 16)
    {
        if (radius == 0)
            return source;
        sg::Polygon out;
        for (size_t i = 0; i < source.size(); ++i)
        {
            auto p = source[i], a = source[(i + source.size() - 1) % source.size()] - p,
                 b = source[(i + 1) % source.size()] - p;
            double al = cv::norm(a), bl = cv::norm(b);
            a /= al;
            b /= bl;
            double theta = std::acos(std::clamp(a.dot(b), -1.0, 1.0)),
                   tangent = radius / std::tan(theta / 2);
            if (tangent * 2 >= std::min(al, bl))
                throw std::runtime_error("rounding tangents overlap");
            auto center = p + (a + b) * (radius / (std::sin(theta / 2) * cv::norm(a + b)));
            auto start = p + a * tangent - center, end = p + b * tangent - center;
            double angle = std::atan2(start.y, start.x),
                   sweep = std::remainder(std::atan2(end.y, end.x) - angle, 2 * CV_PI);
            for (int k = 0; k <= subdivisions; ++k)
            {
                double v = angle + sweep * k / subdivisions;
                out.push_back(center + sg::Point(std::cos(v), std::sin(v)) * radius);
            }
        }
        if (!sg::valid_polygon(out))
            throw std::runtime_error("invalid rounded polygon");
        return out;
    }

    // 显式本次中心(非旧480/360中心)，调用共享变换工具保留连续坐标。
    sg::Scene scene(const std::vector<sg::Piece> &pieces, double angle, double radius,
                    sg::Point center, int subdivisions = 16)
    {
        double r = angle * CV_PI / 180, c = std::cos(r), s = std::sin(r);
        cv::Matx33d shift(1, 0, center.x, 0, 1, center.y, 0, 0, 1),
            rotate(c, -s, 0, s, c, 0, 0, 0, 1), local(2, 0, -80, 0, 2, -80, 0, 0, 1);
        sg::Scene out;
        for (auto piece : pieces)
        {
            // 圆角半径为原图px，在2px/u的本地画布构造后再旋转/平移。
            piece.ideal = sg::transform_polygon(shift * rotate * local, piece.source);
            auto local_polygon = sg::transform_polygon(local, piece.source);
            piece.perturbed =
                sg::transform_polygon(shift * rotate, rounded(local_polygon, radius, subdivisions));
            out.pieces.push_back(piece);
        }
        return out;
    }

    // 固定mt19937+Box-Muller v1，灰度σ=1，先加噪声再lround/截断，最后复制至BGR。
    cv::Mat input(sg::Raster raster, int seed)
    {
        cv::Mat gray = raster.image.clone();
        if (seed)
        {
            std::mt19937 rng(static_cast<uint32_t>(seed));
            for (int y = 0; y < gray.rows; ++y)
                for (int x = 0; x < gray.cols; ++x)
                {
                    double u = (double(rng()) + 0.5) / 4294967296.0,
                           v = (double(rng()) + 0.5) / 4294967296.0;
                    double noise = std::sqrt(-2 * std::log(u)) * std::cos(2 * CV_PI * v);
                    gray.at<uint8_t>(y, x) = static_cast<uint8_t>(
                        std::clamp<long>(std::lround(gray.at<uint8_t>(y, x) + noise), 0, 255));
                }
        }
        cv::Mat bgr;
        cv::cvtColor(gray, bgr, cv::COLOR_GRAY2BGR);
        return bgr;
    }

    // 真值仅写工具文件；正式pipeline不知道它的存在，不用圆弧上的点替名义角。
    sg::Json descriptor(const std::string &group, int radius, int angle, int seed)
    {
        return {{"profile", group},
                {"radius_original_px", radius},
                {"rotation_deg", angle},
                {"noise_sigma", seed ? 1 : 0},
                {"seed", seed},
                {"original_size", {1440, 1080}},
                {"work_sizes", {{480, 360}, {960, 720}, {1440, 1080}}},
                {"center",
                 group == "C" ? sg::Json::array({720, 540}) : sg::Json::array({1000.25, 750.5})}};
    }
}

int main(int argc, char **argv)
{
    try
    {
        if (argc != 4)
            throw std::runtime_error(
                "用法: block3_fixture <plan|sample|generate> <C|H> <新输出目录>");
        std::string mode = argv[1], group = argv[2];
        if (group != "C" && group != "H")
            throw std::runtime_error("profile must be C/H");
        if (mode != "plan" && mode != "sample" && mode != "generate")
            throw std::runtime_error("invalid mode");
        auto pieces = model(MODEL_YAML);
        sg::fs::path dir = argv[3];
        if (sg::fs::exists(dir))
            throw std::runtime_error("refusing to overwrite output");
        sg::fs::create_directories(dir);
        sg::Json manifest = {
            {"version", "block3-v1"},
            {"profile", group},
            {"model_yaml_sha256", sg::hash_file(MODEL_YAML)},
            {"approval", "2026-10-05 user confirmed marker_geometry.yaml; M160 L416 S64"},
            {"renderer", "synthetic_gen_core 8x8 coverage"},
            {"noise", "mt19937/Box-Muller-v1; sigma1 gray; lround-clamp-BGR"},
            {"rounding", "tangent circular arcs;16 segments per90deg; nominal intersection truth"},
            {"model_truth_is_detector_input", false},
            {"planned_originals", 240},
            {"planned_localizations", 720},
            {"mode", mode},
            {"environment", sg::environment()}};
        std::string records;
        int count = 0;
        sg::Point center = group == "C" ? sg::Point(720, 540) : sg::Point(1000.25, 750.5);
        for (int radius : {0, 2})
            for (int angle = 0; angle < 360; angle += 15)
                for (int seed : {0, group == "C" ? 11 : 55, group == "C" ? 22 : 66,
                                 group == "C" ? 33 : 77, group == "C" ? 44 : 88})
                {
                    if (mode == "sample" &&
                        (angle != 15 || (seed != 0 && seed != (group == "C" ? 11 : 55))))
                        continue;
                    auto record = descriptor(group, radius, angle, seed);
                    std::string id = group + "-r" + std::to_string(radius) + "-a" +
                                     std::to_string(angle) + "-s" + std::to_string(seed);
                    record["case_id"] = id;
                    auto generated = scene(pieces, angle, radius, center);
                    sg::Json truth = sg::Json::array();
                    for (auto [part, vertex] : std::vector<std::pair<std::string, int>>{
                             {"L0", 0}, {"M1", 1}, {"L2", 0}, {"L3", 0}})
                    {
                        for (const auto &piece : generated.pieces)
                            if (piece.id == part)
                            {
                                auto p = piece.ideal[vertex];
                                truth.push_back({p.x, p.y});
                            }
                    }
                    record["physical_corners_original"] = truth;
                    if (mode != "plan")
                    {
                        auto raster = sg::rasterize(generated, 1440, 1080);
                        auto bgr = input(raster, seed);
                        auto path = dir / (id + ".png");
                        if (!cv::imwrite(path.string(), bgr))
                            throw std::runtime_error("PNG write failed");
                        record["image"] = path.filename().string();
                        record["image_sha256"] = sg::hash_file(path);
                        auto instance_path = dir / (id + "-instances.png");
                        if (!cv::imwrite(instance_path.string(), raster.instances))
                            throw std::runtime_error("instance PNG write failed");
                        record["instances"] = instance_path.filename().string();
                        record["instances_sha256"] = sg::hash_file(instance_path);
                        auto read = cv::imread(path.string());
                        if (read.size() != cv::Size(1440, 1080) || read.type() != CV_8UC3 ||
                            cv::norm(read, bgr, cv::NORM_INF) != 0)
                            throw std::runtime_error("PNG readback failed");
                        if (mode == "sample" && radius == 2 && seed == 0)
                        {
                            auto finer =
                                sg::rasterize(scene(pieces, angle, radius, center, 32), 1440, 1080);
                            record["rounding_16_vs_32_gray_max_diff"] =
                                cv::norm(raster.image, finer.image, cv::NORM_INF);
                            record["rounding_sagitta_bound_px"] = 2 * (1 - std::cos(CV_PI / 64));
                        }
                    }
                    records += sg::canonical(record) + "\n";
                    ++count;
                }
        manifest["records"] = count;
        sg::write_text(dir / "truth.jsonl", records);
        sg::write_json(dir / "manifest.json", manifest);
        std::cout << "records=" << count << " output=" << dir << '\n';
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
