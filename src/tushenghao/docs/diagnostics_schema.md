# Block5 诊断 schema 1

记录版本 `record_schema_version=1` 与配置 `schema_version=1` 独立。旧 Block4 JSONL 只读适配，不回写或补造计时。

## 帧、范围与当前结果

`FrameRecord` 是内部实例通道，公共 `FrameResult.diagnostics` 仍为 `vector<string>`；Detector只增加私有friend，公共四调用及Detection/FrameResult/DisplayState布局不变。应用内部使用 `execute_and_record` 一次取得公开结果、诊断记录及失败异常；桥内保持 prepare→process→take；重复prepare/take、未取记录就继续process主动失败。未使用内部访问器的普通公共消费继续工作。

身份含run_id、十进制字符串frame_id/source_timestamp_us、timestamp_source及timestamp_recipe；原图/工作图尺寸为整数数组。`execution_scope` 为full/geometry/decode/temporal。geometry的result_status为null，geometry_scope_result为READY/NOT_READY/INVALID_INPUT，不把几何假设称DETECTED。audit的public_status为NOT_EVALUATED。

可选当前result含原始detections、稳定tracks和独立display；详情复用PreparedFrame、WhiteComponent、ShapeObservation、GeometryBatch、CornerMeasurement和TemporalDiagnostics。只在选中帧分配/复制，不重拟合或改写原始证据。实验网格详情的truth仅工具填写，Detector不消费。

## 计时

九个固定顺序槽：capture、preprocess、detect、decode、stabilize、process_total、visualize、wait、export。MEASURED有有限非负elapsed_us（保留小数），DISABLED/SKIPPED/NOT_IMPLEMENTED/NOT_EXECUTED值为null。未进入stage不填0。计时用steady_clock，算法timestamp由输入视频fps生成，二者不混用。

内部单次桥包围一次真正公共调用，替换内部近似total，`process_total_boundary=public_call`；没有两项相加。process包含输入/预算检查、阶段、结果及内部诊断装配；不包括prepare/take、指纹、渲染、等待和文件写。audit的process_total为NOT_EXECUTED。

`diagnostics_construct_intervals_us`只计实际包围的context.begin、事件push和context.output区间，**不是全部诊断开销**。细节复制在对应stage和process内；未单独隔离的复制成本不猜算，不把debug−baseline称精确诊断成本。框架差额只在四算法stage与total均MEASURED时计算。runner的bookkeeping计prepare/take/指纹及关联装配实测区间。

每帧导出先写frames.jsonl，Export槽仍为NOT_EXECUTED，再将实际写入耗时写到`export_timings.jsonl`，以run_id/frame_id唯一关联，汇总替换一个状态/样本，计时关闭写 DISABLED/null，无实际导出保留 NOT_EXECUTED。侧表自身不递归计时，其写入和finishRun摘要写入归run_export；`run_export.yaml`补全最终实测值，该补充报告自身写入不再递归计时。EOF失败read单列manifest，不算算法帧。

统计N、mean、median（偶数两项平均）、nearest-rank p95/p99、max；空统计null，首帧单列仍在全量，慢帧不删除。各stage分位不相加。14ms@1440×1080是用户系统目标，只对照公共process_total，不是作业下限或保证。

## 原因、整数与JSON

事件含stage、ReasonCode、detail原文、可选source_frame_id/hypothesis_id/component_id、occurrences。旧不可细分文本归OtherRecordedReason，不由字符串猜遮挡等物理原因。reset使用无分配饱和计数；外部来源null，同一语义调用不在Detector和Temporal双计。非法finalize先保留本次TemporalDiagnostics，后清三历史。

所有uint64/int64写JSON十进制字符串，small enum/尺寸为数字；float/double有限数用classic locale和17位精度。非有限写null并追加NonFiniteField事件；合法0不丢。UTF-8保留，控制字符/引号/反斜线转义。reader只供测试/验证，拒重复键、尾随内容、截断、非法转义、非有限number，支持Unicode代理对；不进入Detector。

summary不建全视频几何副本或frames文件。frame/evidence仅选中输入写详情，但所有实际算法帧都计统计。summary计数不被detail采样稀释。旧逐父affine/assignments长trace仅详细选中帧构造；准入/回退原因不删除。

