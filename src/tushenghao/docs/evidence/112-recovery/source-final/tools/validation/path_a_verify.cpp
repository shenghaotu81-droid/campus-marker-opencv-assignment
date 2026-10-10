// Path A 专项只读验收：比较全部公共字段、回溯锚点资格、精确复算原拟合。
// 不参与生产准入，不替用户批准真值/方向；损坏/缺失证据一律非零退出。
#include "common/frame_record_reader.hpp"
#include "config/config.hpp"
#include "core/marker_geometry.hpp"
#include "geometry/geometry_anchor_evidence.hpp"
#include "geometry/geometry_l_topology.hpp"
#include "corners/detection_validator.hpp"
#include "corners/detection_publication.hpp"
#include "corners/semantic_resolver.hpp"
#include "pipeline/diagnostics_recorder.hpp"
#include <opencv2/calib3d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <map>
#include <regex>
#include <set>
#include <sstream>

namespace
{
    namespace fs = std::filesystem;
    using J = mark::validation::JsonValue;
    using mark::validation::readJson;
    using mark::quoteJson;

    void require(bool ok, const std::string &reason)
    {
        if (!ok)
            throw std::runtime_error(reason);
    }

    std::string read(const fs::path &p)
    {
        std::ifstream f(p, std::ios::binary);
        require(bool(f), "cannot read " + p.string());
        std::string s{std::istreambuf_iterator<char>(f), {}};
        require(f.eof() || !f.fail(), "read failed " + p.string());
        return s;
    }

    void write(const fs::path &p, const std::string &s)
    {
        require(!fs::exists(p), "refuse existing file " + p.string());
        std::ofstream f(p);
        f << s;
        f.close();
        require(bool(f), "write failed " + p.string());
    }

    const std::vector<J> &array(const J &v)
    {
        require(v.kind == J::Kind::Array, "expected array");
        return v.array;
    }

    std::string str(const J &v)
    {
        require(v.kind == J::Kind::String, "expected string");
        return v.text;
    }

    bool boolean(const J &v)
    {
        require(v.kind == J::Kind::Bool, "expected bool");
        return v.boolean;
    }

    int integer(const J &v)
    {
        auto n = v.i64();
        require(n >= INT_MIN && n <= INT_MAX, "integer overflow");
        return int(n);
    }

    uint64_t unsignedToken(const std::string &s)
    {
        uint64_t v = 0;
        auto result = std::from_chars(s.data(), s.data() + s.size(), v);
        require(!s.empty() && result.ec == std::errc{} && result.ptr == s.data() + s.size(),
                "invalid unsigned integer");
        return v;
    }

    // 数字保留原 token，原角点/证据 JSON 不做显示舍入。浮点比较不加容差。
    std::string json(const J &v)
    {
        switch (v.kind)
        {
        case J::Kind::Null:
            return "null";
        case J::Kind::String:
            return quoteJson(v.text);
        case J::Kind::Number:
            v.number();
            return v.text;
        case J::Kind::Bool:
            return v.boolean ? "true" : "false";
        case J::Kind::Array:
        {
            std::string s = "[";
            for (size_t i = 0; i < v.array.size(); ++i)
            {
                if (i)
                    s += ',';
                s += json(v.array[i]);
            }
            return s + ']';
        }
        case J::Kind::Object:
        {
            std::string s = "{";
            for (auto &kv : v.object)
            {
                if (s.size() > 1)
                    s += ',';
                s += quoteJson(kv.first) + ':' + json(kv.second);
            }
            return s + '}';
        }
        }
        throw std::runtime_error("JSON kind");
    }

    std::string csv(const std::string &s)
    {
        std::string out = "\"";
        for (char c : s)
        {
            if (c == '"')
                out += '"';
            out += c;
        }
        return out + '"';
    }

    cv::Point2d point(const J &v)
    {
        auto &a = array(v);
        require(a.size() == 2, "point arity");
        return {a[0].number(), a[1].number()};
    }

    std::vector<cv::Point2d> points(const J &v)
    {
        std::vector<cv::Point2d> out;
        for (auto &p : array(v))
            out.push_back(point(p));
        return out;
    }

    cv::Size size(const J &v)
    {
        auto &a = array(v);
        require(a.size() == 2, "size arity");
        int w = integer(a[0]), h = integer(a[1]);
        require(w > 0 && h > 0, "nonpositive size");
        return {w, h};
    }

    std::string diagnostics(const J &record)
    {
        std::string s;
        for (auto &d : array(record.at("details").at("decode").at("diagnostics")))
        {
            if (!s.empty())
                s += ';';
            s += str(d);
        }
        return s;
    }

    bool unresolved(const J &r)
    {
        return diagnostics(r).find("UNRESOLVED_COMPETING_GEOMETRY") != std::string::npos;
    }

    std::vector<J> records(const fs::path &run, uint64_t expected = 0, bool subset = false)
    {
        std::ifstream f(run / "frames.jsonl");
        require(bool(f), "missing frames.jsonl");
        std::string line;
        std::vector<J> out;
        std::set<uint64_t> ids;
        while (std::getline(f, line))
        {
            require(!line.empty(), "empty JSONL record");
            auto r = readJson(line);
            auto id = r.at("frame_id").u64();
            require(ids.insert(id).second, "duplicate frame_id");
            require(subset || id == out.size(), "missing or unordered frame_id");
            require(!r.at("details").null(), "EVIDENCE_DETAILS_MISSING");
            r.at("source_timestamp_us").i64();
            size(r.at("original_size"));
            size(r.at("work_size"));
            out.push_back(std::move(r));
        }
        require(f.eof(), "JSONL read failed");
        require(!out.empty(), "empty run comparison refused");
        if (expected)
            require(out.size() == expected, "record frame count mismatch");
        return out;
    }

