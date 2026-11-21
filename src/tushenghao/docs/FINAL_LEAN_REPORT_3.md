# 第三轮待裁决报告

## 标定来源字段

[CalibrationVideoInfo](../tools/calibration/frames/calibration_video.hpp) 仍保存输入路径，[CalibrationResult](../tools/calibration/solution/calibration_result.hpp) 只转抄该路径，摘要和 YAML 都没有消费它。同-17 明确删总帧数及结果帧 ID 列表，这条路径字段留待单独裁决。

## 历史附件

[旧瘦身报告的验证附录](lean-changes.md#验证与日志附录)提到的 `build/lean-validation-20261102` 是当时的 Git 忽略目录，在所核对的历史提交中没有此附件。若需要完整复现旧结果，需决定是否从原运行环境补归档。
