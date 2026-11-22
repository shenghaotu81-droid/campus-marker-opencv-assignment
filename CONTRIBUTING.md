# 提交流程

1. 在 GitHub 上 Fork 本仓库。
2. 在自己的 Fork 中创建个人分支，例如 `feature/<your_id>`。
3. 在 `src/` 下新建只包含个人标识的目录，例如 `src/zhangsan/`。目录名使用小写英文字母、数字或短横线，避免空格、中文和大小写混用。
4. 将 C++ 源代码、独立的 `CMakeLists.txt`、运行说明、结果材料和 `REPORT.md` 放入个人目录。
5. 将改动推送到自己的 Fork，并向本仓库的默认分支发起 Pull Request。

Pull Request 标题建议使用：`feat(<your_id>): campus marker recognition`。描述中请写明 Linux 环境和依赖版本、CMake 构建及运行命令、测试视频、必做结果和是否完成位姿估计挑战。

## Linux 约定

- 从仓库根目录执行命令；路径使用 `/`。
- 使用 C++ 和 OpenCV C++ 接口实现，通过 CMake 构建；个人目录必须包含 `CMakeLists.txt`。
- 使用 GCC 或 Clang，并在文档中注明编译器版本与 C++ 标准；通过 CMake 查找 OpenCV，避免硬编码安装路径。
- 不提交 Windows 批处理文件、盘符路径或依赖本机用户名的绝对路径。
- 文本文件使用 UTF-8 和 LF 换行；路径和文件名区分大小写。

## CMake 构建约定

从仓库根目录执行以下命令，将 `your-id` 替换为自己的目录名：

```bash
cmake -S src/your-id -B build/your-id -DCMAKE_BUILD_TYPE=Release
cmake --build build/your-id --parallel
```

个人项目应支持上述独立构建方式，无需修改仓库根目录或其他同学的文件。构建产物放在 `build/your-id/`，不要提交到 Git。仓库不提供现成的 `CMakeLists.txt`，由同学自行编写。

在个人 `README.md` 中补充实际可执行文件路径、输入视频参数和输出位置，保证运行命令可直接复现。测试视频为 `src/tushenghao/docs/homework-setup/data/raw/marker_video.avi`，标定视频为 `src/tushenghao/docs/homework-setup/data/raw/calibration_video.avi`；从仓库根目录运行时使用这些相对路径。

## 评审前自查

- 从干净的 Linux 环境按文档安装依赖，使用 CMake 编译并运行 C++ 程序；
- 结果视频或截图能看见目标框和四个关键点；
- 无目标帧不会沿用上一帧结果；
- `REPORT.md` 写明参数、失败案例和限制；
- 没有提交构建产物和无关的大文件。