    cv::FileStorage yaml(const fs::path &p)
    {
        auto s = read(p);
        if (s.rfind("%YAML", 0) != 0)
            s = "%YAML:1.0\n---\n" + s;
        cv::FileStorage f(s, cv::FileStorage::READ | cv::FileStorage::MEMORY);
        require(f.isOpened(), "bad YAML " + p.string());
        return f;
    }

    // manifest 是统一日志的普通 YAML（环境键有引号），不能误用 OpenCV 的专属 YAML 方言。
    // 仿照原 observability_verify 的扁平 schema 读取，额外拒绝重复键/非法层级和类型。
    std::map<std::string, std::string> manifest(const fs::path &path)
    {
        std::map<std::string, std::string> out;
        std::istringstream input(read(path));
        std::string line;
        bool environment = false;
        while (std::getline(input, line))
        {
            if (line.empty())
                continue;
            bool nested = line.rfind("  ", 0) == 0;
            require(nested ? environment : line[0] != ' ', "invalid manifest indentation");
            auto text = nested ? line.substr(2) : line;
            auto split = text.find(':');
            require(split != std::string::npos, "invalid manifest field");
            auto key = text.substr(0, split);
            if (!key.empty() && key[0] == '"')
                key = str(readJson(key));
            auto value = text.substr(split + 1);
            if (!nested && key == "environment")
            {
                require(value.empty() && !environment, "duplicate/malformed environment");
                environment = true;
                continue;
            }
            require(!value.empty() && value[0] == ' ', "missing manifest scalar");
            value.erase(0, 1);
            if (!value.empty() && value[0] == '"')
                value = str(readJson(value));
            else
            {
                auto scalar = readJson(value);
                require(scalar.kind == J::Kind::Number, "unsupported manifest scalar");
                scalar.number();
            }
            require(out.emplace((nested ? "environment." : "") + key, value).second,
                    "duplicate manifest key");
        }
        require(input.eof(), "manifest read failed");
        return out;
    }

    // 递归比较生产配置全部字段；只允许 App 输出路径/源码溯源项不同。
    std::string canonical(const cv::FileNode &n)
    {
        require(!n.empty(), "config/manifest field missing");
        if (n.isMap())
        {
            std::map<std::string, std::string> values;
            for (auto it = n.begin(); it != n.end(); ++it)
                values[(*it).name()] = canonical(*it);
            std::string s = "{";
            for (auto &v : values)
                s += quoteJson(v.first) + ':' + v.second + ',';
            return s + '}';
        }
        if (n.isSeq())
        {
            std::string s = "[";
            for (auto it = n.begin(); it != n.end(); ++it)
                s += canonical(*it) + ',';
            return s + ']';
        }
        if (n.isString())
            return quoteJson(std::string(n));
        require(n.isInt() || n.isReal(), "unsupported config value");
        double v = double(n);
        require(std::isfinite(v), "nonfinite config");
        std::ostringstream o;
        o.imbue(std::locale::classic());
        o << std::setprecision(17) << v;
        return o.str();
    }

    std::string fingerprint(const fs::path &p)
    {
        std::ifstream f(p, std::ios::binary);
        require(bool(f), "cannot fingerprint " + p.string());
        uint64_t hash = 14695981039346656037ull, count = 0;
        char buf[65536];
        while (f.read(buf, sizeof buf) || f.gcount())
        {
            for (std::streamsize i = 0; i < f.gcount(); ++i)
            {
                hash ^= static_cast<unsigned char>(buf[i]);
                hash *= 1099511628211ull;
            }
            count += f.gcount();
        }
        require(f.eof(), "fingerprint read failed");
        std::ostringstream s;
        s << "FNV1a64:" << std::hex << std::setfill('0') << std::setw(16) << hash
          << ":size=" << std::dec << count;
        return s.str();
    }

    void configs(const fs::path &old, const fs::path &now, const fs::path &supplied)
    {
        auto a = mark::loadConfig(old / "effective_config.yaml"),
             b = mark::loadConfig(now / "effective_config.yaml"), c = mark::loadConfig(supplied);
        (void)a;
        (void)b;
        auto x = yaml(old / "effective_config.yaml"), y = yaml(now / "effective_config.yaml"),
             z = yaml(supplied);
        for (auto k : {"schema_version", "input", "preprocess", "geometry", "detector", "temporal"})
            require(canonical(x[k]) == canonical(y[k]) && canonical(x[k]) == canonical(z[k]),
                    "algorithm config mismatch: " + std::string(k));
        auto om = manifest(old / "manifest.yaml"), nm = manifest(now / "manifest.yaml");
        require(om.at("input_fingerprint") == nm.at("input_fingerprint"),
                "input fingerprint mismatch");
        require(om.at("environment.model_fingerprint") == nm.at("environment.model_fingerprint"),
                "model fingerprint mismatch");
        require(nm.at("environment.model_fingerprint") ==
                    fingerprint(c.detector_config.marker_geometry_path_),
                "supplied model mismatch");
        require(nm.at("input_fingerprint") == fingerprint(nm.at("source_path")),
                "input file mismatch");
        for (auto *m : {&om, &nm})
        {
            require(m->at("environment.completion_reason") == "COMPLETE" &&
                        m->at("environment.coverage_verified") == "true" &&
                        m->at("environment.execution_scope") == "full",
                    "run not complete full scope");
        }
    }

