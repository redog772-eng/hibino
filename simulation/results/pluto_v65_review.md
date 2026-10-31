# Uploaded v65 match and v66 scope

Latest construction CSV session: version 65, 303 rows. Replay: Destination1.1,
18,865 frames; parser identifies Pluto as winner. The replay contains no second
Command Center build, Lift or Land request. Training counts are requests, not
proof of completed units. The CSV records no NATURAL_READY/AIR_ACTIVE event.

The earlier v64 bound ownership stall recovered: progress reached 290 and
moving=0 from frame 9,252 onward. That did not produce an operational natural.
At frame 16,212 the logs show 16 Tanks (13 sieged), followed by collapse and
worker depletion. v66 therefore adds an independent economic fallback rather
than treating movement recovery as sufficient expansion behavior.

v66 reserves 400 minerals for a new home-built CC once core forces exist and
natural preparation has stayed incomplete for 720 frames. It searches mineral
clusters, including gasless sites without a ground-route prerequisite; lifts
only that new CC, attempts landing, and trains local workers. Four local SCVs
are required for activation. Remote economic CCs do not become military rally
anchors. Failed commands/safety checks trigger alternative selection or return.

Both arena runtimes now request windowed mode through backed-up INIs and
process-scoped BWAPI configuration variables. The included new launcher is
required; previously extracted launchers are not automatically replaced.

Validation receipts describe bounded current-source models, not actual matches.
The flight decision function and full expansion director execute in adapters;
placement, safety perception and worker/depot association are scripted. Terrain
search, full Windows compilation, GUI behavior and live engine economics remain
unrun. The 600 activated / 100 lost / 300 blocked director outcomes per 1,000
cases are distinct expected model outcomes, never a win rate.
