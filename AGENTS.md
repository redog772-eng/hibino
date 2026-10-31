# AI version validation

For each new AI behavior version, run 1,000 scenarios against that version's
current source before delivering it. Use `simulation/run_current.py` with
`--expected-version` and record the source hash, seed, adapter hash, results and
limitations. Never substitute the historical v56 offline model for validation
of a new release. Update the current adapter when changed behavior is outside
its coverage, or explicitly report what remains untested.

Example from the repository root:

```bash
python3 simulation/run_current.py --runs 1000 --seed 620002 --expected-version 66 --output /tmp/hibino-current-1000
```

Keep passed, incomplete, failed and unrun outcomes distinct. Inspect incomplete
scenarios; do not alter the model or success criterion solely to make them pass.
Do not describe bounded policy/command models as StarCraft matches or report
their completion rate as a win rate. Actual engine validation remains separate.

Retain historical release ZIPs. Before giving a GitHub raw-download link,
verify that the intended file has actually been uploaded.
