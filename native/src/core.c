/*
 * core.c - DTREK simulation core.
 *
 * Rules implemented in this slice:
 *   - fixed 100 ms updates, 200,000-unit galaxy, reflective boundaries
 *   - original turn / acceleration / deceleration / fuel formulas
 *   - original 256-direction Sin/Cos tables and Ihypot
 *   - multi-bank phaser fire, range attenuation, shield facing
 *   - shield -> armor -> hull damage and ten-tick explosions
 *   - photon torpedoes: launch, reload, wobble, fuse, proximity fuse,
 *     area blast damage, and defensive phaser interception
 *   - guided missiles and plasma torpedoes, chaff clouds, and mines
 *   - planets, starbases, orbits, fuel/rearm/repair, black holes, pulsars,
 *     nebulae, and torpedo/planet interactions
 *   - POSIX lrand48-compatible deterministic RNG
 *
 * The built-in trainer ship is the Reliant-class default from
 * shiparchives/shipdefaults.  Robots, planetary armies, and subsystem damage
 * are deliberately left for later slices.
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dtrek_internal.h"

#define DTREK_ALL_TEAM_MASK 0x1F
#define DTREK_VERSION_STRING "dtrek_core 0.6.0"

static const DtrekShipSpec kReliantSpec = {
    150,                    /* s_hull (ktons) */
    213,                    /* s_hull_str */
    1,                      /* s_armor */
    { 158, 92, 110, 92 },   /* front, right, rear, left shields */
    8500,                   /* s_maxfuel */
    27,                     /* s_engines */
    6,                      /* s_maxspeed */
    /*
     * Shipmenu stores maneuverability as a 0..1234 point buy and derives
     * the helm field with 500000 / hull * engines + points * 8000.  The
     * stock Reliant spends no points, so the archive yields 89991, not the
     * 70% figure shown in the builder UI.  Using 70 here made warp turns
     * ~1300 times too slow.
     */
    89991,                  /* s_turns */
    72,                     /* s_accint */
    144,                    /* s_decint */
    4,                      /* s_warpcost */
    11,                     /* s_recharge */
    /*
     * Damage control.  The archive stores ffield(DAMAGE_CONTROL, 3), i.e.
     * 150 + 30 * 3 = 240 in 1/256 points per 30-update repair pulse.  The
     * original divides this by REPAIRFUSE (30) for per-update shield regen.
     */
    240,                    /* s_repair (damage control) */
    3,                      /* s_cargo */
    6,                      /* s_beam_ktons (beamsize() = 5.625, rounded) */
    5,                      /* s_torp_ktons (torpsize() = 4.875, rounded) */

    4,                      /* s_beams */
    7500,                   /* s_beamrange */
    30,                     /* s_beamdamage */
    2,                      /* s_beamaccuracy */
    30,                     /* s_beamreload (ticks) */
    240,                    /* s_beamcost */
    250,                    /* s_beamatten */
    DTREK_WPN_PHASER,       /* s_beamclass */

    5,                      /* s_torps */
    14,                     /* s_torpspeed */
    42,                     /* s_torpdamage */
    20,                     /* s_torpduration (ticks, plus 0..29 random) */
    20,                     /* s_torpreload (ticks) */
    0,                      /* s_torpdecay (photon) */
    420,                    /* s_torpcost */
    DTREK_WPN_PHOTON        /* s_torpclass */
};

/*
 * Stage 2 ordnance, scaled from the Reliant-class photon values and the
 * weapon prototypes in shipmenu.c.  "speed" is the original s_torpspeed unit
 * multiplied by WARP1 during flight.
 */
static const DtrekOrdnanceSpec kOrdnanceSpecs[DTREK_ORDNANCE_COUNT] = {
    {
        DTREK_WPN_PHOTON, 14, 42, 20, 20, 0, 420, 0, 0,
        "Photon torpedo"
    },
    {
        DTREK_WPN_MISSILE, 8, 21, 50, 30, 0, 210, 0, 1,
        "Missile"
    },
    {
        DTREK_WPN_PLASMA, 8, 110, 100, 60, 8, 1100, 0, 1,
        "Plasma torpedo"
    },
    {
        DTREK_WPN_CHAFF, 0, 19, 50, 50, 0, 95, DTREK_CHAFF_ARM, 0,
        "Chaff"
    },
    {
        DTREK_WPN_MINE, 0, 35, 60, 60, 0, 35, DTREK_MINE_ARM, 0,
        "Mine"
    }
};

static const DtrekOrdnanceSpec *ordnance_for_class(int weapon_class)
{
    if (weapon_class < 0 || weapon_class >= DTREK_ORDNANCE_COUNT) {
        return NULL;
    }

    return &kOrdnanceSpecs[weapon_class];
}

static int default_class_for_team(int team)
{
    switch (team) {
    case DTREK_TEAM_ROMULAN:
        return DTREK_WPN_PLASMA;
    case DTREK_TEAM_KLINGON:
        return DTREK_WPN_MISSILE;
    default:
        return DTREK_WPN_PHOTON;
    }
}

static int is_guided_class(int weapon_class)
{
    return weapon_class == DTREK_WPN_MISSILE
        || weapon_class == DTREK_WPN_PLASMA;
}

static int is_valid_ship(const DtrekEngine *engine, int32_t slot)
{
    return engine != NULL
        && slot >= 0
        && slot < DTREK_MAX_PLAYERS
        && engine->ships[slot].in_use;
}

static int is_alive(const DtrekShip *ship)
{
    return ship->in_use && ship->status == DTREK_STATUS_ALIVE;
}

static void copy_ship_name(char *dest, size_t dest_size, const char *source)
{
    size_t i = 0;

    if (source == NULL || source[0] == '\0') source = "Player";
    while (i + 1 < dest_size && source[i] != '\0') {
        dest[i] = source[i];
        ++i;
    }
    dest[i] = '\0';
}

/* Team planet names from planets.c, in slot order 1..9 per team. */
static const char *const kPlanetNames[40] = {
    "Earth",
    "Sasus",
    "Candeleron",
    "Beta III",
    "Janus",
    "Deneb VI",
    "Ceti IV",
    "Altar",
    "Dekar",
    "Daltus",
    "Romulus",
    "Ethen",
    "Amur",
    "Remus",
    "Bal",
    "Tahndar",
    "Dact",
    "Sirius II",
    "Rakhir",
    "Rus",
    "Klingus",
    "Malatrakir",
    "Amakron",
    "Laltir",
    "Khartair",
    "Monpur III",
    "Sectus",
    "Makus",
    "Jakar",
    "Gendus",
    "Orion",
    "Amterion",
    "Lumecis",
    "Bitar V",
    "Prastan",
    "Sorlen",
    "Zetus",
    "Jih",
    "Markus II",
    "Oren"
};

/* Names used for independents and spatial features, from planets.c. */
static const char *const kPreNames[] = {
    "Darkstar", "Hermes", "Syrinx", "Styx", "Midnight", "Lifebane",
    "Mutara", "Paradise", "Cerebus", "TNG-473", "Pagar", "Golar",
    "Lethal", "Forbidden", "Detroit", "Walrus", "Xanadu", "Glorious",
    "Purgatory", "Vortigern", "Yagara", "Galileo's", "Abandoned",
    "Fenson", "Menoran", "Gartivia", "Dragon", "Ascolais", "Pavia",
    "Xeres", "Ilibrial", "Hanion", "Craven", "Hassan's", "Inkarien",
    "Justice", "Ajax", "Mordor", "Hades", "Kipling's", "B&D's",
    "Schwartz", "Calenor", "Novgren", "Deadly", "Quiet", "Raed's",
    "Basilisk", "Untouched", "Half", "RUXK-23", "TGP-371", "BXX II"
};

static int planet_is_valid(const DtrekEngine *engine, int slot)
{
    return engine != NULL
        && slot >= 0
        && slot < DTREK_MAX_PLANETS
        && engine->planets[slot].in_use;
}

static int planet_is_friendly(const DtrekShip *ship, const DtrekPlanet *planet)
{
    return planet->owner == ship->team
        || planet->owner == DTREK_TEAM_INDEPENDENT;
}

static int nearest_planet(const DtrekEngine *engine, int x, int y,
                          int *distance)
{
    int slot;
    int best = -1;
    int best_distance = INT_MAX;

    for (slot = 0; slot < DTREK_MAX_PLANETS; ++slot) {
        const DtrekPlanet *planet = &engine->planets[slot];
        int dist;

        if (!planet->in_use) continue;
        dist = dtrek_ihypot(x - planet->x, y - planet->y);
        if (dist < best_distance) {
            best_distance = dist;
            best = slot;
        }
    }

    if (distance != NULL) *distance = best >= 0 ? best_distance : 0;
    return best;
}

static void update_nearest_planet(DtrekEngine *engine, DtrekShip *ship)
{
    ship->nearest_planet_slot = nearest_planet(
        engine, ship->x, ship->y, &ship->nearest_planet_dist);
}

static int shield_hit(uint8_t weapon_course, uint8_t player_course)
{
    uint8_t delta = (uint8_t)(weapon_course - player_course + 128);

    if (delta >= 224 || delta <= 32) return DTREK_SHIELD_FRONT;
    if (delta <= 96) return DTREK_SHIELD_RIGHT;
    if (delta <= 160) return DTREK_SHIELD_REAR;
    return DTREK_SHIELD_LEFT;
}

static void changedir(DtrekShip *ship)
{
    uint8_t delta;
    uint8_t turns;

    if (ship->speed == 0) {
        ship->course = ship->desired_course;
        ship->subdir = 0;
        return;
    }

    ship->subdir += ship->spec.s_turns * ship->engine_health
        / 256 / (1 << ship->speed);

    turns = (uint8_t)(ship->subdir / 1000);
    if (turns != 0) {
        delta = (uint8_t)(ship->course - ship->desired_course);
        if (turns > delta) {
            ship->course = ship->desired_course;
            ship->subdir = 0;
        }
        else if (delta > 127) {
            ship->course = (uint8_t)(ship->course + turns);
            ship->subdir %= 1000;
        }
        else {
            ship->course = (uint8_t)(ship->course - turns);
            ship->subdir %= 1000;
        }
    }
}

static void reload_beams(DtrekShip *ship)
{
    if (ship->beams_unloaded <= 0) {
        ship->beams_reload = 0;
        return;
    }

    if (--ship->beams_reload <= 0) {
        if (--ship->beams_unloaded > 0) {
            ship->beams_reload = ship->spec.s_beamreload;
        }
        else {
            ship->beams_reload = 0;
        }
    }
}

static void reload_torps(DtrekShip *ship)
{
    if (ship->torps_unloaded <= 0) {
        ship->torps_reload = 0;
        return;
    }

    if (--ship->torps_reload <= 0) {
        if (--ship->torps_unloaded > 0) {
            ship->torps_reload = ship->torp_reload_interval;
        }
        else {
            ship->torps_reload = 0;
        }
    }
}

static void apply_black_hole(DtrekEngine *engine, DtrekShip *ship);
static void apply_pulsar(DtrekEngine *engine, DtrekShip *ship);
static void service_ship(DtrekEngine *engine, DtrekShip *ship);
static void repair_ship(DtrekEngine *engine, DtrekShip *ship);

