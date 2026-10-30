# v61 uploaded match review

Replay: `STARTcraft_vs_Pluto.rep` (81,656 bytes), Destination 1.1, Top vs Bottom,
9,091 frames, screp duration 6:21. The parser reports Pluto's team as winner.
The previous replay was Melee and lasted 17,757 frames. Different openings,
settings and durations prevent treating the two games as a controlled strength
comparison. Commands were parsed with official screp v1.13.8; no engine playback
or unit-state reconstruction was performed.

## Verified observations

- Construction CSV has three sessions: two older version-59 sessions and a
  latest version-61 session with 78 records. The latest session ends at frame
  8,880 and records no completed natural readiness. This confirms v61 logging.
- Breach CSV has three segments separated by decreasing frame numbers; only
  the final 42 rows are relevant to this match. It has no version/session header.
- Same-unit/same-frame Move, Attack1 or AttackMove followed by Hold Position:
  zero in the new replay, compared with 1,511 in the earlier uploaded replay.
  Selection state is tracked. This measures that conflict pattern, not cancelled
  shots or total command conflicts.
- One Bunker unit tag (3563) received 567 PickupBunker commands. Multiple
  requests occur in the same frame. v61 checks Marine last-command state but
  issues load commands on the Bunker, so it fails to track these requests.
- Terran issued 26 SCV, 11 Marine, one Vulture and one Tank train commands.
  These are requests, not completed/surviving unit counts. The latest CSV first
  reports one completed Tank at frame 8,160 and still only one at 8,880.
- Factory accepted frame 4,536, started 4,902, completed 6,175. Interrupted
  construction was resumed twice. First Vulture request 6,186, Machine Shop
  request 6,654, Tank request 7,266, Siege research request 7,590, Siege command
  8,802. No additional Command Center request appears.
- Late CSV samples show low minerals alongside hundreds of gas: frame 8,400
  records 11 minerals / 328 gas; frame 8,640 records 19 / 360. Neither sample
  alone proves worker saturation or exact income.
- Latest breach segment reports a base breach repeatedly in the final phase.
  The pulled-worker quota drops to zero. This indicates eligible worker defense
  was exhausted or unavailable, not proof that all SCVs were dead.
- Pluto issued four Zealot and eight Dragoon train requests, then Observer and
  Shuttle requests. Its first Dragoon request was frame 3,960; first Nexus
  request was 5,178. No Reaver, Carrier or High Templar train request occurs.
- CSV seconds use approximately frame/16; replay time uses a different scale.
  Frame numbers are the comparison key; no wall-clock timing is inferred.

## v62 response and limits

Keep v61 movement-command ownership. Fix boarding using Marine-side right-click
orders, account for incoming seats and bound the retry interval. Prioritize the
first Factory, Tanks and Siege funding over extra workers and Marines beyond a
minimum screen; postpone additional Factory scaling until the initial mech core
and Siege research have started. Record OPENING_BUDGET diagnostics for the next
match. These changes do not establish a faster opening or stronger combat until
another engine match is run.
