# G2 阈值标定脚本使用说明

## 脚本位置
`g2_threshold_script.py`（与本 README 同目录，建议归档到 `docs/evidence/g2/`）

## 用途
从 `pose_sensitivity` 生成的 `candidate_errors.csv` 按"合成数据三倍标准差法"计算 `pose.yaml` budgets 的四个阈值。

## 使用方法

```bash
python3 g2_threshold_script.py /path/to/candidate_errors.csv
```

示例（在仓库根目录）：
```bash
python3 src/tushenghao/docs/evidence/g2/g2_threshold_script.py runs/g2-sensitivity-002/candidate_errors.csv
python3 src/tushenghao/docs/evidence/g2/runs/g2_threshold_script.py src/tushenghao/docs/evidence/g2/runs/g2-sensitivity-002/candidate_errors.csv
```

需要安装 pandas：
```bash
pip install pandas
pip install pandas --break-system-packages
```

## 输入文件

`runs/g2-sensitivity-002/candidate_errors.csv`（**8 列版本**），表头：
```
sample_id,candidate_index,rmse_px,min_depth_m,translation_error_m,rotation_error_rad,max_error_px,quad_area_px2
```

> ⚠️ 旧版 6 列 CSV（`g2-sensitivity-001`）没有 `max_error_px` 和 `quad_area_px2`，不能用。
> 必须用修复后的 `pose_sensitivity`（见下方"为什么修了 pose_sensitivity.cpp"）重跑生成 8 列版本。

## 四个阈值

| pose.yaml 字段 | 数据列 | 公式 | 状态 |
|---|---|---|---|
| `max_rmse_px` | `rmse_px` | μ + 3σ | ✅ 脚本自动计算 |
| `max_corner_error_px` | `max_error_px` | μ + 3σ | ✅ 脚本自动计算 |
| `min_quad_area_px2` | `quad_area_px2` | μ − 3σ（截断到 ≥0） | ✅ 脚本自动计算 |
| `max_time_uncertainty_s` | — | 硬件实测 | ⏸️ 先空着，等硬件 |

## 填入 pose.yaml

把脚本输出的值填入 `src/tushenghao/config/pose.yaml`：

```yaml
budgets:
  max_rmse_px: "X.XXXX"            # 脚本输出
  max_corner_error_px: "X.XXXX"    # 脚本输出
  min_quad_area_px2: "X.XX"        # 脚本输出
  max_time_uncertainty_s: ""       # 等硬件实测，先空着
```

注意：值用字符串格式（带引号），空字符串表示未标定。

## 为什么修了 pose_sensitivity.cpp

G2.md 里的 `pose_sensitivity.cpp` 写 CSV 时漏了 `max_error_px` 和 `quad_area_px2` 两列，
导致 `max_corner_error_px` 和 `min_quad_area_px2` 无法从独立数据标定。

修复（三处）：
1. 加 `#include "../../lib/pose/pose_geometry.hpp"`（为了 `detail::quad_area`）
2. 表头加 `,max_error_px,quad_area_px2`
3. 数据行加 `candidate.max_error_px_` 和 `mark::pose::detail::quad_area(observed)`

重编、重跑 `pose_sensitivity` 后 CSV 即为 8 列版本。