static void step_orbit(DtrekEngine *engine, DtrekShip *ship)
{
    DtrekPlanet *planet;

    if (!planet_is_valid(engine, ship->planet_slot)
        || (engine->planets[ship->planet_slot].flags
            & (DTREK_PLMASK_BLACKHOLE | DTREK_PLMASK_NEBULA))) {
        ship->orbiting = 0;
        ship->planet_slot = -1;
        return;
    }

    planet = &engine->planets[ship->planet_slot];
    {
        int dir = dtrek_atan2_course(
            planet->y - ship->y, ship->x - planet->x);
        dir = (dir + DTREK_ORBIT_ROTATION) & 0xff;

        ship->x = planet->x + (int)(DTREK_ORBIT_DISTANCE
            * dtrek_cos_table[dir]);
        ship->y = planet->y + (int)(DTREK_ORBIT_DISTANCE
            * dtrek_sin_table[dir]);
        ship->course = (dir + 64) & 0xff;
        ship->desired_course = ship->course;
        ship->speed = 0;
        ship->desired_speed = 0;
        ship->subspeed = 0;
        ship->subdir = 0;
    }

    update_nearest_planet(engine, ship);
}

static void step_ship(DtrekEngine *engine, DtrekShip *ship)
{
    int maxspeed;
    int fuel_cost;
    int fuel_gain;

    if (!ship->in_use) return;

    if (ship->status == DTREK_STATUS_EXPLODING) {
        if (--ship->explode_fuse <= 0) {
            ship->status = DTREK_STATUS_DEAD;
        }
        return;
    }

    if (ship->status != DTREK_STATUS_ALIVE) return;

    if (ship->orbiting) {
        step_orbit(engine, ship);
        if (!ship->orbiting) return;
    }
    else {
        if (ship->course != ship->desired_course) {
            changedir(ship);
        }

        maxspeed = ship->spec.s_maxspeed * ship->engine_health / 256;
        if (ship->desired_speed > maxspeed) ship->desired_speed = maxspeed;
        if (ship->fuel <= 0) ship->desired_speed = 0;

        if (ship->desired_speed > ship->speed) {
            ship->subspeed += ship->spec.s_accint * ship->engine_health / 256;
        }
        else if (ship->desired_speed < ship->speed) {
            ship->subspeed -= ship->spec.s_decint * ship->engine_health / 256;
        }

        if (ship->subspeed / 1000 != 0) {
            ship->speed += ship->subspeed / 1000;
            ship->subspeed %= 1000;
            if (ship->speed < 0) ship->speed = 0;
            if (ship->speed > maxspeed) ship->speed = maxspeed;
        }

        fuel_cost = ship->spec.s_warpcost * ship->speed;
        ship->fuel -= fuel_cost;
        if (ship->fuel <= 0) {
            ship->fuel = 0;
            ship->desired_speed = 0;
        }

        ship->x += (int)(ship->speed * dtrek_cos_table[ship->course]
            * DTREK_WARP1);
        ship->y += (int)(ship->speed * dtrek_sin_table[ship->course]
            * DTREK_WARP1);

        /* Reflective galaxy boundaries, exactly as daemon.c. */
        if (ship->x < 0) {
            ship->x = -ship->x;
            ship->course = (256 - ship->course) & 0xff;
            ship->desired_course = (256 - ship->desired_course) & 0xff;
        }
        else if (ship->x > DTREK_GALAXY_WIDTH) {
            ship->x = (DTREK_GALAXY_WIDTH * 2) - ship->x;
            ship->course = (256 - ship->course) & 0xff;
            ship->desired_course = (256 - ship->desired_course) & 0xff;
        }

        if (ship->y < 0) {
            ship->y = -ship->y;
            ship->course = (128 - ship->course) & 0xff;
            ship->desired_course = (128 - ship->desired_course) & 0xff;
        }
        else if (ship->y > DTREK_GALAXY_WIDTH) {
            ship->y = (DTREK_GALAXY_WIDTH * 2) - ship->y;
            ship->course = (128 - ship->course) & 0xff;
            ship->desired_course = (128 - ship->desired_course) & 0xff;
        }

        update_nearest_planet(engine, ship);
        apply_black_hole(engine, ship);
        if (!is_alive(ship)) return;
        update_nearest_planet(engine, ship);
        apply_pulsar(engine, ship);
        if (!is_alive(ship)) return;
    }

    service_ship(engine, ship);

    fuel_gain = ship->recharge * 2;
    if (ship->orbiting && ship->at_fuel_planet) fuel_gain += 6;
    ship->fuel += fuel_gain;
    if (ship->fuel > ship->fuel_max) ship->fuel = ship->fuel_max;

    repair_ship(engine, ship);
    reload_beams(ship);
    reload_torps(ship);
}

/*
 * Stage 5 hit table.  Raw repair rates are the per-item values from sys[] in
 * shipmenu.c, in 1/256 of a kton per REPAIRFUSE-update pulse.  The ship
 * builder multiplies self repair by ffield(DAMAGE_CONTROL, points) and the
 * planet rate by REPAIRFUSE/5.
 */
static void system_repair_rates(int item, int *self_rate, int *base_rate)
{
    switch (item) {
    case DTREK_SYS_ARMOR:
        *self_rate = 0;
        *base_rate = 250;
        break;
    case DTREK_SYS_HULL:
        *self_rate = 100;
        *base_rate = 500;
        break;
    case DTREK_SYS_ENGINE:
        *self_rate = 25;
        *base_rate = 75;
        break;
    case DTREK_SYS_FUEL:
        *self_rate = 20;
        *base_rate = 75;
        break;
    case DTREK_SYS_BEAM_NUM:
        *self_rate = 30;
        *base_rate = 90;
        break;
    case DTREK_SYS_TORP_NUM:
        *self_rate = 15;
        *base_rate = 45;
        break;
    case DTREK_SYS_DILITHIUM:
        *self_rate = 7;
        *base_rate = 25;
        break;
    case DTREK_SYS_CARGO:
        *self_rate = 30;
        *base_rate = 75;
        break;
    default:
        *self_rate = 0;
        *base_rate = 0;
        break;
    }
}

static void add_system_unit(DtrekShip *ship, int item, int subitem, int size,
                            int repair_multiplier, int planet_multiplier)
{
    DtrekSystemUnit *unit;
    int self_rate;
    int base_rate;

    if (ship->system_count >= DTREK_MAX_SYSTEMS) return;

    unit = &ship->system[ship->system_count++];
    unit->item = item;
    unit->subitem = subitem;
    unit->current_size = size;
    unit->undamaged_size = size;
    unit->fractional_size = 0;

    system_repair_rates(item, &self_rate, &base_rate);
    unit->repair_rate = (self_rate * repair_multiplier) >> 8;
    unit->planet_repair_rate = base_rate * planet_multiplier;

    if (item != DTREK_SYS_ARMOR && item != DTREK_SYS_HULL) {
        ship->system_size += size;
    }
}

static void build_ship_systems(DtrekShip *ship, const DtrekShipSpec *spec)
{
    int repair_multiplier = spec->s_repair * (DTREK_REPAIR_FUSE / 5);
    int planet_multiplier = DTREK_REPAIR_FUSE / 5;
    int fuel_ktons = spec->s_maxfuel / 500;
    int dilithium_ktons = spec->s_recharge - 2;
    int i;

    ship->system_count = 0;
    ship->system_size = 0;
    ship->system_damaged = 0;
    ship->system_repairable = 0;

    /* ARMOR MUST BE SYSTEM UNIT #0 and HULL #1, exactly as shipmenu.c. */
    add_system_unit(ship, DTREK_SYS_ARMOR, 0, spec->s_armor,
                    repair_multiplier, planet_multiplier);
    add_system_unit(ship, DTREK_SYS_HULL, 0, ship->hull_max,
                    repair_multiplier, planet_multiplier);
    add_system_unit(ship, DTREK_SYS_ENGINE, 0, spec->s_engines,
                    repair_multiplier, planet_multiplier);
    add_system_unit(ship, DTREK_SYS_FUEL, 0, fuel_ktons,
                    repair_multiplier, planet_multiplier);

    for (i = 0; i < spec->s_beams; ++i) {
        add_system_unit(ship, DTREK_SYS_BEAM_NUM, i, spec->s_beam_ktons,
                        repair_multiplier, planet_multiplier);
    }

    for (i = 0; i < spec->s_torps; ++i) {
        add_system_unit(ship, DTREK_SYS_TORP_NUM, i, spec->s_torp_ktons,
                        repair_multiplier, planet_multiplier);
    }

    if (dilithium_ktons < 0) dilithium_ktons = 0;
    add_system_unit(ship, DTREK_SYS_DILITHIUM, 0, dilithium_ktons,
                    repair_multiplier, planet_multiplier);
    add_system_unit(ship, DTREK_SYS_CARGO, 0, spec->s_cargo,
                    repair_multiplier, planet_multiplier);
}

static void initialize_ship(DtrekShip *ship, const DtrekShipSpec *spec,
                            const char *name, int team, int x, int y)
{
    int i;

    memset(ship, 0, sizeof(*ship));
    ship->in_use = 1;
    ship->spec = *spec;
    copy_ship_name(ship->name, sizeof(ship->name), name);
    ship->team = team;
    ship->team_mask = DTREK_TEAM_MASK(team);
    ship->hostile_mask = DTREK_ALL_TEAM_MASK & ~ship->team_mask;
    ship->status = DTREK_STATUS_ALIVE;
    ship->shields_up = 1;

    ship->x = x;
    ship->y = y;
    ship->course = 64;
    ship->desired_course = 64;
    ship->speed = 0;
    ship->desired_speed = 0;

    ship->hull_max = spec->s_hull * spec->s_hull_str * 100
        / 150 / 256;
    ship->hull = ship->hull_max;
    ship->armor_max = spec->s_armor;
    ship->armor = ship->armor_max;

    for (i = 0; i < DTREK_NUM_SHIELDS; ++i) {
        ship->shield_max[i] = spec->s_shield[i];
        ship->shield[i] = spec->s_shield[i];
    }

    ship->fuel_max = spec->s_maxfuel;
    ship->fuel = ship->fuel_max;
    ship->recharge = spec->s_recharge;
    ship->cargo = spec->s_cargo;
    ship->engine_health = 256;
    ship->beams_total = spec->s_beams;
    ship->torps_total = spec->s_torps;
    ship->beams_unloaded = 0;
    ship->beams_reload = 0;
    ship->torps_unloaded = 0;
    ship->torps_reload = 0;
    ship->torp_class = default_class_for_team(team);
    ship->torp_reload_interval =
        kOrdnanceSpecs[ship->torp_class].reload;

    build_ship_systems(ship, spec);

    ship->orbiting = 0;
    ship->planet_slot = -1;
    ship->repair_mode = 0;
    ship->nearest_planet_slot = -1;
    ship->nearest_planet_dist = 0;
    ship->at_fuel_planet = 0;
    ship->at_repair_planet = 0;

    /* Stage 4 robot state; robots opt in through dtrek_spawn_robot. */
    ship->is_robot = 0;
    ship->robot_flags = DTREK_ROBOT_NONE;
    ship->robot_state = DTREK_ROBOT_IDLE;
    ship->robot_target = -1;
    ship->robot_patrol_planet = -1;
    ship->robot_evade_torpedo = -1;
    ship->robot_last_beam_tick = -1;
    ship->robot_last_beam_target = -1;
    ship->robot_last_torpedo_tick = -1;
    ship->robot_last_torpedo_slot = -1;
    ship->robot_last_torpedo_target = -1;
}

static void kill_ship(DtrekShip *victim, DtrekShip *attacker,
                      DtrekPhaserResult *result)
{
    if (!is_alive(victim)) return;

    victim->hull = 0;
    victim->status = DTREK_STATUS_EXPLODING;
    victim->explode_fuse = DTREK_EXPLODE_TICKS;
    if (attacker != NULL) attacker->kills++;
    if (result != NULL) result->target_destroyed = 1;
}

/*
 * Original damage.c:damage_system().  Armor and hull absorb the full hit up
 * to their remaining size; every other system takes a random slice capped by
 * DAMAGE_SPLITUP.  The item-specific side effects update the aggregate ship
 * fields the rest of the simulation and snapshots use.
 */
