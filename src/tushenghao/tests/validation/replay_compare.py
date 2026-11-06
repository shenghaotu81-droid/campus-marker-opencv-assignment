#!/usr/bin/env python3
"""Read-only comparison of archived f1ee955 and current replay. No production hooks."""
import argparse, collections, gzip, json, math
from pathlib import Path
import cv2
import numpy as np

def stats(values):
    a=np.sort(list(values))
    if not len(a):return {'n':0}
    return dict(n=len(a),median=float(np.median(a)),p95=float(a[math.ceil(.95*len(a))-1]),max=float(a[-1]),mean=float(np.mean(a)))

def storage(path):
    data=gzip.open(path,'rt').read() if str(path).endswith('.gz') else Path(path).read_text()
    return cv2.FileStorage(data,cv2.FILE_STORAGE_READ|cv2.FILE_STORAGE_MEMORY)

def integer(n):return int(n.string()) if n.isString() else int(n.real())

def observations(path):
    fs=storage(path);frames=fs.getNode('frames');out={};statuses=collections.Counter()
    for i in range(frames.size()):
        obs=frames.at(i).getNode('observations')
        for j in range(obs.size()):
            p=obs.at(j)
            if p.getNode('source').string()!='MARK':continue
            statuses[p.getNode('status').string()]+=1
            estimate=p.getNode('estimate')
            if not estimate.empty():
                out[i]=(estimate.getNode('rmse_px').real(),estimate.getNode('t_m').mat().reshape(3))
                continue
            cs=p.getNode('candidates');candidates=[]
            for k in range(cs.size()):
                c=cs.at(k)
                if not c.getNode('metrics_valid').empty() and (not c.getNode('metrics_valid').real() or c.getNode('min_depth_m').real()<=0):continue
                candidates.append((c.getNode('rmse_px').real(),c.getNode('t_m').mat().reshape(3)))
            if candidates:out[i]=min(candidates,key=lambda x:x[0])
    return dict(statuses),out

def tracks(path,poses):
    fs=storage(path);frames=fs.getNode('frames');counts=collections.Counter();velocity=0;positions={};ids=set();residual=[];nis=[];min_eigen=math.inf;max_eigen=0
    for i in range(frames.size()):
        f=frames.at(i);tt=f.getNode('tracks')
        n=f.getNode('association_nis')
        if not n.empty():nis.append(n.real())
        for j in range(tt.size()):
            t=tt.at(j);counts[t.getNode('state').string()]+=1;ids.add(integer(t.getNode('track_id')))
            if not t.getNode('position_m').empty():
                p=t.getNode('position_m').mat().reshape(3);assert np.isfinite(p).all();positions[i]=p
                if i in poses:residual.append(float(np.linalg.norm(p-poses[i][1])))
            if not t.getNode('velocity_mps').empty():
                assert np.isfinite(t.getNode('velocity_mps').mat()).all();velocity+=1
            if not t.getNode('covariance').empty():
                P=t.getNode('covariance').mat();assert np.isfinite(P).all();e=np.linalg.eigvalsh((P+P.T)/2);min_eigen=min(min_eigen,float(e.min()));max_eigen=max(max_eigen,float(e.max()))
    return dict(frames=frames.size(),states=dict(counts),velocity_frames=velocity,track_ids=len(ids),position_frames=len(positions),measured_position_residual_to_nominal_pose_m=stats(residual),reported_nis=stats(nis),minimum_covariance_eigenvalue=None if min_eigen==math.inf else min_eigen,maximum_covariance_eigenvalue=max_eigen),positions

def main(a):
    old_status,old=observations(a.before_poses);new_status,new=observations(a.after_poses)
    old_tracks,op=tracks(a.before_tracks,old);new_tracks,np_=tracks(a.after_tracks,new)
    raw={}
    for line in gzip.open(a.before_raw,'rt'):
        x=line.split()
        if int(x[2]):
            points=np.array(list(map(float,x[3:11]))).reshape(4,2);mapping=list(map(int,x[12:16]));raw[int(x[0])]=points[mapping]
    after={}
    for line in Path(a.after_raw).read_text().splitlines():
        x=line.split()
        if int(x[1]):after[int(x[0])]=np.array(list(map(float,x[2:10]))).reshape(4,2)
    common=old.keys()&new.keys();rr=raw.keys()&after.keys();tr=op.keys()&np_.keys()
    result=dict(baseline='f1ee955',before_status=old_status,after_status=new_status,before_best_rmse_px=stats(x[0] for x in old.values()),after_best_rmse_px=stats(x[0] for x in new.values()),lost_pose_frames=sorted(old.keys()-new.keys()),gained_pose_frames=sorted(new.keys()-old.keys()),lost_detection_frames=sorted(raw.keys()-after.keys()),gained_detection_frames=sorted(after.keys()-raw.keys()),after_corners=sum(len(x) for x in after.values()),corner_displacement_px=stats(float(d) for i in rr for d in np.linalg.norm(raw[i]-after[i],axis=1)),nominal_pose_translation_change_m=stats(float(np.linalg.norm(old[i][1]-new[i][1])) for i in common),before_g4=old_tracks,after_g4=new_tracks,common_g4_position_change_m=stats(float(np.linalg.norm(op[i]-np_[i])) for i in tr),limitations='No independent real corner/pose/velocity ground truth. Residual to nominal PnP is an internal consistency diagnostic, not position accuracy. Old reported NIS is gate-truncated; new NIS is unthresholded. Same reference R/Q/vv values, conditional camera-relative model only.')
    shared = op.keys() & np_.keys() & new.keys()
    result['common_reference_g4_residual_m'] = dict(
        frames=len(shared), reference='Current nominal PnP translation, identical frames and reference for both filters; not ground truth',
        before=stats(float(np.linalg.norm(op[i]-new[i][1])) for i in shared),
        after=stats(float(np.linalg.norm(np_[i]-new[i][1])) for i in shared))
    if a.camera:
        fs=storage(a.camera)
        K=fs.getNode('camera_matrix').mat();dist=fs.getNode('distortion_coefficients').mat()
        pixel_shared=shared & after.keys()
        def reprojection(positions):
            values=[]
            for i in pixel_shared:
                t=positions[i]
                if t[2]<=0:continue
                xy,_=cv2.projectPoints(np.zeros((1,3)),np.zeros(3),t,K,dist)
                values.append(float(np.linalg.norm(xy.reshape(2)-after[i][0])))
            return stats(values)
        result['common_filtered_p0_reprojection_px']=dict(
            reference='Current detected P0; filtered translation projected as model origin, not four-corner PnP RMSE or ground truth',
            before=reprojection(op),after=reprojection(np_))
    a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for k in ['before-poses','after-poses','before-tracks','after-tracks','before-raw','after-raw','output']:p.add_argument('--'+k,type=Path,required=True)
    p.add_argument('--camera',type=Path)
    main(p.parse_args())
