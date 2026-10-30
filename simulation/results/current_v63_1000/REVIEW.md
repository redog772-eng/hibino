# v63 compared with v62 under the same bounded model

Both versions were run with the same adapter, runner, mobile-test checks and
seed 620002. v62 source was extracted from its published installer ZIP without
changing the checkout. Sources and model hashes are recorded in both summaries.

| Outcome | v62 | v63 |
| --- | ---: | ---: |
| Core ready by frame 18,000 | 978/1,000 | 1,000/1,000 |
| Siege never started | 22 | 0 |
| First Tank later than v62 | — | 0/1,000 |

The first Tank completion frame is identical in every paired case. The 22
previously incomplete scenarios all finish in v63, with no formerly completed
case becoming incomplete. Among the 978 cases that finish in both versions,
mean readiness is 6.34 frames earlier in v63; this small change is not evidence
of a meaningful combat improvement. Across all cases, v63's second Tank is
about 49 frames later on average because research receives gas priority.

An independent seed (630001) also reaches readiness in 1,000/1,000 cases; its
summary and scenarios are in `current_v63_validation_1000`.

The change reserves 150 gas against subsequent Tank/Medic spending once the
first Tank exists and until Siege research starts. A discarded implementation
also reserved gas before the first Tank; comparison found an unacceptable
first-Tank delay of up to 2,508 frames. Only the adjusted implementation is
packaged and recorded in these final results.

Readiness is two completed Tanks plus completed Siege research. This is not
a win-rate study. No enemy army, terrain combat, expansion planner or full gas
assignment director is simulated. Full Windows build and actual Pluto matches
remain unverified. Existing movement/boarding behavior is unchanged by v63.
