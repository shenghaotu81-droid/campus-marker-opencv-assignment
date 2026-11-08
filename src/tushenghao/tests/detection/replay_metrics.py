#!/usr/bin/env python3
"""单MARK录像的离线对照；不链接生产链，不回馈检测/PnP，不设成功门限。
Python OpenCV只用于读取输出和生成独立渲染，C++生产构建不依赖本脚本。
"""
import argparse
import collections
import gzip
import hashlib
import json
import math
from pathlib import Path
import statistics

import cv2
import numpy as np


def statistics_px(values):
    values = sorted(values)
    if not values:
        return {"n": 0}
    return dict(n=len(values), median=statistics.median(values),
                p95=values[math.ceil(.95 * len(values)) - 1],
                max=max(values), mean=statistics.mean(values))


def read_fits(path):
    text = gzip.open(path, "rt") if str(path).endswith(".gz") else open(path)
    quads, hits, calls = {}, collections.Counter(), 0
    with text:
        for line in text:
            a = line.rstrip().split("\t")
            calls += 1
            quad = quads.setdefault((int(a[0]), int(a[1])), {})
            if len(a) >= 11:
                quad[int(a[2])] = list(map(float, a[4:]))
                hits[int(a[3])] += 1
    return quads, dict(hits), calls


def first_complete(quads):
    # 本录像只有一个MARK；重复父假设不重复计入精度分布。
    result = {}
    for (frame, index), corners in sorted(quads.items()):
        if len(corners) == 4:
            result.setdefault(frame, corners)
    return result


def read_poses(path):
    fs = cv2.FileStorage(str(path), cv2.FILE_STORAGE_READ)
    if not fs.isOpened():
        raise ValueError(f"cannot read {path}")
    frames = fs.getNode("frames")
    values, states, flags = {}, collections.Counter(), collections.Counter()
    for i in range(frames.size()):
        observations = frames.at(i).getNode("observations")
        for j in range(observations.size()):
            o = observations.at(j)
            states[o.getNode("status").string()] += 1
            if o.getNode("source").string() != "MARK":
                continue
            candidates = o.getNode("candidates")
            valid = [candidates.at(k).getNode("rmse_px").real()
                     for k in range(candidates.size())
                     if candidates.at(k).getNode("metrics_valid").real()
                     and candidates.at(k).getNode("min_depth_m").real() > 0]
            if valid:
                flags[int(o.getNode("corner_quality_flags").real())] += 1
                values[i] = min(valid)  # 仅分析，不把Ambiguous改为Unique。
    return values, dict(states), dict(flags)


def compare(args):
    before, bh, bn = read_fits(args.before_fits)
    after, ah, an = read_fits(args.after_fits)
    old, new = first_complete(before), first_complete(after)
    bp, bs, bf = read_poses(args.before_poses)
    ap, ass, af = read_poses(args.after_poses)
    common = sorted(bp.keys() & ap.keys())
    deltas = [ap[i] - bp[i] for i in common]
    moves = [dict(frame=f, p=p, delta_px=math.dist(old[f][p][:2], new[f][p][:2]))
             for f in sorted(old.keys() & new.keys()) for p in range(4)]
    report = dict(
        percentile="nearest-rank P95", baseline="b600635",
        before=dict(fit_calls=bn, quad_calls=len(before), routes=bh, states=bs,
                    quality_flags=bf, rmse_best_positive_candidate_px=statistics_px(bp.values())),
        after=dict(fit_calls=an, quad_calls=len(after), routes=ah, states=ass,
                   quality_flags=af, rmse_best_positive_candidate_px=statistics_px(ap.values())),
        lost_frames=sorted(bp.keys() - ap.keys()), gained_frames=sorted(ap.keys() - bp.keys()),
        rmse_delta_px=statistics_px(deltas),
        rmse_frame_change_counts=dict(improved_gt_1e_9=sum(x < -1e-9 for x in deltas),
            worsened_gt_1e_9=sum(x > 1e-9 for x in deltas), same_within_1e_9=sum(abs(x) <= 1e-9 for x in deltas),
            maximum_increase_px=max(deltas), maximum_decrease_px=min(deltas)),
        corner_displacement_px=statistics_px(x["delta_px"] for x in moves),
        changed_corner_count=sum(x["delta_px"] > 1e-8 for x in moves),
        largest_displacements=sorted(moves, key=lambda x: -x["delta_px"])[:10],
        old_repeated_frames={f: sum(fr == f for fr, h in before) for f, h in before if h > 0})
    if 784 in old and 784 in new:
        report["frame784"] = dict(before=old[784][1], after=new[784][1],
                                  before_rmse=bp[784], after_rmse=ap[784])
    for key, indices in [("line_mean_residual_px", [2, 3]), ("corner_to_turn_arc_px", [4]),
                         ("support_extension_px", [5, 6])]:
        report[key] = {name: statistics_px(c[p][i] for f, c in data.items()
                       if f in old and f in new for p in range(4) for i in indices)
                       for name, data in [("before", old), ("after", new)]}
    if args.truth:
        truth = json.loads(args.truth.read_text())["views"]
        a = first_complete(read_fits(args.before_truth_fits)[0])
        b = first_complete(read_fits(args.after_truth_fits)[0])
        common = a.keys() & b.keys()
        synthetic = dict(total_views=len(truth), lost_frames=sorted(a.keys() - b.keys()),
                         gained_frames=sorted(b.keys() - a.keys()))
        for name, data in [("before", a), ("after", b)]:
            err = lambda frames: [math.dist(data[f][p][:2], truth[f]["corners"][p])
                                   for f in frames for p in range(4)]
            synthetic[name] = dict(complete_frames=len(data), corner_truth_error_px=statistics_px(err(data)),
                                   common_corner_truth_error_px=statistics_px(err(common)))
        synthetic["not_detected_after"] = [v["id"] for v in truth if v["id"] not in b]
        report["synthetic_72_views"] = synthetic
    report["input_sha256"] = {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in [args.before_fits, args.after_fits, args.before_poses, args.after_poses]}
    args.output.write_text(json.dumps(report, indent=2) + "\n")


