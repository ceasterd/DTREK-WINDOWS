#include <math.h>
#include <stdio.h>
#include <string.h>

#include "dtrek_core.h"

static int failures = 0;
static int checks = 0;

static void check(int condition, const char *message)
{
    checks++;
    if (!condition) {
        failures++;
        fprintf(stderr, "FAIL: %s\n", message);
    }
}

static void check_int(int actual, int expected, const char *message)
{
    checks++;
    if (actual != expected) {
        failures++;
        fprintf(stderr, "FAIL: %s (expected %d, got %d)\n",
                message, expected, actual);
    }
}

static const DtrekShipSnapshot *ship_at(const DtrekGameSnapshot *snapshot,
                                        int slot)
{
    return &snapshot->ships[slot];
}

static int shield_sum(const DtrekShipSnapshot *ship)
{
    int i;
    int total = 0;

    for (i = 0; i < DTREK_NUM_SHIELDS; ++i) total += ship->shield[i];
    return total;
}

static int distance_squared(int x1, int y1, int x2, int y2)
{
    int dx = x1 - x2;
    int dy = y1 - y2;
    return dx * dx + dy * dy;
}

static int isqrt_floor(int n)
{
    int r;

    if (n <= 0) return 0;
    r = (int)sqrt((double)n);
    while ((r + 1) * (r + 1) <= n) r++;
    while (r * r > n) r--;
    return r;
}

static int blast_damage(int full_damage, int dist2)
{
    if (dist2 > DTREK_TORPEDO_DETONATE_DIST * DTREK_TORPEDO_DETONATE_DIST) {
        return 0;
    }
    if (dist2 > DTREK_TORPEDO_EXPLODE_DIST * DTREK_TORPEDO_EXPLODE_DIST) {
        return full_damage
            * (DTREK_TORPEDO_DETONATE_DIST - isqrt_floor(dist2))
            / (DTREK_TORPEDO_DETONATE_DIST - DTREK_TORPEDO_EXPLODE_DIST);
    }
    return full_damage;
}

static int count_torpedoes(const DtrekGameSnapshot *snapshot)
{
    int i;
    int count = 0;

    for (i = 0; i < DTREK_MAX_TORPEDOES; ++i) {
        if (snapshot->torpedoes[i].in_use) count++;
    }
    return count;
}

static int find_torpedo_by_owner(const DtrekGameSnapshot *snapshot, int owner)
{
    int i;

    for (i = 0; i < DTREK_MAX_TORPEDOES; ++i) {
        if (snapshot->torpedoes[i].in_use
            && snapshot->torpedoes[i].owner_slot == owner) {
            return i;
        }
    }
    return -1;
}

static int fire_torpedo(dtrek_engine *engine, int owner, int course,
                        DtrekTorpedoResult *result)
{
    DtrekTorpedoLaunch launch;

    memset(&launch, 0, sizeof(launch));
    launch.course = course;
    launch.target_slot = -1;
    launch.aim_x = -1;
    launch.aim_y = -1;
    launch.weapon_class = -1;
    return dtrek_fire_torpedo(engine, owner, &launch, result);
}

static int fire_tracking(dtrek_engine *engine, int owner, int course,
                         int target_slot, int weapon_class,
                         DtrekTorpedoResult *result)
{
    DtrekTorpedoLaunch launch;

    memset(&launch, 0, sizeof(launch));
    launch.course = course;
    launch.target_slot = target_slot;
    launch.aim_x = -1;
    launch.aim_y = -1;
    launch.weapon_class = weapon_class;
    return dtrek_fire_torpedo(engine, owner, &launch, result);
}

static int fire_at_point(dtrek_engine *engine, int owner, int course,
                         int weapon_class, int aim_x, int aim_y,
                         DtrekTorpedoResult *result)
{
    DtrekTorpedoLaunch launch;

    memset(&launch, 0, sizeof(launch));
    launch.course = course;
    launch.target_slot = -1;
    launch.aim_x = aim_x;
    launch.aim_y = aim_y;
    launch.weapon_class = weapon_class;
    return dtrek_fire_torpedo(engine, owner, &launch, result);
}

static void test_metadata_and_spawn(void)
{
    dtrek_engine *engine = dtrek_create(1989);
    DtrekGameSnapshot snapshot;
    int player;
    int enemy;

    check(engine != NULL, "engine created");
    if (engine == NULL) return;

    check_int((int)dtrek_abi_version(), DTREK_ABI_VERSION, "ABI version");
    check(dtrek_version_string() != NULL, "version string present");

    player = dtrek_spawn(engine, "Kirk", DTREK_TEAM_FEDERATION, 100000, 100000);
    enemy = dtrek_spawn(engine, "Targ", DTREK_TEAM_ROMULAN, 103000, 100000);
    check_int(player, 0, "player slot");
    check_int(enemy, 1, "enemy slot");

    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK, "snapshot");
    check_int(snapshot.tick, 0, "initial tick");
    check_int(snapshot.ship_count, 2, "ship count");
    check_int(snapshot.active_count, 2, "active count");

    check_int(ship_at(&snapshot, player)->hull, 83, "Reliant hull points");
    check_int(ship_at(&snapshot, player)->armor, 1, "Reliant armor");
    check_int(ship_at(&snapshot, player)->fuel, 8500, "Reliant fuel");
    check_int(ship_at(&snapshot, player)->shield[DTREK_SHIELD_FRONT],
              158, "front shield");
    check_int(ship_at(&snapshot, player)->beams_total, 4, "beam banks");
    check_int(ship_at(&snapshot, player)->beams_loaded, 4, "loaded beams");
    check_int(ship_at(&snapshot, player)->tubes_total, 5, "torpedo tubes");
    check_int(ship_at(&snapshot, player)->tubes_loaded, 5, "loaded tubes");
    check_int(ship_at(&snapshot, player)->tubes_reload, 0, "tubes idle");
    check_int(ship_at(&snapshot, player)->torpedoes_in_flight, 0,
              "no torpedoes in flight");
    check_int(snapshot.torpedo_count, 0, "no torpedo slots in use");
    check_int((int)sizeof(DtrekTorpedoSnapshot), 16 * 4,
              "torpedo snapshot is sixteen 32-bit fields");
    check_int(ship_at(&snapshot, player)->shields_up, 1, "shields up on spawn");
    check(strcmp(ship_at(&snapshot, player)->name, "Kirk") == 0, "ship name");

    dtrek_destroy(engine);
}

static void test_rng_determinism(void)
{
    dtrek_engine *a = dtrek_create(12345);
    dtrek_engine *b = dtrek_create(12345);
    int i;

    check(a != NULL && b != NULL, "rng engines created");
    if (a == NULL || b == NULL) {
        dtrek_destroy(a);
        dtrek_destroy(b);
        return;
    }

    for (i = 0; i < 12; ++i) {
        int value_a = dtrek_random_below(a, 1000);
        int value_b = dtrek_random_below(b, 1000);
        check_int(value_a, value_b, "lrand48 sequence is deterministic");
        check(value_a >= 0 && value_a < 1000, "rng value in range");
    }

    dtrek_destroy(a);
    dtrek_destroy(b);
}

static void test_movement_and_shields(void)
{
    dtrek_engine *engine = dtrek_create(77);
    DtrekGameSnapshot before;
    DtrekGameSnapshot after;
    DtrekPhaserResult shot;
    int player;
    int enemy;
    int fuel_before;
    int rear_before;
    int result;

    check(engine != NULL, "movement engine created");
    if (engine == NULL) return;

    player = dtrek_spawn(engine, "Movement", DTREK_TEAM_FEDERATION,
                         100000, 100000);
    enemy = dtrek_spawn(engine, "Target", DTREK_TEAM_ROMULAN,
                        103000, 100000);
    check_int(dtrek_snapshot(engine, &before), DTREK_OK, "before snapshot");

    check_int(dtrek_set_controls(engine, player, 64, 4), DTREK_OK,
              "set movement controls");
    check_int(dtrek_step(engine, 30), DTREK_OK, "step movement");
    check_int(dtrek_snapshot(engine, &after), DTREK_OK, "after snapshot");

    check(after.tick == 30, "tick advanced");
    check(ship_at(&after, player)->x > ship_at(&before, player)->x,
          "ship moved east");
    check(ship_at(&after, player)->speed >= 1, "ship accelerated");
    check(ship_at(&after, player)->fuel > 0
              && ship_at(&after, player)->fuel <= 8500,
          "fuel stayed in range");

    fuel_before = ship_at(&after, player)->fuel;
    rear_before = ship_at(&after, enemy)->shield[DTREK_SHIELD_REAR];

    result = dtrek_fire_phaser(engine, player, 64, &shot);
    check_int(result, 1, "phaser hit");
    check_int(shot.fired, 1, "phaser fired");
    check_int(shot.hit, 1, "phaser result marked hit");
    check_int(shot.target_slot, enemy, "phaser target slot");
    check_int(shot.beams_fired, 4, "all four banks fired");
    check(shot.damage > 0, "phaser did damage");
    check_int(shot.shield_index, DTREK_SHIELD_REAR, "rear shield facing");
    check_int(shot.shield_absorbed, shot.damage, "shield absorbed volley");
    check_int(shot.hull_damage, 0, "shield kept damage from hull");

    check_int(dtrek_snapshot(engine, &after), DTREK_OK, "post-fire snapshot");
    check_int(ship_at(&after, player)->fuel, fuel_before - shot.fuel_cost,
              "phaser fuel cost");
    check_int(ship_at(&after, enemy)->shield[DTREK_SHIELD_REAR],
              rear_before - shot.damage, "rear shield reduced");
    check_int(ship_at(&after, player)->beams_loaded, 0,
              "all banks unloaded after firing");
    check_int(dtrek_fire_phaser(engine, player, 64, &shot), DTREK_ERR_WEAPON,
              "cannot fire while reloading");

    dtrek_destroy(engine);
}

static void test_turn_rate(void)
{
    dtrek_engine *engine = dtrek_create(808);
    DtrekGameSnapshot snapshot;
    int player;

    check(engine != NULL, "turn engine created");
    if (engine == NULL) return;

    player = dtrek_spawn(engine, "Helm", DTREK_TEAM_FEDERATION,
                         100000, 100000);

    /* A stopped ship answers the helm immediately. */
    check_int(dtrek_set_controls(engine, player, 64, 0), DTREK_OK,
              "set stopped course");
    check_int(dtrek_step(engine, 1), DTREK_OK, "step stopped turn");
    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK,
              "stopped turn snapshot");
    check_int(ship_at(&snapshot, player)->course, 64,
              "stopped ship snaps to the new course");

    /* Warp 1 is the most maneuverable speed: a reversal takes a few ticks. */
    check_int(dtrek_set_controls(engine, player, 192, 1), DTREK_OK,
              "set warp 1 course");
    check_int(dtrek_step(engine, 4), DTREK_OK, "step warp 1 turn");
    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK,
              "warp 1 turn snapshot");
    check_int(ship_at(&snapshot, player)->course, 192,
              "warp 1 reverses course within four updates");

    /* At maximum warp the turn is slower, but still usable. */
    check_int(dtrek_set_controls(engine, player, 0, 6), DTREK_OK,
              "set max warp course");
    check_int(dtrek_step(engine, 100), DTREK_OK, "accelerate to max warp");
    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK,
              "max warp snapshot");
    check_int(ship_at(&snapshot, player)->speed, 6, "reached max warp");
    check_int(ship_at(&snapshot, player)->course, 0,
              "course settled at max warp");

    check_int(dtrek_set_controls(engine, player, 64, 6), DTREK_OK,
              "set max warp turn");
    check_int(dtrek_step(engine, 20), DTREK_OK, "step max warp turn");
    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK,
              "max warp turn snapshot");
    check(ship_at(&snapshot, player)->course > 0
              && ship_at(&snapshot, player)->course < 64,
          "max warp ship turns toward the new course");

    check_int(dtrek_step(engine, 80), DTREK_OK, "finish max warp turn");
    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK,
              "finished turn snapshot");
    check_int(ship_at(&snapshot, player)->course, 64,
              "max warp completes a 90 degree turn in under five seconds");

    dtrek_destroy(engine);
}

static void test_damage_and_explosion(void)
{
    dtrek_engine *engine = dtrek_create(123);
    DtrekGameSnapshot snapshot;
    DtrekPhaserResult shot;
    int player;
    int enemy;

    check(engine != NULL, "damage engine created");
    if (engine == NULL) return;

    player = dtrek_spawn(engine, "Shooter", DTREK_TEAM_FEDERATION,
                         100000, 100000);
    enemy = dtrek_spawn(engine, "Victim", DTREK_TEAM_ROMULAN,
                        103000, 100000);
    check_int(dtrek_set_shields(engine, enemy, 0), DTREK_OK,
              "lower victim shields");

    check_int(dtrek_fire_phaser(engine, player, 64, &shot), 1,
              "first unshielded hit");
    check(shot.damage == 72, "range 3000 volley damage is 72");
    check_int(shot.shield_absorbed, 0, "no shield absorption");
    check_int(shot.armor_absorbed, 1, "armor absorbed one point");
    check_int(shot.hull_damage, 71, "hull absorbed remainder");
    check_int(shot.target_destroyed, 0, "victim survived first volley");

    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK, "damage snapshot");
    check_int(ship_at(&snapshot, enemy)->armor, 0, "armor destroyed");
    check_int(ship_at(&snapshot, enemy)->hull, 12, "hull reduced to 12");

    check_int(dtrek_step(engine, 120), DTREK_OK, "reload all banks");
    check_int(dtrek_fire_phaser(engine, player, 64, &shot), 1,
              "second unshielded hit");
    check_int(shot.target_destroyed, 1, "victim destroyed");

    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK, "death snapshot");
    check_int(ship_at(&snapshot, enemy)->status, DTREK_STATUS_EXPLODING,
              "victim is exploding");
    check_int(snapshot.active_count, 1, "only one active ship");

    check_int(dtrek_step(engine, 10), DTREK_OK, "finish explosion");
    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK, "dead snapshot");
    check_int(ship_at(&snapshot, enemy)->status, DTREK_STATUS_DEAD,
              "victim is dead");

    dtrek_destroy(engine);
}

