/*
 * dtrek_core.h - public C ABI for the modern DTREK simulation core.
 *
 * This is a clean-room interface around gameplay rules derived from the
 * 1989 X11 DTREK sources.  The original source is kept in ../../Dtrek and
 * is not modified by this project.
 *
 * Copyright (c) 1986 Chris Guthrie
 * Copyright (c) 1989 Daryl Poe
 * Ported to X11 by Jim Andreas.  Tractors, treaties, and other features by
 * Norm Gee.  Damage window and shield bitmaps by Tom LaStrange.
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of
 * this software for any purpose.  It is provided "as is" without express or
 * implied warranty.
 */

#ifndef DTREK_CORE_H
#define DTREK_CORE_H

#include <stdint.h>

#if defined(_WIN32)
#  if defined(DTREK_CORE_BUILD)
#    define DTREK_API __declspec(dllexport)
#  else
#    define DTREK_API __declspec(dllimport)
#  endif
#  define DTREK_CALL __cdecl
#else
#  if defined(__GNUC__) && __GNUC__ >= 4
#    define DTREK_API __attribute__((visibility("default")))
#  else
#    define DTREK_API
#  endif
#  define DTREK_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define DTREK_ABI_VERSION 6

/* Original configuration constants from defs.h. */
#define DTREK_GALAXY_WIDTH        200000
#define DTREK_WARP1               25
#define DTREK_UPDATE_MICROSECONDS 100000
#define DTREK_UPDATES_PER_SECOND  10
#define DTREK_MAX_PLAYERS         32
#define DTREK_MAX_BEAMS           15
#define DTREK_MAX_TORPEDOES_PER_SHIP 24
#define DTREK_MAX_TORPEDOES       (DTREK_MAX_PLAYERS * DTREK_MAX_TORPEDOES_PER_SHIP)
#define DTREK_MAX_SPEED           12
#define DTREK_NUM_TEAMS           5
#define DTREK_NUM_SHIELDS         4
#define DTREK_SHIP_NAME_LENGTH    32
#define DTREK_MAX_SYSTEMS         50

/* Original place constants from defs.h, planet.h, and planets.c. */
#define DTREK_MAX_PLANETS             80
#define DTREK_PLANET_NAME_LENGTH      32
#define DTREK_ORBIT_DISTANCE          800
#define DTREK_ENTER_ORBIT_DISTANCE    900
#define DTREK_ORBIT_SPEED             2
#define DTREK_NEBULA_RADIUS           (DTREK_GALAXY_WIDTH / 24)
#define DTREK_INTERPLANETARY_DIST     (DTREK_GALAXY_WIDTH / 12)

/* Original torpedo envelopes from defs.h. */
#define DTREK_TORPEDO_EXPLODE_DIST 400
#define DTREK_TORPEDO_DETONATE_DIST 800

#define DTREK_ORDNANCE_COUNT 5
#define DTREK_ORDNANCE_NAME_LENGTH 32

/* Player status values, matching player.h. */
enum DtrekPlayerStatus {
    DTREK_STATUS_FREE = 0,
    DTREK_STATUS_OUTFIT = 1,
    DTREK_STATUS_ALIVE = 2,
    DTREK_STATUS_EXPLODING = 3,
    DTREK_STATUS_DEAD = 4,
    DTREK_STATUS_IMMUNE = 5
};

/* Team values, matching defs.h. */
enum DtrekTeam {
    DTREK_TEAM_INDEPENDENT = 0,
    DTREK_TEAM_FEDERATION = 1,
    DTREK_TEAM_ROMULAN = 2,
    DTREK_TEAM_KLINGON = 3,
    DTREK_TEAM_ORION = 4
};

/* Shield indices, matching defs.h. */
enum DtrekShield {
    DTREK_SHIELD_FRONT = 0,
    DTREK_SHIELD_RIGHT = 1,
    DTREK_SHIELD_REAR = 2,
    DTREK_SHIELD_LEFT = 3
};

/*
 * Damageable system identifiers, matching system.h.  The numbering is the
 * original ship builder's item numbering; 24 is the builder title bar and is
 * intentionally absent.
 */