static int damage_system(DtrekEngine *engine, DtrekShip *attacker,
                         DtrekShip *victim, int system_index, int full_damage,
                         DtrekPhaserResult *result)
{
    DtrekSystemUnit *unit;
    int old_size;
    int damage;

    if (system_index < 0 || system_index >= victim->system_count) return 0;
    unit = &victim->system[system_index];
    if (full_damage <= 0 || unit->current_size <= 0) return 0;

    if (unit->item == DTREK_SYS_ARMOR || unit->item == DTREK_SYS_HULL) {
        damage = full_damage <= unit->current_size
            ? full_damage : unit->current_size;
    }
    else {
        damage = dtrek_rng_below(&engine->rng, unit->current_size) + 1;
        if (full_damage < damage) damage = full_damage;
        if (damage > DTREK_DAMAGE_SPLITUP) {
            damage = dtrek_rng_below(&engine->rng, DTREK_DAMAGE_SPLITUP) + 1;
        }
        victim->system_size -= damage;
    }

    old_size = unit->current_size;
    if (old_size == unit->undamaged_size) unit->fractional_size = 0;
    unit->current_size -= damage;

    switch (unit->item) {
    case DTREK_SYS_HULL:
        victim->hull = unit->current_size;
        if (unit->current_size == 0) {
            kill_ship(victim, attacker, result);
            /* The original returns the full hit, not the hull absorbed. */
            return full_damage;
        }
        break;
    case DTREK_SYS_ENGINE:
        if (unit->undamaged_size > 0) {
            victim->engine_health = unit->current_size * 256
                / unit->undamaged_size;
        }
        break;
    case DTREK_SYS_FUEL:
        victim->fuel_max -= 500 * damage;
        if (victim->fuel > victim->fuel_max) victim->fuel = victim->fuel_max;
        break;
    case DTREK_SYS_BEAM_NUM:
        if (old_size == unit->undamaged_size && victim->beams_total > 0) {
            --victim->beams_total;
        }
        break;
    case DTREK_SYS_TORP_NUM:
        /* Plasma ships lose tube condition but not the tube itself. */
        if (victim->torp_class != DTREK_WPN_PLASMA
            && old_size == unit->undamaged_size) {
            if (victim->torps_total > 0) --victim->torps_total;
            /* The original treats a hit on an empty tube as lucky. */
            if (victim->torps_unloaded > 0) --victim->torps_unloaded;
        }
        break;
    case DTREK_SYS_DILITHIUM:
        victim->recharge -= damage;
        break;
    case DTREK_SYS_CARGO:
        victim->cargo -= damage;
        break;
    case DTREK_SYS_ARMOR:
        victim->armor = unit->current_size;
        break;
    default:
        break;
    }

    return damage;
}

static int apply_damage(DtrekEngine *engine, DtrekShip *attacker,
                        DtrekShip *victim, int damage, int weapon_class,
                        int shield_index, DtrekPhaserResult *result)
{
    int remaining;
    int absorbed;
    int hull_before;

    if (!is_alive(victim) || damage <= 0) return 0;

    remaining = damage;

    if (victim->shields_up
        && weapon_class != DTREK_WPN_TACHYON
        && shield_index >= 0
        && shield_index < DTREK_NUM_SHIELDS) {
        if (victim->shield[shield_index] >= remaining) {
            victim->shield[shield_index] -= remaining;
            if (result != NULL) result->shield_absorbed = remaining;
            return 0;
        }

        absorbed = victim->shield[shield_index];
        victim->shield[shield_index] = 0;
        victim->subshield = 0;
        remaining -= absorbed;
        if (result != NULL) result->shield_absorbed = absorbed;
    }

    if (remaining <= 0) return 0;

    /* Something got through the shields. */
    victim->system_damaged = 1;

    if (victim->armor > 0) {
        absorbed = damage_system(engine, attacker, victim, 0, remaining, result);
        remaining -= absorbed;
        if (result != NULL) result->armor_absorbed = absorbed;
        if (victim->system[0].repair_rate) victim->system_repairable = 1;
        if (remaining <= 0) return 0;
    }

    victim->system_repairable = 1;

    /*
     * The original lets the hull absorb its share and then spills the whole
     * post-shield, post-armor figure over the remaining systems.  It never
     * subtracts the hull absorption from that spill, so a hit can wreck more
     * subsystems than the hull actually took.  Preserve that quirk.
     */
    hull_before = victim->hull;
    damage_system(engine, attacker, victim, 1, remaining, result);
    victim->damage_taken += damage;
    if (result != NULL) result->hull_damage = hull_before - victim->hull;
    if (!is_alive(victim)) return 1;

    if (remaining > victim->system_size) remaining = victim->system_size;

    while (remaining > 0) {
        int hit;
        int index;
        int size;

        if (victim->system_size <= 0) break;

        hit = dtrek_rng_below(&engine->rng, victim->system_size) + 1;
        index = 2;
        while (index < victim->system_count
               && (size = victim->system[index].current_size) < hit) {
            hit -= size;
            ++index;
        }

        if (index >= victim->system_count) break;

        remaining -= damage_system(engine, attacker, victim, index,
                                   remaining, result);
        if (victim->system[index].repair_rate) {
            victim->system_repairable = 1;
        }
    }

    return 0;
}

static void service_ship(DtrekEngine *engine, DtrekShip *ship)
{
    ship->at_fuel_planet = 0;
    ship->at_repair_planet = 0;

    if (!ship->orbiting || !planet_is_valid(engine, ship->planet_slot)) {
        return;
    }

    {
        const DtrekPlanet *planet = &engine->planets[ship->planet_slot];

        if (!planet_is_friendly(ship, planet)) return;
        if (planet->flags & DTREK_PLMASK_FUEL) ship->at_fuel_planet = 1;
        if (planet->flags & DTREK_PLMASK_REPAIR) ship->at_repair_planet = 1;
    }
}

/*
 * Original daemon.c black-hole gravity: pull toward the nearest object when
 * it is a black hole, then apply one 500-strength planet hit inside 1.5
 * orbit radii.  Wormhole teleportation is left for a later slice.
 */
static void apply_black_hole(DtrekEngine *engine, DtrekShip *ship)
{
    const DtrekPlanet *planet;

    if (!planet_is_valid(engine, ship->nearest_planet_slot)) return;
    planet = &engine->planets[ship->nearest_planet_slot];
    if (!(planet->flags & DTREK_PLMASK_BLACKHOLE)) return;

    {
        int dx = planet->x - ship->x;
        int dy = planet->y - ship->y;
        int dist = dtrek_ihypot(dx, dy);

        if (dist > 0) {
            int strength = (DTREK_BLACKHOLE_STRENGTH
                - (dist * DTREK_BLACKHOLE_STRENGTH / DTREK_NEBULA_RADIUS))
                * DTREK_WARP1;

            if (strength > 0) {
                ship->x += dx * strength / dist;
                ship->y += dy * strength / dist;

                if (dist < (DTREK_ORBIT_DISTANCE * 3 / 2)) {
                    int shield_index = dtrek_rng_below(&engine->rng, 4);
                    apply_damage(engine, NULL, ship, 500, DTREK_WPN_PLANET,
                                 shield_index, NULL);
                }
            }
        }
    }
}

/* Original daemon.c: a pulsar within ORBDIST+10 hits for 500. */
static void apply_pulsar(DtrekEngine *engine, DtrekShip *ship)
{
    const DtrekPlanet *planet;

    if (!planet_is_valid(engine, ship->nearest_planet_slot)) return;
    if (ship->nearest_planet_dist > DTREK_ORBIT_DISTANCE + 10) return;

    planet = &engine->planets[ship->nearest_planet_slot];
    if (planet->flags & DTREK_PLMASK_PULSAR) {
        int shield_index = dtrek_rng_below(&engine->rng, 4);
        apply_damage(engine, NULL, ship, 500, DTREK_WPN_PLANET,
                     shield_index, NULL);
    }
}

/*
 * Original repair.c:repair_player(), called once per REPAIRFUSE updates.
 * Shield regeneration remains per update (the original pulse divided by the
 * fuse gives the same rate) so the HUD changes smoothly.  Subsystem repair,
 * including the fractional accumulator and effect restoration, runs on the
 * authentic 30-update cadence.
 */
static void repair_ship(DtrekEngine *engine, DtrekShip *ship)
{
    int repair_shift;
    int at_repair_base;
    int rpr;
    int i;

    if (!is_alive(ship)) return;

    repair_shift = (ship->repair_mode && ship->speed == 0) ? 1 : 0;
    at_repair_base = repair_shift
        && ship->orbiting
        && ship->at_repair_planet;

    /* s_repair is stored per 30-update pulse, so divide for a per-tick rate. */
    rpr = (ship->spec.s_repair << (repair_shift + 1)) / DTREK_REPAIR_FUSE;
    if (at_repair_base) rpr *= 2;

    ship->subshield += rpr;
    if (ship->subshield & 0xff00) {
        int points = ship->subshield >> 8;

        ship->subshield &= 0xff;
        for (i = 0; i < DTREK_NUM_SHIELDS; ++i) {
            if (ship->shield[i] < ship->shield_max[i]) {
                ship->shield[i] += points;
                if (ship->shield[i] > ship->shield_max[i]) {
                    ship->shield[i] = ship->shield_max[i];
                }
            }
        }
    }

    /* A friendly repair base rearms functional banks and tubes every update. */
    if (at_repair_base) {
        ship->beams_unloaded = 0;
        ship->beams_reload = 0;
        ship->torps_unloaded = 0;
        ship->torps_reload = 0;
    }

    if (engine->tick % DTREK_REPAIR_FUSE != 0) return;

    if (!((at_repair_base && ship->system_damaged)
          || ship->system_repairable)) {
        return;
    }

    ship->system_damaged = 0;
    ship->system_repairable = 0;

    for (i = 0; i < ship->system_count; ++i) {
        DtrekSystemUnit *unit = &ship->system[i];
        int incr;
        int still_damaged;

        if (unit->current_size == unit->undamaged_size) continue;

        rpr = unit->repair_rate << repair_shift;
        if (at_repair_base) rpr += unit->planet_repair_rate;

        if (rpr == 0) {
            ship->system_damaged = 1;
            if (unit->repair_rate) ship->system_repairable = 1;
            continue;
        }

        unit->fractional_size += rpr;
        incr = unit->fractional_size >> 8;
        if (incr) unit->fractional_size &= 0xff;

        {
            int missing = unit->undamaged_size - unit->current_size;

            if (missing <= incr) {
                incr = missing;
                unit->fractional_size = 0;
                still_damaged = 0;
            }
            else {
                still_damaged = 1;
            }
        }

        if (unit->item != DTREK_SYS_ARMOR && unit->item != DTREK_SYS_HULL) {
            ship->system_size += incr;
        }
        unit->current_size += incr;

        if (incr) {
            switch (unit->item) {
            case DTREK_SYS_ARMOR:
                ship->armor = unit->current_size;
                break;
            case DTREK_SYS_HULL:
                ship->hull = unit->current_size;
                break;
            case DTREK_SYS_ENGINE:
                if (unit->undamaged_size > 0) {
                    ship->engine_health = unit->current_size * 256
                        / unit->undamaged_size;
                }
                break;
            case DTREK_SYS_FUEL:
                ship->fuel_max += 500 * incr;
                break;
            case DTREK_SYS_BEAM_NUM:
                if (!still_damaged) ++ship->beams_total;
                break;
            case DTREK_SYS_TORP_NUM:
                if (ship->torp_class != DTREK_WPN_PLASMA && !still_damaged) {
                    ++ship->torps_total;
                }
                break;
            case DTREK_SYS_DILITHIUM:
                ship->recharge += incr;
                break;
            case DTREK_SYS_CARGO:
                ship->cargo += incr;
                break;
            default:
                break;
            }
        }

        if (still_damaged) {
            ship->system_damaged = 1;
            if (unit->repair_rate) ship->system_repairable = 1;
        }
    }
}

