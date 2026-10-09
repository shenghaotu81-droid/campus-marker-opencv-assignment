# 中文用途：独立对账完整实验，保留全部失败和耗时长尾，不以局部拟合成功代替检测。
import json, math, hashlib
from pathlib import Path
root=Path('docs/evidence/112-sandbox')
def read(name):
    return [json.loads(x) for x in (root/(name+'.jsonl')).read_text().splitlines()]
def stats(values):
    values=sorted(values)
    return {'mean':sum(values)/len(values),'p50':values[len(values)//2],'p95':values[min(len(values)-1,math.ceil(.95*len(values))-1)],'p99':values[min(len(values)-1,math.ceil(.99*len(values))-1)],'max':max(values),'over33':sum(x>33 for x in values)}
base=read('baseline-repeat');assert len(base)==1676 and [r['frame'] for r in base]==list(range(1676))
ids=set(json.loads(Path('docs/evidence/112-forensics/deep/sample_definition.json').read_text())['failed_ids'])
assert len(ids)==112
summary={}
for name in ['baseline-threads1','baseline-repeat','ab-naive-threads1','ab-optimized-threads1','c1-threads1','c2-threads1','c1median-threads1','c1median-fast-threads1','final-first','lad-dedup-exploratory','final-repeat1','final-repeat2','final-profile']:
    data=read(name);assert len(data)==1676 and [r['frame'] for r in data]==list(range(1676))
    old={r['frame'] for r in base if r['detections']};det={r['frame'] for r in data if r['detections']}
    changes=[r['frame'] for r,b in zip(data,base) if r['frame'] in old and r['detections']!=b['detections']]
    entry={'frames':len(data),'detected':len(det),'new_ids':sorted(det-old),'remaining_target':sorted(ids-det),'old_lost':sorted(old-det),'outside_new':sorted(det-old-ids),'old_raw_changed':changes,'elapsed_ms':stats([r['elapsed_ms'] for r in data]),'stages_mean_ms':{str(k):sum(r['timings'][str(k)] for r in data)/len(data) for k in range(1,5)}}
    if name=='final-profile':entry['functions_mean_ms']={k:sum(r['functions'][k]['ns'] for r in data)/len(data)/1e6 for k in data[0]['functions']};entry['function_calls']={k:sum(r['functions'][k]['calls'] for r in data) for k in data[0]['functions']}
    summary[name]=entry
    if name.startswith('final-repeat') or name=='final-profile':assert det-old==ids and not entry['old_lost'] and not changes
assert [r['detections'] for r in read('final-repeat1')]==[r['detections'] for r in read('final-repeat2')]
summary['independent']={}
for name in ['independent-calibration','independent-test-c1','independent-test-c2','independent-test-baseline']:
    a=read(name);assert len(a)==(25 if 'calibration' in name else 88)
    positives=[r for r in a if not r['negative']];ok=[r for r in positives if r['valid']];negatives=[r for r in a if r['negative']]
    summary['independent'][name]={'positive_count':len(positives),'accepted':len(ok),'rejected_ids':[r['id'] for r in positives if not r['valid']],'max_truth_error':max([r['max_truth_error'] for r in ok],default=-1),'errors_over2_ids':[r['id'] for r in ok if r['max_truth_error']>2],'negative_count':len(negatives),'negative_accepted_ids':[r['id'] for r in negatives if r['valid']],'tamper_not_rejected_ids':[r['id'] for r in ok if r['modeled_edges'] and not r['tamper_rejected']],'tamper_checked_count':sum(r['modeled_edges']>0 for r in ok),'by_perturbation':{str((amp,local)):{'total':sum(r['amplitude']==amp and r['local']==local for r in positives),'accepted':sum(r['amplitude']==amp and r['local']==local for r in ok)} for amp in range(3) for local in range(2)}}
optimized=read('optimized-baseline-correctness');assert len(optimized)==1676
assert all((a['status'],a['detections'])==(b['status'],b['detections']) for a,b in zip(optimized,base))
summary['optimized_baseline_correctness']={'frames':1676,'status_and_raw_differences':0,'detected':sum(bool(r['detections']) for r in optimized),'timing_used':False,'reason':'并行正确性检查，不用于性能结论'}
summary['ctest']={}
for name in ['ctest-baseline','ctest-final','ctest-debug-baseline','ctest-debug-final']:
    log=(root/(name+'.log')).read_text();assert '100% tests passed, 0 tests failed out of 25' in log
    summary['ctest'][name]={'passed':25,'failed':0,'assertions_enabled':'debug' in name}
final_independent=summary['independent']['independent-test-c1']
assert final_independent['accepted']==67 and final_independent['rejected_ids']==[2]
assert not final_independent['errors_over2_ids'] and not final_independent['negative_accepted_ids'] and not final_independent['tamper_not_rejected_ids']
(root/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n')
print(json.dumps({k:{'detected':v['detected'],'ms':v['elapsed_ms']} for k,v in summary.items() if 'elapsed_ms' in v},indent=2))
print(json.dumps(summary['independent'],indent=2))