enum DtrekSystemItem {
    DTREK_SYS_TORP_NUM = 0,
    DTREK_SYS_TORP_SPEED = 1,
    DTREK_SYS_TORP_DAMAGE = 2,
    DTREK_SYS_TORP_DURATION = 3,
    DTREK_SYS_TORP_RELOAD = 4,
    DTREK_SYS_BEAM_NUM = 5,
    DTREK_SYS_BEAM_RANGE = 6,
    DTREK_SYS_BEAM_DAMAGE = 7,
    DTREK_SYS_BEAM_ACCURACY = 8,
    DTREK_SYS_BEAM_RELOAD = 9,
    DTREK_SYS_TRACTOR_BEAM = 10,
    DTREK_SYS_ENGINE = 11,
    DTREK_SYS_MAX_SPEED = 12,
    DTREK_SYS_WARP_COST = 13,
    DTREK_SYS_MANEUVER = 14,
    DTREK_SYS_ACCELERATION = 15,
    DTREK_SYS_DECELERATION = 16,
    DTREK_SYS_DILITHIUM = 17,
    DTREK_SYS_DAMAGE_CONTROL = 18,
    DTREK_SYS_CARGO = 19,
    DTREK_SYS_FRONT_SHIELD = 20,
    DTREK_SYS_ARMOR = 21,
    DTREK_SYS_HULL = 22,
    DTREK_SYS_FUEL = 23,
    DTREK_SYS_SIDE_SHIELD = 25,
    DTREK_SYS_REAR_SHIELD = 26
};

/*
 * One entry in the ship's damage hit table, matching SYSTEM_UNIT in ship.h.
 * Sizes are in the original ktons (hull units).  Repair rates are in 1/256
 * points per 30-update damage-control pulse; shift right by eight for whole
 * points.  "fraction" is the partial point accumulator in the same 1/256
 * units.
 */
typedef struct DtrekSystemSnapshot {
    int32_t item;             /* DtrekSystemItem */
    int32_t subitem;          /* tube/bank number, or 0 */
    int32_t size;             /* current_size */
    int32_t size_max;         /* undamaged_size */
    int32_t fraction;         /* fractional_size, 1/256 */
    int32_t repair_rate;      /* 1/256 per pulse */
    int32_t planet_repair_rate; /* additional 1/256 per pulse at a base */
} DtrekSystemSnapshot;

/*
 * Planet flag bits, matching planet.h.  The low five bits are the owner
 * mask, so a planet can carry exactly one team bit plus feature bits.
 */
enum DtrekPlanetFlags {
    DTREK_PLANET_INDEPENDENT = 0x0001,
    DTREK_PLANET_FEDERATION = 0x0002,
    DTREK_PLANET_ROMULAN = 0x0004,
    DTREK_PLANET_KLINGON = 0x0008,
    DTREK_PLANET_ORION = 0x0010,
    DTREK_PLANET_REPAIR = 0x0020,
    DTREK_PLANET_FUEL = 0x0040,
    DTREK_PLANET_REDRAW = 0x0080,
    DTREK_PLANET_HOME = 0x0100,
    DTREK_PLANET_COUP = 0x0200,
    DTREK_PLANET_BLACKHOLE = 0x0400,
    DTREK_PLANET_PULSAR = 0x0800,
    DTREK_PLANET_BARREN = 0x1000,
    DTREK_PLANET_WANDERER = 0x2000,
    DTREK_PLANET_NEBULA = 0x4000,
    DTREK_PLANET_TMPBARREN = 0x8000,

    DTREK_PLANET_OWNER_MASK = 0x001f
};

/*
 * Robot roles, matching the -o orbiter and starbase flags in robot.c.
 * A starbase robot is held in orbit and never manoeuvres.
 */
enum DtrekRobotFlags {
    DTREK_ROBOT_NONE = 0,
    DTREK_ROBOT_ORBITER = 0x0001,
    DTREK_ROBOT_STARBASE = 0x0002
};

/* Observable robot behaviour, derived from the PATROL_* states in rmove.c. */
enum DtrekRobotState {
    DTREK_ROBOT_IDLE = 0,
    DTREK_ROBOT_PATROL = 1,
    DTREK_ROBOT_ATTACK = 2,
    DTREK_ROBOT_EVADE = 3,
    DTREK_ROBOT_REPAIR = 4,
    DTREK_ROBOT_SEEK_REPAIR = 5,
    DTREK_ROBOT_ORBIT = 6
};

