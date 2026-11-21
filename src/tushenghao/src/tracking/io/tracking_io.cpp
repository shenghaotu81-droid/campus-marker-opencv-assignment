#include "io/tracking_io.hpp"
#include "vision/pose/pose_io.hpp"

namespace mark::tracking
{
    // 把一帧的 TrackingResult 按 YAML 结构写进 tracks.yaml——外层一个 map，里面是 stamp、input_accepted、events、（可选的）association_nis、tracks 列表，每条 track 又是一个 map
    void write_tracking_result(cv::FileStorage &fs, const TrackingResult &r)
    {
        // 开 map + 写 stamp：fs << "{" 打开一个 map，"stamp" 是 key，后面 write_capture_stamp（pose 模块的写 stamp 函数）把这帧的 CaptureStamp 写进去
        fs << "{" << "stamp";
        pose::write_capture_stamp(fs, r.input_stamp_);

        // input_accepted + events：input_accepted 是 bool，这里转成 int 写（0/1）；events 是个序列：fs << "["打开，循环里每个事件字符串依次写进去，fs << "]" 关上
        fs << "input_accepted" << int(r.input_accepted_) << "events" << "[";
        for (const auto &e : r.events_)
            fs << e;
        fs << "]";

        // association_nis（可选）：association_nis_ 是 optional<double>，有值才写，没值（比如这帧没量测、没做 NIS 挑选）就不写这个 key（* 是解 optional 取值）
        if (r.association_nis_)
            fs << "association_nis" << *r.association_nis_;

        // tracks 列表
        /* 每条 track 一个 map：
          track_id、state_time_us_ (微秒时间戳（int64）：这帧的时间，单位 µs)用 std::to_string 转成字符串写（因为 OpenCV 4.6 的 FileStorage 写 int64 不完整，所以这里用字符串绕过去(库缺陷)）
          state 用 track_state_name（"MEASURED" / "COASTING"）
          last_measurement_stamp 再调一次 write_capture_stamp
          position_m、covariance 用 cv::Mat(...) 包一下再写（FileStorage 写 Mat/Matx 用的桥接（定长类型要走 FileStorage 的 Mat 写入通道），同前（FileStorage 能写标量（int、double、string）、序列（[ ]）、map（{ }），Mat等））
          velocity_mps_ 也是 optional，只有确认的轨迹才有速度，所以 if 有值才写
           最后 fs << "]" 关 tracks 序列，fs << "}" 关外层 map，一帧写完
        */
        /* 示例：
       tracks:
  - track_id: "1"
    state: MEASURED
    state_time_us: "1700000000000000"
    last_measurement_stamp:
      frame_id: 42
      received_us: 1700000000000000
    position_m: !!opencv-matrix
      rows: 3
      cols: 1
      dt: d
      data: [0.1, 0.2, 1.5]
    velocity_mps: !!opencv-matrix
      rows: 3
      cols: 1
      dt: d
      data: [0.5, 0.0, 0.0]
    covariance: !!opencv-matrix
      rows: 6
      cols: 6
      dt: d
      data: [ ... 36 个数，按行排 ... ]

      注释：!!opencv-matrix 是 OpenCV 的 FileStorage 标签，dt: d 表示 double，data 是按行排开的数值
            track_id 和 state_time_us 是字符串（带引号），是前面 to_string 的结果
       */
        fs << "tracks" << "[";
        for (const auto &t : r.tracks_)
        {
            fs << "{" << "track_id" << std::to_string(t.track_id_) << "state"
               << track_state_name(t.state_) << "state_time_us" << std::to_string(t.state_time_us_)
               << "last_measurement_stamp";
            pose::write_capture_stamp(fs, t.last_measurement_stamp_);
            fs << "position_m" << cv::Mat(t.position_m_);
            if (t.velocity_mps_)
                fs << "velocity_mps" << cv::Mat(*t.velocity_mps_);
            fs << "covariance" << cv::Mat(t.covariance_);
            fs << "}";
        }
        fs << "]" << "}";
    }

}
