// 把同帧、同身份的合法名义位置转为量测，基础噪声叠加位置分歧矩阵。
#include "measurement/measurement_adapter.hpp"
#include "filter/kalman_cv.hpp"
#include <set>

namespace mark::tracking::detail
{
    // 返回这一帧量测的有效时刻（微秒，int64_t）
    // 滤波器要的是“量测发生在什么时候”，不是“帧什么时候到”
    // 真正的曝光时刻，比收到时间准（收到还包含了传输和处理的延迟）
    int64_t sample_time(const pose::CaptureStamp &s)
    {
        // exposure_us_ 有值就用它——曝光是照片真正拍下的那一刻，是量测最准的时间
        // 没有就退回 received_us_——帧收到的时刻(离线：帧号推导出来的名义时间)
        // std::optional::value_or：template<class U = T> constexpr T value_or(U&& default_value) const&; 作用：optional 有值就返回那个值，没值就返回传进来的默认值。按值返回（拷一份）
        // T 是 optional 里装的值的类型，这里是 int64_t。U 是默认值类型，这里也是 int64_t（推导出来的）
        // value_or 是“有值取值、没值取默认”，不抛异常、不改 optional；跟 value()（空的时候抛 bad_optional_access）和 operator*（空的时候是未定义行为）不一样——它就是专门用来安全地取个兜底值的
        return s.exposure_us_.value_or(s.received_us_);
    }

    // 判断两个 CaptureStamp 是不是同一次采样（同一帧）
    // 8 个字段全相等才返回 true：session、相机、时钟域、像素域、帧号、收到时间、曝光时间、时间不确定度
    bool same_sample(const pose::CaptureStamp &a, const pose::CaptureStamp &b)
    {
        return a.session_id_ == b.session_id_ && a.camera_id_ == b.camera_id_ &&
               a.clock_domain_ == b.clock_domain_ && a.pixel_domain_ == b.pixel_domain_ &&
               a.frame_id_ == b.frame_id_ && a.received_us_ == b.received_us_ &&
               a.exposure_us_ == b.exposure_us_ && a.time_uncertainty_s_ == b.time_uncertainty_s_;
    }

    // 待删
    // 用当前速度估计 v 和速度协方差 vv，把时间不确定度 b 放大 R（位置噪声协方差矩阵）；胀大
    cv::Matx33d time_augmented_noise(const cv::Matx33d &r, const cv::Vec3d &v,
                                     const cv::Matx33d &vv, double b, double q)
    {
        if (b == 0)
            return r;
        return 2 * (r + b * b * 2 * (vv + v * v.t()) + cv::Matx33d::eye() * (q * b * b * b / 3));
    }

    // 翻译：把这帧的位姿观测（PoseEstimate）筛一遍，转成卡尔曼滤波器能吃的位置量测（PositionMeasurement：位置 t_m_ + 噪声 R）
    std::vector<PositionMeasurement> adapt_measurements(const TrackingFrame &f,
                                                        const TrackingConfig &c)
    {
        std::vector<PositionMeasurement> out;
        std::set<size_t> seen, duplicates;
        // 第一遍找重复：扫一遍观测，把符合 source/target/有估计的那些的 detection_index_ 记下来
        for (const auto &p : f.observations_)
            if (p.source_ == c.source_ && p.target_id_ == c.target_class_ && p.estimate_)
                // std::set::insert：std::pair<iterator, bool> insert(const value_type& val);
                // 往 set 里插一个值，返回一个 pair：.first 是指向该元素的迭代器，.second 是 bool——true 表示插进去了（原来没有），false 表示已经存在（没插）
                if (!seen.insert(p.detection_index_).second)
                    // 同一个 detection_index_ 出现两次，说明同一帧里同一个检测被重复报了，两次都记进 duplicates——第二遍里这两个都会被丢掉（同一个东西报两次，宁可都不要）
                    duplicates.insert(p.detection_index_);

        // 第二遍筛选 + 组装：每个观测过五道关，任何一道不过就 continue 跳过
        for (const auto &p : f.observations_)
        {
            // source_ 必须是 "MARK"（TAG 的观测在这里就被滤掉了，不进卡尔曼）
            // target_id_ 必须等于 target_class_（待删，冗余比对）
            // 不能是第一遍找出来的重复项
            // same_sample：观测的 stamp 必须和帧的 stamp 是同一采样（待删，冗余比对）
            // std::set::count：size_type count(const key_type& key) const;
            // 查一个值在不在 set 里，在返回 1，不在返回 0；set 里元素唯一，所以 count 只有 0 或 1 两种结果
            if (p.source_ != c.source_ || p.target_id_ != c.target_class_ ||
                duplicates.count(p.detection_index_) || !same_sample(p.stamp_, f.stamp_))
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