    // 缺任何 CornerEvidence 字段都拒绝，不能用默认0替失踪残差或物理标签。
    mark::CornerMeasurement measurement(const J &v, uint64_t frame,
                                        const std::set<size_t> &components)
    {
        auto &es = array(v);
        require(es.size() == 4, "measurement must contain four evidence entries");
        mark::CornerMeasurement m;
        std::set<int> physical;
        for (auto &data : es)
        {
            int id = integer(data.at("physical"));
            require(id >= 0 && id < 4 && physical.insert(id).second, "invalid physical identity");
            auto &e = m.evidence_[id];
            e.physical_corner_ = static_cast<mark::PhysicalCorner>(id);
            e.frame_id_ = data.at("frame_id").u64();
            e.component_id_ = data.at("component_id").u64();
            require(e.frame_id_ == frame && components.count(e.component_id_),
                    "noncurrent evidence reference");
            e.stable_id_ = str(data.at("evidence_id"));
            require(!e.stable_id_.empty(), "empty evidence ID");
            e.model_vertex_id_ = integer(data.at("model_vertex_id"));
            auto &model = array(data.at("model_edge_ids"));
            auto &starts = array(data.at("observed_segment_ids"));
            auto &ends = array(data.at("observed_segment_end_ids"));
            auto &lines = array(data.at("fitted_lines"));
            auto &segments = array(data.at("finite_segments"));
            auto &arcs = array(data.at("support_arcs"));
            auto &mean = array(data.at("line_mean_residual_px"));
            auto &max = array(data.at("line_max_residual_px"));
            auto &extension = array(data.at("support_extension_px"));
            for (auto *pair :
                 {&model, &starts, &ends, &lines, &segments, &arcs, &mean, &max, &extension})
                require(pair->size() == 2, "two-edge field arity");
            for (size_t i = 0; i < 2; ++i)
            {
                e.model_edge_ids_[i] = integer(model[i]);
                e.observed_segment_ids_.push_back(integer(starts[i]));
                e.observed_segment_end_ids_[i] = integer(ends[i]);
                auto &l = array(lines[i]);
                require(l.size() == 4, "line arity");
                for (size_t k = 0; k < 4; ++k)
                    (i ? e.line_b_ : e.line_a_)[k] = l[k].number();
                auto seg = points(segments[i]);
                require(seg.size() == 2, "segment arity");
                for (size_t k = 0; k < 2; ++k)
                    (i ? e.edge_segment_b_ : e.edge_segment_a_)[k] = seg[k];
                e.original_support_arcs_[i] = points(arcs[i]);
                e.line_mean_residual_px_[i] = mean[i].number();
                e.line_max_residual_px_[i] = max[i].number();
                e.support_extension_px_[i] = extension[i].number();
            }
            e.original_turn_arc_ = points(data.at("turn_arc"));
            e.original_observation_ = boolean(data.at("original_observation"));
            e.truncated_ = boolean(data.at("truncated"));
            e.corner_error_px_ = data.at("corner_error_px").number();
            e.error_ = data.at("measurement_error_px").number();
            e.intersection_ = point(data.at("intersection"));
            m.physical_corners_[id] = e.intersection_;
        }
        return m;
    }

    std::set<size_t> components(const J &r)
    {
        std::set<size_t> out;
        for (auto &c : array(r.at("details").at("components")))
            require(out.insert(c.at("component_id").u64()).second, "duplicate component");
        return out;
    }

    std::map<size_t, mark::ShapeObservation> observations(const J &r)
    {
        std::map<size_t, mark::ShapeObservation> out;
        for (auto &data : array(r.at("details").at("observations")))
        {
            mark::ShapeObservation o;
            o.source_component_id_ = data.at("source_component_id").u64();
            for (auto &raw : array(data.at("l_topology_candidates")))
            {
                mark::LTopologyCandidate t;
                for (auto p : points(raw.at("polygon")))
                {
                    cv::Point2f f(p);
                    require(std::isfinite(f.x) && std::isfinite(f.y) && cv::Point2d(f) == p,
                            "topology float loss");
                    t.polygon_.push_back(f);
                }
                for (auto &i : array(raw.at("concave_vertex_indices")))
                    t.concave_vertex_indices_.push_back(i.u64());
                t.simplification_epsilon_ = raw.at("simplification_epsilon").number();
                o.l_topology_candidates_.push_back(t);
            }
            require(out.emplace(o.source_component_id_, std::move(o)).second,
                    "duplicate observation");
        }
        return out;
    }

    struct AnchorProof
    {
        std::string part;
        size_t component;
        cv::Point2f point;
        std::vector<mark::LAnchorTopologySupport> supports;
    };

    AnchorProof proof(const std::string &text)
    {
        static const std::regex pattern(
            R"(^anchor_topology/v1 part=([^ ]+) component=([0-9]+) x=([^ ]+) y=([^ ]+) supports=([0-9]+:[0-9]+(,[0-9]+:[0-9]+)*)$)");
        std::smatch match;
        require(std::regex_match(text, match, pattern), "invalid anchor evidence syntax");
        AnchorProof p;
        p.part = match[1];
        p.component = unsignedToken(match[2]);
        double x = readJson(match[3]).number(), y = readJson(match[4]).number();
        p.point = {float(x), float(y)};
        require(std::isfinite(p.point.x) && std::isfinite(p.point.y), "nonfinite float anchor");
        std::istringstream input(match[5]);
        std::string token;
        while (std::getline(input, token, ','))
        {
            auto sep = token.find(':');
            p.supports.push_back({size_t(unsignedToken(token.substr(0, sep))),
                                  size_t(unsignedToken(token.substr(sep + 1)))});
        }
        return p;
    }

