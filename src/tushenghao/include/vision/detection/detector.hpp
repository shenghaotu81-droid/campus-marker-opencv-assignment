#pragma once
#include <memory>
#include "vision/config/detector_config.hpp"
#include "vision/types/detector_types.hpp"

namespace mark::detector
{
    class Detector
    {
      public:
        // 构造函数，按值拿一份配置；explicit 防隐式转换
        explicit Detector(DetectorConfig);
        // 析构函数；声明在头文件里，定义在 cpp 里，避免 Impl 的定义泄露到头文件（unique_ptr<Impl> 析构时要 delete Impl，必须在 Impl 完整定义的地方才能做）
        ~Detector();
        // 禁止拷贝构造和拷贝赋值。Detector 独占一个 Impl，不能复制，也顺带禁了移动（“一处拥有、不可复制”的对象）
        Detector(const Detector &) = delete;
        Detector &operator=(const Detector &) = delete;
        // 主入口，每帧调一次
        // 输入 FrameInput（图像 + 帧号 + 时间戳），返回 FrameResult（帧号 + 时间戳 + 状态 + 检到的角点列表 + 诊断信息）
        // 状态有三种：INVALID_INPUT（输入非法）、NOT_DETECTED（没找到）、DETECTED（找到了）
        FrameResult process(const FrameInput &);
        // 重置内部状态（noexcept保证不抛错）
        void reset() noexcept;

      private:
        // pimpl 手法（pointer to implementation）
        // 前向声明 + std::unique_ptr<Impl> impl_
        // 头文件只露一个指针，真正的实现藏在 detector.cpp 里
        // 好处：改内部实现不用重新编译所有包含这个头文件的地方（编译防火墙）；代价：多一次堆分配和一层指针间接
        struct Impl;  // 保存配置和上一帧的帧号及时间戳，用于检查帧序。
        std::unique_ptr<Impl> impl_; // 构造函数体：impl_ = std::make_unique<Impl>() 先在堆上建 Impl，再 impl_->config_ = std::move(config_) 把配置移进去
    };
}
