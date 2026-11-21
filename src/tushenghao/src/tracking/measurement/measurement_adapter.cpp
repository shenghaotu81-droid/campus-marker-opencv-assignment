// 把同帧、同身份的合法名义位置转为量测，基础噪声叠加位置分歧矩阵。
#include "measurement/measurement_adapter.hpp"
#include "filter/kalman_cv.hpp"
#include <set>

namespace mark::tracking::detail
{
    // 返回这一帧量测的有效时刻（微秒，int64_t）
    // 滤波器要的是“量测发生在什么时候”，不是“帧什么时候到”
    int64_t sample_time(const pose::CaptureStamp &s)
    {
        // 没有就退回 received_us_——帧收到的时刻(离线：帧号推导出来的名义时间)
        return s.received_us_;
    }

    // 翻译：把这帧的位姿观测（PoseEstimate）筛一遍，转成卡尔曼滤波器能吃的位置量测（PositionMeasurement：位置 t_m_ + 噪声 R）
    std::vector<PositionMeasurement> adapt_measurements(const TrackingFrame &f,
                                                        const TrackingConfig &c)
    {
        std::vector<PositionMeasurement> out;
        std::set<size_t> seen, duplicates;
        // 第一遍找重复：扫一遍观测，把符合 source/target/有估计的那些的 detection_index_ 记下来
        for (const auto &p : f.observations_)
            if (p.source_ == c.source_ && p.estimate_)
                // std::set::insert：std::pair<iterator, bool> insert(const value_type& val);
                // 往 set 里插一个值，返回一个 pair：.first 是指向该元素的迭代器，.second 是 bool——true 表示插进去了（原来没有），false 表示已经存在（没插）
                if (!seen.insert(p.detection_index_).second)
                    // 同一个 detection_index_ 出现两次，说明同一帧里同一个检测被重复报了，两次都记进 duplicates——第二遍里这两个都会被丢掉（同一个东西报两次，宁可都不要）
                    duplicates.insert(p.detection_index_);

        // 第二遍筛选 + 组装：每个观测过五道关，任何一道不过就 continue 跳过
        for (const auto &p : f.observations_)
        {
            // source_ 必须是 "MARK"（TAG 的观测在这里就被滤掉了，不进卡尔曼）
            // 不能是第一遍找出来的重复项
            // std::set::count：size_type count(const key_type& key) const;
            // 查一个值在不在 set 里，在返回 1，不在返回 0；set 里元素唯一，所以 count 只有 0 或 1 两种结果
            if (p.source_ != c.source_ || duplicates.count(p.detection_index_))
                continue;
            const auto &measurement = p.estimate_;
            // estimate_ 必须存在，t_m_ 必须有限，t_m_[2] > 0（MARK 必须在相机前方，深度为正），translation_spread_m2_（S）必须有限
            if (!measurement || !finite_vector(measurement->pose_.t_m_) ||
                measurement->pose_.t_m_[2] <= 0 ||
                !cv::checkRange(cv::Mat(measurement->translation_spread_m2_)))
                continue;
            // 过关的就组装成量测推入 out：位置取 t_m_，R 取 camera_mse_m2_ + translation_spread_m2_——也就是 YAML 里的 R 加上 检测块 算出来的 S（待删）
            out.push_back({measurement->pose_.t_m_,
                           c.profile_->camera_mse_m2_ + measurement->translation_spread_m2_});
        }
        // 最后 return out;advance 拿到的就是这帧所有合格的 MARK 位置量测（通常就 1 个）
        return out;
    }

}
