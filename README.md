# hibino
히비노

Current-version validation uses `simulation/run_current.py`, which extracts the
selected source's actual production, worker, budget and Siege-research functions
and rejects a mismatched version. See [simulation coverage](simulation/README.md)
and the latest [v62/v63 paired model review](simulation/results/current_v63_1000/REVIEW.md).
These are not engine matches or win-rate results.

# STARTcraft AI v66

**Display correction:** [large window launcher](releases/STARTcraft_large_window_fix.zip).
Close games/bots, extract into a new folder and run its `run_STARTcraft_vs_Pluto.cmd`.
Both games use monitor-sized borderless windows; switch with Alt+Tab. No AI
reinstallation is needed. INI backups are retained. Windows live display is unverified.

[Windows package](releases/STARTcraft_AI_v66_air_expansion_windows.zip): extract into a new folder, close games/bots, run `upgrade_ai_v66.cmd`, then use **this package's** `run_STARTcraft_vs_Pluto.cmd`. Both games default to windowed mode; the new launcher backs up both INIs and applies matching process-specific BWAPI settings. Previously extracted launchers remain unchanged.

The [uploaded v65 review](simulation/results/pluto_v65_review.md) confirms movement recovery without a working natural. v66 can build a separate CC at home, fly to a mineral cluster (including gasless or ground-blocked sites), land and train local SCVs. It requires four workers gathering/carrying local minerals before recording activation. Threats or stalled commands trigger another site or a return; the original main CC is never assigned this task.

Two seeds each run 1,000 current-source opening, route, movement and flight-decision scenarios. The actual expansion director also runs against scripted economy/commands: 600 activated, 100 detected CC losses, 300 safely blocked cases per 1,000. These are expected component outcomes, not wins. Terrain site search, real worker/depot assignment, full Windows compilation, live flight/mining and GUI results remain unverified. Official BWAPI header syntax, PowerShell parsing, INI configuration and mocked installation/rollback checks pass. Construction rows identify version **66**.

# STARTcraft AI v65

[Windows installer](releases/STARTcraft_AI_v65_tank_movement_recovery.zip): close the game and bots, extract into a new folder, and run `upgrade_ai_v65.cmd`. Verified v54-v64 sources are accepted. Current-source model receipts are included in the ZIP.

The [v64 replay/log review](simulation/results/pluto_v64_review.md) confirms a persistent outstanding Tank move: one Tank receives 88 requests to the same point while siege-line metrics repeatedly remain `moving=1`, `front_progress=111`. Other Tanks accumulate and the natural never becomes ready. The earlier route model assumed waypoint arrival and missed this live-command failure mode.

v65 adds progress and total-duration deadlines for natural bounds. A failed bound settles under safe cover or retreats; Siege is confirmed before counting the unit as cover. Recovery itself has a deadline even when Siege or retreat commands fail. A mobile fallback remains non-cover under the outer formation gate. Temporarily rejected/occupied landings can yield a shorter clear waypoint, and the front Tank can be considered after rear candidates if another Tank supplies the required cover. Existing contact, screen and cover requirements remain.

The current-source runner now executes the actual moving-task handler against delayed transitions, persistent/temporary blockers, contact, slow motion, unit loss and command rejection. In 1,000 identical adapter cases, the old v64 handler leaves 625 claims outstanding at the 900-frame horizon; v65 releases all 1,000. A second seed also releases all 1,000. Production models still finish 1,000/1,000 per seed; route models retain 900 arrivals and 100 safe disconnected holds per seed. These are component-model outcomes, not wins. Full formation control, real StarCraft collision/pathfinding and actual expansion success remain unverified, as does full Windows linking/build. Official BWAPI header checks and mocked installer rollback tests pass.

The existing construction CSV filename is retained, with version **65**. New `NATURAL_MOVE`, `NATURAL_MOVE_ABORT` and `NATURAL_SETTLE_FAILED` records expose current distance, move/progress age and recovery failures.

# STARTcraft AI v64

[Windows installer](releases/STARTcraft_AI_v64_ground_route_natural.zip): close the game and bots, extract into a new folder, and run `upgrade_ai_v64.cmd`. Verified v54-v63 sources are accepted. Source-bound model summaries are included in the ZIP.

