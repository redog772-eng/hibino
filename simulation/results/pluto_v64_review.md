# v64 replay plus construction, siege-line and breach logs

The latest construction session explicitly records version 64. The file contains
five sessions (59, 59, 61, 63, 64); only the last 217 rows are used here. The
siege-line log spans frame 8,508 to 14,988. The replay lasts 15,024 frames on
Destination 1.1, Melee, with STARTcraft Terran in the north start. The screp
parser reports Pluto's team as winner. The last breach segment has 42 rows.
Old segments were excluded. CSV seconds and replay duration use different frame
conversions; frame numbers are the comparison key.

## Direct evidence

- Tank tag 3523 receives 89 Move requests: one to (1472,288), then **88** to
  (1616,336). It has Siege/Unsiege requests in between two attempts, but returns
  to the same target. The final requests continue through frame 15,006.
- Siege-line metrics repeatedly report `moving=1`, `front_progress=111` while
  the Tank count rises from two to twelve. Most samples report no contact.
  This shows a persistent outstanding move, not just a temporary enemy pause.
- Construction diagnostics never reach `NATURAL_READY`; no new Command Center
  build request occurs. Latest waiting rows report `sieged_near=0` at the natural
  site (63,19). Later `NATURAL_LINE` samples have eleven sieged Tanks and one
  moving Tank, consistent with the uncompleted movement claim.
- Terran training requests: 24 SCV, 12 Tank, 10 Marine, two Vulture. These are
  requests, not completed or surviving unit counts.
- First Factory accepted frame 4,188, completed 5,542; second completed 10,188.
  A Starport and Armory also complete. Late budget samples show worker counts
  declining to three. The breach log records a final base breach, but neither
  file alone identifies every worker's cause of death.

The physical reason that the moving Tank did not reach its point is not fully
known: unit obstruction, exact arrival geometry and pathfinding need engine
state or playback. The control-flow defect is clear: the prior moving-task
handler had no progress deadline, and its outstanding claim prevented another
bound. The earlier route model teleported its abstract Tank to each waypoint
and could not expose this defect.

## v65 repair and evidence

Actual moving-task handling is now a separately executable helper. A natural
bound with no eight-pixel improvement for 144 frames, or total age 480 frames,
enters recovery. With safe local cover it confirms Siege before releasing the
claim; without cover or under contact it accepts a retreat command and releases
the advance. Failed Siege recovery is itself bounded, with stationary/mobile
fallback rather than a fictitious cover assignment. A blocked landing cell has
a retry cooldown, while shorter unoccupied endpoints remain candidates.

The new adapter executes this actual helper with normal motion, a permanent
landing obstruction, persistent contact, slow progress, Siege rejection, unit
loss, missing cover and temporary obstruction. The old inline v64 handler is
also extracted from the published archive and run in the same adapter. It leaves
625/1,000 claims outstanding at the 900-frame model horizon; the final v65 code releases
1,000/1,000, including 125 Siege-failure and 125 retreat-rejection cases. A second
seed also releases 1,000/1,000. This is a bounded-state regression comparison,
not 625 match losses or proof of actual engine recovery.

Current production and route checks also remain successful. The full formation
controller, live BWAPI cache, real collision/pathfinding, enemy combat and actual
expansion success remain outside this adapter. Full Windows build and a new
match are still needed. New `NATURAL_MOVE`, `NATURAL_MOVE_ABORT`, and
`NATURAL_SETTLE_FAILED` events make those outcomes visible in the next CSV.
