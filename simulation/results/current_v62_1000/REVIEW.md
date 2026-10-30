# Current v62 bounded model, seed 620002

1,000 scenarios executed against source SHA-256
`afbaadd313fbd3c2c0119ad66a5ecf6e8a32a58d82ed3c4e7a03f8bf97311bcd`.
978 reached two completed Tanks plus completed Siege research before frame
18,000. In 22, Tanks continued to be produced but Siege research never began.
These are readiness outcomes, not wins/losses. No enemy army was simulated.

The incomplete cases end with 8-9 completed Tanks and only 0-72 gas. Several
have thousands of minerals left. In this adapter, recurring Tank purchases
can spend gas before the 150-gas Siege threshold is reached. Trying Siege first
does not by itself reserve its gas. This is a useful current-source regression
candidate, but the adapter does not execute full gas assignment or production
scaling, so it does not prove the same failure occurs in an engine match.

Current mobile command checks also passed. Their command adapter covers
boarding slots/retries, the breach lock, in-range targeting and no-chase behavior;
it does not measure damage, losses or real movement/pathfinding.

The source version, source hash, adapter hash and runner hash are recorded in
`summary.json`. Per-scenario resource/milestone outcomes remain in `scenarios.csv`.
Incomplete cases were retained rather than adjusted to force a 100% result.
