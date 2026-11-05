# 校内赛标志物识别：传统视觉 OpenCV 作业

本仓库是假期作业的统一提交空间。每位同学从仓库派生（Fork）自己的副本，在 `src/` 下创建个人目录，完成基于传统视觉和 OpenCV 的校内赛标志物识别，并通过 Pull Request 提交回本仓库。

作业要求、验收标准、素材说明和提交流程见 [ASSIGNMENT.md](ASSIGNMENT.md)。

统一使用 **C++ 和 OpenCV C++ 接口**实现，使用 **CMake** 配置和编译，在 **Linux** 环境下验收。每位同学在个人目录中提供独立的 `CMakeLists.txt`，构建方式见 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 仓库结构

```text
.
├── ASSIGNMENT.md
├── CONTRIBUTING.md
├── LICENSE
├── data/
│   ├── README.md
│   ├── raw/
│   └── camera/
├── src/
│   └── tushenghao/
│       └── docs/          # 规则、报告、素材与验证记录
└── submissions/
```

仓库脚本和文档以 Linux/POSIX 环境为准，使用 UTF-8、LF 换行和大小写敏感的路径。仓库只提供项目空间与数据资料，不预置识别算法代码。
