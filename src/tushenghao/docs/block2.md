# 板块2 验收记录

> 历史状态：下文保留板块 2 当时的测试清单与验收结论；无业务消费者的旧 helper 专属测试及空配置测试已在瘦身中删除。 [现行接口](dataflow-interfaces.md)。

## 1. 完成内容

Block 2 完成从视觉证据到二维几何解释的完整推理链。

完成步骤：

- Step 3（`1f1f7c9`）：几何类型定义

  完成 Block 2 核心数据结构定义：

  - `WhiteComponent`
  - `ShapeObservation`
  - `ComponentAssignment`
  - `GeometryHypothesis`
  - `GeometryBatch`

  确定几何推理阶段的数据流接口。

---

- Step 4（`e4845ed`）：MARK 几何模型 + YAML 加载

  完成 MARK 几何模型定义：

  - `GeometryPolygon`
  - `MarkerGeometry`

  支持从 YAML 加载 MARK 几何描述。

  引入 `loadMarkerGeometry()`。

---

- Step 5（`60df357`）：白片观测、L 结构识别

  完成视觉几何观测：

  - 白色区域提取
  - contour 保存
  - 多边形简化
  - 凸凹转折分析
  - L 结构候选识别

  输入：`PreparedFrame`，输出：`WhiteComponent` / `ShapeObservation`。

---

- Step 6（`835ef16`）：几何假设生成

  完成从多个几何观测生成 MARK 假设：

  - ComponentAssignment 组合
  - 模型组件匹配
  - 二维仿射拟合
  - GeometryHypothesis 构造

  输出：`GeometryBatch`。

---

- Step 7（`be84416`）：几何验证

  完成三阶段几何验证：结构验证、几何验证、约束验证。

  验证对象为 `GeometryBatch`，使用合成数据完成逻辑验证。

---

- Step 8（`849edcd`）：Detector::process() 接入完整 pipeline

  Detector 已接入 Block 2 完整流程：

  FrameInput → preprocess → PreparedFrame → extractWhiteComponents → vector WhiteComponent → observeShapes → vector ShapeObservation → generateGeometryHypotheses → GeometryBatch → validateGeometryBatch → GeometryBatch(validated)

  当前 pipeline 已运行，GeometryBatch 已生成，Detector 保持 `Status::NOT_READY`。

---

## 2. 验证方法

### 构建

执行：

    cmake --build build/tushenghao

结果：100% build success。

### 测试

执行：

    ctest --test-dir build/tushenghao --output-on-failure

测试项：

- config_contract_test
- config_error_test
- config_roundtrip_test
- detector_contract_test
- geometry_model_test
- geometry_utils_test
- geometry_matcher_test
- manual_validation_check

结果：100% tests passed。

---

## 3. 踩坑记录

### 坑1：detector_contract_test 几何路径 CWD 依赖

现象：测试运行时无法正确找到 `marker_geometry.yaml`。

排查：发现相对路径依赖运行目录，不同运行位置导致加载失败。

解决：使用 `__FILE__` 推导路径，避免测试环境依赖固定 CWD。

经验：涉及资源文件路径时，不应默认假设运行目录。

---

### 坑2：preprocess.cpp 越界实现 threshold

现象：Block 1 preprocess 中提前加入 threshold 逻辑，导致职责边界混乱。

排查：发现 threshold 属于 Step 5 白片观测阶段，Block 1 只负责预处理。

解决：删除 preprocess.cpp 中 threshold 处理，交还 Step 5。

经验：严格保持 Block 边界，避免提前实现后续模块逻辑。

---

### 坑3：testProcess 空图输入

现象：Detector process 测试使用空输入导致处理异常。

排查：发现 pipeline 已依赖有效图像尺寸。

解决：补充 `cv::Mat(720,960,CV_8UC3)` 作为测试输入。

经验：接口测试需要满足最低有效输入条件。

---

## 4. 已知限制

- Step 4 正式合成数据流程被 `approval.json` 阻塞，目前只使用 mock 数据完成验证。
- `marker_geometry_path_` 使用相对路径，依赖当前工作目录。当前要求 `cd src/tushenghao` 后运行，后续需要统一资源路径管理。
- `GeometryBatch::diagnostics_` 当前只存在于局部 batch 生命周期内，外部无法长期获取诊断信息，后续由 Block 5 observability 解决。
- 当前参数仍为占位值（threshold、epsilon、validation residual、area ratio），后续使用真实数据调优。
- `Detector::process()` 当前仍返回 `Status::NOT_READY`，Block 3 完成后再输出最终 Detection。
