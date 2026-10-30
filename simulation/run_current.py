#!/usr/bin/env python3
"""Version-bound current-source model; no win-rate or engine-match claims."""
import argparse
import csv
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import statistics
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--runs', type=int, default=1000)
parser.add_argument('--seed', type=int, default=620002)
parser.add_argument('--expected-version', type=int, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
if args.runs < 1:
    parser.error('--runs must be positive')
raw = (ROOT / 'source/StarterBot.cpp').read_bytes()
source = raw.decode()
on_start = source[source.index('void StarterBot::onStart()'):source.index('void StarterBot::onFrame()')]
version = int(re.search(r'printf\("AI v(\d+):', on_start).group(1))
if version != args.expected_version:
    parser.error(f'source is v{version}; requested v{args.expected_version}')


def function(name):
    match = re.search(r'\b' + re.escape(name) + r'\([^;]*?\)\s*\{', source, re.S)
    if not match:
        raise RuntimeError(f'current source adapter missing function: {name}')
    start = match.start()
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


actual = source[source.index('struct AIV11Counts {'):source.index('static int AIV62OpeningBudget(')]
for result, name in [('int', 'AIV62OpeningBudget'), ('AIV11Counts', 'AIV11Count'),
                     ('void', 'AIV11TrainWorkers'), ('void', 'AIV3ResearchSiegeMode'),
                     ('void', 'AIV11Produce')]:
    actual += '\n' + result + ' ' + function(name) + '\n'
template = (ROOT / 'simulation/current_runtime.cpp').read_text()
assert template.count('// @ACTUAL_SOURCE@') == 1
args.output.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / 'current.cpp'
    exe = Path(directory) / 'current'
    cpp.write_text(template.replace('// @ACTUAL_SOURCE@', actual))
    subprocess.run(['g++','-std=c++20','-O2','-Wall','-Wextra',str(cpp),'-o',str(exe)],check=True)
    with (args.output / 'scenarios.csv').open('w') as output:
        subprocess.run([str(exe),str(args.runs),str(args.seed)],stdout=output,check=True)
rows = list(csv.DictReader((args.output / 'scenarios.csv').open()))
assert len(rows) == args.runs
completed = [r for r in rows if r['model_completed'] == '1']
summary = {
    'source_version': version,
    'source_sha256': hashlib.sha256(raw).hexdigest(),
    'adapter_sha256': hashlib.sha256(template.encode()).hexdigest(),
    'runner_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    'mobile_test_sha256': hashlib.sha256((ROOT/'tools/check_v58_hold.py').read_bytes()).hexdigest(),
    'seed': args.seed, 'runs': len(rows),
    'validation_type': 'bounded_current_cpp_opening_model_not_engine_matches',
    'model_start_frame': 3600, 'model_deadline_frame': 18000,
    'core_ready': len(completed), 'core_not_ready_by_deadline': len(rows)-len(completed),
    'incomplete_reasons': dict(Counter(
        'siege_never_started' if int(r['siege_start_frame'])<0 else
        'siege_not_finished' if int(r['siege_done_frame'])<0 else 'second_tank_not_finished'
        for r in rows if r['model_completed']=='0')),
    'milestones_mean_frame': {
        name: round(statistics.mean(int(r[name]) for r in rows if int(r[name])>=0),1)
        if any(int(r[name])>=0 for r in rows) else None
        for name in ['factory_frame','first_tank_frame','second_tank_frame',
                     'siege_start_frame','siege_done_frame','ready_frame']},
    'assumptions': [
        'Starts after Barracks/Refinery with 14-18 workers and 2-4 Marines, not game start.',
        'Mineral income is 0.018-0.036 per miner/frame; gas income is assumed.',
        'Placement/supply scheduling, travel and interruptions are modeled, not the full bot.',
        'Does not execute the full economy/expansion, scouting, gas assignment or production-scaling directors.',
        'Up to four worker losses; no enemy army, combat damage, terrain, pathfinding or win condition.',
        'Budget, worker spending, Factory training and Siege research execute actual extracted current C++.',
    ],
}
# Also exercise the actual current mobile controller against its command adapter.
commands = subprocess.run(['python3','tools/check_v58_hold.py'],cwd=ROOT,capture_output=True,text=True)
(args.output / 'mobile_checks.txt').write_text(commands.stdout+commands.stderr)
summary['current_mobile_command_checks'] = 'passed' if commands.returncode==0 else 'failed'
(args.output / 'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,indent=2))
if commands.returncode:
    raise SystemExit(commands.returncode)