static void test_subsystem_damage_and_repair(void)
{
    dtrek_engine *engine = dtrek_create(1234);
    DtrekGameSnapshot snapshot;
    DtrekPhaserResult shot;
    const DtrekShipSnapshot *victim;
    int player;
    int enemy;
    int i;
    int system_size;
    int intact_beams;
    int intact_torps;
    int hull_before;
    int armor_before;

    check(engine != NULL, "subsystem engine created");
    if (engine == NULL) return;

    player = dtrek_spawn(engine, "Shooter", DTREK_TEAM_FEDERATION,
                         100000, 100000);
    enemy = dtrek_spawn(engine, "Victim", DTREK_TEAM_KLINGON,
                        103000, 100000);
    check(player >= 0 && enemy >= 0, "subsystem ships spawned");
    if (player < 0 || enemy < 0) {
        dtrek_destroy(engine);
        return;
    }

    check_int(dtrek_set_shields(engine, enemy, 0), DTREK_OK,
              "subsystem victim lowers shields");
    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK,
              "subsystem initial snapshot");
    victim = ship_at(&snapshot, enemy);

    /* Armor #0 and hull #1 come first; the other systems follow. */
    check_int(victim->system_count, 15, "hit table holds fifteen units");
    check_int(victim->system_size, 105, "hit table size excludes armor and hull");
    check_int(victim->systems[0].item, DTREK_SYS_ARMOR, "armor is unit zero");
    check_int(victim->systems[1].item, DTREK_SYS_HULL, "hull is unit one");
    check_int(victim->systems[2].item, DTREK_SYS_ENGINE, "engine is unit two");
    check_int(victim->systems[3].item, DTREK_SYS_FUEL, "fuel is unit three");

    check_int(victim->systems[0].size, 1, "armor starts at one kton");
    check_int(victim->systems[1].size, 83, "hull starts at eighty-three");
    check_int(victim->systems[2].size, 27, "engine starts at twenty-seven");
    check_int(victim->systems[3].size, 17, "fuel starts at seventeen ktons");

    check_int(victim->systems[0].repair_rate, 0,
              "armor has no self repair");
    check_int(victim->systems[0].planet_repair_rate, 1500,
              "armor repairs only at a base");
    check_int(victim->systems[1].repair_rate, 562, "hull self repair rate");
    check_int(victim->systems[1].planet_repair_rate, 3000,
              "hull base repair rate");
    check_int(victim->systems[2].repair_rate, 140, "engine self repair rate");
    check_int(victim->systems[2].planet_repair_rate, 450,
              "engine base repair rate");
    check_int(victim->systems[3].repair_rate, 112, "fuel self repair rate");
    check_int(victim->systems[3].planet_repair_rate, 450,
              "fuel base repair rate");

    for (i = 0; i < 4; ++i) {
        const DtrekSystemSnapshot *unit = &victim->systems[4 + i];
        check_int(unit->item, DTREK_SYS_BEAM_NUM, "beam bank item");
        check_int(unit->subitem, i, "beam bank number");
        check_int(unit->size, 6, "beam bank size");
        check_int(unit->repair_rate, 168, "beam self repair rate");
        check_int(unit->planet_repair_rate, 540, "beam base repair rate");
    }

    for (i = 0; i < 5; ++i) {
        const DtrekSystemSnapshot *unit = &victim->systems[8 + i];
        check_int(unit->item, DTREK_SYS_TORP_NUM, "torpedo tube item");
        check_int(unit->subitem, i, "torpedo tube number");
        check_int(unit->size, 5, "torpedo tube size");
        check_int(unit->repair_rate, 84, "tube self repair rate");
        check_int(unit->planet_repair_rate, 270, "tube base repair rate");
    }

    check_int(victim->systems[13].item, DTREK_SYS_DILITHIUM,
              "dilithium follows the tubes");
    check_int(victim->systems[13].size, 9, "dilithium size");
    check_int(victim->systems[13].repair_rate, 39, "dilithium self rate");
    check_int(victim->systems[13].planet_repair_rate, 150,
              "dilithium base rate");
    check_int(victim->systems[14].item, DTREK_SYS_CARGO,
              "cargo is the last unit");
    check_int(victim->systems[14].size, 3, "cargo size");
    check_int(victim->systems[14].repair_rate, 168, "cargo self rate");
    check_int(victim->systems[14].planet_repair_rate, 450,
              "cargo base rate");

    check_int(dtrek_fire_phaser(engine, player, 64, &shot), 1,
              "subsystem volley fired");
    check_int(shot.damage, 72, "subsystem volley damage");
    check_int(shot.armor_absorbed, 1, "armor absorbs the first point");
    check_int(shot.hull_damage, 71, "hull takes the remainder");

    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK,
              "subsystem damage snapshot");
    victim = ship_at(&snapshot, enemy);
    check_int(victim->armor, 0, "armor is gone");
    check_int(victim->hull, 12, "hull dropped to twelve");
    check_int(victim->systems[1].size, 12, "hull unit matches the ship");

    /*
     * The original spills the full post-armor figure over the other systems,
     * without subtracting what the hull absorbed, so subsystems take damage
     * even when the hull is still intact.
     */
    check(victim->system_size < 105,
          "hull damage does not consume the subsystem spill");

    system_size = 0;
    intact_beams = 0;
    intact_torps = 0;
    for (i = 0; i < victim->system_count; ++i) {
        const DtrekSystemSnapshot *unit = &victim->systems[i];

        if (unit->item != DTREK_SYS_ARMOR && unit->item != DTREK_SYS_HULL) {
            system_size += unit->size;
        }
        if (unit->item == DTREK_SYS_BEAM_NUM
            && unit->size == unit->size_max) {
            ++intact_beams;
        }
        if (unit->item == DTREK_SYS_TORP_NUM
            && unit->size == unit->size_max) {
            ++intact_torps;
        }
        if (unit->item == DTREK_SYS_ENGINE
            && unit->size < unit->size_max) {
            check_int(victim->engine_health,
                      unit->size * 256 / unit->size_max,
                      "engine health follows the engine unit");
        }
    }
    check_int(victim->system_size, system_size,
              "system size tracks the surviving units");
    check_int(victim->beams_total, intact_beams,
              "destroyed beam banks leave the ship");
    check_int(victim->tubes_total, intact_torps,
              "destroyed tubes leave the ship");

    /* Self repair runs on the original thirty-update fuse. */
    hull_before = victim->hull;
    armor_before = victim->armor;
    check_int(dtrek_step(engine, 29), DTREK_OK, "subsystem pre-fuse step");
    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK,
              "subsystem pre-fuse snapshot");
    check_int(ship_at(&snapshot, enemy)->hull, hull_before,
              "no repair before the fuse");
    check_int(dtrek_step(engine, 1), DTREK_OK, "subsystem fuse step");
    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK,
              "subsystem fuse snapshot");
    victim = ship_at(&snapshot, enemy);
    check_int(victim->hull, hull_before + 2,
              "self repair restores the hull rate");
    check_int(victim->armor, armor_before,
              "armor cannot self repair");

    /* At a friendly repair base the planet rate is added on the fuse. */
    check_int(dtrek_place_planet(engine, 0, DTREK_PLANET_REPAIR,
                                 DTREK_TEAM_KLINGON, 0,
                                 victim->x, victim->y, "Repair Base"),
              DTREK_OK, "subsystem repair base placed");
    check_int(dtrek_orbit(engine, enemy, 0), 0, "subsystem orbit entered");
    check_int(dtrek_set_repair(engine, enemy, 1), DTREK_OK,
              "subsystem damage control enabled");

    hull_before = victim->hull;
    armor_before = victim->armor;
    check_int(dtrek_step(engine, 29), DTREK_OK, "base pre-fuse step");
    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK,
              "base pre-fuse snapshot");
    check_int(ship_at(&snapshot, enemy)->hull, hull_before,
              "base repair also waits for the fuse");
    check_int(dtrek_step(engine, 1), DTREK_OK, "base fuse step");
    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK,
              "base fuse snapshot");
    victim = ship_at(&snapshot, enemy);
    check_int(victim->hull, hull_before + 16,
              "base repair adds the planet hull rate");
    check_int(victim->armor, armor_before + 1,
              "base repair restores armor");

    dtrek_destroy(engine);
}

static void test_torpedo_launch_and_reload(void)
{
    dtrek_engine *engine = dtrek_create(42);
    DtrekGameSnapshot snapshot;
    DtrekTorpedoResult shot;
    const DtrekTorpedoSnapshot *torp;
    int player;
    int first_slot;
    int i;
    int result;

    check(engine != NULL, "torpedo engine created");
    if (engine == NULL) return;

    player = dtrek_spawn(engine, "Kirk", DTREK_TEAM_FEDERATION,
                         100000, 100000);
    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK,
              "pre-launch snapshot");
    check_int(ship_at(&snapshot, player)->tubes_loaded, 5, "five loaded tubes");
    check_int(ship_at(&snapshot, player)->tubes_total, 5, "five total tubes");

    result = fire_torpedo(engine, player, 64, &shot);
    check_int(result, 1, "torpedo launched");
    check_int(shot.fired, 1, "torpedo result fired");
    check_int(shot.owner_slot, player, "torpedo owner");
    check_int(shot.weapon_class, DTREK_WPN_PHOTON, "photon torpedo class");
    check_int(shot.damage, 42, "torpedo damage");
    check_int(shot.speed, 14, "torpedo speed");
    check_int(shot.fuel_cost, 420, "torpedo fuel cost");
    check_int(shot.tubes_total, 5, "result tube total");
    check_int(shot.tubes_loaded, 4, "one tube emptied");
    check(shot.fuse >= 20 && shot.fuse <= 49,
          "fuse is duration plus 0..29 random ticks");

    first_slot = shot.torpedo_slot;
    check(first_slot >= player * DTREK_MAX_TORPEDOES_PER_SHIP
              && first_slot < (player + 1) * DTREK_MAX_TORPEDOES_PER_SHIP,
          "torpedo uses the owner's slot block");

    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK,
              "post-launch snapshot");
    check_int(ship_at(&snapshot, player)->fuel, 8500 - 420,
              "launch consumed torpedo fuel");
    check_int(ship_at(&snapshot, player)->tubes_loaded, 4,
              "snapshot reports four loaded tubes");
    check_int(ship_at(&snapshot, player)->tubes_unloaded, 1,
              "snapshot reports one empty tube");
    check_int(ship_at(&snapshot, player)->tubes_reload, 20,
              "tube reload started");
    check_int(ship_at(&snapshot, player)->torpedoes_in_flight, 1,
              "one torpedo in flight");
    check_int(snapshot.torpedo_count, 1, "one torpedo slot in use");

    torp = &snapshot.torpedoes[first_slot];
    check_int(torp->in_use, 1, "torpedo slot in use");
    check_int(torp->status, DTREK_TORP_MOVE, "torpedo is moving");
    check_int(torp->owner_slot, player, "torpedo snapshot owner");
    check_int(torp->team, DTREK_TEAM_FEDERATION, "torpedo team");
    check_int(torp->x, 100000, "torpedo starts at the ship");
    check_int(torp->y, 100000, "torpedo starts at the ship");
    check_int(torp->course, 64, "torpedo course");
    check_int(torp->speed, 14, "torpedo snapshot speed");
    check_int(torp->damage, 42, "torpedo snapshot damage");
    check_int(torp->fuse, shot.fuse, "torpedo snapshot fuse");
    check_int(torp->arm, 0, "photon torpedoes arm immediately");
    check_int(torp->weapon_class, DTREK_WPN_PHOTON, "torpedo snapshot class");
    check_int(torp->target_slot, -1, "unguided torpedo has no target");

    /* All five tubes may fire before the first reload completes. */
    for (i = 1; i < 5; ++i) {
        result = fire_torpedo(engine, player, 64, &shot);
        check_int(result, 1, "rapid torpedo launch");
        check_int(shot.tubes_loaded, 4 - i, "tube count drops per launch");
    }

    result = fire_torpedo(engine, player, 64, &shot);
    check_int(result, DTREK_ERR_WEAPON, "sixth launch rejected");
    check(result != 0, "rejected launch is reported");

    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK,
              "five-torpedo snapshot");
    check_int(snapshot.torpedo_count, 5, "five torpedoes in flight");
    check_int(ship_at(&snapshot, player)->tubes_loaded, 0,
              "all tubes empty");
    check_int(ship_at(&snapshot, player)->fuel, 8500 - 5 * 420,
              "five launches consumed five times the cost");

    check_int(dtrek_step(engine, 19), DTREK_OK, "step before reload");
    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK,
              "pre-reload snapshot");
    check_int(ship_at(&snapshot, player)->tubes_loaded, 0,
              "reload takes the full twenty ticks");

    check_int(dtrek_step(engine, 1), DTREK_OK, "finish first reload");
    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK,
              "first reload snapshot");
    check_int(ship_at(&snapshot, player)->tubes_loaded, 1,
              "one tube reloaded");

    check_int(dtrek_step(engine, 80), DTREK_OK, "finish all reloads");
    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK,
              "full reload snapshot");
    check_int(ship_at(&snapshot, player)->tubes_loaded, 5,
              "all five tubes reloaded");
    check_int(ship_at(&snapshot, player)->torpedoes_in_flight, 0,
              "no torpedoes remain in flight");
    check_int(snapshot.torpedo_count, 0, "all torpedo slots free");

    dtrek_destroy(engine);
}

