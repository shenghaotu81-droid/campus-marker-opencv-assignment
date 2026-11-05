#!/usr/bin/env python3
"""Analyze native G2/G4 replay artifacts; no production filtering or parameter updates."""
import argparse,csv,json,math,collections
from pathlib import Path
import cv2
import numpy as np

def stats(values):
    a=sorted(float(x) for x in values)
    if not a:return {'n':0}
    return dict(n=len(a),min=a[0],mean=float(np.mean(a)),median=float(np.median(a)),p05=a[math.ceil(.05*len(a))-1],p95=a[math.ceil(.95*len(a))-1],p99=a[math.ceil(.99*len(a))-1],max=a[-1])

def integer(n):return int(n.string()) if n.isString() else int(n.real())
def poses(path):
    fs=cv2.FileStorage(str(path),cv2.FILE_STORAGE_READ)
    if not fs.isOpened():raise ValueError(str(path))
    frames=fs.getNode('frames');out={};counts=collections.Counter()
    for i in range(frames.size()):
        f=frames.at(i);obs=f.getNode('observations');frame=integer(f.getNode('stamp').getNode('frame_id'))
        for j in range(obs.size()):
            o=obs.at(j)
            if o.getNode('source').string()!='MARK':continue
            status=o.getNode('status').string();counts[status]+=1
            cs=o.getNode('candidates');cc=[]
            for k in range(cs.size()):
                c=cs.at(k);cc.append({key:c.getNode(key).real() for key in ['rmse_px','max_error_px','min_depth_m','metrics_valid','eligible']}|{key:c.getNode(key).mat().reshape(3).tolist() for key in ['rvec','t_m']})
            out[frame]=dict(status=status,candidates=cc,flags=integer(o.getNode('corner_quality_flags')),selected=None if o.getNode('selected_index').empty() else integer(o.getNode('selected_index')))
    return out,dict(counts)

def angle(a,b):
    A=cv2.Rodrigues(np.array(a))[0];B=cv2.Rodrigues(np.array(b))[0]
    return math.acos(np.clip((np.trace(A.T@B)-1)/2,-1,1))

def chi3_cdf(x):return math.erf(math.sqrt(x/2))-math.sqrt(2*x/math.pi)*math.exp(-x/2)
def chi3_quantile(p):
    lo,hi=0.,100.
    for _ in range(80):
        mid=(lo+hi)/2
        if chi3_cdf(mid)<p:lo=mid
        else:hi=mid
    return (lo+hi)/2

