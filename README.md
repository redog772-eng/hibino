# hibino
히비노

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

