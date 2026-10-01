#include <opencv2/opencv.hpp>
#include <iostream>
#include <chrono>
#include <vector>
#include <string>

enum class PartType
{
    LARGE,
    MEDIUM,
    SMALL,
    UNKNOWN
};

struct MarkerPart
{
    double area;

    cv::Point2f center;

    cv::Rect bounding_box;

    PartType type;
}; // 模块数据结构暂定

/*  decode_marker v0.1 决策记录：
v0.1:
marker_id represents direction encoding only.

Known limitation:
large part identity depends on current sorting method.
Large rotation may break sorting.

Future:
v0.8.1 use geometric invariant / temporal tracking.
*/
int decode_marker(const std::vector<MarkerPart> &sorted_large_parts, const std::vector<MarkerPart> &candidates)
{
    // 返回一个固定的 marker_id
    if (sorted_large_parts.size() != 3)
    {
        return -1;
    }

    cv::Point2f top = sorted_large_parts[0].center;
    cv::Point2f left = sorted_large_parts[1].center;
    cv::Point2f right = sorted_large_parts[2].center;

    cv::Point2f medium_point;

    std::vector<cv::Point2f> small_points;
    cv::Point2f small_left;
    cv::Point2f small_right;

    // 标记是否找到中模块,小模块可以通过数组长度判断
    bool has_medium = false;

    for(const auto& part : candidates)
    {
        if(part.type == PartType::MEDIUM)
        {
            medium_point = part.center;
            has_medium = true;
        }
        else if(part.type == PartType::SMALL)
        {
            small_points.push_back(part.center);
        }
    }

    if(!has_medium || small_points.size() != 2)
    {
        return -1; // 如果没有中模块或者小模块数量不为2，则返回-1表示无法解码
    }

    // 完整性检查结束，数据备好，开始解码
    cv::Point2f trangle_center = (top + left + right) / 3.0f; // D = B + C - A 可留作第 4 点恢复用（草稿内）
    // cv::Point2f medium_vector = medium_point - trangle_center;
    // v0.8.1 向量坐标系用，v0.1 暂不用
    // cv::Point2f medium_vector = medium_point - triangle_center;

    double dist_top = cv::norm(medium_point - top);
    double dist_left = cv::norm(medium_point - left);
    double dist_right = cv::norm(medium_point - right);    // 欧式几何距离

    int medium_region = -1; // -1表示未知

    double margin = 0.0; // 误差容忍范围，避免跳变过于剧烈(只有当最近方向明显近至少 margin，才认为属于该区域。)

    if (dist_top + margin < dist_left &&
        dist_top + margin < dist_right)
    {
        medium_region = 0; // top
    }
    else if (dist_left + margin < dist_top &&
             dist_left + margin < dist_right)
    {
        medium_region = 1; // left
    }
    else if (dist_right + margin < dist_top &&
             dist_right + margin < dist_left)
    {
        medium_region = 2; // right
    }                  // top[0], left[1], right[2], 经过实践，采用margin保护，避免跳变过于剧烈(else保持-1)：现在其实没有用哦,加入时间滤波！

    // std::cout << "medium_region: " << medium_region << std::endl;

    // small排序（按x坐标从左到右）
    if (small_points[0].x < small_points[1].x)
    {
        small_left = small_points[0];
        small_right = small_points[1];
    }
    else
    {
        small_left = small_points[1];
        small_right = small_points[0];
    }

    // small 是在 medium 的哪个方向？
    cv::Point2f small_centor = (small_left + small_right) / 2.0f;
    bool small_above_medium = small_centor.y < medium_point.y; // 小模块在中模块上方为 true

    //信息齐了，建立 marker_id 编码规则（表）
    int marker_id = -1;      // 失败状态

    if (medium_region == 0)
    {
        marker_id = small_above_medium ? 0 : 1;
    }
    else if (medium_region == 1)
    {
        marker_id = small_above_medium ? 2 : 3;
    }
    else if (medium_region == 2)
    {
        marker_id = small_above_medium ? 4 : 5;
    }
    else
    {
        marker_id = -1; // 未知状态
    }

    return marker_id;
}

// (波动处理)
// temporal filter v0.1
// only handles short-term detection loss
// does not correct wrong ID decoding
int temporal_filter(int raw_id)
{
    static int last_valid_id = -1; // 上一帧的有效 marker_id
    static int invalid_streak = 0; // 连续无效帧计数

    const int MAX_HOLD = 5; // 连续无效帧的最大容忍数

    if(raw_id != -1)
    {
        last_valid_id = raw_id;// 更新上一帧有效 marker_id
        invalid_streak = 0;    // 重置无效帧计数
        return raw_id;         // 返回当前有效 marker_id
    }

    if(last_valid_id != -1 && invalid_streak < MAX_HOLD)
    {
        ++invalid_streak; // 增加无效帧计数
        return last_valid_id; // 返回上一帧有效 marker_id
    }

    last_valid_id = -1; // 超过容忍范围，重置上一帧有效 marker_id
    invalid_streak = 0; // 重置无效帧计数
    return -1;          // 返回无效状态
}