The [latest uploaded replay](simulation/results/pluto_after_v63_review.md) contains 17 Tank training requests, one Unsiege request and no additional Command Center request. CSVs were not provided, so the actual running version and exact natural-staging blocker remain unconfirmed. The review separates these observations from source findings and inferred causes.

v64 shares a ground-route objective between Tank staging and its mobile screen. Reinforcements join the forward Tank instead of sieging beside the nearest rear Tank. Natural bounds follow a cached four-neighbor ground-distance field around obstacles; candidate segments check a 32-pixel footprint and retain the existing cover/screen/contact constraints. Tanks finish natural bounds within 16 pixels before sieging, rather than using the offensive controller's 105-pixel arrival radius. Offensive bounds and Siege gas reservation are retained. `NATURAL_BOUND` diagnostics include route distance, cover count, screen count, path validity and gap.

Current-source validation: both seeds complete 1,000/1,000 opening readiness scenarios. Each seed also executes 1,000 route scenarios: 900 reachable cases arrive, 100 disconnected cases hold, with zero reachable stalls or measured safety violations. Current mobile command checks, official BWAPI header syntax checks, and mocked-build installation/rollback checks pass. Route checks cover actual route helper code but do not execute the entire Tank controller or the live grid/cache adapter. Full Windows build, Destination terrain traversal, real unit collision and actual match performance remain unverified. Construction rows identify **64** using the existing `AI_v59_construction.csv` filename.

# STARTcraft AI v63

[Windows installer](releases/STARTcraft_AI_v63_siege_gas_priority.zip): close the game and bots, extract into a new folder, and run `upgrade_ai_v63.cmd`. Verified v54-v62 sources are accepted. Model receipts matching the packaged source are included under `validation/`.

v62 attempted Siege research before Factory production but did not preserve its gas. In the current-source bounded model, recurring Tank purchases prevented Siege from starting in 22/1,000 scenarios. v63 keeps the first defensive Tank available, then requires subsequent Tank and Medic purchases to leave 150 gas until Siege is researched/researching. The reservation releases as soon as research starts. Existing v62 movement ownership, boarding and natural staging are retained.

Same-adapter, same-seed comparison: v62 core readiness 978/1,000; v63 1,000/1,000. First-Tank completion is identical in every paired case. A second seed also gives 1,000/1,000 ready. Readiness is two completed Tanks and completed Siege research by the model deadline, not wins. Terrain, enemy combat and full economy/gas assignment are outside model coverage. Giving research priority can delay later Tanks; the paired mean second-Tank delay is approximately 49 frames.

Validation: 1,000 current-version scenarios per seed, current mobile command cases, `tools/check_v63_siege_budget.py`, existing ownership/advance checks, official BWAPI header syntax checks, and packaged installation/rollback checks with mocked MSBuild. Full Windows linking/build and actual Pluto gameplay remain unverified. The existing construction CSV filename is retained; rows identify **63** and `OPENING_BUDGET` now includes `siege_gas_reserve`.

# STARTcraft AI v62

[Windows installer](releases/STARTcraft_AI_v62_bunker_opening_priority.zip): close the game and bots, extract into a new folder, and run `upgrade_ai_v62.cmd`. Verified v54-v61 sources are accepted.

The latest [v61 match review](simulation/results/pluto_v61_20261030_review.md) confirms zero of the previously observed same-frame move/attack-to-hold overrides. It also records 567 Bunker pickup requests, only one Tank production request, and no Command Center expansion request before the replay ends. This is not evidence of improved match strength; the opponent opening, game type and duration differ from the earlier replay.

v62 issues boarding commands on Marines via right-click, counts approaching Marines against the Bunker's four seats, and gives an existing request 192 frames before retrying. The controller test now models real Bunker-side `load()` command ownership, rather than incorrectly updating a Marine's last command. Base-breach boarding restrictions remain authoritative.