static void test_torpedo_blast_and_friendly_fire(void)
{
    dtrek_engine *engine = dtrek_create(2024);
    DtrekGameSnapshot before;
    DtrekGameSnapshot after;
    DtrekTorpedoResult shot;
    const DtrekTorpedoSnapshot *torp;
    int player;
    int target;
    int bystander;
    int slot;
    int dist_player;
    int dist_target;
    int dist_bystander;
    int expected_player;
    int expected_target;
    int expected_bystander;

    check(engine != NULL, "blast engine created");
    if (engine == NULL) return;

    player = dtrek_spawn(engine, "Kirk", DTREK_TEAM_FEDERATION,
                         100000, 100000);
    target = dtrek_spawn(engine, "Targ", DTREK_TEAM_ROMULAN,
                         100200, 100000);
    bystander = dtrek_spawn(engine, "Bystander", DTREK_TEAM_ROMULAN,
                            100350, 100500);

    check_int(dtrek_snapshot(engine, &before), DTREK_OK, "blast before");
    check_int(fire_torpedo(engine, player, 64, &shot), 1,
              "blast torpedo launched");
    check_int(dtrek_step(engine, 1), DTREK_OK, "blast tick");

    check_int(dtrek_snapshot(engine, &after), DTREK_OK, "blast after");
    slot = find_torpedo_by_owner(&after, player);
    check(slot >= 0, "exploding torpedo is still tracked");
    if (slot < 0) {
        dtrek_destroy(engine);
        return;
    }

    torp = &after.torpedoes[slot];
    check_int(torp->status, DTREK_TORP_EXPLODE, "torpedo detonated");
    check_int(torp->fuse, 10, "detonation lasts ten ticks");

    dist_player = distance_squared(
        torp->x, torp->y,
        ship_at(&before, player)->x, ship_at(&before, player)->y);
    dist_target = distance_squared(
        torp->x, torp->y,
        ship_at(&before, target)->x, ship_at(&before, target)->y);
    dist_bystander = distance_squared(
        torp->x, torp->y,
        ship_at(&before, bystander)->x, ship_at(&before, bystander)->y);

    expected_player = blast_damage(42, dist_player);
    expected_target = blast_damage(42, dist_target);
    expected_bystander = blast_damage(42, dist_bystander);

    check(dist_player < DTREK_TORPEDO_EXPLODE_DIST
              * DTREK_TORPEDO_EXPLODE_DIST,
          "owner is inside the full-damage radius");
    check(dist_target < DTREK_TORPEDO_EXPLODE_DIST
              * DTREK_TORPEDO_EXPLODE_DIST,
          "target is inside the full-damage radius");
    check(dist_bystander > DTREK_TORPEDO_EXPLODE_DIST
              * DTREK_TORPEDO_EXPLODE_DIST
              && dist_bystander < DTREK_TORPEDO_DETONATE_DIST
              * DTREK_TORPEDO_DETONATE_DIST,
          "bystander is in the falloff band");

    check_int(expected_player, 42, "owner takes full blast damage");
    check_int(expected_target, 42, "target takes full blast damage");
    check(expected_bystander > 0 && expected_bystander < 42,
          "falloff damage is partial");

    check_int(shield_sum(ship_at(&after, player)),
              shield_sum(ship_at(&before, player)) - expected_player,
              "friendly fire damaged the launching ship");
    check_int(shield_sum(ship_at(&after, target)),
              shield_sum(ship_at(&before, target)) - expected_target,
              "direct target took full blast damage");
    check_int(shield_sum(ship_at(&after, bystander)),
              shield_sum(ship_at(&before, bystander)) - expected_bystander,
              "bystander took falloff damage");
    check_int(ship_at(&after, target)->hull,
              ship_at(&before, target)->hull,
              "blast did not reach the hull through the shield");

    check_int(dtrek_step(engine, 11), DTREK_OK, "finish detonation");
    check_int(dtrek_snapshot(engine, &after), DTREK_OK,
              "post-detonation snapshot");
    check_int(after.torpedo_count, 0, "detonation cleared the torpedo slot");

    dtrek_destroy(engine);
}

static void test_torpedo_point_defense(void)
{
    dtrek_engine *engine = dtrek_create(4242);
    DtrekGameSnapshot before;
    DtrekGameSnapshot after;
    DtrekTorpedoResult launch;
    DtrekPhaserResult shot;
    const DtrekTorpedoSnapshot *torp;
    int player;
    int enemy;
    int slot;
    int range;
    int expected_damage;

    check(engine != NULL, "point-defense engine created");
    if (engine == NULL) return;

    player = dtrek_spawn(engine, "Kirk", DTREK_TEAM_FEDERATION,
                         100000, 100000);
    enemy = dtrek_spawn(engine, "Targ", DTREK_TEAM_ROMULAN,
                        103000, 100000);

    check_int(dtrek_set_ordnance(engine, enemy, DTREK_WPN_PHOTON), DTREK_OK,
              "enemy selects a photon for the interception test");
    check_int(fire_torpedo(engine, enemy, 192, &launch), 1,
              "enemy torpedo launched");
    check_int(launch.owner_slot, enemy, "enemy owns the torpedo");
    check_int(dtrek_step(engine, 5), DTREK_OK, "enemy torpedo closes");

    check_int(dtrek_snapshot(engine, &before), DTREK_OK,
              "point-defense before");
    slot = find_torpedo_by_owner(&before, enemy);
    check(slot >= 0, "incoming torpedo tracked");
    if (slot < 0) {
        dtrek_destroy(engine);
        return;
    }

    torp = &before.torpedoes[slot];
    check_int(torp->status, DTREK_TORP_MOVE, "incoming torpedo is moving");
    range = isqrt_floor(distance_squared(
        torp->x, torp->y,
        ship_at(&before, player)->x, ship_at(&before, player)->y));
    check(range > DTREK_TORPEDO_DETONATE_DIST && range < 2000,
          "incoming torpedo is in beam range but outside the blast");
    expected_damage = 30 * 2 - range / 250;
    if (expected_damage <= 0) expected_damage = 1;

    check_int(dtrek_fire_phaser(engine, player, 64, &shot), 1,
              "point-defense phaser fired");
    check_int(shot.fired, 1, "point-defense volley fired");
    check_int(shot.hit, 1, "point-defense volley connected");
    check_int(shot.target_is_torpedo, 1, "phaser selected the torpedo");
    check_int(shot.torpedo_slot, slot, "phaser reported torpedo slot");
    check_int(shot.torpedo_team, DTREK_TEAM_ROMULAN,
              "phaser reported torpedo team");
    check_int(shot.target_slot, -1, "no ship was targeted");
    check_int(shot.beams_fired, 1, "interception spends one beam");
    check_int(shot.fuel_cost, 240, "interception spends one beam of fuel");
    check_int(shot.damage, expected_damage,
              "photon torpedo takes double beam damage");
    check(shot.damage > torp->damage, "beam damage exceeds torpedo damage");
    check_int(shot.torpedo_destroyed, 1, "torpedo destroyed");
    check_int(shot.torpedo_damage, shot.damage,
              "photon torpedo damage reported");

    check_int(dtrek_snapshot(engine, &after), DTREK_OK,
              "point-defense after");
    check_int(after.torpedoes[slot].status, DTREK_TORP_EXPLODE,
              "intercepted torpedo detonates");
    check_int(ship_at(&after, player)->beams_loaded, 3,
              "one phaser bank was unloaded");
    check_int(ship_at(&after, player)->fuel, 8500 - 240,
              "one beam of fuel was spent");
    check_int(shield_sum(ship_at(&after, player)),
              shield_sum(ship_at(&before, player)),
              "detonation outside DETDIST left the player untouched");
    check_int(shield_sum(ship_at(&after, enemy)),
              shield_sum(ship_at(&before, enemy)),
              "the hostile ship was not hit");

    check_int(dtrek_step(engine, 11), DTREK_OK, "finish interception");
    check_int(dtrek_snapshot(engine, &after), DTREK_OK,
              "post-interception snapshot");
    check_int(after.torpedo_count, 0, "interception cleared the slot");

    dtrek_destroy(engine);
}

static void test_torpedo_expiry_and_bounds(void)
{
    dtrek_engine *engine = dtrek_create(7);
    DtrekGameSnapshot snapshot;
    DtrekTorpedoResult shot;
    int player;
    int slot;

    check(engine != NULL, "expiry engine created");
    if (engine == NULL) return;

    player = dtrek_spawn(engine, "Kirk", DTREK_TEAM_FEDERATION,
                         100000, 100000);
    check_int(fire_torpedo(engine, player, 64, &shot), 1,
              "expiry torpedo launched");
    check_int(dtrek_step(engine, 60), DTREK_OK, "run out the fuse");
    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK,
              "expiry snapshot");
    check_int(snapshot.torpedo_count, 0, "untriggered torpedo expired");
    check_int(ship_at(&snapshot, player)->tubes_loaded, 5,
              "tubes reloaded after the shot");
    check_int(ship_at(&snapshot, player)->torpedoes_in_flight, 0,
              "no torpedoes in flight after expiry");

    dtrek_destroy(engine);

    /* A torpedo that crosses the galaxy edge is clamped and removed. */
    engine = dtrek_create(8);
    check(engine != NULL, "boundary engine created");
    if (engine == NULL) return;

    player = dtrek_spawn(engine, "Edge", DTREK_TEAM_FEDERATION,
                         100, 100000);
    check_int(fire_torpedo(engine, player, 192, &shot), 1,
              "westward torpedo launched");
    check_int(dtrek_step(engine, 1), DTREK_OK, "torpedo crosses the edge");
    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK,
              "edge snapshot");
    slot = find_torpedo_by_owner(&snapshot, player);
    check(slot >= 0, "edge torpedo still tracked");
    if (slot >= 0) {
        check_int(snapshot.torpedoes[slot].x, 0, "torpedo clamped to the edge");
        check_int(snapshot.torpedoes[slot].status, DTREK_TORP_OFF,
                  "torpedo marked off after leaving the galaxy");
    }

    check_int(dtrek_step(engine, 1), DTREK_OK, "off state resolves");
    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK,
              "free snapshot");
    check_int(snapshot.torpedo_count, 0, "edge torpedo freed");

    dtrek_destroy(engine);
}

static void test_ordnance_profiles(void)
{
    dtrek_engine *engine = dtrek_create(1989);
    DtrekGameSnapshot snapshot;
    DtrekOrdnanceSpec spec;
    int fed;
    int rom;
    int klingon;

    check(engine != NULL, "ordnance engine created");
    if (engine == NULL) return;

    fed = dtrek_spawn(engine, "Fed", DTREK_TEAM_FEDERATION,
                      100000, 100000);
    rom = dtrek_spawn(engine, "Rom", DTREK_TEAM_ROMULAN,
                      101000, 100000);
    klingon = dtrek_spawn(engine, "Klingon", DTREK_TEAM_KLINGON,
                          102000, 100000);

    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK,
              "ordnance snapshot");
    check_int(ship_at(&snapshot, fed)->torpedo_class, DTREK_WPN_PHOTON,
              "Federation defaults to photons");
    check_int(ship_at(&snapshot, rom)->torpedo_class, DTREK_WPN_PLASMA,
              "Romulans default to plasma");
    check_int(ship_at(&snapshot, klingon)->torpedo_class, DTREK_WPN_MISSILE,
              "Klingons default to missiles");
    check(strcmp(ship_at(&snapshot, rom)->ordnance.name, "Plasma torpedo")
              == 0,
          "snapshot carries the selected profile name");
    check_int(ship_at(&snapshot, rom)->ordnance.damage, 110,
              "snapshot carries the selected profile damage");

    check_int(dtrek_ordnance_spec(engine, DTREK_WPN_PHOTON, &spec), DTREK_OK,
              "photon profile");
    check_int(spec.speed, 14, "photon speed");
    check_int(spec.damage, 42, "photon damage");
    check_int(spec.duration, 20, "photon duration");
    check_int(spec.reload, 20, "photon reload");
    check_int(spec.decay, 0, "photon decay");
    check_int(spec.fuel_cost, 420, "photon fuel");
    check_int(spec.arm, 0, "photon arms immediately");
    check_int(spec.tracking, 0, "photon is unguided");

    check_int(dtrek_ordnance_spec(engine, DTREK_WPN_MISSILE, &spec), DTREK_OK,
              "missile profile");
    check_int(spec.speed, 8, "missile speed");
    check_int(spec.damage, 21, "missile damage");
    check_int(spec.duration, 50, "missile duration");
    check_int(spec.reload, 30, "missile reload");
    check_int(spec.fuel_cost, 210, "missile fuel");
    check_int(spec.tracking, 1, "missile is guided");

    check_int(dtrek_ordnance_spec(engine, DTREK_WPN_PLASMA, &spec), DTREK_OK,
              "plasma profile");
    check_int(spec.speed, 8, "plasma speed");
    check_int(spec.damage, 110, "plasma damage");
    check_int(spec.duration, 100, "plasma duration");
    check_int(spec.reload, 60, "plasma reload");
    check_int(spec.decay, 8, "plasma decay");
    check_int(spec.fuel_cost, 1100, "plasma fuel");
    check_int(spec.tracking, 1, "plasma is guided");

    check_int(dtrek_ordnance_spec(engine, DTREK_WPN_CHAFF, &spec), DTREK_OK,
              "chaff profile");
    check_int(spec.speed, 0, "chaff uses the ship speed");
    check_int(spec.damage, 19, "chaff damage");
    check_int(spec.arm, 10, "chaff arming delay");
    check_int(spec.tracking, 0, "chaff is unguided");

    check_int(dtrek_ordnance_spec(engine, DTREK_WPN_MINE, &spec), DTREK_OK,
              "mine profile");
    check_int(spec.speed, 0, "mine is stationary");
    check_int(spec.damage, 35, "mine damage");
    check_int(spec.arm, 20, "mine arming delay");
    check_int(spec.tracking, 0, "mine is unguided");

    check_int((int)sizeof(DtrekOrdnanceSpec),
              9 * 4 + DTREK_ORDNANCE_NAME_LENGTH,
              "ordnance spec layout is nine words plus a name");
    check_int((int)sizeof(DtrekTorpedoLaunch), 5 * 4,
              "launch request is five 32-bit fields");

    check_int(dtrek_ordnance_spec(engine, DTREK_WPN_PHASER, &spec),
              DTREK_ERR_ARGUMENT, "beam class has no ordnance profile");
    check_int(dtrek_ordnance_spec(engine, DTREK_WPN_PHOTON, NULL),
              DTREK_ERR_ARGUMENT, "profile requires an output");

    check_int(dtrek_set_ordnance(engine, fed, DTREK_WPN_CHAFF), DTREK_OK,
              "select chaff");
    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK,
              "chaff selection snapshot");
    check_int(ship_at(&snapshot, fed)->torpedo_class, DTREK_WPN_CHAFF,
              "chaff selection reflected in snapshot");
    check_int(ship_at(&snapshot, fed)->ordnance.arm, 10,
              "chaff selection carries the profile");

    check_int(dtrek_set_ordnance(engine, fed, DTREK_WPN_PHASER),
              DTREK_ERR_ARGUMENT, "cannot select a beam as ordnance");
    check_int(dtrek_set_ordnance(engine, DTREK_MAX_PLAYERS,
                                 DTREK_WPN_PHOTON),
              DTREK_ERR_SHIP, "ordnance selection requires a ship");

    dtrek_destroy(engine);
}