static int find_free_torpedo(const DtrekEngine *engine, int owner_slot)
{
    int base = owner_slot * DTREK_MAX_TORPEDOES_PER_SHIP;
    int i;

    for (i = 0; i < DTREK_MAX_TORPEDOES_PER_SHIP; ++i) {
        if (!engine->torpedoes[base + i].in_use) return base + i;
    }

    return -1;
}

static int torpedoes_in_flight(const DtrekEngine *engine, int owner_slot)
{
    int base = owner_slot * DTREK_MAX_TORPEDOES_PER_SHIP;
    int i;
    int count = 0;

    for (i = 0; i < DTREK_MAX_TORPEDOES_PER_SHIP; ++i) {
        if (engine->torpedoes[base + i].in_use) count++;
    }

    return count;
}

/* Original daemon.c:near(): any hostile hull inside the trigger radius. */
static int torpedo_near(const DtrekEngine *engine, const DtrekTorpedo *torp)
{
    int slot;

    for (slot = 0; slot < DTREK_MAX_PLAYERS; ++slot) {
        const DtrekShip *ship = &engine->ships[slot];
        int dx;
        int dy;

        if (!is_alive(ship)) continue;
        if (slot == torp->owner_slot) continue;
        if (!(torp->war_mask & ship->team_mask)) continue;

        dx = torp->x - ship->x;
        dy = torp->y - ship->y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx > DTREK_EXPDIST) continue;
        if (dy > DTREK_EXPDIST) continue;
        if (dx * dx + dy * dy < DTREK_EXPDIST * DTREK_EXPDIST) return 1;
    }

    return 0;
}

/*
 * Original daemon.c:explode(): full damage inside EXPDIST, linear falloff
 * to zero at DETDIST, friendly fire included.
 */
static void explode_torpedo(DtrekEngine *engine, DtrekTorpedo *torp)
{
    DtrekShip *attacker;
    int slot;

    if (torp->owner_slot < 0 || torp->owner_slot >= DTREK_MAX_PLAYERS) {
        attacker = NULL;
    }
    else {
        attacker = &engine->ships[torp->owner_slot];
    }

    for (slot = 0; slot < DTREK_MAX_PLAYERS; ++slot) {
        DtrekShip *ship = &engine->ships[slot];
        int dx;
        int dy;
        int dist;
        int damage;
        int shield_index;

        if (!is_alive(ship)) continue;

        dx = torp->x - ship->x;
        dy = torp->y - ship->y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx > DTREK_DETDIST) continue;
        if (dy > DTREK_DETDIST) continue;

        dist = dx * dx + dy * dy;
        if (dist > DTREK_DETDIST * DTREK_DETDIST) continue;

        if (dist > DTREK_EXPDIST * DTREK_EXPDIST) {
            damage = torp->damage
                * (DTREK_DETDIST - dtrek_isqrt(dist))
                / (DTREK_DETDIST - DTREK_EXPDIST);
        }
        else {
            damage = torp->damage;
        }

        shield_index = shield_hit(
            dtrek_course_to(torp->x, torp->y, ship->x, ship->y),
            (uint8_t)ship->course);

        apply_damage(engine, attacker, ship, damage, torp->weapon_class,
                     shield_index, NULL);
    }

    torp->status = DTREK_TORP_EXPLODE;
    torp->fuse = DTREK_TORP_EXPLODE_TICKS;
}

static void step_torpedo(DtrekEngine *engine, DtrekTorpedo *torp)
{
    switch (torp->status) {
    case DTREK_TORP_FREE:
        torp->in_use = 0;
        return;

    case DTREK_TORP_TRACK:
        if (torp->target_slot < 0
            || torp->target_slot >= DTREK_MAX_PLAYERS
            || !is_alive(&engine->ships[torp->target_slot])
            || !(torp->war_mask
                 & engine->ships[torp->target_slot].team_mask)) {
            /* A lost target demotes the torpedo to straight flight. */
            torp->status = DTREK_TORP_STRAIGHT;
            torp->tracking = 0;
        }
        else {
            const DtrekShip *target = &engine->ships[torp->target_slot];
            uint8_t newdir = dtrek_course_to(
                torp->x, torp->y, target->x, target->y);

            if (newdir != (uint8_t)torp->course) {
                torp->course = newdir;
                torp->dx = (int)(dtrek_cos_table[torp->course]
                    * torp->speed * DTREK_WARP1);
                torp->dy = (int)(dtrek_sin_table[torp->course]
                    * torp->speed * DTREK_WARP1);
            }
        }
        /* fall through */
    case DTREK_TORP_MOVE:
        /* Original wobble: one-in-five updates nudges an unguided course. */
        if (torp->status == DTREK_TORP_MOVE
            && dtrek_rng_below(&engine->rng, 5) == 0) {
            int wobble = dtrek_rng_below(&engine->rng, 5) - 2;
            if (wobble != 0) {
                torp->course = (torp->course + wobble) & 0xff;
                torp->dx = (int)(dtrek_cos_table[torp->course]
                    * torp->speed * DTREK_WARP1);
                torp->dy = (int)(dtrek_sin_table[torp->course]
                    * torp->speed * DTREK_WARP1);
            }
        }
        /* fall through */
    case DTREK_TORP_STRAIGHT:
        if (torp->fuse-- <= 0) {
            torp->status = DTREK_TORP_OFF;
            break;
        }

        torp->x += torp->dx;
        if (torp->x < 0) {
            torp->x = 0;
            torp->status = DTREK_TORP_OFF;
            break;
        }
        if (torp->x > DTREK_GALAXY_WIDTH) {
            torp->x = DTREK_GALAXY_WIDTH;
            torp->status = DTREK_TORP_OFF;
            break;
        }

        torp->y += torp->dy;
        if (torp->y < 0) {
            torp->y = 0;
            torp->status = DTREK_TORP_OFF;
            break;
        }
        if (torp->y > DTREK_GALAXY_WIDTH) {
            torp->y = DTREK_GALAXY_WIDTH;
            torp->status = DTREK_TORP_OFF;
            break;
        }

        /* Original daemon.c planetary impact and black-hole deflection. */
        {
            int planet_distance;
            int planet_slot = nearest_planet(
                engine, torp->x, torp->y, &planet_distance);

            if (planet_slot >= 0) {
                const DtrekPlanet *planet = &engine->planets[planet_slot];

                if (!(planet->flags & DTREK_PLMASK_NEBULA)
                    && (long long)planet_distance * planet_distance
                        < (long long)DTREK_ORBIT_DISTANCE
                            * DTREK_ORBIT_DISTANCE / 2) {
                    torp->status = DTREK_TORP_OFF;
                    break;
                }

                if ((planet->flags & DTREK_PLMASK_BLACKHOLE)
                    && planet_distance > 0) {
                    int strength = (DTREK_BLACKHOLE_STRENGTH
                        - (planet_distance * DTREK_BLACKHOLE_STRENGTH
                            / DTREK_NEBULA_RADIUS)) * DTREK_WARP1;

                    if (strength > 0) {
                        torp->x += (planet->x - torp->x)
                            * strength / planet_distance;
                        torp->y += (planet->y - torp->y)
                            * strength / planet_distance;
                    }
                }
            }
        }

        if (engine->tick % (10 / DTREK_TORPFUSE) == 0) {
            torp->damage -= torp->decay;
        }

        if (torp->arm-- <= 0) {
            if (torpedo_near(engine, torp)) {
                explode_torpedo(engine, torp);
                if (torp->weapon_class == DTREK_WPN_CHAFF) {
                    /* Original chaff keeps flying after doing damage. */
                    torp->status = DTREK_TORP_STRAIGHT;
                }
            }
        }
        break;

    case DTREK_TORP_EXPLODE:
        if (torp->fuse-- <= 0) {
            torp->status = DTREK_TORP_FREE;
            torp->in_use = 0;
        }
        break;

    case DTREK_TORP_OFF:
        torp->status = DTREK_TORP_FREE;
        torp->in_use = 0;
        break;
    }
}

static void step_torpedoes(DtrekEngine *engine)
{
    int slot;

    for (slot = 0; slot < DTREK_MAX_TORPEDOES; ++slot) {
        if (engine->torpedoes[slot].in_use) {
            step_torpedo(engine, &engine->torpedoes[slot]);
        }
    }
}

/*
 * Wanderers, from daemon.c plfight: move every tick and pick a fresh course
 * with probability 1/WANDERERFUSE.  The galaxy RNG keeps this independent of
 * combat randomness so the same seed always yields the same galaxy.
 */
static void step_planets(DtrekEngine *engine)
{
    int slot;

    for (slot = 0; slot < DTREK_MAX_PLANETS; ++slot) {
        DtrekPlanet *planet = &engine->planets[slot];

        if (!planet->in_use) continue;
        if (!(planet->flags & DTREK_PLMASK_WANDERER)) continue;

        planet->x += planet->dx;
        planet->y += planet->dy;

        if (dtrek_rng_below(&engine->galaxy_rng, DTREK_WANDERER_FUSE) == 0) {
            int dir = dtrek_rng_below(&engine->galaxy_rng, 256);

            planet->dx = (int)(dtrek_cos_table[dir] * DTREK_WARP1);
            planet->dy = (int)(dtrek_sin_table[dir] * DTREK_WARP1);
        }

        if (planet->x < DTREK_ORBIT_DISTANCE) {
            planet->x = DTREK_ORBIT_DISTANCE;
            planet->dx = planet->dx < 0 ? -planet->dx : planet->dx;
        }
        else if (planet->x > DTREK_GALAXY_WIDTH - DTREK_ORBIT_DISTANCE) {
            planet->x = DTREK_GALAXY_WIDTH - DTREK_ORBIT_DISTANCE;
            planet->dx = planet->dx < 0 ? planet->dx : -planet->dx;
        }

        if (planet->y < DTREK_ORBIT_DISTANCE) {
            planet->y = DTREK_ORBIT_DISTANCE;
            planet->dy = planet->dy < 0 ? -planet->dy : planet->dy;
        }
        else if (planet->y > DTREK_GALAXY_WIDTH - DTREK_ORBIT_DISTANCE) {
            planet->y = DTREK_GALAXY_WIDTH - DTREK_ORBIT_DISTANCE;
            planet->dy = planet->dy < 0 ? planet->dy : -planet->dy;
        }
    }
}

static void refresh_all_nearest(DtrekEngine *engine)
{
    int slot;

    for (slot = 0; slot < DTREK_MAX_PLAYERS; ++slot) {
        if (engine->ships[slot].in_use) {
            update_nearest_planet(engine, &engine->ships[slot]);
        }
    }
}

/* Planet generation, ported from planets.c. */
#define DTREK_LOCATION_AWAYHOME (-3)
#define DTREK_LOCATION_FIXED (-2)
#define DTREK_LOCATION_RANDOM (-1)

#define DTREK_SECTOR_CENTER 0
#define DTREK_SECTOR_EDGE 1
#define DTREK_SECTOR_CORNER 2
#define DTREK_SECTOR_BAD 3

