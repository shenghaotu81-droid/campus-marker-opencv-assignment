#!/usr/bin/env python3
"""只读H/视频结果归档，不定门限；独立核查物理对应、屏幕顺序、bbox及覆盖分母。"""
import hashlib
import itertools
import json
import math
import statistics
import sys
from collections import Counter
from pathlib import Path
from summarize import quantile


# V03/V04：证据条件在 python -O 下也必须执行，失败保留具体条件位置。
def require(condition, message):
    if not condition:
        raise AssertionError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def count_unique_support_pixels(support_arc):
    """独立核查实际坐标；回走访问不能充当新像素，不依赖测量输出的自述点数。"""
    pixels = set()
    for point in support_arc:
        if len(point) != 2 or not all(math.isfinite(value) for value in point):
            raise ValueError("SUPPORT_PIXEL_COORDINATE_NOT_FINITE_OR_NOT_2D")
        pixels.add(tuple(point))
    return len(pixels)


def screen_order(physical):
    """冻结四循环、全局最小能量、64ε集合和(y,x)字典序的独立工具核查。"""
    xmin, xmax = min(p[0] for p in physical), max(p[0] for p in physical)
    ymin, ymax = min(p[1] for p in physical), max(p[1] for p in physical)
    candidates = []
    for start in range(4):
        indices = [(start+i) % 4 for i in range(4)]
        points = [physical[i] for i in indices]
        area = sum(points[i][0]*points[(i+1)%4][1]-points[i][1]*points[(i+1)%4][0] for i in range(4))
        if area < 0:
            indices = [indices[0], *reversed(indices[1:])]
            points = [physical[i] for i in indices]
        energy = sum(((p[0]-xmin)/(xmax-xmin)-x)**2 + ((p[1]-ymin)/(ymax-ymin)-y)**2
                     for p, (x, y) in zip(points, [(0,0),(1,0),(1,1),(0,1)]))
        candidates.append((energy, tuple((p[1],p[0]) for p in points), indices))
    low = min(c[0] for c in candidates)
    tied = [c for c in candidates if abs(c[0]-low) <= 64*sys.float_info.epsilon*max(1, abs(c[0]), abs(low))]
    indices = min(tied, key=lambda c:c[1])[2]
    return [indices.index(i) for i in range(4)]


