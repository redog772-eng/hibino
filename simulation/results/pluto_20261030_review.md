# Uploaded Pluto replay review, 2026-10-30

Inputs: `STARTcraft_vs_Pluto.rep` (91,340 bytes) and
`AI_v59_construction.csv` (12,207 bytes). Replay commands were parsed locally
with the official Linux screp v1.13.8 release. The game was not played back in
StarCraft, and no engine-state simulation was performed.

## Evidence

- Replay: Destination 1.1, 17,757 frames, screp duration 12:25; players Pluto
  (Protoss) and STARTcraft (Terran). The parser reports Pluto's team as winner.
- Construction CSV contains two sessions. The latest has 83 records, ending
  at frame 17,748, close to the replay's final frame. Several construction
  commands in the replay follow the corresponding CSV acceptance by six frames.
  This supports using the latest session for this replay.
- The CSV `seconds` field uses approximately 16 frames per second; the replay
  duration uses a different conversion. Frame numbers are the comparison key.
  Neither timeline should be silently treated as measured wall-clock time.
- Current source hard-codes the CSV version as 59, including v60.1. The filename
  and version column therefore do not identify the installed executable version.
- Terran issued no Command Center build commands. Latest CSV likewise records
  no Command Center construction. Protoss first issued a Nexus build command
  at frame 2,688; this is a request, not proof of a completed expansion.
- Latest Terran CSV: first Factory completed at frame 6,344; second completed
  at 7,629. The highest recorded mineral balance was 1,710. No completed-unit
  counts or complete resource history can be inferred from these event samples.
- Terran train commands: SCV 32, Marine 11, Vulture 4, Tank 20. No Medic or
  Goliath train commands. These are command counts, not surviving or completed
  unit counts; cancellations and losses require separate state reconstruction.
- Same-unit, same-frame command overrides: 903 Move -> Hold Position,
  271 AttackMove -> Hold Position, 337 Attack1 -> Hold Position; total 1,511.
  Selection state was tracked using the replay's Select commands. This is
  direct evidence of conflicting orders, not a count of cancelled shots.
- Example: unit tag 3516 receives Move then Hold Position at frame 7,398,
  repeated at 7,446, 7,494, 7,542 and later frames.
- Latest CSV ends with worker losses, a lost unfinished Depot, a Depot timeout,
  and 14 NO_WORKER Bunker events. NO_WORKER proves the builder selector failed,
  not necessarily that no SCVs remained alive.
- Protoss issued Reaver, High Templar, Shuttle, Dark Templar and Corsair train
  commands. No Carrier train command appears in this replay; earlier match
  reports must not be used to label this match a Carrier loss.

## Development implications

Prioritize one owner for mobile combat orders and diagnose the natural expansion
readiness gate. The v60 attack-frame exception does not eliminate same-frame
order conflicts, and its Medic change cannot explain this match because there
are no Medic train commands. Diagnose the visible/remembered enemy composition,
detection readiness and resource spending after those foundational problems.

Command-only parsing cannot establish unit positions at death, damage dealt,
effective attacks, completed enemy expansions, or the exact engagement that
decided the match. No claim of improved win rate is supported by this review.