/*
 * Optional robot tuning.  A zero offense/impatience/attack_speed selects the
 * original team and ordnance defaults from robot.c.  patrol_planet is a
 * fixed planet slot, or -1 to choose one automatically.
 */
typedef struct DtrekRobotConfig {
    int32_t offense;         /* 1..100, or 0 for automatic */
    int32_t impatience;      /* 1..100, or 0 for automatic */
    int32_t attack_speed;    /* 0..12, or 0 for the team default */
    int32_t flags;           /* DtrekRobotFlags */
    int32_t patrol_planet;   /* planet slot, or -1 */
} DtrekRobotConfig;

/* Torpedo status values, matching torp.h. */
enum DtrekTorpedoStatus {
    DTREK_TORP_FREE = 0,
    DTREK_TORP_MOVE = 1,
    DTREK_TORP_EXPLODE = 2,
    DTREK_TORP_OFF = 3,
    DTREK_TORP_STRAIGHT = 4,
    DTREK_TORP_TRACK = 5
};

/* Weapon classes, matching weapon.h. */
enum DtrekWeaponClass {
    DTREK_WPN_PHOTON = 0,
    DTREK_WPN_MISSILE = 1,
    DTREK_WPN_PLASMA = 2,
    DTREK_WPN_CHAFF = 3,
    DTREK_WPN_MINE = 4,
    DTREK_WPN_PHASER = 5,
    DTREK_WPN_DISRUPTER = 6,
    DTREK_WPN_TACHYON = 7,
    DTREK_WPN_FUSION = 8,
    DTREK_WPN_PLANET = 9,
    DTREK_WPN_DESTRUCT = 10
};

/*
 * One ordnance profile.  The five trainer profiles are derived from the
 * Reliant-class ship defaults: photon, missile, plasma, chaff, and mine.
 * Speed 0 means "stationary" for mines and "use the ship's speed" for chaff.
 */
typedef struct DtrekOrdnanceSpec {
    int32_t weapon_class;    /* DtrekWeaponClass */
    int32_t speed;           /* units per tick multiplier, as s_torpspeed */
    int32_t damage;          /* warhead damage at launch */
    int32_t duration;        /* base fuse in ticks, before random/arm */
    int32_t reload;          /* ticks between reloaded tubes */
    int32_t decay;           /* damage lost per ten ticks */
    int32_t fuel_cost;       /* fuel consumed per launch */
    int32_t arm;             /* ticks before the proximity fuse is armed */
    int32_t tracking;        /* 1 when the class can home on a locked target */
    char    name[DTREK_ORDNANCE_NAME_LENGTH];
} DtrekOrdnanceSpec;

/*
 * One torpedo launch request.
 *   course       - initial heading, 0..255
 *   target_slot  - locked ship slot for guided weapons, or -1
 *   aim_x/aim_y  - mine placement point; negative means
 *                  "WPN_MINE_RANGE ahead along course"
 *   weapon_class - DtrekWeaponClass, or -1 to use the ship's selection
 */
typedef struct DtrekTorpedoLaunch {
    int32_t course;
    int32_t target_slot;
    int32_t aim_x;
    int32_t aim_y;
    int32_t weapon_class;
} DtrekTorpedoLaunch;

/* Return codes. */
enum DtrekResult {
    DTREK_OK = 0,
    DTREK_ERR_ARGUMENT = -1,
    DTREK_ERR_FULL = -2,
    DTREK_ERR_SHIP = -3,
    DTREK_ERR_STATE = -4,
    DTREK_ERR_WEAPON = -5,
    DTREK_ERR_MEMORY = -6
};

typedef struct dtrek_engine dtrek_engine;

/*
 * All snapshot structures use only 32-bit integers and fixed char arrays so
 * that their layout is identical for MSVC, GCC, and C# sequential layout.
 */