static void test_missile_tracking(void)
{
    dtrek_engine *engine = dtrek_create(777);
    DtrekGameSnapshot snapshot;
    DtrekTorpedoResult shot;
    DtrekPhaserResult volley;
    const DtrekTorpedoSnapshot *torp;
    int player;
    int enemy;

    check(engine != NULL, "missile engine created");
    if (engine == NULL) return;

    player = dtrek_spawn(engine, "Kirk", DTREK_TEAM_FEDERATION,
                         100000, 100000);
    enemy = dtrek_spawn(engine, "Targ", DTREK_TEAM_ROMULAN,
                        100000, 102000);

    check_int(dtrek_set_ordnance(engine, player, DTREK_WPN_MISSILE),
              DTREK_OK, "player selects missiles");
    check_int(fire_tracking(engine, player, 64, enemy, DTREK_WPN_MISSILE,
                            &shot),
              1, "locked missile launched");
    check_int(shot.weapon_class, DTREK_WPN_MISSILE, "missile class");
    check_int(shot.target_slot, enemy, "missile carries the lock");
    check_int(shot.tracking, 1, "missile entered tracking mode");
    check_int(shot.arm, 0, "missile arms immediately");
    check_int(shot.damage, 21, "missile damage");
    check_int(shot.speed, 8, "missile speed");
    check_int(shot.fuel_cost, 210, "missile fuel");
    check(shot.fuse >= 50 && shot.fuse <= 79,
          "missile fuse is duration plus 0..29 random ticks");
    check_int(shot.tubes_loaded, 4, "missile consumed one tube");

    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK,
              "missile launch snapshot");
    torp = &snapshot.torpedoes[shot.torpedo_slot];
    check_int(torp->status, DTREK_TORP_TRACK, "missile starts tracking");
    check_int(torp->target_slot, enemy, "missile snapshot target");
    check_int(torp->course, 64, "missile starts on the launch course");

    check_int(dtrek_step(engine, 1), DTREK_OK, "missile guidance tick");
    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK,
              "guided snapshot");
    torp = &snapshot.torpedoes[shot.torpedo_slot];
    check_int(torp->course, 128, "missile turned south toward the target");
    check(torp->y > 100000, "missile moved toward the target");
    check(torp->x <= 100000, "missile course follows the target");

    /* Kill the locked target; the missile must fall back to straight flight. */
    check_int(dtrek_set_shields(engine, enemy, 0), DTREK_OK,
              "lower target screens");
    check_int(dtrek_fire_phaser(engine, player, 128, &volley), 1,
              "phaser destroys the locked target");
    check_int(volley.target_destroyed, 1, "target destruction confirmed");
    check_int(dtrek_step(engine, 1), DTREK_OK, "missile sees the dead target");
    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK,
              "demoted snapshot");
    torp = &snapshot.torpedoes[shot.torpedo_slot];
    check_int(torp->status, DTREK_TORP_STRAIGHT,
              "dead target demotes the missile to straight flight");

    /* An unguided missile wobbles instead of tracking. */
    check_int(fire_torpedo(engine, player, 64, &shot), 1,
              "unguided missile launched");
    check_int(shot.tracking, 0, "unguided missile does not track");
    check_int(shot.target_slot, -1, "unguided missile has no target");
    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK,
              "unguided missile snapshot");
    check_int(snapshot.torpedoes[shot.torpedo_slot].status,
              DTREK_TORP_MOVE, "unguided missile uses move mode");

    /* A lock on a friendly ship is ignored. */
    check_int(fire_tracking(engine, player, 64, player, DTREK_WPN_MISSILE,
                            &shot),
              1, "self-locked missile launched");
    check_int(shot.tracking, 0, "friendly lock is ignored");
    check_int(shot.target_slot, -1, "friendly lock has no target");

    dtrek_destroy(engine);
}

static void test_plasma_decay(void)
{
    dtrek_engine *engine = dtrek_create(31337);
    DtrekGameSnapshot snapshot;
    DtrekTorpedoResult shot;
    const DtrekTorpedoSnapshot *torp;
    int player;
    int enemy;

    check(engine != NULL, "plasma engine created");
    if (engine == NULL) return;

    player = dtrek_spawn(engine, "Kirk", DTREK_TEAM_FEDERATION,
                         100000, 100000);
    enemy = dtrek_spawn(engine, "Targ", DTREK_TEAM_ROMULAN,
                        100000, 130000);

    check_int(dtrek_set_ordnance(engine, player, DTREK_WPN_PLASMA),
              DTREK_OK, "player selects plasma");
    check_int(fire_tracking(engine, player, 64, enemy, DTREK_WPN_PLASMA,
                            &shot),
              1, "plasma torpedo launched");
    check_int(shot.weapon_class, DTREK_WPN_PLASMA, "plasma class");
    check_int(shot.tracking, 1, "plasma tracks a locked target");
    check_int(shot.damage, 110, "plasma damage");
    check_int(shot.speed, 8, "plasma speed");
    check_int(shot.fuse, 100, "plasma fuse is fixed");
    check_int(shot.fuel_cost, 1100, "plasma fuel");
    check_int(shot.tubes_loaded, 4, "plasma consumed one tube");

    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK,
              "plasma snapshot");
    torp = &snapshot.torpedoes[shot.torpedo_slot];
    check_int(torp->base_damage, 110, "plasma base damage");
    check_int(torp->decay, 8, "plasma decay per ten ticks");
    check_int(torp->status, DTREK_TORP_TRACK, "plasma starts tracking");
    check_int(ship_at(&snapshot, player)->fuel, 8500 - 1100,
              "plasma fuel cost applied");
    check_int(ship_at(&snapshot, player)->tubes_reload, 60,
              "plasma reload interval started");

    check_int(dtrek_step(engine, 10), DTREK_OK, "plasma first decay");
    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK,
              "plasma decay snapshot");
    torp = &snapshot.torpedoes[shot.torpedo_slot];
    check_int(torp->damage, 102, "plasma lost one decay step");
    check_int(torp->course, 128, "plasma turned south toward the target");

    check_int(dtrek_step(engine, 10), DTREK_OK, "plasma second decay");
    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK,
              "plasma second snapshot");
    torp = &snapshot.torpedoes[shot.torpedo_slot];
    check_int(torp->damage, 94, "plasma lost a second decay step");

    check_int(dtrek_step(engine, 39), DTREK_OK, "reload nearly complete");
    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK,
              "plasma reload snapshot");
    check_int(ship_at(&snapshot, player)->tubes_loaded, 4,
              "plasma reload takes sixty ticks");
    check_int(dtrek_step(engine, 1), DTREK_OK, "plasma reload completes");
    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK,
              "plasma reloaded snapshot");
    check_int(ship_at(&snapshot, player)->tubes_loaded, 5,
              "plasma tube reloaded");

    dtrek_destroy(engine);
}

static void test_chaff(void)
{
    dtrek_engine *engine = dtrek_create(5150);
    DtrekGameSnapshot before;
    DtrekGameSnapshot after;
    DtrekTorpedoResult shot;
    const DtrekTorpedoSnapshot *torp;
    int player;
    int enemy;
    int chaff_x;
    int chaff_y;
    int shields_before;

    check(engine != NULL, "chaff engine created");
    if (engine == NULL) return;

    player = dtrek_spawn(engine, "Kirk", DTREK_TEAM_FEDERATION,
                         100000, 100000);
    check_int(dtrek_set_controls(engine, player, 0, 4), DTREK_OK,
              "chaff ship accelerates north");
    check_int(dtrek_step(engine, 30), DTREK_OK, "chaff ship builds speed");
    check_int(dtrek_set_ordnance(engine, player, DTREK_WPN_CHAFF),
              DTREK_OK, "player selects chaff");

    check_int(dtrek_snapshot(engine, &before), DTREK_OK,
              "pre-chaff snapshot");
    check_int(fire_torpedo(engine, player, 128, &shot), 1,
              "chaff launched");
    check_int(shot.weapon_class, DTREK_WPN_CHAFF, "chaff class");
    check_int(shot.tracking, 0, "chaff does not track");
    check_int(shot.arm, 10, "chaff arms after ten ticks");
    check_int(shot.damage, 19, "chaff damage");
    check(shot.fuse >= 60 && shot.fuse <= 89,
          "chaff fuse includes duration, random, and arm delay");

    check_int(dtrek_snapshot(engine, &before), DTREK_OK,
              "post-chaff snapshot");
    torp = &before.torpedoes[shot.torpedo_slot];
    check_int(torp->status, DTREK_TORP_STRAIGHT, "chaff is straight");
    check_int(torp->speed, ship_at(&before, player)->speed,
              "chaff inherits the ship speed");
    check_int(torp->course, ship_at(&before, player)->course,
              "chaff inherits the ship heading");
    chaff_x = torp->x;
    chaff_y = torp->y;
    check_int(distance_squared(chaff_x, chaff_y,
                               ship_at(&before, player)->x,
                               ship_at(&before, player)->y),
              DTREK_TORPEDO_EXPLODE_DIST * 2
                  * DTREK_TORPEDO_EXPLODE_DIST * 2,
              "chaff starts 800 units ahead");

    /* Put a hostile 900 ahead; the moving cloud drifts into blast range. */
    enemy = dtrek_spawn(engine, "Cloud Target", DTREK_TEAM_ROMULAN,
                        chaff_x, chaff_y - 900);
    check(enemy >= 0, "chaff target spawned");
    check_int(dtrek_snapshot(engine, &before), DTREK_OK,
              "chaff target snapshot");
    shields_before = shield_sum(ship_at(&before, enemy));

    check_int(dtrek_step(engine, 11), DTREK_OK, "chaff arms and detonates");
    check_int(dtrek_snapshot(engine, &after), DTREK_OK,
              "chaff detonation snapshot");
    torp = &after.torpedoes[shot.torpedo_slot];
    check_int(torp->status, DTREK_TORP_STRAIGHT,
              "chaff keeps flying after detonating");
    check_int(torp->fuse, 10, "chaff detonation resets the short fuse");
    check_int(shield_sum(ship_at(&after, enemy)), shields_before - 19,
              "chaff damaged the nearby hostile");

    dtrek_destroy(engine);
}

static void test_chaff_point_defense(void)
{
    dtrek_engine *engine = dtrek_create(6161);
    DtrekTorpedoResult launch;
    DtrekPhaserResult shot;
    int player;
    int enemy;

    check(engine != NULL, "chaff defense engine created");
    if (engine == NULL) return;

    player = dtrek_spawn(engine, "Kirk", DTREK_TEAM_FEDERATION,
                         100000, 100000);
    enemy = dtrek_spawn(engine, "Targ", DTREK_TEAM_ROMULAN,
                        108000, 100000);
    check_int(dtrek_set_ordnance(engine, enemy, DTREK_WPN_CHAFF),
              DTREK_OK, "enemy selects chaff");
    check_int(dtrek_set_controls(engine, enemy, 192, 0), DTREK_OK,
              "enemy turns toward the player");
    check_int(dtrek_step(engine, 1), DTREK_OK, "enemy heading applied");
    check_int(fire_torpedo(engine, enemy, 192, &launch), 1,
              "enemy chaff launched");
    check_int(launch.weapon_class, DTREK_WPN_CHAFF, "enemy chaff class");

    check_int(dtrek_fire_phaser(engine, player, 64, &shot), 0,
              "phasers ignore chaff as a point-defense target");
    check_int(shot.fired, 1, "phasers still fired");
    check_int(shot.hit, 0, "no ship or torpedo was hit");
    check_int(shot.target_is_torpedo, 0, "chaff was not selected");

    dtrek_destroy(engine);
}

