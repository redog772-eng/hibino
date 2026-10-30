"""Compare paired scenario outcomes only when model/seed/runner agree."""
import argparse
import csv
import json
from pathlib import Path
import statistics

parser=argparse.ArgumentParser()
parser.add_argument('baseline',type=Path)
parser.add_argument('candidate',type=Path)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
a=json.loads((args.baseline/'summary.json').read_text())
b=json.loads((args.candidate/'summary.json').read_text())
for key in ['adapter_sha256','runner_sha256','mobile_test_sha256','seed','runs',
            'model_start_frame','model_deadline_frame','validation_type']:
    if a[key]!=b[key]:
        raise SystemExit(f'Incompatible runs: {key}')
left=list(csv.DictReader((args.baseline/'scenarios.csv').open()))
right=list(csv.DictReader((args.candidate/'scenarios.csv').open()))
assert len(left)==len(right)==a['runs']
fields=['scenario','initial_workers','initial_marines','mineral_rate',
        'placement_delay','build_interruption','planned_worker_losses']
for x,y in zip(left,right):
    assert all(x[k]==y[k] for k in fields),'scenario inputs differ'
both=[(x,y)for x,y in zip(left,right)if x['model_completed']==y['model_completed']=='1']
report={
    'baseline_version':a['source_version'],'candidate_version':b['source_version'],
    'baseline_source_sha256':a['source_sha256'],'candidate_source_sha256':b['source_sha256'],
    'seed':a['seed'],'paired_scenarios':len(left),
    'baseline_core_ready':a['core_ready'],'candidate_core_ready':b['core_ready'],
    'recovered_scenarios':[x['scenario']for x,y in zip(left,right)if x['model_completed']=='0' and y['model_completed']=='1'],
    'regressed_to_incomplete':[x['scenario']for x,y in zip(left,right)if x['model_completed']=='1' and y['model_completed']=='0'],
    'first_tank_later_count':sum(int(y['first_tank_frame'])>int(x['first_tank_frame'])for x,y in zip(left,right)),
    'first_tank_changed_count':sum(y['first_tank_frame']!=x['first_tank_frame']for x,y in zip(left,right)),
    'paired_completed_cases':len(both),
    'mean_ready_frame_delta_in_paired_completed_cases':round(statistics.mean(int(y['ready_frame'])-int(x['ready_frame'])for x,y in both),2)if both else None,
    'scope':'bounded model readiness; not engine combat or win rate',
}
args.output.parent.mkdir(parents=True,exist_ok=True)
args.output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
