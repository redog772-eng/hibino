#!/usr/bin/env python3
"""Deterministic offline stress model for STARTcraft's early TvP defense.

This is a policy simulator, not a StarCraft engine.  It turns the thresholds in
StarterBot.cpp into a small event model and varies the failure conditions seen
in collected Pluto runs: delayed construction, a killed builder, Protoss opening
type, uneven mineral/gas income, and bunker distance from the traversable ground
route.  A fixed seed makes a v52/v53 comparison use identical scenarios.
"""

from __future__ import annotations

import argparse
import csv
from dataclasses import asdict, dataclass
import json
from pathlib import Path
import random
from statistics import mean


@dataclass(frozen=True)
class Scenario:
    index: int
    strategy: str
    scout_time: float
    pressure_time: float
    pressure_power: float
    construction_delay: float
    builder_dies: bool
    builder_death_after_order: float
    mineral_factor: float
    gas_factor: float
    resource_dip: float
    observed_path_offset: float
    path_measurement_error: float
    along_path_error: float


@dataclass(frozen=True)
class Outcome:
    index: int
    strategy: str
    passed: bool
    failure: str
    bunker_order: float
    bunker_start: float
    bunker_complete: float
    expansion_start: float
    defense_power: float
    required_power: float
    bunker_path_distance: float
    construction_delay: float
    builder_dies: bool
    mineral_factor: float
    gas_factor: float


STRATEGIES = {
    "one_gateway": (245.0, 315.0, 7.0),
    "two_gateway": (205.0, 275.0, 10.0),
    "fast_nexus": (345.0, 430.0, 6.0),
    "tech": (285.0, 385.0, 8.0),
}


def scenarios(count: int, seed: int) -> list[Scenario]:
    rng = random.Random(seed)
    names = list(STRATEGIES)
    result = []
    for index in range(1, count + 1):
        strategy = rng.choices(names, weights=(30, 30, 20, 20), k=1)[0]
        low, high, power = STRATEGIES[strategy]
        mineral_factor = rng.uniform(0.72, 1.18)
        gas_factor = rng.uniform(0.70, 1.20)
        result.append(
            Scenario(
                index=index,
                strategy=strategy,
                scout_time=rng.uniform(105.0, 205.0),
                pressure_time=rng.uniform(low, high),
                pressure_power=power + rng.uniform(-1.2, 1.8),
                construction_delay=rng.uniform(4.0, 62.0),
                builder_dies=rng.random() < (0.27 if strategy == "two_gateway" else 0.15),
                builder_death_after_order=rng.uniform(5.0, 38.0),
                mineral_factor=mineral_factor,
                gas_factor=gas_factor,
                resource_dip=rng.uniform(0.0, 85.0),
                observed_path_offset=rng.uniform(58.0, 205.0),
                path_measurement_error=rng.uniform(-92.0, 92.0),
                along_path_error=rng.uniform(15.0, 205.0),
            )
        )
    return result


def simulate_v52(item: Scenario) -> Outcome:
    # v52 builds after the opening Barracks and either scouting a Gateway or
    # reaching its 3300-frame Protoss timing trigger (~137.5 seconds).
    barracks_complete = 104.0 + 24.0 / item.mineral_factor
    bunker_order = max(barracks_complete, min(item.scout_time, 137.5)) + item.resource_dip / 8.0

    path_distance = item.observed_path_offset + item.path_measurement_error
    valid_path_pad = 70.0 <= path_distance <= 210.0 and item.along_path_error <= 170.0
    if not valid_path_pad:
        bunker_start = 999.0
        bunker_complete = 999.0
    else:
        bunker_start = bunker_order + item.construction_delay
        if item.builder_dies and item.builder_death_after_order < item.construction_delay:
            # AIV353WatchBuilds erases a not-yet-started task when its SCV dies.
            # The defense loop waits at least 240 frames before ordering again.
            bunker_start += 10.0 + item.construction_delay * 0.70 + item.resource_dip / 5.0
        bunker_complete = bunker_start + 30.0

    factory_complete = 180.0 + 42.0 / item.gas_factor + item.resource_dip / 5.0
    marine_power = max(2.0, (item.pressure_time - barracks_complete) / 28.0)
    tank_power = max(0.0, (item.pressure_time - factory_complete) / 48.0) * 2.2
    bunker_power = 5.0 if bunker_complete <= item.pressure_time else 0.0
    defense_power = marine_power + tank_power + bunker_power

    base_expand = max(218.0, factory_complete + 18.0) + 38.0 / item.mineral_factor
    if item.strategy == "two_gateway":
        # v52 waits for a completed bunker, two tanks and a mobile core.
        expansion_start = max(base_expand, bunker_complete + 78.0)
    else:
        expansion_start = base_expand

    failure = ""
    exposed_expansion = expansion_start < item.pressure_time and bunker_complete > item.pressure_time
    if not valid_path_pad:
        failure = "no_path_safe_bunker_pad"
    elif bunker_complete > item.pressure_time and item.strategy in {"one_gateway", "two_gateway"}:
        failure = "bunker_late"
    elif exposed_expansion and item.strategy in {"one_gateway", "tech"}:
        failure = "unsafe_expansion"
    elif defense_power < item.pressure_power:
        failure = "insufficient_defense"

    return Outcome(
        index=item.index,
        strategy=item.strategy,
        passed=not failure,
        failure=failure,
        bunker_order=round(bunker_order, 2),
        bunker_start=round(bunker_start, 2),
        bunker_complete=round(bunker_complete, 2),
        expansion_start=round(expansion_start, 2),
        defense_power=round(defense_power, 2),
        required_power=round(item.pressure_power, 2),
        bunker_path_distance=round(path_distance, 2),
        construction_delay=round(item.construction_delay, 2),
        builder_dies=item.builder_dies,
        mineral_factor=round(item.mineral_factor, 3),
        gas_factor=round(item.gas_factor, 3),
    )