offline实时字段全部null，realtime.applicability=not_applicable；不制造帧槽覆盖、曝光延迟或enqueue时钟差。纯渲染clone原图，empty/INVALID_INPUT/NOT_READY无有效图形；unknown无P标签，历史仅HISTORY/source/age文字。错帧证据先验证，由runner附事件，renderer不改const记录。

## 配置与指纹

`debug.timing_enabled` 控制默认计时；`display.show_window/show_held_state/draw_candidates` 分别写入 AppConfig.show_window 与 RenderConfig，显示值只有一份。旧 output 下的两个显示键及 debug.draw_candidates 明确拒绝。App 配置另存 level、范围、图层、模式、输出目录、导出和回放速率。boolean只接受0/1；范围配置采用非负32位整数并在原始token上检查溢出（记录frame_id仍支持uint64）。baseline拒详细/GUI/等待/证据冲突，关闭timing也拒绝。CLI覆盖路径、模式、报告用途及 Final-fixes 新开关/期待帧数，effective_config导出最终值。当前 export_video=1 在 debug 模式逐帧导出 MJPG/MP4；Block5 旧证据中的 NOT_IMPLEMENTED 只表示当时实现。

结果指纹FNV1a64版本1：初值14695981039346656037、乘1099511628211、uint64溢出。id(uint64)、timestamp(int64位模式)、status(uint32)，raw vector长度(uint64)及每项category/quality(uint32)、corners/bbox(IEEE float32)、orientation/marker_code/confidence，track vector长度/索引(uint64)/同Detection，display可选。optional先0/1；string长度uint64后UTF8；orientation四int32；marker_code为scheme/value(int32)；display为source/age(uint64)、held(1byte)、value(string)。所有整数little-endian。排除diagnostics/timing/runmeta。App文件指纹为FNV1a64+size，明确非密码学摘要，归档工具补SHA256。结果指纹仅连接summary基线，不能代替逐帧比较。

## 报告与失败

新run目录必须不存在；固定manifest.yaml/effective_config.yaml/summary.yaml，按需frames.jsonl/export_timings.jsonl/evidence。生命周期非法、文件打开/写入、零帧或提前退出非零；零帧/打开或生命周期错误写 FAILED.json；提前 EOF、用户停止及未证实覆盖写 INCOMPLETE.json，不能把不完整run当全视频PASS。CPU/系统/编译器/OpenCV/build type/线程/源代码/输入/配置/模型与时戳公式入manifest；不可获取项明确UNKNOWN。

[验收记录](block5_acceptance.md)与[证据导航](evidence/block5/INDEX.md)记录实际运行及限制。

组件详情保留工作图bbox和完整原轮廓；观测保留turns、anchor与实际L拓扑候选，hypothesis assignments始终引用本帧组件。last_reset_reason及last_reset_source_frame_id保留最后语义调用，外部来源为null。验收验证工具检查来源引用，不重新拟合。

`--run-purpose verification`只标明debug报告用途，不增加生产模式；正式baseline仍summary，验证用途debug独立使用公共process产生全帧记录，不能冒称baseline开销。

原角点详情的`physical`来自CornerEvidence自身枚举，`measurement_error_px`直接保存原error_（两线平均残差之和），不从稳定点计算。旧金样未包含error_，verify对共同证据字段exact比较，另核新增error_与原两线均值之和相等。直接Temporal的非法update也保留其详情，不以是否执行geometry决定丢弃已取得的诊断。

## 角证据完整复算协议

外层 `record_schema_version=1` 保持不变；每个角证据单独带 `evidence_schema_version=2`。
除了原有来源、连续支持弧、拟合线、交点与原始残差，还必须完整保存两条线各自的
`line_model_kinds`、`periodic_axes`、`periodic_coefficients`、
`model_mean_residual_px`、`model_max_residual_px`，每组长度固定为二。
原始点到物理线的残差与声明模型的残差分别记录，不用校正残差覆盖原字段。

