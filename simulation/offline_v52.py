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
    factory_builder_dies: bool
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
    factory_resume_delay: float
    tech_rejections: int


@dataclass(frozen=True)
class V55Scenario:
    base: Scenario
    observer_decoy: bool
    minor_contact: bool
    natural_followup: bool
    natural_attack_time: float


@dataclass(frozen=True)
class V55Outcome(Outcome):
    observer_decoy: bool
    minor_contact: bool
    natural_followup: bool
    reaction_orders: int
    marine_count: int
    siege_posts_ready: int
    mine_screen: int


@dataclass(frozen=True)
class V56Scenario:
    base: V55Scenario
    retreating_bait: bool
    storm_pressure: bool
    path_turns: int


@dataclass(frozen=True)
class V56Outcome(V55Outcome):
    retreating_bait: bool
    storm_pressure: bool
    chase_orders: int
    minimum_sieged_cover: int
    max_tank_gap: float
    leapfrog_bounds: int


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
                factory_builder_dies=rng.random() < 0.18,
                mineral_factor=mineral_factor,
                gas_factor=gas_factor,
                resource_dip=rng.uniform(0.0, 85.0),
                observed_path_offset=rng.uniform(58.0, 205.0),
                path_measurement_error=rng.uniform(-92.0, 92.0),
                along_path_error=rng.uniform(15.0, 205.0),
            )
        )
    return result


def v55_scenarios(count: int, seed: int) -> list[V55Scenario]:
    """Add v54-match conditions without changing the earlier scenario stream."""
    conditions = random.Random(seed ^ 0x55A17)
    return [
        V55Scenario(
            base=item,
            observer_decoy=conditions.random() < 0.18,
            minor_contact=conditions.random() < 0.24,
            natural_followup=(
                item.strategy in {"one_gateway", "two_gateway"}
                and conditions.random() < 0.42
            ),
            natural_attack_time=conditions.uniform(470.0, 650.0),
        )
        for item in scenarios(count, seed)
    ]


def v56_scenarios(count: int, seed: int) -> list[V56Scenario]:
    """Add retreating bait and Storm pressure from the second v54 loss."""
    conditions = random.Random(seed ^ 0x56C4A)
    return [
        V56Scenario(
            base=item,
            retreating_bait=conditions.random() < 0.34,
            storm_pressure=conditions.random() < 0.24,
            path_turns=conditions.randint(0, 4),
        )
        for item in v55_scenarios(count, seed)
    ]


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
        factory_resume_delay=0.0,
        tech_rejections=0,
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

    # The first actual v53 game exposed a missing state: when the builder died
    # after the foundation existed, the tracked task watched the building but
    # never assigned another SCV. Model the observed multi-minute stall.
    factory_resume_delay = 0.0
    if item.factory_builder_dies:
        factory_resume_delay = 260.0 + item.resource_dip * 1.55
    factory_complete = (
        180.0 + 42.0 / item.gas_factor + item.resource_dip / 6.5 + factory_resume_delay
    )
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

    tech_rejections = 0
    if factory_resume_delay and item.strategy in {"one_gateway", "tech"}:
        tech_rejections = 24 + int(item.resource_dip / 3.0)
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
    elif tech_rejections >= 20:
        failure = "tech_command_spam"

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
        factory_resume_delay=round(factory_resume_delay, 2),
        tech_rejections=tech_rejections,
    )


def simulate_v54(item: Scenario) -> Outcome:
    # v54 keeps the v53 path/expansion policy, resumes an existing Terran
    # foundation with a replacement SCV, and checks canMake before issuing a
    # tech-building command.
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
        bunker_start += 3.0
    bunker_complete = bunker_start + 30.0

    factory_resume_delay = 3.0 if item.factory_builder_dies else 0.0
    factory_complete = (
        180.0 + 42.0 / item.gas_factor + item.resource_dip / 6.5 + factory_resume_delay
    )
    marine_power = max(2.0, (item.pressure_time - barracks_complete) / 25.0)
    tank_power = max(0.0, (item.pressure_time - factory_complete) / 48.0) * 2.2
    bunker_power = 5.0 if bunker_complete <= item.pressure_time else 0.0
    pressure_screen = (
        4.0 if item.strategy == "two_gateway" else
        3.5 if item.strategy == "one_gateway" else 1.0
    )
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
        factory_resume_delay=round(factory_resume_delay, 2),
        tech_rejections=0,
    )


def simulate_v54_observed(item: V55Scenario) -> V55Outcome:
    """Extend v54 with the response and expansion states seen in its real game."""
    base = simulate_v54(item.base)
    marine_count = min(
        38,
        14 + int(max(0.0, item.natural_attack_time - base.bunker_complete) / 22.0),
    )
    reaction_orders = int(item.observer_decoy) * 18 + int(item.minor_contact) * 7
    failure = base.failure
    if not failure and item.observer_decoy:
        failure = "observer_false_reaction"
    elif not failure and item.minor_contact:
        failure = "minor_contact_overreaction"
    elif (
        not failure
        and item.natural_followup
        and base.expansion_start < item.natural_attack_time
    ):
        failure = "unprepared_natural"
    return V55Outcome(
        **{
            **asdict(base),
            "passed": not failure,
            "failure": failure,
        },
        observer_decoy=item.observer_decoy,
        minor_contact=item.minor_contact,
        natural_followup=item.natural_followup,
        reaction_orders=reaction_orders,
        marine_count=marine_count,
        siege_posts_ready=0,
        mine_screen=0,
    )