typedef struct DtrekShipSnapshot {
    int32_t in_use;
    int32_t slot;
    int32_t team;
    int32_t status;
    int32_t shields_up;
    char    name[DTREK_SHIP_NAME_LENGTH];

    int32_t x;
    int32_t y;
    int32_t course;          /* 0..255, 0 = up, 64 = right */
    int32_t desired_course;
    int32_t speed;
    int32_t desired_speed;

    int32_t shield[DTREK_NUM_SHIELDS];
    int32_t shield_max[DTREK_NUM_SHIELDS];

    int32_t hull;
    int32_t hull_max;
    int32_t armor;
    int32_t armor_max;

    int32_t fuel;
    int32_t fuel_max;
    int32_t engine_health;   /* 1/256 of nominal */

    int32_t beams_loaded;
    int32_t beams_total;
    int32_t beams_unloaded;
    int32_t beams_reload;

    int32_t tubes_loaded;
    int32_t tubes_total;
    int32_t tubes_unloaded;
    int32_t tubes_reload;
    int32_t torpedoes_in_flight;

    int32_t kills;

    int32_t torpedo_class;       /* selected DtrekWeaponClass */
    DtrekOrdnanceSpec ordnance;  /* selected ordnance profile */

    /* Stage 3 place state. */
    int32_t orbiting;            /* 1 while held in orbit */
    int32_t planet_slot;         /* orbit planet, or -1 */
    int32_t repair_mode;         /* 1 when damage control/rearm is enabled */
    int32_t nearest_planet;      /* nearest planet, or -1 */
    int32_t nearest_planet_distance;
    int32_t at_fuel_planet;      /* 1 when orbiting a friendly fuel planet */
    int32_t at_repair_planet;    /* 1 when orbiting a friendly repair planet */

    /* Stage 4 robot state. */
    int32_t is_robot;
    int32_t robot_offense;
    int32_t robot_impatience;
    int32_t robot_attack_speed;
    int32_t robot_flags;         /* DtrekRobotFlags */
    int32_t robot_state;         /* DtrekRobotState */
    int32_t robot_target;        /* offensive target slot, or -1 */
    int32_t robot_patrol_planet; /* current patrol planet, or -1 */
    int32_t robot_patrol_fuse;
    int32_t robot_evasion_fuse;
    int32_t robot_last_beam_tick;
    int32_t robot_last_beam_target;
    int32_t robot_last_beam_hit;
    int32_t robot_last_torpedo_tick;
    int32_t robot_last_torpedo_slot;
    int32_t robot_last_torpedo_target;

    /* Stage 5 subsystem damage state. */
    int32_t system_count;        /* valid entries in systems[] */
    int32_t system_size;         /* sum of non-armor, non-hull ktons */
    DtrekSystemSnapshot systems[DTREK_MAX_SYSTEMS];
} DtrekShipSnapshot;

/* One torpedo slot.  Slots are grouped per owner: owner * 24 + index. */
typedef struct DtrekTorpedoSnapshot {
    int32_t in_use;
    int32_t slot;
    int32_t owner_slot;
    int32_t status;          /* DtrekTorpedoStatus */
    int32_t team;
    int32_t x;
    int32_t y;
    int32_t course;          /* 0..255 */
    int32_t speed;
    int32_t damage;
    int32_t fuse;
    int32_t arm;
    int32_t weapon_class;    /* DtrekWeaponClass */
    int32_t target_slot;     /* tracking target, or -1 */

    int32_t base_damage;     /* warhead damage before decay/interception */
    int32_t decay;           /* damage lost per ten ticks */
} DtrekTorpedoSnapshot;

/* One planet or spatial feature. */
typedef struct DtrekPlanetSnapshot {
    int32_t in_use;
    int32_t slot;
    int32_t flags;           /* DtrekPlanetFlags */
    int32_t owner;           /* DtrekTeam */
    int32_t armies;
    int32_t x;
    int32_t y;
    int32_t dx;              /* wanderer velocity */
    int32_t dy;
    char    name[DTREK_PLANET_NAME_LENGTH];
} DtrekPlanetSnapshot;

typedef struct DtrekGameSnapshot {
    int32_t tick;
    int32_t rng_state_lo;
    int32_t rng_state_hi;
    int32_t ship_count;      /* slots currently in use */
    int32_t active_count;    /* slots with status == DTREK_STATUS_ALIVE */
    int32_t torpedo_count;   /* torpedo slots currently in use */
    int32_t planet_count;    /* planets currently in use */
    DtrekShipSnapshot ships[DTREK_MAX_PLAYERS];
    DtrekTorpedoSnapshot torpedoes[DTREK_MAX_TORPEDOES];
    DtrekPlanetSnapshot planets[DTREK_MAX_PLANETS];
} DtrekGameSnapshot;

