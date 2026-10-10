// Detector 模块的公共接口层组分（定义 外部怎么使用调用 Detector）
#pragma once                                                    

#include <memory>

#include "mark/detector_config.hpp"
#include "mark/detector_types.hpp"

/*
main.cpp                                                 
   |
   | include detector.hpp
   ↓
Detector 类
   |
   ↓
detector.cpp 实现

  -> detector.hpp:

        Detector
           |
           |
   ----------------
   |              |
构造配置       process()
               reset()
               config()


private:
   unique_ptr<Impl>
        |
        ↓
detector.cpp里的真实实现
*/

namespace mark
{
    class DetectorDiagnosticsAccess;
    // 定义检测器类
    class Detector
    {
    public:
        explicit Detector(DetectorConfig config);   // 构造函数，接收配置参数（防止隐式转换（单参数构造函数））

        ~Detector();       // 析构函数，释放资源（可能需要在 .cpp 里处理析构）

        // 禁止拷贝构造和赋值（复制创建和复制赋值）操作（Detector 不允许复制）
        Detector(const Detector &) = delete;

        Detector &operator=(const Detector &) = delete;

        // 处理输入帧：真实decode/稳定装配就绪后返回当前raw与单track，缺预算NOT_READY。
        FrameResult process(const FrameInput &frame);

        // 清除序列及选择/平滑/文字三历史；同尺寸换源或循环前由调用方reset(InputChanged)。
        void reset(ResetReason reason) noexcept;

        // 获取当前配置（只读访问,返回当前配置）
        const DetectorConfig &config() const noexcept;

    private:
        // 可观测性只通过内部friend访问PImpl；不添加公共调用或对象数据成员。
        friend class DetectorDiagnosticsAccess;
        // PImpl 技巧(内部实现隐藏)：将实现细节隐藏在 Impl 类中，减少头文件依赖（内部实现，外部提供接口，避免重新编译）
        /*
        detector.hpp

        外部看到：
        Detector接口


        detector.cpp

        内部：
        算法细节
        变量
        状态
        */
        struct Impl;

        std::unique_ptr<Impl> impl_;    // 独占所有权的智能指针（不能复制，Detector内部有一个Impl对象由 Detector 独占管理）
    };

} // namespace mark