    struct EvidenceStats
    {
        uint64_t parents{}, anchors{}, measurements{}, detections{};
        double max_projection_error{};
    };

    void anchors(const J &r, const mark::MarkerGeometry &model, EvidenceStats &stats)
    {
        auto os = observations(r);
        auto ids = components(r);
        for (auto stage : {"generated", "validated", "completed"})
        {
            auto &hs = array(r.at("details").at(stage).at("hypotheses"));
            require(r.at("counts").at(stage).u64() == hs.size(), "hypothesis count mismatch");
            for (auto &h : hs)
            {
                ++stats.parents;
                std::map<std::string, AnchorProof> ps;
                for (auto &raw : array(h.at("evidence")))
                {
                    auto text = str(raw);
                    if (text.rfind("anchor_topology/", 0) == 0)
                    {
                        auto p = proof(text);
                        require(ps.emplace(p.part, p).second, "duplicate anchor proof");
                    }
                }
                require(ps.size() == 3, "missing three anchor proofs");
                std::vector<cv::Point2f> src, dst;
                std::set<std::string> parts;
                std::set<size_t> used;
                for (auto &a : array(h.at("assignments")))
                {
                    auto part = str(a.at("model_part_id"));
                    auto id = a.at("component_id").u64();
                    require(ids.count(id) && parts.insert(part).second && used.insert(id).second,
                            "bad assignment source");
                    if (part.empty() || part[0] != 'L')
                        continue;
                    auto it = ps.find(part);
                    require(it != ps.end() && it->second.component == id && os.count(id),
                            "anchor assignment reference mismatch");
                    auto &p = it->second;
                    auto collection = mark::collect_observed_l_anchors(os.at(id), id);
                    auto found =
                        std::find_if(collection.anchors_.begin(), collection.anchors_.end(),
                                     [&](auto &a)
                                     {
                                         return a.point_ == p.point;
                                     });
                    require(found != collection.anchors_.end(), "unqualified anchor coordinate");
                    require(p.supports.size() == found->topology_supports_.size(),
                            "incomplete topology supports");
                    for (size_t i = 0; i < p.supports.size(); ++i)
                    {
                        auto &actual = found->topology_supports_[i];
                        auto &supplied = p.supports[i];
                        require(actual.topology_candidate_index_ ==
                                        supplied.topology_candidate_index_ &&
                                    actual.concave_vertex_index_ == supplied.concave_vertex_index_,
                                "forged or reordered topology support");
                        auto &t =
                            os.at(id).l_topology_candidates_.at(supplied.topology_candidate_index_);
                        require(mark::is_valid_l_topology_candidate(t) &&
                                    t.polygon_.at(supplied.concave_vertex_index_) == p.point,
                                "invalid topology support");
                    }
                    auto m = std::find_if(model.polygons.begin(), model.polygons.end(),
                                          [&](auto &m)
                                          {
                                              return m.id == part;
                                          });
                    require(m != model.polygons.end(), "missing model L");
                    src.push_back(m->anchor);
                    dst.push_back(p.point);
                    ++stats.anchors;
                }
                require(src.size() == 3, "not three L assignments");
                auto fit = cv::estimateAffine2D(src, dst);
                require(!fit.empty(), "anchor affine recomputation failed");
                auto &recorded = array(h.at("affine"));
                require(recorded.size() == 6, "affine arity");
                for (size_t k = 0; k < 6; ++k)
                {
                    double a = fit.at<double>(int(k / 3), int(k % 3)), b = recorded[k].number();
                    if (a != b)
                    {
                        std::ostringstream error;
                        error << std::setprecision(17)
                              << "AFFINE_EXACT_MISMATCH frame=" << r.at("frame_id").u64()
                              << " stage=" << stage << " recorded=" << json(h.at("affine"))
                              << " recomputed=" << fit;
                        throw std::runtime_error(error.str());
                    }
                }
                for (size_t i = 0; i < 3; ++i)
                {
                    auto p = src[i];
                    cv::Point2d projected(fit.at<double>(0, 0) * p.x + fit.at<double>(0, 1) * p.y +
                                              fit.at<double>(0, 2),
                                          fit.at<double>(1, 0) * p.x + fit.at<double>(1, 1) * p.y +
                                              fit.at<double>(1, 2));
                    stats.max_projection_error = std::max(
                        stats.max_projection_error, cv::norm(projected - cv::Point2d(dst[i])));
                }
            }
        }
    }

