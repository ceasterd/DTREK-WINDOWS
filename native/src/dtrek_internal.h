/*
 * dtrek_internal.h - private engine structures for dtrek_core.
 *
 * Gameplay constants and formulas in this file are derived from the 1989
 * X11 DTREK sources (defs.h, ship.h, player.h, daemon.c, phaser.c, damage.c,
 * search.c, sintab.c).  See the original tree in ../../Dtrek.
 */

#ifndef DTREK_INTERNAL_H
#define DTREK_INTERNAL_H

#include <stdint.h>

#include "dtrek_core.h"

#define DTREK_EXPDIST 400
#define DTREK_DETDIST 800
#define DTREK_TORP_RADIUS (12 * 40)
#define DTREK_PLAYER_FUSE 1
#define DTREK_TORPFUSE 1
#define DTREK_EXPLODE_TICKS (10 / DTREK_PLAYER_FUSE)
#define DTREK_TORP_EXPLODE_TICKS (10 / DTREK_TORPFUSE)
#define DTREK_CHAFF_ARM (10 / DTREK_TORPFUSE)
#define DTREK_MINE_ARM (20 / DTREK_TORPFUSE)
#define DTREK_MINE_RANGE (DTREK_GALAXY_WIDTH / 20)
#define DTREK_TEAM_MASK(team) (1 << (team))

/* Stage 3 place constants, from defs.h, planet.h, and planets.c. */
#define DTREK_MAX_PLANETS 80
#define DTREK_PLANET_NAME_LENGTH 32
#define DTREK_ORBIT_DISTANCE 800
#define DTREK_ENTER_ORBIT_DISTANCE 900
#define DTREK_ORBIT_SPEED 2
#define DTREK_ORBIT_ROTATION 2
#define DTREK_NEBULA_RADIUS (DTREK_GALAXY_WIDTH / 24)
#define DTREK_INTERPLANETARY_DIST (DTREK_GALAXY_WIDTH / 12)
#define DTREK_BLACKHOLE_STRENGTH 5
#define DTREK_WANDERER_FUSE 1200
#define DTREK_HOME_SMALL (DTREK_GALAXY_WIDTH * 2 / 9)
#define DTREK_HOME_LARGE (DTREK_GALAXY_WIDTH - DTREK_HOME_SMALL)

#define DTREK_PLMASK_INDEPENDENT 0x00000001
#define DTREK_PLMASK_FEDERATION 0x00000002
#define DTREK_PLMASK_ROMULAN 0x00000004
#define DTREK_PLMASK_KLINGON 0x00000008
#define DTREK_PLMASK_ORION 0x00000010
#define DTREK_PLMASK_REPAIR 0x00000020
#define DTREK_PLMASK_FUEL 0x00000040
#define DTREK_PLMASK_REDRAW 0x00000080
#define DTREK_PLMASK_HOME 0x00000100
#define DTREK_PLMASK_COUP 0x00000200
#define DTREK_PLMASK_BLACKHOLE 0x00000400
#define DTREK_PLMASK_PULSAR 0x00000800
#define DTREK_PLMASK_BARREN 0x00001000
#define DTREK_PLMASK_WANDERER 0x00002000
#define DTREK_PLMASK_NEBULA 0x00004000
#define DTREK_PLMASK_TMPBARREN 0x00008000
#define DTREK_PLMASK_OWNER_MASK 0x0000001f

/* Stage 4 robot constants, from rmove.c. */
#define DTREK_BOUNDRY_DIST (DTREK_WARP1 * 50)
#define DTREK_CIRCLE_FUSE 25
#define DTREK_MAX_REPAIR_TRIP (DTREK_GALAXY_WIDTH / 10)
#define DTREK_ROBOT_BEAM_MIN_RANGE (DTREK_EXPDIST * 2)
#define DTREK_ROBOT_BEAM_MAX_RANGE (DTREK_EXPDIST * 6)