static void galaxy_locate_planet(DtrekEngine *engine, int quadrant,
                                 int *out_x, int *out_y)
{
    static const int kHextantType[16] = {
        DTREK_SECTOR_CORNER, DTREK_SECTOR_EDGE, DTREK_SECTOR_EDGE,
        DTREK_SECTOR_CORNER,
        DTREK_SECTOR_EDGE, DTREK_SECTOR_CENTER, DTREK_SECTOR_CENTER,
        DTREK_SECTOR_EDGE,
        DTREK_SECTOR_EDGE, DTREK_SECTOR_CENTER, DTREK_SECTOR_CENTER,
        DTREK_SECTOR_EDGE,
        DTREK_SECTOR_CORNER, DTREK_SECTOR_EDGE, DTREK_SECTOR_EDGE,
        DTREK_SECTOR_CORNER
    };
    int x = 0;
    int y = 0;
    int attempts = 0;

    for (;;) {
        int sector;
        int hextant;
        int slot;

        x = (DTREK_GALAXY_WIDTH / 100)
            + dtrek_rng_below(&engine->galaxy_rng,
                DTREK_GALAXY_WIDTH * 98 / 100);
        y = (DTREK_GALAXY_WIDTH / 100)
            + dtrek_rng_below(&engine->galaxy_rng,
                DTREK_GALAXY_WIDTH * 98 / 100);

        hextant = (x / (DTREK_GALAXY_WIDTH / 4))
            + 4 * (y / (DTREK_GALAXY_WIDTH / 4));
        if (hextant < 0 || hextant > 15) hextant = 0;
        sector = kHextantType[hextant];

        if (sector == DTREK_SECTOR_CORNER) {
            if (dtrek_rng_below(&engine->galaxy_rng, 3) == 0) {
                sector = DTREK_SECTOR_BAD;
            }
        }
        else if (sector == DTREK_SECTOR_EDGE) {
            if (dtrek_rng_below(&engine->galaxy_rng, 2) == 0) {
                sector = DTREK_SECTOR_BAD;
            }
        }

        if (sector != DTREK_SECTOR_BAD
            && quadrant != DTREK_LOCATION_RANDOM
            && quadrant != DTREK_LOCATION_AWAYHOME) {
            switch (quadrant) {
            case 0:
                if (x > DTREK_GALAXY_WIDTH / 2) {
                    x = DTREK_GALAXY_WIDTH - 1 - x;
                }
                if (y < DTREK_GALAXY_WIDTH / 2) {
                    y = DTREK_GALAXY_WIDTH - 1 - y;
                }
                break;
            case 1:
                if (x > DTREK_GALAXY_WIDTH / 2) {
                    x = DTREK_GALAXY_WIDTH - 1 - x;
                }
                if (y > DTREK_GALAXY_WIDTH / 2) {
                    y = DTREK_GALAXY_WIDTH - 1 - y;
                }
                break;
            case 2:
                if (x < DTREK_GALAXY_WIDTH / 2) {
                    x = DTREK_GALAXY_WIDTH - 1 - x;
                }
                if (y > DTREK_GALAXY_WIDTH / 2) {
                    y = DTREK_GALAXY_WIDTH - 1 - y;
                }
                break;
            case 3:
                if (x < DTREK_GALAXY_WIDTH / 2) {
                    x = DTREK_GALAXY_WIDTH - 1 - x;
                }
                if (y < DTREK_GALAXY_WIDTH / 2) {
                    y = DTREK_GALAXY_WIDTH - 1 - y;
                }
                break;
            default:
                break;
            }
        }

        if (sector != DTREK_SECTOR_BAD) {
            for (slot = 0; slot < DTREK_MAX_PLANETS; ++slot) {
                const DtrekPlanet *planet = &engine->planets[slot];
                int dx;
                int dy;

                if (!planet->in_use) continue;
                dx = planet->x - x;
                if (dx < 0) dx = -dx;
                if (dx > DTREK_INTERPLANETARY_DIST) continue;
                dy = planet->y - y;
                if (dy < 0) dy = -dy;
                if (dy > DTREK_INTERPLANETARY_DIST) continue;
                if (dtrek_ihypot(planet->x - x, planet->y - y)
                    < DTREK_INTERPLANETARY_DIST) {
                    sector = DTREK_SECTOR_BAD;
                }
            }
        }

        if (sector != DTREK_SECTOR_BAD
            && quadrant == DTREK_LOCATION_AWAYHOME) {
            int dx = x < DTREK_GALAXY_WIDTH / 2
                ? x : DTREK_GALAXY_WIDTH - x;
            int dy = y < DTREK_GALAXY_WIDTH / 2
                ? y : DTREK_GALAXY_WIDTH - y;
            int margin = DTREK_INTERPLANETARY_DIST * 2;

            if (dx - DTREK_HOME_SMALL < margin
                && DTREK_HOME_SMALL - dx < margin) {
                sector = DTREK_SECTOR_BAD;
            }
            else if (dy - DTREK_HOME_SMALL < margin
                && DTREK_HOME_SMALL - dy < margin) {
                sector = DTREK_SECTOR_BAD;
            }
        }

        if (sector != DTREK_SECTOR_BAD) break;
        if (++attempts > 100000) break;
    }

    *out_x = x;
    *out_y = y;
}

static void galaxy_make_planet(DtrekEngine *engine, int slot, int flags,
                               int quadrant, int x, int y, const char *name,
                               int *name_index)
{
    DtrekPlanet *planet = &engine->planets[slot];
    char buffer[DTREK_PLANET_NAME_LENGTH];
    int owner;

    memset(planet, 0, sizeof(*planet));

    if (flags & DTREK_PLMASK_FEDERATION) owner = DTREK_TEAM_FEDERATION;
    else if (flags & DTREK_PLMASK_ROMULAN) owner = DTREK_TEAM_ROMULAN;
    else if (flags & DTREK_PLMASK_KLINGON) owner = DTREK_TEAM_KLINGON;
    else if (flags & DTREK_PLMASK_ORION) owner = DTREK_TEAM_ORION;
    else {
        flags |= DTREK_PLMASK_INDEPENDENT;
        owner = DTREK_TEAM_INDEPENDENT;
    }

    if (quadrant == DTREK_LOCATION_FIXED) {
        planet->x = x;
        planet->y = y;
    }
    else {
        galaxy_locate_planet(engine, quadrant, &planet->x, &planet->y);
    }

    if (owner != DTREK_TEAM_INDEPENDENT) {
        copy_ship_name(buffer, sizeof(buffer), name);
        copy_ship_name(planet->name, sizeof(planet->name), buffer);
        planet->armies = 20 + dtrek_rng_below(&engine->galaxy_rng, 20);
        if (dtrek_rng_below(&engine->galaxy_rng, 4) == 0) {
            flags |= DTREK_PLMASK_REPAIR;
        }
        if (dtrek_rng_below(&engine->galaxy_rng, 2) == 0) {
            flags |= DTREK_PLMASK_FUEL;
        }
    }
    else if (flags & DTREK_PLMASK_WANDERER) {
        int dir;

        planet->armies = 5 + dtrek_rng_below(&engine->galaxy_rng, 30);
        copy_ship_name(buffer, sizeof(buffer), kPreNames[(*name_index)++]);
        copy_ship_name(planet->name, sizeof(planet->name), buffer);
        dir = dtrek_rng_below(&engine->galaxy_rng, 256);
        planet->dx = (int)(dtrek_cos_table[dir] * DTREK_WARP1);
        planet->dy = (int)(dtrek_sin_table[dir] * DTREK_WARP1);
        if (dtrek_rng_below(&engine->galaxy_rng, 4) == 0) {
            flags |= DTREK_PLMASK_REPAIR;
        }
        if (dtrek_rng_below(&engine->galaxy_rng, 2) == 0) {
            flags |= DTREK_PLMASK_FUEL;
        }
    }
    else if (flags & (DTREK_PLMASK_BLACKHOLE
        | DTREK_PLMASK_PULSAR
        | DTREK_PLMASK_NEBULA
        | DTREK_PLMASK_BARREN)) {
        const char *suffix = "";

        planet->armies = 0;
        if (flags & DTREK_PLMASK_BLACKHOLE) suffix = " BHole";
        else if (flags & DTREK_PLMASK_PULSAR) suffix = " Pulsar";
        else if (flags & DTREK_PLMASK_NEBULA) suffix = " Nebula";
        snprintf(buffer, sizeof(buffer), "%s%s", kPreNames[(*name_index)++],
                 suffix);
        copy_ship_name(planet->name, sizeof(planet->name), buffer);
    }
    else {
        planet->armies = 5 + dtrek_rng_below(&engine->galaxy_rng, 30);
        copy_ship_name(buffer, sizeof(buffer), kPreNames[(*name_index)++]);
        copy_ship_name(planet->name, sizeof(planet->name), buffer);
        if (dtrek_rng_below(&engine->galaxy_rng, 8) == 0) {
            flags |= DTREK_PLMASK_REPAIR;
        }
        if (dtrek_rng_below(&engine->galaxy_rng, 4) == 0) {
            flags |= DTREK_PLMASK_FUEL;
        }
    }

    planet->flags = flags;
    planet->owner = owner;
    planet->in_use = 1;
    engine->planet_count++;
}

uint32_t DTREK_CALL dtrek_abi_version(void)
{
    return DTREK_ABI_VERSION;
}

const char * DTREK_CALL dtrek_version_string(void)
{
    return DTREK_VERSION_STRING;
}

int32_t DTREK_CALL dtrek_clear_planets(dtrek_engine *engine)
{
    if (engine == NULL) return DTREK_ERR_ARGUMENT;

    memset(engine->planets, 0, sizeof(engine->planets));
    engine->planet_count = 0;
    refresh_all_nearest(engine);
    return DTREK_OK;
}

int32_t DTREK_CALL dtrek_place_planet(dtrek_engine *engine,
                                      int32_t planet_slot,
                                      int32_t flags,
                                      int32_t owner,
                                      int32_t armies,
                                      int32_t x,
                                      int32_t y,
                                      const char *name)
{
    DtrekPlanet *planet;
    int was_in_use;

    if (engine == NULL) return DTREK_ERR_ARGUMENT;
    if (planet_slot < 0 || planet_slot >= DTREK_MAX_PLANETS) {
        return DTREK_ERR_ARGUMENT;
    }
    if (owner < 0 || owner >= DTREK_NUM_TEAMS) return DTREK_ERR_ARGUMENT;
    if (armies < 0) return DTREK_ERR_ARGUMENT;
    if (x < 0 || x > DTREK_GALAXY_WIDTH) return DTREK_ERR_ARGUMENT;
    if (y < 0 || y > DTREK_GALAXY_WIDTH) return DTREK_ERR_ARGUMENT;

    planet = &engine->planets[planet_slot];
    was_in_use = planet->in_use;

    memset(planet, 0, sizeof(*planet));
    planet->in_use = 1;
    planet->flags = (flags & ~DTREK_PLMASK_OWNER_MASK) | (1 << owner);
    planet->owner = owner;
    planet->armies = armies;
    planet->x = x;
    planet->y = y;
    copy_ship_name(planet->name, sizeof(planet->name),
        name != NULL ? name : kPreNames[0]);

    if (!was_in_use) engine->planet_count++;
    refresh_all_nearest(engine);
    return DTREK_OK;
}