    // 用实际生产排序、证据validator、语义与float发布重建输出，原双精度不丢字段。
    void detections(const J &r, const mark::CornerConfig &config, EvidenceStats &stats)
    {
        auto ids = components(r);
        auto original = size(r.at("original_size"));
        auto &decoded = r.at("details").at("decode");
        auto frame = r.at("frame_id").u64();
        std::vector<mark::CornerMeasurement> ms;
        for (auto &raw : array(decoded.at("measurements")))
        {
            auto m = measurement(raw, frame, ids);
            auto order = mark::orderScreenCorners(m.physical_corners_, config);
            require(bool(order.screen_order_), "reconstructed screen order failed");
            auto valid = mark::validateDetectionGeometry(m, *order.screen_order_, original, config);
            require(valid.valid_,
                    "reconstructed current evidence invalid: " + valid.rejection_reason_);
            ms.push_back(m);
            ++stats.measurements;
        }
        require(r.at("counts").at("measurements").u64() == ms.size(), "measurement count mismatch");
        auto result =
            mark::validation::readResult(r.at("result"), frame, r.at("source_timestamp_us").i64());
        require(integer(r.at("result_status")) == int(result.status) &&
                    integer(decoded.at("status")) == int(result.status),
                "status mismatch");
        auto &raw = array(r.at("result").at("detections"));
        require(raw == array(decoded.at("detections")), "decode/public raw mismatch");
        require(r.at("counts").at("detections").u64() == raw.size(), "detection count mismatch");
        if (result.status != mark::Status::DETECTED)
        {
            require(raw.empty(), "failed status with payload");
            return;
        }
        require(!raw.empty() && !ms.empty() && !unresolved(r),
                "successful status lacks resolved measurement");
        auto semantic = mark::resolveSemantics(ms, boolean(decoded.at("search_truncated")), config);
        require(semantic.geometry_consistent_ &&
                    semantic.retained_measurements_.size() == raw.size(),
                "semantic result mismatch");
        for (size_t i = 0; i < raw.size(); ++i)
        {
            auto &d = raw[i];
            require(d.at("marker_code").null() && d.at("confidence").null(),
                    "frozen unknown fields changed");
            require(integer(d.at("category")) == 0 && d.at("quality_flags").u64() == 0,
                    "frozen category/flags changed");
            std::string reason;
            auto published =
                mark::publishFloatDetection(semantic.retained_measurements_[i],
                                            semantic.orientation_unique_, original, config, reason);
            require(bool(published), "recomputed publish failed: " + reason);
            auto &actual = result.detections[i];
            require(actual.corners == published->corners && actual.bbox == published->bbox &&
                        actual.attributes.orientation == published->attributes.orientation,
                    "raw geometry/orientation not from current retained measurement");
            auto &corners = array(d.at("corners"));
            for (size_t k = 0; k < 4; ++k)
                require(point(corners[k]) == cv::Point2d(actual.corners[k]),
                        "serialized raw float loss");
            ++stats.detections;
        }
    }

    std::map<std::string, std::string> options(int argc, char **argv, bool render)
    {
        std::set<std::string> allowed =
            render ? std::set<std::string>{"--candidate-run", "--video", "--review-list",
                                           "--output-dir"}
                   : std::set<std::string>{"--baseline-run", "--candidate-run", "--config",
                                           "--expected-frames", "--report-dir"};
        std::map<std::string, std::string> out;
        for (int i = render ? 2 : 1; i < argc; ++i)
        {
            std::string key = argv[i];
            require(allowed.count(key) && i + 1 < argc &&
                        std::string(argv[i + 1]).rfind("--", 0) != 0,
                    "unknown/missing option " + key);
            require(out.emplace(key, argv[++i]).second, "duplicate option " + key);
        }
        require(out.size() == allowed.size(), "all options are required");
        return out;
    }

    struct Differences
    {
        bool corners{}, orientation{}, other{};
    };

    Differences difference(const J &a, const J &b)
    {
        Differences out;
        auto &x = array(a), &y = array(b);
        if (x.size() != y.size())
            return {true, true, true};
        for (size_t i = 0; i < x.size(); ++i)
        {
            out.corners |= !(x[i].at("corners") == y[i].at("corners"));
            out.orientation |= !(x[i].at("orientation") == y[i].at("orientation"));
            auto old = x[i], now = y[i];
            old.object.erase("corners");
            old.object.erase("orientation");
            now.object.erase("corners");
            now.object.erase("orientation");
            out.other |= !(old == now);
        }
        return out;
    }

    uint64_t parents(const J &r)
    {
        return array(r.at("details").at("completed").at("hypotheses")).size();
    }

