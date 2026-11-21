// 提白块：把工作图转灰度、按阈值（200）分割成黑白图，找出所有白色块——每个块记下轮廓、外接矩形、是否碰到图边缘，包成一串 WhiteComponent 交给下一步做四边形匹配（match_quad）
// 把工作图转灰度、阈值分割，输出轮廓、边框和边界截断事实供结构匹配。
#include "geometry/geometry_observation.hpp"
#include <opencv2/imgproc.hpp>

// 修复一下编辑器的报错，让它找的到(脆)
#include "geometry_observation.hpp"

namespace mark::detector
{
    // 把工作图里的白色块找出来，每个块记下轮廓、外接矩形、是否碰边，交给下一步匹配
    std::vector<WhiteComponent> extract_white_components(const PreparedFrame &frame,
                                                         const GeometryConfig &config)
    {
        cv::Mat gray, binary; // 先造两个空图，一个放灰度图，一个放二值图
        // 把工作图（BGR 彩色）转成灰度图（后面按亮度分割，颜色信息用不上）
        // cv::cvtColor完整签名：void cv::cvtColor(InputArray src, OutputArray dst, int code, int dstCn = 0);
        // gray （灰度图）是一张单通道图，每个像素只有一个亮度值（0–255）
        // code 是颜色空间转换代码，cv::COLOR_BGR2GRAY 表示 BGR （三通道）彩色转灰度；dstCn 默认 0，由转换码自动决定输出通道数（灰度是 1 通道）
        // 把每个像素的 B、G、R 三个数按亮度权重合成一个灰度值；公式：灰度 = 0.299×R + 0.587×G + 0.114×B（绿色权重最大，因为人眼对绿最敏感）
        cv::cvtColor(frame.image_, gray, cv::COLOR_BGR2GRAY);
        // 二值化（黑白图）：亮度高于阈值（200）的像素变 255（白），其余变 0（黑）（把白色的 MARK 白片从背景里抠出来）
        // cv::threshold完整签名：double cv::threshold(InputArray src, OutputArray dst, double thresh, double maxval, int type);
        // THRESH_BINARY 的规则：像素亮度 > 200 的变成 255（纯白），≤ 200 的变成 0（纯黑）（注意是严格大于）
        // 把图切成纯黑纯白，白色区域就是 MARK 的白片；返回值是实际用的阈值
        cv::threshold(gray, binary, config.white_threshold_, 255, cv::THRESH_BINARY);

        std::vector<std::vector<cv::Point>> contours; // 造一个容器，准备装所有找出来的轮廓（每个轮廓是一串点）
        // 在黑白图里找白色区域的轮廓（每个白块的外形变成了一串点） 
        // cv::findContours完整签名：void cv::findContours(InputArray image, OutputArrayOfArrays contours, int mode, int method, Point offset = Point());
        // contours 是输出，类型是 std::vector<std::vector<cv::Point>>（一串轮廓，每个轮廓是一串 cv::Point（整数像素坐标点），围成一块白色区域的外形）
        // mode 是轮廓提取模式，cv::RETR_EXTERNAL：只取最外层轮廓（如果白块里有个黑洞，洞的轮廓不取，只要外圈）
        // method 是轮廓近似方法(轮廓点的压缩方式)，cv::CHAIN_APPROX_SIMPLE：只保留轮廓的端点，去掉中间的冗余点（比如一条直线只保留两端点）；CHAIN_APPROX_NONE：保留所有轮廓点（不压缩）
        cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

        std::vector<WhiteComponent> components; // 造一个容器，准备装最终结果（一串 WhiteComponent）
        for (const auto &contour : contours) // 对每个找出来的轮廓逐个处理
        {
            WhiteComponent component; // 给这个轮廓造一个空的白片对象
            component.contour_ = contour; // 把轮廓的点串存进去
            // 算这个轮廓的外接矩形（能框住它的最小正矩形），存进去（后面匹配时拿它快速判断大小和位置）
            // cv::boundingRect完整签名：cv::Rect cv::boundingRect(InputArray points);
            // 给一串点（一个轮廓），返回能框住这些点的最小正矩形（边和坐标轴平行，不旋转）；返回的 cv::Rect 有四个成员：x、y（左上角坐标）、width、height（宽和高）
            component.bounding_box_ = cv::boundingRect(contour); 
            // 接触工作图边缘的白片可能被裁切，不能充当完整角片。
            const auto &box = component.bounding_box_; // 短名字 box
            // 判断这个白片有没有碰到图边缘（左、上、右、下四条边任意一条碰到）；碰边的白片可能被裁切过，不完整，不能当完整的角片用，先打个标记
            // br() 是“bottom-right”（右下角）的缩写，返回矩形的右下角点，坐标是 (x + width, y + height)
            // cols 是图宽； rows 是图高
            component.touches_border_ = box.x <= 0 || box.y <= 0 ||
                                        box.br().x >= frame.image_.cols ||
                                        box.br().y >= frame.image_.rows;
            components.push_back(component); // 把这个白片放进结果列表
        }
        return components; // 把整串白片返回给调用者（run_decode_pipeline）
    }
}