static void test_mine(void)
{
    dtrek_engine *engine = dtrek_create(9090);
    DtrekGameSnapshot before;
    DtrekGameSnapshot after;
    DtrekTorpedoResult shot;
    const DtrekTorpedoSnapshot *torp;
    int player;
    int enemy;
    int shields_before;
    int distance;

    check(engine != NULL, "mine engine created");
    if (engine == NULL) return;

    player = dtrek_spawn(engine, "Kirk", DTREK_TEAM_FEDERATION,
                         100000, 100000);
    enemy = dtrek_spawn(engine, "Targ", DTREK_TEAM_ROMULAN,
                        101000, 100000);

    check_int(dtrek_set_ordnance(engine, player, DTREK_WPN_MINE),
              DTREK_OK, "player selects mines");
    check_int(fire_at_point(engine, player, 64, DTREK_WPN_MINE,
                            101000, 100000, &shot),
              1, "mine placed at the aim point");
    check_int(shot.weapon_class, DTREK_WPN_MINE, "mine class");
    check_int(shot.tracking, 0, "mine does not track");
    check_int(shot.arm, 20, "mine arms after twenty ticks");
    check_int(shot.speed, 0, "mine is stationary");
    check_int(shot.damage, 35, "mine damage");
    check_int(shot.fuel_cost, 35, "mine fuel");
    check(shot.fuse >= 80 && shot.fuse <= 109,
          "mine fuse includes duration, random, and arm delay");

    check_int(dtrek_snapshot(engine, &before), DTREK_OK,
              "mine placement snapshot");
    torp = &before.torpedoes[shot.torpedo_slot];
    check_int(torp->x, 101000, "mine x at the aim point");
    check_int(torp->y, 100000, "mine y at the aim point");
    check_int(torp->status, DTREK_TORP_STRAIGHT, "mine is straight");
    shields_before = shield_sum(ship_at(&before, enemy));

    check_int(dtrek_step(engine, 20), DTREK_OK, "mine still arming");
    check_int(dtrek_snapshot(engine, &before), DTREK_OK,
              "mine arming snapshot");
    check_int(before.torpedoes[shot.torpedo_slot].status,
              DTREK_TORP_STRAIGHT, "mine has not armed yet");

    check_int(dtrek_step(engine, 1), DTREK_OK, "mine arms and detonates");
    check_int(dtrek_snapshot(engine, &after), DTREK_OK,
              "mine detonation snapshot");
    check_int(after.torpedoes[shot.torpedo_slot].status,
              DTREK_TORP_EXPLODE, "mine detonated");
    check_int(shield_sum(ship_at(&after, enemy)), shields_before - 35,
              "mine damaged the nearby hostile");

    /* A negative aim point defaults to WPN_MINE_RANGE along the course. */
    check_int(fire_at_point(engine, player, 64, DTREK_WPN_MINE,
                            -1, -1, &shot),
              1, "mine launched with default aim");
    check_int(dtrek_snapshot(engine, &after), DTREK_OK,
              "default mine snapshot");
    torp = &after.torpedoes[shot.torpedo_slot];
    distance = isqrt_floor(distance_squared(
        torp->x, torp->y,
        ship_at(&after, player)->x, ship_at(&after, player)->y));
    check(distance >= 9999 && distance <= 10001,
          "default mine lands at WPN_MINE_RANGE");
    check(torp->x > ship_at(&after, player)->x,
          "default mine lies along the launch course");

    /* A distant aim point is clamped to WPN_MINE_RANGE. */
    check_int(fire_at_point(engine, player, 64, DTREK_WPN_MINE,
                            200000, 100000, &shot),
              1, "mine launched with a distant aim");
    check_int(dtrek_snapshot(engine, &after), DTREK_OK,
              "clamped mine snapshot");
    torp = &after.torpedoes[shot.torpedo_slot];
    distance = isqrt_floor(distance_squared(
        torp->x, torp->y,
        ship_at(&after, player)->x, ship_at(&after, player)->y));
    check(distance >= 9999 && distance <= 10001,
          "distant mine is clamped to WPN_MINE_RANGE");

    dtrek_destroy(engine);
}

static void test_class_point_defense(void)
{
    dtrek_engine *engine = dtrek_create(1212);
    DtrekGameSnapshot before;
    DtrekTorpedoResult launch;
    DtrekPhaserResult shot;
    int player;
    int enemy;
    int range;

    check(engine != NULL, "class defense engine created");
    if (engine == NULL) return;

    player = dtrek_spawn(engine, "Kirk", DTREK_TEAM_FEDERATION,
                         100000, 100000);
    enemy = dtrek_spawn(engine, "Targ", DTREK_TEAM_ROMULAN,
                        103000, 100000);
    check_int(dtrek_set_ordnance(engine, enemy, DTREK_WPN_MISSILE),
              DTREK_OK, "enemy selects missiles");
    check_int(fire_torpedo(engine, enemy, 192, &launch), 1,
              "enemy missile launched");
    check_int(dtrek_step(engine, 5), DTREK_OK, "enemy missile closes");

    check_int(dtrek_snapshot(engine, &before), DTREK_OK,
              "missile defense snapshot");
    range = isqrt_floor(distance_squared(
        before.torpedoes[launch.torpedo_slot].x,
        before.torpedoes[launch.torpedo_slot].y,
        ship_at(&before, player)->x, ship_at(&before, player)->y));
    check_int(dtrek_fire_phaser(engine, player, 64, &shot), 1,
              "phaser engaged the missile");
    check_int(shot.target_is_torpedo, 1, "missile selected for defense");
    check_int(shot.torpedo_damage, 30 - range / 250,
              "missiles take normal beam damage");
    check_int(shot.torpedo_destroyed, 1, "missile warhead consumed");

    dtrek_destroy(engine);

    engine = dtrek_create(1313);
    check(engine != NULL, "plasma defense engine created");
    if (engine == NULL) return;

    player = dtrek_spawn(engine, "Kirk", DTREK_TEAM_FEDERATION,
                         100000, 100000);
    enemy = dtrek_spawn(engine, "Targ", DTREK_TEAM_ROMULAN,
                        103000, 100000);
    check_int(fire_torpedo(engine, enemy, 192, &launch), 1,
              "enemy plasma launched");
    check_int(launch.weapon_class, DTREK_WPN_PLASMA, "enemy uses plasma");
    check_int(dtrek_step(engine, 5), DTREK_OK, "enemy plasma closes");

    check_int(dtrek_snapshot(engine, &before), DTREK_OK,
              "plasma defense snapshot");
    range = isqrt_floor(distance_squared(
        before.torpedoes[launch.torpedo_slot].x,
        before.torpedoes[launch.torpedo_slot].y,
        ship_at(&before, player)->x, ship_at(&before, player)->y));
    check_int(dtrek_fire_phaser(engine, player, 64, &shot), 1,
              "phaser engaged the plasma");
    check_int(shot.target_is_torpedo, 1, "plasma selected for defense");
    check_int(shot.torpedo_damage, 30 / 2 - range / 250,
              "plasma takes half beam damage");
    check_int(shot.torpedo_destroyed, 0,
              "plasma warhead survives a single bank");
    check_int(dtrek_snapshot(engine, &before), DTREK_OK,
              "damaged plasma snapshot");
    check_int(before.torpedoes[launch.torpedo_slot].damage,
              110 - (30 / 2 - range / 250),
              "plasma warhead was reduced by the beam");

    dtrek_destroy(engine);
}

static void test_launch_validation(void)
{
    dtrek_engine *engine = dtrek_create(1717);
    DtrekTorpedoLaunch launch;
    DtrekTorpedoResult shot;
    int player;

    check(engine != NULL, "validation engine created");
    if (engine == NULL) return;

    player = dtrek_spawn(engine, "Kirk", DTREK_TEAM_FEDERATION,
                         100000, 100000);

    check_int(dtrek_fire_torpedo(engine, player, NULL, &shot),
              DTREK_ERR_ARGUMENT, "launch request is required");

    memset(&launch, 0, sizeof(launch));
    launch.course = 64;
    launch.target_slot = -1;
    launch.aim_x = -1;
    launch.aim_y = -1;
    launch.weapon_class = DTREK_WPN_PHASER;
    check_int(dtrek_fire_torpedo(engine, player, &launch, &shot),
              DTREK_ERR_ARGUMENT, "beam class cannot launch");

    launch.weapon_class = DTREK_WPN_PHOTON;
    launch.target_slot = DTREK_MAX_PLAYERS;
    check_int(dtrek_fire_torpedo(engine, player, &launch, &shot),
              DTREK_ERR_ARGUMENT, "target slot must be in range");

    launch.target_slot = -1;
    launch.course = 256;
    check_int(dtrek_fire_torpedo(engine, player, &launch, &shot),
              DTREK_ERR_ARGUMENT, "launch course must be 0..255");

    launch.course = 64;
    check_int(fire_torpedo(engine, player, 64, &shot), 1,
              "valid launch still works");
    check_int(shot.fired, 1, "valid launch reports fired");

    dtrek_destroy(engine);
}

static void test_reset(void)
{
    dtrek_engine *engine = dtrek_create(1);
    DtrekGameSnapshot snapshot;

    check(engine != NULL, "reset engine created");
    if (engine == NULL) return;

    dtrek_spawn(engine, "One", DTREK_TEAM_FEDERATION, 1000, 1000);
    dtrek_step(engine, 5);
    check_int(dtrek_reset(engine, 9), DTREK_OK, "reset succeeds");
    check_int(dtrek_snapshot(engine, &snapshot), DTREK_OK, "reset snapshot");
    check_int(snapshot.tick, 0, "reset tick");
    check_int(snapshot.ship_count, 0, "reset ships");
    check_int(snapshot.active_count, 0, "reset active");

    dtrek_destroy(engine);
}

static void test_galaxy_generation(void)
{
    dtrek_engine *a = dtrek_create(1);
    dtrek_engine *b = dtrek_create(2);
    DtrekGameSnapshot snapshot_a;
    DtrekGameSnapshot snapshot_b;
    DtrekGameSnapshot after;
    int i;
    int differences = 0;

    check(a != NULL && b != NULL, "galaxy engines created");
    if (a == NULL || b == NULL) return;

    check_int(dtrek_generate_galaxy(a, 1989), DTREK_OK, "galaxy generated");
    check_int(dtrek_generate_galaxy(b, 1989), DTREK_OK,
              "second galaxy generated");
    dtrek_snapshot(a, &snapshot_a);
    dtrek_snapshot(b, &snapshot_b);

    check_int(snapshot_a.planet_count, DTREK_MAX_PLANETS,
              "galaxy has eighty objects");
    check_int(snapshot_b.planet_count, DTREK_MAX_PLANETS,
              "second galaxy has eighty objects");

    for (i = 0; i < DTREK_MAX_PLANETS; ++i) {
        const DtrekPlanetSnapshot *pa = &snapshot_a.planets[i];
        const DtrekPlanetSnapshot *pb = &snapshot_b.planets[i];

        check(pa->in_use == pb->in_use
              && pa->flags == pb->flags
              && pa->owner == pb->owner
              && pa->armies == pb->armies
              && pa->x == pb->x
              && pa->y == pb->y
              && strcmp(pa->name, pb->name) == 0,
              "same seed produces the same galaxies");
    }

    check_int(snapshot_a.planets[0].x, 44444, "Earth x");
    check_int(snapshot_a.planets[0].y, 155556, "Earth y");
    check_int(snapshot_a.planets[0].owner, DTREK_TEAM_FEDERATION,
              "Earth owner");
    check((snapshot_a.planets[0].flags
           & (DTREK_PLANET_HOME | DTREK_PLANET_FUEL
              | DTREK_PLANET_REPAIR))
          == (DTREK_PLANET_HOME | DTREK_PLANET_FUEL
              | DTREK_PLANET_REPAIR),
          "Earth is a home fuel/repair world");
    check_int(snapshot_a.planets[10].x, 44444, "Romulus x");
    check_int(snapshot_a.planets[10].y, 44444, "Romulus y");
    check_int(snapshot_a.planets[20].x, 155556, "Klingus x");
    check_int(snapshot_a.planets[20].y, 44444, "Klingus y");
    check_int(snapshot_a.planets[30].x, 155556, "Orion x");
    check_int(snapshot_a.planets[30].y, 155556, "Orion y");
    check_int(snapshot_a.planets[40].x, 100000, "independent home x");
    check_int(snapshot_a.planets[40].y, 100000, "independent home y");
    check_int(snapshot_a.planets[40].owner, DTREK_TEAM_INDEPENDENT,
              "independent home owner");
    check((snapshot_a.planets[41].flags & DTREK_PLANET_WANDERER) != 0,
          "slot 41 is a wanderer");
    check((snapshot_a.planets[50].flags & DTREK_PLANET_BLACKHOLE) != 0,
          "first sector black hole");
    check((snapshot_a.planets[51].flags & DTREK_PLANET_PULSAR) != 0,
          "first sector pulsar");
    check((snapshot_a.planets[52].flags & DTREK_PLANET_NEBULA) != 0,
          "first sector nebula");
    check(snapshot_a.planets[41].name[0] != '\0', "wanderer named");

    /* The combat RNG must not advance during galaxy generation. */
    dtrek_snapshot(a, &after);
    check_int(after.rng_state_lo, snapshot_a.rng_state_lo,
              "galaxy generation leaves combat rng alone (lo)");
    check_int(after.rng_state_hi, snapshot_a.rng_state_hi,
              "galaxy generation leaves combat rng alone (hi)");

    dtrek_generate_galaxy(b, 2001);
    dtrek_snapshot(b, &snapshot_b);
    for (i = 0; i < DTREK_MAX_PLANETS; ++i) {
        if (snapshot_a.planets[i].x != snapshot_b.planets[i].x
            || snapshot_a.planets[i].y != snapshot_b.planets[i].y
            || strcmp(snapshot_a.planets[i].name,
                      snapshot_b.planets[i].name) != 0) {
            differences++;
        }
    }
    check(differences > 0, "different galaxy seeds differ");

    check_int(dtrek_clear_planets(a), DTREK_OK, "planets cleared");
    dtrek_snapshot(a, &snapshot_a);
    check_int(snapshot_a.planet_count, 0, "no planets after clear");

    dtrek_destroy(a);
    dtrek_destroy(b);
}