def analyze(args):
    before,bc=poses(args.before);after,ac=poses(args.after)
    b={f:r for f,r in before.items() if len(r['candidates'])==2};a={f:r for f,r in after.items() if len(r['candidates'])==2}
    assert b.keys()==a.keys(),'detection set changed'
    raw_equal=True
    for f in b:
        raw_equal &= b[f]['flags']==a[f]['flags']
        for x,y in zip(b[f]['candidates'],a[f]['candidates']):raw_equal &= {k:v for k,v in x.items() if k!='eligible'}=={k:v for k,v in y.items() if k!='eligible'}
    pairs=[]
    for f in b:
        if f-1 not in b:continue
        prev,now=b[f-1]['candidates'],b[f]['candidates']
        pi=min(range(2),key=lambda k:prev[k]['rmse_px']);ni=min(range(2),key=lambda k:now[k]['rmse_px'])
        costs=[sum(angle(prev[k]['rvec'],now[k^s]['rvec']) for k in range(2)) for s in [0,1]]
        swap=int(costs[1]<costs[0])
        pairs.append(dict(frame=f,raw_slot_flip=pi!=ni,rotation_matched_flip=(pi^swap)!=ni,association_margin_rad=abs(costs[0]-costs[1])))
    both=[p for p in pairs if a[p['frame']]['status']=='UNIQUE' and a[p['frame']-1]['status']=='UNIQUE']
    best=lambda r:min(c['rmse_px'] for c in r['candidates'])
    gaps=[abs(r['candidates'][0]['rmse_px']-r['candidates'][1]['rmse_px']) for r in b.values()]
    ratios=[max(c['rmse_px'] for c in r['candidates'])/best(r) for r in b.values()]
    selected=[r['candidates'][r['selected']]['rmse_px'] for r in a.values() if r['status']=='UNIQUE']
    report=dict(baseline='a582c48',states_before=bc,states_after=ac,raw_candidates_and_flags_exact=bool(raw_equal),raw_candidate_count=sum(len(r['candidates']) for r in b.values()),rmse_gap_px=stats(gaps),bad_good_ratio=stats(ratios),baseline_best_rmse_px=stats(best(r) for r in b.values()),after_all_frame_best_rmse_px=stats(best(r) for r in a.values()),selected_unique_rmse_px=stats(selected),baseline_adjacent_pairs=len(pairs),raw_slot_flips=sum(p['raw_slot_flip'] for p in pairs),rotation_matched_flips=sum(p['rotation_matched_flip'] for p in pairs),adjacent_unique_pairs=len(both),adjacent_unique_rotation_flips=sum(p['rotation_matched_flip'] for p in both),single_absolute_candidate_ambiguous=[f for f,r in a.items() if r['status']=='AMBIGUOUS' and sum(c['eligible'] for c in r['candidates'])==1])
    # 最终语义：先绝对预算，无合格支仍NoSolution；有合格支后检查原始两支差距。
    # 本片安装约束/prior均关闭，两支数值/正深度合法。仅用于只读敏感性分析。
    sweep=[]
    for gap_limit,ratio_limit in [(g,1.) for g in [.10,.20,.25,.30,.35,.40,.50]]+[(.25,1.5),(.25,2.)]:
        chosen={};no_solution=0
        for f,r in b.items():
            cs=r['candidates'];eligible=[i for i,c in enumerate(cs) if c['rmse_px']<=1.1 and c['max_error_px']<=1.1]
            if not eligible:
                no_solution+=1;continue
            best_index=min(range(2),key=lambda i:cs[i]['rmse_px']);other=1-best_index
            if cs[other]['rmse_px']-cs[best_index]['rmse_px']<gap_limit or cs[other]['rmse_px']/cs[best_index]['rmse_px']<ratio_limit:continue
            chosen[f]=eligible[0] if len(eligible)==1 else best_index
        pp=[p for p in pairs if p['frame'] in chosen and p['frame']-1 in chosen]
        sweep.append(dict(min_gap_px=gap_limit,min_bad_good_ratio=ratio_limit,unique=len(chosen),ambiguous=len(b)-len(chosen)-no_solution,no_solution=no_solution,adjacent_unique_pairs=len(pp),rotation_matched_flips=sum(p['rotation_matched_flip'] for p in pp)))
    report['threshold_sensitivity']=sweep
    rows=list(csv.DictReader(args.nis.read_text().splitlines()))
    groups={
        'all_pre_gate':[r for r in rows if math.isfinite(float(r['nis']))],
        'confirmed_pre_gate':[r for r in rows if math.isfinite(float(r['nis'])) and r['pre_confirmed']=='1'],
        'unconfirmed_pre_gate':[r for r in rows if math.isfinite(float(r['nis'])) and r['pre_confirmed']=='0'],
        'accepted':[r for r in rows if r['accepted']=='1'],
        'rejected':[r for r in rows if math.isfinite(float(r['nis'])) and r['accepted']=='0']}
    qlo,qhi,qupper=[chi3_quantile(p) for p in [.025,.975,.95]]
    nis={}
    for name,rr in groups.items():
        v=[float(r['nis']) for r in rr];s=stats(v)
        if v:s.update(mean_matching_S_factor=s['mean']/3,mean_matching_S_std_factor=math.sqrt(s['mean']/3),above_chi3_95_fraction=sum(x>qupper for x in v)/len(v),inside_chi3_two_sided_95_fraction=sum(qlo<=x<=qhi for x in v)/len(v),below_chi3_025_count=sum(x<qlo for x in v),above_chi3_975_count=sum(x>qhi for x in v))
        nis[name]=s
    white={}
    for i,r in enumerate(rows):
        if r['pre_confirmed']!='1' or not math.isfinite(float(r['nis'])):continue
        S=np.array([[float(r['s00']),float(r['s01']),float(r['s02'])],[float(r['s01']),float(r['s11']),float(r['s12'])],[float(r['s02']),float(r['s12']),float(r['s22'])]])
        v=np.array([float(r[k]) for k in ['rx','ry','rz']]);white[i]=np.linalg.solve(np.linalg.cholesky(S),v)
    W=np.array(list(white.values()));adj=[(i-1,i) for i in white if i-1 in white and rows[i-1]['track_id']==rows[i]['track_id'] and rows[i]['reseeded']=='0']
    nis.update(ideal_gaussian_reference=dict(dimension=3,mean=3.,q025=qlo,q975=qhi,q95=qupper),whitened_mean=np.mean(W,axis=0).tolist(),whitened_second_moment_eigenvalues=np.linalg.eigvalsh(W.T@W/len(W)).tolist(),consecutive_whitened_pairs=len(adj),whitened_lag1_correlation=[float(np.corrcoef([white[i][k] for i,j in adj],[white[j][k] for i,j in adj])[0,1]) for k in range(3)],reported_velocity_std_mps=stats(float(r['post_vstd']) for r in rows if math.isfinite(float(r['post_vstd']))),velocity_public_frames=sum(r['velocity_public']=='1' for r in rows),rejected_frames=[int(r['frame']) for r in groups['rejected']],velocity_covariance_underestimation_factor=None,velocity_factor_reason='No independent velocity truth; NIS constrains S=H P H^T+R, not P_vv uniquely. Unknown-time MSE envelope and synthetic mismatched profile are conditional assumptions.')
    report['nis']=nis
    args.output.write_text(json.dumps(report,indent=2)+'\n')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for key in ['before','after','nis','output']:p.add_argument('--'+key,type=Path,required=True)
    analyze(p.parse_args())