One-base TvP retains 200 minerals for its first Factory once 14 workers, a Barracks and 100 gas are available; with a Factory it retains 150 for the first two Tanks and Siege readiness. This budget applies to additional SCVs and Marines beyond a minimum four-unit screen, not emergency supply, Tank production or repairs. It releases when two Tanks exist and Siege is researched/researching, or after a second Command Center exists. Siege research is attempted before Factory production spending. Additional Factory scaling waits for that initial Tank/Siege core, and routine TvP supply reserves now engage within eight half-supply rather than 22. This can slow worker and Marine reinforcement, so actual opening timings and survivability need a new match.

Existing filenames are retained. New construction rows identify version **62** and add `OPENING_BUDGET` events. Validate with `python3 tools/check_v58_hold.py`, `python3 tools/check_v59_advance.py`, `python3 tools/check_v62_opening.py`, and `python3 tools/check_v61_installer.py --version 62 --archive releases/STARTcraft_AI_v62_bunker_opening_priority.zip`. The official-header check also covers the changed worker and production functions. Windows linking/build and real-match performance remain unverified.

# STARTcraft AI v61

[Windows installer](releases/STARTcraft_AI_v61_single_owner_natural.zip): close StarCraft and both bots, extract into a new folder, and run `upgrade_ai_v61.cmd`. Verified v54-v60.1 sources are accepted, including recovery from the original v60 source.

The uploaded Destination replay contains 1,511 same-unit, same-frame movement/attack commands followed by Hold Position, and no Terran Command Center build commands. The [review](simulation/results/pluto_20261030_review.md) separates issued commands from completed units and notes that the CSV contains two sessions and uses a different time scale. Neither the CSV's old hard-coded version column nor its filename establishes the executable version.

v61 excludes TvP Marines, Vultures, Goliaths and Medics from legacy army movement, reactive infantry targeting, traffic/stuck correction and staging orders. One mobile controller owns local fire, return to the Tank line, healing and bunker boarding; reserved mine-laying Vultures and Stim remain explicit exceptions. The screen follows the forward sieged Tank rather than remaining at the nearest bunker. It fires at visible, detected enemies already within weapon range and within 320 pixels of its anchor, and returns to the line when targets leave range. In-range High Templar receive targeting priority; Observers are excluded.

Natural staging can start with two Tanks. Each short bound requires one covering sieged Tank, two nearby screen units, no ground contact within 430 pixels of the line, a sampled walkable segment and at most 160 pixels from the front Tank. Offensive bounds retain the three-Tank/two-cover/three-screen gate. Expansion readiness accepts two sieged Tanks within 400 pixels of the natural and rejects armed nonworker ground contacts within 640 pixels; existing economy, bunker, home-threat, site legality and retry conditions still apply. Terrain/pathfinding or other unmet conditions may still prevent staging; no guarantee of expansion timing is made.

The existing `AI_v59_construction.csv` filename is retained, but new rows identify version **61** and include `NATURAL_WAIT`, `NATURAL_READY` and `NATURAL_LINE` diagnostic events. Siege-line and breach log filenames also remain unchanged.

Third/fourth-base eligibility and the expansion dispatcher now accept the one-Barracks TvP mech plan. Their former three/four-Barracks prerequisites could never be met while TvP construction caps Barracks at one. Worker, Factory, completed-base, mineral and home-threat checks remain in place. Missing-site and initial-formation waits also generate diagnostic events.

Validation: `python3 tools/check_v58_hold.py`, `python3 tools/check_v59_advance.py`, `python3 tools/check_v61_ownership.py`, and `python3 tools/check_v61_installer.py` pass. The installer check uses a mocked build to exercise source/EXE rollback and arena copying; it also verifies the executable marker occurs in the actual source. `python3 tools/check_v61_bwapi_headers.py --include <official-bwapi-4.4.0/bwapi/include>` syntax-checks the changed controller bodies against real BWAPI types and APIs. Full Windows linking/build and a new Pluto match remain unverified; these checks establish neither a win rate nor fewer actual casualties.

# STARTcraft AI v60

[Windows installer v60.1](releases/STARTcraft_AI_v60_1_marker_fix.zip): close the game and bots, extract, and run `upgrade_ai_v60.cmd`. Verified v54-v60 sources are accepted. v60.1 fixes the source/EXE version-marker mismatch that caused the original v60 installer to restore the previous installation. Existing v59 log filenames are retained.

