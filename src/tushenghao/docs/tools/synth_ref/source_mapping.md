# 来源映射

原件保存在 inputs，SHA-256 与文档状态见 `inputs/sources.json`。本轮用户要求按 spec 开发，授权工具实现；模型与实验批准仍独立检查。未改写四份 Markdown 原文。

| 输入及原名 | 原文位置 | 实现映射 |
|---|---|---|
| model_coordinates.md | §1 D1–D9；§2 模板；§3 全局落位 | configs/model.json 的 stroke_width/templates/pieces；每个来源行随字段保存 source；model.cpp 从 Markdown 表格核对有序坐标与锚点，以顶点长度核对边表、模板落位核对全局编号 |
| model_coordinates.md | §4 面积、§5 确认关卡 | signed_area 鞋带公式；面积不写入 model 配置；条件模型 status 保留；正式批准检查含原文与私有模型 hash |
| error_budget.md | §2 原文实验网格 | configs/grid.json rotations/ratios/widths/phases/offsets；grid.cpp 完整枚举，不宣称可检测范围 |
| error_budget.md | §3 资源提议 | cases.json 127/128/129、15/16/17、counter_limits；只作输入构造和标签，实际内部计数留空 |
| 板块2_几何原语_完整冻结文档.md → 2.md | §4.3 两关卡、§4.8 Step 4、§4.9 测试 | 独立生成工具、approval 防护、误差观测空模板；工具完成不代表 Step 4 结项 |
| synthetic_gen_spec_Oyster_reviewed.md | §2.1/2.2 | 独立 C++17 CMake、OpenCV/nlohmann_json/OpenSSL、中文注释与普通 C++ CTest |
| 同 spec | §4 工具实现约定 | 连续画布坐标、R/diag/shear、逆转置宽度、已知边偏移、8×8覆盖采样、127阈值、uint16实例掩膜、seed=0 |
| 同 spec | §5 | cases.json 与 cases.cpp 的全部64项构造；六候选只列已知锚点基底关系，独立验证标 NOT_RUN |
| 同 spec | §6 | CLI、记录、hash协议、INCOMPLETE事务与退出码；PNG压缩级别3为本工具固定编码细则 |

单位仍为 u，没有宣称毫米或加入圆角/制造公差。没有从视频选参数。独立自测坐标用工作像素单位，和 model 表格分开。数值测试中 1e-8/1e-7 等仅用于 double 恒等式检查，既不代表观测误差预算也不写成检测配置。

审批记录格式增加 `model_config_hash`，用于绑定逐项转录版本；实验联合 hash 已包含 model/grid/cases。数据协议补充 `semantic_hash`、`artifact_hashes` 和固定 PNG 压缩级别以便可复核，均为工具审计细则，不修改冻结算法契约。