typedef struct DtrekPhaserResult {
    int32_t fired;
    int32_t hit;
    int32_t target_slot;
    int32_t target_team;
    int32_t damage;          /* total damage applied */
    int32_t shield_index;
    int32_t shield_absorbed;
    int32_t armor_absorbed;
    int32_t hull_damage;
    int32_t range;
    int32_t beams_fired;
    int32_t fuel_cost;
    int32_t target_destroyed;

    int32_t target_is_torpedo;
    int32_t torpedo_slot;       /* global torpedo slot, or -1 */
    int32_t torpedo_team;
    int32_t torpedo_damage;     /* beam damage applied to the torpedo */
    int32_t torpedo_destroyed;
} DtrekPhaserResult;

typedef struct DtrekTorpedoResult {
    int32_t fired;
    int32_t torpedo_slot;       /* global slot, or -1 */
    int32_t owner_slot;
    int32_t weapon_class;       /* DtrekWeaponClass */
    int32_t target_slot;        /* locked target, or -1 */
    int32_t tracking;           /* 1 when the torpedo entered track mode */
    int32_t arm;
    int32_t damage;
    int32_t speed;
    int32_t fuse;
    int32_t fuel_cost;
    int32_t tubes_loaded;
    int32_t tubes_total;
} DtrekTorpedoResult;

/* Library metadata. */
DTREK_API uint32_t DTREK_CALL dtrek_abi_version(void);
DTREK_API const char * DTREK_CALL dtrek_version_string(void);

/* Engine lifecycle.  An engine is not thread-safe; use one per thread. */
DTREK_API dtrek_engine * DTREK_CALL dtrek_create(uint32_t seed);
DTREK_API void DTREK_CALL dtrek_destroy(dtrek_engine *engine);
DTREK_API int32_t DTREK_CALL dtrek_reset(dtrek_engine *engine, uint32_t seed);

/*
 * Spawn the built-in Reliant-class trainer ship in a free slot.
 * Returns the slot index (0..31) on success or a negative DtrekResult.
 */
DTREK_API int32_t DTREK_CALL dtrek_spawn(
    dtrek_engine *engine,
    const char *name,
    int32_t team,
    int32_t x,
    int32_t y);

/* Set a ship's desired course (0..255) and desired warp speed (0..12). */
DTREK_API int32_t DTREK_CALL dtrek_set_controls(
    dtrek_engine *engine,
    int32_t ship_slot,
    int32_t desired_course,
    int32_t desired_speed);

/* Raise (1) or lower (0) a ship's shields. */
DTREK_API int32_t DTREK_CALL dtrek_set_shields(
    dtrek_engine *engine,
    int32_t ship_slot,
    int32_t up);

/* Advance the simulation by a number of 100 ms updates. */
DTREK_API int32_t DTREK_CALL dtrek_step(
    dtrek_engine *engine,
    int32_t ticks);

/*
 * Fire the ship's loaded phaser banks along "aim_course".
 * Returns 1 for a hit, 0 for a miss, or a negative DtrekResult.
 * "result" is optional; when supplied it is always filled on a valid shot.
 */
DTREK_API int32_t DTREK_CALL dtrek_fire_phaser(
    dtrek_engine *engine,
    int32_t ship_slot,
    int32_t aim_course,
    DtrekPhaserResult *result);

/*
 * Select one of the five ordnance classes for a ship.  New ships start with
 * a team default: Federation/Independent/Orion use photons, Romulans use
 * plasma, and Klingons use missiles.  Training ships may select any class.
 */
DTREK_API int32_t DTREK_CALL dtrek_set_ordnance(
    dtrek_engine *engine,
    int32_t ship_slot,
    int32_t weapon_class);

/* Copy the built-in profile for one ordnance class into "spec". */
DTREK_API int32_t DTREK_CALL dtrek_ordnance_spec(
    dtrek_engine *engine,
    int32_t weapon_class,
    DtrekOrdnanceSpec *spec);