/* Stage 5 damage-model constants, from damage.c, repair.c, daemon.h, ship.h. */
#define DTREK_DAMAGE_SPLITUP 10
#define DTREK_REPAIR_FUSE 30
#define DTREK_DA_FRACTION 5
#define DTREK_DA_FUEL_CONVERSION (5 * DTREK_DA_FRACTION)

/* Internal patrol and evasion states, matching the PATROL_* and EVADE_* sets. */
enum DtrekRobotPatrolType {
    DTREK_PATROL_NONE = 0,
    DTREK_PATROL_RANDOM = 1,
    DTREK_PATROL_ENEMY = 2,
    DTREK_PATROL_REPAIRING = 3,
    DTREK_PATROL_SEEK_REPAIR = 4
};

enum DtrekRobotEvasionType {
    DTREK_EVADE_NONE = 0,
    DTREK_EVADE_AWAY = 1,
    DTREK_EVADE_SIMPLE = 3,
    DTREK_EVADE_SHIELD = 4,
    DTREK_EVADE_STARBASE = 5
};

typedef struct DtrekRng {
    uint64_t state;
} DtrekRng;

typedef struct dtrek_engine DtrekEngine;

/*
 * The built-in trainer is the Reliant-class default from shipdefaults:
 * hull 150, 27 engines, 4 phaser banks, 5 torpedo tubes.  The five ordnance
 * profiles live in core.c and are selected per ship.
 */
typedef struct DtrekShipSpec {
    int s_hull;
    int s_hull_str;
    int s_armor;
    int s_shield[DTREK_NUM_SHIELDS];
    int s_maxfuel;
    int s_engines;
    int s_maxspeed;
    int s_turns;
    int s_accint;
    int s_decint;
    int s_warpcost;
    int s_recharge;
    int s_repair;
    int s_cargo;
    int s_beam_ktons;        /* hit-table size of one beam bank */
    int s_torp_ktons;        /* hit-table size of one torpedo tube */

    int s_beams;
    int s_beamrange;
    int s_beamdamage;
    int s_beamaccuracy;
    int s_beamreload;
    int s_beamcost;
    int s_beamatten;
    int s_beamclass;

    int s_torps;
    int s_torpspeed;
    int s_torpdamage;
    int s_torpduration;
    int s_torpreload;
    int s_torpdecay;
    int s_torpcost;
    int s_torpclass;
} DtrekShipSpec;

typedef struct DtrekSystemUnit {
    int item;                /* DTREK_SYS_* */
    int subitem;             /* beam bank or tube number */
    int current_size;        /* ktons */
    int undamaged_size;
    int fractional_size;     /* repair accumulator, 1/256 */
    int repair_rate;         /* 1/256 per 30-update repair pulse */
    int planet_repair_rate;  /* additional 1/256 at a friendly repair base */
} DtrekSystemUnit;