static void test_wanderer_movement(void)
{
    dtrek_engine *engine = dtrek_create(3);
    DtrekGameSnapshot before;
    DtrekGameSnapshot after;

    check(engine != NULL, "wanderer engine created");
    if (engine == NULL) return;

    dtrek_generate_galaxy(engine, 77);
    dtrek_snapshot(engine, &before);
    dtrek_step(engine, 1);
    dtrek_snapshot(engine, &after);

    check(before.planets[41].x != after.planets[41].x
          || before.planets[41].y != after.planets[41].y,
          "wanderer moves each tick");
    check(after.planets[41].x >= DTREK_ORBIT_DISTANCE
          && after.planets[41].x
              <= DTREK_GALAXY_WIDTH - DTREK_ORBIT_DISTANCE,
          "wanderer stays inside x bounds");
    check(after.planets[41].y >= DTREK_ORBIT_DISTANCE
          && after.planets[41].y
              <= DTREK_GALAXY_WIDTH - DTREK_ORBIT_DISTANCE,
          "wanderer stays inside y bounds");

    dtrek_destroy(engine);
}

static void test_planet_placement(void)
{
    dtrek_engine *engine = dtrek_create(4);
    DtrekGameSnapshot snapshot;

    check(engine != NULL, "placement engine created");
    if (engine == NULL) return;

    check_int(dtrek_place_planet(engine, DTREK_MAX_PLANETS, 0,
                                 DTREK_TEAM_INDEPENDENT, 0, 1000, 1000,
                                 "Bad"),
              DTREK_ERR_ARGUMENT, "planet slot validated");
    check_int(dtrek_place_planet(engine, 0, 0, DTREK_NUM_TEAMS, 0,
                                 1000, 1000, "Bad"),
              DTREK_ERR_ARGUMENT, "planet owner validated");
    check_int(dtrek_place_planet(engine, 0, 0, DTREK_TEAM_INDEPENDENT,
                                 -1, 1000, 1000, "Bad"),
              DTREK_ERR_ARGUMENT, "planet armies validated");
    check_int(dtrek_place_planet(engine, 0, 0, DTREK_TEAM_INDEPENDENT,
                                 0, -1, 1000, "Bad"),
              DTREK_ERR_ARGUMENT, "planet x validated");

    check_int(dtrek_place_planet(engine, 3,
                                 DTREK_PLANET_FUEL | DTREK_PLANET_REPAIR,
                                 DTREK_TEAM_FEDERATION, 12,
                                 100000, 100000, "Starbase"),
              DTREK_OK, "planet placed");
    check_int(dtrek_place_planet(engine, 3, DTREK_PLANET_BARREN,
                                 DTREK_TEAM_ROMULAN, 1,
                                 120000, 120000, "Replacement"),
              DTREK_OK, "planet replaced");
    dtrek_snapshot(engine, &snapshot);
    check_int(snapshot.planet_count, 1, "replacement reuses the slot");
    check_int(snapshot.planets[3].flags & DTREK_PLANET_OWNER_MASK,
              DTREK_PLANET_ROMULAN, "owner flag follows owner argument");
    check_int(snapshot.planets[3].owner, DTREK_TEAM_ROMULAN,
              "owner field follows owner argument");
    check_int(snapshot.planets[3].armies, 1, "planet armies stored");
    check_int(snapshot.planets[3].x, 120000, "planet x stored");
    check(strcmp(snapshot.planets[3].name, "Replacement") == 0,
          "planet name stored");

    dtrek_destroy(engine);
}

static void test_orbit_and_leave(void)
{
    dtrek_engine *engine = dtrek_create(5);
    DtrekGameSnapshot snapshot;
    int ship;
    int planet_x;
    int planet_y;
    int started;

    check(engine != NULL, "orbit engine created");
    if (engine == NULL) return;

    check_int(dtrek_place_planet(engine, 0,
                                 DTREK_PLANET_FUEL | DTREK_PLANET_REPAIR,
                                 DTREK_TEAM_FEDERATION, 10,
                                 100000, 100000, "Starbase"),
              DTREK_OK, "orbit planet placed");
    ship = dtrek_spawn(engine, "Kirk", DTREK_TEAM_FEDERATION,
                       100000, 100400);
    check(ship >= 0, "orbit ship spawned");
    if (ship < 0) return;

    check_int(dtrek_orbit(engine, ship, -1), 0, "orbit nearest planet");
    dtrek_snapshot(engine, &snapshot);
    check_int(snapshot.ships[ship].orbiting, 1, "ship is orbiting");
    check_int(snapshot.ships[ship].planet_slot, 0, "orbit planet slot");
    check_int(snapshot.ships[ship].nearest_planet, 0, "nearest planet");
    check_int(snapshot.ships[ship].speed, 0, "orbit speed zero");
    check_int(snapshot.ships[ship].at_fuel_planet, 1,
              "friendly fuel planet detected");
    check_int(snapshot.ships[ship].at_repair_planet, 1,
              "friendly repair planet detected");
    check(isqrt_floor(distance_squared(snapshot.ships[ship].x,
                                       snapshot.ships[ship].y,
                                       100000, 100000)) >= 700,
          "orbit radius reached");

    planet_x = snapshot.ships[ship].x;
    planet_y = snapshot.ships[ship].y;
    dtrek_step(engine, 1);
    dtrek_snapshot(engine, &snapshot);
    started = snapshot.ships[ship].x != planet_x
        || snapshot.ships[ship].y != planet_y;
    check(started, "ship rotates in orbit");
    check_int(snapshot.ships[ship].speed, 0, "orbiting ship stays at rest");
    check(isqrt_floor(distance_squared(snapshot.ships[ship].x,
                                       snapshot.ships[ship].y,
                                       100000, 100000)) >= 700,
          "orbit radius held");

    check_int(dtrek_leave_orbit(engine, ship), DTREK_OK, "leave orbit");
    dtrek_snapshot(engine, &snapshot);
    check_int(snapshot.ships[ship].orbiting, 0, "orbit cleared");
    check_int(snapshot.ships[ship].planet_slot, -1, "orbit slot cleared");
    check_int(snapshot.ships[ship].at_fuel_planet, 0,
              "fuel service cleared");
    check_int(snapshot.ships[ship].at_repair_planet, 0,
              "repair service cleared");

    check_int(dtrek_leave_orbit(engine, ship), DTREK_OK,
              "leaving orbit again is harmless");
    check_int(dtrek_leave_orbit(engine, -1), DTREK_ERR_SHIP,
              "leave orbit validates the ship");

    dtrek_destroy(engine);
}

static void test_orbit_validation(void)
{
    dtrek_engine *engine = dtrek_create(6);
    DtrekGameSnapshot snapshot;
    int ship;

    check(engine != NULL, "orbit validation engine created");
    if (engine == NULL) return;

    check_int(dtrek_place_planet(engine, 0, 0, DTREK_TEAM_INDEPENDENT, 0,
                                 100000, 95000, "Far"),
              DTREK_OK, "far planet placed");
    ship = dtrek_spawn(engine, "Kirk", DTREK_TEAM_FEDERATION,
                       100000, 100000);
    check_int(dtrek_orbit(engine, ship, 0), DTREK_ERR_STATE,
              "orbit requires proximity");
    check_int(dtrek_orbit(engine, ship, 1), DTREK_ERR_ARGUMENT,
              "orbit validates the planet slot");

    check_int(dtrek_place_planet(engine, 1, DTREK_PLANET_BLACKHOLE,
                                 DTREK_TEAM_INDEPENDENT, 0,
                                 100000, 100000, "Void"),
              DTREK_OK, "black hole placed");
    check_int(dtrek_orbit(engine, ship, 1), DTREK_ERR_STATE,
              "black holes cannot be orbited");
    check_int(dtrek_orbit(engine, ship, -1), DTREK_ERR_STATE,
              "black hole is not an eligible orbit target");

    check_int(dtrek_leave_orbit(engine, ship), DTREK_OK,
              "leave orbit without orbit is harmless");

    /* Speed above ORBSPEED blocks orbit entry. */
    dtrek_clear_planets(engine);
    dtrek_set_controls(engine, ship, 0, 6);
    dtrek_step(engine, 45);
    dtrek_snapshot(engine, &snapshot);
    check(snapshot.ships[ship].speed > DTREK_ORBIT_SPEED,
          "ship is moving too fast to orbit");
    check_int(dtrek_place_planet(engine, 2, DTREK_PLANET_FUEL,
                                 DTREK_TEAM_FEDERATION, 0,
                                 snapshot.ships[ship].x,
                                 snapshot.ships[ship].y, "Fast Base"),
              DTREK_OK, "close planet placed");
    check_int(dtrek_orbit(engine, ship, -1), DTREK_ERR_STATE,
              "fast ships cannot orbit");
    check_int(dtrek_set_repair(engine, ship, 1), DTREK_OK,
              "repair enabled");
    dtrek_snapshot(engine, &snapshot);
    check_int(snapshot.ships[ship].repair_mode, 1, "repair mode stored");
    check_int(dtrek_set_repair(engine, -1, 1), DTREK_ERR_SHIP,
              "repair validates the ship");

    dtrek_destroy(engine);
}

static void test_gamma_services(void)
{
    dtrek_engine *engine = dtrek_create(7);
    DtrekGameSnapshot snapshot;
    DtrekPhaserResult shot;
    int player;
    int enemy;
    int before_hull;
    int before_fuel;

    check(engine != NULL, "service engine created");
    if (engine == NULL) return;

    player = dtrek_spawn(engine, "Kirk", DTREK_TEAM_FEDERATION,
                         100000, 100200);
    enemy = dtrek_spawn(engine, "Targ", DTREK_TEAM_ROMULAN,
                        100000, 100400);
    check(player >= 0 && enemy >= 0, "service ships spawned");

    /*
     * Kirk's full volley may knock out enemy banks in Stage 5 (the authentic
     * subsystem spill), so the surviving return fire is weaker.  Drop the
     * player's shields to let that volley reach the hull and set up repair.
     */
    memset(&shot, 0, sizeof(shot));
    check_int(dtrek_fire_phaser(engine, player, 128, &shot), 1,
              "crew fires on the pursuer");
    check_int(shot.hit, 1, "player volley hits");
    check_int(shot.beams_fired, 4, "volley uses four banks");
    check_int(shot.damage, 120, "close-range volley damage");

    dtrek_snapshot(engine, &snapshot);
    check_int(snapshot.ships[player].beams_loaded, 0,
              "volley unloads the banks");
    check_int(snapshot.ships[player].fuel, 7540, "beam fuel consumption");

    check_int(dtrek_set_shields(engine, player, 0), DTREK_OK,
              "player lowers shields for the return fire");
    memset(&shot, 0, sizeof(shot));
    check_int(dtrek_fire_phaser(engine, enemy, 0, &shot), 1,
              "pursuer returns fire");
    check_int(shot.hit, 1, "pursuer volley hits");

    dtrek_snapshot(engine, &snapshot);
    check(snapshot.ships[player].hull < snapshot.ships[player].hull_max,
          "return fire damages the hull");
    before_fuel = snapshot.ships[player].fuel;
    before_hull = snapshot.ships[player].hull;

    check_int(dtrek_place_planet(engine, 0,
                                 DTREK_PLANET_FUEL | DTREK_PLANET_REPAIR,
                                 DTREK_TEAM_FEDERATION, 10,
                                 snapshot.ships[player].x,
                                 snapshot.ships[player].y, "Starbase"),
              DTREK_OK, "service planet placed");
    check_int(dtrek_orbit(engine, player, 0), 0, "service orbit entered");
    check_int(dtrek_set_repair(engine, player, 1), DTREK_OK,
              "damage control enabled");

    dtrek_step(engine, 10);
    dtrek_snapshot(engine, &snapshot);
    check_int(snapshot.ships[player].hull, before_hull,
              "subsystem repair waits for the repair fuse");
    {
        int fuel_expected = before_fuel + 280;
        if (fuel_expected > snapshot.ships[player].fuel_max) {
            fuel_expected = snapshot.ships[player].fuel_max;
        }
        check_int(snapshot.ships[player].fuel, fuel_expected,
                  "friendly fuel planet adds six per tick");
    }
    check_int(snapshot.ships[player].beams_loaded,
              snapshot.ships[player].beams_total,
              "repair planet rearms beams");
    check_int(snapshot.ships[player].tubes_loaded,
              snapshot.ships[player].tubes_total,
              "repair planet rearms torpedo tubes");
    check_int(snapshot.ships[player].at_fuel_planet, 1,
              "fuel service active");
    check_int(snapshot.ships[player].at_repair_planet, 1,
              "repair service active");

    dtrek_step(engine, 20);
    dtrek_snapshot(engine, &snapshot);
    check_int(snapshot.ships[player].hull, before_hull + 16,
              "repair planet restores hull on the repair pulse");

    dtrek_destroy(engine);
}

