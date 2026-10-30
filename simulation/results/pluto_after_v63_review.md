# Replay uploaded after v63 delivery

Input: `STARTcraft_vs_Pluto.rep`, 83,568 bytes. Official screp v1.13.8 parses
Destination 1.1, Melee, 16,158 frames, displayed duration 11:18, and reports
Pluto's team as winner. No construction/breach/siege-line CSV was supplied.
The executable version is not identifiable from the replay chat; v63 is the
delivery context, not independently confirmed binary provenance.

## Command evidence

- Terran train requests: 26 SCVs, 10 Marines, 3 Vultures, 17 Tanks. These are
  requests, not surviving unit counts or a damage/loss measurement.
- No additional Command Center construction request. Two Factories, an Armory
  and a Starport are requested. Repeated Depot requests cannot be classified
  as construction failures without the construction CSV and engine state.
- First Factory request frame 4,410, first Vulture 5,838, first Machine Shop
  6,306, first Tank and Siege research both 6,918, first Siege command 8,130.
- Seventeen Siege commands and one Unsiege command. Only two Tank tags show
  movement requests in the extracted histories; the later Tank tags receive
  Siege orders without explicit movement orders. Production rally orders can
  still cause engine movement, so this does not prove all Tanks were stationary.
- The initial Tank staging move targets (1,632, 3,840). The one subsequent
  bound targets (1,547, 3,644). Its Tank is ordered to siege 48 frames later.
  The replay does not establish its exact position when Siege was ordered.
- Zero Bunker PickupBunker requests. Marine-side right-click requests occur,
  consistent with the boarding change, but successful boarding counts are not
  available from these commands alone.
- One same-unit/same-frame Move -> Hold override; no such attack -> Hold
  overrides. This is not a measure of total command interference.
- Pluto requests Zealots, Dragoons, Observers, Reavers, Corsairs, a Shuttle,
  High Templar and Dark Templar. No Carrier request appears.

## Source findings and v64 changes

The natural Tank controller and mobile screen used different objectives to
select a forward Tank. Reinforcements could siege beside a nearby rear Tank
rather than joining the forward line. The bound planner projected a straight
line; a globally reachable destination did not make its sampled straight segment
walkable around a bend. The 105-pixel stopping radius also reduced short-bound
progress. These source findings are plausible contributors, not a proven full
causal reconstruction of this match.

v64 uses a shared natural ground-distance field, routes short bounds around
corners, checks a 32-pixel footprint, brings mobile reinforcements toward the
front Tank, and requires closer arrival during natural bounds. Contact, cover,
screen and defensive readiness checks remain. The live grid adapter is checked
against real BWAPI headers; it has not been exercised in StarCraft. Cache
refresh, dynamic collisions, enemy timing and the full formation controller
remain outside the bounded route model.

Both current-version opening seeds finish 1,000/1,000; each route seed has
900 reachable cases arriving, 100 disconnected cases held, zero stalls and
zero measured safety violations. These are component models, not match wins.
The next match should include all three CSVs to identify whether route,
contact, cover or screen prerequisites still prevent natural preparation.