/*
 * Launch one torpedo using a launch request.
 * Returns 1 when a torpedo left the tube, 0 when the shot was refused
 * without consuming a tube, or a negative DtrekResult.
 * "result" is optional; when supplied it is always filled.
 */
DTREK_API int32_t DTREK_CALL dtrek_fire_torpedo(
    dtrek_engine *engine,
    int32_t ship_slot,
    const DtrekTorpedoLaunch *launch,
    DtrekTorpedoResult *result);

/*
 * Stage 3 places.  An engine starts with an empty map so combat-focused
 * callers keep deterministic spawn coordinates.  Generate the authentic
 * 80-object galaxy explicitly, or place individual planets for scenarios.
 */
DTREK_API int32_t DTREK_CALL dtrek_generate_galaxy(
    dtrek_engine *engine,
    uint32_t seed);

DTREK_API int32_t DTREK_CALL dtrek_clear_planets(dtrek_engine *engine);

/*
 * Write one planet into a fixed slot (0..79).  flags uses DtrekPlanetFlags;
 * owner is a DtrekTeam.  "name" may be NULL.
 */
DTREK_API int32_t DTREK_CALL dtrek_place_planet(
    dtrek_engine *engine,
    int32_t planet_slot,
    int32_t flags,
    int32_t owner,
    int32_t armies,
    int32_t x,
    int32_t y,
    const char *name);

/*
 * Enter orbit around planet_slot, or pass -1 to choose the nearest eligible
 * planet.  Returns the planet slot on success or a negative DtrekResult.
 * Orbit requires speed <= DTREK_ORBIT_SPEED and range <=
 * DTREK_ENTER_ORBIT_DISTANCE; black holes and nebulae cannot be orbited.
 */
DTREK_API int32_t DTREK_CALL dtrek_orbit(
    dtrek_engine *engine,
    int32_t ship_slot,
    int32_t planet_slot);

DTREK_API int32_t DTREK_CALL dtrek_leave_orbit(
    dtrek_engine *engine,
    int32_t ship_slot);

/*
 * Enable or disable damage control.  Shields regenerate every update.  On the
 * original 30-update repair pulse, damaged subsystems self-repair when they
 * have a non-zero repair rate; a stationary ship in orbit of a friendly
 * repair planet also receives the planet repair rate, which can restore
 * otherwise unrepairable armor.  Like the original, damage control also
 * rearms every functional bank and tube at a repair base.
 */
DTREK_API int32_t DTREK_CALL dtrek_set_repair(
    dtrek_engine *engine,
    int32_t ship_slot,
    int32_t on);

/*
 * Stage 4 robots.  Spawn a Reliant-class trainer that runs the ported rmove()
 * behaviour each update: target selection, phaser and torpedo solutions,
 * torpedo evasion, retreat, repair/seek-repair, attack orbits, patrols, and
 * hazard/edge avoidance.  "name" may be NULL; "config" may be NULL to use the
 * original automatic defaults.
 */
DTREK_API int32_t DTREK_CALL dtrek_spawn_robot(
    dtrek_engine *engine,
    const char *name,
    int32_t team,
    int32_t x,
    int32_t y,
    const DtrekRobotConfig *config);

/* Retune an existing robot.  Returns DTREK_ERR_STATE for a non-robot ship. */
DTREK_API int32_t DTREK_CALL dtrek_set_robot(
    dtrek_engine *engine,
    int32_t ship_slot,
    const DtrekRobotConfig *config);

/* Remove a robot and any torpedoes it still has in flight. */
DTREK_API int32_t DTREK_CALL dtrek_clear_robot(
    dtrek_engine *engine,
    int32_t ship_slot);

/* Copy the current state into a caller-owned snapshot. */
DTREK_API int32_t DTREK_CALL dtrek_snapshot(
    dtrek_engine *engine,
    DtrekGameSnapshot *snapshot);

/* Deterministic POSIX lrand48-compatible generator, exposed for tests. */
DTREK_API int32_t DTREK_CALL dtrek_random_below(
    dtrek_engine *engine,
    int32_t limit);

#ifdef __cplusplus
}
#endif

#endif /* DTREK_CORE_H */