def generate(args):
    args.output.mkdir(parents=True, exist_ok=False)
    cal = cv2.FileStorage(str(args.source / "config/calibration.yaml"), cv2.FILE_STORAGE_READ)
    K, dist = cal.getNode("camera_matrix").mat(), cal.getNode("distortion_coefficients").mat()
    # 独立测试图案用于生成像素真值，生产 PnP 只消费 pose.yaml 中的四点模型。
    model = [np.array(points, dtype=float) for points in [
        [[0,0], [30,0], [30,8], [8,8], [8,30], [0,30]],
        [[80,80], [50,80], [50,72], [72,72], [72,50], [80,50]],
        [[0,80], [30,80], [30,72], [8,72], [8,50], [0,50]],
        [[66,0], [80,0], [80,14], [72,14], [72,8], [66,8]],
        [[50,0], [58,0], [58,8], [50,8]],
        [[72,22], [80,22], [80,30], [72,30]]]]
    writer = cv2.VideoWriter(str(args.output / "truth.avi"), cv2.VideoWriter_fourcc(*"MJPG"), 30, (1440, 1080))
    if not writer.isOpened():
        raise ValueError("truth video writer failed")
    views = []
    for z in [.45, .65, .9]:
        for tilt in [.05, .25, .5]:
            for roll in np.linspace(0, 2*np.pi, 8, endpoint=False):
                r1 = cv2.Rodrigues(np.array([tilt, -tilt/2, 0.]))[0]
                r2 = cv2.Rodrigues(np.array([0., 0., roll]))[0]
                r, t = cv2.Rodrigues(r2 @ r1)[0], np.array([-.04, -.04, z])
                image = np.zeros((1080, 1440, 3), np.uint8)
                for p in model:
                    obj = np.column_stack([p*.001, np.zeros(len(p))])
                    pix = cv2.projectPoints(obj, r, t, K, dist)[0].reshape(-1, 2)
                    cv2.fillPoly(image, [np.rint(pix).astype(np.int32)], (255, 255, 255))
                obj = np.array([[0, 0, 0], [.08, 0, 0], [.08, .08, 0], [0, .08, 0]], float)
                q = cv2.projectPoints(obj, r, t, K, dist)[0].reshape(-1, 2)
                writer.write(image)
                views.append(dict(id=len(views), z=z, tilt=tilt, roll=roll, corners=q.tolist()))
    writer.release()
    (args.output / "truth.json").write_text(json.dumps(dict(opencv=cv2.__version__, views=views), indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    gen = commands.add_parser("generate")
    gen.add_argument("output", type=Path)
    gen.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[2])
    cmp = commands.add_parser("compare")
    for flag in ["before-fits", "after-fits", "before-poses", "after-poses", "output"]:
        cmp.add_argument("--"+flag, type=Path, required=True)
    for flag in ["truth", "before-truth-fits", "after-truth-fits"]:
        cmp.add_argument("--"+flag, type=Path)
    args = parser.parse_args()
    if args.command == "compare":
        if args.truth and (not args.before_truth_fits or not args.after_truth_fits):
            parser.error("--truth requires both truth fit tables")
        compare(args)
    else:
        generate(args)