    std::string compare(const std::map<std::string, std::string> &opts, const fs::path &output)
    {
        auto expected = unsignedToken(opts.at("--expected-frames"));
        require(expected > 0, "expected frames must be positive");
        fs::path old = opts.at("--baseline-run"), now = opts.at("--candidate-run");
        auto a = records(old, expected), b = records(now, expected);
        configs(old, now, opts.at("--config"));
        auto cfg = mark::loadConfig(opts.at("--config"));
        auto model = mark::loadMarkerGeometry(cfg.detector_config.marker_geometry_path_);
        std::vector<uint64_t> pathA, remaining, newUnresolved;
        uint64_t recovered = 0, oldSuccess = 0, newSuccess = 0, loss = 0, outside = 0,
                 rawChanges = 0, cornerChanges = 0, orientationChanges = 0, historyChanges = 0;
        double maxDisplacement = 0;
        EvidenceStats evidence;
        std::ostringstream frames, raw, history, failed, review;
        frames
            << "frame_id,old_status,new_status,old_parents,new_parents,old_measurements,new_measurements,recovered,reason\n";
        raw << "frame_id,status_changed,corners_changed,orientation_changed,other_fields_changed,old_raw,new_raw\n";
        history
            << "frame_id,tracks_changed,display_changed,old_tracks,new_tracks,old_display,new_display\n";
        failed << "frame_id,new_status,new_parents,new_measurements,reason\n";
        review << "frame_id,reason,baseline_run,candidate_run\n";
        for (size_t i = 0; i < a.size(); ++i)
        {
            auto &x = a[i];
            auto &y = b[i];
            for (auto k : {"frame_id", "source_timestamp_us", "timestamp_source", "original_size",
                           "work_size"})
                require(x.at(k) == y.at(k), "frame identity/mapping mismatch frame=" +
                                                std::to_string(i) + " field=" + k);
            require(size(y.at("work_size")) == cv::Size(cfg.detector_config.preprocess.work_width,
                                                        cfg.detector_config.preprocess.work_height),
                    "work/config size mismatch");
            anchors(y, model, evidence);
            detections(y, cfg.detector_config.corner_, evidence);
            auto &xr = x.at("result");
            auto &yr = y.at("result");
            int os = integer(xr.at("status")), ns = integer(yr.at("status"));
            require((os == 2 || os == 3) && (ns == 2 || ns == 3), "unexpected video status");
            bool inA = unresolved(x), restore = os != 3 && ns == 3;
            if (os == 3)
            {
                ++oldSuccess;
                if (ns != 3)
                    ++loss;
            }
            if (ns == 3)
                ++newSuccess;
            if (!inA && os != ns)
                ++outside;
            if (inA)
            {
                pathA.push_back(i);
                recovered += restore;
                frames << i << ',' << os << ',' << ns << ',' << parents(x) << ',' << parents(y)
                       << ',' << array(x.at("details").at("decode").at("measurements")).size()
                       << ',' << array(y.at("details").at("decode").at("measurements")).size()
                       << ',' << restore << ',' << csv(diagnostics(y)) << '\n';
                if (ns != 3)
                {
                    remaining.push_back(i);
                    failed << i << ',' << ns << ',' << parents(y) << ','
                           << array(y.at("details").at("decode").at("measurements")).size() << ','
                           << csv(diagnostics(y)) << '\n';
                }
            }
            auto d = difference(xr.at("detections"), yr.at("detections"));
            bool status = os != ns;
            bool rawChanged = status || !(xr.at("detections") == yr.at("detections"));
            if (rawChanged)
            {
                ++rawChanges;
                raw << i << ',' << status << ',' << d.corners << ',' << d.orientation << ','
                    << d.other << ',' << csv(json(xr.at("detections"))) << ','
                    << csv(json(yr.at("detections"))) << '\n';
            }
            if (os == 3 && ns == 3)
            {
                cornerChanges += d.corners;
                orientationChanges += d.orientation;
                if (d.corners)
                {
                    auto &p = array(xr.at("detections")), &q = array(yr.at("detections"));
                    if (p.size() == q.size())
                        for (size_t j = 0; j < p.size(); ++j)
                            for (size_t k = 0; k < 4; ++k)
                                maxDisplacement = std::max(
                                    maxDisplacement, cv::norm(point(array(p[j].at("corners"))[k]) -
                                                              point(array(q[j].at("corners"))[k])));
                }
            }
            bool track = !(xr.at("tracks") == yr.at("tracks")),
                 display = !(xr.at("display") == yr.at("display"));
            if (track || display)
            {
                ++historyChanges;
                history << i << ',' << track << ',' << display << ',' << csv(json(xr.at("tracks")))
                        << ',' << csv(json(yr.at("tracks"))) << ',' << csv(json(xr.at("display")))
                        << ',' << csv(json(yr.at("display"))) << '\n';
            }
            if (restore || (os == 3 && ns == 3 && (d.corners || d.orientation)))
                review << i << ','
                       << csv(restore ? "new_success" : "raw_geometry_or_orientation_change") << ','
                       << csv(fs::absolute(old).string()) << ',' << csv(fs::absolute(now).string())
                       << '\n';
            if (unresolved(y))
                newUnresolved.push_back(i);
        }
        write(output / "path_a_frames.csv", frames.str());
        write(output / "raw_changes.csv", raw.str());
        write(output / "history_changes.csv", history.str());
        write(output / "remaining_failures.csv", failed.str());
        write(output / "review_required.csv", review.str());
        auto list = [](const std::vector<uint64_t> &ids)
        {
            std::string s = "[";
            for (auto id : ids)
            {
                if (s.size() > 1)
                    s += ',';
                s += std::to_string(id);
            }
            return s + ']';
        };
        bool pass = pathA.size() == 118 && oldSuccess == 864 && loss == 0 && outside == 0;
        bool representatives = expected > 1045 && integer(b[2].at("result").at("status")) == 3 &&
                               integer(b[1045].at("result").at("status")) == 3;
        pass &= representatives;
        std::ostringstream report;
        report.imbue(std::locale::classic());
        report
            << std::setprecision(17) << "{\"result\":" << quoteJson(pass ? "PASS" : "FAIL")
            << ",\"machine_only\":true,\"user_review\":\"PENDING\",\"frames\":" << expected
            << ",\"integrity\":\"PASS\",\"config_protection\":\"PASS\",\"anchor_evidence\":\"PASS\",\"affine_exact_recomputation\":\"PASS\",\"current_corner_validator\":\"PASS\",\"orientation_publication\":\"PASS\",\"old_detected\":"
            << oldSuccess << ",\"new_detected\":" << newSuccess
            << ",\"old_path_a\":" << pathA.size() << ",\"recovered\":" << recovered
            << ",\"remaining\":" << list(remaining) << ",\"old_success_losses\":" << loss
            << ",\"outside_a_status_changes\":" << outside << ",\"raw_changes\":" << rawChanges
            << ",\"old_success_corner_changes\":" << cornerChanges
            << ",\"old_success_orientation_changes\":" << orientationChanges
            << ",\"history_changes\":" << historyChanges
            << ",\"max_corresponding_screen_displacement_px\":" << maxDisplacement
            << ",\"parents_checked\":" << evidence.parents
            << ",\"anchors_checked\":" << evidence.anchors
            << ",\"measurements_checked\":" << evidence.measurements
            << ",\"published_detections_checked\":" << evidence.detections
            << ",\"max_model_anchor_projection_error_work_px\":" << evidence.max_projection_error
            << ",\"projection_is_truth_error\":false,\"representatives_2_1045\":"
            << quoteJson(representatives ? "PASS" : "FAIL") << ",\"path_a_ids\":" << list(pathA)
            << ",\"new_unresolved_ids\":" << list(newUnresolved) << "}\n";
        write(output / "path_a_report.json", report.str());
        require(
            pass,
            "hard gate failed: Path A count/old success/old loss/outside-A/representatives; see report");
        return report.str();
    }

