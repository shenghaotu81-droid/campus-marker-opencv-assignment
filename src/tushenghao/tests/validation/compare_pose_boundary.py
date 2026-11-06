#!/usr/bin/env python3
"""Compare a392bd4's candidate stream with the current single-estimate boundary."""
import argparse
import collections
import gzip
import json
from pathlib import Path
import numpy as np
from replay_compare import storage, integer, stats, tracks


def data(path):
    return gzip.decompress(path.read_bytes()) if path.suffix == '.gz' else path.read_bytes()


def main(a):
    before_fs, after_fs = storage(a.before), storage(a.after)
    old_frames, new_frames = before_fs.getNode('frames'), after_fs.getNode('frames')
    assert int(before_fs.getNode('schema_version').real()) == 5
    assert int(after_fs.getNode('schema_version').real()) == 6
    assert old_frames.size() == new_frames.size() == 1676
    old_counts, new_counts, solution_counts = (collections.Counter() for _ in range(3))
    poses = {}
    for i in range(old_frames.size()):
        old_obs = old_frames.at(i).getNode('observations')
        new_obs = new_frames.at(i).getNode('observations')
        assert old_obs.size() == new_obs.size()
        for j in range(old_obs.size()):
            old, new = old_obs.at(j), new_obs.at(j)
            for key in ['source', 'target_id', 'detection_index']:
                assert old.getNode(key).string() == new.getNode(key).string(), (i, key)
            old_counts[old.getNode('status').string()] += 1
            new_counts[new.getNode('status').string()] += 1
            cs = old.getNode('candidates')
            estimate = new.getNode('estimate')
            assert bool(cs.size()) == (not estimate.empty()), i
            assert new.getNode('status').string() == ('ESTIMATED' if cs.size() else 'UNAVAILABLE')
            assert new.getNode('candidates').empty() and new.getNode('selected_index').empty()
            assert new.getNode('rvec').empty() and new.getNode('t_m').empty()
            if not cs.size():
                continue
            solution_counts[cs.size()] += 1
            selected = integer(old.getNode('selected_index'))
            best = cs.at(selected)
            for key in ['rvec', 't_m']:
                assert np.array_equal(best.getNode(key).mat(), estimate.getNode(key).mat()), (i, key)
            e0 = best.getNode('rmse_px').real()
            assert e0 == estimate.getNode('rmse_px').real(), (i, 'rmse')
            t0 = best.getNode('t_m').mat().reshape(3)
            spread = np.zeros((3, 3))
            for k in range(cs.size()):
                if k == selected:
                    continue
                other = cs.at(k)
                ej = other.getNode('rmse_px').real()
                scale = max(e0, ej)
                u, v = (e0 / scale, ej / scale) if scale > 0 else (1., 1.)
                delta = other.getNode('t_m').mat().reshape(3) - t0
                spread += (u*u/(u*u+v*v)) * np.outer(delta, delta)
            assert np.array_equal(spread, estimate.getNode('translation_spread_m2').mat()), (i, 'spread')
            poses[i] = (e0, t0)
    assert data(a.before_tracks) == data(a.after_tracks), 'G4 output changed'
    assert data(a.before_raw) == data(a.after_raw), 'Detector/corners changed'
    summary, _ = tracks(a.after_tracks, poses)
    result = dict(
        baseline='a392bd4', frames=old_frames.size(), before_status=dict(old_counts),
        after_status=dict(new_counts), baseline_legal_solution_counts=dict(solution_counts),
        nominal_rvec_translation_rmse_exact=True, translation_spread_matches_old_formula_exact=True,
        g4_stream_byte_identical=True, detection_raw_byte_identical=True,
        detections=len(poses), corners=4*len(poses), pnP_rmse_px=stats(v[0] for v in poses.values()),
        g4=summary,
        limitations='Same conditional synthetic R/q/vv profile; no independent corner/pose/velocity ground truth.')
    a.output.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for name in ['before', 'after', 'before-tracks', 'after-tracks', 'before-raw', 'after-raw', 'output']:
        p.add_argument('--'+name, type=Path, required=True)
    main(p.parse_args())