def main():
    h_path, video_path, report = map(Path, sys.argv[1:])
    h = [json.loads(s) for s in h_path.read_text().splitlines()]
    video = [json.loads(s) for s in video_path.read_text().splitlines()]
    require(len(h) == 720 and len({r['case_work_id'] for r in h}) == 720 and all((r['profile'] == 'H' for r in h)), "report_verification.py:53: len(h) == 720 and len({r['case_work_id'] for r in h}) == 720 and all((r['profile'] == 'H' for r in h))")
    require(len(video) == 1676 and [r['frame_id'] for r in video] == list(range(1676)), "report_verification.py:54: len(video) == 1676 and [r['frame_id'] for r in video] == list(range(1676))")
    require(len({r['config_sha256'] for r in h + video}) == 1, "report_verification.py:55: len({r['config_sha256'] for r in h + video}) == 1")
    failures, wrong_geometry, wrong_direction, bad_screen, bad_bbox, bad_evidence = [], 0, 0, 0, 0, 0
    worst = 0
    for r in h:
        good = len(r['detections'])==1
        truth = r['physical_corners_original']
        for d in r['detections']:
            points, orientation = d['corners'], d['orientation']
            error = min(max(math.dist(points[j],truth[i]) for i,j in enumerate(perm)) for perm in itertools.permutations(range(4)))
            wrong_geometry += error>2
            known = orientation is not None and sorted(orientation)==list(range(4))
            direction_error = max(math.dist(points[orientation[i]],truth[i]) for i in range(4)) if known else None
            wrong_direction += known and direction_error>2
            good = good and known and error<=2 and direction_error<=2
            worst = max(worst,error)
            if known:
                # 用实际双精度测量验屏幕规则，不用名义菱形替代噪声观测定义。
                def distance(measurement):
                    return max(math.dist(e['intersection'], points[orientation[i]]) for i,e in enumerate(measurement))
                measurement = min(r['measurements'], key=distance)
                bad_screen += screen_order([e['intersection'] for e in measurement])!=orientation
                bad_evidence += len(measurement)!=4 or len({e['component_id'] for e in measurement})!=4 or any(
                    len(e['support_arcs'])!=2 or any(count_unique_support_pixels(arc)<10 for arc in e['support_arcs']) for e in measurement)
            if 'bbox' in d:
                bbox = [min(p[0] for p in points),min(p[1] for p in points),max(p[0] for p in points)-min(p[0] for p in points),max(p[1] for p in points)-min(p[1] for p in points)]
                bad_bbox += any(abs(a-b)>64*2**-23*max(1,abs(a),abs(b)) for a,b in zip(bbox,d['bbox']))
        require(good == r['stage_correct'], 'runner result disagrees with independent truth evaluation')
        if not good:
            failures.append({'case':r['case_work_id'],'diagnostics':r['diagnostics'],'isolated_reason':r['isolated_reason']})
    rows = []
    for noisy in (False,True):
        for size in (480,960,1440):
            for radius in (0,2):
                group = [r for r in h if bool(r['seed'])==noisy and r['work_size'][0]==size and r['radius_original_px']==radius]
                rows.append({'noisy':noisy,'work_width':size,'radius':radius,'n':len(group),
                             'stage_correct':sum(r['stage_correct'] for r in group),'isolated_correct':sum(r['isolated_correct'] for r in group)})
    no_noise = sum(r['stage_correct'] for r in h if not r['seed'])
    noise = sum(r['stage_correct'] for r in h if r['seed'])
    h_pass = no_noise==144 and noise>=571 and all(r['stage_correct']>=95 for r in rows if r['noisy']) and not any((wrong_geometry,wrong_direction,bad_screen,bad_bbox,bad_evidence))
    reasons = Counter()
    for r in video:
        for reason in r['diagnostics']:
            if reason.startswith(('corner/','validator/','assignment/')) and '=' not in reason:
                reasons[reason.split('/')[-1]] += 1
    timing = [r['stage_ms'] for r in video if 'stage_ms' in r]
    summary = {'H_acceptance':h_pass,'H_no_noise':no_noise,'H_noise':noise,'H_isolated':sum(r['isolated_correct'] for r in h),
               'wrong_geometry':wrong_geometry,'wrong_direction':wrong_direction,'bad_screen':bad_screen,'bad_bbox':bad_bbox,
               'bad_evidence':bad_evidence,'H_max_truth_error_px':worst,'strata':rows,'video_records':len(video),
               'video_detected_frames':sum(bool(r['detections']) for r in video),'video_truncated_frames':sum(r['search_truncated'] for r in video),
               'video_reasons':dict(reasons),'V_labels':'SKIPPED_BY_USER_2026-10-06','video_recall':None,'Q_error':None,
               'config_sha256':h[0]['config_sha256'],'H_result_sha256':digest(h_path),'video_result_sha256':digest(video_path)}
    if timing:summary['video_stage_ms']={'mean':statistics.mean(timing),'p99':quantile(timing,.99),'max':max(timing)}
    report.with_suffix('.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n')
    report.with_suffix('.failures.json').write_text(json.dumps(failures,ensure_ascii=False,indent=2)+'\n')
    text=['# H与全视频阶段回归', '', f'配置SHA-256 `{summary["config_sha256"]}`。',
          f'H：无噪声{no_noise}/144，噪声{noise}/576，隔离{summary["H_isolated"]}/720；阶段门槛 **{"PASS" if h_pass else "FAIL"}**。',
          f'错误有效几何{wrong_geometry}、错误方向{wrong_direction}、屏幕规则差异{bad_screen}、bbox差异{bad_bbox}、证据缺失{bad_evidence}；输出最大真值误差{worst:.9g}px。', '',
          '| 噪声 | 工作宽 | 圆角 | n | 阶段正确 | 隔离正确 |','|---|---:|---:|---:|---:|---:|']
    text += [f'| {r["noisy"]} | {r["work_width"]} | {r["radius"]} | {r["n"]} | {r["stage_correct"]} | {r["isolated_correct"]} |' for r in rows]
    text += ['',f'视频1676/1676记录，Detection帧{summary["video_detected_frames"]}，截断帧{summary["video_truncated_frames"]}。',
             'V标注按用户决定暂时跳过；N/U/O尚无确认标签，Q依赖V且无人工真值。召回、负例错误率、人工定位均未验证，不能用检测数量代替正确率。',
             '阶段输出与公共process仍NOT_READY分开。无真值视频误差保留null。',
             f'原始H `{h_path}`；视频 `{video_path}`；hash和分层/原因见同名JSON，所有H失败见failures.json。']
    if timing:text += [f'视频单帧pipeline耗时（不含解码）mean={statistics.mean(timing):.3f}ms，P99={quantile(timing,.99):.3f}ms，max={max(timing):.3f}ms。单次本机实测不作为硬实时承诺。']
    report.write_text('\n'.join(text)+'\n');print(json.dumps(summary,ensure_ascii=False))


if __name__ == '__main__':
    main()
