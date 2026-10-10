#include "debug_renderer.hpp"
#include <opencv2/imgproc.hpp>
#include <sstream>

namespace mark
{
    // 纯渲染不写记录；由runner先将验证事件附到记录，避免通过const接口篡改诊断载荷。
    std::vector<ReasonEvent> validateRenderEvidence(const FrameRecord &r)
    {
        std::vector<ReasonEvent> events;
        if (r.details)
            for (const auto &m : r.details->decoded.measurements)
                for (const auto &e : m.evidence_)
                    if (e.frame_id_ != r.frame_id)
                    {
                        events.push_back({Stage::Visualize, ReasonCode::ValidationRejected,
                                          "EVIDENCE_FRAME_MISMATCH", r.frame_id, std::nullopt,
                                          e.component_id_, 1});
                        return events;
                    }
        return events;
    }

    namespace
    {
        // 文本只画在克隆图上，图例与历史来源不进入检测结果。
        void text(cv::Mat &image, const std::string &s, cv::Point p, cv::Scalar color)
        {
            cv::putText(image, s, p, cv::FONT_HERSHEY_SIMPLEX, .45, color, 1, cv::LINE_AA);
        }

        // 稳定层使用虚线与标记，不能把颜色相同的历史四点伪装为当前检测。
        void polygon(cv::Mat &image, const Detection &d, cv::Scalar color, bool dashed)
        {
            for (size_t i = 0; i < 4; ++i)
            {
                auto a = d.corners[i], b = d.corners[(i + 1) % 4];
                if (!dashed)
                    cv::line(image, a, b, color, 1, cv::LINE_AA);
                else
                {
                    double n = cv::norm(b - a);
                    for (double start = 0; start < n; start += 12)
                    {
                        auto p = a + float(start / n) * (b - a),
                             q = a + float(std::min(start + 6., n) / n) * (b - a);
                        cv::line(image, p, q, color, 2, cv::LINE_AA);
                    }
                }
                cv::circle(image, a, 3, color, 1);
            }
        }
    } // namespace

    // 当前有效图层按本帧状态绘制；empty清层，unknown不借用旧方向，历史仅文字。
    cv::Mat renderDebugFrame(const cv::Mat &original, const FrameResult &result,
                             const FrameRecord &r, const RenderConfig &config)
    {
        auto out = original.clone();
        if (out.empty())
            return out;
        int y = 18;
        if (config.draw_raw)
        {
            text(out, "RAW current", {8, y}, {255, 255, 0});
            y += 18;
        }
        if (config.draw_stable)
        {
            text(out, "STABLE current", {8, y}, {0, 255, 0});
            y += 18;
        }
        const bool current = result.status == Status::DETECTED && result.frame_id == r.frame_id;
        if (current && config.draw_raw)
            for (const auto &d : result.detections)
            {
                polygon(out, d, {255, 255, 0}, false);
                const char *labels[] = {"LT", "RT", "RB", "LB"};
                for (size_t i = 0; i < 4; ++i)
                {
                    std::string label = labels[i];
                    if (d.attributes.orientation)
                        for (size_t p = 0; p < 4; ++p)
                            if ((*d.attributes.orientation)[p] == int(i))
                                label += " P" + std::to_string(p);
                    text(out, label, d.corners[i] + cv::Point2f(3, -3), {255, 255, 0});
                }
                if (!d.attributes.orientation)
                {
                    text(out, "orientation=UNKNOWN", {8, y}, {255, 255, 0});
                    y += 18;
                }
            }
        if (current && config.draw_stable)
            for (const auto &t : result.tracks)
                if (t.detection_index < result.detections.size())
                    polygon(out, t.result, {0, 255, 0}, true);
        const bool evidence_ok = validateRenderEvidence(r).empty();
        if (config.draw_candidates)
        {
            text(out, "CANDIDATE", {8, y}, {0, 165, 255});
            y += 18;
            if (r.details && r.details->prepared.frame_id_ == r.frame_id)
                for (const auto &c : r.details->components)
                {
                    std::vector<cv::Point> contour;
                    for (auto p : c.contour_)
                        contour.push_back(r.details->prepared.workToOriginal(p));
                    if (!contour.empty())
                        cv::polylines(out, contour, true, {0, 165, 255}, 1);
                }
        }
        if (config.draw_corner_evidence)
        {
            if (!r.details)
            {
                text(out, "EVIDENCE not sampled", {8, y}, {0, 165, 255});
                y += 18;
            }
            else if (!evidence_ok)
            {
                text(out, "ERROR EVIDENCE_FRAME_MISMATCH", {8, y}, {0, 0, 255});
                y += 18;
            }
            else
                for (const auto &m : r.details->decoded.measurements)
                    for (const auto &e : m.evidence_)
                    {
                        for (const auto &arc : e.original_support_arcs_)
                            for (size_t i = 1; i < arc.size(); ++i)
                                cv::line(out, arc[i - 1], arc[i], {255, 0, 255}, 1);
                        cv::circle(out, e.intersection_, 4, {255, 0, 255}, 1);
                    }
        }
        if (config.show_held_state && result.display_state)
        {
            const auto &d = *result.display_state;
            text(out,
                 (d.is_held ? "HISTORY " : "CURRENT DISPLAY ") + d.value + " source=" +
                     std::to_string(d.source_frame_id) + " age=" + std::to_string(d.age),
                 {8, y}, {255, 255, 255});
            y += 18;
        }
        if (config.draw_timing)
        {
            const auto &t = r.timings[size_t(Stage::ProcessTotal)];
            std::ostringstream s;
            s << "process_total ";
            if (t.elapsed)
                s << double(t.elapsed->count()) / 1000 << " us";
            else
                s << timingStatusName(t.status);
            text(out, s.str(), {8, y}, {255, 255, 255});
        }
        return out;
    }
} // namespace mark