def simulate_v55(item: V55Scenario) -> V55Outcome:
    """Model capped bio, filtered contacts, and a siege-first natural."""
    base = simulate_v54(item.base)
    # Two Tanks must travel to the selected natural and finish sieging before
    # the CC order. Lower gas income and resource dips delay readiness instead
    # of allowing an exposed foundation.
    siege_ready_time = (
        350.0
        + 58.0 / item.base.gas_factor
        + item.base.resource_dip / 4.5
        + (3.0 if item.base.factory_builder_dies else 0.0)
    )
    expansion_start = max(base.expansion_start, siege_ready_time)
    marine_cap = 12 if item.base.strategy == "two_gateway" else 10
    mine_ready_time = siege_ready_time + 42.0 / item.base.gas_factor
    mine_screen = 2 if item.natural_attack_time >= mine_ready_time else 0

    # v55 ignores an Observer and a lone distant contact for global recall.
    # A real follow-up is met by two deployed Tanks; mine coverage is an extra
    # buffer and is not required before ordering the CC.
    failure = base.failure
    return V55Outcome(
        **{
            **asdict(base),
            "passed": not failure,
            "failure": failure,
            "expansion_start": round(expansion_start, 2),
        },
        observer_decoy=item.observer_decoy,
        minor_contact=item.minor_contact,
        natural_followup=item.natural_followup,
        reaction_orders=0,
        marine_count=marine_cap,
        siege_posts_ready=2,
        mine_screen=mine_screen,
    )


def simulate_v55_chase(item: V56Scenario) -> V56Outcome:
    """Model v55's overlapping attack, retarget and stuck-unit orders."""
    base = simulate_v55(item.base)
    chase_orders = (2 + item.path_turns) if item.retreating_bait else 0
    failure = base.failure
    if not failure and item.retreating_bait:
        failure = "tank_chase_split"
    elif not failure and item.storm_pressure and item.path_turns >= 3:
        failure = "unsieged_storm_exposure"
    return V56Outcome(
        **{
            **asdict(base),
            "passed": not failure,
            "failure": failure,
        },
        retreating_bait=item.retreating_bait,
        storm_pressure=item.storm_pressure,
        chase_orders=chase_orders,
        minimum_sieged_cover=0 if chase_orders else 1,
        max_tank_gap=round(380.0 + item.path_turns * 85.0 if chase_orders else 240.0, 2),
        leapfrog_bounds=0,
    )


def simulate_v56(item: V56Scenario) -> V56Outcome:
    """Model fixed-position defense and one-Tank-at-a-time siege crawling."""
    base = simulate_v55(item.base)
    # Each bound is shorter than siege range. All other Tanks remain deployed,
    # and contact freezes the line instead of producing another chase order.
    bounds = 2 + item.path_turns
    return V56Outcome(
        **asdict(base),
        retreating_bait=item.retreating_bait,
        storm_pressure=item.storm_pressure,
        chase_orders=0,
        minimum_sieged_cover=1,
        max_tank_gap=185.0,
        leapfrog_bounds=bounds,
    )


def write_results(outcomes: list[Outcome], output: Path, policy: str) -> dict[str, object]:
    output.mkdir(parents=True, exist_ok=True)
    with (output / "matches.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=asdict(outcomes[0]).keys(),
            lineterminator="\n",
        )
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
        "mean_factory_resume_delay_seconds": round(mean(item.factory_resume_delay for item in outcomes), 2),
        "tech_rejections": sum(item.tech_rejections for item in outcomes),
    }
    if isinstance(outcomes[0], V55Outcome):
        summary.update({
            "observer_decoys": sum(item.observer_decoy for item in outcomes),
            "minor_contacts": sum(item.minor_contact for item in outcomes),
            "natural_followups": sum(item.natural_followup for item in outcomes),
            "reaction_orders": sum(item.reaction_orders for item in outcomes),
            "mean_marine_count": round(mean(item.marine_count for item in outcomes), 2),
            "prepared_naturals": sum(item.siege_posts_ready >= 2 for item in outcomes),
            "mine_screens": sum(item.mine_screen >= 2 for item in outcomes),
        })
    if isinstance(outcomes[0], V56Outcome):
        summary.update({
            "retreating_baits": sum(item.retreating_bait for item in outcomes),
            "storm_pressure_cases": sum(item.storm_pressure for item in outcomes),
            "chase_orders": sum(item.chase_orders for item in outcomes),
            "minimum_sieged_cover": min(item.minimum_sieged_cover for item in outcomes),
            "maximum_tank_gap": max(item.max_tank_gap for item in outcomes),
            "leapfrog_bounds": sum(item.leapfrog_bounds for item in outcomes),
        })
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    return summary


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--runs", type=int, default=100)
    parser.add_argument("--seed", type=int, default=520053)
    parser.add_argument(
        "--policy",
        choices=(
            "v52", "v53", "v54", "v54_observed", "v55",
            "v55_chase", "v56",
        ),
        default="v52",
    )
    parser.add_argument("--output", type=Path, default=None)
    args = parser.parse_args()
    if args.runs < 1:
        raise SystemExit("--runs must be positive")
    output = args.output or Path(f"simulation/results/{args.policy}")
    simulate = {
        "v52": simulate_v52,
        "v53": simulate_v53,
        "v54": simulate_v54,
        "v54_observed": simulate_v54_observed,
        "v55": simulate_v55,
        "v55_chase": simulate_v55_chase,
        "v56": simulate_v56,
    }[args.policy]
    scenario_set = (
        v56_scenarios(args.runs, args.seed)
        if args.policy in {"v55_chase", "v56"}
        else v55_scenarios(args.runs, args.seed)
        if args.policy in {"v54_observed", "v55"}
        else scenarios(args.runs, args.seed)
    )
    outcomes = [simulate(item) for item in scenario_set]
    summary = write_results(outcomes, output, args.policy)
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
