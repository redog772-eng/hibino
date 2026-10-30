#include <cstdio>
#include <set>
#include <map>
// V32: expansion garrison IDs, shared by attack and siege controllers.
static std::set<int> g_v32GuardIds;
// V56: Tanks controlled as one siege line. Older movement controllers must
// not issue individual chase, unsiege, traffic-clear or stuck-unit orders.
static std::set<int> g_v56SiegeLineIds;
static int g_v57NoLoadUntil=-1;
// STARTCRAFT_AI_V39_COMMAND_OWNERSHIP: construction reservations are explicit
// state, not inferred from a last-command snapshot that other directors can alter.
static std::map<int,int> g_v39BuilderUntil;
static int g_v39OwnershipFrame=-1;
#include <fstream>
#include <ctime>
#include <direct.h>
#include <ctime>
#include "StarterBot.h"
#include "Tools.h"
#include "MapTools.h"

// STARTCRAFT_AI_V35_3_BUILDER_OWNERSHIP
namespace {
static bool AIV353ReservedBuilder(BWAPI::Unit worker) {
    if(!worker || !worker->exists() || !worker->isCompleted() ||
       worker->getType()!=BWAPI::UnitTypes::Terran_SCV)return false;
    const int now=BWAPI::Broodwar->getFrameCount();
    if(now<g_v39OwnershipFrame)g_v39BuilderUntil.clear();
    g_v39OwnershipFrame=now;
    const auto held=g_v39BuilderUntil.find(worker->getID());
    if(held!=g_v39BuilderUntil.end() && now<=held->second)return true;
    const auto command=worker->getLastCommand();
    if(command.getType()!=BWAPI::UnitCommandTypes::Build)return false;
    const int age=now-worker->getLastCommandFrame();
    const int limit=command.getUnitType()==BWAPI::UnitTypes::Terran_Command_Center?1080:720;
    return age>=0 && age<limit;
}
}


// STARTCRAFT_AI_V1: basic Terran barracks / marine / attack behavior.
namespace {
int g_lastBarracksCommandFrame = -10000;

void AIV1BuildBarracks()
{
    auto& game = BWAPI::Broodwar;
    auto me = game->self();
    if (!me || me->getRace() != BWAPI::Races::Terran) return;

    const auto & units = me->getUnits();
    const int workers = Tools::CountUnitsOfType(BWAPI::UnitTypes::Terran_SCV, units);
    const int barracks = Tools::CountUnitsOfType(BWAPI::UnitTypes::Terran_Barracks, units);
    const int desiredBarracks = (workers >= 16 ? 2 : 1);

    // Start after the first depot is completed; wait until economy is established.
    if (workers < 10 || barracks >= desiredBarracks || me->minerals() < 150) return;
    if (!Tools::GetUnitOfType(BWAPI::UnitTypes::Terran_Supply_Depot)) return;

    const int frame = game->getFrameCount();
    if (frame - g_lastBarracksCommandFrame < 240) return;

    // A worker might still be walking to construct a barracks.
    for (auto worker : units)
    {
        if (worker->getType() != BWAPI::UnitTypes::Terran_SCV) continue;
        const auto command = worker->getLastCommand();
        if (command.getType() == BWAPI::UnitCommandTypes::Build &&
            command.getUnitType() == BWAPI::UnitTypes::Terran_Barracks &&
            frame - worker->getLastCommandFrame() < 360) return;
    }

    const auto tile = game->getBuildLocation(BWAPI::UnitTypes::Terran_Barracks,
                                              me->getStartLocation(), 64, false);
    if (!tile.isValid()) return;

    for (auto worker : units)
    {
        if (worker->getType() != BWAPI::UnitTypes::Terran_SCV ||
            !worker->isCompleted() || worker->isConstructing() ||
            worker->isRepairing()) continue;
        if (worker->build(BWAPI::UnitTypes::Terran_Barracks, tile))
        {
            g_lastBarracksCommandFrame = frame;
            game->printf("AI v1: constructing barracks");
            return;
        }
    }
}

void AIV1TrainMarines()
{
    auto& game = BWAPI::Broodwar;
    auto me = game->self();
    if (!me || me->getRace() != BWAPI::Races::Terran) return;

    const auto & units = me->getUnits();
    if (Tools::CountUnitsOfType(BWAPI::UnitTypes::Terran_Marine, units) >= 60) return;
    if (me->minerals() < 50 || me->supplyTotal() - me->supplyUsed() < 2) return;

    for (auto unit : units)
    {
        if (unit->getType() == BWAPI::UnitTypes::Terran_Barracks &&
            unit->isCompleted() && !unit->isTraining() && me->minerals() >= 50)
        {
            unit->train(BWAPI::UnitTypes::Terran_Marine);
        }
    }
}

void AIV1AttackWithMarines()
{
    auto& game = BWAPI::Broodwar;
    auto me = game->self();
    if (!me || me->getRace() != BWAPI::Races::Terran) return;

    const auto & units = me->getUnits();
    const int marines = Tools::CountUnitsOfType(BWAPI::UnitTypes::Terran_Marine, units);
    if (marines < 6) return;

    BWAPI::Position target = BWAPI::Positions::None;
    for (auto enemy : game->enemies())
    {
        for (auto enemyUnit : enemy->getUnits())
        {
            if (enemyUnit->exists() && enemyUnit->isVisible() &&
                enemyUnit->getType().isBuilding())
            {
                target = enemyUnit->getPosition();
                break;
            }
        }
        if (target.isValid()) break;
    }

    // When we have not found the enemy, search the other possible spawn points.
    if (!target.isValid())
    {
        for (const auto & spawn : game->getStartLocations())
        {
            if (spawn == me->getStartLocation()) continue;
            if (!game->isExplored(spawn))
            {
                target = BWAPI::Position(spawn);
                break;
            }
        }
    }
    if (!target.isValid()) return;

    // Only give idle marines a new order, so combat micro is not interrupted.
    for (auto unit : units)
    {
        if (unit->getType() == BWAPI::UnitTypes::Terran_Marine &&
            unit->isCompleted() && unit->isIdle())
        {
            unit->attack(target);
        }
    }
}
} // namespace
// STARTCRAFT_AI_V2: first basic Terran gas/factory/tank progression.
// This extends (does not replace) the working v1 economy and marine opening.
namespace {
int g_v2LastRefineryOrder = -10000;
int g_v2LastFactoryOrder = -10000;

void AIV2BuildRefinery()
{
    auto& game = BWAPI::Broodwar;
    auto me = game->self();
    if (!me || me->getRace() != BWAPI::Races::Terran) return;

    const auto& units = me->getUnits();
    if (Tools::CountUnitsOfType(BWAPI::UnitTypes::Terran_Refinery, units) > 0) return;
    if (Tools::CountUnitsOfType(BWAPI::UnitTypes::Terran_SCV, units) < 12 || me->minerals() < 100) return;
    const int frame = game->getFrameCount();
    if (frame - g_v2LastRefineryOrder < 240) return;

    const BWAPI::Position home(me->getStartLocation());
    for (auto geyser : game->getGeysers())
    {
        if (!geyser || !geyser->exists() || geyser->getDistance(home) > 550) continue;
        for (auto worker : units)
        {
            if (worker->getType() != BWAPI::UnitTypes::Terran_SCV || !worker->isCompleted()
                || worker->isConstructing() || worker->isRepairing()) continue;
            if (worker->build(BWAPI::UnitTypes::Terran_Refinery, geyser->getTilePosition()))
            {
                g_v2LastRefineryOrder = frame;
                game->printf("AI v2: refinery order");
                return;
            }
        }
    }
}

void AIV2GatherGas()
{
    auto& game = BWAPI::Broodwar;
    auto me = game->self();
    if (!me || me->getRace() != BWAPI::Races::Terran) return;
    auto refinery = Tools::GetUnitOfType(BWAPI::UnitTypes::Terran_Refinery);
    if (!refinery) return;
    const int frame = game->getFrameCount();
    if (frame % 24 != 0) return;

    int assigned = 0;
    for (auto worker : me->getUnits())
    {
        if (worker->getType() != BWAPI::UnitTypes::Terran_SCV) continue;
        if (worker->isGatheringGas() || worker->getLastCommand().getTarget() == refinery)
            ++assigned;
    }
    if (assigned >= 3) return;

    for (auto worker : me->getUnits())
    {
        if (assigned >= 3) break;
        if (worker->getType() != BWAPI::UnitTypes::Terran_SCV || !worker->isCompleted()
            || worker->isGatheringGas() || worker->isConstructing() || worker->isRepairing()) continue;
        if (worker->getLastCommand().getTarget() == refinery) continue;
        if (worker->rightClick(refinery)) ++assigned;
    }
}

void AIV2BuildFactory()
{
    auto& game = BWAPI::Broodwar;
    auto me = game->self();
    if (!me || me->getRace() != BWAPI::Races::Terran) return;
    const auto& units = me->getUnits();
    if (!Tools::GetUnitOfType(BWAPI::UnitTypes::Terran_Barracks)) return;
    if (Tools::CountUnitsOfType(BWAPI::UnitTypes::Terran_Factory, units) > 0) return;
    if (me->minerals() < 200 || me->gas() < 100) return;

    const int frame = game->getFrameCount();
    if (frame - g_v2LastFactoryOrder < 240) return;
    const auto tile = game->getBuildLocation(BWAPI::UnitTypes::Terran_Factory,
                                               me->getStartLocation(), 64, false);
    if (!tile.isValid()) return;

    for (auto worker : units)
    {
        if (worker->getType() != BWAPI::UnitTypes::Terran_SCV || !worker->isCompleted()
            || worker->isConstructing() || worker->isRepairing()) continue;
        if (worker->build(BWAPI::UnitTypes::Terran_Factory, tile))
        {
            g_v2LastFactoryOrder = frame;
            game->printf("AI v2: factory order");
            return;
        }
    }
}

void AIV2BuildMachineShopAndTanks()
{
    auto& game = BWAPI::Broodwar;
    auto me = game->self();
    if (!me || me->getRace() != BWAPI::Races::Terran) return;
    if (game->getFrameCount() % 12 != 0) return;

    for (auto factory : me->getUnits())
    {
        if (factory->getType() != BWAPI::UnitTypes::Terran_Factory || !factory->isCompleted()) continue;
        auto addon = factory->getAddon();
        if (!addon)
        {
            if (me->minerals() >= 50 && me->gas() >= 50)
                factory->buildAddon(BWAPI::UnitTypes::Terran_Machine_Shop);
            continue;
        }
        if (!addon->isCompleted()) continue;
        if (me->minerals() >= 150 && me->gas() >= 100
            && me->supplyTotal() - me->supplyUsed() >= 4
            && !factory->isTraining())
        {
            factory->train(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode);
        }
    }
}

void AIV2AttackWithTanks()
{
    auto& game = BWAPI::Broodwar;
    auto me = game->self();
    if (!me || me->getRace() != BWAPI::Races::Terran) return;
    if (game->getFrameCount() % 24 != 0) return;

    const auto& units = me->getUnits();
    const int tanks = Tools::CountUnitsOfType(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode, units);
    const int marines = Tools::CountUnitsOfType(BWAPI::UnitTypes::Terran_Marine, units);
    if (tanks < 1 && marines < 6) return;

    BWAPI::Position destination = BWAPI::Positions::None;
    // Visible enemy buildings make useful long-term attack targets.
    for (auto enemy : game->enemies())
    {
        for (auto enemyUnit : enemy->getUnits())
        {
            if (enemyUnit->exists() && enemyUnit->isVisible() && enemyUnit->getType().isBuilding())
            {
                destination = enemyUnit->getPosition();
                break;
            }
        }
        if (destination.isValid()) break;
    }
    if (!destination.isValid())
    {
        for (auto enemy : game->enemies())
        {
            for (auto enemyUnit : enemy->getUnits())
            {
                if (enemyUnit->exists() && enemyUnit->isVisible())
                {
                    destination = enemyUnit->getPosition();
                    break;
                }
            }
            if (destination.isValid()) break;
        }
    }
    if (!destination.isValid())
    {
        for (const auto& start : game->getStartLocations())
        {
            if (start == me->getStartLocation()) continue;
            if (!game->isExplored(start))
            {
                destination = BWAPI::Position(start);
                break;
            }
        }
    }
    if (!destination.isValid()) return;

    for (auto unit : units)
    {
        if (unit->getType() != BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode
            || !unit->isCompleted() || !unit->isIdle()) continue;
        unit->attack(destination);
    }
}
} // STARTCRAFT_AI_V2 helpers


// STARTCRAFT_AI_V3: experimental Terran siege and sustained production.
// These helpers extend the proven v1/v2 bot; original fast simulation speed is unchanged.
namespace {
int g_v3LastFactoryOrder = -10000;

// Add a second production facility once the first factory is operational.
void AIV3BuildSecondFactory()
{
    auto& game = BWAPI::Broodwar;
    auto me = game->self();
    if (!me || me->getRace() != BWAPI::Races::Terran) return;
    const int frame = game->getFrameCount();
    if (frame < g_v3LastFactoryOrder) g_v3LastFactoryOrder = -10000;
    if (frame - g_v3LastFactoryOrder < 240 || frame % 24 != 0) return;

    const auto& units = me->getUnits();
    if (Tools::CountUnitsOfType(BWAPI::UnitTypes::Terran_Factory, units) != 1) return;
    if (Tools::CountUnitsOfType(BWAPI::UnitTypes::Terran_SCV, units) < 17) return;
    if (me->minerals() < 350 || me->gas() < 220) return;

    bool firstFactoryFinished = false;
    for (auto unit : units)
        if (unit->getType() == BWAPI::UnitTypes::Terran_Factory && unit->isCompleted())
            firstFactoryFinished = true;
    if (!firstFactoryFinished) return;

    const auto tile = game->getBuildLocation(BWAPI::UnitTypes::Terran_Factory,
                                              me->getStartLocation(), 64, false);
    if (!tile.isValid()) return;

    for (auto worker : units)
    {
        if (worker->getType() != BWAPI::UnitTypes::Terran_SCV || !worker->isCompleted()
            || worker->isConstructing() || worker->isRepairing() || worker->isGatheringGas()) continue;
        if (worker->build(BWAPI::UnitTypes::Terran_Factory, tile))
        {
            g_v3LastFactoryOrder = frame;
            game->printf("AI v3: second factory started");
            return;
        }
    }
}

// Upgrade at the existing machine shop. Each tank can then enter siege mode.
void AIV3ResearchSiegeMode()
{
    auto& game = BWAPI::Broodwar;
    auto me = game->self();
    if (!me || me->getRace() != BWAPI::Races::Terran) return;
    if (game->getFrameCount() % 24 != 0) return;
    const auto tech = BWAPI::TechTypes::Tank_Siege_Mode;
    if (me->hasResearched(tech) || me->isResearching(tech)) return;
    if (me->minerals() < 150 || me->gas() < 150) return;
    for (auto unit : me->getUnits())
    {
        if (unit->getType() == BWAPI::UnitTypes::Terran_Machine_Shop && unit->isCompleted()
            && !unit->isResearching())
        {
            if (unit->research(tech)) game->printf("AI v3: researching siege mode");
            return;
        }
    }
}

// Siege near visible enemy ground positions; unsiege to resume advancing when clear.
// STARTCRAFT_AI_V21_TANK_EXIT
// Static terrain heuristic: if a tank is inside a narrow ground corridor
// near the home base, do not newly siege there. Already-sieged tanks leave
// when no visible enemy is nearby or friendly army traffic is crowded.
static int AIV21WalkSpan(BWAPI::Position p, int dx, int dy) {
    auto& game=BWAPI::Broodwar;
    int length=0;
    for (int i=1;i<=12;++i) {
        const int x=(p.x+dx*i*16)/8;
        const int y=(p.y+dy*i*16)/8;
        if (!game->isWalkable(x,y)) break;
        length+=16;
    }
    return length;
}
// STARTCRAFT_AI_V28_CHOKE: apply congestion guard at ENEMY as well as home exit.
// A lane must remain mobile for Marines, Medics and Tanks to pass through.
static bool AIV21TankExitGuard(BWAPI::Unit tank) {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if(!me || !tank || !tank->exists())return false;
    // Expansion garrison Tanks intentionally hold a prepared firing line.
    // Traffic logic must not undo the pre-expansion siege order.
    if(g_v32GuardIds.count(tank->getID()) ||
       g_v56SiegeLineIds.count(tank->getID()))return false;
    BWAPI::Position p=tank->getPosition();
    BWAPI::Position home(me->getStartLocation());
    // All candidate opponent starts are safe proxies here. This helper is
    // declared before v6 and v7 in the original source, so it must NOT refer
    // to their later-defined global symbols.
    int otherStartDist=999999;
    for(auto start:game->getStartLocations()) {
        if(start==me->getStartLocation())continue;
        // BWAPI Position::getDistance can return double; std::min(int,double) fails MSVC C2672.
        const int distancePixels=static_cast<int>(p.getDistance(BWAPI::Position(start)));
        if(distancePixels<otherStartDist)otherStartDist=distancePixels;
    }
    const int homeDist=p.getDistance(home);
    const bool nearExit=(homeDist>=160 && homeDist<1350);
    const bool nearEnemyEntrance=(otherStartDist>=180 && otherStartDist<1200);
    if(!nearExit && !nearEnemyEntrance)return false;
    const int acrossX=AIV21WalkSpan(p,1,0)+AIV21WalkSpan(p,-1,0);
    const int acrossY=AIV21WalkSpan(p,0,1)+AIV21WalkSpan(p,0,-1);
    bool narrow=(acrossX<192 && acrossY>=96) || (acrossY<192 && acrossX>=96);
    // Add a diagonal-aware sample; older x/y-only checks missed some ramps.
    if(!narrow){
        const int dx[4]={1,1,1,-1},dy[4]={0,1,-1,0};
        for(int i=1;i<=2 && !narrow;++i){
            int walkable=0;
            for(int k=-5;k<=5;++k){
                const int wx=p.x/8 + dx[i]*k*2;
                const int wy=p.y/8 + dy[i]*k*2;
                if(game->isWalkable(wx,wy))++walkable;
            }
            if(walkable>=3 && walkable<=6)narrow=true;
        }
    }
    int alliedTraffic=0;
    for(auto u:me->getUnits()){
        if(!u || !u->exists() || u==tank || !u->isCompleted() ||
           u->isFlying() || u->getType().isBuilding())continue;
        const auto t=u->getType();
        if((t==BWAPI::UnitTypes::Terran_Marine ||
            t==BWAPI::UnitTypes::Terran_Medic ||
            t==BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode ||
            t==BWAPI::UnitTypes::Terran_SCV) && u->getDistance(tank)<160)
            ++alliedTraffic;
    }
    // At the enemy entrance, a busy assault route has precedence over static
    // firepower. Allow siege once tanks have fully passed through the lane.
    const bool attackLane=nearEnemyEntrance &&
                          alliedTraffic>=2 && otherStartDist<770;
    const bool mustRemainMobile=narrow || attackLane;
    if(!mustRemainMobile)return false;
    if(tank->getType()==BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)
        tank->unsiege();
    return true; // don't re-siege in the same controller on the next tick
}




void AIV3ControlSiegeTanks()
{
    auto& game = BWAPI::Broodwar;
    auto me = game->self();
    if (!me || me->getRace() != BWAPI::Races::Terran) return;
    if (game->getFrameCount() % 24 != 0) return;
    if (!me->hasResearched(BWAPI::TechTypes::Tank_Siege_Mode)) return;

    for (auto tank : me->getUnits())
    {
        const auto type = tank->getType();
        if (type != BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode
            && type != BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode) continue;
        if (!tank->isCompleted()) continue;
        if (g_v56SiegeLineIds.count(tank->getID())) continue;
        if (AIV21TankExitGuard(tank)) continue; // STARTCRAFT_AI_V21_TANK_EXIT

        bool closeGroundEnemy = false;
        for (auto enemy : game->enemies())
        {
            for (auto enemyUnit : enemy->getUnits())
            {
                if (!enemyUnit || !enemyUnit->exists() || !enemyUnit->isVisible()
                    || enemyUnit->isFlying()) continue;
                const int distance = tank->getDistance(enemyUnit);
                if (distance > 112 && distance < 340)
                {
                    closeGroundEnemy = true;
                    break;
                }
            }
            if (closeGroundEnemy) break;
        }

        if (tank->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode && closeGroundEnemy)
        {
            tank->siege();
        }
        else if (tank->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode
                 && !closeGroundEnemy && !g_v32GuardIds.count(tank->getID()))
        {
            // If all known nearby ground targets are gone, keep the army mobile.
            tank->unsiege();
        }
    }
}

// Repair a heavily damaged tank when a nearby worker can do so safely.
void AIV3RepairTank()
{
    auto& game = BWAPI::Broodwar;
    auto me = game->self();
    if (!me || me->getRace() != BWAPI::Races::Terran) return;
    if (game->getFrameCount() % 48 != 0 || me->minerals() < 40) return;
    const auto& units = me->getUnits();

    for (auto tank : units)
    {
        if (tank->getType() != BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode
            && tank->getType() != BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode) continue;
        if (!tank->isCompleted() || tank->getHitPoints() + 90 >= tank->getType().maxHitPoints()) continue;

        bool alreadyRepairing = false;
        for (auto worker : units)
            if (worker->getType() == BWAPI::UnitTypes::Terran_SCV && worker->isRepairing()
                && worker->getOrderTarget() == tank) alreadyRepairing = true;
        if (alreadyRepairing) continue;

        for (auto worker : units)
        {
            if (worker->getType() != BWAPI::UnitTypes::Terran_SCV || !worker->isCompleted()
                || worker->isConstructing() || worker->isRepairing() || worker->isGatheringGas()
                || AIV353ReservedBuilder(worker) || worker->getDistance(tank) > 230) continue;
            if (worker->repair(tank)) return;
        }
    }
}
} // STARTCRAFT_AI_V3 helpers


// STARTCRAFT_AI_V4: staged waves instead of one-at-a-time attacks.
// v1 marine and v2 tank attack calls must be DISABLED in onFrame.
// This code intentionally preserves v3 production, siege and repair routines.
#include <set>
namespace {
std::set<int> g_v4Dispatched;
int g_v4LastFrame = -1;
int g_v4LastWaveFrame = -10000;
int g_v4WaveNumber = 0;
BWAPI::Position g_v4LastTarget = BWAPI::Positions::None;

bool AIV4IsCombatUnit(BWAPI::Unit unit)
{
    if (!unit || !unit->exists() || !unit->isCompleted()) return false;
    const auto t = unit->getType();
    return t == BWAPI::UnitTypes::Terran_Marine
        || t == BWAPI::UnitTypes::Terran_Medic
        || t == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode
        || t == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode;
}

bool AIV4IsTank(BWAPI::Unit unit)
{
    const auto t = unit->getType();
    return t == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode
        || t == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode;
}

BWAPI::Position AIV4ChooseRally(BWAPI::Player me)
{
    auto& game = BWAPI::Broodwar;
    const BWAPI::Position home(me->getStartLocation());
    const int mapX = game->mapWidth() * 16;
    const int mapY = game->mapHeight() * 16;
    const int sx = mapX >= home.x ? 1 : -1;
    const int sy = mapY >= home.y ? 1 : -1;
    // Prefer open ground between the command center and map center.
    const int distances[] = { 224, 288, 160, 352, 112 };
    const int offsets[] = { 0, 96, -96, 160, -160 };
    for (int d : distances)
    {
        for (int side : offsets)
        {
            const BWAPI::Position pos(home.x + sx*d + side,
                                      home.y + sy*d - side);
            if (pos.x < 40 || pos.y < 40 || pos.x >= game->mapWidth()*32 - 40
                || pos.y >= game->mapHeight()*32 - 40) continue;
            if (!game->isWalkable(pos.x / 8, pos.y / 8)) continue;
            bool obstructed = false;
            for (auto nearby : game->getUnitsInRadius(pos, 96))
            {
                if (nearby && nearby->exists() && nearby->getType().isBuilding())
                {
                    obstructed = true;
                    break;
                }
            }
            if (obstructed) continue;
            // Ensure at least one of our ground units can reach the staging point.
            for (auto unit : me->getUnits())
            {
                if (AIV4IsCombatUnit(unit) && unit->hasPath(pos)) return pos;
            }
            // In early game, no combat unit exists yet: keep looking on next update.
        }
    }
    // Safe fallback: our initial base area is typically reachable.
    return home;
}

BWAPI::Position AIV4ChooseTarget()
{
    auto& game = BWAPI::Broodwar;
    auto me = game->self();
    if (!me) return BWAPI::Positions::None;
    // Prioritize known enemy buildings; keeps troops focused on finishing the game.
    for (auto enemy : game->enemies())
        for (auto unit : enemy->getUnits())
            if (unit && unit->exists() && unit->isVisible() && unit->getType().isBuilding())
                return unit->getPosition();

    for (auto enemy : game->enemies())
        for (auto unit : enemy->getUnits())
            if (unit && unit->exists() && unit->isVisible())
                return unit->getPosition();

    for (const auto& spawn : game->getStartLocations())
        if (spawn != me->getStartLocation() && !game->isExplored(spawn))
            return BWAPI::Position(spawn);
    return g_v4LastTarget;
}

bool AIV4EnemyCloseTo(BWAPI::Unit soldier, int distance)
{
    for (auto enemy : BWAPI::Broodwar->enemies())
        for (auto unit : enemy->getUnits())
            if (unit && unit->exists() && unit->isVisible()
                && soldier->getDistance(unit) <= distance) return true;
    return false;
}

void AIV4RallyAndAttack()
{
    auto& game = BWAPI::Broodwar;
    auto me = game->self();
    if (!me || me->getRace() != BWAPI::Races::Terran) return;
    const int frame = game->getFrameCount();
    if (frame < g_v4LastFrame)
    {
        g_v4Dispatched.clear();
        g_v4LastWaveFrame = -10000;
        g_v4LastTarget = BWAPI::Positions::None;
        g_v4WaveNumber = 0;
    }
    g_v4LastFrame = frame;
    if (frame % 24 != 0) return;

    const auto& units = me->getUnits();
    std::set<int> alive;
    for (auto unit : units)
        if (AIV4IsCombatUnit(unit)) alive.insert(unit->getID());
    for (auto it = g_v4Dispatched.begin(); it != g_v4Dispatched.end(); )
    {
        if (alive.count(*it) == 0) it = g_v4Dispatched.erase(it);
        else ++it;
    }

    const BWAPI::Position rally = AIV4ChooseRally(me);
    int readyMarines = 0;
    int readyTanks = 0;
    for (auto unit : units)
    {
        if (!AIV4IsCombatUnit(unit) || g_v4Dispatched.count(unit->getID())) continue;
        // Newly produced soldiers gather rather than immediately attacking.
        if (AIV4EnemyCloseTo(unit, 180)) continue; // auto-defend locally
        if (unit->getDistance(rally) <= 170)
        {
            if (AIV4IsTank(unit)) ++readyTanks;
            else if (unit->getType() == BWAPI::UnitTypes::Terran_Marine) ++readyMarines;
        }
        else if (unit->isIdle() || frame - unit->getLastCommandFrame() > 120)
        {
            if (unit->getType() != BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)
                unit->move(rally);
        }
    }

    BWAPI::Position target = AIV4ChooseTarget();
    if (target.isValid()) g_v4LastTarget = target;
    else target = g_v4LastTarget;

    // Launch only a useful-sized wave. Subsequent reinforcements assemble anew.
    // After a long wait, allow a larger marine-only wave if tanks are unavailable.
    const bool balancedWave = readyMarines >= 12 && readyTanks >= 2;
    const bool marineWave = readyMarines >= 20 && readyTanks == 0;
    const bool heavyWave = readyMarines >= 6 && readyTanks >= 4;
    if (target.isValid() && frame - g_v4LastWaveFrame >= 240
        && (balancedWave || marineWave || heavyWave))
    {
        int sent = 0;
        BWAPI::Unit waveLead = nullptr;
        for (auto candidate : units)
        {
            if (candidate->getType() == BWAPI::UnitTypes::Terran_Marine
                && candidate->isCompleted() && !g_v4Dispatched.count(candidate->getID())
                && candidate->getDistance(rally) <= 170)
            {
                waveLead = candidate;
                break;
            }
        }
        for (auto unit : units)
        {
            if (!AIV4IsCombatUnit(unit) || g_v4Dispatched.count(unit->getID())
                || unit->getDistance(rally) > 170) continue;
            if (unit->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)
                unit->unsiege();
            bool ordered = false;
            if (unit->getType() == BWAPI::UnitTypes::Terran_Medic)
                ordered = waveLead && unit->move(waveLead->getPosition());
            else
                ordered = unit->attack(target);
            if (ordered)
            {
                g_v4Dispatched.insert(unit->getID());
                ++sent;
            }
        }
        if (sent > 0)
        {
            ++g_v4WaveNumber;
            g_v4LastWaveFrame = frame;
            game->printf("AI v4: wave %d launched, %d soldiers", g_v4WaveNumber, sent);
        }
    }

    // Keep the earlier units advancing; newly produced units are NOT dispatched
    // until they form another wave. Do not override units fighting nearby enemies.
    for (auto unit : units)
    {
        if (!AIV4IsCombatUnit(unit) || !g_v4Dispatched.count(unit->getID())) continue;
        if (unit->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode) continue;
        if (unit->getType() == BWAPI::UnitTypes::Terran_Medic)
        {
            BWAPI::Unit leader = nullptr;
            int closest = 1000000;
            for (auto marine : units)
            {
                if (marine->getType() != BWAPI::UnitTypes::Terran_Marine
                    || !marine->isCompleted() || !g_v4Dispatched.count(marine->getID())) continue;
                const int d = unit->getDistance(marine);
                if (d < closest) { closest = d; leader = marine; }
            }
            if (leader && ((unit->isIdle() && closest > 96)
                || (closest > 170 && frame - unit->getLastCommandFrame() > 72)))
                unit->move(leader->getPosition());
            continue;
        }
        if (AIV4EnemyCloseTo(unit, 260)) continue;

        // Marines should not outrun deployed tanks by the entire length of the map.
        if (unit->getType() == BWAPI::UnitTypes::Terran_Marine)
        {
            BWAPI::Unit nearestTank = nullptr;
            int nearestDistance = 1000000;
            for (auto tank : units)
                if (AIV4IsCombatUnit(tank) && AIV4IsTank(tank)
                    && g_v4Dispatched.count(tank->getID()))
                {
                    const int d = unit->getDistance(tank);
                    if (d < nearestDistance) { nearestTank = tank; nearestDistance = d; }
                }
            const BWAPI::Position home(me->getStartLocation());
            if (nearestTank && nearestDistance > 280
                && unit->getDistance(home) > nearestTank->getDistance(home) + 240)
            {
                if (unit->isIdle() || frame - unit->getLastCommandFrame() >= 96)
                    unit->move(nearestTank->getPosition());
                continue;
            }
        }

        // Units that finished a move, including marines that waited for tanks,
        // resume attack-move toward the latest visible enemy base.
        if (target.isValid()
            && (unit->isIdle() ||
                (unit->getLastCommand().getType() == BWAPI::UnitCommandTypes::Move
                 && frame - unit->getLastCommandFrame() >= 72)))
        {
            unit->attack(target);
        }
    }
}
} // END STARTCRAFT_AI_V4


// STARTCRAFT_AI_V5: supply planning, Academy/Medics, barracks scaling, SCV scout.
// Only the Terran routines are altered. The AI keeps its existing fast speed.
namespace {
int g_v5LastSupplyFrame = -10000;
int g_v5LastAcademyFrame = -10000;
int g_v5LastExtraRaxFrame = -10000;
int g_v5ScoutId = -1;
int g_v5LastScoutFrame = -10000;
int g_v5PreviousFrame = -1;
bool g_v5ScoutFinished = false;

bool AIV5IsAvailableWorker(BWAPI::Unit worker)
{
    return worker && worker->exists() && worker->isCompleted()
        && worker->getType() == BWAPI::UnitTypes::Terran_SCV
        && worker->getID() != g_v5ScoutId
        && !worker->isConstructing() && !worker->isRepairing()
        && !worker->isGatheringGas();
}

// BWAPI supply units are twice the values shown in the StarCraft UI:
// a Marine uses 2 API supply; a Tank uses 4 API supply.
void AIV5ManageSupply()
{
    auto& game = BWAPI::Broodwar;
    auto me = game->self();
    if (!me || me->getRace() != BWAPI::Races::Terran) return;
    const int frame = game->getFrameCount();
    if (frame % 12 != 0 || frame - g_v5LastSupplyFrame < 96) return;
    if (me->supplyTotal() >= 400 || me->minerals() < 100) return;

    int production = 0;
    int unfinished = 0;
    for (auto unit : me->getUnits())
    {
        const auto type = unit->getType();
        if (type == BWAPI::UnitTypes::Terran_Barracks
            || type == BWAPI::UnitTypes::Terran_Factory
            || type == BWAPI::UnitTypes::Terran_Command_Center) ++production;
        if (type == BWAPI::UnitTypes::Terran_Supply_Depot && !unit->isCompleted()) ++unfinished;
    }
    // Maintain about 6-10 normal supply free, increasing with production buildings.
    const int freeSupply = me->supplyTotal() - me->supplyUsed();
    const int earlyWarning = 12 + ((production > 5 ? 5 : production) * 2);
    if (freeSupply > earlyWarning) return;
    // One depot in progress is usually sufficient; make a second if dangerously low.
    if (unfinished >= (freeSupply <= 4 ? 2 : 1)) return;
    if (Tools::BuildBuilding(BWAPI::UnitTypes::Terran_Supply_Depot))
    {
        g_v5LastSupplyFrame = frame;
        game->printf("AI v5: early supply depot (free: %d)", freeSupply / 2);
    }
}

void AIV5BuildAcademy()
{
    auto& game = BWAPI::Broodwar;
    auto me = game->self();
    if (!me || me->getRace() != BWAPI::Races::Terran) return;
    const int frame = game->getFrameCount();
    if (frame % 24 != 0 || frame - g_v5LastAcademyFrame < 240) return;
    if (Tools::CountUnitsOfType(BWAPI::UnitTypes::Terran_Academy, me->getUnits()) > 0) return;
    if (!Tools::GetUnitOfType(BWAPI::UnitTypes::Terran_Barracks)) return;
    if (Tools::CountUnitsOfType(BWAPI::UnitTypes::Terran_SCV, me->getUnits()) < 13) return;
    // Reserve some money when supply is close to blocked.
    if (me->minerals() < (me->supplyTotal() - me->supplyUsed() <= 6 ? 250 : 150)) return;

    const BWAPI::TilePosition tile = game->getBuildLocation(
        BWAPI::UnitTypes::Terran_Academy, me->getStartLocation(), 64, false);
    if (!tile.isValid()) return;
    for (auto worker : me->getUnits())
    {
        if (!AIV5IsAvailableWorker(worker)) continue;
        if (worker->build(BWAPI::UnitTypes::Terran_Academy, tile))
        {
            g_v5LastAcademyFrame = frame;
            game->printf("AI v5: academy started");
            return;
        }
    }
}

void AIV5BuildExtraBarracks()
{
    auto& game = BWAPI::Broodwar;
    auto me = game->self();
    if (!me || me->getRace() != BWAPI::Races::Terran) return;
    const int frame = game->getFrameCount();
    if (frame % 24 != 0 || frame - g_v5LastExtraRaxFrame < 240) return;
    const auto& units = me->getUnits();
    if (Tools::CountUnitsOfType(BWAPI::UnitTypes::Terran_Barracks, units) >= 3) return;
    if (Tools::CountUnitsOfType(BWAPI::UnitTypes::Terran_Factory, units) < 1) return;
    if (Tools::CountUnitsOfType(BWAPI::UnitTypes::Terran_SCV, units) < 20) return;
    if (me->minerals() < 400) return;

    const BWAPI::TilePosition tile = game->getBuildLocation(
        BWAPI::UnitTypes::Terran_Barracks, me->getStartLocation(), 64, false);
    if (!tile.isValid()) return;
    for (auto worker : units)
    {
        if (!AIV5IsAvailableWorker(worker)) continue;
        if (worker->build(BWAPI::UnitTypes::Terran_Barracks, tile))
        {
            g_v5LastExtraRaxFrame = frame;
            game->printf("AI v5: adding barracks");
            return;
        }
    }
}

// Replace v1's Marine-only trainer with an infantry ratio that includes Medics.
void AIV5TrainInfantry()
{
    auto& game = BWAPI::Broodwar;
    auto me = game->self();
    if (!me || me->getRace() != BWAPI::Races::Terran) return;
    if (game->getFrameCount() % 12 != 0) return;
    const auto& units = me->getUnits();
    int marines = Tools::CountUnitsOfType(BWAPI::UnitTypes::Terran_Marine, units);
    int medics = Tools::CountUnitsOfType(BWAPI::UnitTypes::Terran_Medic, units);
    BWAPI::Unit academy = Tools::GetUnitOfType(BWAPI::UnitTypes::Terran_Academy);
    const bool canTrainMedics = academy && academy->isCompleted();

    for (auto rax : units)
    {
        if (rax->getType() != BWAPI::UnitTypes::Terran_Barracks
            || !rax->isCompleted() || rax->isTraining()) continue;
        if (me->supplyTotal() - me->supplyUsed() < 2) return;
        if (me->minerals() < 50) return;

        // Keep at least 2 Medics as soon as infantry/Academy are available;
        // after that aim for approximately one Medic per six Marines.
        int desiredMedics = (marines + 5) / 6;
        if (desiredMedics < 2) desiredMedics = 2;
        if (desiredMedics > 12) desiredMedics = 12;
        if (canTrainMedics && marines >= 6 && medics < desiredMedics && me->gas() >= 25)
        {
            if (rax->train(BWAPI::UnitTypes::Terran_Medic))
            {
                ++medics;
                game->printf("AI v5: medic queued");
                continue;
            }
        }
        if (marines < 60 && rax->train(BWAPI::UnitTypes::Terran_Marine))
            ++marines;
    }
}

// A single SCV explores unvisited enemy spawn sites. It does not cheat by
// accessing unseen enemy units. After visiting start sites, it returns to mine.
void AIV5ScoutWithSCV()
{
    auto& game = BWAPI::Broodwar;
    auto me = game->self();
    if (!me || me->getRace() != BWAPI::Races::Terran) return;
    const int frame = game->getFrameCount();
    if (frame < g_v5PreviousFrame)
    {
        g_v5ScoutId = -1;
        g_v5ScoutFinished = false;
        g_v5LastScoutFrame = -10000;
    }
    g_v5PreviousFrame = frame;
    if (frame % 24 != 0 || frame < 240 || g_v5ScoutFinished) return;
    if (Tools::CountUnitsOfType(BWAPI::UnitTypes::Terran_SCV, me->getUnits()) < 9) return;

    BWAPI::TilePosition unvisited = BWAPI::TilePositions::None;
    for (const auto& spawn : game->getStartLocations())
    {
        if (spawn != me->getStartLocation() && !game->isExplored(spawn))
        {
            unvisited = spawn;
            break;
        }
    }
    if (!unvisited.isValid())
    {
        g_v5ScoutFinished = true;
        g_v5ScoutId = -1; // Idle-worker mining logic resumes.
        game->printf("AI v5: scout explored all spawn positions");
        return;
    }

    BWAPI::Unit scout = nullptr;
    if (g_v5ScoutId >= 0)
    {
        for (auto unit : me->getUnits())
            if (unit->getID() == g_v5ScoutId && unit->exists()) { scout = unit; break; }
        if (!scout) g_v5ScoutId = -1;
    }
    if (!scout)
    {
        for (auto worker : me->getUnits())
            if (AIV5IsAvailableWorker(worker)) { scout = worker; break; }
    }
    if (!scout) return;

    // Only re-issue orders if needed; frequent move commands disrupt pathfinding.
    const BWAPI::Position destination(unvisited);
    if (g_v5ScoutId < 0 || scout->isIdle()
        || (frame - g_v5LastScoutFrame >= 240 && scout->getDistance(destination) > 160))
    {
        if (scout->move(destination))
        {
            if (g_v5ScoutId < 0) game->printf("AI v5: SCV scouting started");
            g_v5ScoutId = scout->getID();
            g_v5LastScoutFrame = frame;
        }
    }
}
} // STARTCRAFT_AI_V5


#include <algorithm>
#include <set>
// STARTCRAFT_AI_V6 -- Fix supply, scouting memory, reactive defense and wave deadlocks.
// Depends on the already-working v5 source. Keeps fast simulation speed.
namespace {
int g_v6LastFrame = -1;
int g_v6LastSupplyOrder = -10000;
int g_v6LastScoutOrder = -10000;
int g_v6LastWaveFrame = -10000;
int g_v6LastHudFrame = -10000;
int g_v6FirstReadyFrame = -1;
int g_v6ScoutStall = 0;
std::set<int> g_v6WaveUnits;
BWAPI::TilePosition g_v6EnemyStart = BWAPI::TilePositions::None;
BWAPI::Position g_v6LastEnemyBuilding = BWAPI::Positions::None;
BWAPI::Position g_v6WaveGoal = BWAPI::Positions::None;
BWAPI::Position g_v6ScoutDestination = BWAPI::Positions::None;
BWAPI::Position g_v6ScoutPrevPos = BWAPI::Positions::None;

void AIV6ResetIfNewMatch()
{
    const int frame = BWAPI::Broodwar->getFrameCount();
    if (frame >= g_v6LastFrame) { g_v6LastFrame = frame; return; }
    g_v6LastFrame = frame;
    g_v6LastSupplyOrder = -10000;
    g_v6LastScoutOrder = -10000;
    g_v6LastWaveFrame = -10000;
    g_v6LastHudFrame = -10000;
    g_v6FirstReadyFrame = -1;
    g_v6ScoutStall = 0;
    g_v6WaveUnits.clear();
    g_v6EnemyStart = BWAPI::TilePositions::None;
    g_v6LastEnemyBuilding = BWAPI::Positions::None;
    g_v6WaveGoal = BWAPI::Positions::None;
    g_v6ScoutDestination = BWAPI::Positions::None;
    g_v6ScoutPrevPos = BWAPI::Positions::None;
    g_v5ScoutId = -1;
}

// Only count visible enemy facts, not hidden opponent start locations.
void AIV6ObserveEnemy()
{
    auto& game = BWAPI::Broodwar;
    auto me = game->self();
    if (!me) return;
    for (auto enemy : game->enemies())
        for (auto unit : enemy->getUnits())
        {
            if (!unit || !unit->exists() || !unit->isVisible()) continue;
            if (unit->getType().isBuilding())
                g_v6LastEnemyBuilding = unit->getPosition();

            if (g_v6EnemyStart.isValid()) continue;
            // Prefer known bases over forward structures and wandering units.
            if (!unit->getType().isBuilding()) continue;
            for (const auto& spawn : game->getStartLocations())
            {
                if (spawn == me->getStartLocation()) continue;
                if (unit->getDistance(BWAPI::Position(spawn)) < 640)
                {
                    g_v6EnemyStart = spawn;
                    game->printf("AI v6: enemy base confirmed from scouting");
                    break;
                }
            }
        }
}

BWAPI::Unit AIV6GetScout()
{
    auto me = BWAPI::Broodwar->self();
    if (!me || g_v5ScoutId < 0) return nullptr;
    for (auto unit : me->getUnits())
        if (unit->exists() && unit->getID() == g_v5ScoutId)
            return unit;
    g_v5ScoutId = -1;
    return nullptr;
}

void AIV6ScoutAndRemember()
{
    auto& game = BWAPI::Broodwar;
    auto me = game->self();
    if (!me || me->getRace() != BWAPI::Races::Terran) return;
    AIV6ResetIfNewMatch();
    const int frame = game->getFrameCount();
    if (frame % 24 != 0) return;
    AIV6ObserveEnemy();
    if (g_v6EnemyStart.isValid())
    {
        // Scout returns to the normal idle-worker mineral routine.
        g_v5ScoutId = -1;
        return;
    }
    if (frame < 240 || Tools::CountUnitsOfType(BWAPI::UnitTypes::Terran_SCV, me->getUnits()) < 9)
        return;

    BWAPI::TilePosition next = BWAPI::TilePositions::None;
    for (const auto& spawn : game->getStartLocations())
    {
        if (spawn != me->getStartLocation() && !game->isExplored(spawn))
        { next = spawn; break; }
    }
    if (!next.isValid())
    {
        // All starts visited: no confirmed enemy base. Don't send the entire army
        // blindly. Preserve any last actually seen enemy structure.
        g_v5ScoutId = -1;
        return;
    }

    auto scout = AIV6GetScout();
    if (!scout)
    {
        for (auto worker : me->getUnits())
            if (AIV5IsAvailableWorker(worker) && !worker->isCarryingMinerals()
                && !worker->isCarryingGas())
            { scout = worker; break; }
    }
    if (!scout) return;
    const BWAPI::Position destination(next);
    if (g_v6ScoutDestination != destination)
    {
        g_v6ScoutDestination = destination;
        g_v6ScoutStall = 0;
        g_v6ScoutPrevPos = scout->getPosition();
        g_v6LastScoutOrder = -10000;
    }
    if (frame - g_v6LastScoutOrder < 96 && !scout->isIdle()) return;

    if (scout->getPosition().getDistance(g_v6ScoutPrevPos) < 30 &&
        frame - g_v6LastScoutOrder > 192) ++g_v6ScoutStall;
    else g_v6ScoutStall = 0;
    g_v6ScoutPrevPos = scout->getPosition();
    // If stuck, don't spam every frame. Try another route via attacker's movement.
    if (scout->move(destination))
    {
        g_v5ScoutId = scout->getID();
        g_v6LastScoutOrder = frame;
        if (g_v6ScoutStall >= 3)
        {
            game->printf("AI v6: scout path blocked, retrying");
            g_v6ScoutStall = 0;
        }
    }
}

void AIV6ManageSupply()
{
    auto& game = BWAPI::Broodwar;
    auto me = game->self();
    if (!me || me->getRace() != BWAPI::Races::Terran) return;
    AIV6ResetIfNewMatch();
    const int frame = game->getFrameCount();
    if (frame % 12 != 0 || frame - g_v6LastSupplyOrder < 72) return;
    if (me->supplyTotal() >= 400 || me->minerals() < 100) return;

    int barracks = 0, factories = 0, cc = 0, pending = 0;
    for (auto unit : me->getUnits())
    {
        const auto type = unit->getType();
        if (type == BWAPI::UnitTypes::Terran_Barracks && unit->isCompleted()) ++barracks;
        if (type == BWAPI::UnitTypes::Terran_Factory && unit->isCompleted()) ++factories;
        if (type == BWAPI::UnitTypes::Terran_Command_Center && unit->isCompleted()) ++cc;
        if (type == BWAPI::UnitTypes::Terran_Supply_Depot && !unit->isCompleted()) ++pending;
    }
    // Supply is doubled in BWAPI: a depot contributes 16 API supply (8 in UI).
    // Plan for units currently being produced, not just the current supply gap.
    const int freeSupply = me->supplyTotal() - me->supplyUsed();
    const int warning = (std::min)(44, 12 + 4 * barracks + 6 * factories + 2 * cc);
    const int projected = freeSupply + 16 * pending;
    if (projected > warning || pending >= 3) return;

    // Avoid issuing a new depot while a worker is still walking to an old order.
    for (auto worker : me->getUnits())
    {
        if (worker->getType() != BWAPI::UnitTypes::Terran_SCV) continue;
        const auto cmd = worker->getLastCommand();
        if (cmd.getType() == BWAPI::UnitCommandTypes::Build
            && cmd.getUnitType() == BWAPI::UnitTypes::Terran_Supply_Depot
            && frame - worker->getLastCommandFrame() < 240)
            return;
    }
    const auto tile = game->getBuildLocation(BWAPI::UnitTypes::Terran_Supply_Depot,
                                              me->getStartLocation(), 64, false);
    if (!tile.isValid()) return;
    for (auto worker : me->getUnits())
    {
        if (!AIV5IsAvailableWorker(worker) || worker->isCarryingGas()
            || worker->isCarryingMinerals()) continue;
        if (worker->build(BWAPI::UnitTypes::Terran_Supply_Depot, tile))
        {
            g_v6LastSupplyOrder = frame;
            game->printf("AI v6: supply %d/%d, projected free %d, constructing depot",
                         me->supplyUsed()/2, me->supplyTotal()/2, projected/2);
            return;
        }
    }
}

// Avoid the artificial 60-marine cap, but save gas for Tanks and Medics.
void AIV6TrainInfantry()
{
    auto& game = BWAPI::Broodwar;
    auto me = game->self();
    if (!me || me->getRace() != BWAPI::Races::Terran) return;
    if (game->getFrameCount() % 12 != 0) return;
    const auto& units = me->getUnits();
    int marines = Tools::CountUnitsOfType(BWAPI::UnitTypes::Terran_Marine, units);
    int medics = Tools::CountUnitsOfType(BWAPI::UnitTypes::Terran_Medic, units);
    auto academy = Tools::GetUnitOfType(BWAPI::UnitTypes::Terran_Academy);
    const int desiredMedics = (std::min)(18, (std::max)(2, marines / 7));
    for (auto rax : units)
    {
        if (rax->getType() != BWAPI::UnitTypes::Terran_Barracks || !rax->isCompleted()
            || rax->isTraining()) continue;
        if (me->minerals() < 50 || me->supplyTotal() - me->supplyUsed() < 2) return;
        if (academy && marines >= 6 && medics < desiredMedics && me->gas() >= 75)
        {
            if (rax->train(BWAPI::UnitTypes::Terran_Medic)) { ++medics; continue; }
        }
        if (marines < 125 && rax->train(BWAPI::UnitTypes::Terran_Marine)) ++marines;
    }
}

BWAPI::Position AIV6HomeThreat()
{
    auto& game = BWAPI::Broodwar;
    auto me = game->self();
    if (!me) return BWAPI::Positions::None;
    const BWAPI::Position home(me->getStartLocation());
    BWAPI::Position threat = BWAPI::Positions::None;
    int nearest = 10000000;
    int fighters = 0;
    int threatWeight = 0;
    for (auto enemy : game->enemies())
        for (auto hostile : enemy->getUnits())
        {
            if (!hostile || !hostile->exists() || !hostile->isVisible()
                || !hostile->isDetected() || hostile->getType().isBuilding()
                || hostile->isFlying()
                || hostile->getType().groundWeapon()==BWAPI::WeaponTypes::None) continue;
            if (hostile->getType().isWorker() && !hostile->isAttacking()) continue;
            int distance = hostile->getDistance(home);
            for (auto unit : me->getUnits())
                if (unit->getType().isBuilding() && unit->isCompleted())
                    distance = (std::min)(distance, hostile->getDistance(unit));
            if (distance >= 900) continue;
            ++fighters;
            threatWeight += hostile->getType().isWorker() ? 1 :
                (hostile->getType()==BWAPI::UnitTypes::Protoss_Zealot ? 3 : 2);
            if (distance < nearest)
            { nearest = distance; threat = hostile->getPosition(); }
        }
    // STARTCRAFT_AI_V55_DISCIPLINED_REACTION: an Observer has no ground
    // weapon and one distant scout is not a base emergency. Recall the whole
    // army only for a real group, or for a fighter already at the buildings.
    if (fighters==0 || (fighters==1 && nearest>420) ||
        (threatWeight<4 && nearest>600)) return BWAPI::Positions::None;
    return threat;
}

BWAPI::Position AIV6AttackTarget()
{
    auto& game = BWAPI::Broodwar;
    // Use the SCV-confirmed enemy start; do not chase a random proxy building.
    if (g_v6EnemyStart.isValid()) return BWAPI::Position(g_v6EnemyStart);
    for (auto enemy : game->enemies())
        for (auto unit : enemy->getUnits())
            if (unit && unit->exists() && unit->isVisible() && unit->getType().isBuilding())
                return unit->getPosition();
    if (g_v6LastEnemyBuilding.isValid()) return g_v6LastEnemyBuilding;
    // No enemy base found: hold the army, and let the SCV actually scout.
    return BWAPI::Positions::None;
}

BWAPI::Unit AIV6NearestMarine(BWAPI::Unit unit)
{
    BWAPI::Unit best = nullptr;
    int distance = 10000000;
    auto me = BWAPI::Broodwar->self();
    for (auto marine : me->getUnits())
    {
        if (!marine->exists() || marine->getType() != BWAPI::UnitTypes::Terran_Marine
            || !marine->isCompleted()) continue;
        int d = marine->getDistance(unit);
        if (d < distance) { distance = d; best = marine; }
    }
    return best;
}

void AIV6RallyDefendAttack()
{
    auto& game = BWAPI::Broodwar;
    auto me = game->self();
    if (!me || me->getRace() != BWAPI::Races::Terran) return;
    AIV6ResetIfNewMatch();
    const int frame = game->getFrameCount();
    if (frame % 24 != 0) return;
    AIV6ObserveEnemy();
    const auto& units = me->getUnits();
    const BWAPI::Position home(me->getStartLocation());
    const BWAPI::Position rally = AIV4ChooseRally(me);
    const auto threat = AIV6HomeThreat();

    std::set<int> alive;
    for (auto unit : units)
        if (AIV4IsCombatUnit(unit)) alive.insert(unit->getID());
    for (auto it = g_v6WaveUnits.begin(); it != g_v6WaveUnits.end(); )
        if (alive.count(*it) == 0) it = g_v6WaveUnits.erase(it);
        else ++it;

    // Emergency defense overrides rally and wave requirements. A premature rush
    // should never leave 10+ Marines waiting motionless inside the base.
    if (threat.isValid())
    {
        int defenders = 0;
        for (auto unit : units)
        {
            if (!AIV4IsCombatUnit(unit) || unit->getDistance(home) > 1350) continue;
            if (unit->getType() == BWAPI::UnitTypes::Terran_Medic) continue;
            if (unit->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode) continue;
            if (unit->getDistance(threat) > 96 &&
                (unit->isIdle() || frame - unit->getLastCommandFrame() > 72))
            {
                if (unit->attack(threat)) ++defenders;
            }
        }
        if (defenders > 0 && frame - g_v6LastHudFrame > 120)
        {
            game->printf("AI v6: BASE UNDER ATTACK - %d defenders responding", defenders);
            g_v6LastHudFrame = frame;
        }
        return;
    }

    int readyMarines = 0, readyTanks = 0;
    for (auto unit : units)
    {
        if (!AIV4IsCombatUnit(unit) || g_v6WaveUnits.count(unit->getID())) continue;
        // Rally point may be imperfect on custom maps; don't require all units to
        // stand within an exact 170px circle before counting the army.
        const bool nearBase = unit->getDistance(home) <= 950;
        const bool nearRally = unit->getDistance(rally) <= 450;
        if (!nearBase && !nearRally) continue;
        if (unit->getType() == BWAPI::UnitTypes::Terran_Marine) ++readyMarines;
        if (AIV4IsTank(unit)) ++readyTanks;
        if (unit->getDistance(rally) > 200 && unit->getType() != BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode
            && (unit->isIdle() || frame - unit->getLastCommandFrame() > 240))
            unit->move(rally);
    }
    if (readyMarines >= 8 || readyTanks >= 2)
    {
        if (g_v6FirstReadyFrame < 0) g_v6FirstReadyFrame = frame;
    }
    else g_v6FirstReadyFrame = -1;

    const auto target = AIV6AttackTarget();
    const bool attackArmy = readyMarines >= 12 && readyTanks >= 2;
    const bool delayedWave = g_v6FirstReadyFrame >= 0 &&
        frame - g_v6FirstReadyFrame >= 800 &&
        ((readyMarines >= 14 && readyTanks >= 1) ||
         (readyMarines >= 18) || (readyMarines >= 8 && readyTanks >= 3));
    if (target.isValid() && (attackArmy || delayedWave) &&
        frame - g_v6LastWaveFrame >= 360)
    {
        int dispatched = 0;
        for (auto unit : units)
        {
            if (!AIV4IsCombatUnit(unit) || g_v6WaveUnits.count(unit->getID())) continue;
            if (unit->getDistance(home) > 950 && unit->getDistance(rally) > 450) continue;
            if (unit->getType() == BWAPI::UnitTypes::Terran_Medic)
            {
                auto marine = AIV6NearestMarine(unit);
                if (marine && unit->move(marine->getPosition()))
                { g_v6WaveUnits.insert(unit->getID()); ++dispatched; }
            }
            else if (unit->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)
            {
                if (unit->unsiege())
                { g_v6WaveUnits.insert(unit->getID()); ++dispatched; }
            }
            else if (unit->attack(target))
            { g_v6WaveUnits.insert(unit->getID()); ++dispatched; }
        }
        if (dispatched > 0)
        {
            g_v6LastWaveFrame = frame;
            g_v6FirstReadyFrame = -1;
            g_v6WaveGoal = target;
            game->printf("AI v6: %d units attack known enemy position", dispatched);
        }
    }

    // Re-issue orders only to stalled units. Medics follow infantry, not attack-move.
    for (auto unit : units)
    {
        if (!AIV4IsCombatUnit(unit) || !g_v6WaveUnits.count(unit->getID())) continue;
        if (unit->getType() == BWAPI::UnitTypes::Terran_Medic)
        {
            auto marine = AIV6NearestMarine(unit);
            if (marine && unit->getDistance(marine) > 180 &&
                frame - unit->getLastCommandFrame() > 96)
                unit->move(marine->getPosition());
            continue;
        }
        if (unit->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode) continue;
        if (AIV4EnemyCloseTo(unit, 280)) continue;
        if (target.isValid() && (unit->isIdle() ||
            (frame - unit->getLastCommandFrame() > 240 &&
             unit->getLastCommand().getType() == BWAPI::UnitCommandTypes::Move)))
            unit->attack(target);
    }
    game->drawTextScreen(BWAPI::Position(10, 68),
        "AI v6 | supply %d/%d | waiting M:%d T:%d | enemy: %s",
        me->supplyUsed()/2, me->supplyTotal()/2, readyMarines, readyTanks,
        g_v6EnemyStart.isValid() ? "SCOUTED" : "SEARCHING");
}
} // namespace STARTCRAFT_AI_V6


// STARTCRAFT_AI_V7: one army, production rally switching, medics and regroup.
// Only replaces the v6 army controller. Keeps the v6 economy and scouting.
#include <set>
#include <algorithm>
#include <cstdlib>
namespace {
enum AIV7State { AIV7_ASSEMBLE, AIV7_ATTACK, AIV7_REGROUP };
AIV7State g_v7State = AIV7_ASSEMBLE;
int g_v7PrevFrame = -1;
int g_v7StateFrame = 0;
int g_v7FirstEnough = -1;
int g_v7LastProgressFrame = -1;
int g_v7BestDistance = 10000000;
int g_v7LastStatus = -10000;
std::set<int> g_v7Army;
BWAPI::Position g_v7AdvancePoint = BWAPI::Positions::None;

bool AIV7IsArmyUnit(BWAPI::Unit u)
{
    if (!u || !u->exists() || !u->isCompleted()) return false;
    const BWAPI::UnitType t = u->getType();
    return t == BWAPI::UnitTypes::Terran_Marine
        || t == BWAPI::UnitTypes::Terran_Medic
        || t == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode
        || t == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode
        || t == BWAPI::UnitTypes::Terran_Vulture
        || t == BWAPI::UnitTypes::Terran_Goliath;
}
bool AIV7IsTank(BWAPI::Unit u)
{
    const BWAPI::UnitType t = u->getType();
    return t == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode
        || t == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode;
}
void AIV7ResetGame()
{
    const int frame = BWAPI::Broodwar->getFrameCount();
    if (frame >= g_v7PrevFrame) { g_v7PrevFrame = frame; return; }
    g_v7PrevFrame = frame;
    g_v7State = AIV7_ASSEMBLE;
    g_v7StateFrame = frame;
    g_v7FirstEnough = -1;
    g_v7LastProgressFrame = frame;
    g_v7BestDistance = 10000000;
    g_v7LastStatus = -10000;
    g_v7AdvancePoint = BWAPI::Positions::None;
    g_v7Army.clear();
}
BWAPI::Unit AIV7MarineNear(BWAPI::Unit u, bool activeOnly)
{
    BWAPI::Unit nearest = nullptr;
    int best = 10000000;
    for (auto soldier : BWAPI::Broodwar->self()->getUnits())
    {
        if (!soldier || !soldier->exists() || !soldier->isCompleted()
            || soldier->getType() != BWAPI::UnitTypes::Terran_Marine) continue;
        if (activeOnly && !g_v7Army.count(soldier->getID())) continue;
        const int d = soldier->getDistance(u);
        if (d < best) { best = d; nearest = soldier; }
    }
    return nearest;
}
void AIV7SetProductionRally(BWAPI::Position pos)
{
    if (!pos.isValid()) return;
    for (auto b : BWAPI::Broodwar->self()->getUnits())
    {
        if (!b || !b->exists() || !b->isCompleted()) continue;
        const BWAPI::UnitType t = b->getType();
        if (t != BWAPI::UnitTypes::Terran_Barracks
            && t != BWAPI::UnitTypes::Terran_Factory) continue;
        // STARTCRAFT_AI_V37_4_AUTHORITATIVE_RALLY: the caller already chose
        // the strategic assembly point. Recomputing a different point from the
        // production building's nearest CC sent main-base units back inside.
        const BWAPI::Position rally=pos;
        if (b->getRallyUnit() || !b->getRallyPosition().isValid()
            || b->getRallyPosition().getDistance(rally) > 96)
            b->setRallyPoint(rally);
    }
}
// Choose army center anchored on the tanks (if any), not the fastest Marine.
BWAPI::Position AIV7CenterOfArmy(BWAPI::Position fallback)
{
    long long sx=0, sy=0;
    int count=0;
    auto me = BWAPI::Broodwar->self();
    for (auto u : me->getUnits())
    {
        if (!AIV7IsArmyUnit(u) || !g_v7Army.count(u->getID())) continue;
        if (AIV7IsTank(u)) { sx+=u->getPosition().x; sy+=u->getPosition().y; ++count; }
    }
    if (count == 0)
        for (auto u : me->getUnits())
            if (AIV7IsArmyUnit(u) && g_v7Army.count(u->getID())
                && u->getType() == BWAPI::UnitTypes::Terran_Marine)
            { sx+=u->getPosition().x; sy+=u->getPosition().y; ++count; }
    if (!count) return fallback;
    return BWAPI::Position(int(sx/count),int(sy/count));
}
// Combat proximity check used only to avoid retreating from a real engagement.
bool AIV7FrontInCombat(BWAPI::Position front, int distance)
{
    for (auto player : BWAPI::Broodwar->enemies())
        for (auto u : player->getUnits())
            if (u && u->exists() && u->isVisible()
                && u->getDistance(front) < distance) return true;
    return false;
}
// Advance rally toward enemy, but keep it behind the army's lead position.
BWAPI::Position AIV7SafeForwardRally(BWAPI::Position center,
                                    BWAPI::Position home, BWAPI::Position enemy)
{
    if (!center.isValid()) return home;
    const int dx = enemy.isValid() ? enemy.x - center.x : center.x - home.x;
    const int dy = enemy.isValid() ? enemy.y - center.y : center.y - home.y;
    const int norm = (std::max)(1, (std::max)(std::abs(dx), std::abs(dy)));
    BWAPI::Position p(center.x - 220 * dx / norm,
                      center.y - 220 * dy / norm);
    const int maxx = BWAPI::Broodwar->mapWidth()*32-32;
    const int maxy = BWAPI::Broodwar->mapHeight()*32-32;
    p = BWAPI::Position((std::max)(32,(std::min)(maxx,p.x)),
                        (std::max)(32,(std::min)(maxy,p.y)));
    // A front line can be dangerous; if the rally is already under fire, fall back.
    for (auto enemyPlayer : BWAPI::Broodwar->enemies())
        for (auto e : enemyPlayer->getUnits())
            if (e && e->exists() && e->isVisible() && !e->getType().isBuilding()
                && e->getDistance(p) < 320)
                return home;
    return p;
}
void AIV7BeginRegroup(BWAPI::Position home, int frame, const char* reason)
{
    g_v7State = AIV7_REGROUP;
    g_v7StateFrame = frame;
    g_v7FirstEnough = -1;
    g_v7AdvancePoint = home;
    AIV7SetProductionRally(home);
    for (auto u : BWAPI::Broodwar->self()->getUnits())
    {
        if (!AIV7IsArmyUnit(u)) continue;
        if (u->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)
            u->unsiege();
        else if (u->getDistance(home) > 160) u->move(home);
    }
    g_v7Army.clear();
    BWAPI::Broodwar->printf("AI v7: REGROUP - %s", reason);
}
void AIV7ArmyController()
{
    auto& game = BWAPI::Broodwar;
    auto me = game->self();
    if (!me || me->getRace() != BWAPI::Races::Terran) return;
    AIV7ResetGame();
    int frame = game->getFrameCount();
    if (frame % 24 != 0) return;
    AIV6ObserveEnemy(); // v6 remembered scouting coordinates are still authoritative.
    const BWAPI::Position home = AIV4ChooseRally(me);
    const BWAPI::Position enemy = AIV6AttackTarget();

    std::set<int> alive;
    for (auto u : me->getUnits()) if (AIV7IsArmyUnit(u)) alive.insert(u->getID());
    for (auto it = g_v7Army.begin(); it != g_v7Army.end(); )
        if (!alive.count(*it)) it = g_v7Army.erase(it); else ++it;

    // Emergency defense overrides all wave logic; the Barracks and Factory rally home.
    const BWAPI::Position threat = AIV6HomeThreat();
    if (threat.isValid())
    {
        if (g_v7State != AIV7_REGROUP)
            AIV7BeginRegroup(home,frame,"base attacked");
        AIV7SetProductionRally(home);
        for (auto u : me->getUnits())
        {
            if (!AIV7IsArmyUnit(u) || u->getDistance(home) > 1350) continue;
            if (u->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode) continue;
            if (u->getType() == BWAPI::UnitTypes::Terran_Medic)
            {
                auto marine = AIV7MarineNear(u,false);
                if (marine && u->getDistance(marine) > 140
                    && frame - u->getLastCommandFrame() > 96)
                    u->move(marine->getPosition());
            }
            else if (frame-u->getLastCommandFrame()>72 || u->isIdle())
                u->attack(threat);
        }
        return;
    }

    if (g_v7State == AIV7_REGROUP)
    {
        AIV7SetProductionRally(home);
        int atHome=0, total=0;
        for (auto u : me->getUnits()) if (AIV7IsArmyUnit(u))
        {
            ++total;
            if (u->getDistance(home) < 580) ++atHome;
            else if (u->getType() != BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode
                && (u->isIdle() || frame-u->getLastCommandFrame()>180)) u->move(home);
        }
        // Require some actual time to gather. Don't repeatedly launch isolated squads.
        if (frame-g_v7StateFrame>360 && (total==0 || atHome*4>=total*3))
        { g_v7State=AIV7_ASSEMBLE; g_v7StateFrame=frame; }
        return;
    }

    if (g_v7State == AIV7_ASSEMBLE)
    {
        AIV7SetProductionRally(home);
        int marines=0, tanks=0, medics=0;
        for (auto u : me->getUnits())
        {
            if (!AIV7IsArmyUnit(u)) continue;
            if (u->getDistance(home) < 800)
            {
                if (u->getType()==BWAPI::UnitTypes::Terran_Marine) ++marines;
                if (u->getType()==BWAPI::UnitTypes::Terran_Medic) ++medics;
                if (AIV7IsTank(u)) ++tanks;
            }
            else if (u->getType()!=BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode
                     && !AIV4EnemyCloseTo(u,240)
                     && (u->isIdle() || frame-u->getLastCommandFrame()>200))
                u->move(home);
        }
        const bool useful = marines>=12 && tanks>=1 && medics>=1;
        if (useful && g_v7FirstEnough<0) g_v7FirstEnough=frame;
        if (!useful) g_v7FirstEnough=-1;
        // Prefer a full combined-arms force; allow a slower fallback to avoid deadlock.
        const bool strong = marines>=18 && tanks>=2 && medics>=2;
        const bool longWait = useful && frame-g_v7FirstEnough>1400
                              && marines>=15 && medics>=1;
        if (enemy.isValid() && (strong || longWait))
        {
            g_v7State=AIV7_ATTACK;
            g_v7StateFrame=frame;
            g_v7LastProgressFrame=frame;
            g_v7BestDistance=10000000;
            g_v7Army.clear();
            for (auto u : me->getUnits())
            {
                if (!AIV7IsArmyUnit(u) || u->getDistance(home)>=950) continue;
                g_v7Army.insert(u->getID());
            }
            // Launch ALL gathered units, not a 12-unit sub-group.
            for (auto u : me->getUnits())
            {
                if (!g_v7Army.count(u->getID())) continue;
                if (u->getType()==BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)
                    u->unsiege();
                else if (u->getType()==BWAPI::UnitTypes::Terran_Medic)
                {
                    auto marine = AIV7MarineNear(u,true);
                    if (marine) u->move(marine->getPosition());
                    else u->attack(enemy); // BWAPI Medic attack-move = heal-move.
                }
                else u->attack(enemy);
            }
            game->printf("AI v7: attack TOGETHER M:%d T:%d Medic:%d", marines,tanks,medics);
        }
        game->drawTextScreen(BWAPI::Position(10,88),
            "AI v7 ASSEMBLE | Marine:%d Tank:%d Medic:%d",marines,tanks,medics);
        return;
    }

    // ATTACK: continuously set production rally BEHIND the current tank-led front.
    const BWAPI::Position center=AIV7CenterOfArmy(home);
    BWAPI::Position forward=AIV7SafeForwardRally(center,home,enemy);
    if (!forward.isValid()) forward=home;
    g_v7AdvancePoint=forward;
    AIV7SetProductionRally(forward);
    int marines=0, medics=0, tanks=0, fighters=0;
    BWAPI::Unit leadingTank=nullptr;
    for (auto u : me->getUnits())
    {
        if (!AIV7IsArmyUnit(u) || !g_v7Army.count(u->getID())) continue;
        if (u->getType()==BWAPI::UnitTypes::Terran_Marine) { ++marines; ++fighters; }
        if (u->getType()==BWAPI::UnitTypes::Terran_Medic) ++medics;
        if (AIV7IsTank(u))
        {
            ++tanks; ++fighters;
            if (!leadingTank || u->getDistance(center)<leadingTank->getDistance(center))
                leadingTank=u;
        }
    }
    if (fighters<6 || (tanks==0 && marines<10))
    {
        AIV7BeginRegroup(home,frame,"army depleted");
        return;
    }
    if (enemy.isValid())
    {
        const int distance=center.getDistance(enemy);
        if (distance+120<g_v7BestDistance)
        { g_v7BestDistance=distance; g_v7LastProgressFrame=frame; }
        // Stall = little forward progress for a sustained period. Under heavy battle
        // give the army more time, but eventually rally everything back home.
        if (frame-g_v7LastProgressFrame>2300
           || (frame-g_v7LastProgressFrame>1100
               && !AIV7FrontInCombat(center,400)))
        {
            AIV7BeginRegroup(home,frame,"attack stalled");
            return;
        }
    }
    for (auto u : me->getUnits())
    {
        if (!AIV7IsArmyUnit(u)) continue;
        // New units join the SAME army only after reaching its forward rally.
        if (!g_v7Army.count(u->getID()))
        {
            if (u->getDistance(center)<540)
                g_v7Army.insert(u->getID());
            else if (u->getType()!=BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode
                && !AIV4EnemyCloseTo(u,280)
                && (u->isIdle() || frame-u->getLastCommandFrame()>240))
                u->move(forward);
            continue;
        }
        if (u->getType()==BWAPI::UnitTypes::Terran_Medic)
        {
            auto marine=AIV7MarineNear(u,true);
            if (marine && u->getDistance(marine)>180
                && (u->isIdle() || frame-u->getLastCommandFrame()>96))
                u->move(marine->getPosition());
            // When close, let the Medic's automatic healing AI do its job.
            continue;
        }
        if (u->getType()==BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode) continue;
        if (AIV4EnemyCloseTo(u,260)) continue;
        // Marines do not race far ahead of the slow siege line.
        if (leadingTank && u->getType()==BWAPI::UnitTypes::Terran_Marine
            && u->getDistance(leadingTank)>420
            && enemy.isValid()
            && u->getDistance(enemy)+220<leadingTank->getDistance(enemy))
        {
            if (u->isIdle() || frame-u->getLastCommandFrame()>120)
                u->move(leadingTank->getPosition());
            continue;
        }
        if (enemy.isValid() && (u->isIdle() ||
           (u->getLastCommand().getType()==BWAPI::UnitCommandTypes::Move
            && frame-u->getLastCommandFrame()>180)))
            u->attack(enemy);
    }
    game->drawTextScreen(BWAPI::Position(10,88),
        "AI v7 ATTACK | M:%d T:%d Medic:%d | Front rally",marines,tanks,medics);
}
} // STARTCRAFT_AI_V7


// STARTCRAFT_AI_V8: no timer-based army withdrawal; traffic clearing; build-planning.
// Additive patch for the proven v7 source. Original v7/v6 routines remain as fallbacks.
#include <map>
#include <set>
#include <algorithm>
#include <cstdlib>
namespace {
struct AIV8Motion {
    BWAPI::Position position;
    int since;
    int lastDetour;
    AIV8Motion() : position(BWAPI::Positions::None), since(0), lastDetour(-10000) {}
};
std::map<int, AIV8Motion> g_v8Motion;
int g_v8PrevFrame = -1;
int g_v8LastClear = -10000;
int g_v8LastDepot = -10000;
int g_v8LastBarracks = -10000;

void AIV8Reset()
{
    int frame = BWAPI::Broodwar->getFrameCount();
    if (frame < g_v8PrevFrame) {
        g_v8Motion.clear();
        g_v8LastClear = g_v8LastDepot = g_v8LastBarracks = -10000;
    }
    g_v8PrevFrame = frame;
}

// Prefer open building pads at the edge of the main base, preserving an open
// corridor toward the army staging area. This is a conservative placement
// heuristic, not full choke-point analysis or a guaranteed wall-in.
BWAPI::TilePosition AIV8BuildingTile(BWAPI::UnitType type)
{
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    const BWAPI::TilePosition home=me->getStartLocation();
    const BWAPI::Position origin(home);
    const BWAPI::Position rally=AIV4ChooseRally(me);
    BWAPI::Position toward(game->mapWidth()*16,game->mapHeight()*16);
    if (g_v6EnemyStart.isValid()) toward=BWAPI::Position(g_v6EnemyStart);
    int vx=toward.x-origin.x, vy=toward.y-origin.y;
    const long long len2=(long long)vx*vx+(long long)vy*vy;
    for (int radius=5;radius<=19;radius++)
        for (int dy=-radius;dy<=radius;dy++)
            for (int dx=-radius;dx<=radius;dx++)
            {
                if ((std::max)(std::abs(dx),std::abs(dy))!=radius) continue;
                BWAPI::TilePosition tile(home.x+dx,home.y+dy);
                if (!tile.isValid() || !game->canBuildHere(tile,type)) continue;
                const BWAPI::Position center(tile.x*32+type.tileWidth()*16,
                                              tile.y*32+type.tileHeight()*16);
                if (center.getDistance(origin)<185) continue;
                if (rally.isValid() && rally!=origin && center.getDistance(rally)<195) continue;
                // Keep a ~3-tile-wide main-base exit route open when possible.
                if (len2>0) {
                    const long long px=center.x-origin.x, py=center.y-origin.y;
                    const long long dot=px*vx+py*vy;
                    if (dot>0 && dot<len2 &&
                        (px*(long long)vy-py*(long long)vx)*
                        (px*(long long)vy-py*(long long)vx) < 130LL*130LL*len2)
                        continue;
                }
                return tile;
            }
    // Fallback favors actually constructing over a permanent supply lock.
    return game->getBuildLocation(type,home,64,false);
}

BWAPI::Unit AIV8Builder()
{
    auto me=BWAPI::Broodwar->self();
    BWAPI::Unit best=nullptr;
    for (auto u : me->getUnits())
    {
        if (!AIV5IsAvailableWorker(u) || u->isCarryingGas() ||
            u->isCarryingMinerals() || u->isGatheringGas()) continue;
        if (!best || u->isIdle()) {best=u;if(u->isIdle())break;}
    }
    return best;
}

void AIV8ManageSupply()
{
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if (!me || me->getRace()!=BWAPI::Races::Terran) return;
    AIV8Reset();
    int frame=game->getFrameCount();
    if (frame%24 !=0 || frame-g_v8LastDepot<120 || me->minerals()<100 ||
        me->supplyTotal()>=400) return;
    int rax=0, factory=0, cc=0, pending=0;
    for (auto u : me->getUnits()) {
        if (!u->exists()) continue;
        if (u->getType()==BWAPI::UnitTypes::Terran_Barracks && u->isCompleted())++rax;
        if (u->getType()==BWAPI::UnitTypes::Terran_Factory && u->isCompleted())++factory;
        if (u->getType()==BWAPI::UnitTypes::Terran_Command_Center && u->isCompleted())++cc;
        if (u->getType()==BWAPI::UnitTypes::Terran_Supply_Depot && !u->isCompleted())++pending;
    }
    const int free=me->supplyTotal()-me->supplyUsed();  // BWAPI doubled supply.
    // Predict roughly one production cycle rather than triggering at 0 supply.
    const int reserve=(std::min)(36, 8+4*rax+6*factory+2*cc);
    if (free + pending*16 > reserve || pending>=2) return;
    for (auto scv : me->getUnits()) {
        if (scv->getType()!=BWAPI::UnitTypes::Terran_SCV)continue;
        if (scv->getLastCommand().getType()==BWAPI::UnitCommandTypes::Build &&
            scv->getLastCommand().getUnitType()==BWAPI::UnitTypes::Terran_Supply_Depot &&
            frame-scv->getLastCommandFrame()<240) return;
    }
    auto worker=AIV8Builder();
    auto tile=AIV8BuildingTile(BWAPI::UnitTypes::Terran_Supply_Depot);
    if(worker && tile.isValid() && worker->build(BWAPI::UnitTypes::Terran_Supply_Depot,tile)) {
        g_v8LastDepot=frame;
        game->printf("AI v8: supply %d/%d reserve %d pending %d",me->supplyUsed()/2,
                     me->supplyTotal()/2,reserve/2,pending);
    }
}

// Brood War build-order guidance: prioritize bio infrastructure against Zerg;
// keep Factory tech available against Protoss/Terran. Add capacity only when
// economy can afford to sustain it (rather than at a fixed timer).
void AIV8BuildExtraBarracks()
{
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if (!me || me->getRace()!=BWAPI::Races::Terran) return;
    int frame=game->getFrameCount();
    if (frame%24 || frame-g_v8LastBarracks<300) return;
    int rax=0, scvs=0;
    for (auto u:me->getUnits()) {
        if(u->getType()==BWAPI::UnitTypes::Terran_Barracks) ++rax;
        if(u->getType()==BWAPI::UnitTypes::Terran_SCV) ++scvs;
    }
    bool vsZerg=false;
    for (auto player:game->enemies())
        if(player && player->getRace()==BWAPI::Races::Zerg)vsZerg=true;
    int desired=2;
    if(scvs>=23 && me->minerals()>=420)desired=3;
    if(vsZerg && scvs>=26 && me->minerals()>=650)desired=4;
    if(rax>=desired || me->minerals()<300 || scvs<16) return;
    // No extra Barracks if it would consume critical early Factory gas timing.
    if(!vsZerg && Tools::CountUnitsOfType(BWAPI::UnitTypes::Terran_Factory,
                                           me->getUnits())==0)return;
    auto tile=AIV8BuildingTile(BWAPI::UnitTypes::Terran_Barracks);
    auto worker=AIV8Builder();
    if(worker && tile.isValid() && worker->build(BWAPI::UnitTypes::Terran_Barracks,tile)){
        g_v8LastBarracks=frame;
        game->printf("AI v8: adding Barracks %d/%d (SCV %d)",rax+1,desired,scvs);
    }
}

bool AIV8GroundCombat(BWAPI::Unit u)
{
    return AIV7IsArmyUnit(u) && !u->isFlying();
}

// Prove a path is grid-valid and passable before asking a blocker to step aside.
BWAPI::Position AIV8SideStep(BWAPI::Unit blocker,BWAPI::Position toward)
{
    auto& game=BWAPI::Broodwar;
    if (!blocker || !blocker->exists() || !toward.isValid())
        return BWAPI::Positions::None;
    BWAPI::Position now=blocker->getPosition();
    int dx=toward.x-now.x,dy=toward.y-now.y;
    int norm=(std::max)(1,(std::max)(std::abs(dx),std::abs(dy)));
    // Move perpendicular to traffic, not back toward the mineral line.
    int sx=-dy*128/norm,sy=dx*128/norm;
    BWAPI::Position options[4]={
        BWAPI::Position(now.x+sx,now.y+sy), BWAPI::Position(now.x-sx,now.y-sy),
        BWAPI::Position(now.x+sx*2,now.y+sy*2), BWAPI::Position(now.x-sx*2,now.y-sy*2)};
    for(const auto& dest:options){
        if(dest.x<24 || dest.y<24 ||
           dest.x>=game->mapWidth()*32-24 || dest.y>=game->mapHeight()*32-24)continue;
        if(!game->isWalkable(dest.x/8,dest.y/8) || !blocker->hasPath(dest))continue;
        bool occupied=false;
        for(auto other:game->getUnitsInRadius(dest,55))
            if(other && other->exists() && other->getType().isBuilding())occupied=true;
        if(!occupied)return dest;
    }
    return BWAPI::Positions::None;
}

void AIV8UnblockTraffic(BWAPI::Position goal,int frame)
{
    if(frame-g_v8LastClear<96 || !goal.isValid())return;
    auto me=BWAPI::Broodwar->self();
    for(auto u:me->getUnits()){
        if(!AIV8GroundCombat(u) || !g_v7Army.count(u->getID()) ||
           g_v56SiegeLineIds.count(u->getID()) ||
           u->getType()==BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode ||
           AIV4EnemyCloseTo(u,320) || u->getDistance(goal)<220)continue;
        auto& track=g_v8Motion[u->getID()];
        if(!track.position.isValid() || track.position.getDistance(u->getPosition())>32){
            track.position=u->getPosition();track.since=frame;continue;
        }
        if(frame-track.since<168 || frame-track.lastDetour<240)continue;
        int crowd=0;
        BWAPI::Unit blocker=nullptr;
        for(auto near:me->getUnits()){
            if(!near || !near->exists() || !near->isCompleted() || near==u ||
               near->getType().isBuilding() || near->isFlying() ||
               g_v56SiegeLineIds.count(near->getID()) ||
               near->getDistance(u)>130) continue;
            ++crowd;
            const bool freeWorker = near->getType()==BWAPI::UnitTypes::Terran_SCV &&
                !AIV353ReservedBuilder(near) && !near->isConstructing() && !near->isRepairing() &&
                !near->isGatheringMinerals() && !near->isGatheringGas();
            if (!AIV4EnemyCloseTo(near,320) && near->getDistance(u)<85 &&
                (near->getType()==BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode ||
                 (near->isIdle() && (AIV8GroundCombat(near) || freeWorker))))
                blocker=near;
        }
        if(crowd<1)continue; // at least one friendly unit is blocking a stalled attacker
        if(blocker && blocker->getType()==BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode){
            if(blocker->unsiege()){
                g_v8LastClear=frame;track.lastDetour=frame;
                BWAPI::Broodwar->printf("AI v8: friendly siege tank clears choke");
                return;
            }
        }
        BWAPI::Unit mover=blocker?blocker:u;
        BWAPI::Position step=AIV8SideStep(mover,goal);
        if(step.isValid() && mover->move(step)){
            g_v8LastClear=frame;track.lastDetour=frame;
            BWAPI::Broodwar->printf("AI v8: traffic cleared by sidestep");
            return;
        }
    }
}

BWAPI::Position AIV8FrontRally(BWAPI::Position center,BWAPI::Position home,
                               BWAPI::Position enemy)
{
    if(!center.isValid())return home;
    BWAPI::Position p=AIV7SafeForwardRally(center,home,enemy);
    // v7 used to rally ALL the way back home if enemy was near its rally spot.
    // Keep rally behind frontline instead of triggering unintended withdrawals.
    if(p==home && center.getDistance(home)>650){
        int dx=center.x-home.x,dy=center.y-home.y;
        int norm=(std::max)(1,(std::max)(std::abs(dx),std::abs(dy)));
        p=BWAPI::Position(center.x-320*dx/norm,center.y-320*dy/norm);
    }
    const int x=(std::max)(32,(std::min)(BWAPI::Broodwar->mapWidth()*32-32,p.x));
    const int y=(std::max)(32,(std::min)(BWAPI::Broodwar->mapHeight()*32-32,p.y));
    return BWAPI::Position(x,y);
}

BWAPI::Position AIV10AttackTarget();
// STARTCRAFT_AI_V33_NATURAL_ASSEMBLY
// A completed natural becomes the defensive staging anchor. The existing
// attack state still uses the original mobile forward-frontline rally.
static BWAPI::Position AIV33AssemblyPoint() {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    BWAPI::Position fallback=AIV4ChooseRally(me);
    if(!me)return fallback;
    BWAPI::Unit main=nullptr,natural=nullptr;
    const BWAPI::Position origin(me->getStartLocation());
    for(auto cc:me->getUnits())if(cc && cc->exists() && cc->isCompleted() &&
        !cc->isFlying() && cc->getType()==BWAPI::UnitTypes::Terran_Command_Center &&
        (!main || cc->getDistance(origin)<main->getDistance(origin)))main=cc;
    if(!main)return fallback;
    // STARTCRAFT_AI_V44_DEFEND_BUILDING_NATURAL: rally to the natural from
    // the moment construction starts. v43 waited for completion and Pluto
    // destroyed the exposed CC while the army remained at the main.
    for(auto cc:me->getUnits())if(cc && cc->exists() &&
        !cc->isFlying() && cc!=main && cc->getType()==BWAPI::UnitTypes::Terran_Command_Center &&
        (!natural || cc->getDistance(main)<natural->getDistance(main)))natural=cc;
    if(!natural)return fallback;
    BWAPI::Position b=natural->getPosition(),foe=AIV6AttackTarget();
    if(!foe.isValid())foe=BWAPI::Position(game->mapWidth()*16,game->mapHeight()*16);
    const int dx=foe.x-b.x,dy=foe.y-b.y;
    const int norm=(std::max)(1,(std::max)(std::abs(dx),std::abs(dy)));
    BWAPI::Unit pathUnit=nullptr;
    for(auto u:me->getUnits())if(u && u->exists() && u->isCompleted() &&
        (u->getType()==BWAPI::UnitTypes::Terran_Marine ||
         u->getType()==BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode)){
        pathUnit=u;break;
    }
    const int rallyRadii[4]={230,180,130,0};
    for(int radius:rallyRadii) {
        BWAPI::Position spot(b.x+dx*radius/norm,b.y+dy*radius/norm);
        if(spot.x<20 || spot.y<20 || spot.x>=game->mapWidth()*32-20 ||
           spot.y>=game->mapHeight()*32-20 || !game->isWalkable(spot.x/8,spot.y/8) ||
           (pathUnit && !pathUnit->hasPath(spot)))continue;
        bool obstructed=false;
        for(auto u:game->getUnitsInRadius(spot,85))if(u && u->exists() &&
            (u->getType().isBuilding() || u->getType().isMineralField()))obstructed=true;
        if(!obstructed)return spot;
    }
    return b;
}
BWAPI::Race AIV13OpponentRace(); // STARTCRAFT_AI_V32_DECL
static bool AIV25AttackReady(int marines,int tanks,int medics,BWAPI::Position enemy);
void AIV8ArmyController()
{
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if(!me || me->getRace()!=BWAPI::Races::Terran)return;
    AIV7ResetGame();AIV8Reset();
    int frame=game->getFrameCount();
    if(frame%24)return;
    AIV6ObserveEnemy();
    BWAPI::Position home=AIV33AssemblyPoint(); // STARTCRAFT_AI_V33_NATURAL_RALLY
    BWAPI::Position enemy=AIV10AttackTarget();
    BWAPI::Position baseThreat=AIV6HomeThreat();

    std::set<int> alive;
    for(auto u:me->getUnits())if(AIV7IsArmyUnit(u))alive.insert(u->getID());
    for(auto it=g_v7Army.begin();it!=g_v7Army.end();)
        if(!alive.count(*it))it=g_v7Army.erase(it);else ++it;

    // Defend using LOCAL troops only. Never recall the entire attacking army
    // because one worker or a new enemy unit is seen near the main base.
    if(baseThreat.isValid()){
        for(auto u:me->getUnits()){
            if(!AIV7IsArmyUnit(u) || u->getDistance(baseThreat)>850 ||
               g_v56SiegeLineIds.count(u->getID()) ||
               (g_v7State==AIV7_ATTACK && g_v7Army.count(u->getID())))continue;
            if(u->getType()==BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)continue;
            if(u->getType()==BWAPI::UnitTypes::Terran_Medic){
                auto marine=AIV7MarineNear(u,false);
                if(marine && u->getDistance(marine)>160)u->move(marine->getPosition());
            }else if(u->isIdle() || frame-u->getLastCommandFrame()>120)
                u->attack(baseThreat);
        }
    }

    if(g_v7State!=AIV7_ATTACK){
        AIV7SetProductionRally(home);
        int marines=0,tanks=0,medics=0;
        for(auto u:me->getUnits()){
            if(!AIV7IsArmyUnit(u) || g_v32GuardIds.count(u->getID()))continue;
            if(u->getDistance(home)<950){
                if(u->getType()==BWAPI::UnitTypes::Terran_Marine)++marines;
                if(u->getType()==BWAPI::UnitTypes::Terran_Medic)++medics;
                if(AIV7IsTank(u))++tanks;
            }else if(!g_v56SiegeLineIds.count(u->getID()) &&
                !AIV4EnemyCloseTo(u,300) &&
                u->getType()!=BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode &&
                (u->isIdle() || frame-u->getLastCommandFrame()>240))u->move(home);
        }
        if(enemy.isValid() && AIV25AttackReady(marines,tanks,medics,enemy) &&
           !baseThreat.isValid()){
            g_v7State=AIV7_ATTACK;
            g_v7StateFrame=frame;
            g_v7Army.clear();
            const bool mechPush=(AIV13OpponentRace()==BWAPI::Races::Terran);
            for(auto u:me->getUnits())
                if(AIV7IsArmyUnit(u) && !g_v32GuardIds.count(u->getID()) &&
                   (u->getDistance(home)<1100 || (mechPush && u->getDistance(home)<3600)))
                    g_v7Army.insert(u->getID());
            for(auto u:me->getUnits()){
                if(!g_v7Army.count(u->getID()))continue;
                if(g_v56SiegeLineIds.count(u->getID()))continue;
                if(u->getType()==BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)u->unsiege();
                else if(u->getType()==BWAPI::UnitTypes::Terran_Medic){
                    auto marine=AIV7MarineNear(u,true);
                    if(marine)u->move(marine->getPosition());
                }else u->attack(enemy);
            }
            game->printf("AI v8: MAIN ARMY ATTACK M%d T%d D%d",marines,tanks,medics);
        }
        game->drawTextScreen(BWAPI::Position(10,88),
           "AI v8 ASSEMBLE M:%d T:%d Medic:%d",marines,tanks,medics);
        return;
    }

    // Stay on offense while the army exists. A distance/time stall is NOT a
    // valid reason to order hundreds of units to retreat across the map.
    int fighters=0;
    for(auto u:me->getUnits())
        if(g_v7Army.count(u->getID()) &&
           (u->getType()==BWAPI::UnitTypes::Terran_Marine || AIV7IsTank(u)))++fighters;
    if(fighters==0){
        g_v7State=AIV7_ASSEMBLE;g_v7Army.clear();
        AIV7SetProductionRally(home);
        game->printf("AI v8: main army lost, preparing next push");
        return;
    }

    BWAPI::Position center=AIV7CenterOfArmy(home);
    BWAPI::Position forward=AIV8FrontRally(center,home,enemy);
    AIV7SetProductionRally(forward);
    AIV8UnblockTraffic(enemy,frame);

    for(auto u:me->getUnits()){
        if(!AIV7IsArmyUnit(u) || g_v32GuardIds.count(u->getID()))continue;
        if(g_v56SiegeLineIds.count(u->getID()))continue;
        if(!g_v7Army.count(u->getID())){
            if(u->getDistance(center)<600)g_v7Army.insert(u->getID());
            else if(!AIV4EnemyCloseTo(u,270) &&
                u->getType()!=BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode &&
                (u->isIdle() || frame-u->getLastCommandFrame()>200))u->move(forward);
            continue;
        }
        if(u->getType()==BWAPI::UnitTypes::Terran_Medic){
            auto marine=AIV7MarineNear(u,true);
            if(marine && u->getDistance(marine)>165 &&
                (u->isIdle() || frame-u->getLastCommandFrame()>96))
                u->move(marine->getPosition());
            continue;
        }
        if(u->getType()==BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode ||
           AIV4EnemyCloseTo(u,250))continue;
        if(enemy.isValid() && (u->isIdle() ||
           (u->getLastCommand().getType()==BWAPI::UnitCommandTypes::Move &&
            frame-u->getLastCommandFrame()>170)))u->attack(enemy);
    }
    game->drawTextScreen(BWAPI::Position(10,88),
        "AI v8 ATTACK: %d fighters | HOLD FRONT / rally forward",fighters);
}
} // STARTCRAFT_AI_V8


// STARTCRAFT_AI_V10: economic priorities, measured supply, expansions, and cleanup.
// Based on the proven v8 army controller. No changes to the fast game speed.
#include <map>
#include <set>
#include <vector>
#include <algorithm>
#include <cstdlib>
namespace {
int g_v10PriorFrame = -1;
int g_v10LastSupply = -10000;
int g_v10LastExpand = -10000;
int g_v10LastGoalChange = -10000;
int g_v10LastRetarget = -10000;
BWAPI::Position g_v10SearchGoal = BWAPI::Positions::None;
BWAPI::Position g_v10LastAttackGoal = BWAPI::Positions::None;
std::map<int, BWAPI::Position> g_v10EnemyBuildings; // ID -> last actually observed place
std::map<int, int> g_v10SearchVisits;  // map scan tile key -> last visit frame

void AIV10Reset()
{
    auto& game = BWAPI::Broodwar;
    const int frame = game->getFrameCount();
    if (frame < g_v10PriorFrame) {
        g_v10LastSupply = g_v10LastExpand = -10000;
        g_v10LastGoalChange = g_v10LastRetarget = -10000;
        g_v10SearchGoal = g_v10LastAttackGoal = BWAPI::Positions::None;
        g_v10EnemyBuildings.clear(); g_v10SearchVisits.clear();
    }
    g_v10PriorFrame = frame;
}

int AIV10WorkerTarget()
{
    int completedCC = 0;
    auto me = BWAPI::Broodwar->self();
    if (!me || me->getRace()!=BWAPI::Races::Terran) return 26;
    for (auto u : me->getUnits())
        if (u && u->exists() && u->isCompleted() &&
            u->getType()==BWAPI::UnitTypes::Terran_Command_Center) ++completedCC;
    return (std::min)(66, completedCC>=3 ? 60 : completedCC==2 ? 44 : 29);
}

void AIV10ManageSupply()
{
    auto& game = BWAPI::Broodwar;
    auto me = game->self();
    if (!me || me->getRace()!=BWAPI::Races::Terran) return;
    AIV10Reset();
    int frame=game->getFrameCount();
    if (frame%24 || frame-g_v10LastSupply<240 || me->minerals()<100 ||
        me->supplyTotal()>=400) return;
    int depots=0, unfinished=0, barracks=0, factory=0;
    for (auto u : me->getUnits()) {
        if (!u || !u->exists()) continue;
        auto t=u->getType();
        if (t==BWAPI::UnitTypes::Terran_Supply_Depot){ ++depots; if(!u->isCompleted()) ++unfinished; }
        if (t==BWAPI::UnitTypes::Terran_Barracks) ++barracks;
        if (t==BWAPI::UnitTypes::Terran_Factory) ++factory;
    }
    // BWAPI supply is doubled. A single unfinished depot = +16 supply.
    // Do not queue more while an earlier depot is still being constructed.
    if (unfinished>0) return;
    const int free = me->supplyTotal()-me->supplyUsed();
    const int reserve=(std::min)(22, 8+4*barracks+4*factory);
    if (free>reserve) return;
    // Do not spend the first tech/building money on redundant depots.
    // Emergency supply (<=2 shown in-game) always takes priority.
    if (free>4 && ((barracks==0 && depots>=1) ||
        (factory==0 && barracks>=1 && me->minerals()<260))) return;
    for (auto u : me->getUnits())
        if (u && u->getType()==BWAPI::UnitTypes::Terran_SCV &&
            u->getLastCommand().getType()==BWAPI::UnitCommandTypes::Build &&
            u->getLastCommand().getUnitType()==BWAPI::UnitTypes::Terran_Supply_Depot &&
            frame-u->getLastCommandFrame()<420) return;
    auto worker=AIV8Builder();
    if (!worker) return;
    const auto tile=AIV8BuildingTile(BWAPI::UnitTypes::Terran_Supply_Depot);
    if (tile.isValid() && worker->build(BWAPI::UnitTypes::Terran_Supply_Depot,tile)) {
        g_v10LastSupply=frame;
        game->printf("v10: depot needed: used %d / cap %d; planned buffer %d",
                     me->supplyUsed()/2, me->supplyTotal()/2,reserve/2);
    }
}

// Save for a natural after basic defenses exist; don't let continuous
// infantry/tank queues spend every last mineral indefinitely.
bool AIV10SavingForNatural()
{
    auto me=BWAPI::Broodwar->self();
    if(!me || me->getRace()!=BWAPI::Races::Terran)return false;
    int cc=0,scvs=0,rax=0,factory=0,marines=0,tanks=0;
    for(auto u:me->getUnits()){
        auto t=u->getType();
        if(t==BWAPI::UnitTypes::Terran_Command_Center)++cc;
        if(t==BWAPI::UnitTypes::Terran_SCV)++scvs;
        if(t==BWAPI::UnitTypes::Terran_Barracks)++rax;
        if(t==BWAPI::UnitTypes::Terran_Factory)++factory;
        if(t==BWAPI::UnitTypes::Terran_Marine)++marines;
        if(t==BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode ||
           t==BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)++tanks;
    }
    return cc==1 && scvs>=22 && rax>=1 && factory>=1 &&
           marines>=10 && tanks>=1 && me->minerals()<475;
}
void AIV10TrainInfantry()
{
    if(!AIV10SavingForNatural()) AIV6TrainInfantry();
}
void AIV10BuildTanks()
{
    if(!AIV10SavingForNatural()) AIV2BuildMachineShopAndTanks();
}
void AIV10BuildSecondFactory()
{
    auto me=BWAPI::Broodwar->self();
    if(!me)return;
    int cc=0;
    for(auto u:me->getUnits())
        if(u->getType()==BWAPI::UnitTypes::Terran_Command_Center)++cc;
    if(cc>=2) AIV3BuildSecondFactory();
}

// Prioritise the first natural expansion over a third/fourth Barracks.
void AIV10BuildExtraBarracks()
{
    auto me=BWAPI::Broodwar->self();
    if (!me || me->getRace()!=BWAPI::Races::Terran) return;
    int cc=0, scvs=0;
    for (auto u: me->getUnits()) {
        if(u->getType()==BWAPI::UnitTypes::Terran_Command_Center) ++cc;
        if(u->getType()==BWAPI::UnitTypes::Terran_SCV) ++scvs;
    }
    if (cc<2 && scvs>=21) return;
    AIV8BuildExtraBarracks();
}

// Simple natural-expansion heuristic (not BWEM): search real mineral clusters,
// avoid own/enemy bases, and require a reachable 4x3 CC footprint.
void AIV10Expand()
{
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if (!me || me->getRace()!=BWAPI::Races::Terran) return;
    AIV10Reset();
    int frame=game->getFrameCount();
    if(frame<2800 || frame%240 || frame-g_v10LastExpand<900 || me->minerals()<425) return;
    int cc=0,scv=0;
    for(auto u:me->getUnits()) {
        if (u->getType()==BWAPI::UnitTypes::Terran_Command_Center) {
            ++cc;
            if(!u->isCompleted())return;
        }
        if(u->getType()==BWAPI::UnitTypes::Terran_SCV)++scv;
    }
    if(cc>=3 || scv<(cc==1?21:36))return;
    auto builder=AIV8Builder();
    if(!builder) return;
    const BWAPI::UnitType ccType=BWAPI::UnitTypes::Terran_Command_Center;
    BWAPI::TilePosition best=BWAPI::TilePositions::None;
    int bestScore=100000000;
    const int shifts[9]={-12,-9,-6,-3,0,3,6,9,12};
    for(auto mineral:game->getMinerals()) {
        if(!mineral || !mineral->exists())continue;
        BWAPI::Position mp=mineral->getPosition();
        bool tooClose=false;
        for(auto base:me->getUnits())
            if(base->getType()==ccType && base->getDistance(mp)<850){tooClose=true;break;}
        if(tooClose) continue;
        // Ignore isolated mineral patches; only consider an actual base-sized cluster.
        int mineralGroup=0;
        for(auto other:game->getMinerals())
            if(other && other->exists() && other->getDistance(mp)<360)++mineralGroup;
        if(mineralGroup<5)continue;
        auto mt=mineral->getTilePosition();
        for(int ox:shifts)for(int oy:shifts) {
            BWAPI::TilePosition tile(mt.x+ox,mt.y+oy);
            if(!tile.isValid())continue;
            BWAPI::Position pos(tile.x*32+64,tile.y*32+48);
            if(!game->canBuildHere(tile,ccType) || !builder->hasPath(pos))continue;
            int patches=0;
            for(auto other:game->getMinerals())
                if(other && other->exists()){
                    int d=other->getDistance(pos);
                    if(d>110 && d<375)++patches;
                }
            if(patches<5)continue;
            bool occupied=false;
            for(auto base:me->getUnits())
                if(base->getType()==ccType && base->getDistance(pos)<950){occupied=true;break;}
            if(occupied) continue;
            for(auto p:game->enemies())for(auto e:p->getUnits())
                if(e && e->exists() && e->isVisible() && e->getType().isBuilding() &&
                   e->getDistance(pos)<600)occupied=true;
            if(occupied)continue;
            int distance=pos.getDistance(BWAPI::Position(me->getStartLocation()));
            int score=distance - patches*40;
            if(score<bestScore) {bestScore=score;best=tile;}
        }
    }
    if(best.isValid() && builder->build(ccType,best)){
        g_v10LastExpand=frame;
        game->printf("v10: expanding at %d,%d",best.x,best.y);
    } else if (frame-g_v10LastExpand>2400) {
        g_v10LastExpand=frame-1500; // retry later without filling console
        game->printf("v10: natural not reachable/buildable yet");
    }
}

void AIV10RememberEnemyBases()
{
    auto& game=BWAPI::Broodwar;
    AIV10Reset();
    for(auto p:game->enemies())for(auto e:p->getUnits())
        if(e && e->exists() && e->isVisible() && e->getType().isBuilding())
            g_v10EnemyBuildings[e->getID()]=e->getPosition();
    for(auto it=g_v10EnemyBuildings.begin();it!=g_v10EnemyBuildings.end();) {
        const BWAPI::Position location=it->second;
        if(!game->isVisible(BWAPI::TilePosition(location))){++it;continue;}
        bool stillThere=false;
        for(auto p:game->enemies())for(auto e:p->getUnits())
            if(e && e->exists() && e->isVisible() && e->getType().isBuilding() &&
               e->getDistance(location)<95){stillThere=true;break;}
        if(stillThere)++it;
        else it=g_v10EnemyBuildings.erase(it);
    }
}

// Prefer last SEEN enemy buildings, not a spawn point where the enemy base
// has already been destroyed. If none are known, explore for expansions.
BWAPI::Position AIV10AttackTarget()
{
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if(!me)return BWAPI::Positions::None;
    AIV10RememberEnemyBases();
    const int frame=game->getFrameCount();
    BWAPI::Position center=AIV7CenterOfArmy(BWAPI::Position(me->getStartLocation()));
    BWAPI::Position target=BWAPI::Positions::None;
    int scoreBest=10000000;
    for(auto it:g_v10EnemyBuildings){
        int score=center.getDistance(it.second);
        if(score<scoreBest){scoreBest=score;target=it.second;}
    }
    if(target.isValid()) return target;
    // Keep scouting target stable until it is visited, or an enemy is sighted.
    if(g_v10SearchGoal.isValid() &&
        !game->isVisible(BWAPI::TilePosition(g_v10SearchGoal)) &&
        frame-g_v10LastGoalChange<2400) return g_v10SearchGoal;
    if(g_v10SearchGoal.isValid())
        g_v10SearchVisits[(g_v10SearchGoal.x/32)*1000+(g_v10SearchGoal.y/32)]=frame;
    const int mapW=game->mapWidth(),mapH=game->mapHeight();
    int best=-10000000;
    // Only consider ground-accessible points. Search the entire map for
    // unexplored expansion sites after enemy main buildings are gone.
    for(int y=8;y<mapH-8;y+=12)for(int x=8;x<mapW-8;x+=12){
        BWAPI::TilePosition tile(x,y);
        if(!game->isWalkable(x*4+2,y*4+2) || game->isVisible(tile))continue;
        BWAPI::Position p(tile);
        if(!me->getUnits().empty()) {
            BWAPI::Unit pathUnit=nullptr;
            for(auto u:me->getUnits())
                if(u && u->isCompleted() && (u->getType()==BWAPI::UnitTypes::Terran_Marine ||
                   u->getType()==BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode)){
                    pathUnit=u;break;
                }
            if(pathUnit && !pathUnit->hasPath(p))continue;
        }
        int explored=game->isExplored(tile)?0:1;
        int last=0;
        const int key=x*1000+y;
        auto found=g_v10SearchVisits.find(key);
        if(found!=g_v10SearchVisits.end())last=found->second;
        int score=explored*10000 + (std::min)(4000,frame-last) - center.getDistance(p)/5;
        if(score>best){best=score;target=p;}
    }
    if(target.isValid()) {
        g_v10SearchGoal=target;
        g_v10LastGoalChange=frame;
    }
    return target;
}

void AIV10ArmyController()
{
    AIV8ArmyController();
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if(!me || me->getRace()!=BWAPI::Races::Terran || game->getFrameCount()%48)return;
    if(g_v7State!=AIV7_ATTACK)return;
    BWAPI::Position goal=AIV10AttackTarget();
    if(!goal.isValid())return;
    int frame=game->getFrameCount();
    // Only issue mass retarget orders if the strategic target changed.
    bool changed=!g_v10LastAttackGoal.isValid() ||
        g_v10LastAttackGoal.getDistance(goal)>350;
    if(!changed || frame-g_v10LastRetarget<120)return;
    g_v10LastAttackGoal=goal;g_v10LastRetarget=frame;
    int moved=0;
    for(auto u:me->getUnits()){
        if(!AIV7IsArmyUnit(u) || !g_v7Army.count(u->getID()) ||
           g_v56SiegeLineIds.count(u->getID()) || AIV4EnemyCloseTo(u,250))continue;
        if(u->getType()==BWAPI::UnitTypes::Terran_Medic){
            auto m=AIV7MarineNear(u,true);
            if(m)u->move(m->getPosition());
        }else if(u->getType()==BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode){
            u->unsiege();
        }else if(u->attack(goal))++moved;
    }
    game->printf("v10: switched attack target, ordering %d fighters",moved);
}
} // STARTCRAFT_AI_V10


// STARTCRAFT_AI_V11: unified Terran economy director.
// Replaces duplicate construction/training loops in onFrame, not army/scout micro.
#include <algorithm>
#include <map>
#include <set>
#include <vector>
namespace {
int g_v11PrevFrame = -1;
// Used by the production policy before the intelligence implementation below.
static int AIV25Seen(BWAPI::UnitType kind);
int g_v11LastBuildOrder = -10000;
int g_v11LastExpandCheck = -10000;
int g_v11LastNotice = -10000;
int g_v11NoExpansionUntil = -1;
int g_v11Reserve = 0;
std::map<int,int> g_v11ShopFailures;
// Accepted construction commands remain pending even when another director
// overwrites the SCV's last command while it is walking to the site.
std::map<int,int> g_v50TrackedPendingBuilds;

struct AIV11Counts {
    int scv=0, barracks=0, factory=0, academy=0, refinery=0, cc=0;
    int completedCC=0, unfinishedCC=0, unfinishedDepot=0;
    int marine=0, medic=0, tank=0, pending=0;
};

AIV11Counts AIV11Count() {
    AIV11Counts c;
    auto me=BWAPI::Broodwar->self();
    if(!me)return c;
    for(auto u:me->getUnits()) {
        if(!u || !u->exists())continue;
        auto t=u->getType();
        if(t==BWAPI::UnitTypes::Terran_SCV)++c.scv;
        if(t==BWAPI::UnitTypes::Terran_Barracks)++c.barracks;
        if(t==BWAPI::UnitTypes::Terran_Factory)++c.factory;
        if(t==BWAPI::UnitTypes::Terran_Academy)++c.academy;
        if(t==BWAPI::UnitTypes::Terran_Refinery)++c.refinery;
        if(t==BWAPI::UnitTypes::Terran_Command_Center) {
            ++c.cc;
            if(u->isCompleted())++c.completedCC;
            else ++c.unfinishedCC;
        }
        if(t==BWAPI::UnitTypes::Terran_Supply_Depot && !u->isCompleted())++c.unfinishedDepot;
        if(t==BWAPI::UnitTypes::Terran_Marine)++c.marine;
        if(t==BWAPI::UnitTypes::Terran_Medic)++c.medic;
        if(t==BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode ||
           t==BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)++c.tank;
    }
    return c;
}

void AIV11Reset() {
    int frame=BWAPI::Broodwar->getFrameCount();
    if(frame<g_v11PrevFrame) {
        g_v11LastBuildOrder=g_v11LastExpandCheck=g_v11LastNotice=-10000;
        g_v11NoExpansionUntil=-1;g_v11Reserve=0;g_v11ShopFailures.clear();
        g_v50TrackedPendingBuilds.clear();
    }
    g_v11PrevFrame=frame;
}

// Saturate current bases without filling all possible supply with SCVs.
// STARTCRAFT_AI_V31_DYNAMIC_WORKERS
// Use the *actual* mineral field and refinery capacity at completed bases.
// A second CC alone does not justify 23 extra SCVs if there is no 3rd expansion.
int AIV11WorkerTarget() {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if (!me || me->getRace()!=BWAPI::Races::Terran) return 20;
    std::vector<BWAPI::Unit> completed;
    int unfinished=0;
    for(auto cc:me->getUnits()) {
        if (!cc || !cc->exists() ||
            cc->getType()!=BWAPI::UnitTypes::Terran_Command_Center) continue;
        if(cc->isCompleted() && !cc->isFlying()) completed.push_back(cc);
        else ++unfinished;
    }
    if(completed.empty()) return 18;
    int patches=0,gasSites=0;
    for(auto mineral:game->getMinerals()) {
        if(!mineral || !mineral->exists() ||
           !mineral->getType().isMineralField() || mineral->getResources()<=0) continue;
        int distance=1000000;
        for(auto cc:completed)distance=(std::min)(distance,cc->getDistance(mineral));
        if(distance<=450)++patches;
    }
    for(auto refinery:me->getUnits()) {
        if(!refinery || !refinery->exists() || !refinery->isCompleted() ||
           refinery->getType()!=BWAPI::UnitTypes::Terran_Refinery) continue;
        int distance=1000000;
        for(auto cc:completed)distance=(std::min)(distance,cc->getDistance(refinery));
        if(distance<=450)++gasSites;
    }
    // Two per mineral patch, three per working gas site, two roaming builders.
    // Keep enough workers early for the first expansion and basic tech.
    const int capacity=2*patches+3*gasSites+2+(unfinished>0?2:0);
    if(completed.size()==1) return (std::min)(25,(std::max)(22,capacity));
    if(completed.size()==2) return (std::min)(44,(std::max)(26,capacity));
    return (std::min)(68,(std::max)(34,capacity));
}
void AIV11TrainWorkers() {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if(!me || me->getRace()!=BWAPI::Races::Terran)return;
    AIV11Reset();
    if(game->getFrameCount()%12)return;
    AIV11Counts c=AIV11Count();
    // STARTCRAFT_AI_V46_TVZ_TWO_RAX_EXPAND: once the 15th SCV and both
    // opening Barracks exist, stop adding workers until the natural is
    // ordered.  v45 announced an expansion at 16 SCVs but AIV16Expand still
    // required 18, so the screen showed uninterrupted SCV production while
    // neither an early army nor an early Command Center materialised.
    if(AIV13OpponentRace()==BWAPI::Races::Zerg && c.cc==1 && c.barracks>=2 &&
       c.scv>=15 && game->getFrameCount()>=g_v11NoExpansionUntil)return;
    if(c.scv>=AIV11WorkerTarget() || me->minerals()<50 ||
       me->supplyTotal()-me->supplyUsed()<2)return;
    // Early army infrastructure takes precedence over saturating past 14/18 SCVs.
    if((c.scv>=14 && c.barracks==0 && me->minerals()<200) ||
       (c.scv>=18 && c.refinery==0 && me->minerals()<125) ||
       (c.scv>=20 && c.factory==0 && me->gas()>=100 && me->minerals()<240))return;
    int pending=0;
    for(auto cc:me->getUnits())
        if(cc->getType()==BWAPI::UnitTypes::Terran_Command_Center && cc->isTraining())++pending;
    if(c.scv+pending>=AIV11WorkerTarget())return;
    for(auto cc:me->getUnits()) {
        if(cc->getType()!=BWAPI::UnitTypes::Terran_Command_Center || !cc->isCompleted() ||
           cc->isTraining() || !game->canMake(BWAPI::UnitTypes::Terran_SCV,cc))continue;
        if(g_v11Reserve>0 && c.scv>=20 && me->minerals()<g_v11Reserve+100)return;
        if(cc->train(BWAPI::UnitTypes::Terran_SCV))return; // one queue at a time
    }
}

// In-flight SCV build orders are not yet visible as buildings.
// Count them, or the next frame can order a duplicate Depot/Command Center.
bool AIV11PendingBuild(BWAPI::UnitType type, int maxAge=420) {
    if(g_v50TrackedPendingBuilds[type.getID()]>0)return true;
    auto me=BWAPI::Broodwar->self();
    if(!me)return false;
    int frame=BWAPI::Broodwar->getFrameCount();
    for(auto w:me->getUnits()) {
        if(!w || !w->exists() || w->getType()!=BWAPI::UnitTypes::Terran_SCV)continue;
        auto cmd=w->getLastCommand();
        if(cmd.getType()==BWAPI::UnitCommandTypes::Build && cmd.getUnitType()==type &&
           frame-w->getLastCommandFrame()<maxAge)return true;
    }
    return false;
}

BWAPI::Unit AIV11BuildWorker(BWAPI::Position place=BWAPI::Positions::None) {
    auto me=BWAPI::Broodwar->self();
    BWAPI::Unit best=nullptr;
    int distance=10000000;
    if(!me)return nullptr;
    for(auto u:me->getUnits()) {
        if(!u || !u->exists() || u->getType()!=BWAPI::UnitTypes::Terran_SCV ||
           !u->isCompleted() || !u->isInterruptible() || u->isConstructing() || u->isRepairing() ||
           u->isGatheringGas() || u->getID()==g_v5ScoutId)continue;
        if(AIV353ReservedBuilder(u))continue;
        int d=place.isValid()?u->getDistance(place):0;
        if(!best || d<distance) {best=u;distance=d;}
    }
    return best;
}

// STARTCRAFT_AI_V12: traffic-aware Terran building placement.
// Injected inside existing anonymous namespace, before AIV11BuildNear().
// A build pad is safe only if adding the planned building preserves reachable
// outer sectors of the main base and access to nearby resource fields.
struct AIV12Map {
    int x0, y0, w, h;
    std::vector<unsigned char> pass;
    AIV12Map() : x0(0),y0(0),w(0),h(0) {}
    int index(int x,int y) const {
        if (x<x0 || y<y0 || x>=x0+w || y>=y0+h) return -1;
        return (y-y0)*w+x-x0;
    }
    bool free(int x,int y) const {
        int i=index(x,y); return i>=0 && pass[i]!=0;
    }
    void block(int x,int y,int ww,int hh) {
        for(int yy=y;yy<y+hh;++yy)
            for(int xx=x;xx<x+ww;++xx){int i=index(xx,yy);if(i>=0)pass[i]=0;}
    }
};

AIV12Map AIV12BuildMap(BWAPI::TilePosition home) {
    auto& game=BWAPI::Broodwar;
    AIV12Map m;
    m.x0=(std::max)(0,home.x-39);m.y0=(std::max)(0,home.y-39);
    int endx=(std::min)(game->mapWidth(),home.x+43);
    int endy=(std::min)(game->mapHeight(),home.y+43);
    m.w=endx-m.x0;m.h=endy-m.y0;
    m.pass.assign(m.w*m.h,0);
    for(int y=m.y0;y<endy;++y)for(int x=m.x0;x<endx;++x){
        // A build tile has 4x4 BWAPI walk tiles. Both centre cells must pass.
        bool clear=game->isWalkable(x*4+1,y*4+1) &&
                   game->isWalkable(x*4+2,y*4+2);
        if(clear)for(auto u:game->getUnitsOnTile(x,y)){
            if(!u || !u->exists())continue;
            auto t=u->getType();
            if(t.isBuilding() || t.isMineralField() ||
               t==BWAPI::UnitTypes::Resource_Vespene_Geyser){clear=false;break;}
        }
        m.pass[m.index(x,y)]=clear?1:0;
    }
    return m;
}

std::vector<unsigned char> AIV12Flood(const AIV12Map& m, int sx, int sy){
    std::vector<unsigned char> reached(m.w*m.h,0);
    int start=m.index(sx,sy);
    if(start<0 || !m.pass[start])return reached;
    std::vector<int> queue;
    queue.reserve(m.w*m.h);
    reached[start]=1;queue.push_back(start);
    for(size_t head=0;head<queue.size();++head){
        int i=queue[head],x=m.x0+(i%m.w),y=m.y0+(i/m.w);
        const int nx[4]={x+1,x-1,x,x},ny[4]={y,y,y+1,y-1};
        for(int j=0;j<4;++j){
            int p=m.index(nx[j],ny[j]);
            if(p>=0 && m.pass[p] && !reached[p]){
                reached[p]=1;queue.push_back(p);
            }
        }
    }
    return reached;
}

// Compare two-build-tile-wide lanes as well as the SCV-sized routes.
// The wider check is activated only when a wide route existed before placing
// the candidate; this avoids rejecting every pad on naturally narrow maps.
AIV12Map AIV12WideMap(const AIV12Map& m) {
    AIV12Map wide=m;
    for(int y=m.y0;y<m.y0+m.h;++y)for(int x=m.x0;x<m.x0+m.w;++x){
        int p=m.index(x,y);
        wide.pass[p] = m.free(x,y) && m.free(x+1,y) &&
                       m.free(x,y+1) && m.free(x+1,y+1) ? 1 : 0;
    }
    return wide;
}

bool AIV12WideSeed(const AIV12Map& wide,int x,int y,int& sx,int& sy){
    if(wide.free(x,y)){sx=x;sy=y;return true;}
    for(int r=1;r<=7;++r)
        for(int dy=-r;dy<=r;++dy)
            for(int dx=-r;dx<=r;++dx)
                if(wide.free(x+dx,y+dy)){
                    sx=x+dx;sy=y+dy;return true;
                }
    return false;
}

// Sector masks represent accessible 8-way exterior approach lanes.
unsigned int AIV12ExitSectors(const AIV12Map& m,
               const std::vector<unsigned char>& seen,BWAPI::TilePosition home){
    unsigned int mask=0;
    for(int y=m.y0;y<m.y0+m.h;++y)for(int x=m.x0;x<m.x0+m.w;++x){
        int dx=x-home.x-2,dy=y-home.y-1;
        int ring=(std::max)(std::abs(dx),std::abs(dy));
        if(ring<19 || ring>21)continue;
        int i=m.index(x,y);if(i<0 || !seen[i])continue;
        int sector=0;
        if(dx>=0 && dy>=0)sector=(dx>2*dy)?0:((dy>2*dx)?2:1);
        else if(dx<0 && dy>=0)sector=(-dx>2*dy)?4:((dy>-2*dx)?2:3);
        else if(dx<0 && dy<0)sector=(-dx>-2*dy)?4:((-dy>-2*dx)?6:5);
        else sector=(dx>-2*dy)?0:((-dy>2*dx)?6:7);
        mask |= (1u<<sector);
    }
    return mask;
}

bool AIV12ReachResource(const AIV12Map& m,
          const std::vector<unsigned char>& seen, BWAPI::TilePosition tile){
    // Resource tile itself is blocked; an adjacent free approach tile must be reachable.
    for(int dy=-1;dy<=2;++dy)for(int dx=-1;dx<=2;++dx){
        int p=m.index(tile.x+dx,tile.y+dy);
        if(p>=0 && seen[p] && m.pass[p])return true;
    }
    return false;
}

bool AIV12SidesReachable(const AIV12Map& m,
       const std::vector<unsigned char>& seen,BWAPI::TilePosition t,BWAPI::UnitType type){
    const int x=t.x,y=t.y,w=type.tileWidth(),h=type.tileHeight();
    int sides=0;
    for(int j=0;j<w;++j){
        int p=m.index(x+j,y-1);if(p>=0 && seen[p]){++sides;break;}
    }
    for(int j=0;j<w;++j){
        int p=m.index(x+j,y+h);if(p>=0 && seen[p]){++sides;break;}
    }
    for(int j=0;j<h;++j){
        int p=m.index(x-1,y+j);if(p>=0 && seen[p]){++sides;break;}
    }
    for(int j=0;j<h;++j){
        int p=m.index(x+w,y+j);if(p>=0 && seen[p]){++sides;break;}
    }
    return sides>=2;
}


// STARTCRAFT_AI_V14_ZONES: placement *preferences*, never overriding the v12
// road/path/resource checks. Second pass retains the old safe placements.
bool AIV14ZonePreferred(BWAPI::UnitType type,int dx,int dy,int radius,int fx,int fy) {
    const int scale=(std::max)(1,std::abs(fx)+std::abs(fy));
    const long long front=(long long)dx*fx+(long long)dy*fy;
    const long long side=(long long)dx*fy-(long long)dy*fx;
    const long long ar=(long long)radius*scale;
    if(type==BWAPI::UnitTypes::Terran_Supply_Depot)
        return front<=-ar/8; // rear of mineral/army approach
    if(type==BWAPI::UnitTypes::Terran_Barracks)
        return front>=ar/6;  // forward, without walling off the exit
    if(type==BWAPI::UnitTypes::Terran_Factory)
        return front>=-ar/8 && front<=ar*3/5 &&
               std::abs(side)>=ar/4; // forward-side, addon on the right
    if(type==BWAPI::UnitTypes::Terran_Academy)
        return front<=ar/8; // behind the frontline barracks
    return true;
}

// STARTCRAFT_AI_V35_KEEP_THE_MAIN_GATE_OPEN
// Reserve a fixed outward route as well as the CC's unloading/army maneuver area.
// Previous sector-mask flood tests only preserved *some* outer sectors: they did
// not keep the actual route out of the base free of buildings.
static bool AIV35NearSegment(BWAPI::Position p, BWAPI::Position a,
                             BWAPI::Position b,int width) {
    const long long dx=(long long)b.x-a.x,dy=(long long)b.y-a.y;
    const long long len=dx*dx+dy*dy;
    if(len<1)return false;
    const long long px=(long long)p.x-a.x,py=(long long)p.y-a.y;
    const long long dot=px*dx+py*dy;
    if(dot<0 || dot>len)return false;
    const long long cross=px*dy-py*dx;
    return cross*cross<=(long long)width*width*len;
}
static bool AIV35ForbiddenBuildPad(BWAPI::Position center,
                                   BWAPI::TilePosition home,
                                   BWAPI::Player me,int buildingRadius) {
    auto& game=BWAPI::Broodwar;
    const BWAPI::Position main(home.x*32+64,home.y*32+48);
    // Keep this clear for SCV traffic, units spawning and turning around CC.
    if(center.getDistance(main)<288+buildingRadius)return true;
    // Never put permanent production buildings across the main army egress.
    // The map-center vector is deterministic; scouting won't rotate the lane.
    BWAPI::Position toward(game->mapWidth()*16,game->mapHeight()*16);
    int dx=toward.x-main.x,dy=toward.y-main.y;
    const int scale=(std::max)(1,(std::max)(std::abs(dx),std::abs(dy)));
    const BWAPI::Position exit(main.x+dx*850/scale,main.y+dy*850/scale);
    if(AIV35NearSegment(center,main,exit,116+buildingRadius))return true;
    // An expansion gives a second crucial traffic lane; never sever it with
    // factories, depots or addon footprints.
    for(auto u:me->getUnits()) {
        if(!u || !u->exists() || u->isFlying() ||
           u->getType()!=BWAPI::UnitTypes::Terran_Command_Center ||
           u->getDistance(main)<400)continue;
        const BWAPI::Position natural=u->getPosition();
        if(center.getDistance(natural)<245+buildingRadius)return true;
        if(AIV35NearSegment(center,main,natural,103+buildingRadius))return true;
    }
    return false;
}

// STARTCRAFT_AI_V35_WIDE_ARMY_ROUTE
static std::vector<unsigned char> AIV35WideArmyRoute(const AIV12Map& map,
                                   BWAPI::TilePosition home,int fx,int fy) {
    std::vector<unsigned char> reserved(map.w*map.h,0);
    AIV12Map wide=AIV12WideMap(map);
    int sx=home.x+4,sy=home.y+1;
    if(!AIV12WideSeed(wide,sx,sy,sx,sy))return reserved;
    const int first=wide.index(sx,sy);
    if(first<0)return reserved;
    const auto seen=AIV12Flood(wide,sx,sy);
    const int scale=(std::max)(1,(std::max)(std::abs(fx),std::abs(fy)));
    int best=-1,bestScore=100000000;
    for(int y=wide.y0;y<wide.y0+wide.h;++y)
        for(int x=wide.x0;x<wide.x0+wide.w;++x){
            const int i=wide.index(x,y);
            if(i<0 || !seen[i])continue;
            const int dx=x-home.x-2,dy=y-home.y-1;
            const int radius=(std::max)(std::abs(dx),std::abs(dy));
            if(radius<17 || radius>25)continue;
            const long long ahead=(long long)dx*fx+(long long)dy*fy;
            if(ahead<=0)continue;
            const long long cross=(long long)dx*fy-(long long)dy*fx;
            const long long side=cross>=0?cross:-cross;
            const int score=std::abs(radius-21)*13+
                            (int)(side/scale)*11-(int)(ahead/scale)*3;
            if(score<bestScore){bestScore=score;best=i;}
        }
    if(best<0)return reserved;
    // Reconstruct one contiguous 2x2-wide walk lane along real traversable
    // tiles. Depots and factories may not land on or squeeze this lane.
    std::vector<int> parent(wide.w*wide.h,-1);
    std::vector<int> queue;
    queue.reserve(wide.w*wide.h);
    parent[first]=first;queue.push_back(first);
    for(size_t k=0;k<queue.size() && parent[best]<0;++k){
        const int id=queue[k];
        const int x=wide.x0+id%wide.w,y=wide.y0+id/wide.w;
        const int nx[4]={x+1,x-1,x,x};
        const int ny[4]={y,y,y+1,y-1};
        for(int a=0;a<4;++a){
            const int j=wide.index(nx[a],ny[a]);
            if(j>=0 && wide.pass[j] && parent[j]<0){
                parent[j]=id;queue.push_back(j);
            }
        }
    }
    if(parent[best]<0)return reserved;
    for(int id=best;id!=first;id=parent[id]){
        const int x=wide.x0+id%wide.w,y=wide.y0+id/wide.w;
        for(int dy=-1;dy<=2;++dy)for(int dx=-1;dx<=2;++dx){
            const int j=map.index(x+dx,y+dy);
            if(j>=0)reserved[j]=1;
        }
    }
    return reserved;
}

// STARTCRAFT_AI_V35_1_RECOVERY_LEGACY_FALLBACK
BWAPI::TilePosition AIV351LegacyBuildTile(BWAPI::UnitType type,BWAPI::Unit builder){
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if(!me || !builder)return BWAPI::TilePositions::None;
    const BWAPI::TilePosition home=me->getStartLocation();
    AIV12Map map=AIV12BuildMap(home);
    // Seed flood from an active worker on the accessible side of the CC.
    int sx=-1,sy=-1,best=1000000;
    const BWAPI::Position homePos(home);
    for(auto u:me->getUnits()){
        if(!u || !u->exists() || u->getType()!=BWAPI::UnitTypes::Terran_SCV)continue;
        auto p=u->getTilePosition();
        if(!map.free(p.x,p.y))continue;
        int d=u->getDistance(homePos);
        if(d<best){best=d;sx=p.x;sy=p.y;}
    }
    if(sx<0){
        for(int r=3;r<=9 && sx<0;++r)
            for(int dy=-r;dy<=r && sx<0;++dy)
                for(int dx=-r;dx<=r;++dx){
                    int x=home.x+dx,y=home.y+dy;
                    if(map.free(x,y)){sx=x;sy=y;break;}
                }
    }
    if(sx<0)return BWAPI::TilePositions::None;
    auto before=AIV12Flood(map,sx,sy);
    unsigned int exits=AIV12ExitSectors(map,before,home);
    AIV12Map wideBefore=AIV12WideMap(map);
    int wideX=sx,wideY=sy;
    unsigned int wideExits=0;
    if(AIV12WideSeed(wideBefore,sx,sy,wideX,wideY)){
        auto wb=AIV12Flood(wideBefore,wideX,wideY);
        wideExits=AIV12ExitSectors(wideBefore,wb,home);
    }
    std::vector<BWAPI::TilePosition> resources;
    for(auto u:game->getMinerals()){
        if(u && u->exists() && u->getDistance(homePos)<650 &&
           AIV12ReachResource(map,before,u->getTilePosition()))
            resources.push_back(u->getTilePosition());
    }
    for(auto u:game->getGeysers()){
        if(u && u->exists() && u->getDistance(homePos)<650 &&
           AIV12ReachResource(map,before,u->getTilePosition()))
            resources.push_back(u->getTilePosition());
    }
    const int w=type.tileWidth(),h=type.tileHeight();
    // The enemy-start direction is useful once scouted. Until then, orient
    // front toward the centre of the map. Never treat this as choke detection.
    const BWAPI::Position homePx(home.x*32+64,home.y*32+48);
    BWAPI::Position frontPx(game->mapWidth()*16,game->mapHeight()*16);
    if(g_v6EnemyStart.isValid()) frontPx=BWAPI::Position(g_v6EnemyStart);
    int fx=frontPx.x-homePx.x,fy=frontPx.y-homePx.y;
    if(!fx && !fy) fx=32;
    // STARTCRAFT_AI_V33_SUPPLY_CLUSTER: use open rear-edge pads, not main CC lanes.
    // For existing depots, try adjacent legal pads as a compact block.
    // The old safety/path/resource flood checks still apply; fallback remains.
    const bool clusteringSupply=(type==BWAPI::UnitTypes::Terran_Supply_Depot);
    std::vector<BWAPI::Position> priorDepots;
    BWAPI::TilePosition depotGridAnchor=BWAPI::TilePositions::None;
    int depotAnchorId=2147483647;
    if(clusteringSupply)for(auto u:me->getUnits())if(u && u->exists() &&
        u->getType()==BWAPI::UnitTypes::Terran_Supply_Depot) {
        priorDepots.push_back(u->getPosition());
        // STARTCRAFT_AI_V34_SUPPLY_GRID: never shift the grid as new depots appear.
        if(u->getID()<depotAnchorId){
            depotAnchorId=u->getID();depotGridAnchor=u->getTilePosition();
        }
    }
    // Strict preferred sectors first, safe old placement as second pass.
    // The flood-fill and machine-shop clearance tests below apply to BOTH.
    for(int zonePass=0;zonePass<2;++zonePass){
    const int legacyOuter=clusteringSupply?24:35;
    for(int r=clusteringSupply && zonePass==0?13:6;r<=legacyOuter;++r){
        BWAPI::TilePosition bestTile=BWAPI::TilePositions::None;
        int bestScore=1000000;
        for(int dy=-r;dy<=r;++dy)for(int dx=-r;dx<=r;++dx){
            if((std::max)(std::abs(dx),std::abs(dy))!=r)continue;
            BWAPI::TilePosition t(home.x+dx,home.y+dy);
            if(!t.isValid())continue;
            if(zonePass==0 && !AIV14ZonePreferred(type,dx,dy,r,fx,fy))continue;
            // Keep one build tile between 3x2 depots: uniform rows and columns.
            // If no legal aligned pad remains, zonePass 1 uses safe fallback.
            if(clusteringSupply && zonePass==0 && depotGridAnchor.isValid()) {
                if((t.x-depotGridAnchor.x)%(w+1)!=0 ||
                   (t.y-depotGridAnchor.y)%(h+1)!=0)continue;
            }
            // Leave some open space around the initial command center.
            BWAPI::Position center(t.x*32+w*16,t.y*32+h*16);
            if(center.getDistance(homePos)<175)continue;
            int nearestDepot=999999;
            if(clusteringSupply && !priorDepots.empty()) {
                for(const auto& depot:priorDepots)
                    nearestDepot=(std::min)(nearestDepot,static_cast<int>(depot.getDistance(center)));
                if(zonePass==0 && nearestDepot>240)continue;
            }
            bool usable=true;
            for(auto existing:me->getUnits()){
                if(!existing || !existing->exists())continue;
                auto et=existing->getType();
                if(et!=BWAPI::UnitTypes::Terran_Barracks && et!=BWAPI::UnitTypes::Terran_Factory)continue;
                auto ep=existing->getTilePosition();
                if(t.x<ep.x+et.tileWidth()+(et==BWAPI::UnitTypes::Terran_Factory?3:1) &&
                   t.x+w>ep.x-1 && t.y<ep.y+et.tileHeight()+3 && t.y+h>ep.y-1){usable=false;break;}
            }
            if(!usable)continue;
            for(int yy=t.y;yy<t.y+h && usable;++yy)
                for(int xx=t.x;xx<t.x+w;++xx)
                    if(!map.free(xx,yy)) {usable=false;break;}
            if(!usable || !game->canBuildHere(t,type,builder,true))continue;
            AIV12Map hypothetical=map;
            hypothetical.block(t.x,t.y,w,h);
            if(type==BWAPI::UnitTypes::Terran_Factory ||
               type==BWAPI::UnitTypes::Terran_Starport){
                for(int yy=t.y+1;yy<t.y+3;++yy)
                    for(int xx=t.x+w;xx<t.x+w+2;++xx)
                        if(!map.free(xx,yy))usable=false;
                if(!usable)continue;
                hypothetical.block(t.x+w,t.y+1,2,2);
            }
            auto after=AIV12Flood(hypothetical,sx,sy);
            // All formerly accessible outer sectors must remain accessible.
            if((AIV12ExitSectors(hypothetical,after,home)&exits)!=exits)continue;
            if(wideExits){
                AIV12Map newWide=AIV12WideMap(hypothetical);
                auto reachedWide=AIV12Flood(newWide,wideX,wideY);
                if((AIV12ExitSectors(newWide,reachedWide,home)&wideExits)!=wideExits)
                    continue;
            }
            if(!AIV12SidesReachable(hypothetical,after,t,type))continue;
            for(auto res:resources){
                if(!AIV12ReachResource(hypothetical,after,res)){usable=false;break;}
            }
            if(!usable)continue;
            int score=std::abs(dx)*2+std::abs(dy)*2;
            if(clusteringSupply && !priorDepots.empty()) {
                score+=nearestDepot/12;
                if(zonePass==0 && depotGridAnchor.isValid()) {
                    // Among same-ring candidates, favor a tidy tight block.
                    score+=(std::abs(t.x-depotGridAnchor.x)+
                            std::abs(t.y-depotGridAnchor.y))*2;
                }
            }
            if(score<bestScore){bestScore=score;bestTile=t;}
        }
        if(bestTile.isValid()){
            if(zonePass==1){
                static int lastZoneNotice=-10000;
                int now=game->getFrameCount();
                if(now-lastZoneNotice>600){
                    game->printf("AI v14: safe fallback for %s",type.getName().c_str());
                    lastZoneNotice=now;
                }
            }
            return bestTile;
        }
    }
    }
    // Never silently fall back to a map-choking build site.
    static int lastWarning=-10000;
    int frame=game->getFrameCount();
    if(frame-lastWarning>600){
        game->printf("AI v12: no open build site for %s",type.getName().c_str());
        lastWarning=frame;
    }
    return BWAPI::TilePositions::None;
}

BWAPI::TilePosition AIV16NaturalSite(BWAPI::Unit builder);

BWAPI::TilePosition AIV12BuildingTile(BWAPI::UnitType type,BWAPI::Unit builder){
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if(!me || !builder)return BWAPI::TilePositions::None;
    const BWAPI::TilePosition home=me->getStartLocation();
    AIV12Map map=AIV12BuildMap(home);
    // Seed flood from an active worker on the accessible side of the CC.
    int sx=-1,sy=-1,best=1000000;
    const BWAPI::Position homePos(home);
    for(auto u:me->getUnits()){
        if(!u || !u->exists() || u->getType()!=BWAPI::UnitTypes::Terran_SCV)continue;
        auto p=u->getTilePosition();
        if(!map.free(p.x,p.y))continue;
        int d=u->getDistance(homePos);
        if(d<best){best=d;sx=p.x;sy=p.y;}
    }
    if(sx<0){
        for(int r=3;r<=9 && sx<0;++r)
            for(int dy=-r;dy<=r && sx<0;++dy)
                for(int dx=-r;dx<=r;++dx){
                    int x=home.x+dx,y=home.y+dy;
                    if(map.free(x,y)){sx=x;sy=y;break;}
                }
    }
    if(sx<0)return BWAPI::TilePositions::None;
    auto before=AIV12Flood(map,sx,sy);
    unsigned int exits=AIV12ExitSectors(map,before,home);
    AIV12Map wideBefore=AIV12WideMap(map);
    int wideX=sx,wideY=sy;
    unsigned int wideExits=0;
    if(AIV12WideSeed(wideBefore,sx,sy,wideX,wideY)){
        auto wb=AIV12Flood(wideBefore,wideX,wideY);
        wideExits=AIV12ExitSectors(wideBefore,wb,home);
    }
    std::vector<BWAPI::TilePosition> resources;
    for(auto u:game->getMinerals()){
        if(u && u->exists() && u->getDistance(homePos)<650 &&
           AIV12ReachResource(map,before,u->getTilePosition()))
            resources.push_back(u->getTilePosition());
    }
    for(auto u:game->getGeysers()){
        if(u && u->exists() && u->getDistance(homePos)<650 &&
           AIV12ReachResource(map,before,u->getTilePosition()))
            resources.push_back(u->getTilePosition());
    }
    const int w=type.tileWidth(),h=type.tileHeight();
    // STARTCRAFT_AI_V46_NATURAL_FRONT: the map centre is not the entrance on
    // many spawn positions.  On Destination it classified the mineral line as
    // "forward" and put the first Barracks behind the minerals.  The selected
    // natural is stable map geometry and is a much better entrance direction.
    const BWAPI::Position homePx(home.x*32+64,home.y*32+48);
    BWAPI::Position frontPx(game->mapWidth()*16,game->mapHeight()*16);
    const BWAPI::TilePosition naturalFront=AIV16NaturalSite(builder);
    if(naturalFront.isValid())frontPx=BWAPI::Position(naturalFront);
    int fx=frontPx.x-homePx.x,fy=frontPx.y-homePx.y;
    if(!fx && !fy) fx=32;
    int mineralDx=0,mineralDy=0,mineralCount=0;
    for(auto mineral:game->getMinerals())if(mineral && mineral->exists() &&
        mineral->getDistance(homePx)<650){
        mineralDx+=mineral->getPosition().x-homePx.x;
        mineralDy+=mineral->getPosition().y-homePx.y;
        ++mineralCount;
    }
    if(mineralCount){mineralDx/=mineralCount;mineralDy/=mineralCount;}
    const auto armyRoad=AIV35WideArmyRoute(map,home,
                 game->mapWidth()*16-homePx.x,game->mapHeight()*16-homePx.y);
    // STARTCRAFT_AI_V33_SUPPLY_CLUSTER: use open rear-edge pads, not main CC lanes.
    // For existing depots, try adjacent legal pads as a compact block.
    // The old safety/path/resource flood checks still apply; fallback remains.
    const bool clusteringSupply=(type==BWAPI::UnitTypes::Terran_Supply_Depot);
    std::vector<BWAPI::Position> priorDepots;
    BWAPI::TilePosition depotGridAnchor=BWAPI::TilePositions::None;
    int depotAnchorId=2147483647;
    if(clusteringSupply)for(auto u:me->getUnits())if(u && u->exists() &&
        u->getType()==BWAPI::UnitTypes::Terran_Supply_Depot) {
        priorDepots.push_back(u->getPosition());
        // STARTCRAFT_AI_V34_SUPPLY_GRID: never shift the grid as new depots appear.
        if(u->getID()<depotAnchorId){
            depotAnchorId=u->getID();depotGridAnchor=u->getTilePosition();
        }
    }
    // STARTCRAFT_AI_V35_CORNER_DEPOT_BLOCK: scan an entire outer wing for
    // supply, not merely the first available ring near the command center.
    // For other buildings retain the v34 near-to-far placement order.
    for(int zonePass=0;zonePass<3;++zonePass){
    if(!clusteringSupply && zonePass==2)break;
    BWAPI::TilePosition regionBest=BWAPI::TilePositions::None;
    int regionScore=100000000;
    const bool productionBuilding=type==BWAPI::UnitTypes::Terran_Barracks ||
        type==BWAPI::UnitTypes::Terran_Factory ||
        type==BWAPI::UnitTypes::Terran_Starport;
    std::vector<BWAPI::TilePosition> productionAnchors;
    if(productionBuilding)for(auto u:me->getUnits())
        if(u && u->exists() && !u->isFlying() &&
           (u->getType()==BWAPI::UnitTypes::Terran_Barracks ||
            u->getType()==BWAPI::UnitTypes::Terran_Factory ||
            u->getType()==BWAPI::UnitTypes::Terran_Starport))
            productionAnchors.push_back(u->getTilePosition());
    // Forward production may occupy the safe side of the main-to-natural
    // corridor. All path, resource, exit and add-on tests below still apply.
    const int outerRadius=productionBuilding?32:(clusteringSupply?24:35);
    for(int r=clusteringSupply?(zonePass==2?12:16):6;r<=outerRadius;++r){
        BWAPI::TilePosition bestTile=BWAPI::TilePositions::None;
        int bestScore=1000000;
        for(int dy=-r;dy<=r;++dy)for(int dx=-r;dx<=r;++dx){
            if((std::max)(std::abs(dx),std::abs(dy))!=r)continue;
            BWAPI::TilePosition t(home.x+dx,home.y+dy);
            if(!t.isValid())continue;
            if(zonePass==0 && !clusteringSupply && !AIV14ZonePreferred(type,dx,dy,r,fx,fy))continue;
            // Never lose the grid between the preferred and fallback passes.
            // A 3x2 supply occupies each 4x3 tile-cell, with a 1-tile aisle.
            if(clusteringSupply && depotGridAnchor.isValid()) {
                if((t.x-depotGridAnchor.x)%(w+1)!=0 ||
                   (t.y-depotGridAnchor.y)%(h+1)!=0)continue;
            }
            // Leave some open space around the initial command center.
            BWAPI::Position center(t.x*32+w*16,t.y*32+h*16);
            // A road or CC circulation pad stays empty even if placing the
            // building would leave *another* flood-fill sector reachable.
            if(AIV35ForbiddenBuildPad(center,home,me,(std::max)(w,h)*16))continue;
            bool roadFootprint=false;
            for(int yy=t.y;yy<t.y+h && !roadFootprint;++yy)
                for(int xx=t.x;xx<t.x+w;++xx){
                    int i=map.index(xx,yy);
                    if(i>=0 && armyRoad[i]){roadFootprint=true;break;}
                }
            if(roadFootprint)continue;
            if(center.getDistance(homePos)<175)continue;
            int nearestDepot=999999;
            if(clusteringSupply && !priorDepots.empty()) {
                for(const auto& depot:priorDepots)
                    nearestDepot=(std::min)(nearestDepot,static_cast<int>(depot.getDistance(center)));
                if(zonePass==0 && nearestDepot>240)continue;
            }
            int depotSiteScore=0;
            if(clusteringSupply){
                if(depotGridAnchor.isValid()){
                    const int col=std::abs(t.x-depotGridAnchor.x)/(w+1);
                    const int row=std::abs(t.y-depotGridAnchor.y)/(h+1);
                    // Fill the established wing as short straight rows.
                    depotSiteScore=col*12+row*20+(col+row)*3;
                    if(col>5)depotSiteScore+=(col-5)*90;
                    if(row>3)depotSiteScore+=(row-3)*90;
                }else{
                    // Rank the outer *map* edge over interior land;
                    // distance is in build tiles, not pixels.
                    const int nearestWall=(std::min)(
                       (std::min)(t.x,t.y),
                       (std::min)(game->mapWidth()-t.x-w,game->mapHeight()-t.y-h));
                    // Prefer a reachable rear wing near the base. The old
                    // nearest-wall term pulled long rows to the map boundary.
                    depotSiteScore=std::abs(r-17)*18+
                                   (std::max)(0,nearestWall<4?4-nearestWall:0)*35;
                    const long long front=(long long)dx*fx+(long long)dy*fy;
                    const long long norm=(std::max)(1,std::abs(fx)+std::abs(fy));
                    // Prefer a rear/side wing, but do not assume one exists
                    // on corner spawns (where 'rear' may be outside the map).
                    if(front>0)depotSiteScore+=(int)(front/norm)*10;
                }
                if(depotSiteScore>=regionScore)continue;
            }
            bool usable=true;
            // STARTCRAFT_AI_V37_1_PRODUCTION_EXIT: never build on a production
            // apron, and never place a new production building against map edge.
            const bool newProduction=type==BWAPI::UnitTypes::Terran_Barracks ||
                                     type==BWAPI::UnitTypes::Terran_Factory ||
                                     type==BWAPI::UnitTypes::Terran_Starport;
            // Never place production on the far side of the main mineral
            // line.  It lengthens every rally and can trap units among miners.
            if(newProduction && mineralCount){
                const int mineralNorm=(std::max)(1,(std::max)(std::abs(mineralDx),std::abs(mineralDy)));
                const long long mineralSide=(long long)(center.x-homePx.x)*mineralDx+
                                            (long long)(center.y-homePx.y)*mineralDy;
                if(mineralSide/mineralNorm>96)continue;
            }
            if(newProduction && (t.x<2 || t.y<2 || t.x+w+3>=game->mapWidth() ||
                                 t.y+h+3>=game->mapHeight()))continue;
            for(auto existing:me->getUnits()){
                if(!existing || !existing->exists())continue;
                const auto et=existing->getType();
                if(et!=BWAPI::UnitTypes::Terran_Barracks &&
                   et!=BWAPI::UnitTypes::Terran_Factory)continue;
                const auto ep=existing->getTilePosition();
                const int left=ep.x-1,top=ep.y-1;
                const int right=ep.x+et.tileWidth()+(et==BWAPI::UnitTypes::Terran_Factory?3:1);
                const int bottom=ep.y+et.tileHeight()+3;
                if(t.x<right && t.x+w>left && t.y<bottom && t.y+h>top){usable=false;break;}
            }
            if(!usable)continue;
            for(int yy=t.y;yy<t.y+h && usable;++yy)
                for(int xx=t.x;xx<t.x+w;++xx)
                    if(!map.free(xx,yy)) {usable=false;break;}
            if(!usable || !game->canBuildHere(t,type,builder,true))continue;
            AIV12Map hypothetical=map;
            hypothetical.block(t.x,t.y,w,h);
            if(type==BWAPI::UnitTypes::Terran_Factory){
                for(int yy=t.y+1;yy<t.y+3;++yy)
                    for(int xx=t.x+w;xx<t.x+w+2;++xx)
                        if(!map.free(xx,yy))usable=false;
                if(!usable)continue;
                hypothetical.block(t.x+w,t.y+1,2,2);
            }
            auto after=AIV12Flood(hypothetical,sx,sy);
            // All formerly accessible outer sectors must remain accessible.
            if((AIV12ExitSectors(hypothetical,after,home)&exits)!=exits)continue;
            if(wideExits){
                AIV12Map newWide=AIV12WideMap(hypothetical);
                auto reachedWide=AIV12Flood(newWide,wideX,wideY);
                if((AIV12ExitSectors(newWide,reachedWide,home)&wideExits)!=wideExits)
                    continue;
            }
            if(!AIV12SidesReachable(hypothetical,after,t,type))continue;
            for(auto res:resources){
                if(!AIV12ReachResource(hypothetical,after,res)){usable=false;break;}
            }
            if(!usable)continue;
            int score=clusteringSupply ? depotSiteScore :
                      (std::abs(dx)*2+std::abs(dy)*2);
            // STARTCRAFT_AI_V43_FORWARD_PRODUCTION: even when the strict
            // preferred zone has no legal pad, rank Barracks/Factories toward
            // the entrance rather than choosing the closest rear tile. The
            // road, resource, exit and add-on safety checks above still win.
            if(productionBuilding){
                const long long front=(long long)dx*fx+(long long)dy*fy;
                const int norm=(std::max)(1,std::abs(fx)+std::abs(fy));
                int forward=(int)(front/norm);
                forward=(std::max)(-20,(std::min)(20,forward));
                score-=forward*14;
            }
            // STARTCRAFT_AI_V38_PRODUCTION_ROWS: after the first production
            // building, prefer a nearby aligned row instead of another ring
            // around the command center. Exit and add-on checks above remain.
            if(productionBuilding && !productionAnchors.empty()) {
                int nearest=1000000,alignment=1000000;
                for(const auto& anchor:productionAnchors) {
                    nearest=(std::min)(nearest,
                        std::abs(t.x-anchor.x)+std::abs(t.y-anchor.y));
                    alignment=(std::min)(alignment,
                        (std::min)(std::abs(t.x-anchor.x),std::abs(t.y-anchor.y)));
                }
                if(zonePass==0 && nearest>13)continue;
                score+=nearest*9+alignment*18;
            }
            if(clusteringSupply){
                if(score<regionScore){regionScore=score;regionBest=t;}
            }else if(score<bestScore){bestScore=score;bestTile=t;}
        }
        if(!clusteringSupply && bestTile.isValid()){
            if(zonePass==1){
                static int lastZoneNotice=-10000;
                int now=game->getFrameCount();
                if(now-lastZoneNotice>600){
                    game->printf("AI v14: safe fallback for %s",type.getName().c_str());
                    lastZoneNotice=now;
                }
            }
            return bestTile;
        }
    }
    // Among all candidates in the rear/side corner, select the safest
    // buildable grid pad without falling back toward the command center.
    if(clusteringSupply && regionBest.isValid()){
        if(zonePass==2){
            game->printf("AI v35: external depot area full; safe grid fallback");
        }
        return regionBest;
    }
    }
    // Never silently fall back to a map-choking build site.
    static int lastWarning=-10000;
    int frame=game->getFrameCount();
    if(frame-lastWarning>600){
        game->printf("AI v12: no open build site for %s",type.getName().c_str());
        lastWarning=frame;
    }
    // STARTCRAFT_AI_V35_1_NO_BUILD_STALL:
    // v35 forbade too many pads on corner spawns (e.g. bottom-right 128x128).
    // Never deadlock before the FIRST depot/barracks. The old v34 location
    // planner retains passability/flood/resource/Factory-addon checks.
    BWAPI::TilePosition recovery=AIV351LegacyBuildTile(type,builder);
    if(recovery.isValid()) {
        static int lastRecoveryNotice=-10000;
        const int now=game->getFrameCount();
        if(now-lastRecoveryNotice>100) {
            game->printf("AI v35.1: safe construction fallback %s @%d,%d",
                        type.getName().c_str(),recovery.x,recovery.y);
            lastRecoveryNotice=now;
        }
    }
    return recovery;
}



// STARTCRAFT_AI_V35_2_BUILD_DIAGNOSTICS
static void AIV353Log(const char* stage,BWAPI::UnitType type,
                     BWAPI::Unit worker,BWAPI::TilePosition tile,
                     std::string error="") {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();if(!me)return;
    const char* dir=R"(C:\Users\User\starcraft-api\STARTcraft\AI_reports)";
    const char* path=R"(C:\Users\User\starcraft-api\STARTcraft\AI_reports\AI_v58_construction.csv)";
    _mkdir(dir);
    std::ifstream prior(path,std::ios::binary);
    const bool header=!prior.good() || prior.peek()==std::ifstream::traits_type::eof();
    prior.close();
    std::ofstream out(path,std::ios::app);
    if(!out){game->printf("AI v36.1: construction log unavailable");return;}
    for(char& c:error)if(c==',' || c=='\n' || c=='\r')c=' ';
    if(header)out<<"version,frame,seconds,stage,building,worker,tile_x,tile_y,minerals,gas,error\n";
    out<<"58,"<<game->getFrameCount()<<','<<game->elapsedTime()<<','<<stage<<','
       <<type.getName()<<','<<(worker?worker->getID():-1)<<','<<tile.x<<','<<tile.y<<','
       <<me->minerals()<<','<<me->gas()<<','<<error<<'\n';
}

// STARTCRAFT_AI_V35_3_BUILD_LIFECYCLE
struct AIV353BuildTask {
    BWAPI::UnitType type;
    BWAPI::TilePosition tile;
    int workerId,issuedFrame,buildingId,lastRetryFrame,replacementAttempts;
    bool orderChanged,workerLost;
    std::vector<int> existingBuildingIds;
};
static std::vector<AIV353BuildTask> g_v353Tasks;
static int g_v353WatchPreviousFrame=-1;

static std::string AIV353TaskDetail(const AIV353BuildTask& task) {
    return "issued_frame="+std::to_string(task.issuedFrame)+
           ";building_id="+std::to_string(task.buildingId);
}

static void AIV353TrackBuild(BWAPI::Unit worker,BWAPI::UnitType type,
                            BWAPI::TilePosition tile) {
    if(!worker)return;
    const int frame=BWAPI::Broodwar->getFrameCount();
    for(const auto& active:g_v353Tasks)
        if(active.workerId==worker->getID() && active.type==type &&
           active.tile==tile)return;
    const int hold=type==BWAPI::UnitTypes::Terran_Command_Center?1080:720;
    g_v39BuilderUntil[worker->getID()]=frame+hold;
    AIV353BuildTask task{type,tile,worker->getID(),frame,-1,frame,0,false,false,{}};
    auto me=BWAPI::Broodwar->self();
    if(me)for(auto unit:me->getUnits())
        if(unit && unit->exists() && unit->getType()==type)
            task.existingBuildingIds.push_back(unit->getID());
    g_v353Tasks.push_back(std::move(task));
    ++g_v50TrackedPendingBuilds[type.getID()];
    AIV353Log("ACCEPTED",type,worker,tile,"issued_frame="+std::to_string(frame));
}

static void AIV50ReleaseTrackedBuild(BWAPI::UnitType type) {
    auto found=g_v50TrackedPendingBuilds.find(type.getID());
    if(found==g_v50TrackedPendingBuilds.end())return;
    if(found->second>1)--found->second;
    else g_v50TrackedPendingBuilds.erase(found);
}

// Observe accepted construction before and after the directors. If another
// command or a pathing stall prevents the foundation from starting, restore
// the same owned build task without selecting another worker or site.
static void AIV353WatchBuilds() {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();if(!me)return;
    const int frame=game->getFrameCount();
    if(frame<g_v353WatchPreviousFrame)g_v353Tasks.clear();
    g_v353WatchPreviousFrame=frame;
    for(auto it=g_v353Tasks.begin();it!=g_v353Tasks.end();) {
        BWAPI::Unit worker=nullptr,building=nullptr;
        for(auto unit:me->getUnits()) {
            if(!unit || !unit->exists())continue;
            if(unit->getID()==it->workerId &&
               unit->getType()==BWAPI::UnitTypes::Terran_SCV)worker=unit;
            if(it->buildingId>=0) {
                if(unit->getID()==it->buildingId && unit->getType()==it->type)
                    building=unit;
            }else if(!unit->isFlying() && unit->getType()==it->type &&
                     unit->getTilePosition()==it->tile &&
                     std::find(it->existingBuildingIds.begin(),
                               it->existingBuildingIds.end(),unit->getID())==
                         it->existingBuildingIds.end())building=unit;
        }
        if(building) {
            if(it->buildingId<0) {
                it->buildingId=building->getID();
                AIV353Log("STARTED",it->type,worker,it->tile,AIV353TaskDetail(*it));
            }
            // STARTCRAFT_AI_V54_RESUME_CONSTRUCTION: the v53 Pluto match lost
            // the Factory builder at 6:05 and left the unfinished Factory
            // stopped until 12:36. A Terran building needs a live SCV, so hand
            // an interrupted foundation to the nearest safe worker immediately.
            if(!building->isCompleted() && !worker && it->replacementAttempts<2 &&
               frame-it->lastRetryFrame>=24) {
                BWAPI::Unit replacement=AIV11BuildWorker(building->getPosition());
                it->lastRetryFrame=frame;
                if(replacement && !replacement->isUnderAttack() &&
                   replacement->hasPath(building->getPosition()) &&
                   replacement->rightClick(building)) {
                    g_v39BuilderUntil.erase(it->workerId);
                    it->workerId=replacement->getID();
                    ++it->replacementAttempts;
                    it->workerLost=false;it->orderChanged=false;
                    const int hold=it->type==BWAPI::UnitTypes::Terran_Command_Center?1080:720;
                    g_v39BuilderUntil[it->workerId]=frame+hold;
                    worker=replacement;
                    AIV353Log("CONSTRUCTION_RESUMED",it->type,worker,it->tile,
                        AIV353TaskDetail(*it)+";attempt="+
                        std::to_string(it->replacementAttempts));
                }
            }
            if(building->isCompleted()) {
                AIV353Log("COMPLETED",it->type,worker,it->tile,AIV353TaskDetail(*it));
                AIV50ReleaseTrackedBuild(it->type);
                g_v39BuilderUntil.erase(it->workerId);
                it=g_v353Tasks.erase(it);continue;
            }
        }else if(it->buildingId>=0) {
            AIV353Log("BUILDING_LOST",it->type,worker,it->tile,AIV353TaskDetail(*it));
            if(it->type==BWAPI::UnitTypes::Terran_Command_Center) {
                // Losing an expansion is strategic evidence. Rebuild an army
                // for 150 seconds instead of donating another SCV and 400 M.
                g_v11NoExpansionUntil=(std::max)(g_v11NoExpansionUntil,frame+2400);
                g_v11Reserve=0;
            }
            AIV50ReleaseTrackedBuild(it->type);
            g_v39BuilderUntil.erase(it->workerId);
            it=g_v353Tasks.erase(it);continue;
        }
        // STARTCRAFT_AI_V53_BUILDER_HANDOFF: a killed worker no longer drops
        // an accepted defensive or production task. Rebind the exact site to
        // the nearest safe SCV before the normal director can spend the money.
        if(!worker && !building && it->replacementAttempts<2 &&
           frame-it->lastRetryFrame>=24 &&
           it->type!=BWAPI::UnitTypes::Terran_Command_Center) {
            const BWAPI::Position site(it->tile.x*32+it->type.tileWidth()*16,
                                       it->tile.y*32+it->type.tileHeight()*16);
            BWAPI::Unit replacement=AIV11BuildWorker(site);
            it->lastRetryFrame=frame;
            if(replacement && !replacement->isUnderAttack() &&
               me->minerals()>=it->type.mineralPrice() &&
               me->gas()>=it->type.gasPrice() && replacement->hasPath(site) &&
               replacement->build(it->type,it->tile)) {
                g_v39BuilderUntil.erase(it->workerId);
                it->workerId=replacement->getID();
                it->lastRetryFrame=frame;
                ++it->replacementAttempts;
                it->workerLost=false;it->orderChanged=false;
                const int hold=it->type==BWAPI::UnitTypes::Terran_Command_Center?1080:720;
                g_v39BuilderUntil[it->workerId]=frame+hold;
                worker=replacement;
                AIV353Log("BUILDER_REPLACED",it->type,worker,it->tile,
                    AIV353TaskDetail(*it)+";attempt="+
                    std::to_string(it->replacementAttempts));
            }
        }
        if(!worker && !it->workerLost) {
            it->workerLost=true;
            AIV353Log("WORKER_LOST",it->type,nullptr,it->tile,AIV353TaskDetail(*it));
            if(!building){
                if(it->type==BWAPI::UnitTypes::Terran_Command_Center) {
                    g_v11NoExpansionUntil=(std::max)(g_v11NoExpansionUntil,frame+2400);
                    g_v11Reserve=0;
                    game->printf("v49 EXPAND: builder lost; retry delayed for defense");
                }
                if(it->type!=BWAPI::UnitTypes::Terran_Command_Center &&
                   it->replacementAttempts<2) {
                    ++it;continue;
                }
                AIV50ReleaseTrackedBuild(it->type);
                g_v39BuilderUntil.erase(it->workerId);
                it=g_v353Tasks.erase(it);continue;}
        }
        if(worker && !it->orderChanged) {
            const auto command=worker->getLastCommand();
            if(command.getType()!=BWAPI::UnitCommandTypes::Build ||
               command.getUnitType()!=it->type ||
               command.getTargetTilePosition()!=it->tile ||
               worker->getLastCommandFrame()!=(std::max)(it->issuedFrame,it->lastRetryFrame)) {
                it->orderChanged=true;
                AIV353Log("ORDER_CHANGED",it->type,worker,it->tile,
                    AIV353TaskDetail(*it)+";last_command="+command.getType().getName());
            }
        }
        // Accepted BWAPI build orders occasionally stall while the SCV is
        // walking: v50 lost 45 seconds on both the Refinery and Factory. The
        // task owns this worker, so restore the exact build command instead of
        // waiting for the full timeout and selecting a different SCV.
        if(!building && worker && !worker->isUnderAttack() &&
           frame-it->issuedFrame>=144 && frame-it->lastRetryFrame>=96 &&
           me->minerals()>=it->type.mineralPrice() &&
           me->gas()>=it->type.gasPrice()) {
            const BWAPI::Position site(it->tile.x*32+it->type.tileWidth()*16,
                                       it->tile.y*32+it->type.tileHeight()*16);
            if(worker->hasPath(site) && worker->build(it->type,it->tile)) {
                it->lastRetryFrame=frame;it->orderChanged=false;
                AIV353Log("RETRY_ACCEPTED",it->type,worker,it->tile,AIV353TaskDetail(*it));
            } else {
                it->lastRetryFrame=frame;
            }
        }
        const int limit=it->type==BWAPI::UnitTypes::Terran_Command_Center?1080:720;
        if(!building && frame-it->issuedFrame>=limit) {
            AIV353Log("NOT_STARTED_TIMEOUT",it->type,worker,it->tile,AIV353TaskDetail(*it));
            AIV50ReleaseTrackedBuild(it->type);
            g_v39BuilderUntil.erase(it->workerId);
            it=g_v353Tasks.erase(it);continue;
        }
        ++it;
    }
}

static std::map<int,int> g_v40SiteRetryAfter;
bool AIV11BuildNear(BWAPI::UnitType type, BWAPI::TilePosition desired=BWAPI::TilePositions::None) {
    auto& game=BWAPI::Broodwar;
    const int now=game->getFrameCount();
    if(now<g_v39OwnershipFrame)g_v40SiteRetryAfter.clear();
    if(g_v40SiteRetryAfter[type.getID()]>now)return false;
    auto me=game->self();
    if(!me || me->minerals()<type.mineralPrice() || me->gas()<type.gasPrice() ||
       AIV11PendingBuild(type))return false;
    // Every production caller must obey the one-Barracks TvP plan.
    if(type==BWAPI::UnitTypes::Terran_Barracks &&
       AIV13OpponentRace()==BWAPI::Races::Protoss)
        for(auto u:me->getUnits())if(u && u->exists() && u->getType()==type)return false;
    // A requested defensive pad needs the nearest suitable SCV.  Choosing an
    // arbitrary main-base worker made accepted natural-turret orders time out
    // before construction started.
    BWAPI::Unit builder=desired.isValid()
        ?AIV11BuildWorker(BWAPI::Position(desired)):AIV11BuildWorker();
    if(!builder){AIV353Log("NO_WORKER",type,nullptr,BWAPI::TilePositions::None);return false;}
    // STARTCRAFT_AI_V54_TECH_GUARD: v53 issued 41 Starport build commands
    // while BWAPI reported Insufficient_Tech. Back off before placement and
    // command ownership when the selected SCV cannot make this type yet.
    if(!game->canMake(type,builder)) {
        AIV353Log("TECH_NOT_READY",type,builder,BWAPI::TilePositions::None);
        g_v40SiteRetryAfter[type.getID()]=now+240;
        return false;
    }
    BWAPI::TilePosition tile=desired;
    if(!tile.isValid())tile=AIV12BuildingTile(type,builder);
    if(!tile.isValid() && type==BWAPI::UnitTypes::Terran_Supply_Depot) {
        const BWAPI::Position main(me->getStartLocation());
        for(auto cc:me->getUnits()) {
            if(!cc || !cc->exists() || !cc->isCompleted() || cc->isFlying() ||
               cc->getType()!=BWAPI::UnitTypes::Terran_Command_Center ||
               cc->getDistance(main)<500)continue;
            BWAPI::TilePosition candidate=game->getBuildLocation(
                type,cc->getTilePosition(),16,false);
            if(candidate.isValid() && game->canBuildHere(candidate,type,builder,true)) {
                bool nearResource=false;
                BWAPI::Position center(candidate);
                for(auto m:game->getMinerals())if(m && m->exists() &&
                    m->getDistance(center)<240)nearResource=true;
                for(auto g:game->getGeysers())if(g && g->exists() &&
                    g->getDistance(center)<260)nearResource=true;
                if(!nearResource){tile=candidate;break;}
            }
        }
    }
    if(!tile.isValid()){
        AIV353Log("NO_SITE",type,builder,tile);
        g_v40SiteRetryAfter[type.getID()]=now+120;
        return false;
    }
    // STARTCRAFT_AI_V37_3_MAIN_BASE_PRODUCTION: production belongs to the
    // main-base footprint. A safe planner fallback may find a legal distant
    // pad, but accepting it creates long rallies and scattered simcity.
    const bool productionBuilding=
        type==BWAPI::UnitTypes::Terran_Barracks ||
        type==BWAPI::UnitTypes::Terran_Factory ||
        type==BWAPI::UnitTypes::Terran_Starport;
    if(productionBuilding) {
        const BWAPI::TilePosition home=me->getStartLocation();
        const int dx=std::abs(tile.x-home.x),dy=std::abs(tile.y-home.y);
        int nearestProduction=1000000;
        const BWAPI::Position site(tile.x*32+type.tileWidth()*16,
                                   tile.y*32+type.tileHeight()*16);
        for(auto b:me->getUnits())if(b && b->exists() &&
           (b->getType()==BWAPI::UnitTypes::Terran_Barracks ||
            b->getType()==BWAPI::UnitTypes::Terran_Factory ||
            b->getType()==BWAPI::UnitTypes::Terran_Starport))
            nearestProduction=(std::min)(nearestProduction,b->getDistance(site));
        bool nearExpansion=false;
        const BWAPI::Position origin(home);
        for(auto cc:me->getUnits())if(cc && cc->exists() && !cc->isFlying() &&
            cc->getType()==BWAPI::UnitTypes::Terran_Command_Center &&
            cc->getDistance(origin)>500 && cc->getDistance(site)<620)
            nearExpansion=true;
        if((std::max)(dx,dy)>32 ||
           ((std::max)(dx,dy)>16 && nearestProduction>520 && !nearExpansion)) {
            AIV353Log("ZONE_REJECTED",type,builder,tile,"outside_production_campus");
            g_v40SiteRetryAfter[type.getID()]=now+240;
            return false;
        }
    }
    if((type==BWAPI::UnitTypes::Terran_Barracks || type==BWAPI::UnitTypes::Terran_Factory) &&
       (tile.x<2 || tile.y<2 || tile.x+type.tileWidth()+3>=game->mapWidth() ||
        tile.y+type.tileHeight()+3>=game->mapHeight())){
        AIV353Log("EXIT_REJECTED",type,builder,tile,"map_edge");return false;
    }
    for(auto existing:me->getUnits())if(existing && existing->exists() &&
       (existing->getType()==BWAPI::UnitTypes::Terran_Barracks ||
        existing->getType()==BWAPI::UnitTypes::Terran_Factory)){
        auto ep=existing->getTilePosition();auto et=existing->getType();
        if(tile.x<ep.x+et.tileWidth()+(et==BWAPI::UnitTypes::Terran_Factory?3:1) &&
           tile.x+type.tileWidth()>ep.x-1 && tile.y<ep.y+et.tileHeight()+3 &&
           tile.y+type.tileHeight()>ep.y-1){
            AIV353Log("EXIT_REJECTED",type,builder,tile,"production_apron");return false;
        }
    }
    if(!game->canBuildHere(tile,type,builder,true) ||
       (desired.isValid() && !builder->hasPath(BWAPI::Position(tile)))){
        AIV353Log("SITE_REJECTED",type,builder,tile,
                  !builder->hasPath(BWAPI::Position(tile))?"No_Path":game->getLastError().getName());
        return false;
    }
    if(builder->build(type,tile)) {
        AIV353TrackBuild(builder,type,tile);
        g_v11LastBuildOrder=game->getFrameCount();
        // STARTCRAFT_AI_V35_SITE_REPORT: exact pads for next-match verification.
        if(type==BWAPI::UnitTypes::Terran_Supply_Depot ||
           type==BWAPI::UnitTypes::Terran_Barracks ||
           type==BWAPI::UnitTypes::Terran_Factory ||
           type==BWAPI::UnitTypes::Terran_Academy){
            const char* path=R"(C:\Users\User\starcraft-api\STARTcraft\AI_reports\AI_v35_layout.csv)";
            _mkdir(R"(C:\Users\User\starcraft-api\STARTcraft\AI_reports)");
            static int lastBuildFrame=-1;
            const bool reset=game->getFrameCount()<lastBuildFrame;
            std::ofstream log(path,(reset || lastBuildFrame<0)?std::ios::trunc:std::ios::app);
            if(log){
                if(reset || lastBuildFrame<0)log<<"frame,seconds,building,tile_x,tile_y,base_x,base_y\n";
                const BWAPI::TilePosition start=me->getStartLocation();
                log<<game->getFrameCount()<<','<<game->elapsedTime()<<','
                   <<type.getName()<<','<<tile.x<<','<<tile.y<<','
                   <<start.x<<','<<start.y<<'\n';
            }
            lastBuildFrame=game->getFrameCount();
        }
        game->printf("v11 build: %s @%d,%d",type.getName().c_str(),tile.x,tile.y);
        return true;
    }
    AIV353Log("COMMAND_REJECTED",type,builder,tile,game->getLastError().getName());
    return false;
}

// Only one supply depot being built at a time; enough headroom for units in training.
bool AIV11Supply(AIV11Counts c) {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if(me->supplyTotal()>=400 || c.unfinishedDepot>0 || me->minerals()<100 ||
       AIV11PendingBuild(BWAPI::UnitTypes::Terran_Supply_Depot))return false;
    int free=me->supplyTotal()-me->supplyUsed();
    int production=c.completedCC+c.barracks*2+c.factory*3;
    int buffer=(std::min)(22,6+production*2);
    if(free>buffer)return false;
    // Avoid a second supply depot before the first Barracks.
    if(c.barracks==0 && free>3 && c.scv<14)return false;
    return AIV11BuildNear(BWAPI::UnitTypes::Terran_Supply_Depot);
}

bool AIV11Refinery(AIV11Counts c) {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if(c.scv<10 || me->minerals()<100 || c.refinery>=c.completedCC ||
       AIV11PendingBuild(BWAPI::UnitTypes::Terran_Refinery))return false;
    for(auto base:me->getUnits()){
        if(base->getType()!=BWAPI::UnitTypes::Terran_Command_Center || !base->isCompleted())continue;
        for(auto geyser:game->getGeysers()){
            if(!geyser || !geyser->exists() || geyser->getDistance(base)>500)continue;
            bool taken=false;
            for(auto u:me->getUnits())
                if(u->getType()==BWAPI::UnitTypes::Terran_Refinery && u->getDistance(geyser)<110)
                    taken=true;
            if(taken)continue;
            auto w=AIV11BuildWorker(geyser->getPosition());
            if(w && w->build(BWAPI::UnitTypes::Terran_Refinery,geyser->getTilePosition())){
                AIV353TrackBuild(w,BWAPI::UnitTypes::Terran_Refinery,geyser->getTilePosition());
                g_v11LastBuildOrder=game->getFrameCount();
                game->printf("v11: refinery started");
                return true;
            }
        }
    }
    return false;
}

// Uses known mineral clusters, not arbitrary empty land. No reservation if no location.
// STARTCRAFT_AI_V15: natural expansion selection and Terran detection.
// This block lives in the existing AIV11 anonymous namespace.
// No assumptions about which starting position or map orientation is in use.

// Static map walkability: the build planner decides whether the chosen pad can
// host a CC; this grid only supplies a conservative ground-distance estimate.
std::vector<int> AIV15GroundDistances(BWAPI::TilePosition home) {
    auto& game = BWAPI::Broodwar;
    const int w = game->mapWidth(), h = game->mapHeight();
    std::vector<int> distances(w*h, -1);
    std::vector<unsigned char> open(w*h, 0);
    if(w<=0 || h<=0 || !home.isValid())return distances;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        open[y*w+x] = game->isWalkable(x*4+1,y*4+1) &&
                       game->isWalkable(x*4+2,y*4+2) ? 1:0;
    }
    // Mineral walls and geysers are impassable and can otherwise produce a
    // false short path through the centre of resource patches.
    for(auto m:game->getStaticMinerals()) {
        if(!m)continue;
        auto t=m->getInitialTilePosition();
        for(int yy=t.y;yy<t.y+m->getType().tileHeight();++yy)
            for(int xx=t.x;xx<t.x+m->getType().tileWidth();++xx)
                if(xx>=0&&yy>=0&&xx<w&&yy<h)open[yy*w+xx]=0;
    }
    for(auto g:game->getStaticGeysers()) {
        if(!g)continue;
        auto t=g->getInitialTilePosition();
        for(int yy=t.y;yy<t.y+g->getType().tileHeight();++yy)
            for(int xx=t.x;xx<t.x+g->getType().tileWidth();++xx)
                if(xx>=0&&yy>=0&&xx<w&&yy<h)open[yy*w+xx]=0;
    }
    // The Command Center occupies its own tile; seed from nearby clear land.
    int start=-1;
    for(int r=1;r<=12 && start<0;++r)
        for(int dy=-r;dy<=r && start<0;++dy)
            for(int dx=-r;dx<=r;++dx) {
                int x=home.x+2+dx, y=home.y+1+dy;
                if(x>=0&&y>=0&&x<w&&y<h&&open[y*w+x]){
                    start=y*w+x;break;
                }
            }
    if(start<0)return distances;
    std::vector<int> queue;
    queue.reserve(w*h);queue.push_back(start);distances[start]=0;
    for(size_t i=0;i<queue.size();++i){
        int at=queue[i],x=at%w,y=at/w;
        const int nx[4]={x+1,x-1,x,x},ny[4]={y,y,y+1,y-1};
        for(int j=0;j<4;++j){
            int xx=nx[j],yy=ny[j];
            if(xx<0||yy<0||xx>=w||yy>=h)continue;
            int n=yy*w+xx;
            if(open[n] && distances[n]<0){distances[n]=distances[at]+1;queue.push_back(n);}
        }
    }
    return distances;
}

// Natural-first selection: use an UNCLAIMED mineral + gas cluster with the
// shortest usable ground route, not a random centre-map mineral cluster.
BWAPI::TilePosition AIV15NaturalExpansionSite(BWAPI::Unit builder) {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if(!me || !builder)return BWAPI::TilePositions::None;
    const auto cc=BWAPI::UnitTypes::Terran_Command_Center;
    const auto homeTile=me->getStartLocation();
    const BWAPI::Position home(homeTile);
    const int w=game->mapWidth(),h=game->mapHeight();
    auto ground=AIV15GroundDistances(homeTile);
    BWAPI::TilePosition best=BWAPI::TilePositions::None;
    int bestScore=100000000;
    for(auto geyser:game->getStaticGeysers()){
        if(!geyser)continue;
        BWAPI::Position gp=geyser->getInitialPosition();
        if(!gp.isValid() || gp.getDistance(home)<530 || gp.getDistance(home)>3000)continue;
        int cluster=0;
        for(auto mineral:game->getStaticMinerals())
            if(mineral && mineral->getInitialPosition().isValid() &&
               mineral->getInitialPosition().getDistance(gp)<410)++cluster;
        if(cluster<5)continue;
        BWAPI::TilePosition gt=geyser->getInitialTilePosition();
        // Search around the geyser for a depot pad with reasonable resource
        // distances. No fallback to arbitrary map terrain or mineral patches.
        for(int dy=-13;dy<=13;dy+=2)for(int dx=-13;dx<=13;dx+=2){
            BWAPI::TilePosition candidate(gt.x+dx,gt.y+dy);
            if(candidate.x<0 || candidate.y<0 || candidate.x+4>=w ||
               candidate.y+3>=h)continue;
            BWAPI::Position pos(candidate.x*32+64,candidate.y*32+48);
            int homeAir=pos.getDistance(home);
            if(homeAir<530 || homeAir>2600)continue;
            int gasDistance=pos.getDistance(gp);
            if(gasDistance<165 || gasDistance>455)continue;
            int minerals=0;
            for(auto m:game->getStaticMinerals()){
                if(!m || !m->getInitialPosition().isValid())continue;
                int d=m->getInitialPosition().getDistance(pos);
                if(d>=125 && d<=380)++minerals;
            }
            if(minerals<5)continue;
            // Prevent trying to occupy main or a known enemy start position.
            bool blocked=false;
            for(auto u:me->getUnits()){
                if(u && u->exists() && u->getType()==cc && u->getDistance(pos)<625){
                    blocked=true;break;
                }
            }
            if(blocked)continue;
            for(auto start:game->getStartLocations()){
                if(start==homeTile)continue;
                if(pos.getDistance(BWAPI::Position(start))<620){blocked=true;break;}
            }
            if(blocked)continue;
            for(auto enemy:game->enemies())for(auto e:enemy->getUnits()){
                if(e && e->exists() && e->isVisible() && e->getType().isBuilding() &&
                   e->getDistance(pos)<530){blocked=true;break;}
            }
            if(blocked || !game->canBuildHere(candidate,cc,builder) ||
               !builder->hasPath(pos))continue;
            // Reachability grid is measured on static land to disfavor resource
            // clusters beyond a ridge or behind a mineral wall.
            int groundSteps=1000000;
            for(int yy=candidate.y-2;yy<=candidate.y+5;++yy)
                for(int xx=candidate.x-2;xx<=candidate.x+5;++xx){
                    if(xx>=0&&yy>=0&&xx<w&&yy<h && ground[yy*w+xx]>=0)
                        groundSteps=(std::min)(groundSteps,ground[yy*w+xx]);
                }
            if(groundSteps>=1000000)continue;
            // Shortest GROUND path first, balanced resource cluster second.
            int score=groundSteps*32 - (std::min)(8,minerals)*17 +
                      (std::abs)(gasDistance-270)/2;
            if(score<bestScore){bestScore=score;best=candidate;}
        }
    }
    return best;
}

int g_v15LastScan=-10000;
int g_v15LastDetectionOrder=-10000;
int g_v15PrevFrame=-1;
void AIV15DetectionDirector(){
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if(!me || me->getRace()!=BWAPI::Races::Terran)return;
    int now=game->getFrameCount();
    if(now<g_v15PrevFrame){
        g_v15LastScan=g_v15LastDetectionOrder=-10000;
    }
    g_v15PrevFrame=now;
    if(now%24)return;
    bool academy=false, engineering=false, engineeringCompleted=false, engineeringPending=false;
    for(auto u:me->getUnits()) {
        if(!u || !u->exists())continue;
        if(u->getType()==BWAPI::UnitTypes::Terran_Academy && u->isCompleted())academy=true;
        if(u->getType()==BWAPI::UnitTypes::Terran_Engineering_Bay){
            engineering=true;
            if(u->isCompleted())engineeringCompleted=true;
            else engineeringPending=true;
        }
    }
    if(academy){
        for(auto cc:me->getUnits()){
            if(!cc || cc->getType()!=BWAPI::UnitTypes::Terran_Command_Center ||
               !cc->isCompleted() || cc->getAddon())continue;
            if(me->minerals()>=100 && me->gas()>=50 &&
               game->canMake(BWAPI::UnitTypes::Terran_Comsat_Station,cc) &&
               cc->buildAddon(BWAPI::UnitTypes::Terran_Comsat_Station)){
                game->printf("v15 DETECTION: building Comsat");
                break;
            }
        }
    }
    // Defensive tech and one turret per occupied base: no turret spam.
    if(academy && !engineering && !engineeringPending &&
       !AIV11PendingBuild(BWAPI::UnitTypes::Terran_Engineering_Bay) &&
       now-g_v15LastDetectionOrder>120 &&
       now-g_v11LastBuildOrder>120 && me->minerals()>=250){
        if(AIV11BuildNear(BWAPI::UnitTypes::Terran_Engineering_Bay))
            g_v15LastDetectionOrder=now;
    }
    if(engineeringCompleted && now-g_v15LastDetectionOrder>120 &&
       now-g_v11LastBuildOrder>120 &&
       !AIV11PendingBuild(BWAPI::UnitTypes::Terran_Missile_Turret) &&
       me->minerals()>=150){
        for(auto cc:me->getUnits()){
            if(!cc || !cc->exists() || !cc->isCompleted() ||
               cc->getType()!=BWAPI::UnitTypes::Terran_Command_Center)continue;
            int turrets=0;
            for(auto u:me->getUnits())
                if(u && u->exists() && u->getType()==BWAPI::UnitTypes::Terran_Missile_Turret &&
                   u->getDistance(cc)<620)++turrets;
            if(turrets>0)continue;
            // The established v12/v14 placement planner keeps the first
            // turret from sealing the main exit. Do not force unsafe pads.
            if(cc->getTilePosition()==me->getStartLocation()){
                if(AIV11BuildNear(BWAPI::UnitTypes::Terran_Missile_Turret)){
                    g_v15LastDetectionOrder=now;break;
                }
            }
        }
    }
    // STARTCRAFT_AI_V20_SCAN: no Wraith-by-type scans, no unexplained-damage guesses.
    // Scan only an observed, genuinely invisible-to-weapons enemy close to our forces.
    if(now-g_v15LastScan<240)return;
    BWAPI::Position target=BWAPI::Positions::None;
    for(auto player:game->enemies())for(auto enemy:player->getUnits()){
        if(!enemy || !enemy->exists() || !enemy->isVisible() ||
           enemy->isDetected() ||
           !(enemy->isCloaked() || enemy->isBurrowed()))continue;
        bool nearby=false;
        for(auto ally:me->getUnits()){
            if(!ally || !ally->exists() || !ally->isCompleted())continue;
            if(ally->getDistance(enemy)<620){nearby=true;break;}
        }
        if(nearby && enemy->getPosition().isValid()){
            target=enemy->getPosition();break;
        }
    }
    if(!target.isValid())return;
    for(auto comsat:me->getUnits()){
        if(!comsat || !comsat->exists() || !comsat->isCompleted() ||
           comsat->getType()!=BWAPI::UnitTypes::Terran_Comsat_Station ||
           comsat->getEnergy()<50)continue;
        if(comsat->useTech(BWAPI::TechTypes::Scanner_Sweep,target)){
            g_v15LastScan=now;
            game->printf("v20 SCAN: undetected cloaked/burrowed enemy");
            break;
        }
    }
}


// STARTCRAFT_AI_V16: conservative natural fallback + expansion diagnostics.
// Keep all production/combat from v15. Does not disable detection scans.
int g_v16LastExpandNotice = -10000;
int g_v16LastExploreOrder = -10000;
int g_v16LastDetectionNotice = -10000;

// STARTCRAFT_AI_V22_NATURAL: replace AIV16NaturalSite() implementation.
// Require a gas+mineral base cluster, a reachable CC pad and significant
// space between our existing CCs. Within the nearest accessible base cluster,
// favor a pad close to MOST minerals, not merely close to the original CC.
// STARTCRAFT_AI_V24_NATURAL
// Select a reachable GAS+MINERAL expansion cluster, then rank valid CC pads
// for short harvesting distances. A CC is 4x3 BUILD tiles; never overlap
// resources, other bases or enemy positions.
// STARTCRAFT_AI_V27_NATURAL
// Select a gas+mineral BASE first by ground travel distance, THEN place the
// CC near its minerals/gas. A 4x3 CC cannot legally overlap mineral patches.
BWAPI::TilePosition AIV16NaturalSite(BWAPI::Unit builder) {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if(!me || !builder)return BWAPI::TilePositions::None;
    const auto cc=BWAPI::UnitTypes::Terran_Command_Center;
    const BWAPI::TilePosition homeTile=me->getStartLocation();
    const BWAPI::Position home(homeTile);
    const int w=game->mapWidth(),h=game->mapHeight();
    if(w<=0 || h<=0)return BWAPI::TilePositions::None;
    const std::vector<int> walk=AIV15GroundDistances(homeTile);
    BWAPI::TilePosition answer=BWAPI::TilePositions::None;
    int bestBaseTravel=1000000, bestHarvest=1000000;
    for(auto gas:game->getStaticGeysers()) {
        if(!gas || !gas->getInitialPosition().isValid())continue;
        BWAPI::Position geyser=gas->getInitialPosition();
        if(geyser.getDistance(home)<510 || geyser.getDistance(home)>2800)continue;
        std::vector<BWAPI::Position> patch;
        for(auto m:game->getStaticMinerals())
            if(m && m->getInitialPosition().isValid() &&
               m->getInitialPosition().getDistance(geyser)<435)
                patch.push_back(m->getInitialPosition());
        if(patch.size()<5)continue;
        // Ground distance to THIS resource cluster is fixed independently of
        // the candidate pad; keeps a distant expansion from looking like natural.
        BWAPI::TilePosition gt=gas->getInitialTilePosition();
        int baseTravel=1000000;
        for(int oy=-7;oy<=7;++oy)for(int ox=-7;ox<=7;++ox) {
            int xx=gt.x+ox,yy=gt.y+oy;
            if(xx<0 || yy<0 || xx>=w || yy>=h)continue;
            const int idx=yy*w+xx;
            if(idx<(int)walk.size() && walk[idx]>=0)
                baseTravel=(std::min)(baseTravel,walk[idx]);
        }
        if(baseTravel>=1000000)continue;
        BWAPI::TilePosition local=BWAPI::TilePositions::None;
        int localHarvest=1000000;
        for(int y=gt.y-16;y<=gt.y+16;++y)for(int x=gt.x-16;x<=gt.x+16;++x) {
            BWAPI::TilePosition pos(x,y);
            if(x<1 || y<1 || x+4>=w-1 || y+3>=h-1)continue;
            BWAPI::Position center(x*32+64,y*32+48);
            if(center.getDistance(home)<500 || center.getDistance(home)>2850)continue;
            int gasDist=center.getDistance(geyser);
            if(gasDist<140 || gasDist>360)continue;
            std::vector<int> mineralDistances;
            for(auto p:patch) if(center.getDistance(p)<370)
                mineralDistances.push_back(center.getDistance(p));
            if(mineralDistances.size()<5)continue;
            std::sort(mineralDistances.begin(),mineralDistances.end());
            if(mineralDistances[4]>335)continue;
            int mineralSum=0;
            for(int k=0;k<5;++k)mineralSum+=mineralDistances[k];
            if(mineralSum/5>265)continue;
            // Forbid already-claimed bases and confirmed enemy start sites.
            bool blocked=false;
            for(auto u:me->getUnits())
                if(u && u->exists() && u->getType()==cc && u->getDistance(center)<620)
                    blocked=true;
            for(auto start:game->getStartLocations())
                if(start!=homeTile && center.getDistance(BWAPI::Position(start))<520)
                    blocked=true;
            for(auto foe:game->enemies())for(auto u:foe->getUnits())
                if(u && u->exists() && u->isVisible() && u->getType().isBuilding() &&
                   u->getDistance(center)<530)blocked=true;
            if(blocked || !builder->hasPath(center) ||
               !game->canBuildHere(pos,cc,builder))continue;
            // Strongly prefer short harvest trips; game->canBuildHere enforces
            // legal depot footprint and prevents CC-on-mineral collisions.
            const int score=mineralSum*4 + gasDist*3;
            if(score<localHarvest){localHarvest=score;local=pos;}
        }
        if(!local.isValid())continue;
        if(baseTravel<bestBaseTravel-7 ||
           ((std::abs)(baseTravel-bestBaseTravel)<=7 && localHarvest<bestHarvest)) {
            bestBaseTravel=baseTravel;bestHarvest=localHarvest;answer=local;
        }
    }
    return answer; // No arbitrary centre-map CC fallback.
}









bool AIV16Expand(AIV11Counts c){
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    const bool tvz=AIV13OpponentRace()==BWAPI::Races::Zerg;
    const int neededScv=c.cc<=1?(tvz?15:20):(c.cc==2?32:44);
    const int neededFactories=c.cc<=1?(tvz?0:1):(c.cc==2?1:2);
    const int neededBarracks=c.cc<=1?(tvz?2:1):3;
    if(!me || c.cc<1 || c.cc>=4 || c.unfinishedCC || c.scv<neededScv ||
       c.factory<neededFactories || c.barracks<neededBarracks || me->minerals()<400 ||
       AIV11PendingBuild(BWAPI::UnitTypes::Terran_Command_Center,900))return false;
    int frame=game->getFrameCount();
    if(frame-g_v11LastExpandCheck<192)return false;
    g_v11LastExpandCheck=frame;
    BWAPI::Unit builder=AIV11BuildWorker();
    if(!builder)return false;
    BWAPI::TilePosition tile=AIV16NaturalSite(builder);
    if(!tile.isValid()){
        if(frame-g_v16LastExpandNotice>550){
            game->printf("v16 natural: no gas+mineral reachable CC site (retrying)");
            g_v16LastExpandNotice=frame;
        }
        // If no natural exists, don't keep holding funds indefinitely.
        g_v11NoExpansionUntil=frame+720;
        g_v11Reserve=0;
        return false;
    }
    if(builder->build(BWAPI::UnitTypes::Terran_Command_Center,tile)){
        AIV353TrackBuild(builder,BWAPI::UnitTypes::Terran_Command_Center,tile);
        g_v11LastBuildOrder=frame;
        game->printf("v16 NATURAL: CC build issued at %d,%d",tile.x,tile.y);
        return true;
    }
    // Unexplored or fogged natural can reject the build command. Send an SCV
    // to reveal the site, then retry. Never place a CC in the map centre as a
    // random fallback.
    AIV353Log("COMMAND_REJECTED",BWAPI::UnitTypes::Terran_Command_Center,builder,tile,game->getLastError().getName());
    if((!game->isExplored(tile) || !game->isVisible(tile)) &&
       frame-g_v16LastExploreOrder>160){
        BWAPI::Position dest(tile.x*32+64,tile.y*32+48);
        if(builder->move(dest)){
            AIV353Log("REVEAL_MOVE",BWAPI::UnitTypes::Terran_Command_Center,builder,tile);
            g_v16LastExploreOrder=frame;
            game->printf("v16 natural: revealing %d,%d",tile.x,tile.y);
        }
    }else if(frame-g_v16LastExpandNotice>550){
        game->printf("v16 natural: CC order rejected at %d,%d",tile.x,tile.y);
        g_v16LastExpandNotice=frame;
        g_v11NoExpansionUntil=frame+320;
        g_v11Reserve=0;
    }
    return false;
}

void AIV16DetectionDirector(){
    // v15 scans/comsat behaviour stays exactly as before.
    AIV15DetectionDirector();
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if(!me || me->getRace()!=BWAPI::Races::Terran)return;
    int f=game->getFrameCount();
    bool academy=false, eBay=false, eBayCompleted=false;
    int turrets=0;
    for(auto u:me->getUnits()){
        if(!u || !u->exists())continue;
        if(u->getType()==BWAPI::UnitTypes::Terran_Academy && u->isCompleted())academy=true;
        if(u->getType()==BWAPI::UnitTypes::Terran_Engineering_Bay){
            eBay=true;if(u->isCompleted())eBayCompleted=true;
        }
        if(u->getType()==BWAPI::UnitTypes::Terran_Missile_Turret)++turrets;
    }
    AIV11Counts c=AIV11Count();
    // This overlay is redrawn each frame, not just when checking construction.
    game->drawTextScreen(BWAPI::Position(10,156),
        "v16 NAT CC%d / SCV%d / M%d  DET: EB%d done%d turret%d",
        c.cc,c.scv,me->minerals(),eBay?1:0,eBayCompleted?1:0,turrets);
    if(f%24 || !academy || f-g_v11LastBuildOrder<96)return;
    if(!eBay && !AIV11PendingBuild(BWAPI::UnitTypes::Terran_Engineering_Bay)
       && me->minerals()>=150){
        if(AIV11BuildNear(BWAPI::UnitTypes::Terran_Engineering_Bay)){
            game->printf("v16 DETECTION: engineering bay ordered");
        }else if(f-g_v16LastDetectionNotice>600){
            game->printf("v16 DETECTION: cannot place engineering bay safely");
            g_v16LastDetectionNotice=f;
        }
        return;
    }
    if(!eBayCompleted || turrets>0 ||
       AIV11PendingBuild(BWAPI::UnitTypes::Terran_Missile_Turret) ||
       me->minerals()<100)return;
    if(AIV11BuildNear(BWAPI::UnitTypes::Terran_Missile_Turret)){
        game->printf("v16 DETECTION: turret ordered");
    }else if(f-g_v16LastDetectionNotice>600){
        game->printf("v16 DETECTION: turret placement failed; preserving lanes");
        g_v16LastDetectionNotice=f;
    }
}


// STARTCRAFT_AI_V17: cautious, reversible Terran gate/traffic manager.
// Existing defensive building layouts are kept. A Barracks is lifted only
// when multiple outbound troops have demonstrably stopped at that Barracks.
// This is a *traffic controller*, not a complete BWEM/BWEB wall planner.
struct AIV17Motion {
    BWAPI::Position last;
    int since;
    AIV17Motion():last(BWAPI::Positions::None),since(0) {}
};
std::map<int,AIV17Motion> g_v17Motion;
int g_v17GateId = -1;
BWAPI::TilePosition g_v17GatePad = BWAPI::TilePositions::None;
int g_v17GateOrder = -100000;
int g_v17LastNotice = -100000;
int g_v17PrevFrame = -1;

void AIV17ResetIfNewGame(int frame) {
    if (frame < g_v17PrevFrame) {
        g_v17Motion.clear();
        g_v17GateId = -1;
        g_v17GatePad = BWAPI::TilePositions::None;
        g_v17GateOrder = g_v17LastNotice = -100000;
    }
    g_v17PrevFrame = frame;
}

// The footprint must intersect the outgoing *local* travel segment before
// calling the Barracks a gate. Distance to Barracks alone is insufficient.
bool AIV17OnRoute(BWAPI::Position building,
                  BWAPI::Position from,BWAPI::Position to,int radius) {
    if (!building.isValid() || !from.isValid() || !to.isValid())return false;
    long long dx=to.x-from.x,dy=to.y-from.y;
    long long length=dx*dx+dy*dy;
    if (length<64LL*64LL)return false;
    long long px=building.x-from.x,py=building.y-from.y;
    long long dot=px*dx+py*dy;
    if (dot<0 || dot>length)return false;
    long long cross=px*dy-py*dx;
    return cross*cross<(long long)radius*radius*length;
}

// Choose a short sideways detour for an IDLE unit; never move harvesting SCVs,
// units fighting enemies, or a siege tank that is actively providing cover.
bool AIV17TryMoveAside(BWAPI::Unit u,BWAPI::Position goal) {
    if (!u || !u->exists() || u->isFlying() || !u->isCompleted()
        || g_v56SiegeLineIds.count(u->getID()) || AIV4EnemyCloseTo(u,300))return false;
    if (u->getType()==BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)
        return u->unsiege();
    if (!u->isIdle())return false;
    if (u->getType()==BWAPI::UnitTypes::Terran_SCV &&
        (u->isGatheringMinerals() || u->isGatheringGas() ||
         u->isConstructing() || u->isRepairing()))return false;
    BWAPI::Position step=AIV8SideStep(u,goal);
    return step.isValid() && u->move(step);
}

void AIV17GateDirector() {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if (!me || me->getRace()!=BWAPI::Races::Terran)return;
    int frame=game->getFrameCount();
    AIV17ResetIfNewGame(frame);
    if (frame%24)return;
    const BWAPI::Position home(me->getStartLocation());
    const BWAPI::Position enemy=AIV6AttackTarget();
    const BWAPI::Position homeThreat=AIV6HomeThreat();
    const bool pushing=(g_v7State==AIV7_ATTACK && enemy.isValid());

    // A gate that this controller opened belongs to this controller alone.
    // On regroup / defense, re-land at precisely its original footprint;
    // never choose a new wall tile or strand production buildings elsewhere.
    BWAPI::Unit gate=nullptr;
    for(auto u:me->getUnits())
        if(u && u->exists() && u->getID()==g_v17GateId) {gate=u;break;}
    if(g_v17GateId>=0 && !gate){g_v17GateId=-1;g_v17GatePad=BWAPI::TilePositions::None;}
    if(gate) {
        if(gate->isFlying() && (!pushing || homeThreat.isValid()) && frame-g_v17GateOrder>72) {
            if(g_v17GatePad.isValid() && gate->canLand(g_v17GatePad)
                && gate->land(g_v17GatePad)) {
                g_v17GateOrder=frame;
                game->printf("AI v17: defensive gate closing");
            } else if(frame-g_v17LastNotice>480) {
                game->printf("AI v17: gate pad occupied; holding in air");
                g_v17LastNotice=frame;
            }
        }
        if(!gate->isFlying() && (!pushing || homeThreat.isValid()) && frame-g_v17GateOrder>240) {
            g_v17GateId=-1;
            g_v17GatePad=BWAPI::TilePositions::None;
        }
    }

    // Leave the defense intact if the army isn't moving out. Do NOT use a
    // simple time-based timer as proof of a blockage.
    if(!pushing || homeThreat.isValid() || !enemy.isValid()) {
        game->drawTextScreen(BWAPI::Position(10,176),"v17 WALL: DEFEND");
        return;
    }
    if(gate && gate->isFlying()) {
        game->drawTextScreen(BWAPI::Position(10,176),"v17 WALL: OPEN");
        return;
    }
    if(frame-g_v17GateOrder<360)return;

    // Count army units with no meaningful displacement for >= 3 seconds at
    // the main-base exit (not at the enemy frontline). Ignore firing troops.
    std::vector<BWAPI::Unit> stuck;
    for(auto u:me->getUnits()) {
        if(!u || !u->exists() || !AIV8GroundCombat(u) ||
           !g_v7Army.count(u->getID()) ||
           u->getType()==BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode ||
           AIV4EnemyCloseTo(u,300))continue;
        int d=u->getDistance(home);
        if(d<210 || d>1300)continue;
        AIV17Motion &motion=g_v17Motion[u->getID()];
        if(!motion.last.isValid() ||
           motion.last.getDistance(u->getPosition())>48) {
            motion.last=u->getPosition();motion.since=frame;
        } else if(frame-motion.since>72)stuck.push_back(u);
    }
    if(stuck.size()<3)return;

    // Unit-based decongestion comes first; moving idle blockers is less
    // disruptive than lifting a building and interrupting production.
    for(auto u:stuck) {
        for(auto other:me->getUnits()) {
            if(!other || other==u || !other->exists() ||
               other->getDistance(u)>90)continue;
            if(AIV17TryMoveAside(other,enemy)) {
                if(frame-g_v17LastNotice>240){
                    game->printf("AI v17: clearing a jammed exit unit");
                    g_v17LastNotice=frame;
                }
                return;
            }
        }
    }

    // Lift only a COMPLETE, IDLE Barracks demonstrably intersecting the
    // route of at least 3 stuck outbound ground troops. This avoids lifting
    // an unrelated Barracks just because it happens to be near the ramp.
    for(auto barracks:me->getUnits()) {
        if(!barracks || !barracks->exists() || !barracks->isCompleted() ||
           barracks->getType()!=BWAPI::UnitTypes::Terran_Barracks ||
           barracks->isFlying() || barracks->isTraining() ||
           !barracks->canLift())continue;
        int dh=barracks->getDistance(home);
        if(dh<240 || dh>1200)continue;
        int obstructed=0;
        BWAPI::Position b=barracks->getPosition();
        for(auto u:stuck) {
            if(u->getDistance(b)>430)continue;
            if(AIV17OnRoute(b,u->getPosition(),enemy,145))++obstructed;
        }
        if(obstructed<3)continue;
        auto pad=barracks->getTilePosition();
        if(barracks->lift()) {
            g_v17GateId=barracks->getID();
            g_v17GatePad=pad;
            g_v17GateOrder=frame;
            game->printf("AI v17: outbound troops jammed, opening Barracks gate");
            return;
        }
    }
}


// STARTCRAFT_AI_V20: conservative mineral distribution and medic choke safety.
// This block goes into the established economy anonymous namespace.
// Do NOT modify workers constructing, repairing, scouting, carrying cargo, or gathering gas.
static std::map<int,int> g_v20MineralMoveFrame;
static int g_v20PrevFrame = -1;
static int g_v20LastMineralOrder = -100000;
static std::map<int,BWAPI::Position> g_v20MedicLastPosition;
static std::map<int,int> g_v20MedicStillSince;
static std::map<int,int> g_v20MedicLastDetour;

static bool AIV20MineralPatch(BWAPI::Unit u) {
    if (!u || !u->exists()) return false;
    const auto t = u->getType();
    return t.isMineralField();
}

static void AIV20Reset(int frame) {
    if (frame < g_v20PrevFrame) {
        g_v20MineralMoveFrame.clear();
        g_v20LastMineralOrder=-100000;
        g_v20MedicLastPosition.clear();
        g_v20MedicStillSince.clear();
        g_v20MedicLastDetour.clear();
    }
    g_v20PrevFrame=frame;
}

// STARTCRAFT_AI_V21_MINING
// Replace the old v20 mining function (not run alongside it).
// Initial SCVs: one worker per patch when spare mineral patches exist.
// New SCVs: spread by patch load. Completed expansions: receive transfers
// from oversaturated bases, without interrupting gas/build/scout/repair tasks.
struct AIV21MiningBase {
    BWAPI::Unit cc;
    std::vector<BWAPI::Unit> minerals;
    int workers;
    AIV21MiningBase() : cc(nullptr), workers(0) {}
};
static std::map<int,int> g_v21MineIntent;
static std::map<int,int> g_v21MineCommandFrame;
static int g_v21MinePrevFrame=-1;
static int g_v21MineLastIssue=-99999;

static bool AIV21IsMine(BWAPI::Unit mineral) {
    return mineral && mineral->exists() && mineral->getType().isMineralField();
}
static bool AIV21AvailableMiner(BWAPI::Unit w) {
    return w && w->exists() && w->isCompleted() &&
        w->getType()==BWAPI::UnitTypes::Terran_SCV &&
        !AIV353ReservedBuilder(w) && !w->isConstructing() && !w->isRepairing() &&
        !w->isGatheringGas() && !w->isCarryingGas() &&
        w->getID()!=g_v5ScoutId &&
        (w->isIdle() || w->isGatheringMinerals() || w->isCarryingMinerals());
}

static void AIV21MiningDirector() {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if (!me || me->getRace()!=BWAPI::Races::Terran) return;
    const int now=game->getFrameCount();
    if (now<g_v21MinePrevFrame) {
        g_v21MineIntent.clear();
        g_v21MineCommandFrame.clear();
        g_v21MineLastIssue=-99999;
    }
    g_v21MinePrevFrame=now;
    if (now>96 && now%24!=0) return;

    std::vector<AIV21MiningBase> bases;
    for (auto cc:me->getUnits()) {
        if (cc && cc->exists() && cc->isCompleted() && !cc->isFlying() &&
            cc->getType()==BWAPI::UnitTypes::Terran_Command_Center) {
            AIV21MiningBase b; b.cc=cc; bases.push_back(b);
        }
    }
    if (bases.empty()) return;

    std::map<int,BWAPI::Unit> mineById;
    std::map<int,int> mineBase;
    std::map<int,int> mineWorkers;
    for (auto m:game->getMinerals()) {
        if (!AIV21IsMine(m) || m->getResources()<=0)continue;
        int closest=-1, distance=1000000;
        for (int i=0;i<(int)bases.size();++i) {
            const int d=bases[i].cc->getDistance(m);
            if (d<distance) { distance=d; closest=i; }
        }
        // Avoid enemy bases and far-away neutral minerals.
        if (closest<0 || distance>475) continue;
        bases[closest].minerals.push_back(m);
        mineById[m->getID()]=m;
        mineBase[m->getID()]=closest;
        mineWorkers[m->getID()]=0;
    }
    if (mineById.empty()) return;

    // Preserve a planned assignment while the worker returns cargo or walks to a patch.
    // Track actual mining targets as well, so previous-version assignments are counted.
    std::map<int,int> selected;
    for (auto w:me->getUnits()) {
        if (!AIV21AvailableMiner(w)) { if(w)g_v21MineIntent.erase(w->getID()); continue; }
        int id=-1;
        // STARTCRAFT_AI_V33_BASE_LOCK: scheduled patch must dominate the
        // temporary order target while returning minerals to a Command Center.
        const auto scheduled=g_v21MineIntent.find(w->getID());
        if(scheduled!=g_v21MineIntent.end() && mineById.count(scheduled->second))
            id=scheduled->second;
        else {
            BWAPI::Unit actual=w->getOrderTarget();
            if(!AIV21IsMine(actual))actual=w->getTarget();
            if(AIV21IsMine(actual) && mineById.count(actual->getID()))id=actual->getID();
        }
        if (id>=0) {
            selected[w->getID()]=id;
            ++mineWorkers[id];
            ++bases[mineBase[id]].workers;
        }
    }

    const bool opening=(now<=96);
    int maxOrders=opening?6:1;
    if (!opening && now-g_v21MineLastIssue<24) return;

    for (auto w:me->getUnits()) {
        if (maxOrders<=0) break;
        if (!AIV21AvailableMiner(w) || w->isCarryingMinerals()) continue;
        if (!opening && now-w->getLastCommandFrame()<48) continue;
        if (g_v21MineCommandFrame.count(w->getID()) &&
            now-g_v21MineCommandFrame[w->getID()]<(opening?12:144)) continue;
        int source=-1;
        int oldId=-1;
        if (selected.count(w->getID())) {
            oldId=selected[w->getID()];
            source=mineBase[oldId];
        }
        if (source<0) {
            int minDist=1000000;
            for (int i=0;i<(int)bases.size();++i) {
                int d=w->getDistance(bases[i].cc);
                if (d<minDist) { minDist=d; source=i; }
            }
        }
        // STARTCRAFT_AI_V32_DEPLETED_BASE: migrate mineral workers from a
        // mined-out main, even if their old patch assignment still exists.
        if(source<0)continue;
        int destination=source;
        if(bases[source].minerals.empty()) {
            int best=-1, bestDistance=10000000;
            for(int i=0;i<(int)bases.size();++i) {
                if(bases[i].minerals.empty())continue;
                const int dist=w->getDistance(bases[i].cc);
                if(dist<bestDistance){bestDistance=dist;best=i;}
            }
            if(best<0)continue;
            // Never push every SCV into an already saturated seven-patch natural.
            if(bases[best].workers >= (int)bases[best].minerals.size()*2+2)continue;
            destination=best;
        }

        // Move workers to a completed natural only after the home mineral line
        // is well staffed. Planned transfers already count toward its staffing.
        if (!opening && bases.size()>1 && !bases[source].minerals.empty() &&
            (oldId<0 || bases[source].workers>(std::max)(10,(int)bases[source].minerals.size()*3/2))) {
            int best=-1, bestScore=1000000;
            for (int i=0;i<(int)bases.size();++i) {
                if (i==source || bases[i].minerals.empty()) continue;
                if (bases[i].cc->getDistance(bases[source].cc)>2400) continue;
                const int destCap=(int)bases[i].minerals.size()*2;
                if (bases[i].workers>=destCap) continue;
                const int sourceFloor=(std::max)(10,(int)bases[source].minerals.size()*3/2);
                if (bases[source].workers<=sourceFloor)continue;
                // Require a meaningful saturation difference even after a transfer.
                // Prevent a newly staffed natural from sending miners back home.
                const int srcRatio=(bases[source].workers-1)*1000/(int)bases[source].minerals.size();
                const int dstRatio=(bases[i].workers+1)*1000/(int)bases[i].minerals.size();
                if(srcRatio-dstRatio<450)continue;
                // Prefer the least-saturated completed expansion.
                const int score=bases[i].workers*1000/(int)bases[i].minerals.size()
                               +w->getDistance(bases[i].cc)/8;
                if (score<bestScore) {best=i;bestScore=score;}
            }
            if (best>=0) destination=best;
        }

        BWAPI::Unit bestMine=nullptr;
        int bestScore=100000000;
        for (auto m:bases[destination].minerals) {
            const int workers=mineWorkers[m->getID()];
            const int score=workers*10000 + w->getDistance(m) +
                            bases[destination].cc->getDistance(m)/3;
            if (score<bestScore) {bestScore=score;bestMine=m;}
        }
        if (!bestMine) continue;
        const int newId=bestMine->getID();
        if (oldId==newId && !w->isIdle()) continue; // STARTCRAFT_AI_V33_KEEP_MINING
        const int oldCount=oldId>=0?mineWorkers[oldId]:0;
        const int newCount=mineWorkers[newId];
        const bool crossBase=destination!=source;
        // Start: eliminate duplicate assignments until each patch has 1 SCV.
        // Later: do not reshuffle balanced 1-2 worker patches unnecessarily.
        if (!crossBase && oldId>=0 && !w->isIdle() &&
            !(opening ? (oldCount>1 && newCount==0) : (oldCount>newCount+1))) continue;
        if (w->gather(bestMine)) {
            if (oldId>=0) {
                --mineWorkers[oldId];
                --bases[mineBase[oldId]].workers;
            }
            ++mineWorkers[newId];
            ++bases[destination].workers;
            selected[w->getID()]=newId;
            g_v21MineIntent[w->getID()]=newId;
            g_v21MineCommandFrame[w->getID()]=now;
            g_v21MineLastIssue=now;
            --maxOrders;
            if (crossBase)
                game->printf("v21: SCV transferred to completed expansion");
        }
    }
}


void AIV20BalanceMineralPatches() {
    AIV21MiningDirector(); // STARTCRAFT_AI_V21_MINING
}


// STARTCRAFT_AI_V31_CORRECT_CARGO
// When an SCV is assigned to an expansion patch, do not let an automated
// ReturnMinerals order take its cargo to a DIFFERENT Command Center.
// This is deliberately reactive; normal cargo trips receive no extra orders.
static std::map<int,int> g_v31LastDepotCorrection;
static int g_v31LastFrame=-1;
static int g_v31DepotCorrections=0;
static int g_v31BadDepotSeen=0;

static BWAPI::Unit AIV31PatchForWorker(BWAPI::Unit worker) {
    if (!worker || !worker->exists()) return nullptr;
    auto it=g_v21MineIntent.find(worker->getID());
    if (it!=g_v21MineIntent.end()) {
        for(auto m:BWAPI::Broodwar->getMinerals())
            if(AIV21IsMine(m) && m->getID()==it->second) return m;
    }
    BWAPI::Unit actual=worker->getOrderTarget();
    if(!AIV21IsMine(actual))actual=worker->getTarget();
    return AIV21IsMine(actual)?actual:nullptr;
}

static BWAPI::Unit AIV31DepotForPatch(BWAPI::Unit patch) {
    if(!AIV21IsMine(patch))return nullptr;
    BWAPI::Unit best=nullptr;
    int bestDistance=1000000;
    for(auto cc:BWAPI::Broodwar->self()->getUnits()) {
        if(!cc || !cc->exists() || !cc->isCompleted() || cc->isFlying() ||
           cc->getType()!=BWAPI::UnitTypes::Terran_Command_Center)continue;
        const int d=cc->getDistance(patch);
        if(d<bestDistance){best=cc;bestDistance=d;}
    }
    return bestDistance<=475?best:nullptr;
}

// STARTCRAFT_AI_V33_STABLE_CARGO
// BWAPI auto-returns cargo to a suitable depot. Forcing rightClick on a CC
// during return cancels the mining order; gather() then re-targets a patch,
// resulting in oscillation. Observe depot mismatch but NEVER override cargo.
static void AIV31CorrectCargoDepot() {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if(!me || me->getRace()!=BWAPI::Races::Terran)return;
    const int frame=game->getFrameCount();
    if(frame<g_v31LastFrame){g_v31LastDepotCorrection.clear();
        g_v31DepotCorrections=0;g_v31BadDepotSeen=0;}
    g_v31LastFrame=frame;
    if(frame%24)return;
    for(auto w:me->getUnits()) {
        if(!w || !w->exists() || w->getType()!=BWAPI::UnitTypes::Terran_SCV ||
           !w->isCarryingMinerals())continue;
        BWAPI::Unit patch=AIV31PatchForWorker(w);
        BWAPI::Unit own=AIV31DepotForPatch(patch);
        BWAPI::Unit actual=w->getOrderTarget();
        if(!actual || actual->getType()!=BWAPI::UnitTypes::Terran_Command_Center)
            actual=w->getTarget();
        if(own && actual && actual->exists() && actual!=own &&
           actual->getType()==BWAPI::UnitTypes::Terran_Command_Center &&
           own->getDistance(patch)+64<actual->getDistance(patch))
            ++g_v31BadDepotSeen;
    }
}

// Per-base evidence for the next post-game analysis.
static void AIV31WorkerFlowLog() {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if(!me || me->getRace()!=BWAPI::Races::Terran)return;
    const int frame=game->getFrameCount();
    static int lastLogFrame=-1;
    if(frame<lastLogFrame)lastLogFrame=-1;
    if(frame!=0 && frame%240!=0)return;
    if(frame==lastLogFrame)return;
    lastLogFrame=frame;
    const char* filename="C:\\Users\\User\\starcraft-api\\STARTcraft\\AI_reports\\AI_v31_worker_flow.csv";
    _mkdir("C:\\Users\\User\\starcraft-api\\STARTcraft\\AI_reports");
    std::ofstream out(filename,frame==0 ? std::ios::trunc : std::ios::app);
    if(!out)return;
    if(frame==0)out << "frame,seconds,cc_id,cc_x,cc_y,mineral_patches,assigned_mineral_workers,gas_workers,scv_total,scv_target,wrong_depot_seen,redirected_depots\n";
    int scvs=0;
    for(auto u:me->getUnits())if(u && u->exists() &&
        u->getType()==BWAPI::UnitTypes::Terran_SCV)++scvs;
    for(auto cc:me->getUnits()) {
        if(!cc || !cc->exists() || !cc->isCompleted() || cc->isFlying() ||
           cc->getType()!=BWAPI::UnitTypes::Terran_Command_Center)continue;
        int patches=0,assigned=0,gas=0;
        for(auto m:game->getMinerals())if(AIV21IsMine(m) && m->getResources()>0 &&
            AIV31DepotForPatch(m)==cc)++patches;
        for(auto w:me->getUnits()) {
            if(!w || !w->exists() || w->getType()!=BWAPI::UnitTypes::Terran_SCV)continue;
            if(w->isGatheringGas()) {
                BWAPI::Unit target=w->getOrderTarget();
                if(target && target->getType()==BWAPI::UnitTypes::Terran_Refinery &&
                   target->getDistance(cc)<=450)++gas;
            } else if(AIV31DepotForPatch(AIV31PatchForWorker(w))==cc)++assigned;
        }
        out << frame << ',' << game->elapsedTime() << ',' << cc->getID() << ','
            << cc->getPosition().x << ',' << cc->getPosition().y << ','
            << patches << ',' << assigned << ',' << gas << ','
            << scvs << ',' << AIV11WorkerTarget() << ','
            << g_v31BadDepotSeen << ',' << g_v31DepotCorrections << '\n';
    }
}

// STARTCRAFT_AI_V32_EXPANSION_GARRISON
// Keep two tanks at the actual completed natural, separate from the attack army.
static BWAPI::Position AIV32GuardPosition(BWAPI::Unit cc,BWAPI::Unit tank) {
    auto& game=BWAPI::Broodwar;
    if(!cc || !tank)return BWAPI::Positions::None;
    const BWAPI::Position base=cc->getPosition();
    BWAPI::Position foe=AIV6AttackTarget();
    if(!foe.isValid())foe=BWAPI::Position(game->mapWidth()*16,game->mapHeight()*16);
    const int dx=foe.x-base.x,dy=foe.y-base.y;
    const int norm=(std::max)(1,(std::max)(std::abs(dx),std::abs(dy)));
    const int px=-dy,py=dx;
    BWAPI::Position best=BWAPI::Positions::None;
    int bestScore=10000000;
    for(int r=220;r<=350;r+=65) for(int side=-1;side<=1;++side) {
        const BWAPI::Position dest(base.x + dx*r/norm + px*side*90/norm,
                                   base.y + dy*r/norm + py*side*90/norm);
        if(dest.x<24 || dest.y<24 || dest.x>=game->mapWidth()*32-24 ||
           dest.y>=game->mapHeight()*32-24 ||
           !game->isWalkable(dest.x/8,dest.y/8) ||
           !tank->hasPath(dest))continue; // STARTCRAFT_AI_V33_GUARD_PATH_FIX
        bool blocked=false;
        for(auto u:game->getUnitsInRadius(dest,90)) {
            if(!u || !u->exists())continue;
            if(u->getType().isMineralField() || u->getType().isBuilding()) {
                blocked=true;break;
            }
        }
        if(blocked)continue;
        const int widthX=AIV21WalkSpan(dest,1,0)+AIV21WalkSpan(dest,-1,0);
        const int widthY=AIV21WalkSpan(dest,0,1)+AIV21WalkSpan(dest,0,-1);
        if(widthX<176 || widthY<176)continue;
        const int score=tank->getDistance(dest)+std::abs(side)*50;
        if(score<bestScore){bestScore=score;best=dest;}
    }
    return best;
}
static int g_v32PrevFrame=-1;
static int g_v32LastGuardOrder=-10000;
static int g_v32LastIdleOrder=-10000;
static int g_v32WorkerRescues=0;
static int g_v32LastMetrics=-10000;
static std::set<int> g_v55NaturalGuardIds;
static std::map<int,int> g_v55LastMineOrder;
static bool g_v55NaturalPrepared=false;
static int g_v55PreviousFrame=-1;
static void AIV32DefenseAndWorkers() {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if(!me || me->getRace()!=BWAPI::Races::Terran)return;
    const int now=game->getFrameCount();
    if(now<g_v32PrevFrame){g_v32GuardIds.clear();g_v55NaturalGuardIds.clear();
        g_v55LastMineOrder.clear();g_v55NaturalPrepared=false;g_v32WorkerRescues=0;
        g_v32LastGuardOrder=-10000;g_v32LastIdleOrder=-10000;g_v32LastMetrics=-10000;}
    g_v32PrevFrame=now;
    if(now%24)return;
    BWAPI::Unit main=nullptr,natural=nullptr;
    const BWAPI::Position home(me->getStartLocation());
    for(auto cc:me->getUnits()) {
        if(!cc || !cc->exists() || !cc->isCompleted() || cc->isFlying() ||
           cc->getType()!=BWAPI::UnitTypes::Terran_Command_Center)continue;
        if(!main || cc->getDistance(home)<main->getDistance(home))main=cc;
    }
    int selectedThreat=-1;
    if(main) for(auto cc:me->getUnits()) {
        if(!cc || !cc->exists() || cc->isFlying() || cc==main ||
           cc->getType()!=BWAPI::UnitTypes::Terran_Command_Center)continue;
        int threatScore=0;
        for(auto ep:game->enemies())for(auto e:ep->getUnits())
            if(e && e->exists() && e->isVisible() && !e->getType().isBuilding() &&
               e->getDistance(cc)<900)threatScore+=e->getType().isWorker()?1:3;
        if(!natural || threatScore>selectedThreat ||
           (threatScore==selectedThreat && cc->getDistance(main)<natural->getDistance(main))){
            natural=cc;selectedThreat=threatScore;
        }
    }
    int totalTanks=0;
    for(auto u:me->getUnits())if(u && u->exists() && u->isCompleted() && AIV7IsTank(u))++totalTanks;
    if(!natural)g_v32GuardIds=g_v55NaturalGuardIds;
    else {
        std::set<int> updated;
        int marines=0;
        for(auto u:me->getUnits())if(u && u->exists() && u->isCompleted() &&
            u->getType()==BWAPI::UnitTypes::Terran_Marine)++marines;
        // STARTCRAFT_AI_V34_EARLY_NATURAL_GUARD: station the first units at a
        // completed natural, before waiting for a 4-tank / 14-Marine threshold.
        const int tankQuota=totalTanks>=2?2:(totalTanks>=1?1:0);
        const int marineQuota=marines>=6?6:marines;
        auto accept=[&](BWAPI::Unit u)->bool {
            return u && u->exists() && u->isCompleted() &&
                !u->isLoaded() &&
                !(g_v7State==AIV7_ATTACK && g_v7Army.count(u->getID())) &&
                (AIV7IsTank(u) || u->getType()==BWAPI::UnitTypes::Terran_Marine ||
                 (u->getType()==BWAPI::UnitTypes::Terran_Vulture &&
                  g_v55NaturalGuardIds.count(u->getID())));
        };
        // Preserve assignments to prevent new rally orders pulling the garrison home.
        int tanksSelected=0,marinesSelected=0;
        for(auto u:me->getUnits())if(accept(u) && g_v32GuardIds.count(u->getID())){
            if(u->getType()==BWAPI::UnitTypes::Terran_Vulture &&
               g_v55NaturalGuardIds.count(u->getID()))updated.insert(u->getID());
            else if(AIV7IsTank(u) && tanksSelected<tankQuota){updated.insert(u->getID());++tanksSelected;}
            else if(u->getType()==BWAPI::UnitTypes::Terran_Marine && marinesSelected<marineQuota){
                updated.insert(u->getID());++marinesSelected;
            }
        }
        while(tanksSelected<tankQuota || marinesSelected<marineQuota){
            BWAPI::Unit best=nullptr;int distance=10000000;
            for(auto u:me->getUnits())if(accept(u) && !updated.count(u->getID()) &&
                ((AIV7IsTank(u) && tanksSelected<tankQuota) ||
                 (u->getType()==BWAPI::UnitTypes::Terran_Marine && marinesSelected<marineQuota)) &&
                u->getDistance(natural)<distance){best=u;distance=u->getDistance(natural);}
            if(!best)break;
            updated.insert(best->getID());
            if(AIV7IsTank(best))++tanksSelected;else ++marinesSelected;
        }
        g_v32GuardIds.swap(updated);
        if(now-g_v32LastGuardOrder>=48) {
            BWAPI::Position threat=BWAPI::Positions::None;
            int bestEnemy=10000000;
            for(auto e:game->getUnitsInRadius(natural->getPosition(),710))if(e && e->exists() &&
                e->isVisible() && e->getPlayer() && e->getPlayer()->isEnemy(me) &&
                !e->getType().isBuilding() && !e->isFlying() &&
                e->getDistance(natural)<bestEnemy){bestEnemy=e->getDistance(natural);threat=e->getPosition();}
            int actions=0;
            for(auto u:me->getUnits()) {
                if(!u || !u->exists() || !g_v32GuardIds.count(u->getID()))continue;
                // v55 owns mine-laying Vultures; retain their army exclusion
                // without replacing their mine-screen orders here.
                if(u->getType()==BWAPI::UnitTypes::Terran_Vulture)continue;
                BWAPI::Position post=AIV32GuardPosition(natural,u);
                if(!post.isValid()) {
                    const BWAPI::Position base=natural->getPosition();
                    const BWAPI::Position rear=main?main->getPosition():BWAPI::Position(home);
                    const int dx=rear.x-base.x,dy=rear.y-base.y;
                    const int norm=(std::max)(1,(std::max)(std::abs(dx),std::abs(dy)));
                    const int distances[4]={220,170,120,70};
                    for(int distance:distances) {
                        BWAPI::Position candidate(base.x+dx*distance/norm,
                                                  base.y+dy*distance/norm);
                        if(candidate.x>=24 && candidate.y>=24 &&
                           candidate.x<game->mapWidth()*32-24 &&
                           candidate.y<game->mapHeight()*32-24 &&
                           game->isWalkable(candidate.x/8,candidate.y/8) &&
                           u->hasPath(candidate)){post=candidate;break;}
                    }
                }
                if(!post.isValid())continue;
                if(u->getDistance(post)<=115) {
                    if(u->getType()==BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode &&
                       !AIV21TankExitGuard(u) &&
                       me->hasResearched(BWAPI::TechTypes::Tank_Siege_Mode) &&
                       u->siege())++actions;
                    else if(u->getType()==BWAPI::UnitTypes::Terran_Marine &&
                            threat.isValid() && !u->isAttackFrame() &&
                            now-u->getLastCommandFrame()>96 && u->holdPosition())++actions;
                } else if(u->getType()==BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode) {
                    if(u->unsiege())++actions;
                } else if(!u->isAttacking() &&
                          (u->isIdle() || now-u->getLastCommandFrame()>180) &&
                          u->move(post))++actions;
            }
            if(actions)g_v32LastGuardOrder=now;
        }
    }
    // Recover genuine idle SCVs, including miners from depleted original base.
    if(now-g_v32LastIdleOrder>=24) {
        int orders=0;
        for(auto w:me->getUnits()) {
            if(orders>=3)break;
            if(!w || !w->exists() || !w->isCompleted() ||
               w->getType()!=BWAPI::UnitTypes::Terran_SCV || !w->isIdle() ||
               w->isCarryingMinerals() || w->isCarryingGas() || w->isUnderAttack() ||
               w->getID()==g_v5ScoutId || AIV353ReservedBuilder(w) || w->isConstructing() || w->isRepairing())continue;
            // Do not interrupt workers recently called up by reactive base defence.
            bool nearbyEnemy=false;
            for(auto e:game->getUnitsInRadius(w->getPosition(),260))
                if(e && e->exists() && e->getPlayer() && e->getPlayer()->isEnemy(me) &&
                   !e->getType().isBuilding()){nearbyEnemy=true;break;}
            if(nearbyEnemy)continue;
            BWAPI::Unit cc=nullptr,patch=nullptr;
            BWAPI::Unit preferred=AIV31DepotForPatch(AIV31PatchForWorker(w));
            if(!preferred || !preferred->exists() || !preferred->isCompleted()) {
                int d=1000000;
                for(auto base:me->getUnits())if(base && base->exists() && base->isCompleted() &&
                    !base->isFlying() && base->getType()==BWAPI::UnitTypes::Terran_Command_Center &&
                    w->getDistance(base)<d){d=w->getDistance(base);preferred=base;}
            }
            bool prefHasMinerals=false;
            for(auto m:game->getMinerals())if(AIV21IsMine(m) && m->getResources()>0 &&
                AIV31DepotForPatch(m)==preferred)prefHasMinerals=true;
            int bestScore=10000000;
            for(auto m:game->getMinerals()) {
                if(!AIV21IsMine(m) || m->getResources()<=0)continue;
                BWAPI::Unit depot=AIV31DepotForPatch(m);
                if(!depot || !w->hasPath(m->getPosition()))continue;
                // A worker at a healthy natural must NOT switch to the main.
                if(preferred && prefHasMinerals && depot!=preferred)continue;
                int assigned=0;
                for(auto coworker:me->getUnits()) {
                    if(!coworker || !coworker->exists() || coworker==w ||
                       coworker->getType()!=BWAPI::UnitTypes::Terran_SCV)continue;
                    if(AIV31PatchForWorker(coworker)==m)++assigned;
                }
                // If minerals are saturated, idle SCVs have no productive patch;
                // keep their reason visible instead of endlessly reissuing orders.
                if(assigned>=3)continue;
                const int score=assigned*10000+w->getDistance(m)+depot->getDistance(m)/2;
                if(score<bestScore){bestScore=score;patch=m;cc=depot;}
            }
            if(patch && cc && w->gather(patch)) {
                g_v21MineIntent[w->getID()]=patch->getID();
                g_v21MineCommandFrame[w->getID()]=now;
                ++g_v32WorkerRescues;++orders;
            }
        }
        if(orders)g_v32LastIdleOrder=now;
    }
    if(now==0 || now-g_v32LastMetrics>=240) {
        g_v32LastMetrics=now;
        int idle=0,miners=0,guards=0;
        for(auto u:me->getUnits()) {
            if(!u || !u->exists())continue;
            if(u->getType()==BWAPI::UnitTypes::Terran_SCV){if(u->isIdle())++idle;if(u->isGatheringMinerals())++miners;}
            if(g_v32GuardIds.count(u->getID()))++guards;
        }
        _mkdir("C:\\Users\\User\\starcraft-api\\STARTcraft\\AI_reports");
        std::ofstream out("C:\\Users\\User\\starcraft-api\\STARTcraft\\AI_reports\\AI_v32_commands.csv",
                          now==0?std::ios::trunc:std::ios::app);
        if(out) {
            if(now==0)out << "frame,seconds,army_state,enemy_race,scv_idle,scv_mining,scv_rescued,expansion_guard_tanks,army_attackers,tanks_total\n";
            out << now << ',' << game->elapsedTime() << ',' << (int)g_v7State << ','
                << AIV13OpponentRace().getName() << ',' << idle << ',' << miners << ','
                << g_v32WorkerRescues << ',' << guards << ',' << g_v7Army.size() << ',' << totalTanks << "\n";
        }
    }
}

// STARTCRAFT_AI_V55_PREPARED_NATURAL
// Against Protoss, occupy the expansion approach before the SCV starts a CC.
// Two Tanks must reach their posts and enter Siege Mode. Vultures then seed a
// mine screen farther down the same approach instead of idling in the main.
static BWAPI::Position AIV55NaturalPost(BWAPI::Position natural,
                                        BWAPI::Position foe,
                                        BWAPI::Unit unit,int side,int distance) {
    auto& game=BWAPI::Broodwar;
    if(!unit || !natural.isValid())return BWAPI::Positions::None;
    if(!foe.isValid())foe=BWAPI::Position(game->mapWidth()*16,game->mapHeight()*16);
    const int dx=foe.x-natural.x,dy=foe.y-natural.y;
    const int norm=(std::max)(1,(std::max)(std::abs(dx),std::abs(dy)));
    const int px=-dy,py=dx;
    BWAPI::Position best=BWAPI::Positions::None;
    int bestScore=10000000;
    const int offsets[7]={0,48,-48,88,-88,128,-128};
    for(int offset:offsets) {
        const int lateral=side*82+offset;
        BWAPI::Position candidate(natural.x+dx*distance/norm+px*lateral/norm,
                                  natural.y+dy*distance/norm+py*lateral/norm);
        if(candidate.x<24 || candidate.y<24 ||
           candidate.x>=game->mapWidth()*32-24 ||
           candidate.y>=game->mapHeight()*32-24 ||
           !game->isWalkable(candidate.x/8,candidate.y/8) ||
           !unit->hasPath(candidate))continue;
        bool blocked=false;
        for(auto near:game->getUnitsInRadius(candidate,72))
            if(near && near->exists() &&
               (near->getType().isBuilding() || near->getType().isMineralField())){
                blocked=true;break;
            }
        if(blocked)continue;
        const int score=unit->getDistance(candidate)+std::abs(offset)*2;
        if(score<bestScore){bestScore=score;best=candidate;}
    }
    return best;
}

static void AIV55PrepareNatural() {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();if(!me)return;
    const int now=game->getFrameCount();
    if(now<g_v55PreviousFrame){
        g_v55NaturalGuardIds.clear();g_v55LastMineOrder.clear();
        g_v55NaturalPrepared=false;
    }
    g_v55PreviousFrame=now;
    if(now%24)return;
    for(int id:g_v55NaturalGuardIds)g_v32GuardIds.erase(id);
    g_v55NaturalGuardIds.clear();g_v55NaturalPrepared=false;
    if(AIV13OpponentRace()!=BWAPI::Races::Protoss)return;

    BWAPI::Unit main=nullptr,naturalCC=nullptr;
    const BWAPI::Position home(me->getStartLocation());
    for(auto cc:me->getUnits())if(cc && cc->exists() && !cc->isFlying() &&
        cc->getType()==BWAPI::UnitTypes::Terran_Command_Center) {
        if(!main || cc->getDistance(home)<main->getDistance(home))main=cc;
    }
    if(main)for(auto cc:me->getUnits())if(cc && cc->exists() && !cc->isFlying() &&
        cc!=main && cc->getType()==BWAPI::UnitTypes::Terran_Command_Center &&
        (!naturalCC || cc->getDistance(main)<naturalCC->getDistance(main)))naturalCC=cc;
    BWAPI::TilePosition tile=BWAPI::TilePositions::None;
    if(naturalCC)tile=naturalCC->getTilePosition();
    else {
        BWAPI::Unit builder=AIV11BuildWorker();
        tile=AIV16NaturalSite(builder);
    }
    if(!tile.isValid())return;
    const auto ccType=BWAPI::UnitTypes::Terran_Command_Center;
    const BWAPI::Position natural(tile.x*32+ccType.tileWidth()*16,
                                  tile.y*32+ccType.tileHeight()*16);
    BWAPI::Position foe=AIV6AttackTarget();
    std::vector<BWAPI::Unit> tanks;
    for(auto u:me->getUnits())if(u && u->exists() && u->isCompleted() && AIV7IsTank(u))
        tanks.push_back(u);
    std::sort(tanks.begin(),tanks.end(),[&](BWAPI::Unit a,BWAPI::Unit b){
        return a->getDistance(natural)<b->getDistance(natural);
    });
    int siegedReady=0;
    for(int i=0;i<(std::min)(2,(int)tanks.size());++i) {
        auto tank=tanks[i];
        const BWAPI::Position post=AIV55NaturalPost(natural,foe,tank,i?1:-1,215);
        if(!post.isValid())continue;
        g_v55NaturalGuardIds.insert(tank->getID());
        g_v32GuardIds.insert(tank->getID());
        if(tank->getDistance(post)<=145 &&
           tank->getType()==BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode) {
            ++siegedReady;continue;
        }
        if(tank->getType()==BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode) {
            if(tank->getDistance(post)>175)tank->unsiege();
        } else if(tank->getDistance(post)<=115 &&
                  me->hasResearched(BWAPI::TechTypes::Tank_Siege_Mode)) {
            tank->siege();
        } else if(!tank->isAttackFrame() &&
                  (tank->isIdle() || now-tank->getLastCommandFrame()>72)) {
            tank->move(post);
        }
    }

    // Siege Mode comes first; Spider Mines follow as the second Machine Shop
    // research so the expansion approach receives a disposable warning screen.
    const auto mines=BWAPI::TechTypes::Spider_Mines;
    if(me->hasResearched(BWAPI::TechTypes::Tank_Siege_Mode) &&
       !me->hasResearched(mines) && !me->isResearching(mines)) {
        for(auto shop:me->getUnits())if(shop && shop->exists() && shop->isCompleted() &&
            shop->getType()==BWAPI::UnitTypes::Terran_Machine_Shop &&
            !shop->isResearching() && me->minerals()>=mines.mineralPrice() &&
            me->gas()>=mines.gasPrice() && game->canResearch(mines,shop)) {
            if(shop->research(mines))break;
        }
    }
    if(me->hasResearched(mines)) {
        int assigned=0;
        for(auto vulture:me->getUnits()) {
            if(assigned>=2)break;
            if(!vulture || !vulture->exists() || !vulture->isCompleted() ||
               vulture->getType()!=BWAPI::UnitTypes::Terran_Vulture ||
               vulture->getSpiderMineCount()<=0 || vulture->isAttackFrame())continue;
            const int mineDistance=355+(3-vulture->getSpiderMineCount())*85;
            const BWAPI::Position minePost=AIV55NaturalPost(
                natural,foe,vulture,assigned?1:-1,mineDistance);
            if(!minePost.isValid())continue;
            g_v55NaturalGuardIds.insert(vulture->getID());
            g_v32GuardIds.insert(vulture->getID());
            if(vulture->getDistance(minePost)>85) {
                if(now-g_v55LastMineOrder[vulture->getID()]>72 &&
                   vulture->move(minePost))g_v55LastMineOrder[vulture->getID()]=now;
            } else if(now-g_v55LastMineOrder[vulture->getID()]>240 &&
                      vulture->useTech(mines,minePost)) {
                g_v55LastMineOrder[vulture->getID()]=now;
            }
            ++assigned;
        }
    }
    bool approachClear=true;
    for(auto ep:game->enemies())for(auto e:ep->getUnits())
        if(e && e->exists() && e->isVisible() && e->isDetected() && !e->isFlying() &&
           !e->getType().isBuilding() &&
           e->getType().groundWeapon()!=BWAPI::WeaponTypes::None &&
           e->getDistance(natural)<1050)approachClear=false;
    g_v55NaturalPrepared=siegedReady>=2 && approachClear;
    game->drawTextScreen(BWAPI::Position(10,282),
        "v55 NATURAL: siege posts %d/2 mines %s ready %s",siegedReady,
        me->hasResearched(mines)?"ON":"WAIT",g_v55NaturalPrepared?"YES":"NO");
}

void AIV20MedicExitClearance() {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if (!me || me->getRace()!=BWAPI::Races::Terran)return;
    const int frame=game->getFrameCount();
    AIV20Reset(frame);
    if(frame%24)return;
    const BWAPI::Position home(me->getStartLocation());
    const BWAPI::Position enemy=AIV6AttackTarget();
    if (!enemy.isValid() || AIV6HomeThreat().isValid())return;
    for (auto medic:me->getUnits()) {
        if (!medic || !medic->exists() || !medic->isCompleted() ||
            medic->getType()!=BWAPI::UnitTypes::Terran_Medic ||
            !medic->isIdle() || AIV4EnemyCloseTo(medic,350) ||
            medic->getDistance(home)<200 || medic->getDistance(home)>1300)continue;
        const int id=medic->getID();
        BWAPI::Position pos=medic->getPosition();
        if (!AIV17OnRoute(pos,home,enemy,240))continue;
        if (!g_v20MedicLastPosition[id].isValid() ||
            g_v20MedicLastPosition[id].getDistance(pos)>40) {
            g_v20MedicLastPosition[id]=pos;
            g_v20MedicStillSince[id]=frame;
            continue;
        }
        if (frame-g_v20MedicStillSince[id]<96 ||
            frame-g_v20MedicLastDetour[id]<240)continue;
        int traffic=0;
        for (auto u:me->getUnits()) {
            if (!u || !u->exists() || u==medic || !AIV7IsArmyUnit(u))continue;
            if (u->getDistance(medic)<115)++traffic;
        }
        if(traffic<2)continue;
        BWAPI::Position step=AIV8SideStep(medic,enemy);
        if(step.isValid() && medic->move(step)) {
            g_v20MedicLastDetour[id]=frame;
            g_v20MedicStillSince[id]=frame;
            game->printf("v20 traffic: idle Medic moved aside");
            return;
        }
    }
}


BWAPI::TilePosition AIV11ExpansionSite(BWAPI::Unit builder) {
    // STARTCRAFT_AI_V15: choose a ground-accessible resource base, never an
    // arbitrary central mineral cluster.
    return AIV15NaturalExpansionSite(builder);
}

bool AIV11Expand(AIV11Counts c) {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    const int frame=game->getFrameCount();
    if(c.cc>=3 || c.unfinishedCC || c.scv<20 || c.factory==0 || c.barracks==0 ||
       AIV11PendingBuild(BWAPI::UnitTypes::Terran_Command_Center,900) ||
       me->minerals()<400 || frame-g_v11LastExpandCheck<192)return false;
    g_v11LastExpandCheck=frame;
    auto w=AIV11BuildWorker();
    auto tile=AIV11ExpansionSite(w);
    if(tile.isValid() && w && w->build(BWAPI::UnitTypes::Terran_Command_Center,tile)){
        g_v11LastBuildOrder=frame;
        game->printf("v11 EXPAND @%d,%d",tile.x,tile.y);
        return true;
    }
    if(frame-g_v11LastNotice>1000){
        game->printf("v11: no safe expansion site: continuing normal production");
        g_v11LastNotice=frame;
    }
    g_v11NoExpansionUntil=frame+400;
    return false;
}

// STARTCRAFT_AI_V19_GAS_BALANCE
// Replaces AIV11BalanceGas. Corrects workers permanently remaining on gas
// while mineral income is short. One reallocation per refinery / 3 seconds.
void AIV11BalanceGas() {
    auto& game = BWAPI::Broodwar;
    auto me = game->self();
    if (!me || game->getFrameCount() % 72) return;
    const int mineralCash = me->minerals();
    const int gasCash = me->gas();
    int target = 3;
    if (gasCash >= 350 && gasCash >= mineralCash + 150) target = 1;
    else if (gasCash >= 220 && gasCash >= mineralCash + 80) target = 2;

    for (auto refinery : me->getUnits()) {
        if (!refinery || !refinery->exists() ||
            refinery->getType() != BWAPI::UnitTypes::Terran_Refinery ||
            !refinery->isCompleted()) continue;
        std::vector<BWAPI::Unit> gasWorkers;
        for (auto w : me->getUnits()) {
            if (!w || !w->exists() || w->getType() != BWAPI::UnitTypes::Terran_SCV ||
                !w->isCompleted() || w->isConstructing() || w->isRepairing() ||
                w->getID() == g_v5ScoutId || AIV353ReservedBuilder(w)) continue;
            if (w->getOrderTarget() == refinery ||
                (w->isGatheringGas() && w->getDistance(refinery) < 175)) {
                gasWorkers.push_back(w);
            }
        }
        if (static_cast<int>(gasWorkers.size()) > target) {
            // Select a mineral patch near one of our active command centers.
            BWAPI::Unit nearestMineral = nullptr;
            BWAPI::Unit transfer = gasWorkers.back();
            int bestDistance = 100000000;
            for (auto mineral : game->getMinerals()) {
                if (!mineral || !mineral->exists()) continue;
                bool nearOwnBase = false;
                for (auto base : me->getUnits()) {
                    if (base && base->exists() && base->isCompleted() &&
                        base->getType() == BWAPI::UnitTypes::Terran_Command_Center &&
                        base->getDistance(mineral) < 550) {
                        nearOwnBase = true;
                        break;
                    }
                }
                if (!nearOwnBase) continue;
                const int distance = transfer->getDistance(mineral);
                if (distance < bestDistance) { bestDistance = distance; nearestMineral = mineral; }
            }
            if (nearestMineral) transfer->rightClick(nearestMineral);
            continue;
        }
        if (static_cast<int>(gasWorkers.size()) >= target) continue;
        BWAPI::Unit nearest = nullptr;
        int dist = 10000000;
        for (auto worker : me->getUnits()) {
            if (!worker || !worker->exists() ||
                worker->getType() != BWAPI::UnitTypes::Terran_SCV ||
                !worker->isCompleted() || worker->isConstructing() ||
                worker->isRepairing() || worker->isGatheringGas() ||
                worker->getID() == g_v5ScoutId || AIV353ReservedBuilder(worker) ||
                worker->getDistance(refinery) > 620) continue;
            const int d = worker->getDistance(refinery);
            if (d < dist) { nearest = worker; dist = d; }
        }
        if (nearest) nearest->rightClick(refinery);
    }
}


static int g_v28GasReserve=0; // STARTCRAFT_AI_V28_GAS
static int g_v28MilitaryMineralReserve=0;
static bool g_v29TerranMech=false; // STARTCRAFT_AI_V29_MECH
void AIV11Produce(AIV11Counts c) {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    const AIV11Counts nowSupply = AIV11Count();
    const bool depotMissing = me->supplyTotal() < 400 &&
        me->supplyTotal() - me->supplyUsed() <= 22 &&
        nowSupply.unfinishedDepot == 0 &&
        !AIV11PendingBuild(BWAPI::UnitTypes::Terran_Supply_Depot);
    const int reserve=(std::max)((std::max)(g_v11Reserve,g_v28MilitaryMineralReserve),depotMissing?100:0); // STARTCRAFT_AI_V19
    const int frame=game->getFrameCount();
    if(frame%12 || !me)return;
    // Factory add-ons/production in each idle facility.
    for(auto fact:me->getUnits()){
        if(fact->getType()!=BWAPI::UnitTypes::Terran_Factory || !fact->isCompleted() ||
           fact->isTraining())continue;
        auto addon=fact->getAddon();
        if(!addon){
            // STARTCRAFT_AI_V42_FIRST_VULTURE: an add-on blocks Factory
            // production while it is under construction.  Against Protoss,
            // field one Vulture before committing the first Factory to its
            // Machine Shop so the early Zealot/Dragoon timing is not met by
            // Marines alone.  This first defender may spend through the CC
            // reserve; it is the small price that makes the expansion viable.
            if(AIV13OpponentRace()==BWAPI::Races::Protoss ||
               AIV13OpponentRace()==BWAPI::Races::Terran){
                int vultures=0;
                for(auto u:me->getUnits())
                    if(u && u->exists() &&
                       u->getType()==BWAPI::UnitTypes::Terran_Vulture)++vultures;
                if(vultures==0){
                    if(me->minerals()>=75 &&
                       game->canMake(BWAPI::UnitTypes::Terran_Vulture,fact))
                        fact->train(BWAPI::UnitTypes::Terran_Vulture);
                    continue;
                }
            }
            if(me->minerals()>=50+reserve && me->gas()>=50){
                if(fact->buildAddon(BWAPI::UnitTypes::Terran_Machine_Shop)){
                    g_v11ShopFailures[fact->getID()]=0;
                    continue;
                }
                ++g_v11ShopFailures[fact->getID()];
            }
            // An addon may be blocked by bad building placement; do not leave
            // this Factory idle forever if it can at least produce Vultures.
            if(g_v11ShopFailures[fact->getID()]>=5 && me->minerals()>=75+reserve &&
               game->canMake(BWAPI::UnitTypes::Terran_Vulture,fact))
                fact->train(BWAPI::UnitTypes::Terran_Vulture);
            continue;
        }
        // STARTCRAFT_AI_V41_TVP_TEMPO: a Machine Shop takes long enough that
        // waiting for it left the first Factory completely idle during the
        // timing when Pluto already had Zealots and Dragoons.  Factories may
        // train while their add-on is being constructed, so produce one or
        // more Vultures instead of banking resources and fielding no mech.
        if(!addon->isCompleted()){
            if(me->minerals()>=75+reserve &&
               game->canMake(BWAPI::UnitTypes::Terran_Vulture,fact))
                fact->train(BWAPI::UnitTypes::Terran_Vulture);
            continue;
        }
        // STARTCRAFT_AI_V29_FACTORY_MIX: real Terran-vs-Terran mech, not
        // endless barracks bio with a different strategic HUD label.
        if(g_v29TerranMech && addon->isCompleted() && c.tank>=2){
            int vultures=0,goliaths=0;
            bool armory=false,enemyAir=false;
            for(auto u:me->getUnits())if(u && u->exists()){
                if(u->getType()==BWAPI::UnitTypes::Terran_Vulture)++vultures;
                if(u->getType()==BWAPI::UnitTypes::Terran_Goliath)++goliaths;
                if(u->getType()==BWAPI::UnitTypes::Terran_Armory && u->isCompleted())armory=true;
            }
            for(auto ep:game->enemies())for(auto e:ep->getUnits())
                if(e && e->exists() && e->isVisible() && e->isFlying() &&
                   !e->getType().isBuilding())enemyAir=true;
            if(enemyAir && armory && goliaths<4 &&
               me->minerals()>=100+reserve && me->gas()>=50+g_v28GasReserve &&
               game->canMake(BWAPI::UnitTypes::Terran_Goliath,fact) &&
               fact->train(BWAPI::UnitTypes::Terran_Goliath))continue;
            if(vultures<(std::max)(2,c.tank/2) && me->minerals()>=75+reserve &&
               game->canMake(BWAPI::UnitTypes::Terran_Vulture,fact) &&
               fact->train(BWAPI::UnitTypes::Terran_Vulture))continue;
        }
        // STARTCRAFT_AI_V55_TVP_MINE_SCREEN: after the first two Tanks,
        // maintain two Vultures to seed the natural approach with Spider Mines.
        if(AIV13OpponentRace()==BWAPI::Races::Protoss && c.tank>=2) {
            int vultures=0;
            for(auto u:me->getUnits())if(u && u->exists() &&
                u->getType()==BWAPI::UnitTypes::Terran_Vulture)++vultures;
            if(vultures<2 && me->minerals()>=75+reserve &&
               game->canMake(BWAPI::UnitTypes::Terran_Vulture,fact) &&
               fact->train(BWAPI::UnitTypes::Terran_Vulture))continue;
        }
        if(addon->isCompleted() && me->minerals()>=150+reserve && me->gas()>=100+g_v28GasReserve &&
           game->canMake(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode,fact))
            fact->train(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode);
    }
    // Call existing research logic (does not replace proven tank control).
    AIV3ResearchSiegeMode();
    for(auto rax:me->getUnits()){
        if(rax->getType()!=BWAPI::UnitTypes::Terran_Barracks || !rax->isCompleted() ||
           rax->isTraining() || me->minerals()<50+reserve)continue;
        const bool tvz=AIV13OpponentRace()==BWAPI::Races::Zerg;
        const int targetMedics=(std::min)(18,tvz?(c.marine+4)/5:c.marine/6);
        bool canMedic=false;
        for(auto academy:me->getUnits())
            if(academy->getType()==BWAPI::UnitTypes::Terran_Academy && academy->isCompleted())canMedic=true;
        if(canMedic && c.medic<targetMedics && me->gas()>=25 &&
           game->canMake(BWAPI::UnitTypes::Terran_Medic,rax)){
            if(rax->train(BWAPI::UnitTypes::Terran_Medic)){++c.medic;continue;}
        }
        // A confirmed TvT matchup transitions to tank/vulture production.
        // STARTCRAFT_AI_V43_TVT_MECH: six opening Marines cover the Factory
        // transition. TvT then spends on Vultures/Tanks instead of drifting
        // into the ineffective bio-mech mix seen against Pluto.
        if(g_v29TerranMech && c.marine>=6)continue;
        // STARTCRAFT_AI_V55_TVP_MARINE_CAP: Marines screen the mech army;
        // they do not replace Tanks and Vultures. Keep the one-base count
        // compact and allow a modest reinforcement after the natural lands.
        if(AIV13OpponentRace()==BWAPI::Races::Protoss) {
            const int marineCap=c.completedCC>=2?16:
                (AIV25Seen(BWAPI::UnitTypes::Protoss_Gateway)>=2?12:10);
            if(c.marine>=marineCap)continue;
        }
        if(game->canMake(BWAPI::UnitTypes::Terran_Marine,rax) &&
           rax->train(BWAPI::UnitTypes::Terran_Marine))++c.marine;
    }
}

// Single priority chain replaces the conflicting v1-v10 economy loops.
void AIV11EconomyDirector() {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if(!me || me->getRace()!=BWAPI::Races::Terran)return;
    AIV11Reset();
    int frame=game->getFrameCount();
    AIV11Counts c=AIV11Count();
    game->drawTextScreen(BWAPI::Position(10,126),
         "AI v11 Eco: M%d G%d SCV%d/%d CC%d Rax%d Fac%d cap%d/%d",
         me->minerals(),me->gas(),c.scv,AIV11WorkerTarget(),c.cc,c.barracks,c.factory,
         me->supplyUsed()/2,me->supplyTotal()/2);
    if(g_v11Reserve)
        game->drawTextScreen(BWAPI::Position(10,138),"Expansion reserve %d (army still building)",g_v11Reserve);
    if(frame%12)return;
    const int free=me->supplyTotal()-me->supplyUsed();
    // Small, temporary mineral buffer to reach first expansion, not a full production stop.
    bool saveExpansion=c.cc==1 && c.scv>=23 && c.factory>=1 && c.barracks>=1 &&
                       c.tank>=1 && frame>=g_v11NoExpansionUntil &&
                       !AIV6HomeThreat().isValid();
    g_v11Reserve=saveExpansion?150:0;
    bool built=false;
    if(frame-g_v11LastBuildOrder>=96) {
        // Supply emergency takes precedence; ordinary supply only when needed.
        if(free<=4) built=AIV11Supply(c);
        if(!built && c.barracks==0 && c.scv>=11)built=AIV11BuildNear(BWAPI::UnitTypes::Terran_Barracks);
        if(!built && c.refinery<c.completedCC)built=AIV11Refinery(c);
        if(!built && c.factory==0 && c.barracks>0 && c.scv>=16 && me->gas()>=100)
            built=AIV11BuildNear(BWAPI::UnitTypes::Terran_Factory);
        if(!built && c.academy==0 && c.barracks>0 && c.factory>0 && c.scv>=18 && me->minerals()>=175)
            built=AIV11BuildNear(BWAPI::UnitTypes::Terran_Academy);
        if(!built && saveExpansion && me->minerals()>=400)built=AIV11Expand(c);
        if(!built && c.barracks>=1 && c.scv>=18 && c.barracks<(c.completedCC>=2?4:2) &&
           me->minerals()>=(c.barracks==1?300:500))
            built=AIV11BuildNear(BWAPI::UnitTypes::Terran_Barracks);
        if(!built && c.factory>=1 && c.scv>=22 && c.factory<(c.completedCC>=2?3:2) &&
           me->minerals()>=350 && me->gas()>=200)
            built=AIV11BuildNear(BWAPI::UnitTypes::Terran_Factory);
        if(!built && free>4)built=AIV11Supply(c);
        // When income floats, prioritize adding production capacity, not depots.
    }
    // Do not stop all military training while saving for an expansion.
    AIV11Produce(c);
    AIV11BalanceGas();
}
// STARTCRAFT_AI_V13: replay-informed Terran opening priorities.
// Derived from partial replay build-order observations, NOT supervised AI training.
// This code is inserted into the existing anonymous namespace at the end of
// AI v11's economy block. It reuses v11 production, supply, workers and v12 placement.
// STARTCRAFT_AI_V30_PERSISTENT_RACE
static BWAPI::Race g_v30EnemyRace=BWAPI::Races::Unknown;
static int g_v30LastRaceFrame=-1;
BWAPI::Race AIV13OpponentRace() {
    auto& game = BWAPI::Broodwar;  // Reference! BWAPI GameWrapper is non-copyable.
    for (auto p : game->enemies()) {
        if (!p) continue;
        BWAPI::Race r = p->getRace();
        if (r == BWAPI::Races::Zerg || r == BWAPI::Races::Protoss ||
            r == BWAPI::Races::Terran) return r;
    }
    return g_v30EnemyRace; // STARTCRAFT_AI_V30_RACE_FALLBACK
}

const char* AIV13MatchupLabel(BWAPI::Race r) {
    if (r == BWAPI::Races::Zerg) return "TvZ";
    if (r == BWAPI::Races::Protoss) return "TvP";
    if (r == BWAPI::Races::Terran) return "TvT";
    return "Tv?";
}

// STARTCRAFT_AI_V22 - source is inserted immediately before AIV13EconomyDirector.
// Preserves pre-existing combat, rally, unit production and supply logic.
// STARTCRAFT_AI_V25_INTELLIGENCE
// Reactive enemy intelligence, persistent SCV reconnaissance and ground-path rally.
// Inserted before AIV22MatchupGoals, inside STARTcraft's existing translation unit.
static int g_v25LastFrame = -1;
static int g_v25NextScout = 240;
static int g_v25LastScoutCommand = -100000;
static int g_v25LastScoutRecruit = -100000;
static int g_v25LastRally = -100000;
static int g_v25PerimeterStep = 0;
static int g_v25LastPlanReport = -100000;
static std::map<int,int> g_v25LastSpawnVisit;
static std::map<int,std::pair<int,int>> g_v25SeenBuilding; // unit ID -> (UnitType ID, last seen frame)
static std::map<int,std::pair<int,int>> g_v25SeenArmy;
static BWAPI::Position g_v25Rally = BWAPI::Positions::None;

static void AIV25ResetIfNewMatch() {
    const int frame=BWAPI::Broodwar->getFrameCount();
    if (frame>=g_v25LastFrame) {g_v25LastFrame=frame;return;}
    g_v25LastFrame=frame;
    g_v25NextScout=240;
    g_v25LastScoutCommand=-100000;
    g_v25LastScoutRecruit=-100000;
    g_v25LastRally=-100000;
    g_v25PerimeterStep=0;
    g_v25LastPlanReport=-100000;
    g_v25LastSpawnVisit.clear();
    g_v25SeenBuilding.clear();
    g_v25SeenArmy.clear();
    g_v25Rally=BWAPI::Positions::None;
    g_v5ScoutId=-1;
}

static void AIV25ObserveVisibleEnemies() {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if (!me) return;
    const int frame=game->getFrameCount();
    // Only process units currently visible. Never inspect unseen unit state.
    for (auto player:game->enemies()) {
        if (!player) continue;
        for (auto unit:player->getUnits()) {
            if (!unit || !unit->exists() || !unit->isVisible()) continue;
            if (!unit->getType().isBuilding()) {
                if (!unit->getType().isWorker())
                    g_v25SeenArmy[unit->getID()]=std::make_pair(unit->getType().getID(),frame);
                continue;
            }
            g_v25SeenBuilding[unit->getID()]=std::make_pair(unit->getType().getID(),frame);
            if (g_v6EnemyStart.isValid()) continue;
            for (auto start:game->getStartLocations()) {
                if (start==me->getStartLocation())continue;
                if (unit->getDistance(BWAPI::Position(start))<630) {
                    g_v6EnemyStart=start;
                    game->printf("v25 Scout: enemy main found");
                    break;
                }
            }
        }
    }
    if(frame%240==0) {
        for(auto it=g_v25SeenBuilding.begin();it!=g_v25SeenBuilding.end();) {
            // Keep a recent sighting as evidence, not as permanently existing structure.
            if(frame-it->second.second>3600)it=g_v25SeenBuilding.erase(it);
            else ++it;
        }
        for(auto it=g_v25SeenArmy.begin();it!=g_v25SeenArmy.end();) {
            if(frame-it->second.second>720)it=g_v25SeenArmy.erase(it);
            else ++it;
        }
    }
}

static int AIV25Seen(BWAPI::UnitType kind) {
    int n=0;
    for(const auto& item:g_v25SeenBuilding)
        if(item.second.first==kind.getID())++n;
    return n;
}

static int AIV25RecentArmy(BWAPI::UnitType kind) {
    int count=0;
    const int frame=BWAPI::Broodwar->getFrameCount();
    for(const auto& item:g_v25SeenArmy)
        if(item.second.first==kind.getID() && frame-item.second.second<=720)++count;
    return count;
}

static int AIV25VisibleEnemyArmy(BWAPI::Position p,int radius) {
    int n=0;
    for(auto enemyPlayer:BWAPI::Broodwar->enemies()) {
        if(!enemyPlayer)continue;
        for(auto e:enemyPlayer->getUnits())
            if(e && e->exists() && e->isVisible() && !e->getType().isBuilding() &&
               !e->getType().isWorker() && e->getDistance(p)<radius)++n;
    }
    return n;
}

// STARTCRAFT_AI_V46_MUTA_RING: one turret was not enough against Pluto's
// eleven-Mutalisk wave. Confirmed Spire/Mutalisk intelligence creates a small
// non-overlapping turret ring at every completed base, one building at a time.
static void AIV46AirDefense() {
    auto& game=BWAPI::Broodwar;auto me=game->self();
    if(!me || AIV13OpponentRace()!=BWAPI::Races::Zerg || game->getFrameCount()%24)return;
    bool visibleMuta=false;
    for(auto ep:game->enemies())for(auto e:ep->getUnits())
        if(e && e->exists() && e->isVisible() &&
           (e->getType()==BWAPI::UnitTypes::Zerg_Mutalisk ||
            e->getType()==BWAPI::UnitTypes::Zerg_Guardian ||
            e->getType()==BWAPI::UnitTypes::Zerg_Devourer))visibleMuta=true;
    if(!visibleMuta && AIV25Seen(BWAPI::UnitTypes::Zerg_Spire)==0 &&
       AIV25Seen(BWAPI::UnitTypes::Zerg_Greater_Spire)==0)return;
    bool ebay=false;
    for(auto u:me->getUnits())if(u && u->exists() && u->isCompleted() &&
        u->getType()==BWAPI::UnitTypes::Terran_Engineering_Bay)ebay=true;
    if(!ebay){
        if(me->minerals()>=125)AIV11BuildNear(BWAPI::UnitTypes::Terran_Engineering_Bay);
        return;
    }
    if(AIV11PendingBuild(BWAPI::UnitTypes::Terran_Missile_Turret))return;
    const BWAPI::Position main(me->getStartLocation());
    std::vector<BWAPI::Unit> bases;
    for(auto cc:me->getUnits())if(cc && cc->exists() && cc->isCompleted() &&
        !cc->isFlying() && cc->getType()==BWAPI::UnitTypes::Terran_Command_Center)
        bases.push_back(cc);
    // Cover the exposed natural before filling the main-base ring.
    std::sort(bases.begin(),bases.end(),[&](BWAPI::Unit a,BWAPI::Unit b){
        return a->getDistance(main)>b->getDistance(main);
    });
    for(auto cc:bases) {
        int nearby=0;
        for(auto u:me->getUnits())if(u && u->exists() &&
            u->getType()==BWAPI::UnitTypes::Terran_Missile_Turret &&
            u->getDistance(cc)<620)++nearby;
        const int target=cc->getDistance(main)<400?2:3;
        if(nearby>=target)continue;
        const auto base=cc->getTilePosition();
        for(int r=5;r<=10;++r)for(int dy=-r;dy<=r;++dy)for(int dx=-r;dx<=r;++dx){
            if((std::max)(std::abs(dx),std::abs(dy))!=r)continue;
            BWAPI::TilePosition t(base.x+dx,base.y+dy);
            if(!t.isValid() || !game->canBuildHere(t,BWAPI::UnitTypes::Terran_Missile_Turret,nullptr,true))continue;
            const BWAPI::Position center(t);
            bool resource=false;
            for(auto m:game->getMinerals())if(m && m->exists() && m->getDistance(center)<170)resource=true;
            for(auto g:game->getGeysers())if(g && g->exists() && g->getDistance(center)<190)resource=true;
            if(resource)continue;
            if(AIV11BuildNear(BWAPI::UnitTypes::Terran_Missile_Turret,t))return;
        }
    }
}

// Distinct strategies, informed by *observed* technology and army development.
// Baseline only until scout intel is available; goals may increase with bases.
// STARTCRAFT_AI_V28_STRATEGY
// Observed opponent tech affects actual spending, not just an HUD target.
static int g_v28ScaleFailures=0;
static int g_v28LastScaleMessage=-10000;
static int g_v28ScalePausedUntil=-10000;
static const char* g_v28PlanLabel="UNSCOUTED";


static void AIV25AdaptiveGoals(BWAPI::Race enemy,int bases,int& rax,int& fac) {
    if((enemy==BWAPI::Races::Unknown || enemy==BWAPI::Races::Random) &&
       g_v30EnemyRace!=BWAPI::Races::Unknown)enemy=g_v30EnemyRace;
    // Random-opponent races may remain unknown until scouted. Infer from
    // observed buildings instead of locking into the default 2+2 template.
    if(enemy==BWAPI::Races::Unknown || enemy==BWAPI::Races::Random){
        if(AIV25Seen(BWAPI::UnitTypes::Zerg_Hatchery)>0 ||
           AIV25Seen(BWAPI::UnitTypes::Zerg_Spawning_Pool)>0)
            enemy=BWAPI::Races::Zerg;
        else if(AIV25Seen(BWAPI::UnitTypes::Protoss_Gateway)>0 ||
                AIV25Seen(BWAPI::UnitTypes::Protoss_Pylon)>0)
            enemy=BWAPI::Races::Protoss;
        else if(AIV25Seen(BWAPI::UnitTypes::Terran_Barracks)>0 ||
                AIV25Seen(BWAPI::UnitTypes::Terran_Factory)>0)
            enemy=BWAPI::Races::Terran;
    }
    const bool multi=bases>=2;
    if(enemy==BWAPI::Races::Zerg){
        const bool spire=AIV25Seen(BWAPI::UnitTypes::Zerg_Spire)>0;
        const bool hydra=AIV25Seen(BWAPI::UnitTypes::Zerg_Hydralisk_Den)>0 ||
                         AIV25RecentArmy(BWAPI::UnitTypes::Zerg_Hydralisk)>=3;
        if(hydra){g_v28PlanLabel="TvZ BIONIC";rax=multi?6:4;fac=multi?3:2;} // STARTCRAFT_AI_V33_TVZ_FAC
        else if(spire){g_v28PlanLabel="TvZ BIONIC AIR";rax=multi?6:4;fac=multi?2:1;}
        else {g_v28PlanLabel="TvZ BIONIC";rax=multi?6:4;fac=multi?2:1;}
    }else if(enemy==BWAPI::Races::Protoss){
        g_v28PlanLabel="TvP MECH ONE RAX";rax=1;fac=multi?5:2;
    }else if(enemy==BWAPI::Races::Terran){
        bool enemyBio=AIV25Seen(BWAPI::UnitTypes::Terran_Barracks)>=3 &&
                      AIV25Seen(BWAPI::UnitTypes::Terran_Factory)==0;
        if(enemyBio){g_v28PlanLabel="TvT MECH VS BIO";rax=multi?2:1;fac=multi?4:3;}
        else {g_v28PlanLabel="TvT MECH";rax=1;fac=multi?5:3;}
    }else{
        g_v28PlanLabel="UNKNOWN FLEX";rax=multi?3:2;fac=multi?3:2;
    }
}


// Scout-driven timing: a lightly defended confirmed expansion may be hit
// with a smaller coherent army. Heavy observed resistance delays a push.
// If there is no *recently observed* intel, use the proven v8 default.
static bool AIV25AttackReady(int marines,int tanks,int medics,BWAPI::Position enemy) {
    if(!enemy.isValid())return false;
    // STARTCRAFT_AI_V42_TVP_MECH_PUSH: v41 deliberately skips the early
    // Academy in TvP, so requiring Medics made every offensive threshold
    // unreachable. Use a coherent tank-supported mech threshold instead.
    if (AIV13OpponentRace()==BWAPI::Races::Protoss) {
        int vultures=0,goliaths=0;
        for(auto u:BWAPI::Broodwar->self()->getUnits()) {
            if(!u || !u->exists() || !u->isCompleted() ||
               g_v32GuardIds.count(u->getID()))continue;
            if(u->getType()==BWAPI::UnitTypes::Terran_Vulture)++vultures;
            else if(u->getType()==BWAPI::UnitTypes::Terran_Goliath)++goliaths;
        }
        return (tanks>=4 && (marines>=10 || vultures+goliaths>=3)) ||
               tanks>=6;
    }
    // STARTCRAFT_AI_V32_MECH_ATTACK: a TvT tank/vulture army must NEVER
    // wait for the Marines and Medics deliberately disabled by its own build.
    if (AIV13OpponentRace()==BWAPI::Races::Terran) {
        int readyTanks=0, readyVultures=0, readyGoliaths=0;
        for(auto u:BWAPI::Broodwar->self()->getUnits()) {
            if(!u || !u->exists() || !u->isCompleted() ||
               g_v32GuardIds.count(u->getID()))continue;
            const BWAPI::UnitType t=u->getType();
            if(t==BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode ||
               t==BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)++readyTanks;
            else if(t==BWAPI::UnitTypes::Terran_Vulture)++readyVultures;
            else if(t==BWAPI::UnitTypes::Terran_Goliath)++readyGoliaths;
        }
        return (readyTanks>=5 && (readyVultures>=2 || readyGoliaths>=2)) ||
               readyTanks>=8;
    }
    const bool normal=marines>=16 && tanks>=2 && medics>=2;
    if(!g_v6EnemyStart.isValid())return normal;
    const int now=BWAPI::Broodwar->getFrameCount();
    int freshEnemyBuildings=0;
    for(const auto& item:g_v25SeenBuilding)
        if(now-item.second.second<480)++freshEnemyBuildings;
    if(freshEnemyBuildings==0)return normal;
    const int recentUnits=(int)g_v25SeenArmy.size();
    const int enemyFighters=AIV25VisibleEnemyArmy(BWAPI::Position(g_v6EnemyStart),720);
    bool expansion=false;
    if(AIV13OpponentRace()==BWAPI::Races::Protoss)
        expansion=AIV25Seen(BWAPI::UnitTypes::Protoss_Nexus)>=2;
    else if(AIV13OpponentRace()==BWAPI::Races::Terran)
        expansion=AIV25Seen(BWAPI::UnitTypes::Terran_Command_Center)>=2;
    else if(AIV13OpponentRace()==BWAPI::Races::Zerg)
        expansion=AIV25Seen(BWAPI::UnitTypes::Zerg_Hatchery)>=2;
    if(expansion && enemyFighters<=3 && recentUnits<=5)
        return marines>=12 && tanks>=1 && medics>=1;
    if(enemyFighters>=12 || recentUnits>=14)
        return marines>=20 && tanks>=3 && medics>=2;
    return normal;
}

static BWAPI::Position AIV25ChooseScoutDestination(BWAPI::Unit scout) {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if (!me) return BWAPI::Positions::None;
    const int now=game->getFrameCount();
    if (!g_v6EnemyStart.isValid()) {
        int oldest=2147483647;
        BWAPI::TilePosition chosen=BWAPI::TilePositions::None;
        for (auto start:game->getStartLocations()) {
            if(start==me->getStartLocation())continue;
            const int visit=g_v25LastSpawnVisit.count(start.y*game->mapWidth()+start.x)
                ?g_v25LastSpawnVisit[start.y*game->mapWidth()+start.x]:-100000;
            // Revisit locations if enemy remains unidentified.
            if (visit<oldest) {oldest=visit;chosen=start;}
        }
        if(!chosen.isValid())return BWAPI::Positions::None;
        BWAPI::Position target(chosen);
        if(scout && scout->getDistance(target)<220)
            g_v25LastSpawnVisit[chosen.y*game->mapWidth()+chosen.x]=now;
        return target;
    }
    // Repeatedly sweep a ring around enemy production buildings. Do not
    // return home merely because the enemy main has been discovered.
    BWAPI::Position base(g_v6EnemyStart);
    const int dx[8]={-190,0,190,260,190,0,-190,-260};
    const int dy[8]={-170,-240,-170,0,170,245,170,0};
    if(scout && scout->getDistance(BWAPI::Position(base.x+dx[g_v25PerimeterStep],
                                                  base.y+dy[g_v25PerimeterStep]))<110)
        g_v25PerimeterStep=(g_v25PerimeterStep+1)%8;
    for(int k=0;k<8;++k) {
        int i=(g_v25PerimeterStep+k)%8;
        int x=base.x+dx[i],y=base.y+dy[i];
        if(x<24 || y<24 || x>=game->mapWidth()*32-24 || y>=game->mapHeight()*32-24)continue;
        BWAPI::Position dest(x,y);
        if(scout && scout->hasPath(dest)){
            g_v25PerimeterStep=i;
            return dest;
        }
    }
    return base;
}

static void AIV25ScoutController() {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if(!me || me->getRace()!=BWAPI::Races::Terran)return;
    AIV25ResetIfNewMatch();
    const int now=game->getFrameCount();
    if(now%12)return;
    AIV25ObserveVisibleEnemies();
    if(now<g_v25NextScout)return;
    BWAPI::Unit scout=nullptr;
    for(auto unit:me->getUnits()) {
        if(unit && unit->exists() && unit->getType()==BWAPI::UnitTypes::Terran_SCV &&
           unit->getID()==g_v5ScoutId){scout=unit;break;}
    }
    if(!scout){
        g_v5ScoutId=-1;
        // Preserve economy and avoid sending SCVs repeatedly to their deaths.
        int scvCount=0;
        for(auto unit:me->getUnits()) if(unit && unit->exists() &&
             unit->getType()==BWAPI::UnitTypes::Terran_SCV)++scvCount;
        if(scvCount<11 || now-g_v25LastScoutRecruit<480)return;
        BWAPI::Position goal=g_v6EnemyStart.isValid() ? BWAPI::Position(g_v6EnemyStart)
                                                        : BWAPI::Position(me->getStartLocation());
        int best=2147483647;
        for(auto w:me->getUnits()) {
            if(!w || !w->exists() || w->getType()!=BWAPI::UnitTypes::Terran_SCV ||
               !w->isCompleted() || w->isConstructing() || w->isRepairing() ||
               w->isGatheringGas() || w->isCarryingGas() || w->getHitPoints()<48)continue;
            int d=w->getDistance(goal);
            if(d<best){best=d;scout=w;}
        }
        if(!scout)return;
        g_v5ScoutId=scout->getID();
        g_v25LastScoutRecruit=now;
        game->printf("v25 persistent scout recruited");
    }
    // STARTCRAFT_AI_V26_SCOUT_ESCAPE: retreat BEFORE normal scout cooldown.
    // Zealots are recognized farther out so an SCV does not die merely
    // because the next waypoint lies through the enemy army.
    BWAPI::Unit nearestDanger=nullptr;
    int dangerDistance=99999;
    for(auto opponent:game->enemies()) for(auto foe:opponent->getUnits()) {
        if(!foe || !foe->exists() || !foe->isVisible() || foe->isFlying() ||
           foe->getType().isBuilding() ||
           foe->getType().groundWeapon()==BWAPI::WeaponTypes::None)continue;
        int d=scout->getDistance(foe);
        int warning=(foe->getType()==BWAPI::UnitTypes::Protoss_Zealot ? 420 : 315);
        if(d<warning && d<dangerDistance){nearestDanger=foe;dangerDistance=d;}
    }
    if(nearestDanger) {
        const auto home=BWAPI::Position(me->getStartLocation());
        BWAPI::Position best=home;
        int bestScore=-99999999;
        const int dx[8]={220,165,0,-165,-220,-165,0,165};
        const int dy[8]={0,165,220,165,0,-165,-220,-165};
        for(int i=0;i<8;++i){
            auto p=scout->getPosition();
            BWAPI::Position candidate(p.x+dx[i],p.y+dy[i]);
            if(!candidate.isValid() || !scout->hasPath(candidate))continue;
            int nearest=99999;
            for(auto enemy:game->enemies())for(auto foe:enemy->getUnits()) {
                if(!foe || !foe->exists() || !foe->isVisible() ||
                   foe->isFlying() ||
                   foe->getType().groundWeapon()==BWAPI::WeaponTypes::None)continue;
                nearest=(std::min)(nearest,foe->getDistance(candidate));
            }
            int score=nearest*3-candidate.getApproxDistance(home);
            if(score>bestScore){bestScore=score;best=candidate;}
        }
        if(now-g_v25LastScoutCommand>=8) {
            if(scout->move(best))g_v25LastScoutCommand=now;
        }
        // We have already preserved observed tech/army in v25 memory.
        // Stop sending a nearly-dead scout through the same danger again.
        if(scout->getHitPoints()<=38) {
            g_v5ScoutId=-1;
            g_v25NextScout=now+720;
        }
        return;
    }
    if(now-g_v25LastScoutCommand<36 || scout->isAttackFrame())return;
    const auto current=scout->getPosition();
    const auto home=BWAPI::Position(me->getStartLocation());
    // Inspect the visible enemy area while scouting; opportunistic worker harass
    // is limited to an isolated worker and cancelled when combat units appear.
    bool defenders=false;
    BWAPI::Unit workerTarget=nullptr;
    int closest=99999;
    for(auto player:game->enemies())for(auto enemy:player->getUnits()) {
        if(!enemy || !enemy->exists() || !enemy->isVisible())continue;
        const int d=scout->getDistance(enemy);
        if(d<210 && !enemy->getType().isWorker() && !enemy->getType().isBuilding() &&
           enemy->getType().groundWeapon()!=BWAPI::WeaponTypes::None)defenders=true;
        if(d<115 && enemy->getType().isWorker() && d<closest) {closest=d;workerTarget=enemy;}
        if(d<210 && enemy->getType().isBuilding() &&
           enemy->getType().groundWeapon()!=BWAPI::WeaponTypes::None)defenders=true;
    }
    if(scout->getHitPoints()<28) {
        if(scout->move(home)) {
            g_v5ScoutId=-1; // Low-HP scout is released; try a replacement later.
            g_v25NextScout=now+360;
            g_v25LastScoutCommand=now;
        }
        return;
    }
    if(workerTarget && !defenders && scout->getHitPoints()>44 &&
       !scout->isUnderAttack() && g_v6EnemyStart.isValid()) {
        // Keep the current attack order; reissuing orders cancels SCV swings.
        if(scout->getOrderTarget()==workerTarget)return;
        if(scout->attack(workerTarget)) {
            g_v25LastScoutCommand=now;
            return;
        }
    }
    if(scout->isUnderAttack() || defenders)g_v25PerimeterStep=(g_v25PerimeterStep+2)%8;
    BWAPI::Position target=AIV25ChooseScoutDestination(scout);
    if(target.isValid() && (scout->isIdle() || scout->getDistance(target)>95 ||
                            scout->isUnderAttack())) {
        if(scout->move(target)) g_v25LastScoutCommand=now;
    }
}

// Ground-path based rally just inside the home-to-enemy exit rather than
// an arbitrary straight-line tile; existing v7 controls forward rally on ATTACK.
static BWAPI::Position AIV25PathRally() {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if(!me)return BWAPI::Positions::None;
    BWAPI::Position fallback=AIV4ChooseRally(me);
    BWAPI::TilePosition home=me->getStartLocation();
    BWAPI::TilePosition dest=BWAPI::TilePositions::None;
    if(g_v6EnemyStart.isValid())dest=g_v6EnemyStart;
    else {
        for(auto spawn:game->getStartLocations())
            if(spawn!=home){dest=spawn;break;}
    }
    if(!dest.isValid())return fallback;
    const int w=game->mapWidth(), h=game->mapHeight();
    if(w<=0 || h<=0 || w>512 || h>512)return fallback;
    const int size=w*h;
    std::vector<int> prev(size,-1);
    std::vector<int> queue;
    queue.reserve(size);
    const int sx=(std::max)(1,(std::min)(w-2,home.x+2));
    const int sy=(std::max)(1,(std::min)(h-2,home.y+2));
    auto walkable=[&](int x,int y)->bool {
        if(x<1 || x>=w-1 || y<1 || y>=h-1)return false;
        return game->isWalkable(x*4+1,y*4+1) &&
               game->isWalkable(x*4+2,y*4+2);
    };
    int origin=sy*w+sx;
    if(!walkable(sx,sy)) {
        bool found=false;
        for(int r=1;r<=6 && !found;++r) for(int dy=-r;dy<=r && !found;++dy)
            for(int dx=-r;dx<=r;++dx) {
                if(walkable(sx+dx,sy+dy)){
                    origin=(sy+dy)*w+sx+dx;found=true;break;
                }
            }
        if(!found)return fallback;
    }
    const int targetx=(std::max)(1,(std::min)(w-2,dest.x+2));
    const int targety=(std::max)(1,(std::min)(h-2,dest.y+2));
    queue.push_back(origin);prev[origin]=origin;
    int goal=-1;
    const int dx[4]={1,-1,0,0},dy[4]={0,0,1,-1};
    for(size_t head=0;head<queue.size();++head) {
        int at=queue[head],x=at%w,y=at/w;
        if((std::abs)(x-targetx)+(std::abs)(y-targety)<5){goal=at;break;}
        for(int i=0;i<4;++i){
            int nx=x+dx[i],ny=y+dy[i];
            if(!walkable(nx,ny))continue;
            int next=ny*w+nx;
            if(prev[next]>=0)continue;
            prev[next]=at;queue.push_back(next);
        }
    }
    if(goal<0)return fallback;
    std::vector<int> reversePath;
    for(int at=goal;at!=origin;at=prev[at]){
        reversePath.push_back(at);
        if(reversePath.size()>size)return fallback;
    }
    reversePath.push_back(origin);
    std::reverse(reversePath.begin(),reversePath.end());
    int select=(std::min)((int)reversePath.size()-1,11); // ~11 tiles toward exit
    // If choke identified along outgoing path, gather 3 tiles before it.
    for(int i=6;i<(std::min)((int)reversePath.size(),23);++i){
        int x=reversePath[i]%w,y=reversePath[i]/w;
        int open=0;
        for(int oy=-2;oy<=2;++oy)for(int ox=-2;ox<=2;++ox)
            if(walkable(x+ox,y+oy))++open;
        if(open<=15){select=(std::max)(3,i-3);break;}
    }
    int at=reversePath[select];
    return BWAPI::Position((at%w)*32+16,(at/w)*32+16);
}

static void AIV25MaintainRally() {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if(!me || me->getRace()!=BWAPI::Races::Terran)return;
    if(g_v7State!=AIV7_ASSEMBLE)return; // v7 attack/regroup rally stays in charge
    const int now=game->getFrameCount();
    if(now%24)return;
    if(!g_v25Rally.isValid() || now-g_v25LastRally>120) {
        // Before expanding, use the ground path toward the entrance. Once a
        // second CC completes, assemble in front of that expansion so routine
        // scout-rally maintenance cannot overwrite the natural staging point.
        int completedCC=0;
        for(auto cc:me->getUnits())
            if(cc && cc->exists() && cc->isCompleted() && !cc->isFlying() &&
               cc->getType()==BWAPI::UnitTypes::Terran_Command_Center)++completedCC;
        g_v25Rally=completedCC>=2 ? AIV33AssemblyPoint() : AIV25PathRally();
        g_v25LastRally=now;
    }
    if(!g_v25Rally.isValid())return;
    // Do not funnel fleeing troops into a threatened choke.
    if(AIV6HomeThreat().isValid())return;
    AIV7SetProductionRally(g_v25Rally);
}

// STARTCRAFT_AI_V27_SCOUT
// One SCV, one finite reconnaissance mission: find base -> watch production
// -> look at likely natural -> retreat. No automatic replacement scout spam.
static int g_v27PrevFrame=-1;
static int g_v27ScoutAttempts=0;
static int g_v27MainSeenFrame=-1;
static int g_v27LastMove=-100000;
static int g_v27LastWaypoint=-100000;
static int g_v27OrbitStep=0;
static int g_v27Phase=0; // 0=start search, 1=observe main, 2=check natural, 3=return
static bool g_v27MissionDone=false;
static BWAPI::Position g_v27Natural=BWAPI::Positions::None;
static BWAPI::Position g_v27LastDestination=BWAPI::Positions::None;

static BWAPI::Position AIV27NearestEnemyNatural(BWAPI::Unit scout) {
    auto& game=BWAPI::Broodwar;
    if(!g_v6EnemyStart.isValid() || !scout)return BWAPI::Positions::None;
    const BWAPI::Position main(g_v6EnemyStart);
    int best=999999;
    BWAPI::Position result=BWAPI::Positions::None;
    for(auto gas:game->getStaticGeysers()) {
        if(!gas || !gas->getInitialPosition().isValid())continue;
        BWAPI::Position center=gas->getInitialPosition();
        const int mainDist=center.getDistance(main);
        if(mainDist<500 || mainDist>1450)continue;
        int minerals=0;
        for(auto m:game->getStaticMinerals())
            if(m && m->getInitialPosition().isValid() &&
               m->getInitialPosition().getDistance(center)<420)++minerals;
        if(minerals<5)continue;
        // Approach a nearby view point; don't send an SCV into the gas geyser.
        const int mx=center.x-main.x,my=center.y-main.y;
        const int scale=(std::max)(1,(std::abs)(mx)+(std::abs)(my));
        BWAPI::Position approach(center.x-mx*240/scale,center.y-my*240/scale);
        if(!approach.isValid() || !scout->hasPath(approach))continue;
        if(mainDist<best){best=mainDist;result=approach;}
    }
    return result;
}

// STARTCRAFT_AI_V28_RECON
// One scout observes enemy PRODUCTION positions and fresh combat units rather
// than patrolling arbitrary map coordinates. Never spends replacement SCVs.
static int g_v28PreviousScoutFrame=-1;
static int g_v28LastViewPoint=-100000;
static int g_v28VantageTurn=0;
static int g_v28LastOrder=-100000;
static int g_v28ObserveStart=-1;
static int g_v28ProductionSeen=0;
static int g_v28ArmySeen=0;
static int g_v28IntelLastUpdated=0;
static BWAPI::Position g_v28Observation=BWAPI::Positions::None;
static std::map<int,BWAPI::Position> g_v28KnownProduction;

static bool AIV28ProductionType(BWAPI::UnitType t) {
    return t==BWAPI::UnitTypes::Protoss_Gateway ||
           t==BWAPI::UnitTypes::Protoss_Robotics_Facility ||
           t==BWAPI::UnitTypes::Protoss_Stargate ||
           t==BWAPI::UnitTypes::Terran_Barracks ||
           t==BWAPI::UnitTypes::Terran_Factory ||
           t==BWAPI::UnitTypes::Terran_Starport ||
           t==BWAPI::UnitTypes::Zerg_Hatchery ||
           t==BWAPI::UnitTypes::Zerg_Lair ||
           t==BWAPI::UnitTypes::Zerg_Hive ||
           t==BWAPI::UnitTypes::Zerg_Spawning_Pool ||
           t==BWAPI::UnitTypes::Zerg_Hydralisk_Den;
}

static void AIV28UpdateScoutIntel() {
    auto& game=BWAPI::Broodwar;
    int latest=game->getFrameCount();
    for(auto player:game->enemies()) for(auto foe:player->getUnits()) {
        if(!foe || !foe->exists() || !foe->isVisible()) continue;
        if(AIV28ProductionType(foe->getType())) {
            g_v28KnownProduction[foe->getID()]=foe->getPosition();
            g_v28ProductionSeen=(int)g_v28KnownProduction.size();
            g_v28IntelLastUpdated=latest;
        }
        if(!foe->getType().isWorker() && !foe->getType().isBuilding()) {
            ++g_v28ArmySeen; // Observations, not a reliable count of unique soldiers.
            g_v28IntelLastUpdated=latest;
        }
    }
}

static int AIV28ScoutDangerAt(BWAPI::Position p) {
    int risk=0;
    for(auto player:BWAPI::Broodwar->enemies()) for(auto e:player->getUnits()) {
        if(!e || !e->exists() || !e->isVisible() || e->isFlying() ||
           e->getType().isBuilding() ||
           e->getType().groundWeapon()==BWAPI::WeaponTypes::None)continue;
        const int d=e->getDistance(p);
        const int warning=e->getType()==BWAPI::UnitTypes::Protoss_Zealot?420:330;
        if(d<warning)risk+=(warning-d)*8+120;
    }
    return risk;
}

static BWAPI::Position AIV28SafeView(BWAPI::Unit scout,BWAPI::Position focus,
                                      BWAPI::Position home,bool withdrawing) {
    auto& game=BWAPI::Broodwar;
    if(!scout || !focus.isValid())return BWAPI::Positions::None;
    const int offsets[16][2]={{-230,0},{-175,-175},{0,-230},{175,-175},
                               {230,0},{175,175},{0,230},{-175,175},
                               {-305,0},{-215,-215},{0,-305},{215,-215},
                               {305,0},{215,215},{0,305},{-215,215}};
    int best=-100000000;
    BWAPI::Position target=BWAPI::Positions::None;
    for(int i=0;i<16;++i){
        BWAPI::Position p(focus.x+offsets[i][0],focus.y+offsets[i][1]);
        if(!p.isValid() || !scout->hasPath(p) ||
           !game->isWalkable(p.x/8,p.y/8))continue;
        // Evaluate candidate and intermediate path to avoid commanding through
        // a Zealot simply because the destination itself is safe.
        int risk=AIV28ScoutDangerAt(p);
        BWAPI::Position here=scout->getPosition();
        for(int step=1;step<=3;++step){
            BWAPI::Position intermediate(here.x+(p.x-here.x)*step/4,
                                        here.y+(p.y-here.y)*step/4);
            risk+=(AIV28ScoutDangerAt(intermediate)/3);
        }
        int score=-(scout->getDistance(p)/4)-risk;
        if(withdrawing)score-=p.getApproxDistance(home)/2;
        // Avoid repeatedly issuing the same waypoint and walking back/forth.
        if(p==g_v28Observation)score-=(g_v28VantageTurn>0?520:0);
        if(score>best){best=score;target=p;}
    }
    return target;
}

// STARTCRAFT_AI_V30_SCOUT
// Correct scout orders, avoid one-shot danger abort, observe actual production.
// Recon objective: examine the enemy's production buildings, record live army
// composition, then check a possible natural before returning. One SCV only.
static BWAPI::Position g_v30LastScoutIssued=BWAPI::Positions::None;
static int g_v30LastScoutCommand=-100000;
static int g_v30DodgeCount=0;
static int g_v29ScoutLastDecision=-100000;
static int g_v29ScoutLookFrame=-100000;
static int g_v29ScoutFocusId=-1;
static int g_v29MainChecks=0;
static int g_v29UnitSightings=0;
static int g_v29ScoutStart=-1;
static int g_v29EnemyTechLastSeen=-1;
static BWAPI::Position g_v29ScoutDestination=BWAPI::Positions::None;
static std::map<int,int> g_v29BuildingChecked;
static std::map<int,int> g_v29ArmySeenOnce;

static bool AIV29ScoutSafePoint(BWAPI::Unit scout,BWAPI::Position point) {
    auto& game=BWAPI::Broodwar;
    return scout && point.isValid() &&
           point.x>=16 && point.y>=16 &&
           point.x<game->mapWidth()*32-16 && point.y<game->mapHeight()*32-16 &&
           game->isWalkable(point.x/8,point.y/8) && scout->hasPath(point);
}

static BWAPI::Position AIV29ScoutVantage(BWAPI::Unit scout,BWAPI::Position focus) {
    if(!scout || !focus.isValid())return BWAPI::Positions::None;
    // A close viewing position is needed for SCV sight. v28's 305px ring
    // often observed nothing at all, especially on raised terrain.
    const int dx[16]={180,128,0,-128,-180,-128,0,128,
                      132,93,0,-93,-132,-93,0,93};
    const int dy[16]={0,128,180,128,0,-128,-180,-128,
                      0,93,132,93,0,-93,-132,-93};
    int best=-100000000;
    BWAPI::Position answer=BWAPI::Positions::None;
    for(int i=0;i<16;++i) {
        BWAPI::Position p(focus.x+dx[i],focus.y+dy[i]);
        if(!AIV29ScoutSafePoint(scout,p))continue;
        const int risk=AIV28ScoutDangerAt(p);
        const int dist=scout->getDistance(p);
        const int focusDist=p.getDistance(focus);
        const int score=-(dist/4)-risk*2-(std::abs)(focusDist-160)/2;
        if(score>best){best=score;answer=p;}
    }
    return answer;
}

static BWAPI::Position AIV29NextUnexploredMain(BWAPI::Unit scout) {
    if(!g_v6EnemyStart.isValid())return BWAPI::Positions::None;
    auto& game=BWAPI::Broodwar;
    BWAPI::Position main(g_v6EnemyStart);
    // Sweep genuinely unexplored inner-main tiles, not meaningless fixed orbit.
    const int dx[13]={0,-160,160,0,0,-170,170,-170,170,-290,290,0,0};
    const int dy[13]={0,0,0,-160,160,-170,-170,170,170,0,0,-290,290};
    for(int i=0;i<13;++i) {
        BWAPI::Position p(main.x+dx[i],main.y+dy[i]);
        if(!AIV29ScoutSafePoint(scout,p))continue;
        if(!game->isExplored(BWAPI::TilePosition(p)))return p;
    }
    return BWAPI::Positions::None;
}

static void AIV29CountSightings() {
    auto& game=BWAPI::Broodwar;
    const int now=game->getFrameCount();
    for(auto player:game->enemies())for(auto u:player->getUnits()) {
        if(!u || !u->exists() || !u->isVisible())continue;
        if(!u->getType().isBuilding() && !u->getType().isWorker()) {
            if(g_v29ArmySeenOnce.insert(std::make_pair(u->getID(),u->getType().getID())).second)
                ++g_v29UnitSightings;
        }
        if(AIV28ProductionType(u->getType()))g_v29EnemyTechLastSeen=now;
    }
}

static BWAPI::Position AIV29ProductionPriority(BWAPI::Unit scout) {
    auto& game=BWAPI::Broodwar;
    int best=-100000000;
    BWAPI::Position target=BWAPI::Positions::None;
    g_v29ScoutFocusId=-1;
    const int now=game->getFrameCount();
    for(const auto& kv:g_v28KnownProduction) {
        const int id=kv.first;
        const BWAPI::Position p=kv.second;
        if(!p.isValid() || !AIV29ScoutSafePoint(scout,p)) {
            // Buildings are not walkable: check a nearby viewing location instead.
            if(!p.isValid())continue;
        }
        BWAPI::Position vantage=AIV29ScoutVantage(scout,p);
        if(!vantage.isValid())continue;
        int last=-100000;
        auto it=g_v29BuildingChecked.find(id);
        if(it!=g_v29BuildingChecked.end())last=it->second;
        const int age=(std::min)(2400,now-last);
        const int score=age/2-scout->getDistance(vantage)/3 - AIV28ScoutDangerAt(vantage);
        if(score>best){best=score;target=vantage;g_v29ScoutFocusId=id;}
    }
    return target;
}

static void AIV27ScoutController() {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if(!me || me->getRace()!=BWAPI::Races::Terran)return;
    AIV25ResetIfNewMatch();
    const int now=game->getFrameCount();
    if(now<g_v28PreviousScoutFrame){
        g_v27ScoutAttempts=0;g_v27MainSeenFrame=-1;g_v27Phase=0;
        g_v27MissionDone=false;g_v27Natural=BWAPI::Positions::None;
        g_v28KnownProduction.clear();g_v28ProductionSeen=0;
        g_v29BuildingChecked.clear();g_v29ArmySeenOnce.clear();
        g_v29UnitSightings=0;g_v29MainChecks=0;g_v29ScoutStart=-1;
        g_v29EnemyTechLastSeen=-1;
        g_v29ScoutFocusId=-1;g_v29ScoutDestination=BWAPI::Positions::None;
        g_v29ScoutLastDecision=-100000;g_v29ScoutLookFrame=-100000;
        g_v30LastScoutIssued=BWAPI::Positions::None;
        g_v30LastScoutCommand=-100000;g_v30DodgeCount=0;
    }
    g_v28PreviousScoutFrame=now;
    if(now%12)return;
    AIV25ObserveVisibleEnemies();
    AIV28UpdateScoutIntel();
    AIV29CountSightings();
    if(g_v27MissionDone || now<240)return;

    BWAPI::Unit scout=nullptr;
    for(auto u:me->getUnits())
        if(u && u->exists() && u->getType()==BWAPI::UnitTypes::Terran_SCV &&
           u->getID()==g_v5ScoutId){scout=u;break;}
    if(!scout){
        g_v5ScoutId=-1;
        if(g_v27ScoutAttempts>=1){g_v27MissionDone=true;return;}
        int scvs=0;
        for(auto u:me->getUnits())if(u && u->exists() &&
            u->getType()==BWAPI::UnitTypes::Terran_SCV)++scvs;
        if(scvs<11)return;
        const BWAPI::Position home(me->getStartLocation());
        for(auto u:me->getUnits()) {
            if(!u || !u->exists() || !u->isCompleted() ||
               u->getType()!=BWAPI::UnitTypes::Terran_SCV || u->isConstructing() ||
               u->isRepairing() || u->isGatheringGas() || u->isCarryingGas() ||
               u->getHitPoints()<53 || AIV353ReservedBuilder(u))continue;
            if(!scout || u->getDistance(home)<scout->getDistance(home))scout=u;
        }
        if(!scout)return;
        g_v5ScoutId=scout->getID();++g_v27ScoutAttempts;
        g_v29ScoutStart=now;
        game->printf("v29 recon: single SCV tasked with enemy tech observation");
    }
    const BWAPI::Position home(me->getStartLocation());
    // Knowing the enemy start tile isn't the same as reaching and inspecting it.
    if(g_v6EnemyStart.isValid() && g_v27MainSeenFrame<0 &&
       scout->getDistance(BWAPI::Position(g_v6EnemyStart))<550) {
        g_v27MainSeenFrame=now;g_v27Phase=1;
        g_v27Natural=AIV27NearestEnemyNatural(scout);
        g_v29ScoutLookFrame=now;
    }
    const int risk=AIV28ScoutDangerAt(scout->getPosition());
    // Threat proximity should prompt evasive micro, NOT immediately set the
    // entire intelligence mission to RETURN. Withdraw when badly hurt.
    if(scout->getHitPoints()<38 ||
       (scout->isUnderAttack() && scout->getHitPoints()<47) ||
       (g_v27MainSeenFrame>=0 && now-g_v27MainSeenFrame>1800)) {
        g_v27Phase=3;
    }
    if(risk>0 && g_v27Phase!=3) {
        // Test multiple escape positions: do not attack or run through a zealot.
        BWAPI::Position escape=AIV28SafeView(scout,
            scout->getPosition(),home,false);
        if(escape.isValid() && now-g_v30LastScoutCommand>=12 &&
           scout->getDistance(escape)>55) {
            if(scout->move(escape)) {
                g_v30LastScoutCommand=now;g_v30LastScoutIssued=escape;
                ++g_v30DodgeCount;
            }
        }
        // Attempt another approach when safe instead of permanently aborting.
        if(g_v27MainSeenFrame>=0 && now-g_v27MainSeenFrame>1080 &&
           g_v29MainChecks==0)g_v27Phase=3;
        return;
    }
    BWAPI::Position dest=BWAPI::Positions::None;
    if(g_v27Phase==0){
        dest=AIV25ChooseScoutDestination(scout);
    } else if(g_v27Phase==1){
        // First explore unseen tiles in the enemy main; then watch production.
        // Never mark the mission complete solely for having reached one point.
        // Once a real production building was found, watching it takes
        // priority over coloring unexplored ground tiles.
        BWAPI::Position unexplored=g_v28KnownProduction.empty()
             ? AIV29NextUnexploredMain(scout) : BWAPI::Positions::None;
        if(unexplored.isValid()) {
            dest=unexplored;
        } else {
            if(!g_v29ScoutDestination.isValid() ||
               now-g_v29ScoutLastDecision>=210 ||
               scout->getDistance(g_v29ScoutDestination)<65) {
                dest=AIV29ProductionPriority(scout);
                if(dest.isValid()){
                    g_v29ScoutDestination=dest;
                    g_v29ScoutLastDecision=now;
                    g_v29ScoutLookFrame=now;
                }
            }
            if(!dest.isValid())dest=g_v29ScoutDestination;
            if(!dest.isValid())dest=AIV29ScoutVantage(scout,BWAPI::Position(g_v6EnemyStart));
        }
        if(g_v29ScoutFocusId>=0 && g_v29ScoutDestination.isValid() &&
           scout->getDistance(g_v29ScoutDestination)<95 &&
           g_v28KnownProduction.count(g_v29ScoutFocusId)>0 &&
           game->isVisible(BWAPI::TilePosition(g_v28KnownProduction[g_v29ScoutFocusId])) &&
           now-g_v29BuildingChecked[g_v29ScoutFocusId]>=144) {
            g_v29BuildingChecked[g_v29ScoutFocusId]=now;
            ++g_v29MainChecks;
        }
        const int watched=now-g_v27MainSeenFrame;
        const bool techIdentified=!g_v28KnownProduction.empty() ||
                                  !g_v25SeenBuilding.empty();
        const bool observationDone=watched>=650 && techIdentified &&
                                   (g_v29MainChecks>=2 || watched>=1150);
        if(observationDone || watched>=1750) {
            g_v27Phase=g_v27Natural.isValid() && risk==0 ? 2:3;
            g_v29ScoutLastDecision=now;
            game->printf("v29 recon: tech=%d troop sightings=%d; checking expansion/return",
                         (int)g_v28KnownProduction.size(),g_v29UnitSightings);
        }
    } else if(g_v27Phase==2) {
        dest=g_v27Natural;
        if(!dest.isValid() || !scout->hasPath(dest) ||
           scout->getDistance(dest)<185 || now-g_v29ScoutLastDecision>420)
            g_v27Phase=3;
    }
    if(g_v27Phase==3) {
        dest=home;
        if(scout->getDistance(home)<210) {
            g_v27MissionDone=true;g_v5ScoutId=-1;
            game->printf("v29 recon: one SCV mission complete; no replacements");
            return;
        }
        if(risk>0) {
            BWAPI::Position escape=AIV28SafeView(scout,home,home,true);
            if(escape.isValid())dest=escape;
        }
    }
    // v29 BUG: it set g_v29ScoutDestination=dest before comparing dest to
    // itself. That prevented the actual move() command while SCV was mining.
    // Separate PLANNED destination from the last ISSUED order.
    if(dest.isValid() && scout->getDistance(dest)>65 &&
       now-g_v30LastScoutCommand>=24 &&
       (dest!=g_v30LastScoutIssued || scout->isIdle() ||
        now-g_v30LastScoutCommand>=288)) {
        if(scout->move(dest)) {
            g_v28LastOrder=now;
            g_v30LastScoutCommand=now;
            g_v30LastScoutIssued=dest;
            g_v29ScoutDestination=dest;
        }
    }
    game->drawTextScreen(BWAPI::Position(10,264),
        "v29 Scout phase%d tech%d troops%d checks%d hp%d",
        g_v27Phase,(int)g_v28KnownProduction.size(),g_v29UnitSightings,
        g_v29MainChecks,scout->getHitPoints());
}







static void AIV25IntelligenceTick() {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if(!me || me->getRace()!=BWAPI::Races::Terran)return;
    AIV27ScoutController(); // STARTCRAFT_AI_V27_SCOUT_HOOK
    AIV25MaintainRally();
    if(g_v25Rally.isValid() && g_v7State==AIV7_ASSEMBLE)
        game->drawCircleMap(g_v25Rally,50,BWAPI::Colors::Green,false);
    int rax=0,fac=0;
    AIV25AdaptiveGoals(AIV13OpponentRace(),AIV11Count().completedCC,rax,fac);
    game->drawTextScreen(BWAPI::Position(10,244),
       "v25 Scout:%s Enemy:%s Threat:%d RaxGoal:%d FacGoal:%d",
       g_v5ScoutId>=0?"ACTIVE":"RECRUIT",g_v6EnemyStart.isValid()?"FOUND":"SEARCH",
       AIV25VisibleEnemyArmy(g_v6EnemyStart.isValid()?BWAPI::Position(g_v6EnemyStart):
                             BWAPI::Position(me->getStartLocation()),550),rax,fac);
}


// STARTCRAFT_AI_V26_EARLY_DEFENSE
// Intercepts early pressure without prescribing a fixed anti-rush build.
// Uses visible information only; never assumes hidden enemy army positions.
static int g_v26LastFrame=-1;
static int g_v26LastBunkerAttempt=-10000;
static int g_v26LastScan=-10000;
static int g_v26LastWarning=-10000;
static int g_v26LastEnemyIntel=-10000;
static int g_v26LastStage=-10000;

// STARTCRAFT_AI_V53_GROUND_PATH_BUNKER: locate the traversable route leaving
// any defended Command Center. This extends the main-base path logic to a
// natural whose entrance is not aligned with a straight enemy vector.
static BWAPI::Position AIV53GroundPathPoint(BWAPI::Unit base,
                                            BWAPI::Position foe) {
    auto& game=BWAPI::Broodwar;
    if(!base || !base->exists() || !foe.isValid())return BWAPI::Positions::None;
    const int w=game->mapWidth(),h=game->mapHeight();
    if(w<=0 || h<=0 || w>512 || h>512)return BWAPI::Positions::None;
    auto walkable=[&](int x,int y)->bool {
        return x>0 && y>0 && x<w-1 && y<h-1 &&
            game->isWalkable(x*4+1,y*4+1) && game->isWalkable(x*4+2,y*4+2);
    };
    const auto baseTile=base->getTilePosition();
    const auto foeTile=BWAPI::TilePosition(foe);
    int start=-1,startScore=1000000;
    for(int r=2;r<=8 && start<0;++r)for(int dy=-r;dy<=r;++dy)
        for(int dx=-r;dx<=r;++dx) {
            if((std::max)(std::abs(dx),std::abs(dy))!=r)continue;
            const int x=baseTile.x+dx,y=baseTile.y+dy;
            if(!walkable(x,y))continue;
            const int score=std::abs(x-foeTile.x)+std::abs(y-foeTile.y);
            if(score<startScore){startScore=score;start=y*w+x;}
        }
    if(start<0)return BWAPI::Positions::None;
    const int size=w*h;
    std::vector<int> previous(size,-1),queue;
    previous[start]=start;queue.push_back(start);
    int goal=-1;
    const int stepX[4]={1,-1,0,0},stepY[4]={0,0,1,-1};
    for(size_t head=0;head<queue.size();++head) {
        const int at=queue[head],x=at%w,y=at/w;
        if(std::abs(x-foeTile.x)+std::abs(y-foeTile.y)<5){goal=at;break;}
        for(int direction=0;direction<4;++direction) {
            const int nx=x+stepX[direction],ny=y+stepY[direction];
            if(!walkable(nx,ny))continue;
            const int next=ny*w+nx;
            if(previous[next]>=0)continue;
            previous[next]=at;queue.push_back(next);
        }
    }
    if(goal<0)return BWAPI::Positions::None;
    std::vector<int> reversePath;
    for(int at=goal;at!=start;at=previous[at]) {
        reversePath.push_back(at);
        if(reversePath.size()>(size_t)size)return BWAPI::Positions::None;
    }
    reversePath.push_back(start);
    std::reverse(reversePath.begin(),reversePath.end());
    const int selected=reversePath[(std::min)(6,(int)reversePath.size()-1)];
    return BWAPI::Position((selected%w)*32+16,(selected/w)*32+16);
}

static BWAPI::TilePosition AIV53DefensiveBunkerTile(BWAPI::Unit base,
                                                   BWAPI::Position foe) {
    auto& game=BWAPI::Broodwar;
    if(!base || !base->exists())return BWAPI::TilePositions::None;
    if(!foe.isValid())foe=BWAPI::Position(game->mapWidth()*16,game->mapHeight()*16);
    const auto kind=BWAPI::UnitTypes::Terran_Bunker;
    const bool mainBase=base->getDistance(
        BWAPI::Position(game->self()->getStartLocation()))<400;
    // Anchor both the main and the expansion to a real walkable route. The
    // main keeps its choke-aware rally; a natural gets a local BFS projection.
    BWAPI::Position entrance=mainBase?AIV25PathRally():AIV53GroundPathPoint(base,foe);
    if(!entrance.isValid())entrance=base->getPosition();
    const auto origin=BWAPI::TilePosition(entrance);
    BWAPI::TilePosition best=BWAPI::TilePositions::None;
    int bestScore=100000000;
    const int firstRing=2,lastRing=14;
    for(int r=firstRing;r<=lastRing;++r)for(int dy=-r;dy<=r;++dy)for(int dx=-r;dx<=r;++dx) {
        if((std::max)(std::abs(dx),std::abs(dy))!=r)continue;
        BWAPI::TilePosition tile(origin.x+dx,origin.y+dy);
        if(!tile.isValid() || !game->canBuildHere(tile,kind,nullptr,true))continue;
        BWAPI::Position center(tile.x*32+kind.tileWidth()*16,
                               tile.y*32+kind.tileHeight()*16);
        // AIV11BuildNear protects the exit apron of every Barracks/Factory.
        // v47.1 did not apply the same rule while ranking bunker pads, so it
        // selected one impossible tile seven times. Reject it here and rank
        // the next legal defensive pad instead.
        bool productionApron=false;
        for(auto building:game->self()->getUnits())if(building && building->exists() &&
            (building->getType()==BWAPI::UnitTypes::Terran_Barracks ||
             building->getType()==BWAPI::UnitTypes::Terran_Factory)) {
            const auto ep=building->getTilePosition();const auto et=building->getType();
            const int left=ep.x-1,top=ep.y-1;
            const int right=ep.x+et.tileWidth()+
                            (et==BWAPI::UnitTypes::Terran_Factory?3:1);
            const int bottom=ep.y+et.tileHeight()+3;
            if(tile.x<right && tile.x+kind.tileWidth()>left &&
               tile.y<bottom && tile.y+kind.tileHeight()>top) {
                productionApron=true;break;
            }
        }
        if(productionApron)continue;
        bool resource=false;
        for(auto m:game->getMinerals())if(m && m->exists() && m->getDistance(center)<205)
            resource=true;
        for(auto g:game->getGeysers())if(g && g->exists() && g->getDistance(center)<225)
            resource=true;
        if(resource)continue;
        const int vx=entrance.x-base->getPosition().x;
        const int vy=entrance.y-base->getPosition().y;
        const int rx=center.x-entrance.x,ry=center.y-entrance.y;
        const int norm=(std::max)(1,(std::max)(std::abs(vx),std::abs(vy)));
        const int along=std::abs(rx*vx+ry*vy)/norm;
        const int side=std::abs(rx*vy-ry*vx)/norm;
        // Keep the bunker beside the ground route, with a wider fallback band
        // for irregular ramps. Never place it on the route itself.
        if(side<65 || side>220 || along>210)continue;
        int score=std::abs(side-112)*5+along*3+center.getDistance(foe)/30;
        if(!mainBase)score+=std::abs(center.getDistance(base->getPosition())-205)*2;
        if(score<bestScore){bestScore=score;best=tile;}
    }
    return best;
}

static void AIV26EarlyDefenseAndRecon() {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if (!me || me->getRace()!=BWAPI::Races::Terran) return;
    const int now=game->getFrameCount();
    if(now<g_v26LastFrame) {
        g_v26LastBunkerAttempt=-10000;
        g_v26LastScan=-10000;
        g_v26LastWarning=-10000;
        g_v26LastEnemyIntel=-10000;
        g_v26LastStage=-10000;
    }
    g_v26LastFrame=now;
    if(now%12) return;
    BWAPI::Unit cc=nullptr;
    BWAPI::Position home(me->getStartLocation());
    for(auto ally:me->getUnits()) {
        if(ally && ally->exists() && ally->isCompleted() &&
           ally->getType()==BWAPI::UnitTypes::Terran_Command_Center &&
           (!cc || ally->getDistance(home)<cc->getDistance(home))) cc=ally;
    }
    if(!cc)return;
    BWAPI::Unit main=cc,natural=nullptr;
    for(auto base:me->getUnits())if(base && base->exists() && !base->isFlying() &&
        base!=main && base->getType()==BWAPI::UnitTypes::Terran_Command_Center &&
        (!natural || base->getDistance(main)<natural->getDistance(main)))natural=base;
    // An unfinished expansion is already an investment that must be defended.
    // All early-defense distances, staging and bunker loading use it as soon as
    // its foundation appears instead of waiting for completion.
    BWAPI::Unit defenseCC=natural?natural:main;

    BWAPI::Position threatPos=BWAPI::Positions::None;
    int threatWeight=0, minThreatDistance=1000000;
    int visibleThreats=0;
    for(auto opponent:game->enemies()) {
        if(!opponent) continue;
        for(auto foe:opponent->getUnits()) {
            if(!foe || !foe->exists() || !foe->isVisible() ||
               foe->getType().isBuilding() || foe->getType().isWorker() ||
               foe->getType().groundWeapon()==BWAPI::WeaponTypes::None || foe->isFlying()) continue;
            int d=foe->getDistance(defenseCC);
            if(d>1100) continue;
            int power=2;
            if(foe->getType()==BWAPI::UnitTypes::Protoss_Zealot) power=3;
            if(foe->getType()==BWAPI::UnitTypes::Zerg_Zergling) power=1;
            threatWeight+=power;
            ++visibleThreats;
            if(d<minThreatDistance){minThreatDistance=d;threatPos=foe->getPosition();}
        }
    }
    int nearbyFighting=0, completedBunker=0, localBunker=0;
    BWAPI::Unit loadBunker=nullptr;
    for(auto ally:me->getUnits()) {
        if(!ally || !ally->exists())continue;
        auto t=ally->getType();
        if(t==BWAPI::UnitTypes::Terran_Bunker) {
            if(ally->getDistance(defenseCC)<700)++localBunker;
            if(ally->isCompleted() && ally->getDistance(defenseCC)<700) {
                ++completedBunker;loadBunker=ally;
            }
        }
        if(!ally->isCompleted() || ally->getDistance(defenseCC)>640)continue;
        if(t==BWAPI::UnitTypes::Terran_Marine) nearbyFighting+=2;
        else if(t==BWAPI::UnitTypes::Terran_Firebat) nearbyFighting+=3;
        else if(t==BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode ||
                t==BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)nearbyFighting+=5;
        else if(t==BWAPI::UnitTypes::Terran_Vulture)nearbyFighting+=3;
    }
    const bool exposed = threatPos.isValid() && visibleThreats>=3 &&
                          minThreatDistance<1050 && threatWeight*2>=nearbyFighting+3;
    const BWAPI::Race opponent=AIV13OpponentRace();
    // Both Zerg and Protoss can punish a naked natural before reactive
    // threat detection sees the wave. Prepare its bunker and Marine line as
    // soon as the CC foundation exists.
    const bool protectNatural=natural &&
        (opponent==BWAPI::Races::Zerg || opponent==BWAPI::Races::Protoss);
    // STARTCRAFT_AI_V48_TWO_GATE_BUNKER: a scouted two-Gateway opening is
    // enough evidence to prepare the main entrance before Zealots arrive.
    // v47.1 waited until the Factory builder was already under attack.
    // A single-gateway Protoss pressure killed v50's first Factory builder
    // before the scout could confirm two Gateways. Once the opening Barracks
    // is ready, prepare one entrance bunker on matchup and timing evidence.
    const bool protectMain=!natural && opponent==BWAPI::Races::Protoss &&
        (AIV25Seen(BWAPI::UnitTypes::Protoss_Gateway)>=1 || now>=2880);
    const bool proactiveDefense=protectNatural || protectMain;
    // Construct a bunker only for observed mass pressure, not for a fixed opening.
    // A TvZ natural is the exception: prepare its bunker before speedlings are
    // already hitting the construction SCV.
    if((exposed || proactiveDefense) && localBunker==0 && now-g_v26LastBunkerAttempt>240 &&
       me->minerals()>=100 && !AIV11PendingBuild(BWAPI::UnitTypes::Terran_Bunker)) {
        bool haveRax=false;
        for(auto ally:me->getUnits())
            if(ally && ally->exists() && ally->isCompleted() &&
               ally->getType()==BWAPI::UnitTypes::Terran_Barracks)haveRax=true;
        BWAPI::Position approach=threatPos;
        if(!approach.isValid())approach=AIV6AttackTarget();
        BWAPI::TilePosition bunkerTile=AIV53DefensiveBunkerTile(defenseCC,approach);
        if(haveRax && bunkerTile.isValid() &&
           AIV11BuildNear(BWAPI::UnitTypes::Terran_Bunker,bunkerTile)) {
            g_v26LastBunkerAttempt=now;
            game->printf("v53 DEFENSE: ground-path bunker ordered at %d,%d",
                         bunkerTile.x,bunkerTile.y);
        }
    }
    // Stage some home Marines at the defensive line before attackers arrive.
    // Never recall remote troops already attacking the enemy.
    if((exposed || proactiveDefense) && now-g_v26LastStage>=48 &&
       (!exposed || minThreatDistance>250)) {
        auto p=defenseCC->getPosition();
        BWAPI::Position approach=threatPos;
        if(!approach.isValid())approach=AIV6AttackTarget();
        if(!approach.isValid())approach=BWAPI::Position(game->mapWidth()*16,game->mapHeight()*16);
        int vx=approach.x-p.x,vy=approach.y-p.y;
        int norm=(std::max)(1,(std::abs)(vx)+(std::abs)(vy));
        BWAPI::Position stage=(!natural && protectMain)
            ?AIV25PathRally():BWAPI::Position(p.x+vx*145/norm,p.y+vy*145/norm);
        if(!stage.isValid())stage=BWAPI::Position(p.x+vx*145/norm,p.y+vy*145/norm);
        const int stageTarget=AIV25Seen(BWAPI::UnitTypes::Protoss_Gateway)>=2?7:6;
        int staged=0;
        if(stage.isValid() && game->isWalkable(stage.x/8,stage.y/8)) {
          for(auto ally:me->getUnits())
              if(ally && ally->exists() && ally->isCompleted() &&
                 ally->getType()==BWAPI::UnitTypes::Terran_Marine &&
                 ally->getDistance(stage)<115)++staged;
          for(auto ally:me->getUnits()) {
            if(staged>=stageTarget)break;
            if(!ally || !ally->exists() || !ally->isCompleted() ||
               ally->getType()!=BWAPI::UnitTypes::Terran_Marine ||
               g_v32GuardIds.count(ally->getID()) ||
               ally->isLoaded() || ally->isAttackFrame() ||
               (g_v7State==AIV7_ATTACK && g_v7Army.count(ally->getID())) ||
               ally->getDistance(defenseCC)>2000 || !ally->hasPath(stage) ||
               ally->getDistance(stage)<90) continue;
            if(ally->move(stage))++staged;
          }
        }
        g_v26LastStage=now;
    }
    // Put Marines inside an already built bunker while the wave approaches.
    if((proactiveDefense || (threatPos.isValid() && minThreatDistance<900)) && loadBunker &&
       loadBunker->getLoadedUnits().size()<4 && now%48==0 && now>g_v57NoLoadUntil) {
        int ordered=0;
        for(auto ally:me->getUnits()) {
            if(!ally || !ally->exists() || !ally->isCompleted() ||
               ally->getType()!=BWAPI::UnitTypes::Terran_Marine ||
               ally->isLoaded() || ally->getDistance(loadBunker)>1800 ||
               !ally->hasPath(loadBunker->getPosition()) ||
               ally->isAttackFrame()) continue;
            if(loadBunker->load(ally))++ordered;
            if(ordered>=2)break;
        }
    }
    if(exposed && now-g_v26LastWarning>240) {
        game->printf("v26 EARLY PRESSURE: enemy %d / power %d / home defense %d",
                     visibleThreats,threatWeight,nearbyFighting);
        g_v26LastWarning=now;
    }

    // Recon scans: reserved for a *known*, currently fogged enemy base; never
    // spend the last 50 Comsat energy needed for emergency cloak detection.
    if(g_v6EnemyStart.isValid() && now>2200 && now-g_v26LastScan>1440 &&
       now-g_v15LastScan>480 && !exposed && !AIV6HomeThreat().isValid()) {
        BWAPI::TilePosition enemyBase=g_v6EnemyStart;
        if(!game->isVisible(enemyBase)) {
            // Avoid scanning a location known to be empty and erased from memory.
            const bool recentIntel = !g_v25SeenBuilding.empty() || !g_v25SeenArmy.empty();
            if(recentIntel)for(auto comsat:me->getUnits()) {
                if(!comsat || !comsat->exists() || !comsat->isCompleted() ||
                   comsat->getType()!=BWAPI::UnitTypes::Terran_Comsat_Station ||
                   comsat->getEnergy()<150)continue;
                if(comsat->useTech(BWAPI::TechTypes::Scanner_Sweep,
                                   BWAPI::Position(enemyBase))) {
                    g_v26LastScan=now;
                    g_v15LastScan=now;
                    game->printf("v26 RECON SCAN: enemy base recheck");
                    break;
                }
            }
        }
    }
}


static void AIV22MatchupGoals(BWAPI::Race enemy,int bases,int& rax,int& fac) {
    // STARTCRAFT_AI_V25_GOALS: actual scouting intelligence drives building caps.
    AIV25AdaptiveGoals(enemy,bases,rax,fac);
}

static bool AIV22ScaleProduction(AIV11Counts c,BWAPI::Race enemy) {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if(!me)return false;
    int targetRax=2,targetFac=2;
    AIV25AdaptiveGoals(enemy,c.completedCC,targetRax,targetFac); // STARTCRAFT_AI_V29_SCALING
    const int now=game->getFrameCount();
    // Protect the v26 opening and first coordinated push. Expand strategic
    // production after the army is functional, the natural is ready, or the
    // match has moved beyond the initial opening.
    const bool openingReady=c.completedCC>=2 ||
                            (enemy==BWAPI::Races::Protoss && c.factory>=1) ||
                            g_v7State==AIV7_ATTACK ||
                            game->elapsedTime()>=420 || (g_v29TerranMech && c.scv>=22 && c.tank>=1);
    if(c.scv<18 || c.barracks==0 || !openingReady){
        g_v28GasReserve=0;g_v28MilitaryMineralReserve=0;return false;
    }
    // STARTCRAFT_AI_V30_EXPANSION_PRIORITY: v29 constantly bought military
    // buildings before 400 minerals could be accumulated for the natural.
    // Don't steal the CC fund. Normal v13 production remains behind the reserve.
    if(!(enemy==BWAPI::Races::Protoss && c.factory<2) &&
       c.cc==1 && c.scv>=22 && c.tank>=1 &&
       !AIV6HomeThreat().isValid() && now>=g_v11NoExpansionUntil &&
       me->minerals()<400){
        g_v28GasReserve=0;
        g_v28MilitaryMineralReserve=0;
        return false;
    }
    const bool saveAllowed=!AIV6HomeThreat().isValid() && now>=g_v28ScalePausedUntil;
    const bool needRax=c.barracks<targetRax && c.scv>=18 &&
                       !AIV11PendingBuild(BWAPI::UnitTypes::Terran_Barracks);
    const bool needFac=c.factory>0 && c.factory<targetFac && c.scv>=21 &&
                       !AIV11PendingBuild(BWAPI::UnitTypes::Terran_Factory);
    const bool preferFac=targetFac>targetRax; // Real observed tech matters, even vs Random.
    // Save enough for ONE next production structure. AIV11Produce otherwise
    // endlessly spends incoming minerals/gas on marines/tanks before scale-up.
    const bool missingChosen=preferFac?(needFac || needRax):(needRax || needFac);
    if(saveAllowed && missingChosen){
        if(preferFac && needFac){
            g_v28MilitaryMineralReserve=200;
            g_v28GasReserve=100;
        }else if(needRax){
            g_v28MilitaryMineralReserve=150;
            g_v28GasReserve=0;
        }else if(needFac){
            g_v28MilitaryMineralReserve=200;
            g_v28GasReserve=100;
        }
    }
    // Building commands have their own resource availability guards and do
    // not require 80-140 minerals ABOVE the actual building price.
    const bool canRax=needRax && me->minerals()>=150;
    const bool canFac=needFac && me->minerals()>=200 && me->gas()>=100;
    bool attempted=false;
    bool built=false;
    if(preferFac){
        // If the plan needs a Factory, keep the 200M/100G reserve intact.
        // v49 spent 150M on a fallback Barracks at 163M and stayed on one
        // Factory despite holding almost 400 gas.
        if(needFac) {
            if(canFac){attempted=true;built=AIV11BuildNear(BWAPI::UnitTypes::Terran_Factory);}
        } else if(canRax) {
            attempted=true;built=AIV11BuildNear(BWAPI::UnitTypes::Terran_Barracks);
        }
    }else{
        if(canRax){attempted=true;built=AIV11BuildNear(BWAPI::UnitTypes::Terran_Barracks);}
        if(!built && canFac){attempted=true;built=AIV11BuildNear(BWAPI::UnitTypes::Terran_Factory);}
    }
    if(built){
        g_v28ScaleFailures=0;g_v28GasReserve=0;g_v28MilitaryMineralReserve=0;
        if(now-g_v28LastScaleMessage>72){
            game->printf("v28 BUILD %s: Rax %d/%d Factory %d/%d",
                         g_v28PlanLabel,c.barracks,targetRax,c.factory,targetFac);
            g_v28LastScaleMessage=now;
        }
    }else if(attempted && now-g_v28LastScaleMessage>=180){
        ++g_v28ScaleFailures;g_v28LastScaleMessage=now;
        game->printf("v28 BUILD BLOCKED: %s Rax %d/%d Fac %d/%d (check placement)",
                     g_v28PlanLabel,c.barracks,targetRax,c.factory,targetFac);
        // Don't indefinitely starve combat production if no legal pad exists.
        if(g_v28ScaleFailures>=4){
            g_v28ScalePausedUntil=now+480;
            g_v28ScaleFailures=0;g_v28GasReserve=0;g_v28MilitaryMineralReserve=0;
            g_v11Reserve=(std::min)(g_v11Reserve,75);
        }
    }
    if(now%72==0)game->drawTextScreen(BWAPI::Position(10,170),
        "v28 %s Rax %d/%d Fac %d/%d Reserve M%d G%d",
        g_v28PlanLabel,c.barracks,targetRax,c.factory,targetFac,
        g_v28MilitaryMineralReserve,g_v28GasReserve);
    return built;
}


static int g_v22LastAcademyOrder = -99999;
static int g_v22PreviousFrame = -1;
static void AIV22AcademyDirector() {
    auto& game = BWAPI::Broodwar;
    auto me = game->self();
    if (!me || me->getRace() != BWAPI::Races::Terran) return;
    const int now = game->getFrameCount();
    if (now < g_v22PreviousFrame) g_v22LastAcademyOrder = -99999;
    g_v22PreviousFrame = now;

    const AIV11Counts c = AIV11Count();
    const BWAPI::Race opponent = AIV13OpponentRace();
    const bool bioFocus = opponent == BWAPI::Races::Zerg;
    int targetRax = 0, targetFac = 0;
    AIV22MatchupGoals(opponent, c.completedCC, targetRax, targetFac);
    // HUD keeps the actual vs desired ratios visible when money is short.
    const auto stim = BWAPI::TechTypes::Stim_Packs;
    const auto range = BWAPI::UpgradeTypes::U_238_Shells;
    game->drawTextScreen(BWAPI::Position(10,190),
        "v22 %s Rax%d/%d Fac%d/%d Stim%d Range%d",
        AIV13MatchupLabel(opponent), c.barracks, targetRax, c.factory, targetFac,
        me->hasResearched(stim) ? 1 : 0, me->getUpgradeLevel(range));

    if (now % 24 || now - g_v22LastAcademyOrder < 144) return;

    // Combat units need support, but researching a tech should not steal the
    // last minerals from a badly needed supply depot.
    const int mineralBuffer = bioFocus ? 65 : 100;
    for (auto academy : me->getUnits()) {
        if (!academy || !academy->exists() || !academy->isCompleted() ||
            academy->getType() != BWAPI::UnitTypes::Terran_Academy ||
            academy->isResearching() || academy->isUpgrading()) continue;

        if (c.marine >= (bioFocus ? 5 : 10) && !me->hasResearched(stim) &&
            !me->isResearching(stim) &&
            me->minerals() >= stim.mineralPrice() + mineralBuffer &&
            me->gas() >= stim.gasPrice() && game->canResearch(stim,academy)) {
            if (academy->research(stim)) {
                g_v22LastAcademyOrder = now;
                game->printf("v22 ACADEMY: researching Stim Packs");
                return;
            }
        }
        if (c.marine >= (bioFocus ? 8 : 14) &&
            me->getUpgradeLevel(range) == 0 && !me->isUpgrading(range) &&
            me->minerals() >= range.mineralPrice(1) + mineralBuffer &&
            me->gas() >= range.gasPrice(1) && game->canUpgrade(range,academy)) {
            if (academy->upgrade(range)) {
                g_v22LastAcademyOrder = now;
                game->printf("v22 ACADEMY: upgrading U-238 Shells (Marine range)");
                return;
            }
        }
    }
}


void AIV13EconomyDirector() {
    auto& game = BWAPI::Broodwar;
    auto me = game->self();
    if (!me || me->getRace() != BWAPI::Races::Terran) return;
    AIV11Reset();
    g_v28GasReserve=0; g_v28MilitaryMineralReserve=0; // STARTCRAFT_AI_V28_RESET
    const int frame = game->getFrameCount();
    AIV11Counts c = AIV11Count();
    const BWAPI::Race enemy = AIV13OpponentRace();
    const bool versusZerg = (enemy == BWAPI::Races::Zerg);
    const bool versusTerran = (enemy == BWAPI::Races::Terran);
    const bool versusProtoss = (enemy == BWAPI::Races::Protoss);
    const bool enemyTerranAir = AIV25Seen(BWAPI::UnitTypes::Terran_Starport)>0;
    const bool terranAcademyReady = !versusTerran || enemyTerranAir ||
        (c.completedCC>=2 && c.factory>=2 && c.scv>=24);
    const int free = me->supplyTotal() - me->supplyUsed(); // BWAPI half-supply units

    game->drawTextScreen(BWAPI::Position(10,126),
        "AI v13 %s M%d G%d SCV%d CC%d Rax%d Fac%d supply%d/%d",
        AIV13MatchupLabel(enemy),me->minerals(),me->gas(),c.scv,
        c.cc,c.barracks,c.factory,me->supplyUsed()/2,me->supplyTotal()/2);

    // Normal economy/training continues while buildings are being planned.
    if (frame % 12) return;
    const bool underAttack = AIV6HomeThreat().isValid();
    // STARTCRAFT_AI_V37_3_ENEMY_EXPAND_RESPONSE: the intelligence ledger is
    // based only on buildings the scout has actually seen.
    int enemyDepots=0;
    for(const auto& item:g_v25SeenBuilding) {
        const int id=item.second.first;
        if(id==BWAPI::UnitTypes::Terran_Command_Center.getID() ||
           id==BWAPI::UnitTypes::Protoss_Nexus.getID() ||
           id==BWAPI::UnitTypes::Zerg_Hatchery.getID() ||
           id==BWAPI::UnitTypes::Zerg_Lair.getID() ||
           id==BWAPI::UnitTypes::Zerg_Hive.getID())++enemyDepots;
    }
    const bool enemyExpandedEarly=enemyDepots>=2 && frame<11520;
    // STARTCRAFT_AI_V46_TVZ_TWO_RAX_EXPAND: commit to one coherent opening.
    // Two Barracks provide real Zergling safety, then the 15-SCV natural is
    // funded before gas/Academy/Factory spending.
    const bool fastZergExpand=versusZerg && c.scv>=15 && c.barracks>=2;
    // A scouted two-Gateway opening invalidates an undefended fast natural.
    // Require the entrance bunker plus a small mobile core before sending a
    // fourth 400-mineral investment into the same attack path.
    bool completedBunker=false;
    int vultures=0;
    for(auto u:me->getUnits())if(u && u->exists()) {
        if(u->getType()==BWAPI::UnitTypes::Terran_Bunker && u->isCompleted())
            completedBunker=true;
        if(u->getType()==BWAPI::UnitTypes::Terran_Vulture && u->isCompleted())
            ++vultures;
    }
    // STARTCRAFT_AI_V55_PREPARED_EXPAND: unit counts in the main are not a
    // defense. Against Protoss, wait until two Tanks are physically posted and
    // sieged at the selected natural approach and no ground wave is present.
    const bool protossNaturalSafe=!versusProtoss ||
        (completedBunker && g_v55NaturalPrepared && c.tank>=2 &&
         (c.marine>=6 || vultures>=2));
    const bool safeFactoryExpand=!versusZerg && c.scv>=20 && c.factory>=1 &&
                                 c.barracks>=1 && protossNaturalSafe;
    const bool firstExpansion=c.cc==1 &&
        (fastZergExpand || safeFactoryExpand ||
         (enemyExpandedEarly && c.scv>=18 && c.barracks>=2));
    const bool thirdBase=c.cc==2 && c.completedCC==2 && c.scv>=32 &&
                         c.barracks>=3 && c.factory>=1 && me->minerals()>=500;
    const bool fourthBase=c.cc==3 && c.completedCC==3 && c.scv>=44 &&
                          c.barracks>=4 && c.factory>=2 && me->minerals()>=600;
    const bool tryExpand = (firstExpansion || thirdBase || fourthBase) &&
                           !underAttack && frame >= g_v11NoExpansionUntil;
    // Do NOT freeze marines/tanks for a 400 mineral expansion.
    g_v11Reserve = tryExpand ? 350 : 0; // STARTCRAFT_AI_V46_CC_RESERVE

    bool built = false;
    if (frame - g_v11LastBuildOrder >= 96) {
        // CRITICAL: no more than one pending supply depot. Use the proven v11
        // detection of in-flight build orders, including incomplete depots.
        const bool depotPending = c.unfinishedDepot > 0 ||
            AIV11PendingBuild(BWAPI::UnitTypes::Terran_Supply_Depot);

        // Emergency supply gets precedence only when almost blocked.
        if (!depotPending && free <= 4)
            built = AIV11Supply(c);

        // Both TvT and TvP replays heavily favored Rax -> gas -> Factory.
        // The TvZ collection had mixed bio/mech openings; prioritize Academy
        // once infrastructure and mineral income can support it.
        if (!built && c.barracks == 0 && c.scv >= 11)
            built = AIV11BuildNear(BWAPI::UnitTypes::Terran_Barracks);
        // The second opening Barracks is a build-order step, not late-game
        // scaling.  v45 otherwise waited until after the natural completed.
        if (!built && versusZerg && c.cc==1 && c.barracks==1 && c.scv>=13 &&
            me->minerals()>=150)
            built = AIV11BuildNear(BWAPI::UnitTypes::Terran_Barracks);
        // TvP professional median gas timing is about 1:39.  Waiting for 14
        // SCVs produced the first gas at 2:06 and the Factory at 3:00.
        if (!built && c.refinery < c.completedCC && c.barracks > 0 && c.scv >= 10 &&
            (!versusZerg || c.cc>=2))
            built = AIV11Refinery(c);
        if (!built && versusZerg && c.cc>=2 && c.academy == 0 && c.barracks > 0 &&
            c.scv >= 17 && me->minerals() >= 200)
            built = AIV11BuildNear(BWAPI::UnitTypes::Terran_Academy);
        if (!built && c.factory == 0 && c.barracks > 0 && c.scv >= 14 &&
            (!versusZerg || (c.completedCC>=2 && c.barracks>=3)) &&
            me->gas() >= 100 && me->minerals() >= 200)
            built = AIV11BuildNear(BWAPI::UnitTypes::Terran_Factory);
        // TvP from the supplied professional sample reaches Factory and the
        // natural before optional bio tech.  The Pluto loss spent 150 minerals
        // on an Academy while the first tank was still minutes away.
        if (!built && !versusProtoss && c.academy == 0 && c.barracks > 0 && c.factory > 0 &&
            terranAcademyReady && c.scv >= 18 && me->minerals() >= 200)
            built = AIV11BuildNear(BWAPI::UnitTypes::Terran_Academy);

        // Expansion is attempted when affordable; if no valid site exists,
        // never hold the army/production hostage for expansion money.
        if (!built && tryExpand && me->minerals() >= 400)
            built = AIV16Expand(c); // STARTCRAFT_AI_V16

        // STARTCRAFT_AI_V22: matchup-specific production targets instead of
        // the old fixed two-barracks/two-factory behavior.
        if (!built) built = AIV22ScaleProduction(c,enemy);

        // Supply pre-build when available slots fall below production capacity.
        // This call is guarded against duplicate orders by AIV11Supply().
        // While the first natural is funded, do not buy a comfortable extra
        // depot.  Emergency supply above already remains authoritative.
        if (!built && !depotPending && (!tryExpand || free <= 8))
            built = AIV11Supply(c);
    }
    AIV11Produce(c);
    AIV11BalanceGas();
}


} // STARTCRAFT_AI_V11


StarterBot::StarterBot()
{
    
}

// Called when the bot starts!
void StarterBot::onStart()
{
    g_v353Tasks.clear();g_v353WatchPreviousFrame=-1;
    AIV353Log("START",BWAPI::UnitTypes::Terran_SCV,nullptr,BWAPI::TilePositions::None);
    BWAPI::Broodwar->printf("AI v58: hold screen and one barracks mech");
    // Set our BWAPI options here    
	BWAPI::Broodwar->setLocalSpeed(10);
    BWAPI::Broodwar->setFrameSkip(0);
    
    // Enable the flag that tells BWAPI top let users enter input while bot plays
    BWAPI::Broodwar->enableFlag(BWAPI::Flag::UserInput);

    // Call MapTools OnStart
    m_mapTools.onStart();
}

// Called on each frame of the game
// STARTCRAFT_AI_V18: read-only dashboard and 10 game-second telemetry logger.
// Does not issue any unit orders or change speed, production or pathing.
static int g_aiV18LastObservedFrame = -1;
static int g_aiV18LastWrittenFrame = -999999;
static long long g_aiV18Session = 0;
static int g_aiV18LastBuildingsFrame = -999999;

static void AIV18Dashboard()
{
    auto& game = BWAPI::Broodwar; // non-copyable GameWrapper
    BWAPI::Player me = game->self();
    if (!me || me->getRace() != BWAPI::Races::Terran) return;

    const int frame = game->getFrameCount();
    if (g_aiV18LastObservedFrame < 0 || frame < g_aiV18LastObservedFrame) {
        g_aiV18Session = static_cast<long long>(std::time(nullptr));
        g_aiV18LastWrittenFrame = -999999;
        g_aiV18LastBuildingsFrame = -999999;
    }
    g_aiV18LastObservedFrame = frame;
    const int gameSeconds = frame / 24; // 24 logical Brood War frames per game second
    const int apm = game->getAPM(false); // excludes selection-only actions

    int scv = 0, cc = 0, barracks = 0, factory = 0;
    int marine = 0, medic = 0, tank = 0, supplyDepots = 0;
    int idleScvs = 0, idleBarracks = 0, idleFactories = 0;
    for (auto u : me->getUnits()) {
        if (!u || !u->exists()) continue;
        const BWAPI::UnitType type = u->getType();
        if (type == BWAPI::UnitTypes::Terran_SCV) {
            ++scv;
            if (u->isIdle()) ++idleScvs;
        } else if (type == BWAPI::UnitTypes::Terran_Command_Center) {
            ++cc;
        } else if (type == BWAPI::UnitTypes::Terran_Barracks) {
            ++barracks;
            if (u->isCompleted() && !u->isFlying() && !u->isTraining()) ++idleBarracks;
        } else if (type == BWAPI::UnitTypes::Terran_Factory) {
            ++factory;
            if (u->isCompleted() && !u->isFlying() && !u->isTraining()) ++idleFactories;
        } else if (type == BWAPI::UnitTypes::Terran_Marine) {
            ++marine;
        } else if (type == BWAPI::UnitTypes::Terran_Medic) {
            ++medic;
        } else if (type == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode ||
                   type == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode) {
            ++tank;
        } else if (type == BWAPI::UnitTypes::Terran_Supply_Depot) {
            ++supplyDepots;
        }
    }

    const char* state = "ASSEMBLE";
    if (g_v7State == AIV7_ATTACK) state = "ATTACK";
    else if (g_v7State == AIV7_REGROUP) state = "REGROUP";

    // Small heads-up panel in the upper right; existing v7/v13/v16/v17 text
    // stays on the left. Text is redrawn each onFrame, not only once per second.
    game->drawTextScreen(398, 12, "AI v18 | %02d:%02d | APM %d",
                         gameSeconds / 60, gameSeconds % 60, apm);
    game->drawTextScreen(398, 26, "M %d   G %d   Supply %d/%d",
                         me->minerals(), me->gas(), me->supplyUsed()/2, me->supplyTotal()/2);
    game->drawTextScreen(398, 40, "SCV %d  CC %d  Rax %d  Fac %d",
                         scv, cc, barracks, factory);
    game->drawTextScreen(398, 54, "Marine %d  Medic %d  Tank %d",
                         marine, medic, tank);
    game->drawTextScreen(398, 68, "Idle SCV %d  Rax %d  Fac %d",
                         idleScvs, idleBarracks, idleFactories);
    game->drawTextScreen(398, 82, "MODE: %s   Depots: %d", state, supplyDepots);

    // Every 30 logical game seconds record ACTUAL building tile locations.
    // This lets the next version examine bad CC expansion sites and misplaced
    // Barracks/Depots from factual coordinates instead of guessing from screenshots.
    if (frame - g_aiV18LastBuildingsFrame >= 720) {
        g_aiV18LastBuildingsFrame = frame;
        std::FILE* b = std::fopen("AI_v18_buildings.csv", "a+");
        if (b) {
            std::fseek(b, 0, SEEK_END);
            if (std::ftell(b) == 0)
                std::fprintf(b, "session,game_seconds,unit_id,building,tile_x,tile_y,"
                                "completed,flying,start_x,start_y\n");
            const BWAPI::TilePosition start = me->getStartLocation();
            for (auto u : me->getUnits()) {
                if (!u || !u->exists() || !u->getType().isBuilding()) continue;
                const BWAPI::TilePosition tile = u->getTilePosition();
                std::fprintf(b, "%lld,%d,%d,%s,%d,%d,%d,%d,%d,%d\n",
                             g_aiV18Session, gameSeconds, u->getID(),
                             u->getType().getName().c_str(), tile.x, tile.y,
                             u->isCompleted()?1:0, u->isFlying()?1:0,
                             start.x, start.y);
            }
            std::fclose(b);
        }
    }

    // Every ten logical game seconds save diagnostics next to StarterBot.exe.
    // Telemetry is strictly observational and does not modify bot decisions.
    if (frame - g_aiV18LastWrittenFrame < 240) return;
    g_aiV18LastWrittenFrame = frame;
    std::FILE* log = std::fopen("AI_v18_telemetry.csv", "a+");
    if (!log) return; // disk permissions must not interrupt a match
    std::fseek(log, 0, SEEK_END);
    if (std::ftell(log) == 0) {
        std::fprintf(log, "session,game_seconds,apm,minerals,gas,supply_used,supply_total,"
                           "scv,command_centers,barracks,factories,supply_depots,marines,medics,tanks,"
                           "idle_scvs,idle_barracks,idle_factories,army_state\n");
    }
    std::fprintf(log, "%lld,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%s\n",
                 g_aiV18Session, gameSeconds, apm, me->minerals(), me->gas(),
                 me->supplyUsed()/2, me->supplyTotal()/2,
                 scv, cc, barracks, factory, supplyDepots, marine, medic, tank,
                 idleScvs, idleBarracks, idleFactories, state);
    std::fclose(log);
}


// STARTCRAFT_AI_V19_DIAGNOSTICS
// Minimal logging; no pathfinding, extra build orders, or army micro.
static int g_aiV19LastLogFrame = -999999;
static void AIV19Diagnostics() {
    auto& game = BWAPI::Broodwar;
    auto me = game->self();
    if (!me || me->getRace() != BWAPI::Races::Terran) return;
    const int frame = game->getFrameCount();
    if (frame < g_aiV19LastLogFrame) g_aiV19LastLogFrame = -999999;
    if (frame - g_aiV19LastLogFrame < 720) return;
    g_aiV19LastLogFrame = frame;

    AIV11Counts c = AIV11Count();
    const bool threatened = AIV6HomeThreat().isValid();
    const bool expansionReady = c.cc == 1 && c.scv >= 20 &&
        c.factory >= 1 && c.barracks >= 1 && !threatened &&
        frame >= g_v11NoExpansionUntil;
    int gasWorkers = 0;
    for (auto u : me->getUnits()) {
        if (u && u->exists() && u->getType() == BWAPI::UnitTypes::Terran_SCV &&
            u->isGatheringGas()) ++gasWorkers;
    }
    int wantedGas = 3;
    if (me->gas() >= 350 && me->gas() >= me->minerals() + 150) wantedGas = 1;
    else if (me->gas() >= 220 && me->gas() >= me->minerals() + 80) wantedGas = 2;
    std::string map = game->mapName();
    for (auto& ch : map) if (ch == ',' || ch == '\n' || ch == '\r') ch = ' ';

    std::FILE* log = std::fopen("AI_v19_decisions.csv", "a+");
    if (!log) return;
    std::fseek(log, 0, SEEK_END);
    if (std::ftell(log) == 0) {
        std::fprintf(log, "session,game_seconds,map,minerals,gas,scv,cc,"
                          "gas_workers,gas_target,supply_free,unfinished_supply,"
                          "expansion_eligible,expand_wait_until_frame,"
                          "save_for_cc_minerals,home_threat,barracks,factories\n");
    }
    std::fprintf(log, "%lld,%d,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
                 g_aiV18Session, frame / 24, map.c_str(),
                 me->minerals(), me->gas(), c.scv, c.cc, gasWorkers, wantedGas,
                 (me->supplyTotal() - me->supplyUsed()) / 2, c.unfinishedDepot,
                 expansionReady ? 1 : 0, g_v11NoExpansionUntil,
                 g_v11Reserve, threatened ? 1 : 0, c.barracks, c.factory);
    std::fclose(log);
}


// STARTCRAFT_AI_V23_REACTIVE_DEFENSE
// Reactive defense, independent of opening/build order. Invoked after the
// existing directors, so local defense takes priority over routine rally.
// Only pulls workers if nearby fighting strength is insufficient, and
// restores mining when the immediate threat has passed.
static std::set<int> g_v23PulledWorkers;
static std::map<int,int> g_v23LastMicro;
static int g_v23PreviousFrame = -1;
static int g_v23ThreatLastSeen = -99999;
static int g_v23LastMessage = -99999;

static bool AIV23VisibleThreat(BWAPI::Unit enemy) {
    if (!enemy || !enemy->exists() || !enemy->isVisible() ||
        !enemy->isDetected() || enemy->getType().isBuilding()) return false;
    // Enemy workers scouting our mineral line should not trigger a pull;
    // workers actually fighting are considered a threat.
    if (enemy->getType().isWorker()) return enemy->isAttacking();
    return enemy->getType().groundWeapon() != BWAPI::WeaponTypes::None;
}

static int AIV23ThreatWeight(BWAPI::Unit enemy) {
    if (enemy->getType().isWorker() ||
        enemy->getType()==BWAPI::UnitTypes::Zerg_Zergling) return 1;
    if (enemy->getType()==BWAPI::UnitTypes::Terran_Marine ||
        enemy->getType()==BWAPI::UnitTypes::Zerg_Hydralisk) return 2;
    return 3;
}

static BWAPI::Unit AIV23ClosestThreat(BWAPI::Unit unit,
                                      const std::vector<BWAPI::Unit>& threats) {
    BWAPI::Unit selected=nullptr;
    int best=100000000;
    for (auto enemy:threats) {
        // Prioritize an enemy which can be attacked by this ground unit.
        if (enemy->isFlying()) continue;
        const int score=unit->getDistance(enemy)*2 + enemy->getHitPoints();
        if (score<best) {best=score;selected=enemy;}
    }
    return selected;
}

static BWAPI::Unit AIV23NearestMineral(BWAPI::Unit worker) {
    auto& game=BWAPI::Broodwar;
    BWAPI::Unit selected=nullptr;
    int best=100000000;
    for (auto mineral:game->getMinerals()) {
        if (!mineral || !mineral->exists() || !mineral->getType().isMineralField()) continue;
        int d=worker->getDistance(mineral);
        // Do not accidentally assign released SCVs to a faraway enemy base.
        if (d<best && d<600) {best=d;selected=mineral;}
    }
    return selected;
}

static void AIV23ReactiveDefense() {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if (!me || me->getRace()!=BWAPI::Races::Terran) return;
    const int frame=game->getFrameCount();
    if (frame<g_v23PreviousFrame) {
        g_v57NoLoadUntil=-1;
        g_v23PulledWorkers.clear();
        g_v23LastMicro.clear();
        g_v23ThreatLastSeen=-99999;
        g_v23LastMessage=-99999;
    }
    g_v23PreviousFrame=frame;
    // Check eight times a game-second at most, not every rendered frame.
    if (frame%8!=0) return;

    // Defend each Command Center independently, including an unfinished
    // natural.  v46 ignored the expansion until completion, so the builder and
    // foundation could be killed without triggering local SCV/army defense.
    std::vector<BWAPI::Unit> commandCenters;
    for (auto u:me->getUnits())
        if (u && u->exists() && !u->isFlying() &&
            u->getType()==BWAPI::UnitTypes::Terran_Command_Center)
            commandCenters.push_back(u);
    if (commandCenters.empty()) return;

    BWAPI::Unit threatenedCC=nullptr;
    std::vector<BWAPI::Unit> localThreats;
    int largestThreatScore=0;
    int localGroundThreatScore=0;
    for (auto cc:commandCenters) {
        int score=0;
        std::vector<BWAPI::Unit> enemies;
        for (auto enemyPlayer:game->enemies()) {
            if (!enemyPlayer) continue;
            for (auto enemy:enemyPlayer->getUnits()) {
                if (!AIV23VisibleThreat(enemy) || enemy->getDistance(cc)>900) continue;
                enemies.push_back(enemy);
                score+=AIV23ThreatWeight(enemy);
            }
        }
        if (score>largestThreatScore) {
            largestThreatScore=score;
            threatenedCC=cc;
            localThreats=enemies;
        }
    }

    if (!threatenedCC) {
        // A few seconds of hysteresis prevent oscillating between mining and
        // attacking each time a zergling disappears behind the fog of war.
        if (frame-g_v23ThreatLastSeen<96) return;
        if (!g_v23PulledWorkers.empty()) {
            for (auto worker:me->getUnits()) {
                if (!worker || !worker->exists() ||
                    !g_v23PulledWorkers.count(worker->getID())) continue;
                auto mineral=AIV23NearestMineral(worker);
                if (mineral && !AIV353ReservedBuilder(worker)) worker->gather(mineral);
            }
            g_v23PulledWorkers.clear();
            if (frame-g_v23LastMessage>72) {
                game->printf("v23: threat cleared, pulled SCVs returned to mining");
                g_v23LastMessage=frame;
            }
        }
        return;
    }
    g_v23ThreatLastSeen=frame;

    // A bunker behind which enemies have passed is no longer a complete
    // defense. Unload only when its firing arc has no visible threat;
    // otherwise retain the protected front-line firepower.
    bool breached=false;
    for(auto e:localThreats)if(!e->isFlying() && e->getDistance(threatenedCC)<460)
        breached=true;
    if(breached) {
        g_v57NoLoadUntil=frame+96;
        for(auto bunker:me->getUnits()) {
            if(!bunker || !bunker->exists() || !bunker->isCompleted() ||
               bunker->getType()!=BWAPI::UnitTypes::Terran_Bunker ||
               bunker->getDistance(threatenedCC)>1200 ||
               bunker->getLoadedUnits().empty())continue;
            bool engaged=false;
            for(auto ep:game->enemies())for(auto e:ep->getUnits())
                if(AIV23VisibleThreat(e) && e->getDistance(bunker)<=224)engaged=true;
            if(!engaged && frame-bunker->getLastCommandFrame()>=24)
                bunker->unloadAll();
        }
    }

    // Assess strength at this base, not global army size or the opponent's race.
    int nearbyWorkers=0;
    int localDefenderPower=0;
    for (auto u:me->getUnits()) {
        if (!u || !u->exists() || !u->isCompleted() ||
            u->getDistance(threatenedCC)>550) continue;
        const auto type=u->getType();
        if (type==BWAPI::UnitTypes::Terran_SCV) ++nearbyWorkers;
        else if(u->isLoaded())continue;
        else if(type==BWAPI::UnitTypes::Terran_Bunker) {
            bool covering=false;
            for(auto e:localThreats)if(!e->isFlying() && e->getDistance(u)<=224)
                covering=true;
            if(covering)localDefenderPower+=(int)u->getLoadedUnits().size()*2;
        }
        else if (type==BWAPI::UnitTypes::Terran_Marine) localDefenderPower+=2;
        else if (type==BWAPI::UnitTypes::Terran_Firebat) localDefenderPower+=3;
        else if (type==BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode ||
                 type==BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode) localDefenderPower+=5;
        else if (type==BWAPI::UnitTypes::Terran_Vulture) localDefenderPower+=3;
    }
    for (auto e:localThreats) if (!e->isFlying())
        localGroundThreatScore+=AIV23ThreatWeight(e);
    // Do not sacrifice SCVs against air-only raids; SCVs cannot shoot flyers.
    const int deficit=std::max(0,localGroundThreatScore*2-localDefenderPower);
    const int maxPull=std::max(0,std::min(10,nearbyWorkers-4));
    const int desiredPulled=std::min(maxPull,breached?std::max(4,deficit):deficit);
    if(frame%96==0) {
        _mkdir(R"(C:\Users\User\starcraft-api\STARTcraft\AI_reports)");
        std::ofstream log(R"(C:\Users\User\starcraft-api\STARTcraft\AI_reports\AI_v58_breach.csv)",std::ios::app);
        if(log)log<<frame<<','<<threatenedCC->getID()<<','<<breached<<','
                  <<localGroundThreatScore<<','<<localDefenderPower<<','
                  <<desiredPulled<<','<<g_v57NoLoadUntil<<'\n';
    }

    // If we already have more pulled workers than needed, release the extras.
    for (auto it=g_v23PulledWorkers.begin();it!=g_v23PulledWorkers.end();) {
        BWAPI::Unit w=nullptr;
        for (auto candidate:me->getUnits())
            if (candidate && candidate->getID()==*it && candidate->exists()) {w=candidate;break;}
        if (!w || w->getDistance(threatenedCC)>850 ||
            (int)g_v23PulledWorkers.size()>desiredPulled) {
            if (w && !AIV353ReservedBuilder(w)) { auto m=AIV23NearestMineral(w); if(m)w->gather(m); }
            it=g_v23PulledWorkers.erase(it);
        } else ++it;
    }
    // One per iteration: workers closest to visible hostile ground attackers
    // are recruited first. Never commandeer builders, repairers or the scout.
    while ((int)g_v23PulledWorkers.size()<desiredPulled) {
        BWAPI::Unit selected=nullptr;
        int score=100000000;
        for (auto w:me->getUnits()) {
            if (!w || !w->exists() || !w->isCompleted() ||
                w->getType()!=BWAPI::UnitTypes::Terran_SCV ||
                g_v23PulledWorkers.count(w->getID()) ||
                w->getID()==g_v5ScoutId || w->getHitPoints()<30 ||
                AIV353ReservedBuilder(w) || w->isConstructing() || w->isRepairing() ||
                w->isGatheringGas() || w->isCarryingGas() ||
                w->getDistance(threatenedCC)>410 ||
                (!w->isGatheringMinerals() && !w->isIdle())) continue;
            auto target=AIV23ClosestThreat(w,localThreats);
            if (!target) continue;
            int d=w->getDistance(target);
            if (d<score) {score=d;selected=w;}
        }
        if (!selected) break;
        g_v23PulledWorkers.insert(selected->getID());
    }

    // Emergency combat micro. Do not spam attack orders every frame or cancel
    // attack animations. Continue normal macro while these units defend.
    for (auto u:me->getUnits()) {
        if (!u || !u->exists() || !u->isCompleted() ||
            u->isLoaded() || u->getDistance(threatenedCC)>(breached?1200:530)) continue;
        const auto type=u->getType();
        const bool worker=g_v23PulledWorkers.count(u->getID())!=0;
        if(worker && AIV353ReservedBuilder(u)) {
            g_v23PulledWorkers.erase(u->getID());
            continue;
        }
        const bool marine=type==BWAPI::UnitTypes::Terran_Marine;
        const bool firebat=type==BWAPI::UnitTypes::Terran_Firebat;
        if (!worker && !marine && !firebat) continue;
        if (frame-g_v23LastMicro[u->getID()]<12 || u->isAttackFrame())continue;
        auto target=AIV23ClosestThreat(u,localThreats);
        if (!target)continue;
        // STARTCRAFT_AI_V26_EARLY_PULL: gather SCV screen around the CC
        // before the incoming force reaches attack range, without feeding
        // workers directly into a Zealot/Zergling pack.
        if(worker && u->getDistance(target)>460) {
            const auto base=threatenedCC->getPosition();
            const auto foe=target->getPosition();
            const int dx=foe.x-base.x,dy=foe.y-base.y;
            const int norm=(std::max)(1,(std::max)(std::abs(dx),std::abs(dy)));
            BWAPI::Position screen(base.x+dx*180/norm,base.y+dy*180/norm);
            if(!screen.isValid() || !game->isWalkable(screen.x/8,screen.y/8) ||
               !u->hasPath(screen))screen=base;
            if(u->getDistance(screen)>64) {
                if(u->move(screen))
                    g_v23LastMicro[u->getID()]=frame;
            } else if(u->holdPosition())g_v23LastMicro[u->getID()]=frame;
            continue;
        }
        if(u->getDistance(target)>460) {
            if(!worker && breached && u->hasPath(threatenedCC->getPosition()) &&
               u->move(threatenedCC->getPosition()))g_v23LastMicro[u->getID()]=frame;
            continue;
        }
        if (worker && u->getHitPoints()<27) {
            // A wounded SCV retreats; others fill the defense if still needed.
            auto mineral=AIV23NearestMineral(u);
            if (mineral)u->gather(mineral);
            g_v23PulledWorkers.erase(u->getID());
            g_v23LastMicro[u->getID()]=frame;
            continue;
        }
        if (marine && u->getHitPoints()<25 &&
            u->getGroundWeaponCooldown()>2 && u->getDistance(target)<112) {
            const int dx=u->getPosition().x-target->getPosition().x;
            const int dy=u->getPosition().y-target->getPosition().y;
            const int len=std::max(32,std::abs(dx)+std::abs(dy));
            const int x=u->getPosition().x+dx*90/len;
            const int y=u->getPosition().y+dy*90/len;
            const BWAPI::Position retreat(
                std::max(0,std::min(game->mapWidth()*32-1,x)),
                std::max(0,std::min(game->mapHeight()*32-1,y)));
            if (u->move(retreat))g_v23LastMicro[u->getID()]=frame;
        } else if (u->attack(target)) {
            g_v23LastMicro[u->getID()]=frame;
        }
    }
    game->drawTextScreen(BWAPI::Position(10,222),
        "v23 DEFEND: hostile %d, friendly %d, SCV pull %d/%d",
        largestThreatScore,localDefenderPower,(int)g_v23PulledWorkers.size(),desiredPulled);
    if (frame-g_v23LastMessage>240) {
        game->printf("v23: reactive defense, threat=%d, defenders=%d, pulled SCVs=%d",
                     largestThreatScore,localDefenderPower,(int)g_v23PulledWorkers.size());
        g_v23LastMessage=frame;
    }
}


// STARTCRAFT_AI_V24_TECH
// Optional mid-game Terran tech and researched ability usage.
// Kept separate from the existing combat/supply/worker directors.
static int g_v24PreviousFrame = -1;
static int g_v24LastTechOrder = -99999;
static int g_v24LastCombatTick = -99999;
static std::map<int,int> g_v24LastStim;

static int AIV24CountType(BWAPI::UnitType type, bool completeOnly=false) {
    auto me=BWAPI::Broodwar->self();
    if (!me) return 0;
    int count=0;
    for (auto u:me->getUnits())
        if (u && u->exists() && u->getType()==type &&
            (!completeOnly || u->isCompleted())) ++count;
    return count;
}

static bool AIV24Upgrade(BWAPI::Unit building, BWAPI::UpgradeType upgrade,
                         int mineralBuffer, int gasBuffer) {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if (!me || !building || !building->isCompleted() || building->isFlying() ||
        building->isUpgrading() || building->isResearching()) return false;
    const int level=me->getUpgradeLevel(upgrade);
    if (level>=upgrade.maxRepeats() || me->isUpgrading(upgrade)) return false;
    if (me->minerals()<upgrade.mineralPrice(level+1)+mineralBuffer ||
        me->gas()<upgrade.gasPrice(level+1)+gasBuffer) return false;
    if (!game->canUpgrade(upgrade,building)) return false;
    if (building->upgrade(upgrade)) {
        game->printf("v24 upgrade %s %d", upgrade.getName().c_str(),level+1);
        return true;
    }
    return false;
}

static void AIV24TechDirector() {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if (!me || me->getRace()!=BWAPI::Races::Terran) return;
    const int frame=game->getFrameCount();
    if (frame<g_v24PreviousFrame) {
        g_v24LastTechOrder=-99999;g_v24LastCombatTick=-99999;
        g_v24LastStim.clear();
    }
    g_v24PreviousFrame=frame;
    if (frame%24 || frame-g_v24LastTechOrder<120) return;
    const AIV11Counts c=AIV11Count();
    const auto enemy=AIV13OpponentRace();
    const bool bio=(enemy==BWAPI::Races::Zerg || enemy==BWAPI::Races::Unknown);
    const bool versusProtoss=enemy==BWAPI::Races::Protoss;
    const bool versusTerran=enemy==BWAPI::Races::Terran;
    bool visibleEnemyAir=false;
    for(auto ep:game->enemies())for(auto e:ep->getUnits())
        if(e && e->exists() && e->isVisible() && e->isFlying() &&
           !e->getType().isBuilding())visibleEnemyAir=true;
    const bool protossAirTech=AIV25Seen(BWAPI::UnitTypes::Protoss_Stargate)>0 ||
                              visibleEnemyAir;
    const bool terranAirTech=AIV25Seen(BWAPI::UnitTypes::Terran_Starport)>0 ||
                            visibleEnemyAir;
    const int ebay=AIV24CountType(BWAPI::UnitTypes::Terran_Engineering_Bay,true);
    const int armory=AIV24CountType(BWAPI::UnitTypes::Terran_Armory,true);
    const int starport=AIV24CountType(BWAPI::UnitTypes::Terran_Starport,true);
    const int facility=AIV24CountType(BWAPI::UnitTypes::Terran_Science_Facility,true);
    const bool canAffordTech=(c.completedCC>=2 ||
        (c.scv>=23 && game->elapsedTime()>=360 && me->minerals()>=380));

    // 1) Use Engineering Bay for +1/+2/+3, not only turrets.
    if (ebay && c.marine>=7) {
        for (auto u:me->getUnits()) {
            if (!u || u->getType()!=BWAPI::UnitTypes::Terran_Engineering_Bay) continue;
            auto weapons=BWAPI::UpgradeTypes::Terran_Infantry_Weapons;
            auto armor=BWAPI::UpgradeTypes::Terran_Infantry_Armor;
            if (AIV24Upgrade(u,weapons,105,45) ||
                AIV24Upgrade(u,armor,125,45)) {
                g_v24LastTechOrder=frame;return;
            }
        }
    }

    // 2) Armory and vehicle upgrades for a tank-heavy army.
    if (armory && c.tank>=3) {
        for (auto u:me->getUnits()) {
            if (!u || u->getType()!=BWAPI::UnitTypes::Terran_Armory) continue;
            if (AIV24Upgrade(u,BWAPI::UpgradeTypes::Terran_Vehicle_Weapons,120,50) ||
                AIV24Upgrade(u,BWAPI::UpgradeTypes::Terran_Vehicle_Plating,125,50)) {
                g_v24LastTechOrder=frame;return;
            }
        }
    }

    // 3) Extra tech only after a functional mid-game economy; emergency
    // expansion/worker/supply spending stays available via mineral buffers.
    // STARTCRAFT_AI_V41_TVP_TECH_GATE: Pluto won while v40 bought Starport
    // and Science Facility before establishing a durable tank line.  In TvP,
    // air tech now answers observed air or follows a real two-base mech core.
    const bool protossTechReady=!versusProtoss || protossAirTech ||
                                (c.completedCC>=2 && c.tank>=4 && c.factory>=2);
    const bool terranTechReady=!versusTerran || terranAirTech ||
                               (c.completedCC>=2 && c.tank>=5 && c.factory>=3);
    const bool airTechReady=protossTechReady && terranTechReady;
    if (canAffordTech && airTechReady && c.factory>=1 && c.scv>=21 &&
        !AIV24CountType(BWAPI::UnitTypes::Terran_Starport) &&
        !AIV11PendingBuild(BWAPI::UnitTypes::Terran_Starport) &&
        me->minerals()>=310 && me->gas()>=165) {
        if (AIV11BuildNear(BWAPI::UnitTypes::Terran_Starport)) {
            g_v24LastTechOrder=frame;return;
        }
    }
    if (canAffordTech && airTechReady && starport>0 && c.scv>=22 &&
        !AIV24CountType(BWAPI::UnitTypes::Terran_Science_Facility) &&
        !AIV11PendingBuild(BWAPI::UnitTypes::Terran_Science_Facility) &&
        me->minerals()>=240 && me->gas()>=230) {
        if (AIV11BuildNear(BWAPI::UnitTypes::Terran_Science_Facility)) {
            g_v24LastTechOrder=frame;return;
        }
    }
    if (canAffordTech && c.tank>=4 && !AIV24CountType(BWAPI::UnitTypes::Terran_Armory) &&
        !AIV11PendingBuild(BWAPI::UnitTypes::Terran_Armory) &&
        me->minerals()>=230 && me->gas()>=110) {
        if (AIV11BuildNear(BWAPI::UnitTypes::Terran_Armory)) {
            g_v24LastTechOrder=frame;return;
        }
    }
    // Control Tower must fit to the RIGHT of the Starport.
    if (starport>0 && facility>0 &&
        !AIV24CountType(BWAPI::UnitTypes::Terran_Control_Tower) &&
        me->minerals()>=130 && me->gas()>=115) {
        for (auto u:me->getUnits()) {
            if (!u || u->getType()!=BWAPI::UnitTypes::Terran_Starport ||
                !u->isCompleted() || u->isFlying() || u->getAddon()) continue;
            if (u->canBuildAddon(BWAPI::UnitTypes::Terran_Control_Tower) &&
                u->buildAddon(BWAPI::UnitTypes::Terran_Control_Tower)) {
                g_v24LastTechOrder=frame;
                game->printf("v24 Control Tower started");
                return;
            }
        }
    }
    // Vessel is primarily a detector for moving army and unseen threats.
    if (facility>0 && AIV24CountType(BWAPI::UnitTypes::Terran_Control_Tower,true)>0 &&
        AIV24CountType(BWAPI::UnitTypes::Terran_Science_Vessel)<(bio?2:1) &&
        me->minerals()>=175 && me->gas()>=225) {
        for (auto u:me->getUnits()) {
            if (!u || u->getType()!=BWAPI::UnitTypes::Terran_Starport ||
                !u->isCompleted() || u->isTraining() || u->isFlying() ||
                !u->getAddon() || !u->getAddon()->isCompleted()) continue;
            if (game->canMake(BWAPI::UnitTypes::Terran_Science_Vessel,u) &&
                u->train(BWAPI::UnitTypes::Terran_Science_Vessel)) {
                g_v24LastTechOrder=frame;
                game->printf("v24 Science Vessel queued");
                return;
            }
        }
    }
}

// Use researched Stim in a nearby real fight, not while standing at home.
// Use a medical/health threshold and cooldown; never re-stim a stimmed marine.
static void AIV24CombatAbilities() {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if (!me || me->getRace()!=BWAPI::Races::Terran) return;
    const int frame=game->getFrameCount();
    if (frame%6) return;
    if (frame<g_v24LastCombatTick)g_v24LastStim.clear();
    g_v24LastCombatTick=frame;
    if (me->hasResearched(BWAPI::TechTypes::Stim_Packs))
    for (auto marine:me->getUnits()) {
        if (!marine || !marine->exists() || !marine->isCompleted() ||
            marine->getType()!=BWAPI::UnitTypes::Terran_Marine ||
            marine->isStimmed() || marine->getHitPoints()<34) continue;
        auto it=g_v24LastStim.find(marine->getID());
        if (it!=g_v24LastStim.end() && frame-it->second<96) continue;
        bool enemyClose=false;
        for (auto player:game->enemies()) for (auto enemy:player->getUnits()) {
            if (!enemy || !enemy->exists() || !enemy->isVisible() ||
                !enemy->isDetected() || enemy->isInvincible() ||
                enemy->getType().isBuilding()) continue;
            if (marine->getDistance(enemy)<190) {enemyClose=true;break;}
        }
        if (!enemyClose) continue;
        if (marine->useTech(BWAPI::TechTypes::Stim_Packs))
            g_v24LastStim[marine->getID()]=frame;
    }
    // Science Vessels are detectors, not assault units. Keep them just
    // behind the fighting ground force instead of idling over the base.
    if (frame%48==0) {
        const BWAPI::Position home(me->getStartLocation());
        BWAPI::Unit frontline=nullptr;
        int distanceFromHome=0;
        for (auto ally:me->getUnits()) {
            if (!ally || !ally->exists() || !ally->isCompleted() ||
                (ally->getType()!=BWAPI::UnitTypes::Terran_Marine &&
                 ally->getType()!=BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode))continue;
            const int d=ally->getDistance(home);
            if (d>distanceFromHome && d>350) {frontline=ally;distanceFromHome=d;}
        }
        if (frontline)for (auto vessel:me->getUnits()) {
            if (!vessel || !vessel->exists() ||
                vessel->getType()!=BWAPI::UnitTypes::Terran_Science_Vessel ||
                !vessel->isCompleted())continue;
            if (vessel->getDistance(frontline)>180)
                vessel->move(frontline->getPosition());
        }
    }
}

// STARTCRAFT_AI_V37_HIGH_APM_MICRO
// Per-unit combat control. Commands are staggered and never replace an attack
// frame, so higher APM improves targeting without suppressing weapon fire.
static std::map<int,int> g_v37LastOrder;
static int g_v37PreviousFrame=-1;
static BWAPI::Position AIV37Retreat(BWAPI::Unit u,BWAPI::Unit enemy,int distance){
    auto& game=BWAPI::Broodwar;
    int dx=u->getPosition().x-enemy->getPosition().x;
    int dy=u->getPosition().y-enemy->getPosition().y;
    int norm=(std::max)(32,(std::max)(std::abs(dx),std::abs(dy)));
    return BWAPI::Position((std::max)(16,(std::min)(game->mapWidth()*32-16,u->getPosition().x+dx*distance/norm)),
                           (std::max)(16,(std::min)(game->mapHeight()*32-16,u->getPosition().y+dy*distance/norm)));
}
static void AIV37HighApmMicro(){
    auto& game=BWAPI::Broodwar;auto me=game->self();if(!me)return;
    if(AIV13OpponentRace()==BWAPI::Races::Protoss)return;
    int frame=game->getFrameCount();
    if(frame<g_v37PreviousFrame)g_v37LastOrder.clear();g_v37PreviousFrame=frame;
    if(frame%4)return;
    for(auto u:me->getUnits()){
        if(!u || !u->exists() || !u->isCompleted() || u->isLoaded() || u->isAttackFrame() ||
           frame-g_v37LastOrder[u->getID()]<8)continue;
        auto type=u->getType();
        const bool marine=type==BWAPI::UnitTypes::Terran_Marine;
        const bool vulture=type==BWAPI::UnitTypes::Terran_Vulture;
        const bool goliath=type==BWAPI::UnitTypes::Terran_Goliath;
        if(!marine && !vulture && !goliath)continue;
        BWAPI::Unit target=nullptr;int best=100000000;
        for(auto ep:game->enemies())for(auto e:ep->getUnits()){
            if(!e || !e->exists() || !e->isVisible() || !e->isDetected() || e->isInvincible())continue;
            // Static detection and turrets may kill an Observer. Mobile bio
            // must not abandon its prepared ground line to chase one.
            if(e->getType()==BWAPI::UnitTypes::Protoss_Observer)continue;
            int d=u->getDistance(e);if(d>360)continue;
            if(vulture && e->isFlying())continue;
            int score=d*3+(std::max)(0,e->getHitPoints());
            if(goliath && e->isFlying())score-=500;
            if(e->getType().isWorker())score-=70;
            if(e->getType()==BWAPI::UnitTypes::Protoss_High_Templar ||
               e->getType()==BWAPI::UnitTypes::Zerg_Lurker ||
               e->getType()==BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)score-=320;
            if(score<best){best=score;target=e;}
        }
        if(!target)continue;
        const int distance=u->getDistance(target);
        const bool melee=target->getType().groundWeapon()!=BWAPI::WeaponTypes::None &&
            target->getType().groundWeapon().maxRange()<=32;
        const bool cooldown=(target->isFlying()?u->getAirWeaponCooldown():
                                                u->getGroundWeaponCooldown())>2;
        if((marine || vulture) && cooldown && melee && distance<105){
            BWAPI::Position retreat=AIV37Retreat(u,target,vulture?120:82);
            if(game->isWalkable(retreat.x/8,retreat.y/8) && u->move(retreat))
                g_v37LastOrder[u->getID()]=frame;
        }else if(!cooldown && u->attack(target))g_v37LastOrder[u->getID()]=frame;
    }
}

// STARTCRAFT_AI_V29_STRATEGY_SENSE
// STARTCRAFT_AI_V30_RACE_OBSERVER
static void AIV30ObserveEnemyRace() {
    auto& game=BWAPI::Broodwar;
    const int frame=game->getFrameCount();
    if(frame<g_v30LastRaceFrame)g_v30EnemyRace=BWAPI::Races::Unknown;
    g_v30LastRaceFrame=frame;
    if(g_v30EnemyRace==BWAPI::Races::Zerg ||
       g_v30EnemyRace==BWAPI::Races::Protoss ||
       g_v30EnemyRace==BWAPI::Races::Terran)return;
    for(auto player:game->enemies()) {
        if(!player)continue;
        const BWAPI::Race race=player->getRace();
        if(race==BWAPI::Races::Zerg || race==BWAPI::Races::Protoss ||
           race==BWAPI::Races::Terran){g_v30EnemyRace=race;return;}
        // Visible observed units are a permanent, legitimate race clue.
        for(auto foe:player->getUnits()){
            if(!foe || !foe->exists() || !foe->isVisible())continue;
            const BWAPI::Race observed=foe->getType().getRace();
            if(observed==BWAPI::Races::Zerg ||
               observed==BWAPI::Races::Protoss ||
               observed==BWAPI::Races::Terran){
                g_v30EnemyRace=observed;return;
            }
        }
    }
    // In case the enemy's unit left vision before the observer ran, use
    // historical building types from v25; never infer from fogged positions.
    for(const auto& entry:g_v25SeenBuilding){
        const int id=entry.second.first;
        if(id==BWAPI::UnitTypes::Zerg_Hatchery.getID() ||
           id==BWAPI::UnitTypes::Zerg_Spawning_Pool.getID() ||
           id==BWAPI::UnitTypes::Zerg_Hydralisk_Den.getID()){
            g_v30EnemyRace=BWAPI::Races::Zerg;return;
        }
        if(id==BWAPI::UnitTypes::Protoss_Nexus.getID() ||
           id==BWAPI::UnitTypes::Protoss_Gateway.getID()){
            g_v30EnemyRace=BWAPI::Races::Protoss;return;
        }
        if(id==BWAPI::UnitTypes::Terran_Command_Center.getID() ||
           id==BWAPI::UnitTypes::Terran_Factory.getID()){
            g_v30EnemyRace=BWAPI::Races::Terran;return;
        }
    }
}

static void AIV29StrategySense() {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if(!me)return;
    bool terran=false;
    bool knownOther=false;
    for(auto ep:game->enemies())if(ep){
        if(ep->getRace()==BWAPI::Races::Terran)terran=true;
        if(ep->getRace()==BWAPI::Races::Zerg || ep->getRace()==BWAPI::Races::Protoss)
            knownOther=true;
    }
    if(!terran && !knownOther) {
        for(const auto& k:g_v25SeenBuilding){
            const int t=k.second.first;
            if(t==BWAPI::UnitTypes::Terran_Barracks.getID() ||
               t==BWAPI::UnitTypes::Terran_Factory.getID() ||
               t==BWAPI::UnitTypes::Terran_Command_Center.getID())terran=true;
        }
    }
    // No automatic reversion midgame unless a non-Terran race is confirmed.
    g_v29TerranMech=(g_v30EnemyRace==BWAPI::Races::Terran) || (g_v30EnemyRace==BWAPI::Races::Unknown && terran && !knownOther);
}

// STARTCRAFT_AI_V29_POSTGAME
// Logs game-state snapshots. No full-map cheats: enemy data come from the
// already observed v25/v28 intelligence ledgers.
static const char* g_v29TimelinePath=
  "C:\\Users\\User\\starcraft-api\\STARTcraft\\AI_reports\\AI_v29_latest.csv";
static const char* g_v29FinishPath=
  "C:\\Users\\User\\starcraft-api\\STARTcraft\\AI_reports\\AI_v29_finished.flag";
static int g_v29LastFrame=-1;
static bool g_v29ReportStarted=false;
static int g_v29SnapshotCount=0;
static int g_v29MechSwitchFrame=-1;
static int g_v29PeakEnemyKnown=0;

static void AIV29MatchTick(bool final=false) {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if(!me)return;
    const int frame=game->getFrameCount();
    if(g_v29TerranMech && g_v29MechSwitchFrame<0)g_v29MechSwitchFrame=frame;
    if(frame<g_v29LastFrame){
        g_v29ReportStarted=false;g_v29SnapshotCount=0;
        g_v29MechSwitchFrame=-1;g_v29PeakEnemyKnown=0;
    }
    g_v29LastFrame=frame;
    if(!g_v29ReportStarted){
        _mkdir("C:\\Users\\User\\starcraft-api\\STARTcraft\\AI_reports");
        std::ofstream log(g_v29TimelinePath,std::ios::out | std::ios::trunc);
        if(log){
            log << "frame,seconds,enemy_race,minerals,gas,scv,marines,medics,"
                   "tanks,vultures,goliaths,cc,rax,factories,starports,"
                   "known_enemy_buildings,enemy_army_sightings,scout_phase,"
                   "scout_checks,mech_mode,army_state,plan\n";
            g_v29ReportStarted=true;
        }
    }
    // Snapshot every 10 seconds, with first snapshot and final snapshot at onEnd.
    if(!final && frame==0 && g_v29SnapshotCount>0)return; // STARTCRAFT_AI_V30_LOG_FIX
    if(!final && frame!=0 && frame%240!=0)return;
    if(!g_v29ReportStarted)return;
    int scv=0,marine=0,medic=0,tank=0,vulture=0,goliath=0;
    int cc=0,rax=0,fac=0,star=0;
    for(auto u:me->getUnits()){
        if(!u || !u->exists())continue;
        const auto t=u->getType();
        if(t==BWAPI::UnitTypes::Terran_SCV)++scv;
        else if(t==BWAPI::UnitTypes::Terran_Marine)++marine;
        else if(t==BWAPI::UnitTypes::Terran_Medic)++medic;
        else if(t==BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode ||
                t==BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)++tank;
        else if(t==BWAPI::UnitTypes::Terran_Vulture)++vulture;
        else if(t==BWAPI::UnitTypes::Terran_Goliath)++goliath;
        else if(t==BWAPI::UnitTypes::Terran_Command_Center)++cc;
        else if(t==BWAPI::UnitTypes::Terran_Barracks)++rax;
        else if(t==BWAPI::UnitTypes::Terran_Factory)++fac;
        else if(t==BWAPI::UnitTypes::Terran_Starport)++star;
    }
    const int known=(int)g_v25SeenBuilding.size();
    if(known>g_v29PeakEnemyKnown)g_v29PeakEnemyKnown=known;
    std::string race="Unknown";
    for(auto p:game->enemies())if(p){race=p->getRace().getName();break;}
    if((race=="Unknown" || race=="Random") &&
       g_v30EnemyRace!=BWAPI::Races::Unknown)
       race=g_v30EnemyRace.getName(); // STARTCRAFT_AI_V30_REPORT_RACE
    std::ofstream log(g_v29TimelinePath,std::ios::out | std::ios::app);
    if(!log)return;
    log << frame << ',' << game->elapsedTime() << ',' << race << ','
        << me->minerals() << ',' << me->gas() << ',' << scv << ','
        << marine << ',' << medic << ',' << tank << ',' << vulture << ','
        << goliath << ',' << cc << ',' << rax << ',' << fac << ',' << star << ','
        << known << ',' << g_v29UnitSightings << ',' << g_v27Phase << ','
        << g_v29MainChecks << ',' << g_v29TerranMech << ',' << (int)g_v7State
        << ',' << (g_v28PlanLabel?g_v28PlanLabel:"UNKNOWN") << '\n';
    ++g_v29SnapshotCount;
}

static void AIV29MatchEnded(bool isWinner) {
    // Do not kill StarCraft inside its BWAPI callback. Signal the external
    // launch supervisor AFTER all files have been flushed and closed.
    AIV29MatchTick(true);
    std::ofstream report(g_v29FinishPath,std::ios::out | std::ios::trunc);
    if(!report)return;
    report << "result=" << (isWinner?"WIN":"LOSS") << '\n';
    report << "game_seconds=" << BWAPI::Broodwar->elapsedTime() << '\n';
    report << "game_frames=" << BWAPI::Broodwar->getFrameCount() << '\n';
    report << "map=" << BWAPI::Broodwar->mapName() << '\n';
    report << "scout_attempts=" << g_v27ScoutAttempts << '\n';
    report << "scout_main_checks=" << g_v29MainChecks << '\n';
    report << "enemy_production_known=" << g_v28KnownProduction.size() << '\n';
    report << "enemy_units_observed=" << g_v29UnitSightings << '\n';
    report << "enemy_tech_last_seen_frame=" << g_v29EnemyTechLastSeen << '\n';
    report << "first_mech_frame=" << g_v29MechSwitchFrame << '\n';
    report << "strategy=" << (g_v28PlanLabel?g_v28PlanLabel:"UNKNOWN") << '\n';
    report << "samples=" << g_v29SnapshotCount << '\n';
    report << "seen_enemy_barracks=" << AIV25Seen(BWAPI::UnitTypes::Terran_Barracks) << '\n';
    report << "seen_enemy_factory=" << AIV25Seen(BWAPI::UnitTypes::Terran_Factory) << '\n';
    report << "seen_enemy_starport=" << AIV25Seen(BWAPI::UnitTypes::Terran_Starport) << '\n';
    report << "seen_enemy_gateway=" << AIV25Seen(BWAPI::UnitTypes::Protoss_Gateway) << '\n';
    report << "seen_enemy_stargate=" << AIV25Seen(BWAPI::UnitTypes::Protoss_Stargate) << '\n';
    report << "seen_enemy_spawning_pool=" << AIV25Seen(BWAPI::UnitTypes::Zerg_Spawning_Pool) << '\n';
    report << "seen_enemy_hydra_den=" << AIV25Seen(BWAPI::UnitTypes::Zerg_Hydralisk_Den) << '\n';
    report << "seen_enemy_spire=" << AIV25Seen(BWAPI::UnitTypes::Zerg_Spire) << '\n';
    report << "recent_enemy_tanks=" << AIV25RecentArmy(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode) << '\n';
    report << "recent_enemy_vultures=" << AIV25RecentArmy(BWAPI::UnitTypes::Terran_Vulture) << '\n';
    report << "recent_enemy_zealots=" << AIV25RecentArmy(BWAPI::UnitTypes::Protoss_Zealot) << '\n';
    report << "recent_enemy_lings=" << AIV25RecentArmy(BWAPI::UnitTypes::Zerg_Zergling) << '\n';
    report.flush();
}


// STARTCRAFT_AI_V34_FORMATION_AND_BUILDERS
// Follow-up orders at a different frame offset than v8's 24-frame tick.
// They never touch enemies-in-range, active repairs, mining or garrison members.
static std::map<int,BWAPI::Position> g_v34LastPosition;
static std::map<int,int> g_v34StillSince;
static std::map<int,int> g_v34LastOrder;
static std::set<int> g_v34RecentlyBuilding;
static int g_v34PrevFrame=-1;
static int g_v34MedicCorrections=0;
static int g_v34StuckCorrections=0;
static int g_v34BuilderRescues=0;
static int g_v34LastLogged=-1;

static void AIV34UnitFlow() {
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if(!me || me->getRace()!=BWAPI::Races::Terran)return;
    const int now=game->getFrameCount();
    if(now<g_v34PrevFrame){
        g_v34LastPosition.clear();g_v34StillSince.clear();
        g_v34LastOrder.clear();g_v34RecentlyBuilding.clear();
        g_v34MedicCorrections=g_v34StuckCorrections=g_v34BuilderRescues=0;
        g_v34LastLogged=-1;
    }
    g_v34PrevFrame=now;
    // Observe construction every frame: a builder can stop building between
    // 24-frame economy ticks, so don't rely exclusively on isIdle() snapshots.
    for(auto w:me->getUnits()) {
        if(!w || !w->exists() || w->getType()!=BWAPI::UnitTypes::Terran_SCV)continue;
        if(w->isConstructing())g_v34RecentlyBuilding.insert(w->getID());
    }
    if(now%24!=12)return;

    int rescuedNow=0;
    for(auto w:me->getUnits()){
        if(rescuedNow>=2)break;
        if(!w || !w->exists() || !w->isCompleted() ||
           w->getType()!=BWAPI::UnitTypes::Terran_SCV ||
           !g_v34RecentlyBuilding.count(w->getID()) || AIV353ReservedBuilder(w) || w->isConstructing())continue;
        if(w->isGatheringMinerals() || w->isGatheringGas() ||
           w->isCarryingMinerals() || w->isCarryingGas() ||
           w->isRepairing() || w->isUnderAttack() || w->getID()==g_v5ScoutId) {
            g_v34RecentlyBuilding.erase(w->getID());
            continue;
        }
        // Keep the completion flag until the builder has had time to stop.
        if(!w->isIdle() && now-w->getLastCommandFrame()<96)continue;
        BWAPI::Unit bestPatch=nullptr;
        int bestScore=100000000;
        for(auto mineral:game->getMinerals()){
            if(!AIV21IsMine(mineral) || mineral->getResources()<=0 ||
               !w->hasPath(mineral->getPosition()))continue;
            auto cc=AIV31DepotForPatch(mineral);
            if(!cc || !cc->isCompleted())continue;
            int assigned=0;
            for(auto other:me->getUnits())if(other && other->exists() && other!=w &&
                other->getType()==BWAPI::UnitTypes::Terran_SCV &&
                AIV31PatchForWorker(other)==mineral)++assigned;
            if(assigned>=3)continue;
            const int score=assigned*6000 + w->getDistance(mineral) +
                            cc->getDistance(mineral)/2;
            if(score<bestScore){bestScore=score;bestPatch=mineral;}
        }
        if(bestPatch && w->gather(bestPatch)){
            g_v21MineIntent[w->getID()]=bestPatch->getID();
            g_v21MineCommandFrame[w->getID()]=now;
            ++g_v34BuilderRescues;++rescuedNow;
            g_v34RecentlyBuilding.erase(w->getID());
        }
    }
    // If construction finished but the worker still had another queued command,
    // the normal v21/v32 mining director will reassign it when idle.
    if(g_v7State!=AIV7_ATTACK)return;
    const BWAPI::Position enemy=AIV10AttackTarget();
    const BWAPI::Position center=AIV7CenterOfArmy(AIV33AssemblyPoint());
    if(!enemy.isValid() || !center.isValid())return;
    int orders=0;
    for(auto u:me->getUnits()){
        if(orders>=5)break;
        if(!AIV7IsArmyUnit(u) || g_v32GuardIds.count(u->getID()) ||
           g_v56SiegeLineIds.count(u->getID()) ||
           u->isUnderAttack() || u->isAttackFrame() || AIV4EnemyCloseTo(u,265))continue;
        const int id=u->getID();
        if(now-g_v34LastOrder[id]<96)continue;
        const bool isMedic=(u->getType()==BWAPI::UnitTypes::Terran_Medic);
        // Unlike v8's forward waypoint, a trailing Medic must target an
        // actual active Marine. It must not peel off alone toward the enemy.
        if(isMedic){
            BWAPI::Unit marine=nullptr;
            int score=10000000;
            for(auto ally:me->getUnits())if(ally && ally->exists() &&
                ally->getType()==BWAPI::UnitTypes::Terran_Marine &&
                g_v7Army.count(ally->getID()) && !g_v32GuardIds.count(ally->getID()) &&
                !ally->isUnderAttack()){
                const int d=u->getDistance(ally);
                const int groupDrift=ally->getDistance(center);
                const int candidate=d+groupDrift/2;
                if(candidate<score){score=candidate;marine=ally;}
            }
            if(marine && u->getDistance(marine)>155 &&
               (u->isIdle() || now-u->getLastCommandFrame()>110) &&
               u->move(marine->getPosition())){
                g_v34LastOrder[id]=now;++g_v34MedicCorrections;++orders;
            }
            continue;
        }
        if(u->getType()==BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)continue;
        BWAPI::Position pos=u->getPosition();
        auto it=g_v34LastPosition.find(id);
        if(it==g_v34LastPosition.end() || it->second.getDistance(pos)>52){
            g_v34LastPosition[id]=pos;g_v34StillSince[id]=now;
            continue;
        }
        if(now-g_v34StillSince[id]<144)continue;
        // Stationary during a planned combat hold should not be overruled.
        if(u->getDistance(enemy)<320)continue;
        BWAPI::Position dest=enemy;
        // Prefer the current group waypoint if this fighter is far behind.
        if(u->getDistance(center)>480 && u->hasPath(center))dest=center;
        if(!u->hasPath(dest))continue;
        if(u->attack(dest)){
            g_v34LastOrder[id]=now;g_v34StillSince[id]=now;
            ++g_v34StuckCorrections;++orders;
        }
    }
}

static void AIV34FlowLog(){
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if(!me || me->getRace()!=BWAPI::Races::Terran)return;
    const int frame=game->getFrameCount();
    if(frame!=0 && frame%240!=0)return;
    if(frame==g_v34LastLogged)return;
    g_v34LastLogged=frame;
    _mkdir(R"(C:\Users\User\starcraft-api\STARTcraft\AI_reports)");
    std::ofstream out(R"(C:\Users\User\starcraft-api\STARTcraft\AI_reports\AI_v34_flow.csv)",
                      frame==0?std::ios::trunc:std::ios::app);
    if(!out)return;
    if(frame==0)out<<"frame,seconds,army_state,marines,medics,guard_tanks,guard_marines,"
                    "idle_scv,medic_rejoins,stuck_reorders,builder_resumes\n";
    int marine=0,medic=0,guardsT=0,guardsM=0,idle=0;
    for(auto u:me->getUnits())if(u && u->exists()){
        if(u->getType()==BWAPI::UnitTypes::Terran_Marine){
            ++marine;if(g_v32GuardIds.count(u->getID()))++guardsM;
        }
        if(u->getType()==BWAPI::UnitTypes::Terran_Medic)++medic;
        if(AIV7IsTank(u) && g_v32GuardIds.count(u->getID()))++guardsT;
        if(u->getType()==BWAPI::UnitTypes::Terran_SCV && u->isIdle())++idle;
    }
    out<<frame<<','<<game->elapsedTime()<<','<<(int)g_v7State<<','<<marine<<','
       <<medic<<','<<guardsT<<','<<guardsM<<','<<idle<<','
       <<g_v34MedicCorrections<<','<<g_v34StuckCorrections<<','
       <<g_v34BuilderRescues<<'\n';
}

// STARTCRAFT_AI_V33_DIAGNOSTICS
static void AIV33BaseMetrics(){
    auto& game=BWAPI::Broodwar;
    auto me=game->self();
    if(!me || me->getRace()!=BWAPI::Races::Terran)return;
    const int frame=game->getFrameCount();
    static int last=-1;
    if(frame<last)last=-1;
    if(frame!=0 && frame%240!=0)return;
    if(frame==last)return;
    last=frame;
    _mkdir("C:\\Users\\User\\starcraft-api\\STARTcraft\\AI_reports");
    std::ofstream out("C:\\Users\\User\\starcraft-api\\STARTcraft\\AI_reports\\AI_v33_base_metrics.csv",
                      frame==0?std::ios::trunc:std::ios::app);
    if(!out)return;
    if(frame==0)out<<"frame,seconds,cc_id,cc_x,cc_y,mineral_patches,assigned_scv,nearby_scv,defender_tanks,defender_marines,army_state,rax,factories\n";
    int rax=0,fac=0;
    for(auto u:me->getUnits())if(u && u->exists()){
        if(u->getType()==BWAPI::UnitTypes::Terran_Barracks)++rax;
        if(u->getType()==BWAPI::UnitTypes::Terran_Factory)++fac;
    }
    for(auto cc:me->getUnits())if(cc && cc->exists() && cc->isCompleted() &&
        !cc->isFlying() && cc->getType()==BWAPI::UnitTypes::Terran_Command_Center){
        int patches=0,assigned=0,nearby=0,tanks=0,marines=0;
        for(auto m:game->getMinerals())if(AIV21IsMine(m) && m->getResources()>0 &&
            AIV31DepotForPatch(m)==cc)++patches;
        for(auto u:me->getUnits())if(u && u->exists()){
            if(u->getType()==BWAPI::UnitTypes::Terran_SCV){
                if(u->getDistance(cc)<600)++nearby;
                if(AIV31DepotForPatch(AIV31PatchForWorker(u))==cc)++assigned;
            }
            if(g_v32GuardIds.count(u->getID()) && u->getDistance(cc)<650){
                if(AIV7IsTank(u))++tanks;
                if(u->getType()==BWAPI::UnitTypes::Terran_Marine)++marines;
            }
        }
        out<<frame<<','<<game->elapsedTime()<<','<<cc->getID()<<','
           <<cc->getPosition().x<<','<<cc->getPosition().y<<','<<patches<<','
           <<assigned<<','<<nearby<<','<<tanks<<','<<marines<<','
           <<(int)g_v7State<<','<<rax<<','<<fac<<'\n';
    }
}

// STARTCRAFT_AI_V37_3_EARLY_EXPANSION_PUNISH
// A scout-confirmed early second resource depot must change play immediately.
// Six available Marines pressure the enemy start while macro continues toward
// a matching expansion. Guards and units fighting near home are never taken.
static int g_v373PreviousFrame=-1;
static int g_v373EnemyDepotCount=0;
static BWAPI::Position g_v373PressureTarget=BWAPI::Positions::None;
static std::set<int> g_v373PressureUnits;
static void AIV373EnemyExpansionResponse() {
    auto& game=BWAPI::Broodwar;auto me=game->self();
    if(!me || me->getRace()!=BWAPI::Races::Terran)return;
    if(AIV13OpponentRace()==BWAPI::Races::Protoss){g_v373PressureUnits.clear();return;}
    const int now=game->getFrameCount();
    if(now<g_v373PreviousFrame) {
        g_v373EnemyDepotCount=0;g_v373PressureTarget=BWAPI::Positions::None;
        g_v373PressureUnits.clear();
    }
    g_v373PreviousFrame=now;
    if(now%12)return;
    int visibleDepots=0;
    BWAPI::Position newest=BWAPI::Positions::None;
    for(auto ep:game->enemies())for(auto e:ep->getUnits()) {
        if(!e || !e->exists() || !e->isVisible())continue;
        const auto t=e->getType();
        if(t!=BWAPI::UnitTypes::Terran_Command_Center &&
           t!=BWAPI::UnitTypes::Protoss_Nexus &&
           t!=BWAPI::UnitTypes::Zerg_Hatchery &&
           t!=BWAPI::UnitTypes::Zerg_Lair &&
           t!=BWAPI::UnitTypes::Zerg_Hive)continue;
        ++visibleDepots;
        if(!g_v6EnemyStart.isValid() ||
           e->getDistance(BWAPI::Position(g_v6EnemyStart))>480)newest=e->getPosition();
    }
    int rememberedDepots=0;
    for(const auto& item:g_v25SeenBuilding) {
        const int id=item.second.first;
        if(id==BWAPI::UnitTypes::Terran_Command_Center.getID() ||
           id==BWAPI::UnitTypes::Protoss_Nexus.getID() ||
           id==BWAPI::UnitTypes::Zerg_Hatchery.getID() ||
           id==BWAPI::UnitTypes::Zerg_Lair.getID() ||
           id==BWAPI::UnitTypes::Zerg_Hive.getID())++rememberedDepots;
    }
    if(rememberedDepots>=2) {
        g_v373EnemyDepotCount=(std::max)(g_v373EnemyDepotCount,rememberedDepots);
        if(newest.isValid())g_v373PressureTarget=newest;
        else if(g_v6EnemyStart.isValid())g_v373PressureTarget=BWAPI::Position(g_v6EnemyStart);
    }
    if(g_v373EnemyDepotCount<2 || !g_v373PressureTarget.isValid() ||
       now>=11520 || AIV6HomeThreat().isValid())return;
    int ownDepots=0;
    for(auto u:me->getUnits())if(u && u->exists() && !u->isFlying() &&
        u->getType()==BWAPI::UnitTypes::Terran_Command_Center)++ownDepots;
    // Matching a fast expansion is already the strategic response. Do not
    // also pull six Marines away from the unfinished natural into Dragoons.
    if(ownDepots>=2){g_v373PressureUnits.clear();return;}
    for(auto it=g_v373PressureUnits.begin();it!=g_v373PressureUnits.end();) {
        bool alive=false;
        for(auto u:me->getUnits())if(u && u->exists() && u->getID()==*it){alive=true;break;}
        if(!alive)it=g_v373PressureUnits.erase(it);else ++it;
    }
    const BWAPI::Position home(me->getStartLocation());
    for(auto u:me->getUnits()) {
        if(g_v373PressureUnits.size()>=6)break;
        if(!u || !u->exists() || !u->isCompleted() ||
           u->getType()!=BWAPI::UnitTypes::Terran_Marine ||
           g_v32GuardIds.count(u->getID()) || u->getDistance(home)>850)continue;
        g_v373PressureUnits.insert(u->getID());
    }
    if(g_v373PressureUnits.size()<6)return;
    for(auto u:me->getUnits()) {
        if(!u || !g_v373PressureUnits.count(u->getID()) || u->isAttackFrame())continue;
        if(u->isIdle() || now-u->getLastCommandFrame()>96)
            u->attack(g_v373PressureTarget);
    }
    game->drawTextScreen(BWAPI::Position(10,246),
        "v37.3 ENEMY FAST EXPAND: pressure %d/6 + match CC",
        (int)g_v373PressureUnits.size());
}

// STARTCRAFT_AI_V56_SIEGE_CRAWL
// Tanks never chase an individual unit. One Tank advances by a short bound
// while the rest remain sieged; it then sieges before another Tank may move.
// Mobile units are leashed to this line so a retreating enemy cannot pull the
// army apart one unit at a time.
static int g_v56PreviousFrame=-1;
static int g_v56MovingTank=-1;
static int g_v56LastBound=-10000;
static int g_v56LastMetrics=-10000;
static BWAPI::Position g_v56MoveTarget=BWAPI::Positions::None;
static std::map<int,int> g_v56LastOrder;

static int AIV56Progress(BWAPI::Position p,BWAPI::Position home,
                         BWAPI::Position enemy) {
    if(!p.isValid() || !home.isValid() || !enemy.isValid())return 0;
    const int dx=enemy.x-home.x,dy=enemy.y-home.y;
    const int norm=(std::max)(1,(std::max)(std::abs(dx),std::abs(dy)));
    return (int)(((long long)(p.x-home.x)*dx+(long long)(p.y-home.y)*dy)/norm);
}

static BWAPI::Position AIV56PointOnLine(BWAPI::Unit unit,BWAPI::Position home,
                                        BWAPI::Position enemy,int progress,
                                        int side=0) {
    auto& game=BWAPI::Broodwar;
    if(!unit || !home.isValid() || !enemy.isValid())return BWAPI::Positions::None;
    const int dx=enemy.x-home.x,dy=enemy.y-home.y;
    const int norm=(std::max)(1,(std::max)(std::abs(dx),std::abs(dy)));
    const int px=-dy,py=dx;
    const int offsets[9]={0,48,-48,88,-88,128,-128,168,-168};
    BWAPI::Position best=BWAPI::Positions::None;int bestScore=10000000;
    for(int offset:offsets) {
        const int lateral=side+offset;
        BWAPI::Position candidate(home.x+dx*progress/norm+px*lateral/norm,
                                  home.y+dy*progress/norm+py*lateral/norm);
        if(candidate.x<24 || candidate.y<24 ||
           candidate.x>=game->mapWidth()*32-24 ||
           candidate.y>=game->mapHeight()*32-24 ||
           !game->isWalkable(candidate.x/8,candidate.y/8) ||
           !unit->hasPath(candidate))continue;
        bool building=false;
        for(auto near:game->getUnitsInRadius(candidate,60))
            if(near && near->exists() && near->getType().isBuilding()){
                building=true;break;
            }
        if(building)continue;
        const int score=unit->getDistance(candidate)+std::abs(offset);
        if(score<bestScore){bestScore=score;best=candidate;}
    }
    return best;
}

static bool AIV56GroundContact(BWAPI::Position line,int radius) {
    if(!line.isValid())return false;
    auto& game=BWAPI::Broodwar;
    for(auto ep:game->enemies())for(auto e:ep->getUnits())
        if(e && e->exists() && e->isVisible() && e->isDetected() &&
           !e->isFlying() && !e->getType().isBuilding() &&
           e->getType().groundWeapon()!=BWAPI::WeaponTypes::None &&
           e->getDistance(line)<radius)return true;
    return false;
}

static void AIV56SiegeLineController() {
    auto& game=BWAPI::Broodwar;auto me=game->self();if(!me)return;
    const int now=game->getFrameCount();
    if(now<g_v56PreviousFrame) {
        g_v56SiegeLineIds.clear();g_v56LastOrder.clear();
        g_v56MovingTank=-1;g_v56LastBound=g_v56LastMetrics=-10000;
        g_v56MoveTarget=BWAPI::Positions::None;
    }
    g_v56PreviousFrame=now;
    if(now%12)return;

    std::vector<BWAPI::Unit> tanks;
    std::set<int> alive;
    for(auto u:me->getUnits())if(u && u->exists() && u->isCompleted() &&
        AIV7IsTank(u) && !g_v32GuardIds.count(u->getID())) {
        tanks.push_back(u);alive.insert(u->getID());
    }
    g_v56SiegeLineIds.swap(alive);
    if(tanks.empty()) {g_v56MovingTank=-1;return;}

    const BWAPI::Position home=AIV33AssemblyPoint();
    const BWAPI::Position enemy=AIV10AttackTarget();
    const bool canSiege=me->hasResearched(BWAPI::TechTypes::Tank_Siege_Mode);
    const bool pushing=g_v7State==AIV7_ATTACK && enemy.isValid() &&
                       tanks.size()>=2 && canSiege;

    // Before a coordinated push, form a static line at the current assembly
    // area. A single Tank remains defensive instead of walking out alone.
    if(!pushing) {
        g_v56MovingTank=-1;g_v56MoveTarget=BWAPI::Positions::None;
        for(size_t i=0;i<tanks.size();++i) {
            auto tank=tanks[i];
            BWAPI::Position post=home;
            if(enemy.isValid())post=AIV56PointOnLine(
                tank,home,enemy,160,(i%2?1:-1)*(int)(55+45*(i/2)));
            if(!post.isValid())post=home;
            if(tank->getType()==BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode) {
                if(tank->getDistance(post)>260 &&
                   !AIV56GroundContact(tank->getPosition(),430) &&
                   now-g_v56LastOrder[tank->getID()]>96 &&
                   tank->unsiege())g_v56LastOrder[tank->getID()]=now;
            } else if(tank->getDistance(post)>135) {
                if(now-g_v56LastOrder[tank->getID()]>72 && tank->move(post))
                    g_v56LastOrder[tank->getID()]=now;
            } else if(canSiege && now-g_v56LastOrder[tank->getID()]>48 && tank->siege())
                g_v56LastOrder[tank->getID()]=now;
        }
        return;
    }

    std::sort(tanks.begin(),tanks.end(),[&](BWAPI::Unit a,BWAPI::Unit b){
        return AIV56Progress(a->getPosition(),home,enemy)<
               AIV56Progress(b->getPosition(),home,enemy);
    });
    BWAPI::Unit front=tanks.back();
    int frontProgress=AIV56Progress(front->getPosition(),home,enemy);
    int enemyProgress=AIV56Progress(enemy,home,enemy);
    int sieged=0;
    for(auto tank:tanks)
        if(tank->getType()==BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)++sieged;
    bool contact=false;
    for(auto tank:tanks)
        if(AIV56GroundContact(tank->getPosition(),620)){contact=true;break;}

    BWAPI::Unit moving=nullptr;
    for(auto tank:tanks)if(tank->getID()==g_v56MovingTank)moving=tank;
    if(!moving){g_v56MovingTank=-1;g_v56MoveTarget=BWAPI::Positions::None;}
    if(moving) {
        const int id=moving->getID();
        const bool danger=AIV56GroundContact(moving->getPosition(),430);
        if(moving->getType()==BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode) {
            if(moving->getDistance(g_v56MoveTarget)<=125 || danger) {
                g_v56MovingTank=-1;g_v56MoveTarget=BWAPI::Positions::None;
            } else if(!danger && now-g_v56LastOrder[id]>72 && moving->unsiege())
                g_v56LastOrder[id]=now;
        } else if(danger || moving->getDistance(g_v56MoveTarget)<=105) {
            if(now-g_v56LastOrder[id]>36 && moving->siege())g_v56LastOrder[id]=now;
        } else if(now-g_v56LastOrder[id]>60 && moving->move(g_v56MoveTarget)) {
            g_v56LastOrder[id]=now;
        }
    }

    // Every non-moving Tank is cover. If another controller or a completed
    // morph left it mobile, restore Siege Mode instead of sending it forward.
    for(auto tank:tanks) {
        if(tank->getID()==g_v56MovingTank ||
           tank->getType()==BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)continue;
        if(now-g_v56LastOrder[tank->getID()]>48 && tank->siege())
            g_v56LastOrder[tank->getID()]=now;
    }

    // Start one short leapfrog bound only when the rest of the line is fully
    // deployed and no enemy is close enough to be baiting the formation.
    if(g_v56MovingTank<0 && !contact && sieged==(int)tanks.size() &&
       now-g_v56LastBound>=192 && frontProgress<enemyProgress-420) {
        BWAPI::Unit rear=nullptr;
        for(auto tank:tanks)if(tank!=front &&
            tank->getType()==BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode){rear=tank;break;}
        if(rear) {
            const int nextProgress=(std::min)(enemyProgress-420,frontProgress+185);
            const int side=(rear->getID()%2?80:-80);
            BWAPI::Position target=AIV56PointOnLine(rear,home,enemy,nextProgress,side);
            if(target.isValid()) {
                g_v56MovingTank=rear->getID();g_v56MoveTarget=target;g_v56LastBound=now;
                if(rear->unsiege())g_v56LastOrder[rear->getID()]=now;
            }
        }
    }

    // Infantry and Vultures screen the siege line but never pursue beyond it.
    front=tanks.back();frontProgress=AIV56Progress(front->getPosition(),home,enemy);
    int leashed=0;
    for(auto u:me->getUnits()) {
        if(!u || !u->exists() || !u->isCompleted() || u->isAttackFrame() ||
           g_v32GuardIds.count(u->getID()) || !g_v7Army.count(u->getID()))continue;
        const auto type=u->getType();
        if(type!=BWAPI::UnitTypes::Terran_Marine &&
           type!=BWAPI::UnitTypes::Terran_Vulture &&
           type!=BWAPI::UnitTypes::Terran_Medic)continue;
        const int allowance=type==BWAPI::UnitTypes::Terran_Vulture?240:
                            type==BWAPI::UnitTypes::Terran_Marine?165:60;
        const int progress=AIV56Progress(u->getPosition(),home,enemy);
        if(progress<=frontProgress+allowance)continue;
        const int targetProgress=frontProgress+(type==BWAPI::UnitTypes::Terran_Medic?0:90);
        BWAPI::Position leash=AIV56PointOnLine(u,home,enemy,targetProgress,0);
        if(leash.isValid() && now-u->getLastCommandFrame()>24 && u->move(leash))++leashed;
    }

    if(now==0 || now-g_v56LastMetrics>=240) {
        const bool firstMetrics=g_v56LastMetrics<0;
        g_v56LastMetrics=now;
        _mkdir(R"(C:\Users\User\starcraft-api\STARTcraft\AI_reports)");
        std::ofstream out(R"(C:\Users\User\starcraft-api\STARTcraft\AI_reports\AI_v56_siege_line.csv)",
                          firstMetrics?std::ios::trunc:std::ios::app);
        if(out) {
            if(firstMetrics)out<<"frame,seconds,tanks,sieged,moving,contact,front_progress,leashed\n";
            out<<now<<','<<game->elapsedTime()<<','<<tanks.size()<<','<<sieged<<','
               <<(g_v56MovingTank>=0?1:0)<<','<<(contact?1:0)<<','
               <<frontProgress<<','<<leashed<<'\n';
        }
    }
}

// Final TvP command owner: move to friendly anchors and hold. Never issue an
// attack-unit or attack-move command that can follow a retreating enemy.
static void AIV58HoldScreen() {
    auto& game=BWAPI::Broodwar;auto me=game->self();
    if(!me || AIV13OpponentRace()!=BWAPI::Races::Protoss)return;
    for(auto u:me->getUnits()) {
        if(!u || !u->exists() || !u->isCompleted() || u->isLoaded())continue;
        const auto type=u->getType();
        if(type!=BWAPI::UnitTypes::Terran_Marine && type!=BWAPI::UnitTypes::Terran_Vulture &&
           type!=BWAPI::UnitTypes::Terran_Goliath && type!=BWAPI::UnitTypes::Terran_Medic)continue;
        // Keep the reserved mine-screen task; its controller uses move/useTech.
        if(type==BWAPI::UnitTypes::Terran_Vulture && g_v55NaturalGuardIds.count(u->getID()))continue;
        BWAPI::Unit anchor=nullptr;int best=10000000;
        // A breached base takes precedence over the forward siege line.
        for(auto cc:me->getUnits())if(cc && cc->exists() && !cc->isFlying() &&
            cc->getType()==BWAPI::UnitTypes::Terran_Command_Center && u->getDistance(cc)<1200) {
            bool breach=false;
            for(auto ep:game->enemies())for(auto e:ep->getUnits())
                if(AIV23VisibleThreat(e) && !e->isFlying() && e->getDistance(cc)<460)breach=true;
            if(breach && u->getDistance(cc)<best){anchor=cc;best=u->getDistance(cc);}
        }
        if(!anchor)for(auto ally:me->getUnits()) {
            if(!ally || !ally->exists() || !ally->isCompleted() || ally->isFlying())continue;
            const auto t=ally->getType();
            if(t!=BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode &&
               t!=BWAPI::UnitTypes::Terran_Bunker)continue;
            const int d=u->getDistance(ally);
            if(d<best){anchor=ally;best=d;}
        }
        BWAPI::Position post=anchor?anchor->getPosition():AIV33AssemblyPoint();
        if(!post.isValid())continue;
        // Boarding a friendly bunker is a defensive order, not a pursuit.
        // Preserve it until loaded unless the base-breach lock forbids loading.
        auto boarding=u->getLastCommand().getTarget();
        if(game->getFrameCount()>g_v57NoLoadUntil && boarding && boarding->exists() &&
           boarding->getPlayer()==me && boarding->getType()==BWAPI::UnitTypes::Terran_Bunker)
            continue;
        if(u->getDistance(post)>160 && u->hasPath(post)) {
            const auto cmd=u->getLastCommand();
            if(cmd.getType()!=BWAPI::UnitCommandTypes::Move || cmd.getTargetPosition()!=post)
                u->move(post);
        } else if(u->getLastCommand().getType()!=BWAPI::UnitCommandTypes::Hold_Position)
            u->holdPosition();
    }
}

void StarterBot::onFrame()
{
    AIV353WatchBuilds();
    AIV18Dashboard(); // STARTCRAFT_AI_V18
    AIV19Diagnostics(); // STARTCRAFT_AI_V19
    AIV20BalanceMineralPatches(); // STARTCRAFT_AI_V20
    AIV31CorrectCargoDepot(); // STARTCRAFT_AI_V31
    AIV31WorkerFlowLog(); // STARTCRAFT_AI_V31
    AIV20MedicExitClearance(); // STARTCRAFT_AI_V20

    AIV16DetectionDirector(); // STARTCRAFT_AI_V16
    AIV22AcademyDirector(); // STARTCRAFT_AI_V22
    AIV24TechDirector(); // STARTCRAFT_AI_V24
    AIV30ObserveEnemyRace(); // STARTCRAFT_AI_V30_PRE_ECON
    AIV29StrategySense(); // STARTCRAFT_AI_V29_PRE_ECON
    AIV55PrepareNatural(); // STARTCRAFT_AI_V55
    AIV13EconomyDirector(); // STARTCRAFT_AI_V13
    AIV29MatchTick(); // STARTCRAFT_AI_V29_TIMELINE // STARTCRAFT_AI_V11: unified spending

    // Update our MapTools information
    m_mapTools.onFrame();

    // Send our idle workers to mine minerals so they don't just stand there
    sendIdleWorkersToMinerals();

    // Train more workers so we can gather more income
    trainAdditionalWorkers();

    // Build more supply if we are going to run out soon
    // v11 director handles AIV10ManageSupply
    // STARTCRAFT_AI_V1: Terran opening into a small marine attack.
    // v11 director handles AIV1BuildBarracks
    // v11 director handles AIV5BuildAcademy
    // v11 director handles AIV10TrainInfantry
    // AI v4: disabled v1 trickle-attack
    // STARTCRAFT_AI_V2: gas, tank production, and tank attack.
    // v11 director handles AIV2BuildRefinery
    // v11 director handles AIV2GatherGas
    // v11 director handles AIV2BuildFactory
    // v11 director handles AIV10BuildTanks
    // AI v4: disabled v2 trickle-attack
    // STARTCRAFT_AI_V3: stronger production, siege research, combat switching, field repairs.
    // v11 director handles AIV10BuildSecondFactory
    // v11 director handles AIV3ResearchSiegeMode
    AIV3ControlSiegeTanks();
    AIV3RepairTank();
    // STARTCRAFT_AI_V4: controlled rally and grouped attack.
    // STARTCRAFT_AI_V5: Academy, Medic production, economy and scouting.
    // v11 director handles AIV10BuildExtraBarracks
    // v11 director handles AIV10Expand
    /* v25 owns persistent scout; v6 one-shot disabled */
    AIV32DefenseAndWorkers(); // STARTCRAFT_AI_V32
    AIV33BaseMetrics(); // STARTCRAFT_AI_V33
    AIV10ArmyController(); // STARTCRAFT_AI_V10: enemy-expansion cleanup and retarget

    // Draw unit health bars, which brood war unfortunately does not do
    Tools::DrawUnitHealthBars();

    // Draw some relevent information to the screen to help us debug the bot
    drawDebugInformation();

    AIV17GateDirector(); // STARTCRAFT_AI_V17

    AIV24CombatAbilities(); // STARTCRAFT_AI_V24
    AIV37HighApmMicro(); // STARTCRAFT_AI_V37

    AIV25IntelligenceTick(); // STARTCRAFT_AI_V25
    AIV46AirDefense(); // STARTCRAFT_AI_V46
    AIV373EnemyExpansionResponse(); // STARTCRAFT_AI_V37_3
    AIV26EarlyDefenseAndRecon(); // STARTCRAFT_AI_V26
    AIV34UnitFlow(); // STARTCRAFT_AI_V34
    AIV34FlowLog(); // STARTCRAFT_AI_V34
    AIV56SiegeLineController(); // STARTCRAFT_AI_V56: final combat-order owner
    AIV23ReactiveDefense(); // v57 emergency infantry/SCV orders follow staging/loading.
    AIV58HoldScreen(); // final: emergency support must not turn into pursuit.
    AIV353WatchBuilds();
}

// Send our idle workers to mine minerals so they don't just stand there
void StarterBot::sendIdleWorkersToMinerals()
{
    // STARTCRAFT_AI_V31_SINGLE_MINING_DIRECTOR
    // AIV21MiningDirector() already runs in onFrame via AIV20BalanceMineralPatches.
    // Old fallback right-clicked the closest mineral anywhere (including the
    // main-base patch for an SCV stationed at a completed expansion).
    // The single director assigns idle SCVs on its next 24-frame update.
}

// Train more workers so we can gather more income
void StarterBot::trainAdditionalWorkers()
{
    AIV11TrainWorkers(); // STARTCRAFT_AI_V11: SCV saturation limit
}

// Build more supply if we are going to run out soon
void StarterBot::buildAdditionalSupply()
{
    // Get the amount of supply supply we currently have unused
    const int unusedSupply = Tools::GetTotalSupply(true) - BWAPI::Broodwar->self()->supplyUsed();

    // If we have a sufficient amount of supply, we don't need to do anything
    if (unusedSupply >= 2) { return; }

    // Otherwise, we are going to build a supply provider
    const BWAPI::UnitType supplyProviderType = BWAPI::Broodwar->self()->getRace().getSupplyProvider();

    const bool startedBuilding = Tools::BuildBuilding(supplyProviderType);
    if (startedBuilding)
    {
        BWAPI::Broodwar->printf("Started Building %s", supplyProviderType.getName().c_str());
    }
}

// Draw some relevent information to the screen to help us debug the bot
void StarterBot::drawDebugInformation()
{
    BWAPI::Broodwar->drawTextScreen(BWAPI::Position(10, 10), "Hello, World!\n");
    Tools::DrawUnitCommands();
    Tools::DrawUnitBoundingBoxes();
}

// Called whenever the game ends and tells you if you won or not
void StarterBot::onEnd(bool isWinner)
{
    AIV29MatchEnded(isWinner); // STARTCRAFT_AI_V29_FINISH

    std::cout << "We " << (isWinner ? "won!" : "lost!") << "\n";
}

// Called whenever a unit is destroyed, with a pointer to the unit
void StarterBot::onUnitDestroy(BWAPI::Unit unit)
{
	
}

// Called whenever a unit is morphed, with a pointer to the unit
// Zerg units morph when they turn into other units
void StarterBot::onUnitMorph(BWAPI::Unit unit)
{
	
}

// Called whenever a text is sent to the game by a user
void StarterBot::onSendText(std::string text) 
{ 
    if (text == "/map")
    {
        m_mapTools.toggleDraw();
    }
}

// Called whenever a unit is created, with a pointer to the destroyed unit
// Units are created in buildings like barracks before they are visible, 
// so this will trigger when you issue the build command for most units
void StarterBot::onUnitCreate(BWAPI::Unit unit)
{ 
	
}

// Called whenever a unit finished construction, with a pointer to the unit
void StarterBot::onUnitComplete(BWAPI::Unit unit)
{
	
}

// Called whenever a unit appears, with a pointer to the destroyed unit
// This is usually triggered when units appear from fog of war and become visible
void StarterBot::onUnitShow(BWAPI::Unit unit)
{ 
	
}

// Called whenever a unit gets hidden, with a pointer to the destroyed unit
// This is usually triggered when units enter the fog of war and are no longer visible
void StarterBot::onUnitHide(BWAPI::Unit unit)
{ 
	
}

// Called whenever a unit switches player control
// This usually happens when a dark archon takes control of a unit
void StarterBot::onUnitRenegade(BWAPI::Unit unit)
{ 
	
}
