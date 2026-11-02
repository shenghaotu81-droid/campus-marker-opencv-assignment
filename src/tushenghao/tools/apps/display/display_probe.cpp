#include <opencv2/highgui.hpp>
// 旧主进程无法捕获 Qt 的 abort；探测在独立程序执行，成功后主进程才尝试窗口。
int main() {
    try {
        cv::namedWindow("marker display probe", cv::WINDOW_AUTOSIZE);
        cv::imshow("marker display probe", cv::Mat(32, 32, CV_8UC3, cv::Scalar(0, 0, 0)));
        cv::waitKey(1);
        cv::destroyWindow("marker display probe");
        return 0;
    } catch (const cv::Exception &) {
        return 1;
    }
}