typedef struct DtrekShip {
    int in_use;
    DtrekShipSpec spec;
    char name[DTREK_SHIP_NAME_LENGTH];
    int team;
    int team_mask;
    int hostile_mask;
    int status;
    int shields_up;

    int x;
    int y;
    int course;
    int desired_course;
    int subdir;

    int speed;
    int desired_speed;
    int subspeed;

    int shield[DTREK_NUM_SHIELDS];
    int shield_max[DTREK_NUM_SHIELDS];
    int subshield;

    int armor;
    int armor_max;
    int hull;
    int hull_max;

    int fuel;
    int fuel_max;
    int recharge;            /* mutable s_recharge, damaged with dilithium */
    int cargo;               /* mutable s_cargo, damaged with cargo space */
    int engine_health;

    int beams_total;         /* functional banks; damaged with each bank */
    int torps_total;         /* functional tubes; damaged with each tube */
    int beams_unloaded;
    int beams_reload;

    int torps_unloaded;
    int torps_reload;
    int torp_reload_interval;
    int torp_class;          /* selected DtrekWeaponClass */

    int explode_fuse;
    int kills;
    int damage_taken;

    /* Stage 5 subsystem hit table, matching SHIP.s_system. */
    DtrekSystemUnit system[DTREK_MAX_SYSTEMS];
    int system_count;
    int system_size;
    int system_damaged;
    int system_repairable;

    /* Stage 3 place state. */
    int orbiting;
    int planet_slot;
    int repair_mode;
    int nearest_planet_slot;
    int nearest_planet_dist;
    int at_fuel_planet;
    int at_repair_planet;

    /* Stage 4 robot state, one copy per robot. */
    int is_robot;
    int robot_offense;
    int robot_impatience;
    int robot_attack_speed;
    int robot_flags;
    int robot_state;
    int robot_target;
    int robot_patrol_planet;
    int robot_patrol_fuse;
    int robot_patrol_type;
    int robot_evasion_fuse;
    int robot_evasion_type;
    int robot_evade_torpedo;
    int robot_circle_dir;
    int robot_course_deviation;
    int robot_too_close_range;
    int robot_too_far_range;
    int robot_can_fire_beam;
    int robot_last_beam_tick;
    int robot_last_beam_target;
    int robot_last_beam_hit;
    int robot_last_torpedo_tick;
    int robot_last_torpedo_slot;
    int robot_last_torpedo_target;
} DtrekShip;

typedef struct DtrekTorpedo {
    int in_use;
    int status;              /* DTREK_TORP_* */
    int owner_slot;
    int team;
    int x;
    int y;
    int dx;
    int dy;
    int course;              /* 0..255 */
    int damage;
    int speed;
    int fuse;
    int arm;
    int war_mask;
    int team_mask;
    int decay;
    int target_slot;
    int weapon_class;        /* DTREK_WPN_* */
    int base_damage;
    int tracking;
} DtrekTorpedo;

typedef struct DtrekPlanet {
    int in_use;
    int flags;
    int owner;               /* DtrekTeam */
    int armies;
    int x;
    int y;
    int dx;
    int dy;
    int wander_fuse;
    char name[DTREK_PLANET_NAME_LENGTH];
} DtrekPlanet;

struct dtrek_engine {
    DtrekRng rng;
    DtrekRng galaxy_rng;
    DtrekRng robot_rng;
    uint32_t tick;
    int ship_count;
    int planet_count;
    DtrekShip ships[DTREK_MAX_PLAYERS];
    DtrekTorpedo torpedoes[DTREK_MAX_TORPEDOES];
    DtrekPlanet planets[DTREK_MAX_PLANETS];
};

/* rng.c */
void dtrek_rng_seed(DtrekRng *rng, uint32_t seed);
int32_t dtrek_rng_next(DtrekRng *rng);
int32_t dtrek_rng_below(DtrekRng *rng, int32_t limit);

/* math.c */
extern const double dtrek_sin_table[256];
extern const double dtrek_cos_table[256];

uint8_t dtrek_atan2_course(int y, int x);
uint8_t dtrek_course_to(int x1, int y1, int x2, int y2);
uint8_t dtrek_course_delta(uint8_t a, uint8_t b);
int dtrek_isqrt(int n);
int dtrek_ihypot(int x, int y);
int dtrek_vector_close_to_point(
    int refx,
    int refy,
    uint8_t dir,
    int len,
    int targx,
    int targy,
    int delta);

/* core.c helpers shared with robot.c. */
int dtrek_spawn_training_ship(
    DtrekEngine *engine,
    const char *name,
    int team,
    int x,
    int y);

/* robot.c */
int dtrek_robot_config_valid(const DtrekRobotConfig *config);
int dtrek_robot_configure(
    DtrekEngine *engine,
    int ship_slot,
    const DtrekRobotConfig *config);
void dtrek_robot_step(DtrekEngine *engine, int ship_slot);

#endif /* DTREK_INTERNAL_H */