The final TvP hold controller now preserves attack-animation frames. Medics near their defensive anchor heal wounded organic allies within 160 pixels of both the anchor and Medic, rather than having their healing overwritten by a hold order. Identical healing commands are preserved; distant casualties do not pull Medics away from the line.

Validation: `python3 tools/check_v58_hold.py` exercises the extracted controller, including attack-frame preservation, healing, repeated commands and distant patients, plus 1,000 existing randomized cases. `python3 tools/check_v60_installer.py` validates the package and historical upgrade acceptance. `python3 tools/check_v59_advance.py` still passes. These component checks do not establish improved match results. Full Windows/BWAPI build and a new Pluto match remain unverified; no current v59 match logs are present in this checkout.

# STARTcraft AI v59

[Windows installer](releases/STARTcraft_AI_v59_high_ground_cover.zip): extract and run `upgrade_ai_v59.cmd`. Accepts verified v54-v58 sources.

The natural preparation controller previously sent the first two Tanks forward independently and excluded them from the shared siege controller. v59 removes those movement orders, claims all completed TvP Tanks before legacy controllers, and uses ground height and walkability to choose an initial main-base defense pad. Three Tanks must first assemble in Siege Mode before descent. Each bound requires two nearby sieged cover Tanks, three screen units, no close ground contact, and a sampled walkable segment. Moving Tanks retreat toward cover on close contact instead of immediately sieging. A flat map uses an eligible same-height position. No blanket guarantee is made about engine pathfinding or the quality of every defensive pad.

Latest uploaded construction and breach CSVs each contain two sessions; the latest construction session has 44 rows, one Barracks, and two completed Factories. Pluto's log records `Carrier` and a Pluto win; the opening label alone does not establish which units caused the loss. The replay was received, not played back here.

Validation: `tools/check_v59_advance.py` compiles the actual advance predicate and projection helper: 1,000 randomized cases produced 45 allowed and 955 blocked decisions, passing safety assertions. `tools/check_v58_hold.py` passes 1,000 mobile-screen command-stub cases. These are policy/component tests, not engine matches. Full Windows/BWAPI compilation and real-game formation remain unverified. Logs are `AI_v59_construction.csv`, `AI_v59_breach.csv` and `AI_v59_siege_line.csv`.

Map synchronization launcher: [download](releases/STARTcraft_vs_Pluto_map_sync.zip). Back up `tools/run_arena.ps1` next to your existing `run_STARTcraft_vs_Pluto.cmd`, then replace it with the file from this ZIP. Before launching either game, it copies missing/different `.scx` and `.scm` files from host/maps to pluto/maps at matching relative paths, verifies SHA256, and backs up differing destination files. It deletes no maps. This addresses file mismatches; it does not establish the cause of every in-game map transfer. Archive and static launch-order checks passed; PowerShell/StarCraft execution was unavailable here.

# STARTcraft AI v58

[Windows installer](releases/STARTcraft_AI_v58_hold_screen_one_barracks.zip). Extract into a new folder, close the game and bots, and run `upgrade_ai_v58.cmd`. Verified v54 through v57.1 installations are supported.

TvP now keeps one Barracks and targets two Factories on one base, five after expansion. The build dispatcher also rejects additional Barracks so alternate production callers cannot bypass this rule. Factory scaling becomes eligible after the first Factory and no longer waits for the 420-second timing; worker and resource requirements still apply.

The direct-target micro and six-Marine expansion raid are disabled in TvP. The final mobile-unit controller moves Marines, Vultures, Goliaths and Medics to friendly bunker/sieged-Tank positions and holds there. A breached nearby Command Center takes priority. Bunker boarding and reserved mine-laying remain supported. Tank movement retains the existing siege controller. No claim is made that the whole army is immobile or that the previous Tank controller is fully validated.

The uploaded v57 construction log records a second Barracks accepted at 426 seconds, then a Factory ordered at 435 seconds on the same tile (53,114). The Barracks starts at 438 seconds and the Factory times out at 480 seconds. The replay was received but not played back here.