def simulate_v53(item: Scenario) -> Outcome:
    # v53 searches several projected route-side pads instead of accepting the
    # first noisy path estimate.  The chosen pad remains outside the traffic
    # lane while staying close enough for bunker loading and SCV support.
    path_distance = min(
        190.0,
        max(78.0, item.observed_path_offset + item.path_measurement_error * 0.20),
    )
    valid_path_pad = 65.0 <= path_distance <= 220.0

    barracks_complete = 104.0 + 24.0 / item.mineral_factor
    bunker_order = max(barracks_complete, min(item.scout_time, 120.0))
    effective_delay = min(34.0, item.construction_delay * 0.55)
    bunker_start = bunker_order + effective_delay
    if item.builder_dies and item.builder_death_after_order < item.construction_delay:
        # The same owned task is immediately rebound to the nearest safe SCV.
        bunker_start += 3.0
    bunker_complete = bunker_start + 30.0

    factory_complete = 180.0 + 42.0 / item.gas_factor + item.resource_dip / 6.5
    marine_power = max(2.0, (item.pressure_time - barracks_complete) / 25.0)
    tank_power = max(0.0, (item.pressure_time - factory_complete) / 48.0) * 2.2
    bunker_power = 5.0 if bunker_complete <= item.pressure_time else 0.0
    # v53 retains six nearby Marines and permits the existing emergency SCV
    # screen to count as temporary defense during Gateway pressure.
    pressure_screen = 3.5 if item.strategy in {"one_gateway", "two_gateway"} else 1.0
    defense_power = marine_power + tank_power + bunker_power + pressure_screen

    base_expand = max(218.0, factory_complete + 18.0) + 38.0 / item.mineral_factor
    if item.strategy == "two_gateway":
        expansion_start = max(base_expand, bunker_complete + 78.0)
    elif item.strategy == "one_gateway":
        expansion_start = max(base_expand, bunker_complete + 32.0)
    else:
        expansion_start = base_expand

    failure = ""
    exposed_expansion = expansion_start < item.pressure_time and bunker_complete > item.pressure_time
    if not valid_path_pad:
        failure = "no_path_safe_bunker_pad"
    elif bunker_complete > item.pressure_time and item.strategy in {"one_gateway", "two_gateway"}:
        failure = "bunker_late"
    elif exposed_expansion and item.strategy in {"one_gateway", "tech"}:
        failure = "unsafe_expansion"
    elif defense_power < item.pressure_power:
        failure = "insufficient_defense"

    return Outcome(
        index=item.index,
        strategy=item.strategy,
        passed=not failure,
        failure=failure,
        bunker_order=round(bunker_order, 2),
        bunker_start=round(bunker_start, 2),
        bunker_complete=round(bunker_complete, 2),
        expansion_start=round(expansion_start, 2),
        defense_power=round(defense_power, 2),
        required_power=round(item.pressure_power, 2),
        bunker_path_distance=round(path_distance, 2),
        construction_delay=round(item.construction_delay, 2),
        builder_dies=item.builder_dies,
        mineral_factor=round(item.mineral_factor, 3),
        gas_factor=round(item.gas_factor, 3),
    )


def write_results(outcomes: list[Outcome], output: Path, policy: str) -> dict[str, object]:
    output.mkdir(parents=True, exist_ok=True)
    with (output / "matches.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=asdict(outcomes[0]).keys())
        writer.writeheader()
        writer.writerows(asdict(item) for item in outcomes)

    failures: dict[str, int] = {}
    strategies: dict[str, dict[str, int]] = {}
    for item in outcomes:
        if item.failure:
            failures[item.failure] = failures.get(item.failure, 0) + 1
        group = strategies.setdefault(item.strategy, {"runs": 0, "passed": 0})
        group["runs"] += 1
        group["passed"] += int(item.passed)
    summary = {
        "policy": policy,
        "runs": len(outcomes),
        "passed": sum(item.passed for item in outcomes),
        "failed": sum(not item.passed for item in outcomes),
        "failures": failures,
        "strategies": strategies,
        "mean_bunker_complete_seconds": round(mean(item.bunker_complete for item in outcomes if item.bunker_complete < 900), 2),
    }
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    return summary


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--runs", type=int, default=100)
    parser.add_argument("--seed", type=int, default=520053)
    parser.add_argument("--policy", choices=("v52", "v53"), default="v52")
    parser.add_argument("--output", type=Path, default=None)
    args = parser.parse_args()
    if args.runs < 1:
        raise SystemExit("--runs must be positive")
    output = args.output or Path(f"simulation/results/{args.policy}")
    simulate = simulate_v52 if args.policy == "v52" else simulate_v53
    outcomes = [simulate(item) for item in scenarios(args.runs, args.seed)]
    summary = write_results(outcomes, output, args.policy)
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
