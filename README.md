# hibino
히비노

# STARTcraft AI v56

Latest Windows installer: [STARTcraft_AI_v56_leapfrog_siege_no_chase.zip](releases/STARTcraft_AI_v56_leapfrog_siege_no_chase.zip)

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

