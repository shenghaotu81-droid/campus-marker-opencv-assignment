#!/usr/bin/env python3
"""用户明确授权713实拍组四角统计：P99+1px，不改变合成统计口径或把不可测角写0。"""
import hashlib
import json
import statistics
import sys
from collections import Counter
from pathlib import Path
from summarize import quantile


def describe(corners):
    """分母包含每个尝试角；缺支持/冲突不属于有限误差，保持null并报告其原因。"""
    values = [c['error_px'] for c in corners if c['success']]
    result = {'attempted': len(corners), 'measured': len(values), 'unmeasurable': len(corners)-len(values),
              'failure_reasons': dict(Counter(c['reason'].split(':')[0] for c in corners if not c['success']))}
    result.update(mean=None, sample_std=None, p50=None, p95=None, p99=None, maximum=None)
    if values:
        result.update(mean=statistics.mean(values), sample_std=statistics.stdev(values) if len(values)>1 else None,
                      p50=quantile(values,.5), p95=quantile(values,.95), p99=quantile(values,.99), maximum=max(values))
    return result


def display_stat(summary, field):
    """V05 报告适配：缺测和样本方差未定义时显示 N/A，不伪装为零误差。"""
    value = summary[field]
    return 'N/A' if value is None else f'{value:.9g}'


def main():
    baseline_path, expanded_path, report = map(Path,sys.argv[1:])
    baseline = [json.loads(l) for l in baseline_path.read_text().splitlines()]
    expanded = [json.loads(l) for l in expanded_path.read_text().splitlines()]
    assert len(baseline)==len(expanded)==713
    assert {r['frame_id'] for r in baseline}=={r['frame_id'] for r in expanded}
    assert all(len(r['corners'])==4 for r in baseline+expanded)
    original = {r['frame_id']:r['parent'] for r in baseline}
    assert all(r['parent']==original[r['frame_id']] for r in expanded)
    summary = {'synthetic_C_original_budget_px':1.5,'user_method':'nearest-rank P99 + 1 original px',
               'frame_denominator':713,'corner_denominator':2852,'input_sha256':{},'datasets':{}}
    for name, records, path in [('baseline',baseline,baseline_path),('expanded',expanded,expanded_path)]:
        summary['input_sha256'][name] = hashlib.sha256(path.read_bytes()).hexdigest()
        summary['datasets'][name] = {'all_corners':describe([c for r in records for c in r['corners']]),
            'by_physical':[describe([r['corners'][p] for r in records]) for p in range(4)],
            'four_measurable_frames':sum(all(c['success'] for c in r['corners']) for r in records)}
    # 配置消费每角误差；取四个物理角P99的最大值，避免高误差角被低误差角稀释。
    physical = summary['datasets']['expanded']['by_physical']
    # 统计候选来自四个物理角的分布；缺角或不足以计算样本标准差时仅报告已测事实。
    p99 = max(s['p99'] for s in physical) if all(s['sample_std'] is not None for s in physical) else None
    summary['selected_p99_px'] = p99
    summary['new_max_corner_error_px'] = p99+1 if p99 is not None else None
    if p99 is None:
        summary['budget_unavailable_reason'] = 'INSUFFICIENT_PHYSICAL_CORNER_DISTRIBUTION'
    summary['quantile_population'] = 'max of four physical-corner P99; finite measurable errors only; nulls explicitly retained'
    report.with_suffix('.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n')
    lines=['# 713实拍组四角误差与预算重定','',
           '用户明确批准本轮实拍重定：nearest-rank P99+1原图px；合成C原统计方法和1.5px历史预算保留。',
           '固定原视频v2的713完整补全帧、2852个角；扩大弧前后用同一旧生产者矩阵和六片assignment，不从视频构造真值父、不丢首失败后的角。',
           '仅解除待求max_corner_error上限；其他拟合、关联、延伸、连接及病态预算仍冻结。误差为交点到实际转折弧距离，不是人工真值定位误差。',
           '', '|版本|角|尝试|可测|不可测|均值|样本std|P99|最大值|','|---|---|---:|---:|---:|---:|---:|---:|---:|']
    for name in ('baseline','expanded'):
        data=summary['datasets'][name]
        for label,s in [('全部',data['all_corners'])]+[(f'P{i}',v) for i,v in enumerate(data['by_physical'])]:
            lines.append(f'|{name}|{label}|{s["attempted"]}|{s["measured"]}|{s["unmeasurable"]}|{display_stat(s,"mean")}|{display_stat(s,"sample_std")}|{display_stat(s,"p99")}|{display_stat(s,"maximum")}|')
    candidate = (f'新生产max_corner_error = max(P0..P3的P99) + 1 = {p99:.17g} + 1 = **{p99+1:.17g}原图px**。保留double原值，不套原C的0.5px取整，符合本轮用户方法。'
                 if p99 is not None else '候选预算：N/A。至少一个物理角缺少有效分布；保留不可测原因，不从其他角补齐预算。')
    lines+=['',candidate,
            '不可测角保留null及拒绝原因，不计算伪误差、不填0；这个预算只覆盖已获得合法支持的误差分布，不声称解决缺边/不可信父或整视频召回。',
            '四角全可测帧数和不可测原因见同名JSON；原始逐角记录在build，摘要记录其SHA-256，删除build前须归档。']
    report.write_text('\n'.join(lines)+'\n')
    print(json.dumps(summary,ensure_ascii=False))


if __name__=='__main__':
    main()
