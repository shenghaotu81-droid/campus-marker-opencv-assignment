// 预处理：把输入帧备好——原图留一份只读视图，再把图缩放到工作尺寸（960×720），顺手算好横向、纵向两个缩放比，包成 PreparedFrame 交给后面用
#include "preprocess/preprocess.hpp"
#include <opencv2/imgproc.hpp>

// 编辑器报错找不到头文件，修复一下路径
#include "preprocess.hpp"

namespace mark::detector
{
    // 保存原图只读视图和帧上下文，生成工作图及像素中心坐标映射供结构匹配使用。
    PreparedFrame preprocess(const FrameInput &frame, const PreprocessConfig &config)
    {
        PreparedFrame prepared; // 先造一个空的 PreparedFrame，下面逐项填
        prepared.original_image_ = frame.image_; // 把原图存进来;Mat 赋值是浅拷贝（只拷头部、不拷像素，两个 Mat 共享同一块像素内存），只留个只读视图、不复制像素（后面角点拟合要在原图上做，像素共享就行）
        prepared.original_white_threshold_ = config.threshold_; // 把配置里的二值化阈值（200）存进来，后面提白块要用
        prepared.frame_id_ = frame.frame_id_; // 把帧号抄过来，保持帧身份
        prepared.timestamp_us_ = frame.timestamp_us_; // 把时间戳抄过来，保持帧身份
        // cv::resize(...)：把原图缩放到工作尺寸 960×720(now)，结果放进 prepared.image_;这个缩过的图就是后面检测真正用的工作图
        // 完整签名：void cv::resize(InputArray src, OutputArray dst, Size dsize, double fx = 0, double fy = 0, int interpolation = INTER_LINEAR);
        // cv::Size 是 （宽，高）；目标尺寸
        // fx、fy 是缩放倍数，是另一种（二选一）指定输出尺寸的办法（fx = 横向倍数（输出宽 = 原图宽 × fx），fy = 纵向倍数（输出高 = 原图高 × fy））
        // interpolation 默认是 INTER_LINEAR（双线性插值，缩放时平滑地取像素，不是最近邻那种锯齿）
        cv::resize(frame.image_, prepared.image_, {config.work_width_, config.work_height_});
        // 算横向缩放比 = 工作图宽 / 原图宽;算纵向缩放比 = 工作图高 / 原图高
        // 后面把工作图上的坐标换算回原图坐标时用
        // cv::Mat 的两个公开成员变量：cols 是列数（图的宽，单位像素），rows 是行数（图的高）
        prepared.scale_x_ = static_cast<double>(config.work_width_) / frame.image_.cols;
        prepared.scale_y_ = static_cast<double>(config.work_height_) / frame.image_.rows;
        // 把填好的 PreparedFrame 返回给调用者（run_decode_pipeline）
        return prepared;
    }
}