Validation: `python3 tools/check_v58_hold.py` compiles the actual hold-controller body against a small command-recording stub and checks 1,000 randomized positions, including breach return, loaded units and reserved mine layers. These are command tests, not StarCraft matches or a win-rate model. `python3 tools/check_v58_installer.py` checks upgrade acceptance, unknown-source rejection and source integrity. Full Windows/BWAPI compilation and actual gameplay remain to be verified. New logs: `AI_v58_construction.csv` and `AI_v58_breach.csv`.

# STARTcraft AI v57

[Windows installer v57.1](releases/STARTcraft_AI_v57_1_compile_fix.zip): extract and run `upgrade_ai_v57.cmd` with StarCraft and bots closed. Verified v54, v55, v56 and original v57 sources are accepted directly. v57.1 adds the missing forward declaration for `AIV25Seen`, fixing MSVC C3861 and its cascading C2737 error reported in the uploaded build log. The gameplay rules are unchanged.

Ground attackers within 460 pixels of a Command Center trigger emergency SCV support. Empty or out-of-range bunkers no longer inflate local defender strength. Bunkers with no visible attacker in their firing range unload Marines to support the breached base, and loading is suppressed for 96 frames after the last breach observation. Emergency infantry orders execute after normal staging. Builders, repairers, gas workers and wounded SCVs remain protected; at least four local workers are retained from the pull quota. This is worker-based emergency defense, not a map-specific building wall.

The reviewed Pluto log reports `o:1GateCore` and a Pluto win. The attached CSV contains v54 sessions, but a stale CSV cannot establish the active executable version. Check `AI_v57_construction.csv` on the next run; `AI_v57_breach.csv` records emergency decisions. The replay has been retained as input but was not played back in this environment.

Validation: installer checks accept v54-v57 and reject unknown sources without mutation; embedded source matches `source/StarterBot.cpp`; command-order regression checks pass. Windows/BWAPI compilation and actual match behavior remain unverified here. The existing v56 abstract model was rerun for 1,000 cases with seed 570001, but does not exercise this C++ change. Earlier fixed metrics such as 185-pixel gaps were model assumptions, not measured engine behavior or win rates.

# STARTcraft AI v56

Latest Windows installer: [STARTcraft_AI_v56_1_installer_fix.zip](releases/STARTcraft_AI_v56_1_installer_fix.zip)

The v56.1 installer accepts the verified v54 and v55 sources as well as v56 for recovery. This fixes the rejected direct upgrade from v54; the v56 game source is unchanged. Unknown modified sources are still rejected without changing files.

The newest uploaded match was another v54 run, not v55: the construction report contains version 54, Pluto selected `Storm~tr`, and Pluto won. v56 keeps the prepared-natural rules from v55 and gives each non-garrison Tank to one siege-line controller so attack, retarget, traffic, and stuck-unit code cannot pull individual Tanks away.

At the natural, Marines hold their assigned posts and Tanks stay sieged behind the approach. During a push, one rear Tank advances at most 185 pixels while every other Tank remains deployed; detected ground contact freezes the next bound. Marines, Medics, and Vultures are leashed to the Tank line instead of chasing retreating units.

The expanded offline model uses seed `560001` across 1,000 randomized cases, including retreating bait, Storm pressure, path turns, construction delays, worker deaths, Protoss openings, resources, bunker distance, Observers, and natural follow-ups. The v55 chase behavior passed 583/1,000 checks; v56 passed 1,000/1,000. The real-match evidence is in `simulation/results/actual_v54_storm`, with the comparison in `simulation/results/v55_chase_1000` and `simulation/results/v56_1000`. One actual v56 match is still required.

# STARTcraft AI v55

Latest Windows installer: [STARTcraft_AI_v55_prepared_natural_disciplined_response.zip](releases/STARTcraft_AI_v55_prepared_natural_disciplined_response.zip)

The uploaded v54 match against Pluto's two-Gateway opening ended in a loss. Its natural Command Center started at 9:40 and was destroyed four seconds later. v55 caps one-base TvP Marines at 10, or 12 after confirming two Gateways, ignores Observers and isolated distant contacts for global recall, and counts Marines already at their defensive post before moving more units.

