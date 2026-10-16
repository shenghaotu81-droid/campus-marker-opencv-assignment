#!/usr/bin/env python3
"""
G2 阈值标定脚本：从 pose_sensitivity 的 candidate_errors.csv 计算 3σ 阈值

输入：runs/g2-sensitivity-002/candidate_errors.csv（8 列版本）
输出：打印可填入 pose.yaml budgets 的三个阈值

用法：
    python3 g2_threshold_script.py <candidate_errors.csv 路径>

阈值规则（合成数据三倍标准差法）：
- max_rmse_px: rmse_px 的 μ+3σ
- max_corner_error_px: max_error_px 的 μ+3σ
- min_quad_area_px2: quad_area_px2 的 μ-3σ
- max_time_uncertainty_s: 等硬件实测，先空着
"""

import sys
import pandas as pd


def main():
    if len(sys.argv) != 2:
        print(f"用法: {sys.argv[0]} <candidate_errors.csv 路径>")
        sys.exit(1)

    csv_path = sys.argv[1]
    df = pd.read_csv(csv_path)

    print(f"读取 {len(df)} 行数据")
    print()

    # 1. max_rmse_px = rmse_px 的 μ+3σ
    col = 'rmse_px'
    s = df[col].dropna()
    mu, sigma = s.mean(), s.std()
    v1 = mu + 3 * sigma
    print(f"[max_rmse_px]")
    print(f"  {col}: μ={mu:.4f}, σ={sigma:.4f}, μ+3σ={v1:.4f}")

    # 2. max_corner_error_px = max_error_px 的 μ+3σ
    col = 'max_error_px'
    s = df[col].dropna()
    mu, sigma = s.mean(), s.std()
    v2 = mu + 3 * sigma
    print(f"[max_corner_error_px]")
    print(f"  {col}: μ={mu:.4f}, σ={sigma:.4f}, μ+3σ={v2:.4f}")

    # 3. min_quad_area_px2 = quad_area_px2 的 μ-3σ
    col = 'quad_area_px2'
    s = df[col].dropna()
    mu, sigma = s.mean(), s.std()
    v3 = mu - 3 * sigma
    # 面积不能为负，截断到 0
    v3 = max(v3, 0.0)
    print(f"[min_quad_area_px2]")
    print(f"  {col}: μ={mu:.2f}, σ={sigma:.2f}, μ-3σ={v3:.2f}")

    print()
    print("填入 src/tushenghao/config/pose.yaml：")
    print(f'  max_rmse_px: "{v1:.4f}"')
    print(f'  max_corner_error_px: "{v2:.4f}"')
    print(f'  min_quad_area_px2: "{v3:.2f}"')
    print(f'  max_time_uncertainty_s: ""  # 等硬件实测')


if __name__ == '__main__':
    main()