static void test_black_hole_and_pulsar(void)
{
    dtrek_engine *engine = dtrek_create(8);
    DtrekGameSnapshot snapshot;
    int ship;
    int x_before;

    check(engine != NULL, "hazard engine created");
    if (engine == NULL) return;

    check_int(dtrek_place_planet(engine, 0, DTREK_PLANET_BLACKHOLE,
                                 DTREK_TEAM_INDEPENDENT, 0,
                                 100000, 100000, "Void"),
              DTREK_OK, "black hole placed");
    ship = dtrek_spawn(engine, "Kirk", DTREK_TEAM_FEDERATION,
                       106000, 100000);
    dtrek_snapshot(engine, &snapshot);
    x_before = snapshot.ships[ship].x;
    dtrek_step(engine, 1);
    dtrek_snapshot(engine, &snapshot);
    check(snapshot.ships[ship].x < x_before,
          "black hole gravity pulls the ship in");
    check_int(snapshot.ships[ship].status, DTREK_STATUS_ALIVE,
              "distant ship survives black hole gravity");

    ship = dtrek_spawn(engine, "Doomed", DTREK_TEAM_FEDERATION,
                       100100, 100000);
    check(ship >= 0, "close ship spawned");
    dtrek_step(engine, 1);
    dtrek_snapshot(engine, &snapshot);
    check(snapshot.ships[ship].status != DTREK_STATUS_ALIVE,
          "black hole destroys a ship that touches it");

    dtrek_clear_planets(engine);
    check_int(dtrek_place_planet(engine, 0, DTREK_PLANET_PULSAR,
                                 DTREK_TEAM_INDEPENDENT, 0,
                                 100000, 100000, "Pulse"),
              DTREK_OK, "pulsar placed");
    ship = dtrek_spawn(engine, "Burned", DTREK_TEAM_FEDERATION,
                       100500, 100000);
    dtrek_step(engine, 1);
    dtrek_snapshot(engine, &snapshot);
    check(snapshot.ships[ship].status != DTREK_STATUS_ALIVE,
          "pulsar destroys a nearby ship");

    dtrek_destroy(engine);
}

static void test_torpedo_planet_impact(void)
{
    dtrek_engine *engine = dtrek_create(9);
    DtrekGameSnapshot snapshot;
    DtrekTorpedoResult launch;
    int ship;

    check(engine != NULL, "impact engine created");
    if (engine == NULL) return;

    check_int(dtrek_place_planet(engine, 0, 0, DTREK_TEAM_INDEPENDENT, 0,
                                 100000, 98000, "Target"),
              DTREK_OK, "impact planet placed");
    ship = dtrek_spawn(engine, "Kirk", DTREK_TEAM_FEDERATION,
                       100000, 100000);

    check_int(fire_torpedo(engine, ship, 0, &launch), 1,
              "planet-impact torpedo launched");
    dtrek_step(engine, 6);
    dtrek_snapshot(engine, &snapshot);
    check_int(count_torpedoes(&snapshot), 0,
              "torpedo is removed on planet impact");

    dtrek_clear_planets(engine);
    check_int(dtrek_place_planet(engine, 0, DTREK_PLANET_NEBULA,
                                 DTREK_TEAM_INDEPENDENT, 0,
                                 100000, 98000, "Cloud"),
              DTREK_OK, "nebula placed");
    check_int(fire_torpedo(engine, ship, 0, &launch), 1,
              "nebula torpedo launched");
    dtrek_step(engine, 5);
    dtrek_snapshot(engine, &snapshot);
    check(count_torpedoes(&snapshot) > 0,
          "nebulae do not stop torpedoes");

    dtrek_destroy(engine);
}

static void test_reset_clears_planets(void)
{
    dtrek_engine *engine = dtrek_create(10);
    DtrekGameSnapshot snapshot;

    check(engine != NULL, "planet reset engine created");
    if (engine == NULL) return;

    dtrek_generate_galaxy(engine, 1989);
    dtrek_snapshot(engine, &snapshot);
    check_int(snapshot.planet_count, DTREK_MAX_PLANETS,
              "galaxy present before reset");
    check_int(dtrek_reset(engine, 9), DTREK_OK, "reset with planets");
    dtrek_snapshot(engine, &snapshot);
    check_int(snapshot.planet_count, 0, "reset clears planets");

    dtrek_destroy(engine);
}

static void test_robot_metadata_and_config(void)
{
    dtrek_engine *engine = dtrek_create(21);
    DtrekGameSnapshot snapshot;
    DtrekRobotConfig config;
    DtrekRobotConfig invalid;
    int player;
    int robot;
    int orbiter;
    int count_before;
    int fed_robot;
    int kli_robot;
    int ori_robot;
    int ind_robot;

    check(engine != NULL, "robot engine created");
    if (engine == NULL) return;

    check_int((int)dtrek_abi_version(), 6, "robot ABI 6");
    check(strcmp(dtrek_version_string(), "dtrek_core 0.6.0") == 0,
          "robot version string");

    player = dtrek_spawn(engine, "Kirk", DTREK_TEAM_FEDERATION,
                         90000, 90000);
    check(player >= 0, "robot scenario player spawned");

    robot = dtrek_spawn_robot(engine, "Warbird", DTREK_TEAM_ROMULAN,
                              100000, 100000, NULL);
    check(robot >= 0, "robot spawned");
    if (robot < 0) {
        dtrek_destroy(engine);
        return;
    }

    dtrek_snapshot(engine, &snapshot);
    check_int(snapshot.ships[robot].is_robot, 1, "robot flag set");
    check_int(snapshot.ships[robot].robot_flags, DTREK_ROBOT_NONE,
              "robot default flags");
    check(snapshot.ships[robot].robot_offense >= 70
          && snapshot.ships[robot].robot_offense <= 90
          && snapshot.ships[robot].robot_offense % 10 == 0,
          "robot default offense range");
    check(snapshot.ships[robot].robot_impatience >= 50
          && snapshot.ships[robot].robot_impatience <= 100,
          "robot default impatience range");
    check_int(snapshot.ships[robot].robot_attack_speed, 5,
              "Romulan attack speed");
    check_int(snapshot.ships[robot].robot_state, DTREK_ROBOT_IDLE,
              "robot starts idle");
    check_int(snapshot.ships[robot].robot_target, -1,
              "robot starts without a target");
    check_int(snapshot.ships[robot].robot_patrol_planet, -1,
              "robot starts without a patrol planet");
    check_int(snapshot.ships[robot].robot_last_beam_tick, -1,
              "robot has not fired a beam");
    check_int(snapshot.ships[robot].robot_last_torpedo_tick, -1,
              "robot has not fired a torpedo");
    check_int(snapshot.ships[player].is_robot, 0,
              "plain ships are not robots");

    memset(&config, 0, sizeof(config));
    config.offense = 100;
    config.impatience = 100;
    config.flags = DTREK_ROBOT_ORBITER;
    config.patrol_planet = -1;
    orbiter = dtrek_spawn_robot(engine, "Sentinel", DTREK_TEAM_FEDERATION,
                                120000, 120000, &config);
    check(orbiter >= 0, "orbiter robot spawned");
    dtrek_snapshot(engine, &snapshot);
    check_int(snapshot.ships[orbiter].robot_offense, 100,
              "orbiter default offense");
    check_int(snapshot.ships[orbiter].robot_impatience, 100,
              "orbiter default impatience");
    check_int(snapshot.ships[orbiter].robot_flags, DTREK_ROBOT_ORBITER,
              "orbiter flag stored");
    check_int(snapshot.ships[orbiter].robot_attack_speed, 7,
              "Federation attack speed");

    fed_robot = dtrek_spawn_robot(engine, "Fed", DTREK_TEAM_FEDERATION,
                                  80000, 80000, NULL);
    kli_robot = dtrek_spawn_robot(engine, "Kli", DTREK_TEAM_KLINGON,
                                  120000, 80000, NULL);
    ori_robot = dtrek_spawn_robot(engine, "Ori", DTREK_TEAM_ORION,
                                  80000, 120000, NULL);
    ind_robot = dtrek_spawn_robot(engine, "Ind", DTREK_TEAM_INDEPENDENT,
                                  120000, 120000, NULL);
    check(fed_robot >= 0 && kli_robot >= 0 && ori_robot >= 0
          && ind_robot >= 0, "team robots spawned");
    dtrek_snapshot(engine, &snapshot);
    check_int(snapshot.ships[kli_robot].robot_attack_speed, 4,
              "Klingon attack speed");
    check_int(snapshot.ships[ori_robot].robot_attack_speed, 9,
              "Orion attack speed");
    check_int(snapshot.ships[ind_robot].robot_attack_speed, 2,
              "Independent attack speed");

    /* Explicit values are stored verbatim; zero selects automatic defaults. */
    memset(&config, 0, sizeof(config));
    config.offense = 55;
    config.impatience = 66;
    config.attack_speed = 3;
    config.flags = DTREK_ROBOT_NONE;
    config.patrol_planet = -1;
    check_int(dtrek_set_robot(engine, robot, &config), DTREK_OK,
              "robot retuned");
    dtrek_snapshot(engine, &snapshot);
    check_int(snapshot.ships[robot].robot_offense, 55,
              "explicit offense stored");
    check_int(snapshot.ships[robot].robot_impatience, 66,
              "explicit impatience stored");
    check_int(snapshot.ships[robot].robot_attack_speed, 3,
              "explicit attack speed stored");

    /* Invalid configurations are rejected before a ship is allocated. */
    count_before = snapshot.ship_count;
    memset(&invalid, 0, sizeof(invalid));
    invalid.offense = 101;
    invalid.patrol_planet = -1;
    check_int(dtrek_spawn_robot(engine, "Bad", DTREK_TEAM_FEDERATION,
                                100000, 100000, &invalid),
              DTREK_ERR_ARGUMENT, "offense above 100 rejected");
    invalid.offense = 50;
    invalid.impatience = 101;
    check_int(dtrek_spawn_robot(engine, "Bad", DTREK_TEAM_FEDERATION,
                                100000, 100000, &invalid),
              DTREK_ERR_ARGUMENT, "impatience above 100 rejected");
    invalid.impatience = 50;
    invalid.attack_speed = DTREK_MAX_SPEED + 1;
    check_int(dtrek_spawn_robot(engine, "Bad", DTREK_TEAM_FEDERATION,
                                100000, 100000, &invalid),
              DTREK_ERR_ARGUMENT, "attack speed above maximum rejected");
    invalid.attack_speed = 3;
    invalid.flags = 0x0004;
    check_int(dtrek_spawn_robot(engine, "Bad", DTREK_TEAM_FEDERATION,
                                100000, 100000, &invalid),
              DTREK_ERR_ARGUMENT, "unknown robot flags rejected");
    invalid.flags = DTREK_ROBOT_NONE;
    invalid.patrol_planet = DTREK_MAX_PLANETS;
    check_int(dtrek_spawn_robot(engine, "Bad", DTREK_TEAM_FEDERATION,
                                100000, 100000, &invalid),
              DTREK_ERR_ARGUMENT, "bad patrol planet rejected");
    dtrek_snapshot(engine, &snapshot);
    check_int(snapshot.ship_count, count_before,
              "rejected robots do not consume slots");

    memset(&config, 0, sizeof(config));
    config.offense = 50;
    config.impatience = 50;
    config.attack_speed = 4;
    config.patrol_planet = -1;
    check_int(dtrek_set_robot(engine, player, &config), DTREK_ERR_STATE,
              "cannot tune a non-robot");
    check_int(dtrek_set_robot(engine, -1, &config), DTREK_ERR_SHIP,
              "set robot validates the ship");
    check_int(dtrek_clear_robot(engine, player), DTREK_ERR_STATE,
              "cannot clear a non-robot");
    check_int(dtrek_clear_robot(engine, -1), DTREK_ERR_SHIP,
              "clear robot validates the ship");

    dtrek_destroy(engine);
}

static void test_robot_target_and_combat(void)
{
    dtrek_engine *engine = dtrek_create(22);
    DtrekGameSnapshot snapshot;
    DtrekRobotConfig config;
    int player;
    int robot;
    int initial_shields;
    int torpedo;
    int damaged;

    check(engine != NULL, "robot combat engine created");
    if (engine == NULL) return;

    player = dtrek_spawn(engine, "Kirk", DTREK_TEAM_FEDERATION,
                         100000, 100000);
    memset(&config, 0, sizeof(config));
    config.offense = 100;
    config.impatience = 100;
    config.attack_speed = 6;
    config.patrol_planet = -1;
    robot = dtrek_spawn_robot(engine, "Warbird", DTREK_TEAM_ROMULAN,
                              102000, 100000, &config);
    check(player >= 0 && robot >= 0, "robot combat ships spawned");
    if (player < 0 || robot < 0) {
        dtrek_destroy(engine);
        return;
    }

    dtrek_snapshot(engine, &snapshot);
    initial_shields = shield_sum(&snapshot.ships[player]);

    check_int(dtrek_step(engine, 1), DTREK_OK, "robot combat step");
    dtrek_snapshot(engine, &snapshot);
    check_int(snapshot.ships[robot].robot_target, player,
              "robot locks the nearest enemy");
    check_int(snapshot.ships[robot].robot_state, DTREK_ROBOT_ATTACK,
              "robot enters attack");
    check_int(snapshot.ships[robot].robot_last_torpedo_tick, 1,
              "robot fires a torpedo on its first update");
    check_int(snapshot.ships[robot].robot_last_torpedo_target, player,
              "robot torpedo locks the target");
    torpedo = find_torpedo_by_owner(&snapshot, robot);
    check(torpedo >= 0, "robot torpedo is in flight");
    if (torpedo >= 0) {
        check_int(snapshot.torpedoes[torpedo].weapon_class,
                  DTREK_WPN_PLASMA, "Romulan robot uses plasma");
        check_int(snapshot.torpedoes[torpedo].target_slot, player,
                  "plasma homes on the target");
    }

    check_int(dtrek_step(engine, 9), DTREK_OK,
              "robot empties its tubes and fires beams");
    dtrek_snapshot(engine, &snapshot);
    check(snapshot.ships[robot].robot_last_beam_tick > 0,
          "robot fires a phaser volley");

    check_int(dtrek_step(engine, 30), DTREK_OK, "robot combat settles");
    dtrek_snapshot(engine, &snapshot);
    damaged = snapshot.ships[player].status != DTREK_STATUS_ALIVE
        || shield_sum(&snapshot.ships[player]) < initial_shields
        || snapshot.ships[player].hull < snapshot.ships[player].hull_max;
    check(damaged, "robot weapons damage the target");

    dtrek_destroy(engine);
}