    // CSV 严格读取工具自己写的四字段清单，路径可含逗号/引号；不依赖粗略 split。
    std::vector<std::string> fields(const std::string &line)
    {
        std::vector<std::string> out;
        std::string value;
        bool quoted = false, closed = false;
        for (size_t i = 0; i < line.size(); ++i)
        {
            char c = line[i];
            if (quoted)
            {
                if (c == '"')
                {
                    if (i + 1 < line.size() && line[i + 1] == '"')
                    {
                        value += '"';
                        ++i;
                    }
                    else
                    {
                        quoted = false;
                        closed = true;
                    }
                }
                else
                    value += c;
            }
            else if (c == ',')
            {
                out.push_back(value);
                value.clear();
                closed = false;
            }
            else if (c == '"')
            {
                require(value.empty() && !closed, "bad CSV quote");
                quoted = true;
            }
            else
            {
                require(!closed, "trailing CSV content");
                value += c;
            }
        }
        require(!quoted, "unclosed CSV quote");
        out.push_back(value);
        return out;
    }

    // 按零基顺序实际解码取帧；每份四角 measurement 独立图层，不混合父证据。
    std::string render(const std::map<std::string, std::string> &opts, const fs::path &output)
    {
        auto run = fs::path(opts.at("--candidate-run"));
        auto all = records(run, 0, true);
        std::map<uint64_t, const J *> byId;
        for (auto &r : all)
            byId.emplace(r.at("frame_id").u64(), &r);
        auto cfg = mark::loadConfig(run / "effective_config.yaml");
        auto metadata = manifest(run / "manifest.yaml");
        require(fingerprint(opts.at("--video")) == metadata.at("input_fingerprint"),
                "render input fingerprint mismatch");
        std::ifstream list(opts.at("--review-list"));
        require(bool(list), "missing review list");
        std::string line;
        require(bool(std::getline(list, line)) &&
                    line == "frame_id,reason,baseline_run,candidate_run",
                "invalid review list header");
        std::map<uint64_t, std::vector<std::string>> requests;
        std::map<fs::path, std::vector<J>> baselines;
        while (std::getline(list, line))
        {
            auto f = fields(line);
            require(f.size() == 4, "review row arity");
            auto id = unsignedToken(f[0]);
            require(byId.count(id) && requests.emplace(id, f).second,
                    "missing/duplicate review frame");
            require(fs::weakly_canonical(f[3]) == fs::weakly_canonical(run),
                    "review references wrong candidate");
            auto path = fs::path(f[2]);
            if (!baselines.count(path))
                baselines.emplace(path, records(path));
            require(id < baselines.at(path).size(), "review references missing old frame");
        }
        require(list.eof() && !requests.empty(), "empty or unreadable review list");
        cv::VideoCapture capture(opts.at("--video"));
        require(capture.isOpened(), "render video open failed");
        uint64_t frameId = 0, exported = 0;
        cv::Mat original;
        std::ostringstream index;
        index
            << "frame_id,reason,original,evidence,evidence_json,baseline_record,old_raw_corners_orientation,measurements,user_review\n";
        while (capture.read(original))
        {
            auto it = requests.find(frameId);
            if (it != requests.end())
            {
                auto &r = *byId.at(frameId);
                require(original.size() == size(r.at("original_size")),
                        "render original size mismatch");
                auto id = "frame-" + std::to_string(frameId);
                auto originalName = id + "-original.png", evidenceName = id + "-evidence.png",
                     jsonName = id + "-evidence.json";
                require(cv::imwrite((output / originalName).string(), original),
                        "original PNG write failed");
                auto &decode = r.at("details").at("decode");
                auto &layers = array(decode.at("measurements"));
                auto currentComponents = components(r);
                size_t count = std::max(size_t(1), layers.size());
                cv::Mat canvas(int(count) * (original.rows + 72), original.cols, CV_8UC3,
                               cv::Scalar(24, 24, 24));
                auto &old = baselines.at(fs::path(it->second[2]))[frameId];
                for (size_t layer = 0; layer < count; ++layer)
                {
                    cv::Mat image = original.clone();
                    int top = int(layer) * (original.rows + 72);
                    std::string title = "frame=" + std::to_string(frameId) +
                                        " measurement=" + std::to_string(layer) +
                                        " RAW current evidence";
                    if (layers.empty())
                        title += "; NO MEASUREMENT / NO POINTS";
                    if (!layers.empty())
                    {
                        auto m = measurement(layers[layer], frameId, currentComponents);
                        auto ordered = mark::orderScreenCorners(m.physical_corners_,
                                                                cfg.detector_config.corner_);
                        require(bool(ordered.screen_order_), "render invalid screen order");
                        auto valid = mark::validateDetectionGeometry(m, *ordered.screen_order_,
                                                                     original.size(),
                                                                     cfg.detector_config.corner_);
                        require(valid.valid_,
                                "render invalid evidence: " + valid.rejection_reason_);
                        for (size_t p = 0; p < 4; ++p)
                        {
                            auto &e = m.evidence_[p];
                            for (size_t edge = 0; edge < 2; ++edge)
                            {
                                auto &arc = e.original_support_arcs_[edge];
                                for (size_t k = 1; k < arc.size(); ++k)
                                    cv::line(image, arc[k - 1], arc[k],
                                             edge ? cv::Scalar(0, 255, 255)
                                                  : cv::Scalar(255, 255, 0),
                                             2, cv::LINE_AA);
                            }
                            for (size_t k = 1; k < e.original_turn_arc_.size(); ++k)
                                cv::line(image, e.original_turn_arc_[k - 1],
                                         e.original_turn_arc_[k], cv::Scalar(255, 0, 255), 2,
                                         cv::LINE_AA);
                            cv::circle(image, e.intersection_, 4, cv::Scalar(0, 255, 0), 1,
                                       cv::LINE_AA);
                        }
                    }
                    if (it->second[1] == "raw_geometry_or_orientation_change")
                    {
                        for (auto &d : array(old.at("result").at("detections")))
                        {
                            auto corners = points(d.at("corners"));
                            require(corners.size() == 4, "old corners arity");
                            for (size_t k = 0; k < 4; ++k)
                                cv::line(image, corners[k], corners[(k + 1) % 4],
                                         cv::Scalar(0, 128, 255), 1, cv::LINE_AA);
                        }
                        title += "; orange=OLD contrast";
                    }
                    cv::putText(canvas, title, {12, top + 24}, cv::FONT_HERSHEY_SIMPLEX, .5,
                                {240, 240, 240}, 1, cv::LINE_AA);
                    cv::putText(
                        canvas,
                        "cyan/yellow=support; magenta=turn; green=intersection. Physical P0..P3 and orientation: see numeric JSON",
                        {12, top + 50}, cv::FONT_HERSHEY_SIMPLEX, .48, {240, 240, 240}, 1,
                        cv::LINE_AA);
                    image.copyTo(canvas(cv::Rect(0, top + 72, image.cols, image.rows)));
                }
                require(cv::imwrite((output / evidenceName).string(), canvas),
                        "evidence PNG write failed");
                write(output / jsonName,
                      "{\"frame_id\":" + std::to_string(frameId) +
                          ",\"source_timestamp_us\":" + json(r.at("source_timestamp_us")) +
                          ",\"candidate_record\":" + quoteJson((run / "frames.jsonl").string()) +
                          ",\"baseline_record\":" +
                          quoteJson((fs::path(it->second[2]) / "frames.jsonl").string()) +
                          ",\"old_raw\":" + json(old.at("result").at("detections")) +
                          ",\"current_raw\":" + json(r.at("result").at("detections")) +
                          ",\"measurements\":" + json(decode.at("measurements")) +
                          ",\"search_truncated\":" + json(decode.at("search_truncated")) +
                          ",\"diagnostics\":" + json(decode.at("diagnostics")) +
                          ",\"completed_hypotheses\":" +
                          json(r.at("details").at("completed").at("hypotheses")) +
                          ",\"observations\":" + json(r.at("details").at("observations")) +
                          ",\"user_review\":\"PENDING\"}\n");
                index << frameId << ',' << csv(it->second[1]) << ',' << originalName << ','
                      << evidenceName << ',' << jsonName << ','
                      << csv((fs::path(it->second[2]) / "frames.jsonl").string()) << ','
                      << csv(json(old.at("result").at("detections"))) << ',' << layers.size()
                      << ",PENDING\n";
                ++exported;
            }
            ++frameId;
        }
        require(exported == requests.size(), "render missing video frame");
        write(output / "review_index.csv", index.str());
        std::string report =
            "{\"result\":\"PASS\",\"actual_decoded_frames\":" + std::to_string(frameId) +
            ",\"exported_frames\":" + std::to_string(exported) +
            ",\"measurement_layers\":\"independent\",\"user_review\":\"PENDING\"}\n";
        write(output / "render_report.json", report);
        return report;
    }
} // namespace

int main(int argc, char **argv)
{
    fs::path output;
    bool created = false;
    try
    {
        bool images = argc > 1 && std::string(argv[1]) == "render";
        auto opts = options(argc, argv, images);
        output = opts.at(images ? "--output-dir" : "--report-dir");
        require(!output.empty() && !fs::exists(output), "refuse existing output directory");
        require(fs::create_directories(output), "cannot create output directory");
        created = true;
        auto report = images ? render(opts, output) : compare(opts, output);
        std::cout << report;
        return 0;
    }
    catch (std::exception &e)
    {
        std::cerr << "FAIL: " << e.what() << '\n';
        if (created)
        {
            try
            {
                auto target = output / "path_a_report.json";
                if (fs::exists(target))
                    target = output / "FAILED.json";
                write(target, "{\"result\":\"FAIL\",\"error\":" + quoteJson(e.what()) +
                                  ",\"user_review\":\"NOT_APPROVED\"}\n");
            }
            catch (...)
            {
            }
        }
        return 1;
    }
}