工具共用 `readCornerMeasurement` 恢复字段与本帧身份，随后仍由现行 validator
从原始弧独立重拟合并核对周期参数、两类残差及几何；解码成功不等于复算通过。
无版本旧记录不能确定当时采用的线模型，明确报
`UNREPLAYABLE_CORNER_EVIDENCE`，不默认 Legacy 或用无穷/零补造遗漏字段；
历史记录保持原文，不回写。未知版本、缺失字段、非法类型/长度或非本帧来源均明确失败。

## Final-fixes 增补契约

记录 schema 仍为 1，manifest.environment 的 `final_fixes_contract="1"` 表示本轮覆盖契约。
manifest.environment 保存 expected_frames、expected_frames_source（explicit/metadata/unknown）、
frames_read、coverage（full_input/explicit_subset）、coverage_verified、completion_reason、
first_submitted_frame_id，以及显式子集 ID。
完整性以实际读取数与可靠期待数核对，不把 EOF 或 CAP_PROP_FRAME_COUNT 当作单独的成功证明。
合法子集的 submitted 仅为指定 ID，缺失 ID 失败；未知期待数不宣称完整。
旧 manifest 无新契约时只在原 metadata 具有可靠正整数帧数且读取/提交一致时接受。

compare 拒绝空输入、重复或倒序帧 ID；不提供期待数时只声明 compared_records_only。
提供 `--expected-frames N` 要求两侧全量且 ID 连续 0…N−1，才声明 full_input。
summary 的 FNV 不能替代逐帧结构对账；非法、失败、不完整或覆盖未证实的 run 不被 check-run 接受。

`complete_export(frame_id, timing, selected_record)` 只接受当前提交帧的一次真实导出完成值，
修正首帧快照及计数，分母仍是 submitted。侧表含 status、selected_record 和 elapsed_us；
视频未采样帧也写一条，详情选中标记不表示是否写视频。
计时开启写 MEASURED 与有限非负值，关闭写 DISABLED/null；重复/错帧/错选择及非法耗时主动拒绝。
frames.jsonl 先序列化，避免导出计时自引用；侧表核验后归并，侧表自身与 writer 打开/收尾归 run_export。

显示 manifest.environment 区分 display_requested、display_active 和 display_reason。
只有独立探测成功才尝试主窗口；无屏与探测失败降级并继续。
视频记录请求编码、后端、读回帧数及 FPS；中间文件 `.partial.mp4` 只有在完整性和完整读回通过后发布。
当前图层仍遵守 empty 不画旧框、unknown 不补标签、历史只显示文字的规则。

新增 expected_frame_count 为 uint64 正整数或配置 0（自动）；video_fourcc 固定 MJPG，
video_filename 固定 overlay.mp4，display_probe_timeout_ms 为正整数。
`--display`/`--export-video` 无值且只供 debug；这些 App 字段不改变公共类型、生产参数或算法。
本轮证据见[Final-fixes 导航](evidence/final-fixes/INDEX.md)。

输入路径不存在或 CLI/配置在创建 run 前被拒绝时，只保留退出码与 stderr，不保证有 run 目录；
文件存在但解码器无法打开、以及已打开却零帧时，FAILED 生命周期已实跑验证，不生成成功 summary。

运行完成判断以 `RunOutcome` 交给记录器与终端，外部耗时及结果指纹以 `RunOverhead` 交给 `finishRun`；`summary()` 只读。manifest 边界仍编码原 coverage/completion_reason 键。执行范围是算法入口的显式参数，观察请求仅保存记录属性。主链 `runGeometrySteps` 依次输出预处理、组件、形状观察、生成/验证/补全批次；旧独立探针仍可用 PreparedFrame.components_ 适配，主链不回填该兼容字段。渲染适配器将阶段快照转换为原图点线 `RenderInput`，证据检查只做一次；实验网格 JSON 由 temporal_audit 编码，检测与渲染不消费实验结构。

测试资源由目标的 `VISION_SOURCE_ROOT` 显式定位，不再按测试源文件的父目录层数猜配置根。夹具 CMake 优先链接父工程已有目标；独立构建仍由旧根装配，完整模块开关与递归导入消除留待布局搬迁。

形状观察的兼容字段 `simplification_error` 为 `null`（unknown），内部不再保存恒零伪测量。记录枚举／重置计数编码归 diagnostics_types.hpp，计时器实现仅负责时间槽；编码字符串与饱和行为保持。
