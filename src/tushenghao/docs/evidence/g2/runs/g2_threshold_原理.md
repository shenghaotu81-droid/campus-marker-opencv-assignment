# G2 阈值脚本原理

## 干啥的

把 `pose_sensitivity` 的合成数据误差分布，变成 `pose.yaml` 里能用的阈值。

## 原理：合成数据三倍标准差法

- `pose_sensitivity` 跑了 3456 组合成数据，每组都知道真值，能算出每组的 `rmse_px`、`max_error_px`、`quad_area_px2`
- 假设误差近似正态分布，μ±3σ 覆盖 99.7% 的情况
- `max_rmse_px` 取 μ+3σ：比这个还大的 RMSE 就是异常，拒掉
- `max_corner_error_px` 取 μ+3σ：同理，单角点最大误差超了就拒
- `min_quad_area_px2` 取 μ−3σ：比这个还小的四边形就是太小了，拒掉（负数截到 0）

## 运行逻辑

1. `sys.argv[1]` 拿 CSV 路径
2. `pandas.read_csv` 读进来
3. 对三列分别 `dropna()` → `mean()` → `std()` → 算 μ±3σ
4. 打印成 YAML 格式，直接抄进 `pose.yaml`

## 四个阈值

| 字段 | 来源 | 公式 |
|------|------|------|
| `max_rmse_px` | `rmse_px` | μ + 3σ |
| `max_corner_error_px` | `max_error_px` | μ + 3σ |
| `min_quad_area_px2` | `quad_area_px2` | μ − 3σ（截断 ≥0） |
| `max_time_uncertainty_s` | 硬件实测 | 先空着 |
