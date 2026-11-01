#!/usr/bin/env python3
"""只读C原始证据；关联/assignment预算共用既有统计公式，输出候选及所有结构失败。"""
import csv
import json
import sys
from pathlib import Path
from summarize import distribution

# V05：列由测量协议决定；没有可用分布时也输出同一表头，不从首行数据猜列。
STAT_FIELDS = ('n', 'mean', 'sample_std', 'percentile', 'maximum', 'minimum', 'raw', 'rounded', 'step')
CSV_FIELDS = ('work_width', 'radius', 'noisy', 'metric', 'total', 'failures',
              *STAT_FIELDS, 'worst_case')


def main():
    source, report = map(Path, sys.argv[1:])
    records = [json.loads(line) for line in source.read_text().splitlines()]
    assert len(records) == 720 and all(r['profile'] == 'C' for r in records)
    strata, failed = {}, []
    for r in records:
        key = (r['work_size'][0], r['radius_original_px'], bool(r['seed']))
        group = strata.setdefault(key, {'total': 0, 'metrics': {}})
        group['total'] += 1
        if not r['success']:
            failed.append((r['measurement_case_id'], r['reason']))
            continue
        c = r['corners']
        values = {
            'edge_position_original_px': max(v for a in c for v in a['edge_position_original_px']),
            'component_mapping_original_px': r['component_mapping_original_px'],
            'turn_connection_length_original_px': max(a['turn_connection_length_original_px'] for a in c),
            'min_support_span_original_px': min(v for a in c for v in a['support_span_original_px']),
        }
        if 'producer_truth_assignment_count' in r:
            if r['producer_truth_assignment_count']:
                values['producer_edge_position_original_px'] = r['producer_edge_position_original_px']
                values['producer_edge_direction_deg'] = r['producer_edge_direction_deg']
                if 'producer_assignment_boundary_work_px' in r:
                    values['producer_assignment_boundary_work_px'] = r['producer_assignment_boundary_work_px']
                    values['producer_assignment_direction_deg'] = r['producer_assignment_direction_deg']
            else:
                failed.append((r['measurement_case_id'], 'NO_CORRECT_TRUTH_THREE_L_PARENT'))
        matches = r['assignment_metrics']
        invalid = [a for a in matches if not a['valid'] or not a['topology_valid']]
        if invalid:
            failed.append((r['measurement_case_id'], 'ASSIGNMENT_TOPOLOGY:' + ','.join(a['part'] for a in invalid)))
        else:
            values.update({
                'assignment_boundary_work_px': max(a['boundary_distance_work_px'] for a in matches),
                'assignment_direction_deg': max(a['direction_diff_deg'] for a in matches),
                'assignment_relative_area_error': max(a['relative_area_error'] for a in matches),
            })
        for metric, value in values.items():
            group['metrics'].setdefault(metric, []).append((r['measurement_case_id'], value))
    # 先保全结构失败；后续统计失败也不会把已有拒绝明细一起丢掉。
    report.write_text('\n'.join(['# C-v2 关联与assignment原始预算测量（待审）', '',
        '分布统计尚未完成；以下为已读取的结构失败。', '', '结构失败：'] +
        ([f'- {case}: {reason}' for case, reason in failed] or ['- 无'])) + '\n')
    rows, candidates = [], {}
    for key, group in sorted(strata.items()):
        for metric, samples in group['metrics'].items():
            if len(samples) < 2:
                failed.append((str(key), 'INSUFFICIENT_DISTRIBUTION:' + metric))
                rows.append(dict(work_width=key[0], radius=key[1], noisy=int(key[2]), metric=metric,
                    total=group['total'], failures=group['total']-len(samples),
                    **{field: len(samples) if field == 'n' else None for field in STAT_FIELDS},
                    worst_case=None))
                continue
            lower = metric.startswith('min_')
            # 相对面积无量纲，报告明确采用0.005网格，不冒称终稿指定了这一网格。
            step = 0.005 if metric.endswith('area_error') else 1 if metric.endswith('_deg') else 0.5
            stats = distribution([v for _, v in samples], lower, step)
            worst = (min if lower else max)(samples, key=lambda item: item[1])
            rows.append(dict(work_width=key[0], radius=key[1], noisy=int(key[2]), metric=metric,
                             total=group['total'], failures=group['total']-len(samples), **stats, worst_case=worst[0]))
            value = stats['rounded']
            candidates[metric] = (min if lower else max)(candidates.get(metric, value), value)
    with report.with_suffix('.csv').open('w') as out:
        writer = csv.DictWriter(out, fieldnames=CSV_FIELDS)
        writer.writeheader()
        writer.writerows(rows)
    text = ['# C-v2 关联与assignment原始预算测量（待审）', '',
            f'样例720；定位及assignment结构失败记录{len(failed)}，所有失败保留。', '',
            '规则：原图epsilon1/trim2，assignment epsilon上限3工作px，固定五比例1/3..1；采样步长1工作px。',
            '原图关联指标不应用待求关联上限；定位弧指标仍受已公开recipe划分，须连同规则审批。',
            '同一分层公式见summarize.py；相对面积步长0.005为此次显式候选规则。',
            '采样边界距连续上界可再加step/2；该裕量不隐藏在统计均值中。', '',
            '| 字段 | 统计候选（待审） |', '|---|---:|']
    text += [f'| {name} | {value:g} |' for name, value in candidates.items()]
    if not candidates:
        text += ['| 无有效分布 | N/A |', '', '有效样本不足；统计量留空，不生成候选预算。']
    text += ['', '结构失败：'] + ([f'- {case}: {reason}' for case, reason in failed] or ['- 无'])
    text += ['', f'原始记录 `{source}`；逐层统计 `{report.with_suffix(".csv").name}`。',
             '候选不能因成功率自动获批；H/视频不得反向参与定值。']
    report.write_text('\n'.join(text)+'\n')
    print(json.dumps({'total':720,'structure_failures':len(failed),'candidates':candidates}, ensure_ascii=False))


if __name__ == '__main__':
    main()