int main()
{
    cv::VideoCapture cap("../../../data/raw/marker_video.avi");

    if (!cap.isOpened())
    {
        std::cerr << "Error: Could not open video file." << std::endl;
        return -1;
    }

    // 获取视频信息（只需要一次）
    std::cout << "Width: "
              << cap.get(cv::CAP_PROP_FRAME_WIDTH)
              << std::endl;

    std::cout << "Height: "
              << cap.get(cv::CAP_PROP_FRAME_HEIGHT)
              << std::endl;

    std::cout << "FPS: "
              << cap.get(cv::CAP_PROP_FPS)
              << std::endl;

    std::cout << "Frames: "
              << cap.get(cv::CAP_PROP_FRAME_COUNT)
              << std::endl;

    cv::Mat frame;

    double total_time_ms = 0.0;
    int timed_frame_count = 0; // 性能检测算法
    int frame_index = 0;       // 帧索引

    while (true)
    {
        if (!cap.read(frame))
        {
            if (cap.get(cv::CAP_PROP_POS_FRAMES) >= cap.get(cv::CAP_PROP_FRAME_COUNT))
            {
                std::cout << "Video finished normally." << std::endl;
            }
            else
            {
                std::cerr << "Video reading failed." << std::endl;
            }

            break;
        } // 错误判定

        // cv::resize(frame, frame, cv::Size(960, 720)); // 调整帧大小为 960x720（还没放进计时区间）

        double timestamp_ms = cap.get(cv::CAP_PROP_POS_MSEC); // 获取当前帧的时间戳（毫秒）
        ++frame_index;

        auto start = std::chrono::high_resolution_clock::now();

        cv::resize(frame, frame, cv::Size(960, 720));

        cv::Mat gray;
        cv::Mat binary;
        cv::Mat display;

        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);

        cv::threshold(gray, binary, 200, 255, cv::THRESH_BINARY); // 二值化处理，阈值为 200，最大值为 255

        display = frame.clone(); // 克隆原始帧用于显示

        std::vector<std::vector<cv::Point>> contours;

        cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

        // if (frame_index % 30 == 0) // 每处理 30 帧输出一次轮廓数量
        // {
        /*std::cout << "Countours:"
                  << contours.size()
                  << std::endl;            // 输出轮廓数量
        */

        std::vector<MarkerPart> candidates;

        for (size_t i = 0; i < contours.size(); ++i)
        {
            double area = cv::contourArea(contours[i]);

            if (area < 80 || area > 1200)
            {
                continue; // 忽略面积过小或过大的轮廓
            }

            cv::Rect rect = cv::boundingRect(contours[i]);

            MarkerPart part;
            part.center = cv::Point2f(rect.x + rect.width / 2.0, rect.y + rect.height / 2.0);
            part.bounding_box = rect;
            part.area = area;

            if (area > 700)
            {
                part.type = PartType::LARGE;
            }
            else if (area > 250)
            {
                part.type = PartType::MEDIUM;
            }
            else if (area > 80)
            {
                part.type = PartType::SMALL;
            }
            else
            {
                part.type = PartType::UNKNOWN;
            }

            candidates.push_back(part);
        }

        /*
        std::cout << "Candidates: "
                  << candidates.size()
                  << std::endl;            // 输出候选区域数量
        */

        /*
        for(size_t i=0; i<candidates.size(); ++i)
        {
            std::cout << "Candidate " << i << ": "
                      << " Area: " << candidates[i].area
                      << ", Center: (" << candidates[i].center.x << ", " << candidates[i].center.y << ")"
                      << std::endl;
           // 打印
        }
        */
        // largepart
        std::vector<MarkerPart> large_parts;
        std::vector<MarkerPart> sorted_large_parts = large_parts; // 用于排序的大模块副本

        for (size_t i = 0; i < candidates.size(); ++i)
        {
            if (candidates[i].type == PartType::LARGE)
            {
                large_parts.push_back(candidates[i]);
            }
        } // 筛选出大模块

        /*
        if (frame_index % 30 == 0)
        {
            std::cout << "Large count: "
                      << large_parts.size()
                      << std::endl;

            for (size_t i = 0; i < large_parts.size(); ++i)
            {
                std::cout << "L" << i << ": ( "
                          << large_parts[i].center.x << ", " << large_parts[i].center.y << " )"
                          << std::endl;
            }
        }      // 每 30 帧输出一次大模块数量和位置
        */
        // 排序三个L
        if(large_parts.size() == 3)
        {
            MarkerPart top;
            std::vector<MarkerPart> bottom_parts;

            top = large_parts[0];
            for(const auto& part : large_parts)
            {
                if(part.center.y < top.center.y){
                    top = part;
                }
            }

            for(const auto& part : large_parts)
            {
                if(part.center != top.center)
                {
                    bottom_parts.push_back(part);
                }
            }

            sort(bottom_parts.begin(), bottom_parts.end(), [](const MarkerPart& a, const MarkerPart& b){
                return a.center.x < b.center.x;
            });

            sorted_large_parts.clear();                 // 清空排序后的模块列表!!

            sorted_large_parts.push_back(top);
            sorted_large_parts.push_back(bottom_parts[0]);
            sorted_large_parts.push_back(bottom_parts[1]);// 排序完成，top, left, right

            /*if (frame_index % 30 == 0)
            {
                std::cout << "TOP:"
                          << sorted_large_parts[0].center
                          << std::endl;

                std::cout << "LEFT:"
                          << sorted_large_parts[1].center
                          << std::endl;

                std::cout << "RIGHT:"
                          << sorted_large_parts[2].center
                          << std::endl;
            }    // 每 30 帧输出一次排序后的三个大模块位置
            */
        }
        
        /*
        for (size_t i = 0; i < sorted_large_parts.size(); ++i)
        {
            cv::circle(display, sorted_large_parts[i].center, 8, cv::Scalar(255, 0, 0), -1);
        }        // 绘制三个大模块的中心点
        */
        
        if(sorted_large_parts.size() == 3)
        {
            // 绘制三角形的边
           // cv::line(display, sorted_large_parts[0].center, sorted_large_parts[1].center, cv::Scalar(0, 255, 255), 2);
           // cv::line(display, sorted_large_parts[1].center, sorted_large_parts[2].center, cv::Scalar(0, 255, 255), 2);
           // cv::line(display, sorted_large_parts[2].center, sorted_large_parts[0].center, cv::Scalar(0, 255, 255), 2);
        
        // 恢复第四点
        cv::Point2f fourth_point = sorted_large_parts[0].center + sorted_large_parts[2].center - sorted_large_parts[1].center;

        cv::circle(display, fourth_point, 8, cv::Scalar(0, 0, 255), -1); // 绘制第四点
        
        // 最终整理（中心点）
        std::vector<cv::Point2f> corners(4);

        corners[0] = sorted_large_parts[0].center; // 左上
        corners[1] = fourth_point;             // 右上(注意作用域)
        corners[2] = sorted_large_parts[2].center; // 右下
        corners[3] = sorted_large_parts[1].center; // 左下

        //四点输出(如果检测到了三个基准点的话)
        for(size_t i = 0; i < corners.size(); ++i)
        {
            cv::circle(display, corners[i], 8, cv::Scalar(0, 255, 255), -1);
        }

        }

        // 加入temporal filter
        int raw_id = decode_marker(sorted_large_parts, candidates);

        int stable_id = temporal_filter(raw_id); // 更新稳定的 marker_id

        // 画id
        if(stable_id != -1)
        {
            cv::putText(display, "ID: " + std::to_string(stable_id),cv::Point(50, 80), cv::FONT_HERSHEY_SIMPLEX, 2, cv::Scalar(0, 255, 0), 3);
        }

        /*
        if(frame_index % 30 == 0)
        {
           std::cout << "marker_id: " << stable_id << std::endl;
        }  // 打印id
        */

        // small & medium part
        /* 每 30 帧输出一次中模块和小模块的位置
        if (frame_index % 30 == 0 )
        {
            std::cout << "MEDIUM:" << std::endl;

            for (const auto &part : candidates)
            {
                if (part.type == PartType::MEDIUM)
                {
                    std::cout << part.center << std::endl;
                }
            }

            std::cout << "SMALL:" << std::endl;

            for (const auto &part : candidates)
            {
                if (part.type == PartType::SMALL)
                {
                    std::cout << part.center << std::endl;
                }
            }
        }
        */

        // 在显示图像上绘制候选区域
        /*
        for (size_t i = 0; i < candidates.size(); ++i)
        {
            cv::rectangle(display, candidates[i].bounding_box, cv::Scalar(0, 255, 0), 2);

            cv::circle(display, candidates[i].center, 3, cv::Scalar(0, 0, 255), -1);

            std::string label;
            switch (candidates[i].type)
            {
            case PartType::LARGE:
                label = "L";
                break;
            case PartType::MEDIUM:
                label = "M";
                break;
            case PartType::SMALL:
                label = "S";
                break;
            default:
                label = "?";
                break;
            }

            cv::putText(display, label, candidates[i].center, cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(255, 0, 0), 2);
        }
        */
        // }// 每 30 帧处理一次候选区域

        cv::imshow("marker_video", display);

        auto end = std::chrono::high_resolution_clock::now();

        double time_ms = std::chrono::duration<double, std::milli>(end - start).count();

        total_time_ms += time_ms;
        timed_frame_count++;

        if (cv::waitKey(30) == 27)
        { // Wait for 'ESC' key press for 30 ms
            break;
        }
    }

    if (timed_frame_count > 0)
    {
        std::cout << "Timed frames: " << timed_frame_count << std::endl;
        std::cout << "Average frame time: "
                  << total_time_ms / timed_frame_count
                  << " ms"
                  << std::endl;
    }

    return 0;
}