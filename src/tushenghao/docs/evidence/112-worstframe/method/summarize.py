# 中文用途：逐帧对账全部样本，不删除长尾，不以改变计时范围或测试集证明速度。
from pathlib import Path
import json,math
root=Path('docs/evidence/112-worstframe');previous=Path('docs/evidence/112-sandbox')
def read(path):return [json.loads(x) for x in path.read_text().splitlines()]
def stats(a):
 t=sorted(r['elapsed_ms'] for r in a);q=lambda p:t[math.ceil(p*len(t))-1]
 return dict(mean=sum(t)/len(t),p50=q(.5),p95=q(.95),p99=q(.99),max=max(t),over33=sum(x>33 for x in t),frame153=a[153]['elapsed_ms'],slowest=sorted([(r['elapsed_ms'],r['frame']) for r in a],reverse=True)[:20])
old=read(previous/'final-repeat2.jsonl');base=read(previous/'baseline-repeat.jsonl');ids=set(json.loads(Path('docs/evidence/112-forensics/deep/sample_definition.json').read_text())['failed_ids']);assert len(ids)==112
summary={}
for name in ['frozen-repeat','zero-index','cache-profile','ranges-first','ranges-profile','descriptors-first','priority-first','pre-pair-repeat1','pre-pair-repeat2','pre-pair-profile','pair-cache-final-repeat1','pair-cache-final-repeat2','pair-cache-final-profile','final-repeat1','final-repeat2','final-profile','exact-final','optimized-baseline']:
 a=read(root/(name+'.jsonl'));assert len(a)==1676 and [r['frame'] for r in a]==list(range(1676))
 assert set(r['status'] for r in a).issubset({2,3})
 det={r['frame'] for r in a if r['detections']};original={r['frame'] for r in base if r['detections']};changed=[]
 for x,y in zip(a,old):
  if x['detections']!=y['detections']:
   delta=max([math.dist(p,q) for dx,dy in zip(x['detections'],y['detections']) for p,q in zip(dx,dy)],default=-1);changed.append({'frame':x['frame'],'max_corner_delta':delta})
 entry={'frames':len(a),'detected':len(det),'elapsed_ms':stats(a),'old_success_changed':[x['frame'] for x,b in zip(a,base) if b['detections'] and x['detections']!=b['detections']],'outside_new':sorted(det-original-ids),'target_missing':sorted(ids-det),'changed_vs_frozen':changed}
 assert not entry['old_success_changed'] and not entry['outside_new']
 if name=='optimized-baseline':assert [(r['status'],r['detections']) for r in a]==[(r['status'],r['detections']) for r in base]
 else:assert not entry['target_missing'];assert [r['status'] for r in a]==[r['status'] for r in old]
 if name in ['frozen-repeat','zero-index','cache-profile','ranges-first','ranges-profile','descriptors-first','exact-final']:assert not changed
 if 'profile' in name:entry['functions_mean_ms']={k:sum(r['functions'][k]['ns'] for r in a)/1676/1e6 for k in a[0]['functions']};entry['per_frame_functions']={str(f):a[f]['functions'] for f in [7,14,153,382,395,620,1132]}
 summary[name]=entry
assert [r['detections'] for r in read(root/'final-repeat1.jsonl')]==[r['detections'] for r in read(root/'final-repeat2.jsonl')]
for name in ['final-repeat1','final-repeat2']:
 assert summary[name]['elapsed_ms']['max']<=33 and summary[name]['elapsed_ms']['mean']<=25
summary['independent']={}
for name,count in [('independent-calibration',25),('independent-test',88)]:
 a=read(root/(name+'.jsonl'));assert len(a)==count
 reference=read(previous/('independent-calibration.jsonl' if count==25 else 'independent-test-c1.jsonl'))
 assert [(r['id'],r['amplitude'],r['local'],r['angle'],r['negative']) for r in a]==[(r['id'],r['amplitude'],r['local'],r['angle'],r['negative']) for r in reference]
 positives=[r for r in a if not r['negative']];ok=[r for r in positives if r['valid']];neg=[r for r in a if r['negative']]
 r={'samples':len(a),'positive':len(positives),'accepted':len(ok),'rejected_ids':[r['id'] for r in positives if not r['valid']],'max_truth_error':max(r['max_truth_error'] for r in ok),'negative':len(neg),'negative_accepted':[r['id'] for r in neg if r['valid']],'tamper_checked':sum(r['modeled_edges']>0 for r in ok),'tamper_not_rejected':[r['id'] for r in ok if r['modeled_edges'] and not r['tamper_rejected']]}
 assert r['rejected_ids']==[2] and r['max_truth_error']<=2 and not r['negative_accepted'] and not r['tamper_not_rejected']
 summary['independent'][name]=r
summary['ctest']={}
for name in ['ctest-release-baseline','ctest-release-final','ctest-debug-baseline','ctest-debug-final']:
 assert '100% tests passed, 0 tests failed out of 25' in (root/(name+'.log')).read_text();summary['ctest'][name]={'passed':25,'failed':0,'assertions_enabled':'debug' in name}
(root/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n');print(json.dumps({k:r['elapsed_ms'] for k,r in summary.items() if 'elapsed_ms' in r},indent=2));print(json.dumps(summary['independent'],indent=2))