int32_t DTREK_CALL dtrek_generate_galaxy(dtrek_engine *engine, uint32_t seed)
{
    int i;
    int name_index = 0;

    if (engine == NULL) return DTREK_ERR_ARGUMENT;

    dtrek_clear_planets(engine);
    dtrek_rng_seed(&engine->galaxy_rng, seed);

    /* Home worlds, exactly as planets.c. */
    galaxy_make_planet(engine, 0,
        DTREK_PLMASK_FEDERATION | DTREK_PLMASK_HOME
            | DTREK_PLMASK_FUEL | DTREK_PLMASK_REPAIR,
        DTREK_LOCATION_FIXED, DTREK_HOME_SMALL, DTREK_HOME_LARGE,
        "Earth", &name_index);
    engine->planets[0].armies += 10;
    engine->planets[0].flags |=
        DTREK_PLMASK_REPAIR | DTREK_PLMASK_FUEL;

    galaxy_make_planet(engine, 10,
        DTREK_PLMASK_ROMULAN | DTREK_PLMASK_HOME
            | DTREK_PLMASK_FUEL | DTREK_PLMASK_REPAIR,
        DTREK_LOCATION_FIXED, DTREK_HOME_SMALL, DTREK_HOME_SMALL,
        "Romulus", &name_index);
    engine->planets[10].armies += 10;
    engine->planets[10].flags |=
        DTREK_PLMASK_REPAIR | DTREK_PLMASK_FUEL;

    galaxy_make_planet(engine, 20,
        DTREK_PLMASK_KLINGON | DTREK_PLMASK_HOME
            | DTREK_PLMASK_FUEL | DTREK_PLMASK_REPAIR,
        DTREK_LOCATION_FIXED, DTREK_HOME_LARGE, DTREK_HOME_SMALL,
        "Klingus", &name_index);
    engine->planets[20].armies += 10;
    engine->planets[20].flags |=
        DTREK_PLMASK_REPAIR | DTREK_PLMASK_FUEL;

    galaxy_make_planet(engine, 30,
        DTREK_PLMASK_ORION | DTREK_PLMASK_HOME
            | DTREK_PLMASK_FUEL | DTREK_PLMASK_REPAIR,
        DTREK_LOCATION_FIXED, DTREK_HOME_LARGE, DTREK_HOME_LARGE,
        "Orion", &name_index);
    engine->planets[30].armies += 10;
    engine->planets[30].flags |=
        DTREK_PLMASK_REPAIR | DTREK_PLMASK_FUEL;

    for (i = 1; i < 10; ++i) {
        galaxy_make_planet(engine, i, DTREK_PLMASK_FEDERATION, 0,
            0, 0, kPlanetNames[i], &name_index);
    }
    for (i = 11; i < 20; ++i) {
        galaxy_make_planet(engine, i, DTREK_PLMASK_ROMULAN, 1,
            0, 0, kPlanetNames[i], &name_index);
    }
    for (i = 21; i < 30; ++i) {
        galaxy_make_planet(engine, i, DTREK_PLMASK_KLINGON, 2,
            0, 0, kPlanetNames[i], &name_index);
    }
    for (i = 31; i < 40; ++i) {
        galaxy_make_planet(engine, i, DTREK_PLMASK_ORION, 3,
            0, 0, kPlanetNames[i], &name_index);
    }

    /* Independent home world at the galactic center. */
    galaxy_make_planet(engine, 40,
        DTREK_PLMASK_INDEPENDENT | DTREK_PLMASK_HOME
            | DTREK_PLMASK_REPAIR | DTREK_PLMASK_FUEL,
        DTREK_LOCATION_FIXED, DTREK_GALAXY_WIDTH / 2,
        DTREK_GALAXY_WIDTH / 2, NULL, &name_index);

    /* A wanderer starting away from any home world. */
    galaxy_make_planet(engine, 41,
        DTREK_PLMASK_INDEPENDENT | DTREK_PLMASK_WANDERER
            | DTREK_PLMASK_REPAIR | DTREK_PLMASK_FUEL,
        DTREK_LOCATION_AWAYHOME, 0, 0, NULL, &name_index);

    /* Two independents per home quadrant. */
    for (i = 0; i < 8; ++i) {
        galaxy_make_planet(engine, 42 + i, DTREK_PLMASK_INDEPENDENT,
            i / 2, 0, 0, NULL, &name_index);
    }

    /* Black hole, pulsar, and nebula in each quadrant. */
    for (i = 0; i < 4; ++i) {
        galaxy_make_planet(engine, 50 + i * 3,
            DTREK_PLMASK_BLACKHOLE, i, 0, 0, NULL, &name_index);
        galaxy_make_planet(engine, 51 + i * 3,
            DTREK_PLMASK_PULSAR, i, 0, 0, NULL, &name_index);
        galaxy_make_planet(engine, 52 + i * 3,
            DTREK_PLMASK_NEBULA, i, 0, 0, NULL, &name_index);
    }

    /* The rest are random objects, from planets.c. */
    for (i = 62; i < DTREK_MAX_PLANETS; ++i) {
        switch (dtrek_rng_below(&engine->galaxy_rng, 9)) {
        case 0:
            galaxy_make_planet(engine, i, DTREK_PLMASK_BLACKHOLE,
                DTREK_LOCATION_RANDOM, 0, 0, NULL, &name_index);
            break;
        case 1:
            galaxy_make_planet(engine, i, DTREK_PLMASK_PULSAR,
                DTREK_LOCATION_RANDOM, 0, 0, NULL, &name_index);
            break;
        case 2:
            galaxy_make_planet(engine, i,
                DTREK_PLMASK_WANDERER | DTREK_PLMASK_INDEPENDENT,
                DTREK_LOCATION_RANDOM, 0, 0, NULL, &name_index);
            break;
        case 3:
            galaxy_make_planet(engine, i, DTREK_PLMASK_NEBULA,
                DTREK_LOCATION_RANDOM, 0, 0, NULL, &name_index);
            break;
        case 4:
            galaxy_make_planet(engine, i, DTREK_PLMASK_INDEPENDENT,
                DTREK_LOCATION_RANDOM, 0, 0, NULL, &name_index);
            break;
        default:
            galaxy_make_planet(engine, i, DTREK_PLMASK_BARREN,
                DTREK_LOCATION_RANDOM, 0, 0, NULL, &name_index);
            break;
        }
    }

    refresh_all_nearest(engine);
    return DTREK_OK;
}

int32_t DTREK_CALL dtrek_orbit(dtrek_engine *engine, int32_t ship_slot,
                               int32_t planet_slot)
{
    DtrekShip *ship;
    int chosen = -1;
    int slot;

    if (!is_valid_ship(engine, ship_slot)) return DTREK_ERR_SHIP;
    ship = &engine->ships[ship_slot];
    if (!is_alive(ship)) return DTREK_ERR_STATE;

    if (planet_slot >= DTREK_MAX_PLANETS) return DTREK_ERR_ARGUMENT;
    if (planet_slot < -1) return DTREK_ERR_ARGUMENT;

    if (planet_slot >= 0) {
        const DtrekPlanet *planet;

        if (!planet_is_valid(engine, planet_slot)) {
            return DTREK_ERR_ARGUMENT;
        }
        planet = &engine->planets[planet_slot];
        if (planet->flags
            & (DTREK_PLMASK_BLACKHOLE | DTREK_PLMASK_NEBULA)) {
            return DTREK_ERR_STATE;
        }
        if (dtrek_ihypot(ship->x - planet->x, ship->y - planet->y)
            > DTREK_ENTER_ORBIT_DISTANCE) {
            return DTREK_ERR_STATE;
        }
        chosen = planet_slot;
    }
    else {
        int best_distance = INT_MAX;

        for (slot = 0; slot < DTREK_MAX_PLANETS; ++slot) {
            const DtrekPlanet *planet = &engine->planets[slot];
            int distance;

            if (!planet->in_use) continue;
            if (planet->flags
                & (DTREK_PLMASK_BLACKHOLE | DTREK_PLMASK_NEBULA)) {
                continue;
            }
            distance = dtrek_ihypot(ship->x - planet->x,
                                    ship->y - planet->y);
            if (distance <= DTREK_ENTER_ORBIT_DISTANCE
                && distance < best_distance) {
                best_distance = distance;
                chosen = slot;
            }
        }
    }

    if (chosen < 0) return DTREK_ERR_STATE;
    if (ship->speed > DTREK_ORBIT_SPEED) return DTREK_ERR_STATE;

    {
        const DtrekPlanet *planet = &engine->planets[chosen];
        int dir = dtrek_atan2_course(planet->y - ship->y,
                                     ship->x - planet->x);

        ship->x = planet->x
            + (int)(DTREK_ORBIT_DISTANCE * dtrek_cos_table[dir]);
        ship->y = planet->y
            + (int)(DTREK_ORBIT_DISTANCE * dtrek_sin_table[dir]);
        ship->course = (dir + 64) & 0xff;
        ship->desired_course = ship->course;
        ship->speed = 0;
        ship->desired_speed = 0;
        ship->subspeed = 0;
        ship->subdir = 0;
        ship->orbiting = 1;
        ship->planet_slot = chosen;
        ship->nearest_planet_slot = chosen;
        ship->nearest_planet_dist = DTREK_ORBIT_DISTANCE;
    }

    service_ship(engine, ship);
    return chosen;
}

int32_t DTREK_CALL dtrek_leave_orbit(dtrek_engine *engine, int32_t ship_slot)
{
    DtrekShip *ship;

    if (!is_valid_ship(engine, ship_slot)) return DTREK_ERR_SHIP;

    ship = &engine->ships[ship_slot];
    ship->orbiting = 0;
    ship->planet_slot = -1;
    ship->at_fuel_planet = 0;
    ship->at_repair_planet = 0;
    return DTREK_OK;
}

int32_t DTREK_CALL dtrek_set_repair(dtrek_engine *engine, int32_t ship_slot,
                                    int32_t on)
{
    if (!is_valid_ship(engine, ship_slot)) return DTREK_ERR_SHIP;
    engine->ships[ship_slot].repair_mode = on ? 1 : 0;
    return DTREK_OK;
}

dtrek_engine * DTREK_CALL dtrek_create(uint32_t seed)
{
    DtrekEngine *engine = (DtrekEngine *)calloc(1, sizeof(*engine));

    if (engine == NULL) return NULL;
    dtrek_reset(engine, seed);
    return engine;
}

void DTREK_CALL dtrek_destroy(dtrek_engine *engine)
{
    free(engine);
}

int32_t DTREK_CALL dtrek_reset(dtrek_engine *engine, uint32_t seed)
{
    if (engine == NULL) return DTREK_ERR_ARGUMENT;

    memset(engine->ships, 0, sizeof(engine->ships));
    memset(engine->torpedoes, 0, sizeof(engine->torpedoes));
    memset(engine->planets, 0, sizeof(engine->planets));
    engine->tick = 0;
    engine->ship_count = 0;
    engine->planet_count = 0;
    dtrek_rng_seed(&engine->rng, seed);
    dtrek_rng_seed(&engine->galaxy_rng, seed);
    dtrek_rng_seed(&engine->robot_rng, seed ^ 0x524F424FU);
    return DTREK_OK;
}

int dtrek_spawn_training_ship(DtrekEngine *engine, const char *name,
                              int team, int x, int y)
{
    int slot;

    if (engine == NULL) return DTREK_ERR_ARGUMENT;
    if (team < 0 || team >= DTREK_NUM_TEAMS) return DTREK_ERR_ARGUMENT;
    if (x < 0 || x > DTREK_GALAXY_WIDTH) return DTREK_ERR_ARGUMENT;
    if (y < 0 || y > DTREK_GALAXY_WIDTH) return DTREK_ERR_ARGUMENT;

    for (slot = 0; slot < DTREK_MAX_PLAYERS; ++slot) {
        if (!engine->ships[slot].in_use) break;
    }
    if (slot == DTREK_MAX_PLAYERS) return DTREK_ERR_FULL;

    initialize_ship(&engine->ships[slot], &kReliantSpec, name, team, x, y);
    update_nearest_planet(engine, &engine->ships[slot]);
    engine->ship_count++;
    return slot;
}

int32_t DTREK_CALL dtrek_spawn(dtrek_engine *engine, const char *name,
                               int32_t team, int32_t x, int32_t y)
{
    return dtrek_spawn_training_ship(engine, name, (int)team, (int)x, (int)y);
}