Against Protoss, v55 now requires two Tanks to reach the selected natural approach and enter Siege Mode before the expansion may begin. It also maintains two Vultures, researches Spider Mines after Siege Mode, and mines the approach. The expanded offline policy model covers Observer decoys, isolated contacts, and natural follow-up attacks. With seed `550001`, the observed v54 rules passed 458/1,000 checks and v55 passed 1,000/1,000.

The real v54 findings are in `simulation/results/actual_v54`; the comparative runs are in `simulation/results/v54_observed_1000` and `simulation/results/v55`. These checks validate policy conditions rather than StarCraft match win rate, so one actual v55 match is still required.

# STARTcraft AI v54

Latest Windows installer: [STARTcraft_AI_v54_construction_resume_tech_guard.zip](releases/STARTcraft_AI_v54_construction_resume_tech_guard.zip)

The uploaded v53 match against Pluto ended in a loss and exposed two concrete problems: a Factory remained unattended for 391 seconds after its SCV died, and the bot issued 41 Starport orders before the required tech was ready. v54 assigns a replacement SCV to unfinished Terran buildings, checks whether a tech building can be made before issuing its order, and stages a seventh Marine against a confirmed two-Gateway opening.

The offline model now runs 1,000 randomized scenarios with seed `540001`. Under the same scenarios, v53 passed 899/1,000 and v54 passed 1,000/1,000. The final results are in `simulation/results/v53_1000` and `simulation/results/v54`; the uploaded real-match findings are in `simulation/results/actual_v53`.

One new actual v54 match is still required to confirm the fixes in StarCraft. Install v54, run the existing `run_STARTcraft_vs_Pluto.cmd` once, and retain `AI_v54_construction.csv`, `pluto.log`, `pluto_infer.log`, and the replay.

# STARTcraft AI v53

Latest Windows installer: [STARTcraft_AI_v53_resilient_ground_path_defense.zip](releases/STARTcraft_AI_v53_resilient_ground_path_defense.zip)

v53 hands an interrupted build to a replacement SCV, projects main and natural bunkers from traversable ground paths, prepares the Protoss entrance defense earlier, and holds pressured expansions until the defense is ready.

The deterministic offline stress model is in `simulation/offline_v52.py`. With seed `520053`, the v52 policy passed 36/100 scenarios and the corrected v53 policy passed 100/100. The model covers one-Gateway, two-Gateway, fast-Nexus, and tech openings while varying construction delay, builder death, mineral/gas income, and bunker distance from the ground route.

# STARTcraft AI v52

Latest Windows installer: [STARTcraft_AI_v52_path_bunker_auto_benchmark.zip](releases/STARTcraft_AI_v52_path_bunker_auto_benchmark.zip)

v52 places the main bunker beside the detected ground path and choke. The package also includes a five-match automatic runner that collects all results into one ZIP.

# STARTcraft AI v51

Latest Windows installer: [STARTcraft_AI_v51_resilient_construction_protoss_hold.zip](releases/STARTcraft_AI_v51_resilient_construction_protoss_hold.zip)

v51 prepares one main-entrance bunker against early Protoss pressure and restores accepted construction commands that stall before the foundation starts.

# STARTcraft AI v50

Latest Windows installer: [STARTcraft_AI_v50_natural_defense_factory_discipline.zip](releases/STARTcraft_AI_v50_natural_defense_factory_discipline.zip)

v50 prepares the natural defense as soon as the expansion starts, keeps matched fast-expansion Marines at home, preserves Factory funds, and prevents duplicate construction orders when an SCV command is overwritten.

# STARTcraft AI v49

Latest Windows installer: [STARTcraft_AI_v49_disciplined_expansion_defense.zip](releases/STARTcraft_AI_v49_disciplined_expansion_defense.zip)

v49 delays a destroyed expansion attempt, requires a prepared defense before expanding into a scouted Protoss two-Gateway opening, caps one-base TvP Factory growth, and places the main bunker closer to the entrance.

