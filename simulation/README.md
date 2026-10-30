# Current-version simulation

`run_current.py` builds an adapter around functions extracted from the **current**
`source/StarterBot.cpp`. It does not dispatch to the old v56 abstract model.
The requested version must match the `onStart` version marker; each output
records the source, runner, adapter and mobile-test hashes.

Run from any working directory:

```bash
python3 /workspace/hibino/simulation/run_current.py --runs 1000 --seed 620002 --expected-version 64 --output /tmp/hibino-current-1000
```

The runner executes actual current `AIV11Count`, `AIV62OpeningBudget`,
`AIV11TrainWorkers`, `AIV3ResearchSiegeMode` and `AIV11Produce` C++ bodies against
the bounded resource/queue adapter. It also runs the current mobile controller's
1,000 randomized command cases and deterministic boarding/fire/healing checks.
For v63 it includes the actual `AIV63SiegeGasReserve` helper. An explicit
`--source` path can test a historical release's extracted source; both production
and mobile checks then use that same source. No checkout reset is needed.

The opening model starts at frame 3,600 with a Barracks, Refinery, 14-18 workers,
2-4 Marines and variable resources. It varies mineral income, Factory placement
delay, interrupted construction and up to four worker losses. Readiness means
two completed Tanks **and completed Siege research** by frame 18,000. The result
is model readiness, not a win or loss. It ends each scenario when ready or at the
deadline, so final resources and unit counts refer to different end frames.

It does not simulate an enemy army, combat damage, terrain, pathfinding, full
economy/expansion/scouting/production-scaling directors or the full game start.
Supply and Factory placement use explicit simplified schedulers. Income and
travel assumptions are not measurements from the uploaded replay. Change or
extend the adapter when behavior under development needs additional coverage.

Outputs are `summary.json`, `scenarios.csv`, and `mobile_checks.txt`.
Milestone means include only scenarios where the corresponding event occurred.
Incomplete cases remain recorded and are not converted into passes. Reusing a
seed makes scenarios repeatable for the same runner/adapter; compare hashes
before comparing results. Raw frame values avoid mixing the construction CSV's
time scale with replay or wall-clock time.

The older `offline_v52.py` remains a historical model only.

For v64 and later sources exposing the route helpers, the runner also executes
`run_ground_routes.py` with the selected source, run count and seed. This compiles
actual distance-field, segment-footprint and bound-selection functions. In each
1,000-case set, 900 generated maps have a connected route through alternating
wall gates, and 100 deliberately have a disconnected barrier. Two abstract
Tank positions alternately advance under the actual natural-staging predicate.
Reachable stalls, disconnected movement and footprint/distance violations fail
the run. Separate result files live under `ground_routes/`.

This extension covers route/waypoint policy. It does not execute the full Tank
controller, its command timing, unit collisions, dynamic BWAPI grid/cache
adapter, screen casualties or enemies. The route model is separate from the
opening resource model; neither is a match simulator or a replay playback.

Use `compare_runs.py BASELINE_DIR CANDIDATE_DIR --output comparison.json` for
paired comparisons. It requires the same adapter, runner, mobile-test hashes,
seed, scenario count and timing bounds, and checks scenario input fields.