int32_t DTREK_CALL dtrek_set_controls(dtrek_engine *engine, int32_t ship_slot,
                                      int32_t desired_course,
                                      int32_t desired_speed)
{
    DtrekShip *ship;

    if (!is_valid_ship(engine, ship_slot)) return DTREK_ERR_SHIP;
    if (desired_course < 0 || desired_course > 255) return DTREK_ERR_ARGUMENT;
    if (desired_speed < 0 || desired_speed > DTREK_MAX_SPEED) {
        return DTREK_ERR_ARGUMENT;
    }

    ship = &engine->ships[ship_slot];
    ship->desired_course = desired_course;
    ship->desired_speed = desired_speed;
    return DTREK_OK;
}

int32_t DTREK_CALL dtrek_set_shields(dtrek_engine *engine, int32_t ship_slot,
                                     int32_t up)
{
    if (!is_valid_ship(engine, ship_slot)) return DTREK_ERR_SHIP;
    engine->ships[ship_slot].shields_up = up ? 1 : 0;
    return DTREK_OK;
}

int32_t DTREK_CALL dtrek_step(dtrek_engine *engine, int32_t ticks)
{
    int32_t tick;
    int slot;

    if (engine == NULL) return DTREK_ERR_ARGUMENT;
    if (ticks < 0) return DTREK_ERR_ARGUMENT;

    for (tick = 0; tick < ticks; ++tick) {
        engine->tick++;
        step_planets(engine);
        for (slot = 0; slot < DTREK_MAX_PLAYERS; ++slot) {
            dtrek_robot_step(engine, slot);
            step_ship(engine, &engine->ships[slot]);
        }
        step_torpedoes(engine);
    }

    return DTREK_OK;
}

int32_t DTREK_CALL dtrek_fire_phaser(dtrek_engine *engine, int32_t ship_slot,
                                     int32_t aim_course,
                                     DtrekPhaserResult *result)
{
    DtrekShip *shooter;
    DtrekShip *target;
    DtrekShip *victim;
    DtrekPhaserResult local_result;
    DtrekPhaserResult *out;
    int target_slot;
    int best_range;
    int loaded_beams;
    int fired_beams;
    int fuel_cost;
    int range;
    int damage;
    int shield_index;
    int incoming_course;
    int slot;
    DtrekTorpedo *torpedo;
    int torpedo_target_slot;
    int torpedo_best_range;

    out = result != NULL ? result : &local_result;
    memset(out, 0, sizeof(*out));
    out->target_slot = -1;
    out->torpedo_slot = -1;
    out->shield_index = -1;

    if (!is_valid_ship(engine, ship_slot)) return DTREK_ERR_SHIP;
    shooter = &engine->ships[ship_slot];
    if (!is_alive(shooter)) return DTREK_ERR_STATE;
    if (aim_course < 0 || aim_course > 255) return DTREK_ERR_ARGUMENT;

    loaded_beams = shooter->beams_total - shooter->beams_unloaded;
    if (loaded_beams <= 0) return DTREK_ERR_WEAPON;
    if (shooter->spec.s_beamrange <= 0) return DTREK_ERR_WEAPON;

    fired_beams = loaded_beams;
    if (shooter->spec.s_beamcost > 0) {
        int available = shooter->fuel / shooter->spec.s_beamcost;
        if (available < fired_beams) fired_beams = available;
        if (fired_beams <= 0) return DTREK_ERR_WEAPON;
        if (fired_beams > loaded_beams) fired_beams = loaded_beams;
    }
    fuel_cost = shooter->spec.s_beamcost * fired_beams;

    target_slot = -1;
    best_range = DTREK_GALAXY_WIDTH * 2;

    for (slot = 0; slot < DTREK_MAX_PLAYERS; ++slot) {
        int direction;
        uint8_t delta;

        target = &engine->ships[slot];
        if (!is_alive(target) || slot == ship_slot) continue;
        if (!(shooter->hostile_mask & target->team_mask)) continue;

        range = dtrek_ihypot(target->x - shooter->x,
                             target->y - shooter->y);
        if (range > shooter->spec.s_beamrange + DTREK_EXPDIST) continue;

        direction = dtrek_course_to(shooter->x, shooter->y,
                                    target->x, target->y);
        delta = dtrek_course_delta((uint8_t)direction, (uint8_t)aim_course);

        if (delta < (uint8_t)shooter->spec.s_beamaccuracy
            || dtrek_vector_close_to_point(
                   shooter->x, shooter->y, (uint8_t)aim_course,
                   shooter->spec.s_beamrange,
                   target->x, target->y, DTREK_EXPDIST)) {
            if (range < best_range) {
                best_range = range;
                target_slot = slot;
            }
        }
    }

    /*
     * Point defense: the original game scanned for torpedoes only
     * when defensive beams were enabled and the aim point was not near a
     * ship.  This build always offers the nearest incoming torpedo to the
     * arc, but only replaces the ship target when the torpedo is closer.
     */
    torpedo_target_slot = -1;
    torpedo_best_range = DTREK_GALAXY_WIDTH * 2;

    for (slot = 0; slot < DTREK_MAX_TORPEDOES; ++slot) {
        int direction;
        uint8_t delta;

        torpedo = &engine->torpedoes[slot];
        if (!torpedo->in_use) continue;
        if (torpedo->status != DTREK_TORP_MOVE
            && torpedo->status != DTREK_TORP_STRAIGHT
            && torpedo->status != DTREK_TORP_TRACK) {
            continue;
        }
        if (!(torpedo->war_mask & shooter->team_mask)) continue;
        if (torpedo->weapon_class == DTREK_WPN_CHAFF) continue;

        range = dtrek_ihypot(torpedo->x - shooter->x,
                             torpedo->y - shooter->y);
        if (range > shooter->spec.s_beamrange + DTREK_EXPDIST) continue;

        direction = dtrek_course_to(shooter->x, shooter->y,
                                    torpedo->x, torpedo->y);
        delta = dtrek_course_delta((uint8_t)direction, (uint8_t)aim_course);

        if (delta < (uint8_t)shooter->spec.s_beamaccuracy
            || dtrek_vector_close_to_point(
                   shooter->x, shooter->y, (uint8_t)aim_course,
                   shooter->spec.s_beamrange,
                   torpedo->x, torpedo->y, DTREK_TORP_RADIUS)) {
            if (range < torpedo_best_range) {
                torpedo_best_range = range;
                torpedo_target_slot = slot;
            }
        }
    }

    if (torpedo_target_slot >= 0 && torpedo_best_range < best_range) {
        target_slot = -1;
        fired_beams = 1;
        fuel_cost = shooter->spec.s_beamcost * fired_beams;
    }

    shooter->beams_unloaded += fired_beams;
    if (shooter->beams_reload == 0) {
        shooter->beams_reload = shooter->spec.s_beamreload;
    }
    shooter->fuel -= fuel_cost;
    if (shooter->fuel < 0) shooter->fuel = 0;

    out->fired = 1;
    out->beams_fired = fired_beams;
    out->fuel_cost = fuel_cost;

    if (torpedo_target_slot >= 0 && torpedo_best_range < best_range) {
        int beam_damage = shooter->spec.s_beamdamage;

        torpedo = &engine->torpedoes[torpedo_target_slot];

        if (torpedo->weapon_class == DTREK_WPN_PHOTON) {
            beam_damage *= 2;
        }
        else if (torpedo->weapon_class == DTREK_WPN_PLASMA) {
            beam_damage /= 2;
        }
        if (shooter->spec.s_beamclass == DTREK_WPN_TACHYON) {
            beam_damage *= 4;
        }
        if (shooter->spec.s_beamatten > 0) {
            beam_damage -= torpedo_best_range / shooter->spec.s_beamatten;
        }
        if (beam_damage <= 0) beam_damage = 1;

        out->hit = 1;
        out->range = torpedo_best_range;
        out->target_slot = -1;
        out->target_team = torpedo->team;
        out->target_is_torpedo = 1;
        out->torpedo_slot = torpedo_target_slot;
        out->torpedo_team = torpedo->team;
        out->damage = beam_damage;
        out->torpedo_damage = beam_damage;

        if (beam_damage > torpedo->damage) {
            out->torpedo_destroyed = 1;
            explode_torpedo(engine, torpedo);
        }
        else {
            torpedo->damage -= beam_damage;
        }

        return 1;
    }

    if (target_slot < 0) {
        return 0;
    }
    out->range = best_range;

    victim = &engine->ships[target_slot];
    range = best_range;
    damage = shooter->spec.s_beamdamage;
    if (shooter->spec.s_beamatten > 0) {
        damage -= range / shooter->spec.s_beamatten;
    }
    damage *= fired_beams;
    if (damage <= 0) damage = 1;

    incoming_course = dtrek_course_to(shooter->x, shooter->y,
                                      victim->x, victim->y);
    shield_index = shield_hit((uint8_t)incoming_course, (uint8_t)victim->course);

    out->hit = 1;
    out->target_slot = target_slot;
    out->target_team = victim->team;
    out->damage = damage;
    out->shield_index = shield_index;

    apply_damage(engine, shooter, victim, damage, shooter->spec.s_beamclass,
                 shield_index, out);
    return 1;
}

int32_t DTREK_CALL dtrek_set_ordnance(dtrek_engine *engine, int32_t ship_slot,
                                       int32_t weapon_class)
{
    if (!is_valid_ship(engine, ship_slot)) return DTREK_ERR_SHIP;
    if (ordnance_for_class(weapon_class) == NULL) return DTREK_ERR_ARGUMENT;

    engine->ships[ship_slot].torp_class = weapon_class;
    return DTREK_OK;
}

int32_t DTREK_CALL dtrek_ordnance_spec(dtrek_engine *engine,
                                        int32_t weapon_class,
                                        DtrekOrdnanceSpec *spec)
{
    const DtrekOrdnanceSpec *profile;

    if (engine == NULL || spec == NULL) return DTREK_ERR_ARGUMENT;
    profile = ordnance_for_class(weapon_class);
    if (profile == NULL) return DTREK_ERR_ARGUMENT;

    *spec = *profile;
    return DTREK_OK;
}

static int clamp_galaxy_coord(int value)
{
    if (value < 0) return 0;
    if (value > DTREK_GALAXY_WIDTH) return DTREK_GALAXY_WIDTH;
    return value;
}