static void test_robot_evasion(void)
{
    dtrek_engine *engine = dtrek_create(23);
    DtrekGameSnapshot snapshot;
    DtrekRobotConfig config;
    DtrekTorpedoResult shot;
    int player;
    int robot;
    int i;
    int fired;

    check(engine != NULL, "robot evasion engine created");
    if (engine == NULL) return;

    player = dtrek_spawn(engine, "Kirk", DTREK_TEAM_FEDERATION,
                         103000, 100000);
    memset(&config, 0, sizeof(config));
    config.offense = 100;
    config.impatience = 100;
    config.attack_speed = 5;
    config.patrol_planet = -1;
    robot = dtrek_spawn_robot(engine, "Warbird", DTREK_TEAM_ROMULAN,
                              100000, 100000, &config);
    check(player >= 0 && robot >= 0, "robot evasion ships spawned");
    if (player < 0 || robot < 0) {
        dtrek_destroy(engine);
        return;
    }

    fired = 1;
    for (i = 0; i < 5; ++i) {
        if (fire_torpedo(engine, player, 192, &shot) != 1) fired = 0;
    }
    check(fired, "player launches the torpedo salvo");

    check_int(dtrek_step(engine, 1), DTREK_OK, "robot evasion step");
    dtrek_snapshot(engine, &snapshot);
    check_int(snapshot.ships[robot].robot_state, DTREK_ROBOT_EVADE,
              "robot evades incoming torpedoes");
    check(snapshot.ships[robot].robot_evasion_fuse > 0,
          "robot evasion fuse is running");

    dtrek_destroy(engine);
}

static void test_robot_repair(void)
{
    dtrek_engine *engine = dtrek_create(24);
    DtrekGameSnapshot snapshot;
    DtrekRobotConfig config;
    DtrekPhaserResult shot;
    int robot;
    int enemy;
    int hull_damaged;

    check(engine != NULL, "robot repair engine created");
    if (engine == NULL) return;

    check_int(dtrek_place_planet(engine, 0,
                                 DTREK_PLANET_FUEL | DTREK_PLANET_REPAIR,
                                 DTREK_TEAM_FEDERATION, 10,
                                 100000, 100800, "Starbase"),
              DTREK_OK, "robot repair planet placed");

    memset(&config, 0, sizeof(config));
    config.offense = 100;
    config.impatience = 100;
    config.attack_speed = 4;
    config.flags = DTREK_ROBOT_ORBITER;
    config.patrol_planet = -1;
    robot = dtrek_spawn_robot(engine, "Sentinel", DTREK_TEAM_FEDERATION,
                              100000, 100800, &config);
    check(robot >= 0, "repair robot spawned");
    if (robot < 0) {
        dtrek_destroy(engine);
        return;
    }

    dtrek_snapshot(engine, &snapshot);
    check_int(snapshot.ships[robot].orbiting, 1,
              "orbiter robot enters orbit");
    check_int(snapshot.ships[robot].planet_slot, 0,
              "orbiter robot selects the base");

    enemy = dtrek_spawn(engine, "Targ", DTREK_TEAM_ROMULAN,
                        103000, 100000);
    check(enemy >= 0, "repair test attacker spawned");
    if (enemy < 0) {
        dtrek_destroy(engine);
        return;
    }

    check_int(dtrek_set_shields(engine, robot, 0), DTREK_OK,
              "robot shields lowered for the test");
    memset(&shot, 0, sizeof(shot));
    check_int(dtrek_fire_phaser(engine, enemy, 192, &shot), 1,
              "attacker hits the robot");
    check_int(shot.hit, 1, "attacker volley connects");
    dtrek_snapshot(engine, &snapshot);
    hull_damaged = snapshot.ships[robot].hull;
    check(hull_damaged < snapshot.ships[robot].hull_max,
          "robot is damaged");

    check_int(dtrek_step(engine, 30), DTREK_OK, "robot repair step");
    dtrek_snapshot(engine, &snapshot);
    check_int(snapshot.ships[robot].robot_state, DTREK_ROBOT_REPAIR,
              "robot enters repair state");
    check_int(snapshot.ships[robot].repair_mode, 1,
              "robot enables damage control");
    check_int(snapshot.ships[robot].shields_up, 0,
              "robot drops shields to repair");
    check(snapshot.ships[robot].hull > hull_damaged,
          "robot repairs damage at the starbase");

    dtrek_destroy(engine);
}

static void test_robot_patrol(void)
{
    dtrek_engine *engine = dtrek_create(25);
    DtrekGameSnapshot snapshot;
    DtrekRobotConfig config;
    int robot;
    int start_x;
    int start_y;

    check(engine != NULL, "robot patrol engine created");
    if (engine == NULL) return;

    check_int(dtrek_place_planet(engine, 0, DTREK_PLANET_FUEL,
                                 DTREK_TEAM_FEDERATION, 10,
                                 110000, 100000, "Patrol Base"),
              DTREK_OK, "robot patrol planet placed");

    memset(&config, 0, sizeof(config));
    config.offense = 50;
    config.impatience = 50;
    config.attack_speed = 4;
    config.patrol_planet = -1;
    robot = dtrek_spawn_robot(engine, "Sentinel", DTREK_TEAM_FEDERATION,
                              100000, 100000, &config);
    check(robot >= 0, "patrol robot spawned");
    if (robot < 0) {
        dtrek_destroy(engine);
        return;
    }

    dtrek_snapshot(engine, &snapshot);
    start_x = snapshot.ships[robot].x;
    start_y = snapshot.ships[robot].y;

    check_int(dtrek_step(engine, 1), DTREK_OK, "robot patrol step");
    dtrek_snapshot(engine, &snapshot);
    check_int(snapshot.ships[robot].robot_state, DTREK_ROBOT_PATROL,
              "idle robot patrols");
    check_int(snapshot.ships[robot].robot_patrol_planet, 0,
              "robot selects a friendly patrol planet");
    check(snapshot.ships[robot].robot_patrol_fuse > 0,
          "robot patrol fuse is running");
    check_int(snapshot.ships[robot].desired_speed, 4,
              "robot patrol speed");
    check(snapshot.ships[robot].desired_course > 16
          && snapshot.ships[robot].desired_course < 96,
          "robot heads toward the patrol planet");

    check_int(dtrek_step(engine, 30), DTREK_OK, "robot patrol cruise");
    dtrek_snapshot(engine, &snapshot);
    check(snapshot.ships[robot].x > start_x + 100
          || isqrt_floor(distance_squared(snapshot.ships[robot].x,
                                          snapshot.ships[robot].y,
                                          start_x, start_y)) > 100,
          "robot moves on patrol");
    check_int(snapshot.ships[robot].robot_state, DTREK_ROBOT_PATROL,
              "robot stays on patrol");

    dtrek_destroy(engine);
}

static void test_robot_hazard_avoidance(void)
{
    dtrek_engine *engine = dtrek_create(30);
    DtrekGameSnapshot snapshot;
    DtrekRobotConfig config;
    int robot;

    check(engine != NULL, "robot hazard engine created");
    if (engine == NULL) return;

    check_int(dtrek_place_planet(engine, 0, DTREK_PLANET_BLACKHOLE,
                                 DTREK_TEAM_INDEPENDENT, 0,
                                 105000, 100000, "Void"),
              DTREK_OK, "robot hazard black hole placed");

    memset(&config, 0, sizeof(config));
    config.offense = 50;
    config.impatience = 50;
    config.attack_speed = 4;
    config.patrol_planet = -1;
    robot = dtrek_spawn_robot(engine, "Sentinel", DTREK_TEAM_FEDERATION,
                              100000, 100000, &config);
    check(robot >= 0, "hazard robot spawned");
    if (robot < 0) {
        dtrek_destroy(engine);
        return;
    }

    check_int(dtrek_step(engine, 1), DTREK_OK, "robot hazard step");
    dtrek_snapshot(engine, &snapshot);
    check_int(snapshot.ships[robot].robot_state, DTREK_ROBOT_EVADE,
              "robot evades a black hole");
    check(snapshot.ships[robot].robot_evasion_fuse > 0,
          "robot hazard avoidance fuse is running");
    check_int(snapshot.ships[robot].desired_course, 192,
              "robot runs directly away from the hazard");
    check_int(snapshot.ships[robot].status, DTREK_STATUS_ALIVE,
              "robot survives hazard avoidance");

    check_int(dtrek_step(engine, 1), DTREK_OK, "robot hazard hold step");
    dtrek_snapshot(engine, &snapshot);
    check_int(snapshot.ships[robot].robot_state, DTREK_ROBOT_EVADE,
              "robot holds its hazard avoidance course");
    check(snapshot.ships[robot].robot_evasion_fuse > 0,
          "robot still holds the hazard fuse");

    dtrek_destroy(engine);
}

static void run_robot_scenario(uint32_t seed, DtrekGameSnapshot *snapshot)
{
    dtrek_engine *engine = dtrek_create(seed);
    DtrekRobotConfig config;

    memset(snapshot, 0, sizeof(*snapshot));
    if (engine == NULL) return;

    dtrek_place_planet(engine, 0,
        DTREK_PLANET_FUEL | DTREK_PLANET_REPAIR,
        DTREK_TEAM_FEDERATION, 10, 100000, 100000, "Earth");
    dtrek_place_planet(engine, 1, DTREK_PLANET_REPAIR,
        DTREK_TEAM_ROMULAN, 10, 44444, 44444, "Chulak");

    dtrek_spawn(engine, "Kirk", DTREK_TEAM_FEDERATION, 100000, 102000);
    dtrek_spawn(engine, "Targ", DTREK_TEAM_ROMULAN, 105000, 99000);

    memset(&config, 0, sizeof(config));
    config.offense = 80;
    config.impatience = 80;
    config.attack_speed = 5;
    config.patrol_planet = -1;
    dtrek_spawn_robot(engine, "Warbird", DTREK_TEAM_ROMULAN,
                      104000, 98000, &config);

    dtrek_step(engine, 150);
    dtrek_snapshot(engine, snapshot);
    dtrek_destroy(engine);
}

static void test_robot_clear_and_determinism(void)
{
    dtrek_engine *engine = dtrek_create(26);
    DtrekGameSnapshot snapshot;
    DtrekGameSnapshot first;
    DtrekGameSnapshot second;
    DtrekRobotConfig config;
    DtrekTorpedoResult shot;
    int robot;

    check(engine != NULL, "robot clear engine created");
    if (engine == NULL) return;

    memset(&config, 0, sizeof(config));
    config.offense = 60;
    config.impatience = 60;
    config.attack_speed = 4;
    config.patrol_planet = -1;
    robot = dtrek_spawn_robot(engine, "Sentinel", DTREK_TEAM_FEDERATION,
                              100000, 100000, &config);
    check(robot >= 0, "clear test robot spawned");
    if (robot < 0) {
        dtrek_destroy(engine);
        return;
    }

    memset(&shot, 0, sizeof(shot));
    check_int(fire_torpedo(engine, robot, 64, &shot), 1,
              "robot launches a torpedo");
    dtrek_snapshot(engine, &snapshot);
    check_int(snapshot.ship_count, 1, "clear test ship count");
    check(count_torpedoes(&snapshot) > 0, "torpedo in flight before clear");

    check_int(dtrek_clear_robot(engine, robot), DTREK_OK,
              "robot cleared");
    dtrek_snapshot(engine, &snapshot);
    check_int(snapshot.ships[robot].in_use, 0, "cleared slot is free");
    check_int(snapshot.ship_count, 0, "ship count decremented");
    check_int(count_torpedoes(&snapshot), 0,
              "cleared robot torpedoes removed");
    check_int(dtrek_step(engine, 5), DTREK_OK,
              "engine steps cleanly after clearing");

    dtrek_destroy(engine);

    run_robot_scenario(27, &first);
    run_robot_scenario(27, &second);
    check(memcmp(&first, &second, sizeof(first)) == 0,
          "robot simulation is deterministic");
    check(first.ships[2].is_robot, "determinism scenario robot survived");
}

int main(void)
{
    test_metadata_and_spawn();
    test_rng_determinism();
    test_movement_and_shields();
    test_turn_rate();
    test_damage_and_explosion();
    test_subsystem_damage_and_repair();
    test_torpedo_launch_and_reload();
    test_torpedo_blast_and_friendly_fire();
    test_torpedo_point_defense();
    test_torpedo_expiry_and_bounds();
    test_ordnance_profiles();
    test_missile_tracking();
    test_plasma_decay();
    test_chaff();
    test_chaff_point_defense();
    test_mine();
    test_class_point_defense();
    test_launch_validation();
    test_reset();
    test_galaxy_generation();
    test_wanderer_movement();
    test_planet_placement();
    test_orbit_and_leave();
    test_orbit_validation();
    test_gamma_services();
    test_black_hole_and_pulsar();
    test_torpedo_planet_impact();
    test_reset_clears_planets();
    test_robot_metadata_and_config();
    test_robot_target_and_combat();
    test_robot_evasion();
    test_robot_repair();
    test_robot_patrol();
    test_robot_hazard_avoidance();
    test_robot_clear_and_determinism();

    if (failures != 0) {
        fprintf(stderr, "%d of %d checks failed\n", failures, checks);
        return 1;
    }

    printf("dtrek_core: all %d checks passed\n", checks);
    return 0;
}