int32_t DTREK_CALL dtrek_fire_torpedo(dtrek_engine *engine, int32_t ship_slot,
                                      const DtrekTorpedoLaunch *launch,
                                      DtrekTorpedoResult *result)
{
    DtrekShip *shooter;
    DtrekTorpedo *torp;
    const DtrekOrdnanceSpec *spec;
    DtrekTorpedoResult local_result;
    DtrekTorpedoResult *out;
    int slot;
    int weapon_class;
    int target_slot = -1;
    int course;
    int aim_x;
    int aim_y;

    out = result != NULL ? result : &local_result;
    memset(out, 0, sizeof(*out));
    out->torpedo_slot = -1;
    out->target_slot = -1;

    if (!is_valid_ship(engine, ship_slot)) return DTREK_ERR_SHIP;
    if (launch == NULL) return DTREK_ERR_ARGUMENT;

    shooter = &engine->ships[ship_slot];
    if (!is_alive(shooter)) return DTREK_ERR_STATE;

    course = launch->course;
    if (course < 0 || course > 255) return DTREK_ERR_ARGUMENT;

    weapon_class = launch->weapon_class;
    if (weapon_class == -1) weapon_class = shooter->torp_class;
    spec = ordnance_for_class(weapon_class);
    if (spec == NULL) return DTREK_ERR_ARGUMENT;

    if (shooter->torps_total <= 0) return DTREK_ERR_WEAPON;
    if (shooter->torps_unloaded >= shooter->torps_total) {
        return DTREK_ERR_WEAPON;
    }
    if (shooter->fuel < spec->fuel_cost) return DTREK_ERR_WEAPON;

    if (launch->target_slot >= DTREK_MAX_PLAYERS) {
        return DTREK_ERR_ARGUMENT;
    }

    if (is_guided_class(weapon_class) && launch->target_slot >= 0) {
        const DtrekShip *target = &engine->ships[launch->target_slot];

        if (launch->target_slot != ship_slot
            && is_alive(target)
            && (shooter->hostile_mask & target->team_mask)) {
            target_slot = launch->target_slot;
        }
    }

    slot = find_free_torpedo(engine, ship_slot);
    if (slot < 0) return DTREK_ERR_FULL;

    torp = &engine->torpedoes[slot];
    memset(torp, 0, sizeof(*torp));
    torp->in_use = 1;
    torp->owner_slot = ship_slot;
    torp->team = shooter->team;
    torp->weapon_class = weapon_class;
    torp->war_mask = shooter->hostile_mask;
    torp->team_mask = shooter->team_mask;
    torp->damage = spec->damage;
    torp->base_damage = spec->damage;
    torp->decay = spec->decay;
    torp->arm = spec->arm;
    torp->target_slot = target_slot;
    torp->x = shooter->x;
    torp->y = shooter->y;
    torp->course = course;
    torp->speed = spec->speed;
    torp->fuse = spec->duration;
    if (weapon_class != DTREK_WPN_PLASMA) {
        torp->fuse += dtrek_rng_below(&engine->rng, 30);
    }

    switch (weapon_class) {
    case DTREK_WPN_CHAFF:
        /* Original chaff starts EXPDIST*2 ahead and keeps ship speed. */
        torp->status = DTREK_TORP_STRAIGHT;
        torp->course = shooter->course;
        torp->speed = shooter->speed;
        torp->x = clamp_galaxy_coord(shooter->x
            + (int)(dtrek_cos_table[shooter->course]
                * (DTREK_EXPDIST * 2)));
        torp->y = clamp_galaxy_coord(shooter->y
            + (int)(dtrek_sin_table[shooter->course]
                * (DTREK_EXPDIST * 2)));
        torp->fuse += spec->arm;
        break;

    case DTREK_WPN_MINE:
        torp->status = DTREK_TORP_STRAIGHT;
        torp->speed = 0;
        aim_x = launch->aim_x;
        aim_y = launch->aim_y;
        if (aim_x < 0 || aim_y < 0) {
            aim_x = shooter->x
                + (int)(dtrek_cos_table[course] * DTREK_MINE_RANGE);
            aim_y = shooter->y
                + (int)(dtrek_sin_table[course] * DTREK_MINE_RANGE);
        }

        if (dtrek_ihypot(shooter->x - aim_x, shooter->y - aim_y)
            > DTREK_MINE_RANGE) {
            uint8_t mine_course = dtrek_course_to(
                shooter->x, shooter->y, aim_x, aim_y);

            aim_x = shooter->x
                + (int)(dtrek_cos_table[mine_course] * DTREK_MINE_RANGE);
            aim_y = shooter->y
                + (int)(dtrek_sin_table[mine_course] * DTREK_MINE_RANGE);
        }

        torp->x = clamp_galaxy_coord(aim_x);
        torp->y = clamp_galaxy_coord(aim_y);
        torp->fuse += spec->arm;
        break;

    case DTREK_WPN_PLASMA:
        torp->status = target_slot >= 0
            ? DTREK_TORP_TRACK : DTREK_TORP_MOVE;
        break;

    case DTREK_WPN_MISSILE:
        torp->status = target_slot >= 0
            ? DTREK_TORP_TRACK : DTREK_TORP_MOVE;
        break;

    default:
        torp->status = DTREK_TORP_MOVE;
        break;
    }

    torp->tracking = torp->status == DTREK_TORP_TRACK;
    if (!torp->tracking) torp->target_slot = -1;

    torp->dx = (int)(dtrek_cos_table[torp->course]
        * torp->speed * DTREK_WARP1);
    torp->dy = (int)(dtrek_sin_table[torp->course]
        * torp->speed * DTREK_WARP1);

    shooter->fuel -= spec->fuel_cost;
    if (shooter->fuel < 0) shooter->fuel = 0;

    shooter->torp_reload_interval = spec->reload;
    shooter->torps_unloaded++;
    if (shooter->torps_reload == 0) {
        shooter->torps_reload = spec->reload;
    }

    out->fired = 1;
    out->torpedo_slot = slot;
    out->owner_slot = ship_slot;
    out->weapon_class = torp->weapon_class;
    out->target_slot = torp->target_slot;
    out->tracking = torp->tracking;
    out->arm = torp->arm;
    out->damage = torp->damage;
    out->speed = torp->speed;
    out->fuse = torp->fuse;
    out->fuel_cost = spec->fuel_cost;
    out->tubes_total = shooter->torps_total;
    out->tubes_loaded = shooter->torps_total - shooter->torps_unloaded;
    if (out->tubes_loaded < 0) out->tubes_loaded = 0;

    return 1;
}

int32_t DTREK_CALL dtrek_snapshot(dtrek_engine *engine,
                                  DtrekGameSnapshot *snapshot)
{
    int slot;

    if (engine == NULL || snapshot == NULL) return DTREK_ERR_ARGUMENT;

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->tick = (int32_t)engine->tick;
    snapshot->rng_state_lo = (int32_t)(engine->rng.state & 0xffffffffu);
    snapshot->rng_state_hi = (int32_t)(engine->rng.state >> 32);
    snapshot->ship_count = engine->ship_count;

    for (slot = 0; slot < DTREK_MAX_PLAYERS; ++slot) {
        const DtrekShip *ship = &engine->ships[slot];
        DtrekShipSnapshot *out = &snapshot->ships[slot];
        int i;

        out->in_use = ship->in_use;
        out->slot = slot;
        if (!ship->in_use) continue;

        out->team = ship->team;
        out->status = ship->status;
        out->shields_up = ship->shields_up;
        memcpy(out->name, ship->name, sizeof(out->name));
        out->name[sizeof(out->name) - 1] = '\0';

        out->x = ship->x;
        out->y = ship->y;
        out->course = ship->course;
        out->desired_course = ship->desired_course;
        out->speed = ship->speed;
        out->desired_speed = ship->desired_speed;

        for (i = 0; i < DTREK_NUM_SHIELDS; ++i) {
            out->shield[i] = ship->shield[i];
            out->shield_max[i] = ship->shield_max[i];
        }

        out->hull = ship->hull;
        out->hull_max = ship->hull_max;
        out->armor = ship->armor;
        out->armor_max = ship->armor_max;
        out->fuel = ship->fuel;
        out->fuel_max = ship->fuel_max;
        out->engine_health = ship->engine_health;

        out->beams_total = ship->beams_total;
        out->beams_unloaded = ship->beams_unloaded;
        out->beams_loaded = ship->beams_total - ship->beams_unloaded;
        if (out->beams_loaded < 0) out->beams_loaded = 0;
        out->beams_reload = ship->beams_reload;

        out->tubes_total = ship->torps_total;
        out->tubes_unloaded = ship->torps_unloaded;
        out->tubes_loaded = ship->torps_total - ship->torps_unloaded;
        if (out->tubes_loaded < 0) out->tubes_loaded = 0;
        out->tubes_reload = ship->torps_reload;
        out->torpedoes_in_flight = torpedoes_in_flight(engine, slot);

        out->kills = ship->kills;
        out->torpedo_class = ship->torp_class;
        out->ordnance = kOrdnanceSpecs[ship->torp_class];

        out->orbiting = ship->orbiting;
        out->planet_slot = ship->planet_slot;
        out->repair_mode = ship->repair_mode;
        out->nearest_planet = ship->nearest_planet_slot;
        out->nearest_planet_distance = ship->nearest_planet_dist;
        out->at_fuel_planet = ship->at_fuel_planet;
        out->at_repair_planet = ship->at_repair_planet;

        out->is_robot = ship->is_robot;
        out->robot_offense = ship->robot_offense;
        out->robot_impatience = ship->robot_impatience;
        out->robot_attack_speed = ship->robot_attack_speed;
        out->robot_flags = ship->robot_flags;
        out->robot_state = ship->robot_state;
        out->robot_target = ship->robot_target;
        out->robot_patrol_planet = ship->robot_patrol_planet;
        out->robot_patrol_fuse = ship->robot_patrol_fuse;
        out->robot_evasion_fuse = ship->robot_evasion_fuse;
        out->robot_last_beam_tick = ship->robot_last_beam_tick;
        out->robot_last_beam_target = ship->robot_last_beam_target;
        out->robot_last_beam_hit = ship->robot_last_beam_hit;
        out->robot_last_torpedo_tick = ship->robot_last_torpedo_tick;
        out->robot_last_torpedo_slot = ship->robot_last_torpedo_slot;
        out->robot_last_torpedo_target = ship->robot_last_torpedo_target;

        out->system_count = ship->system_count;
        out->system_size = ship->system_size;
        for (i = 0; i < ship->system_count && i < DTREK_MAX_SYSTEMS; ++i) {
            const DtrekSystemUnit *unit = &ship->system[i];
            DtrekSystemSnapshot *sys = &out->systems[i];

            sys->item = unit->item;
            sys->subitem = unit->subitem;
            sys->size = unit->current_size;
            sys->size_max = unit->undamaged_size;
            sys->fraction = unit->fractional_size;
            sys->repair_rate = unit->repair_rate;
            sys->planet_repair_rate = unit->planet_repair_rate;
        }

        if (ship->status == DTREK_STATUS_ALIVE) snapshot->active_count++;
    }

    for (slot = 0; slot < DTREK_MAX_TORPEDOES; ++slot) {
        const DtrekTorpedo *torp = &engine->torpedoes[slot];
        DtrekTorpedoSnapshot *out = &snapshot->torpedoes[slot];

        out->slot = slot;
        out->in_use = torp->in_use;
        if (!torp->in_use) continue;

        out->owner_slot = torp->owner_slot;
        out->status = torp->status;
        out->team = torp->team;
        out->x = torp->x;
        out->y = torp->y;
        out->course = torp->course;
        out->speed = torp->speed;
        out->damage = torp->damage;
        out->fuse = torp->fuse;
        out->arm = torp->arm;
        out->weapon_class = torp->weapon_class;
        out->target_slot = torp->target_slot;
        out->base_damage = torp->base_damage;
        out->decay = torp->decay;
        snapshot->torpedo_count++;
    }

    for (slot = 0; slot < DTREK_MAX_PLANETS; ++slot) {
        const DtrekPlanet *planet = &engine->planets[slot];
        DtrekPlanetSnapshot *out = &snapshot->planets[slot];

        out->slot = slot;
        out->in_use = planet->in_use;
        if (!planet->in_use) continue;

        out->flags = planet->flags;
        out->owner = planet->owner;
        out->armies = planet->armies;
        out->x = planet->x;
        out->y = planet->y;
        out->dx = planet->dx;
        out->dy = planet->dy;
        memcpy(out->name, planet->name, sizeof(out->name));
        out->name[sizeof(out->name) - 1] = '\0';
        snapshot->planet_count++;
    }

    return DTREK_OK;
}

int32_t DTREK_CALL dtrek_random_below(dtrek_engine *engine, int32_t limit)
{
    if (engine == NULL || limit <= 0) return DTREK_ERR_ARGUMENT;
    return dtrek_rng_below(&engine->rng, limit);
}
