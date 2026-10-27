/*
 * robot.c - ported robot commander behaviour from the 1989 X11 DTREK sources.
 *
 * The original robot is a separate process that runs rmove() once per update.
 * This file keeps that decision order and its constants, but reads and writes
 * DtrekShip records directly.  All robot choices use engine->robot_rng so the
 * combat and galaxy streams stay independent and reproducible.
 */

#include <limits.h>
#include <math.h>
#include <string.h>

#include "dtrek_internal.h"

#define DTREK_ROBOT_DIR_UP 0
#define DTREK_ROBOT_DIR_UP_RIGHT 32
#define DTREK_ROBOT_DIR_RIGHT 64
#define DTREK_ROBOT_DIR_DOWN_RIGHT 96
#define DTREK_ROBOT_DIR_DOWN 128
#define DTREK_ROBOT_DIR_DOWN_LEFT 160
#define DTREK_ROBOT_DIR_LEFT 192
#define DTREK_ROBOT_DIR_UP_LEFT 224

typedef struct RobotTarget {
    int slot;
    int dist;
    uint8_t course;
    int intruder;
    int is_starbase;
    int torp_ready;
    int torp_course;
    int torp_time;
    int mine_x;
    int mine_y;
    int beam_ready;
} RobotTarget;

static int robot_alive(const DtrekShip *ship)
{
    return ship->in_use && ship->status == DTREK_STATUS_ALIVE;
}

static int robot_is_starbase(const DtrekShip *ship)
{
    return ship->is_robot && (ship->robot_flags & DTREK_ROBOT_STARBASE) != 0;
}

static int robot_is_orbiter(const DtrekShip *ship)
{
    return (ship->robot_flags & DTREK_ROBOT_ORBITER) != 0;
}

static const DtrekPlanet *robot_planet_at(const DtrekEngine *engine, int slot)
{
    if (engine == NULL || slot < 0 || slot >= DTREK_MAX_PLANETS) return NULL;
    if (!engine->planets[slot].in_use) return NULL;
    return &engine->planets[slot];
}

static int robot_planet_friendly(const DtrekShip *ship,
                                 const DtrekPlanet *planet)
{
    return planet->owner == ship->team
        || planet->owner == DTREK_TEAM_INDEPENDENT;
}

static int robot_planet_orbitable(const DtrekPlanet *planet)
{
    return (planet->flags & (DTREK_PLMASK_BLACKHOLE | DTREK_PLMASK_NEBULA))
        == 0;
}

static int robot_nearest_planet(const DtrekEngine *engine, int x, int y,
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

static int robot_nearest_team_planet(const DtrekEngine *engine,
                                     const DtrekShip *ship,
                                     int required_flags,
                                     int *distance)
{
    int slot;
    int best = -1;
    int best_distance = INT_MAX;

    for (slot = 0; slot < DTREK_MAX_PLANETS; ++slot) {
        const DtrekPlanet *planet = &engine->planets[slot];
        int dist;

        if (!planet->in_use) continue;
        if (planet->owner != ship->team) continue;
        if (!robot_planet_orbitable(planet)) continue;
        if ((planet->flags & required_flags) != required_flags) continue;

        dist = dtrek_ihypot(ship->x - planet->x, ship->y - planet->y);
        if (dist < best_distance) {
            best_distance = dist;
            best = slot;
        }
    }

    if (distance != NULL) *distance = best >= 0 ? best_distance : 0;
    return best;
}

static int robot_nearest_friendly_planet(const DtrekEngine *engine,
                                         const DtrekShip *ship,
                                         int required_flags,
                                         int *distance)
{
    int slot;
    int best = -1;
    int best_distance = INT_MAX;

    for (slot = 0; slot < DTREK_MAX_PLANETS; ++slot) {
        const DtrekPlanet *planet = &engine->planets[slot];
        int dist;

        if (!planet->in_use) continue;
        if (!robot_planet_friendly(ship, planet)) continue;
        if (!robot_planet_orbitable(planet)) continue;
        if ((planet->flags & required_flags) != required_flags) continue;

        dist = dtrek_ihypot(ship->x - planet->x, ship->y - planet->y);
        if (dist < best_distance) {
            best_distance = dist;
            best = slot;
        }
    }

    if (distance != NULL) *distance = best >= 0 ? best_distance : 0;
    return best;
}

static int robot_nearest_team_planet_to(const DtrekEngine *engine,
                                        const DtrekShip *ship,
                                        int x, int y, int *distance)
{
    int slot;
    int best = -1;
    int best_distance = INT_MAX;

    for (slot = 0; slot < DTREK_MAX_PLANETS; ++slot) {
        const DtrekPlanet *planet = &engine->planets[slot];
        int dist;

        if (!planet->in_use) continue;
        if (planet->owner != ship->team) continue;
        if (!robot_planet_orbitable(planet)) continue;

        dist = dtrek_ihypot(x - planet->x, y - planet->y);
        if (dist < best_distance) {
            best_distance = dist;
            best = slot;
        }
    }

    if (distance != NULL) *distance = best >= 0 ? best_distance : 0;
    return best;
}

static int robot_pick_team_planet(DtrekEngine *engine,
                                  const DtrekShip *ship)
{
    int planets[DTREK_MAX_PLANETS];
    int count = 0;
    int slot;

    for (slot = 0; slot < DTREK_MAX_PLANETS; ++slot) {
        const DtrekPlanet *planet = &engine->planets[slot];

        if (!planet->in_use) continue;
        if (planet->owner != ship->team) continue;
        if (!robot_planet_orbitable(planet)) continue;
        planets[count++] = slot;
    }

    if (count == 0) return -1;
    return planets[dtrek_rng_below(&engine->robot_rng, count)];
}

static int robot_needs_repair(const DtrekShip *ship)
{
    /* Original rmove.c:rrepair need_repair predicate. */
    return ship->system_repairable
        || (ship->system_damaged && ship->orbiting
            && ship->at_repair_planet);
}

static int robot_needs_fuel(const DtrekShip *ship)
{
    return ship->fuel < ship->fuel_max / 5;
}

static int robot_shield_hit(uint8_t weapon_course, uint8_t player_course)
{
    uint8_t delta = (uint8_t)(weapon_course - player_course + 128);

    if (delta >= 224 || delta <= 32) return DTREK_SHIELD_FRONT;
    if (delta <= 96) return DTREK_SHIELD_RIGHT;
    if (delta <= 160) return DTREK_SHIELD_REAR;
    return DTREK_SHIELD_LEFT;
}

/* Original sintab.c:min_quadratic_solution, kept verbatim. */
static double robot_min_quadratic(double a, double b, double c)
{
    double d;

    if (a == 0.0) {
        if (b != 0.0) c = -c / b;
        else c = -1.0;
    }
    else {
        a *= 2.0;
        if (a > 0.0) {
            a = -a;
            b = -b;
            c = -c;
        }
        c = b * b - 2.0 * a * c;
        if (c > 0.0) {
            c = sqrt(c);
            d = c - b;
            if (d < 0.0) c = d / a;
            else c = (-b - c) / a;
        }
    }

    return c;
}

/* Original rmove.c:time_will_hit.  Galactic warp-adjusted units. */
static int robot_time_will_hit(double px, double py, double pdx, double pdy,
                               double tx, double ty, double tdx, double tdy,
                               int fuse)
{
    double u;
    double v;

    if ((u = tdx * pdy - tdy * pdx) != 0.0) {
        v = ((px * pdy - py * pdx) - (tx * pdy - ty * pdx)) / u;
        if (pdx != 0.0) u = (tx - px + v * tdx) / pdx;
        else u = (ty - py + v * tdy) / pdy;
    }
    else {
        if (tdx == 0.0 && tdy == 0.0) {
            if (px != tx) return -1;
            if (py != ty) return -1;
            return 0;
        }
        if (pdx == 0.0 && pdy == 0.0) {
            if (tdx != 0.0) {
                u = (px - tx) / tdx;
                if (u < 0.0) return -1;
                if (fabs(py - (ty + u * tdy)) > DTREK_EXPDIST) return -1;
                if (u > fuse) return -1;
                return (int)u;
            }
            else {
                u = (py - ty) / tdy;
                if (u < 0.0) return -1;
                if (fabs(px - (tx + u * tdx)) > DTREK_EXPDIST) return -1;
                if (u > fuse) return -1;
                return (int)u;
            }
        }
        if (pdy * (tx - px) == pdx * (ty - py)) {
            if (tdx == pdx && tdy == pdy) return -1;
            if (tdy != pdy) u = v = (py - ty) / (tdy - pdy);
            else u = v = (px - tx) / (tdx - pdx);
        }
        else {
            return -1;
        }
    }

    if (u < 0.0 || v < 0.0) return -1;
    if (v > fuse) return -1;
    if (hypot(tx - px + u * (tdx - pdx),
              ty - py + u * (tdy - pdy)) > DTREK_EXPDIST) {
        return -1;
    }

    return (int)((u + v) / 2.0);
}

/* Original rmove.c:time_track_will_hit. */
static int robot_time_track_will_hit(double px, double py,
                                     double pdx, double pdy,
                                     double tx, double ty,
                                     int t_speed, int fuse)
{
    double ex = px - tx;
    double ey = py - ty;
    double a = pdx * pdx + pdy * pdy - (double)t_speed * (double)t_speed;
    double b = 2.0 * (ex * pdx + ey * pdy);
    double c = ex * ex + ey * ey;
    double t = robot_min_quadratic(a, b, c);

    if ((int)t > fuse) return -1;
    if (t < 0.0) return -1;
    return (int)t;
}

/*
 * Original rmove.c:projectDamage.  The 1989 code never updated its minimum
 * time, so the "closest" torpedo was the last one scanned.  This version
 * tracks the true minimum, which only changes which torpedo a defensive beam
 * targets.
 */
static int robot_project_damage(const DtrekEngine *engine,
                                const DtrekShip *ship,
                                uint8_t *dir_out, int *tracking,
                                int *closest_slot, int *last_time)
{
    double px = (double)ship->x / DTREK_WARP1;
    double py = (double)ship->y / DTREK_WARP1;
    double pdx = (double)ship->speed * dtrek_cos_table[ship->course];
    double pdy = (double)ship->speed * dtrek_sin_table[ship->course];
    double dir_x = 0.0;
    double dir_y = 0.0;
    int damage = 0;
    int min_time = INT_MAX;
    int slot;

    *tracking = 0;
    *last_time = 1;
    *closest_slot = -1;

    for (slot = 0; slot < DTREK_MAX_TORPEDOES; ++slot) {
        const DtrekTorpedo *torp = &engine->torpedoes[slot];
        double tx;
        double ty;
        int mytime;

        if (!torp->in_use) continue;
        if (!(torp->war_mask & ship->team_mask)) continue;
        if (torp->weapon_class == DTREK_WPN_CHAFF) continue;

        if (torp->status == DTREK_TORP_MOVE
            || torp->status == DTREK_TORP_STRAIGHT) {
            double tdx;
            double tdy;

            tx = (double)torp->x / DTREK_WARP1;
            ty = (double)torp->y / DTREK_WARP1;
            tdx = (double)torp->speed
                * dtrek_cos_table[torp->course];
            tdy = (double)torp->speed
                * dtrek_sin_table[torp->course];

            mytime = robot_time_will_hit(px, py, pdx, pdy,
                tx, ty, tdx, tdy, torp->fuse);
            if (mytime >= 0) {
                if (mytime < min_time) {
                    min_time = mytime;
                    *closest_slot = slot;
                }
                if (mytime > *last_time) *last_time = mytime;
                damage += torp->damage;
                dir_x += dtrek_cos_table[torp->course];
                dir_y += dtrek_sin_table[torp->course];
            }
        }
        else if (torp->status == DTREK_TORP_TRACK) {
            int dam;

            if (torp->target_slot != (int)(ship - engine->ships)) continue;
            tx = (double)torp->x / DTREK_WARP1;
            ty = (double)torp->y / DTREK_WARP1;

            mytime = robot_time_track_will_hit(px, py, pdx, pdy,
                tx, ty, torp->speed, torp->fuse);
            if (mytime < 0) continue;

            dam = torp->damage;
            if (torp->decay > 0) {
                dam -= mytime * torp->decay;
                if (dam < 0) continue;
            }

            *tracking = 1;
            if (mytime < min_time) {
                min_time = mytime;
                *closest_slot = slot;
            }
            if (mytime > *last_time) *last_time = mytime;
            dir_x += dtrek_cos_table[torp->course];
            dir_y += dtrek_sin_table[torp->course];
            damage += dam;
        }
    }

    *dir_out = dtrek_atan2_course((int)(-dir_y * 1024.0),
                                  (int)(dir_x * 1024.0));
    return damage;
}

/* Original search.c:is_planet_in_way. */
static int robot_planet_in_way(const DtrekEngine *engine,
                               int x, int y, uint8_t dir, int maxdist)
{
    int slot;

    for (slot = 0; slot < DTREK_MAX_PLANETS; ++slot) {
        const DtrekPlanet *planet = &engine->planets[slot];

        if (!planet->in_use) continue;
        if (planet->flags & DTREK_PLMASK_NEBULA) continue;
        if (dtrek_vector_close_to_point(x, y, dir, maxdist,
                planet->x, planet->y, DTREK_ORBIT_DISTANCE * 3 / 4)) {
            return 1;
        }
    }

    return 0;
}

/* Original rmove.c:compute_torp_course. */
static int robot_compute_torp_course(const DtrekShip *me,
                                     const DtrekShip *target,
                                     int torp_speed, int delay,
                                     int *torp_course, int *torp_time)
{
    double relpos_x;
    double relpos_y;
    double hiscourse_x =
        dtrek_cos_table[target->course] * target->speed;
    double hiscourse_y =
        dtrek_sin_table[target->course] * target->speed;
    double a;
    double b;
    double c;
    int newpos_x;
    int newpos_y;

    if (me->torp_class == DTREK_WPN_CHAFF) {
        relpos_x = ((double)target->x
            - ((double)me->x
                + dtrek_cos_table[me->course] * (DTREK_EXPDIST * 2)))
            / DTREK_WARP1;
        relpos_y = ((double)target->y
            - ((double)me->y
                + dtrek_sin_table[me->course] * (DTREK_EXPDIST * 2)))
            / DTREK_WARP1;
        a = (double)target->speed * target->speed
            - (double)me->speed * me->speed;
    }
    else {
        relpos_x = (double)(target->x - me->x) / DTREK_WARP1;
        relpos_y = (double)(target->y - me->y) / DTREK_WARP1;
        a = (double)target->speed * target->speed
            - (double)torp_speed * torp_speed;
    }

    b = 2.0 * (relpos_x * hiscourse_x + relpos_y * hiscourse_y);
    c = relpos_x * relpos_x + relpos_y * relpos_y;
    c = robot_min_quadratic(a, b, c);

    if (c < 0.0) {
        if (delay < 0) {
            newpos_x = (int)((relpos_x + hiscourse_x * 10.0)
                * DTREK_WARP1);
            newpos_y = (int)((relpos_y + hiscourse_y * 10.0)
                * DTREK_WARP1);
            *torp_course = dtrek_course_to(me->x, me->y,
                me->x + newpos_x, me->y + newpos_y);
        }
        return 0;
    }

    if (delay > 0 && c > (double)delay) return 0;

    newpos_x = (int)((double)target->x
        + c * hiscourse_x * DTREK_WARP1);
    newpos_y = (int)((double)target->y
        + c * hiscourse_y * DTREK_WARP1);
    *torp_course = dtrek_course_to(me->x, me->y, newpos_x, newpos_y);
    *torp_time = (int)c;
    return 1;
}

/* Original rmove.c:compute_mine_placement. */
static int robot_compute_mine(const DtrekShip *me, const DtrekShip *target,
                              int *mine_x, int *mine_y)
{
    int lead = DTREK_WARP1 * (DTREK_MINE_ARM + 5);

    *mine_x = target->x
        + (int)(dtrek_cos_table[target->course] * target->speed * lead);
    if (*mine_x < 0 || *mine_x > DTREK_GALAXY_WIDTH) return 0;

    *mine_y = target->y
        + (int)(dtrek_sin_table[target->course] * target->speed * lead);
    if (*mine_y < 0 || *mine_y > DTREK_GALAXY_WIDTH) return 0;

    return dtrek_ihypot(me->x - *mine_x, me->y - *mine_y)
        <= DTREK_MINE_RANGE;
}

/* Original rmove.c:compute_chaff_course. */
static int robot_compute_chaff(const DtrekShip *me, const DtrekShip *target,
                               int mydelay, int *torp_course, int *torp_time)
{
    double px = (double)target->x / DTREK_WARP1;
    double py = (double)target->y / DTREK_WARP1;
    double pdx = (double)target->speed * dtrek_cos_table[target->course];
    double pdy = (double)target->speed * dtrek_sin_table[target->course];
    double tx = ((double)me->x
        + dtrek_cos_table[me->course] * (DTREK_EXPDIST * 2))
        / DTREK_WARP1;
    double ty = ((double)me->y
        + dtrek_sin_table[me->course] * (DTREK_EXPDIST * 2))
        / DTREK_WARP1;
    double tdx = (double)me->speed * dtrek_cos_table[me->course];
    double tdy = (double)me->speed * dtrek_sin_table[me->course];
    int mytime;

    mytime = robot_time_will_hit(px, py, pdx, pdy,
        tx, ty, tdx, tdy, mydelay);
    if (mytime < 0) return 0;

    *torp_course = me->course;
    *torp_time = mytime;
    return 1;
}

static int robot_can_beam(const DtrekShip *ship)
{
    return ship->beams_total > 0
        && ship->beams_unloaded < ship->beams_total
        && ship->fuel > ship->spec.s_beamcost;
}

static int robot_too_close_range(DtrekEngine *engine,
                                 const DtrekShip *ship,
                                 const DtrekShip *target)
{
    int range = 0;

    if (target->torps_total > 0) {
        DtrekOrdnanceSpec spec;

        if (dtrek_ordnance_spec(engine, target->torp_class, &spec)
            == DTREK_OK) {
            switch (target->torp_class) {
            case DTREK_WPN_PHOTON:
                range = spec.speed * spec.duration * (DTREK_WARP1 / 2);
                break;
            case DTREK_WPN_PLASMA:
                range = spec.speed * (spec.damage / spec.decay)
                    * (DTREK_WARP1 / 2);
                break;
            case DTREK_WPN_MISSILE:
                range = spec.speed * spec.duration * DTREK_WARP1;
                break;
            case DTREK_WPN_CHAFF:
                range = DTREK_GALAXY_WIDTH / 30;
                break;
            case DTREK_WPN_MINE:
                range = DTREK_MINE_RANGE;
                break;
            default:
                range = 0;
                break;
            }
        }
    }

    if (target->beams_total > 0
        && target->spec.s_beamrange > range) {
        range = target->spec.s_beamrange;
    }

    if (range > ship->robot_too_far_range) range = DTREK_DETDIST;
    return range;
}

static int robot_target_intruder(DtrekEngine *engine,
                                 const DtrekShip *ship,
                                 const DtrekShip *target)
{
    int planet_slot;
    int distance;

    if (ship->team == DTREK_TEAM_INDEPENDENT) return 1;

    planet_slot = robot_nearest_planet(engine, target->x, target->y,
        &distance);
    (void)distance;
    if (planet_slot >= 0) {
        const DtrekPlanet *planet = &engine->planets[planet_slot];

        if (planet->owner == ship->team) return 1;
    }

    if (ship->robot_offense > 95
        && dtrek_rng_below(&engine->robot_rng, 8) == 0) {
        return 1;
    }

    return 0;
}

static void robot_fill_target(DtrekEngine *engine,
                              const DtrekShip *ship, int slot,
                              RobotTarget *target)
{
    const DtrekShip *other = &engine->ships[slot];

    memset(target, 0, sizeof(*target));
    target->slot = slot;
    target->dist = dtrek_ihypot(other->x - ship->x,
        other->y - ship->y);
    target->course = dtrek_course_to(ship->x, ship->y,
        other->x, other->y);
    target->intruder = robot_target_intruder(engine, ship, other);
    target->is_starbase = robot_is_starbase(other);
    target->mine_x = -1;
    target->mine_y = -1;
}

static int robot_find_nearest(DtrekEngine *engine,
                              const DtrekShip *ship, int skip_starbase,
                              RobotTarget *out)
{
    int slot;
    int best = -1;
    int best_distance = INT_MAX;

    for (slot = 0; slot < DTREK_MAX_PLAYERS; ++slot) {
        const DtrekShip *other = &engine->ships[slot];
        int distance;

        if (!robot_alive(other)) continue;
        if (other == ship) continue;
        if (!(ship->hostile_mask & other->team_mask)) continue;
        if (skip_starbase && robot_is_starbase(other)) continue;

        distance = dtrek_ihypot(other->x - ship->x,
            other->y - ship->y);
        if (distance < best_distance) {
            best_distance = distance;
            best = slot;
        }
    }

    if (best < 0) {
        memset(out, 0, sizeof(*out));
        out->slot = -1;
        out->mine_x = -1;
        out->mine_y = -1;
        return 0;
    }

    robot_fill_target(engine, ship, best, out);
    return 1;
}

static void robot_analyze_target(DtrekEngine *engine,
                                 DtrekShip *ship, RobotTarget *target)
{
    const DtrekShip *other = &engine->ships[target->slot];
    const DtrekOrdnanceSpec *spec;
    DtrekOrdnanceSpec local_spec;
    int torp_cost;
    int delay;

    target->beam_ready = 0;
    target->torp_ready = 0;
    ship->robot_can_fire_beam = robot_can_beam(ship);

    if (ship->beams_total > 0
        && ship->beams_unloaded < ship->beams_total
        && ship->fuel > ship->spec.s_beamcost
        && target->dist < ship->spec.s_beamrange
            * ship->robot_impatience / 100) {
        target->beam_ready = 1;
    }

    if (target->dist >= DTREK_GALAXY_WIDTH / 10) goto done;
    if (ship->torps_unloaded == ship->torps_total) goto done;
    if (dtrek_ordnance_spec(engine, ship->torp_class,
            &local_spec) != DTREK_OK) {
        goto done;
    }

    spec = &local_spec;
    torp_cost = spec->fuel_cost;
    if (ship->fuel <= torp_cost) goto done;
    if (ship->robot_offense
        < 75 - ((ship->fuel - torp_cost) * 100 / ship->fuel_max)) {
        goto done;
    }

    if (ship->torp_class == DTREK_WPN_CHAFF) {
        if (robot_compute_chaff(ship, other, 20,
                &target->torp_course, &target->torp_time)
            && !robot_planet_in_way(engine, ship->x, ship->y,
                (uint8_t)ship->course,
                ship->speed * DTREK_WARP1 * target->torp_time)) {
            target->torp_ready = 1;
        }
    }
    else if (ship->torp_class == DTREK_WPN_MINE) {
        if (robot_compute_mine(ship, other,
                &target->mine_x, &target->mine_y)) {
            target->torp_ready = 1;
        }
    }
    else {
        delay = (spec->duration + 20)
            * ship->robot_impatience / 100;
        if (robot_compute_torp_course(ship, other, spec->speed,
                delay, &target->torp_course, &target->torp_time)
            && !robot_planet_in_way(engine, ship->x, ship->y,
                (uint8_t)target->torp_course,
                target->torp_time * DTREK_WARP1 * spec->speed)) {
            target->torp_ready = 1;
        }
    }

done:
    ship->robot_too_close_range =
        robot_too_close_range(engine, ship, other);
}

static void robot_record_beam(const DtrekEngine *engine, DtrekShip *ship,
                              const DtrekPhaserResult *result)
{
    ship->robot_last_beam_tick = (int)engine->tick;
    ship->robot_last_beam_target = result->target_slot;
    ship->robot_last_beam_hit = result->hit;
}

static void robot_record_torpedo(const DtrekEngine *engine,
                                 DtrekShip *ship,
                                 const DtrekTorpedoResult *result)
{
    ship->robot_last_torpedo_tick = (int)engine->tick;
    ship->robot_last_torpedo_slot = result->torpedo_slot;
    ship->robot_last_torpedo_target = result->target_slot;
}

static int robot_fire_weapons(DtrekEngine *engine, int slot,
                              DtrekShip *ship, RobotTarget *target)
{
    if (target->torp_ready) {
        DtrekTorpedoLaunch launch;
        DtrekTorpedoResult result;

        memset(&launch, 0, sizeof(launch));
        memset(&result, 0, sizeof(result));
        launch.course = target->torp_course;
        launch.weapon_class = ship->torp_class;
        launch.target_slot = -1;
        if (ship->torp_class == DTREK_WPN_MISSILE
            || ship->torp_class == DTREK_WPN_PLASMA) {
            launch.target_slot = target->slot;
        }
        launch.aim_x = target->mine_x;
        launch.aim_y = target->mine_y;

        if (dtrek_fire_torpedo(engine, slot, &launch, &result) == 1) {
            robot_record_torpedo(engine, ship, &result);
            return 1;
        }
    }

    if (target->beam_ready && robot_can_beam(ship)) {
        DtrekPhaserResult result;

        memset(&result, 0, sizeof(result));
        if (dtrek_fire_phaser(engine, slot, target->course, &result) >= 0) {
            robot_record_beam(engine, ship, &result);
            return 1;
        }
    }

    return 0;
}

static int robot_fire_defensive_beam(DtrekEngine *engine, int slot,
                                     DtrekShip *ship, int torpedo_slot)
{
    const DtrekTorpedo *torp = &engine->torpedoes[torpedo_slot];
    DtrekPhaserResult result;
    uint8_t course;
    int dist;

    if (torpedo_slot < 0 || !torp->in_use) return 0;
    if (torp->status == DTREK_TORP_FREE
        || torp->status == DTREK_TORP_EXPLODE
        || torp->status == DTREK_TORP_OFF) {
        return 0;
    }
    if (torp->weapon_class == DTREK_WPN_CHAFF) return 0;
    if (torp->damage <= ship->spec.s_beamdamage / 2) return 0;

    dist = dtrek_ihypot(ship->x - torp->x, ship->y - torp->y);
    if (dist <= DTREK_ROBOT_BEAM_MIN_RANGE
        || dist >= DTREK_ROBOT_BEAM_MAX_RANGE) {
        return 0;
    }

    course = dtrek_course_to(ship->x, ship->y, torp->x, torp->y);
    memset(&result, 0, sizeof(result));
    if (dtrek_fire_phaser(engine, slot, course, &result) >= 0) {
        robot_record_beam(engine, ship, &result);
        return result.hit;
    }

    return 0;
}

static int robot_good_shield_dir(DtrekShip *ship, uint8_t torpdir,
                                 int *present_ok)
{
    int best_shield = DTREK_SHIELD_FRONT;
    int shield_str = -1;
    int current_shield;
    int i;

    for (i = 0; i < DTREK_NUM_SHIELDS; ++i) {
        if (ship->shield[i] > shield_str) {
            best_shield = i;
            shield_str = ship->shield[i];
        }
    }

    current_shield = robot_shield_hit(torpdir, (uint8_t)ship->course);
    if (abs(ship->shield[current_shield] - shield_str) < 20) {
        *present_ok = 1;
        return ship->course;
    }

    *present_ok = 0;
    ship->desired_speed = 2;
    switch (best_shield) {
    case DTREK_SHIELD_FRONT:
        return (torpdir + 128) & 0xff;
    case DTREK_SHIELD_RIGHT:
        return (torpdir + 64) & 0xff;
    case DTREK_SHIELD_REAR:
        return torpdir;
    case DTREK_SHIELD_LEFT:
        return (torpdir - 64) & 0xff;
    default:
        return ship->course;
    }
}

static void robot_adjust_turn_speed(DtrekShip *ship, int worrytime,
                                    uint8_t direction)
{
    int timeturn;

    if (ship->desired_speed <= 0) return;
    if (ship->spec.s_turns == 0 || ship->engine_health == 0) {
        ship->desired_speed = 0;
        return;
    }

    timeturn = (int)((double)(uint8_t)(ship->course - direction)
        * 1000.0 / ship->spec.s_turns * 256.0 / ship->engine_health);
    while (ship->desired_speed > 0) {
        if ((timeturn << ship->desired_speed) < worrytime) break;
        --ship->desired_speed;
    }
}

static int robot_avoid_torps(DtrekEngine *engine, int slot,
                             DtrekShip *ship, int target_beam_ready)
{
    int worrytime = 1;
    int tracking = 0;
    int closest_slot = -1;
    uint8_t av_dir = 0;
    int damage;

    /*
     * The evade fuse is shared with hazard and starbase avoidance
     * (EVADE_STARBASE), exactly as in rmove.c.  That case has no torpedo
     * slot, so hold the avoidance course without dereferencing one.
     */
    if (ship->robot_evasion_fuse > 0) {
        --ship->robot_evasion_fuse;
        if (ship->robot_evasion_fuse > 0) {
            if (ship->robot_evasion_type == DTREK_EVADE_STARBASE) {
                if (ship->fuel < ship->fuel_max / 20) {
                    ship->desired_speed = 3;
                }
                else if (ship->desired_speed < 4) {
                    ship->desired_speed = 4;
                }

                ship->robot_state = DTREK_ROBOT_EVADE;
                return 1;
            }

            if (ship->robot_evade_torpedo >= 0
                && ship->robot_evade_torpedo < DTREK_MAX_TORPEDOES) {
                const DtrekTorpedo *torp =
                    &engine->torpedoes[ship->robot_evade_torpedo];

                if (ship->fuel < ship->fuel_max / 20) {
                    ship->desired_speed = 3;
                }
                else if (ship->desired_speed < 4) {
                    ship->desired_speed = 4;
                }

                if (robot_can_beam(ship)
                    && !target_beam_ready
                    && torp->in_use
                    && torp->damage > ship->spec.s_beamdamage / 2
                    && torp->status != DTREK_TORP_FREE
                    && torp->status != DTREK_TORP_EXPLODE
                    && torp->weapon_class != DTREK_WPN_CHAFF) {
                    int dist = dtrek_ihypot(ship->x - torp->x,
                        ship->y - torp->y);

                    if (dist > DTREK_ROBOT_BEAM_MIN_RANGE
                        && dist < DTREK_ROBOT_BEAM_MAX_RANGE) {
                        if (robot_fire_defensive_beam(engine, slot, ship,
                                ship->robot_evade_torpedo)) {
                            ship->robot_evasion_fuse = 0;
                        }
                    }
                }

                ship->robot_state = DTREK_ROBOT_EVADE;
                return 1;
            }
        }
    }

    damage = robot_project_damage(engine, ship, &av_dir, &tracking,
        &closest_slot, &worrytime);
    if (damage <= 0) {
        ship->robot_evasion_type = DTREK_EVADE_NONE;
        ship->robot_evasion_fuse = 0;
        return 0;
    }

    {
        const DtrekTorpedo *torp = &engine->torpedoes[closest_slot];
        int shield_str = ship->shield[
            robot_shield_hit((uint8_t)torp->course,
                (uint8_t)ship->course)];

        ship->robot_evade_torpedo = closest_slot;

        if (robot_can_beam(ship)
            && !target_beam_ready
            && torp->weapon_class != DTREK_WPN_CHAFF) {
            int dist = dtrek_ihypot(ship->x - torp->x,
                ship->y - torp->y);

            if (dist > DTREK_ROBOT_BEAM_MIN_RANGE
                && dist < DTREK_ROBOT_BEAM_MAX_RANGE
                && robot_fire_defensive_beam(engine, slot, ship,
                    closest_slot)) {
                damage -= ship->spec.s_beamdamage;
            }
        }

        if (damage > ship->robot_offense / 2 + shield_str - 100) {
            ship->robot_evasion_fuse = worrytime / 2 + 2;

            if (tracking) {
                if (!robot_is_orbiter(ship)) {
                    int maxspeed = ship->spec.s_maxspeed
                        * ship->engine_health / 256;
                    int fleespeed;

                    if (worrytime < 20) fleespeed = ship->speed;
                    else if (worrytime < 40) {
                        fleespeed = (ship->speed + maxspeed) / 2;
                    }
                    else fleespeed = maxspeed;

                    if (worrytime < 50
                        && (uint8_t)(torp->course - ship->course + 128)
                            > 64) {
                        fleespeed >>= 1;
                    }
                    if (fleespeed < 0) fleespeed = 0;

                    if (ship->fuel > 1000
                        && robot_time_track_will_hit(
                            (double)ship->x / DTREK_WARP1,
                            (double)ship->y / DTREK_WARP1,
                            (double)fleespeed
                                * dtrek_cos_table[av_dir],
                            (double)fleespeed
                                * dtrek_sin_table[av_dir],
                            (double)torp->x / DTREK_WARP1,
                            (double)torp->y / DTREK_WARP1,
                            torp->speed, torp->fuse) < 0) {
                        ship->desired_course = av_dir;
                        ship->desired_speed = maxspeed;
                        ship->robot_evasion_type = DTREK_EVADE_AWAY;
                    }
                    else {
                        int shield_ok;

                        ship->desired_course = (uint8_t)
                            robot_good_shield_dir(ship, av_dir,
                                &shield_ok);
                        if (!shield_ok && ship->desired_speed > 0) {
                            robot_adjust_turn_speed(ship, worrytime,
                                (uint8_t)ship->desired_course);
                        }
                        ship->robot_evasion_fuse = 4;
                        ship->robot_evasion_type = DTREK_EVADE_SHIELD;
                    }
                }
            }
            else if (!robot_is_orbiter(ship)) {
                if (ship->speed < 4 && worrytime < 30) {
                    int shield_ok;

                    ship->desired_course = (uint8_t)
                        robot_good_shield_dir(ship, av_dir, &shield_ok);
                    if (!shield_ok && ship->desired_speed > 0) {
                        robot_adjust_turn_speed(ship, worrytime,
                            (uint8_t)ship->desired_course);
                    }
                    ship->robot_evasion_fuse = 4;
                    ship->robot_evasion_type = DTREK_EVADE_SHIELD;
                }
                else {
                    if (dtrek_rng_below(&engine->robot_rng, 2) != 0) {
                        ship->desired_course =
                            (ship->desired_course + 64) & 0xff;
                    }
                    else {
                        ship->desired_course =
                            (ship->desired_course - 64) & 0xff;
                    }
                    ship->robot_evasion_type = DTREK_EVADE_AWAY;
                    ship->robot_evasion_fuse = 5;
                    ship->desired_speed = 4;
                }
            }

            ship->robot_state = DTREK_ROBOT_EVADE;
            return 1;
        }

        /* Light damage: a small course kick is enough. */
        if (ship->robot_evasion_type == DTREK_EVADE_NONE) {
            int kick = dtrek_rng_below(&engine->robot_rng, 2) != 0
                ? 20 : -20;

            ship->desired_course =
                (uint8_t)(ship->desired_course + kick);
            ship->robot_course_deviation += kick;
            ship->robot_evasion_fuse = 0;
            ship->robot_evasion_type = DTREK_EVADE_SIMPLE;
            ship->robot_state = DTREK_ROBOT_EVADE;
            return 1;
        }

        ship->robot_state = DTREK_ROBOT_EVADE;
        return 1;
    }
}

static int robot_run_away(const DtrekEngine *engine, DtrekShip *ship,
                          const RobotTarget *nearest)
{
    int enemy_near = 0;
    int planet_near = 0;

    if (robot_is_orbiter(ship)) return 0;
    if (nearest->slot < 0) return 0;

    if (nearest->dist < 500 * DTREK_WARP1 && robot_needs_repair(ship)) {
        enemy_near = 1;
    }

    if (ship->nearest_planet_slot >= 0
        && ship->nearest_planet_dist < DTREK_ORBIT_DISTANCE * 2) {
        const DtrekPlanet *planet =
            robot_planet_at(engine, ship->nearest_planet_slot);

        if (planet != NULL
            && planet->owner != ship->team
            && !(planet->flags & (DTREK_PLMASK_BLACKHOLE
                | DTREK_PLMASK_BARREN | DTREK_PLMASK_NEBULA))) {
            planet_near = 1;
        }
    }

    if (!enemy_near && !planet_near) return 0;

    if (ship->fuel < ship->fuel_max / 5) ship->desired_speed = 4;
    else ship->desired_speed = 8;

    ship->repair_mode = 0;
    ship->shields_up = 1;
    ship->desired_course = (nearest->course + 128) & 0xff;
    return 1;
}

static int robot_repair(DtrekEngine *engine, int slot, DtrekShip *ship)
{
    int need_repair = robot_needs_repair(ship);
    int need_fuel = robot_needs_fuel(ship);
    int required_flags = 0;
    int repair_now = 0;

    if (!need_repair && !need_fuel) {
        if (ship->repair_mode) {
            ship->repair_mode = 0;
            ship->shields_up = 1;
        }
        if (ship->robot_patrol_type == DTREK_PATROL_SEEK_REPAIR) {
            ship->robot_state = DTREK_ROBOT_SEEK_REPAIR;
            return 1;
        }
        return 0;
    }

    if (need_repair) required_flags |= DTREK_PLMASK_REPAIR;
    if (need_fuel) required_flags |= DTREK_PLMASK_FUEL;

    if (robot_is_orbiter(ship)) {
        repair_now = 1;
    }
    else if (ship->orbiting
        && ship->planet_slot >= 0
        && ship->planet_slot < DTREK_MAX_PLANETS
        && engine->planets[ship->planet_slot].in_use
        && (engine->planets[ship->planet_slot].flags
            & required_flags) == required_flags) {
        repair_now = 1;
    }
    else if (ship->spec.s_maxspeed * ship->engine_health / 256 >= 2
        && ship->fuel > 1000) {
        int distance = 0;
        int planet_slot = robot_nearest_friendly_planet(engine, ship,
            required_flags, &distance);

        if (planet_slot >= 0 && distance < DTREK_MAX_REPAIR_TRIP) {
            const DtrekPlanet *planet = &engine->planets[planet_slot];

            ship->robot_patrol_planet = planet_slot;
            ship->desired_course = dtrek_course_to(ship->x, ship->y,
                planet->x, planet->y);
            ship->desired_speed = 4;
            ship->robot_patrol_fuse = distance / (5 * DTREK_WARP1);
            ship->robot_patrol_type = DTREK_PATROL_SEEK_REPAIR;
            ship->robot_state = DTREK_ROBOT_SEEK_REPAIR;
            return 1;
        }

        repair_now = 1;
    }
    else {
        repair_now = 1;
    }

    if (repair_now) {
        if (ship->robot_patrol_type == DTREK_PATROL_SEEK_REPAIR) {
            ship->robot_patrol_fuse = 0;
            ship->robot_patrol_type = DTREK_PATROL_NONE;
        }

        ship->desired_speed = 0;
        if (ship->speed == 0) {
            ship->repair_mode = 1;
            ship->shields_up = 0;
            ship->robot_patrol_fuse = 20;
            ship->robot_patrol_type = DTREK_PATROL_REPAIRING;
            ship->robot_state = DTREK_ROBOT_REPAIR;
        }
        return 1;
    }

    if (ship->repair_mode) {
        ship->repair_mode = 0;
        ship->shields_up = 1;
    }

    if (ship->robot_patrol_type == DTREK_PATROL_SEEK_REPAIR) {
        ship->robot_state = DTREK_ROBOT_SEEK_REPAIR;
        return 1;
    }

    (void)slot;
    return 0;
}

static int robot_attack(DtrekEngine *engine, int slot, DtrekShip *ship,
                        const RobotTarget *target)
{
    const DtrekShip *other = &engine->ships[target->slot];
    int attackhim = 0;

    if (target->slot < 0 || target->is_starbase) return 0;
    if (ship->fuel < ship->fuel_max / 15) return 0;

    if (target->intruder) {
        attackhim = 1;
    }
    else if (target->dist < ship->robot_offense * DTREK_WARP1) {
        attackhim = 1;
    }
    else if (target->dist
        < ship->robot_offense * (3 * DTREK_WARP1)) {
        int mine = (ship->shield[DTREK_SHIELD_FRONT]
            - ship->hull + 100) * ship->robot_offense / 150;
        int his = other->shield[DTREK_SHIELD_FRONT]
            - other->hull + 50
            + dtrek_rng_below(&engine->robot_rng, 100);

        if (mine > his) attackhim = 1;
    }

    if (!attackhim) return 0;

    if (!robot_is_orbiter(ship) && ship->orbiting) {
        dtrek_leave_orbit(engine, slot);
    }

    if (target->dist < other->spec.s_beamrange
        || target->dist < ship->spec.s_beamrange
        || target->dist < DTREK_GALAXY_WIDTH / 30) {
        ship->shields_up = 1;
    }
    else {
        ship->shields_up = 0;
    }

    if (target->dist < 500 * DTREK_WARP1
        && ship->torp_class == DTREK_WPN_CHAFF
        && ship->torps_unloaded != ship->torps_total) {
        int junk_course = 0;
        int junk_time = 0;

        robot_compute_torp_course(ship, other,
            ship->spec.s_torpduration, -1, &junk_course, &junk_time);
        ship->desired_course = (uint8_t)junk_course;
    }
    else {
        if (target->dist > ship->robot_too_far_range) {
            ship->desired_course = target->course;
        }
        else if (target->dist > ship->robot_too_close_range) {
            ship->desired_course =
                (uint8_t)(target->course + ship->robot_circle_dir);
            if (dtrek_rng_below(&engine->robot_rng,
                    DTREK_CIRCLE_FUSE) == 0) {
                ship->robot_circle_dir = -ship->robot_circle_dir;
            }
        }
        else {
            ship->desired_course = (target->course + 128) & 0xff;
        }

        if (target->dist > DTREK_GALAXY_WIDTH / 10
            && ship->nearest_planet_slot >= 0
            && ship->nearest_planet_dist > DTREK_ORBIT_DISTANCE * 2) {
            ship->shields_up = 0;
        }
        else {
            ship->shields_up = 1;
        }

        if (dtrek_rng_below(&engine->robot_rng, 10) == 0) {
            ship->robot_course_deviation =
                dtrek_rng_below(&engine->robot_rng, 20)
                + dtrek_rng_below(&engine->robot_rng, 20) - 20;
        }
        ship->desired_course = (uint8_t)
            (ship->desired_course + ship->robot_course_deviation);
    }

    if ((uint8_t)(ship->desired_course - ship->course) > 64) {
        ship->desired_speed = 4;
    }
    else if (ship->fuel < ship->fuel_max / 5) {
        ship->desired_speed = 4;
    }
    else if (target->dist > DTREK_WARP1 * 1000) {
        ship->desired_speed = 4;
    }
    else {
        ship->desired_speed = ship->robot_attack_speed;
    }

    ship->robot_patrol_type = DTREK_PATROL_ENEMY;
    ship->robot_patrol_fuse = 6;
    ship->robot_state = DTREK_ROBOT_ATTACK;
    return 1;
}

static int robot_valid_patrol_planet(const DtrekEngine *engine,
                                     const DtrekShip *ship)
{
    const DtrekPlanet *planet;

    if (ship->robot_patrol_planet < 0) return 0;
    planet = robot_planet_at(engine, ship->robot_patrol_planet);
    if (planet == NULL) return 0;
    return planet->owner == ship->team;
}

static int robot_patrol(DtrekEngine *engine, int slot, DtrekShip *ship)
{
    int point_x;
    int point_y;
    int distance;
    int planet_slot = -1;
    int best = INT_MAX;

    ship->robot_evasion_fuse = 0;
    ship->robot_evasion_type = DTREK_EVADE_NONE;
    ship->robot_state = DTREK_ROBOT_PATROL;

    if (robot_is_orbiter(ship)) {
        ship->robot_patrol_fuse = 120;
        ship->robot_patrol_type = DTREK_PATROL_RANDOM;
        ship->desired_speed = 0;
        return 1;
    }

    if (ship->orbiting) dtrek_leave_orbit(engine, slot);

    if (ship->fuel < ship->fuel_max / 3) ship->desired_speed = 3;
    else ship->desired_speed = 4;

    if (ship->team != DTREK_TEAM_INDEPENDENT) {
        int other_slot;

        for (other_slot = 0; other_slot < DTREK_MAX_PLAYERS;
             ++other_slot) {
            const DtrekShip *other = &engine->ships[other_slot];
            int near_planet;
            int near_distance;
            int my_distance;

            if (!robot_alive(other)) continue;
            if (other == ship) continue;
            if (!(ship->hostile_mask & other->team_mask)) continue;

            near_planet = robot_nearest_planet(engine, other->x,
                other->y, &near_distance);
            (void)near_distance;
            if (near_planet >= 0
                && engine->planets[near_planet].owner == ship->team) {
                my_distance = dtrek_ihypot(other->x - ship->x,
                    other->y - ship->y);
                if (my_distance < best) {
                    best = my_distance;
                    planet_slot = near_planet;
                    ship->robot_patrol_type = DTREK_PATROL_ENEMY;
                }
            }
            else if (planet_slot < 0) {
                int owned = robot_nearest_team_planet_to(engine, ship,
                    other->x, other->y, &near_distance);

                if (owned >= 0
                    && near_distance < DTREK_INTERPLANETARY_DIST * 3) {
                    my_distance = dtrek_ihypot(other->x - ship->x,
                        other->y - ship->y);
                    if (my_distance < best) {
                        best = my_distance;
                        planet_slot = owned;
                        ship->robot_patrol_type = DTREK_PATROL_ENEMY;
                    }
                }
            }
        }
    }

    if (planet_slot < 0) {
        if (ship->nearest_planet_slot == ship->robot_patrol_planet
            || !robot_valid_patrol_planet(engine, ship)) {
            planet_slot = robot_pick_team_planet(engine, ship);
            if (planet_slot >= 0) {
                ship->robot_patrol_type = DTREK_PATROL_RANDOM;
            }
        }
        else {
            planet_slot = ship->robot_patrol_planet;
        }
    }

    if (planet_slot >= 0) {
        const DtrekPlanet *planet = &engine->planets[planet_slot];

        point_x = planet->x - (DTREK_GALAXY_WIDTH / 40)
            + dtrek_rng_below(&engine->robot_rng,
                DTREK_GALAXY_WIDTH / 20);
        point_y = planet->y - (DTREK_GALAXY_WIDTH / 40)
            + dtrek_rng_below(&engine->robot_rng,
                DTREK_GALAXY_WIDTH / 20);
        ship->robot_patrol_planet = planet_slot;
        if (ship->robot_patrol_type != DTREK_PATROL_ENEMY) {
            ship->robot_patrol_type = DTREK_PATROL_RANDOM;
        }
    }
    else {
        point_x = (DTREK_GALAXY_WIDTH / 3)
            + dtrek_rng_below(&engine->robot_rng,
                DTREK_GALAXY_WIDTH / 3);
        point_y = (DTREK_GALAXY_WIDTH / 3)
            + dtrek_rng_below(&engine->robot_rng,
                DTREK_GALAXY_WIDTH / 3);
        ship->robot_patrol_type = DTREK_PATROL_RANDOM;
    }

    if (point_x < 0) point_x = 0;
    if (point_x > DTREK_GALAXY_WIDTH) point_x = DTREK_GALAXY_WIDTH;
    if (point_y < 0) point_y = 0;
    if (point_y > DTREK_GALAXY_WIDTH) point_y = DTREK_GALAXY_WIDTH;

    ship->desired_course = dtrek_course_to(ship->x, ship->y,
        point_x, point_y);
    distance = dtrek_ihypot(ship->x - point_x, ship->y - point_y);
    ship->robot_patrol_fuse = distance / ship->desired_speed
        / (DTREK_WARP1 * 10) + 8;
    if (ship->robot_patrol_fuse > 30) ship->robot_patrol_fuse = 30;

    return 1;
}

/* Original rmove.c:avoid_object. */
static void robot_avoid_object(DtrekShip *ship, int dist,
                               int runaway_dist, int avoid_dist,
                               int object_x, int object_y)
{
    uint8_t toward = dtrek_course_to(ship->x, ship->y,
        object_x, object_y);
    uint8_t dir;

    if (dist < runaway_dist) {
        ship->desired_course = (toward + 128) & 0xff;
        ship->desired_speed = ship->spec.s_maxspeed;
        ship->robot_evasion_fuse = 8;
        ship->robot_evasion_type = DTREK_EVADE_STARBASE;
        ship->robot_state = DTREK_ROBOT_EVADE;
    }
    else if (dist < avoid_dist) {
        dir = (uint8_t)(ship->desired_course - toward);

        if (dir <= DTREK_ROBOT_DIR_RIGHT) {
            ship->desired_course = (toward + 64) & 0xff;
            if (ship->desired_speed < 4) ship->desired_speed = 4;
            ship->robot_evasion_fuse = 4;
            ship->robot_evasion_type = DTREK_EVADE_STARBASE;
            ship->robot_state = DTREK_ROBOT_EVADE;
        }
        else if (dir >= DTREK_ROBOT_DIR_LEFT) {
            ship->desired_course = (toward - 64) & 0xff;
            if (ship->desired_speed < 4) ship->desired_speed = 4;
            ship->robot_evasion_fuse = 4;
            ship->robot_evasion_type = DTREK_EVADE_STARBASE;
            ship->robot_state = DTREK_ROBOT_EVADE;
        }
        else {
            if (ship->desired_speed < 4) ship->desired_speed = 4;
        }
    }
}

/* Original rmove.c:check_near_edge. */
static void robot_check_near_edge(DtrekShip *ship)
{
    if (ship->x < DTREK_BOUNDRY_DIST) {
        if (ship->y < DTREK_BOUNDRY_DIST) {
            if (ship->desired_course >= DTREK_ROBOT_DIR_DOWN_RIGHT
                && ship->desired_course < DTREK_ROBOT_DIR_UP_LEFT) {
                ship->desired_course = DTREK_ROBOT_DIR_DOWN;
            }
            else {
                ship->desired_course = DTREK_ROBOT_DIR_RIGHT;
            }
        }
        else if (ship->y > DTREK_GALAXY_WIDTH - DTREK_BOUNDRY_DIST) {
            if (ship->desired_course >= DTREK_ROBOT_DIR_UP_RIGHT
                && ship->desired_course < DTREK_ROBOT_DIR_DOWN_LEFT) {
                ship->desired_course = DTREK_ROBOT_DIR_RIGHT;
            }
            else {
                ship->desired_course = DTREK_ROBOT_DIR_UP;
            }
        }
        else {
            if (ship->desired_course >= DTREK_ROBOT_DIR_RIGHT
                && ship->desired_course < DTREK_ROBOT_DIR_LEFT) {
                ship->desired_course = DTREK_ROBOT_DIR_DOWN;
            }
            else {
                ship->desired_course = DTREK_ROBOT_DIR_UP;
            }
        }
    }
    else if (ship->x > DTREK_GALAXY_WIDTH - DTREK_BOUNDRY_DIST) {
        if (ship->y < DTREK_BOUNDRY_DIST) {
            if (ship->desired_course >= DTREK_ROBOT_DIR_UP_RIGHT
                && ship->desired_course < DTREK_ROBOT_DIR_DOWN_LEFT) {
                ship->desired_course = DTREK_ROBOT_DIR_DOWN;
            }
            else {
                ship->desired_course = DTREK_ROBOT_DIR_LEFT;
            }
        }
        else if (ship->y > DTREK_GALAXY_WIDTH - DTREK_BOUNDRY_DIST) {
            if (ship->desired_course >= DTREK_ROBOT_DIR_DOWN_RIGHT
                && ship->desired_course < DTREK_ROBOT_DIR_UP_LEFT) {
                ship->desired_course = DTREK_ROBOT_DIR_LEFT;
            }
            else {
                ship->desired_course = DTREK_ROBOT_DIR_UP;
            }
        }
        else {
            if (ship->desired_course >= DTREK_ROBOT_DIR_RIGHT
                && ship->desired_course < DTREK_ROBOT_DIR_LEFT) {
                ship->desired_course = DTREK_ROBOT_DIR_DOWN;
            }
            else {
                ship->desired_course = DTREK_ROBOT_DIR_UP;
            }
        }
    }
    else if (ship->y < DTREK_BOUNDRY_DIST
        || ship->y > DTREK_GALAXY_WIDTH - DTREK_BOUNDRY_DIST) {
        if (ship->desired_course < DTREK_ROBOT_DIR_DOWN) {
            ship->desired_course = DTREK_ROBOT_DIR_RIGHT;
        }
        else {
            ship->desired_course = DTREK_ROBOT_DIR_LEFT;
        }
    }

    if (ship->desired_speed < 4) ship->desired_speed = 4;
}

static void robot_check_course(DtrekEngine *engine, int slot,
                               DtrekShip *ship)
{
    if (!robot_is_orbiter(ship)) {
        const DtrekPlanet *planet;

        if (ship->nearest_planet_slot >= 0
            && ship->nearest_planet_dist <= DTREK_ORBIT_DISTANCE * 2) {
            ship->shields_up = 1;
        }

        planet = robot_planet_at(engine, ship->nearest_planet_slot);
        if (planet != NULL
            && (planet->flags
                & (DTREK_PLMASK_BLACKHOLE | DTREK_PLMASK_PULSAR))) {
            robot_avoid_object(ship, ship->nearest_planet_dist,
                DTREK_INTERPLANETARY_DIST,
                DTREK_INTERPLANETARY_DIST * 3 / 2,
                planet->x, planet->y);
        }
        else if (planet != NULL
            && (planet->flags & DTREK_PLMASK_NEBULA)
            && ship->robot_offense < 90
            && (ship->torp_class == DTREK_WPN_PLASMA
                || ship->torp_class == DTREK_WPN_MISSILE)) {
            robot_avoid_object(ship, ship->nearest_planet_dist,
                DTREK_NEBULA_RADIUS, DTREK_NEBULA_RADIUS * 2,
                planet->x, planet->y);
        }

        robot_check_near_edge(ship);
    }
    else {
        const DtrekPlanet *planet;

        if (robot_is_starbase(ship)) return;

        planet = robot_planet_at(engine, ship->robot_patrol_planet);
        if (planet == NULL) planet =
            robot_planet_at(engine, ship->nearest_planet_slot);

        if (planet != NULL && planet->owner == ship->team) {
            int distance = dtrek_ihypot(ship->x - planet->x,
                ship->y - planet->y);

            if (!ship->orbiting) {
                if (distance <= DTREK_ENTER_ORBIT_DISTANCE
                    && ship->speed <= DTREK_ORBIT_SPEED) {
                    dtrek_orbit(engine, slot,
                        ship->robot_patrol_planet);
                }
                else {
                    ship->desired_course = dtrek_course_to(ship->x,
                        ship->y, planet->x, planet->y);
                    if (ship->speed > DTREK_ORBIT_SPEED) {
                        ship->desired_speed = 0;
                    }
                    else if (distance > 5000) {
                        ship->desired_speed = 2;
                    }
                    else {
                        ship->desired_speed = 1;
                    }
                }
            }
        }
    }
}

static int robot_resolve_target(DtrekEngine *engine, DtrekShip *ship,
                                RobotTarget *offensive)
{
    int last = ship->robot_target;
    int last_distance;

    if (offensive->slot < 0) {
        ship->robot_target = -1;
        return -1;
    }

    if (last < 0 || last >= DTREK_MAX_PLAYERS) {
        ship->robot_target = offensive->slot;
        return offensive->slot;
    }

    if (!robot_alive(&engine->ships[last])
        || last == (int)(ship - engine->ships)
        || !(ship->hostile_mask & engine->ships[last].team_mask)
        || robot_is_starbase(&engine->ships[last])) {
        ship->robot_target = offensive->slot;
        return offensive->slot;
    }

    if (last == offensive->slot) return last;

    last_distance = dtrek_ihypot(engine->ships[last].x - ship->x,
        engine->ships[last].y - ship->y);
    if (last_distance * ship->robot_impatience / 100
        < offensive->dist) {
        robot_fill_target(engine, ship, last, offensive);
        return last;
    }

    ship->robot_target = offensive->slot;
    return offensive->slot;
}

int dtrek_robot_config_valid(const DtrekRobotConfig *config)
{
    int flags;

    if (config == NULL) return 1;

    flags = config->flags;
    if (flags & ~(DTREK_ROBOT_ORBITER | DTREK_ROBOT_STARBASE)) return 0;

    if (config->offense < 0 || config->offense > 100) return 0;
    if (config->impatience < 0 || config->impatience > 100) return 0;
    if (config->attack_speed < 0
        || config->attack_speed > DTREK_MAX_SPEED) {
        return 0;
    }
    if (config->patrol_planet < -1
        || config->patrol_planet >= DTREK_MAX_PLANETS) {
        return 0;
    }

    return 1;
}

int dtrek_robot_configure(DtrekEngine *engine, int ship_slot,
                          const DtrekRobotConfig *config)
{
    DtrekShip *ship;
    DtrekOrdnanceSpec spec;
    int team;
    int flags;
    int offense = 0;
    int impatience = 0;
    int attack_speed = 0;
    int patrol_planet = -1;
    int has_ordnance = 0;

    if (engine == NULL) return DTREK_ERR_ARGUMENT;
    if (ship_slot < 0 || ship_slot >= DTREK_MAX_PLAYERS) {
        return DTREK_ERR_SHIP;
    }
    if (!engine->ships[ship_slot].in_use) return DTREK_ERR_SHIP;
    if (!dtrek_robot_config_valid(config)) return DTREK_ERR_ARGUMENT;

    ship = &engine->ships[ship_slot];
    team = ship->team;

    if (config != NULL) {
        flags = config->flags;
        offense = config->offense;
        impatience = config->impatience;
        attack_speed = config->attack_speed;
        patrol_planet = config->patrol_planet;
    }
    else {
        flags = DTREK_ROBOT_NONE;
    }

    if (flags & DTREK_ROBOT_STARBASE) flags |= DTREK_ROBOT_ORBITER;

    if (dtrek_ordnance_spec(engine, ship->torp_class, &spec) == DTREK_OK) {
        has_ordnance = 1;
    }

    if (offense == 0) {
        if (flags & DTREK_ROBOT_ORBITER) {
            offense = 100;
        }
        else {
            offense = 70 + 10
                * dtrek_rng_below(&engine->robot_rng, 3);
        }
    }
    if (offense > 100) offense = 100;
    if (offense < 10) offense = 10;

    if (impatience == 0) {
        if (flags & DTREK_ROBOT_ORBITER) {
            impatience = 100;
        }
        else {
            impatience = 20 + dtrek_rng_below(&engine->robot_rng, 80);
            if (has_ordnance) {
                if (ship->torp_class == DTREK_WPN_PLASMA) {
                    impatience -= 30;
                }
                else if (ship->torp_class == DTREK_WPN_MISSILE) {
                    impatience += 40;
                }
            }
            if (team == DTREK_TEAM_INDEPENDENT) impatience += 30;
        }
    }
    if (impatience > 100) impatience = 100;
    if (impatience < 50) impatience = 50;

    if (attack_speed == 0) {
        switch (team) {
        case DTREK_TEAM_INDEPENDENT:
            attack_speed = 2;
            break;
        case DTREK_TEAM_FEDERATION:
            attack_speed = 7;
            break;
        case DTREK_TEAM_ROMULAN:
            attack_speed = 5;
            break;
        case DTREK_TEAM_KLINGON:
            attack_speed = 4;
            break;
        case DTREK_TEAM_ORION:
            attack_speed = 9;
            break;
        default:
            attack_speed = 4;
            break;
        }
    }

    if (patrol_planet < 0) {
        int distance = 0;

        if (flags & DTREK_ROBOT_ORBITER) {
            patrol_planet = robot_nearest_team_planet(engine, ship,
                0, &distance);
            if (patrol_planet < 0) {
                patrol_planet = robot_nearest_friendly_planet(engine,
                    ship, 0, &distance);
            }
        }
    }
    else if (!robot_planet_at(engine, patrol_planet)) {
        patrol_planet = -1;
    }

    ship->is_robot = 1;
    ship->robot_offense = offense;
    ship->robot_impatience = impatience;
    ship->robot_attack_speed = attack_speed;
    ship->robot_flags = flags;
    ship->robot_state = DTREK_ROBOT_IDLE;
    ship->robot_target = -1;
    ship->robot_patrol_planet = patrol_planet;
    ship->robot_patrol_fuse = 0;
    ship->robot_patrol_type = DTREK_PATROL_NONE;
    ship->robot_evasion_fuse = 0;
    ship->robot_evasion_type = DTREK_EVADE_NONE;
    ship->robot_evade_torpedo = -1;
    ship->robot_circle_dir = 64;
    ship->robot_course_deviation = 0;
    ship->robot_too_close_range = 0;
    ship->robot_can_fire_beam = 0;
    ship->robot_last_beam_tick = -1;
    ship->robot_last_beam_target = -1;
    ship->robot_last_beam_hit = 0;
    ship->robot_last_torpedo_tick = -1;
    ship->robot_last_torpedo_slot = -1;
    ship->robot_last_torpedo_target = -1;

    ship->robot_too_far_range = DTREK_GALAXY_WIDTH / 10;
    if (has_ordnance) {
        switch (ship->torp_class) {
        case DTREK_WPN_PHOTON:
            ship->robot_too_far_range = spec.speed * spec.duration
                * DTREK_WARP1 * impatience / 300;
            break;
        case DTREK_WPN_PLASMA:
            ship->robot_too_far_range = spec.speed
                * (spec.damage / spec.decay)
                * DTREK_WARP1 * impatience / 150;
            break;
        case DTREK_WPN_MISSILE:
            ship->robot_too_far_range = spec.speed * spec.duration
                * DTREK_WARP1 * impatience / 100;
            break;
        case DTREK_WPN_CHAFF:
            ship->robot_too_far_range = DTREK_GALAXY_WIDTH / 100;
            break;
        case DTREK_WPN_MINE:
            ship->robot_too_far_range = DTREK_MINE_RANGE;
            break;
        default:
            ship->robot_too_far_range = DTREK_GALAXY_WIDTH / 10;
            break;
        }
    }

    if (ship->beams_total > 0) {
        int range = ship->spec.s_beamrange * impatience / 100;

        if (range < ship->robot_too_far_range) {
            ship->robot_too_far_range = range;
        }
    }

    if (flags & DTREK_ROBOT_ORBITER) {
        ship->desired_speed = 0;
        if (patrol_planet >= 0) {
            const DtrekPlanet *planet = &engine->planets[patrol_planet];
            int distance = dtrek_ihypot(ship->x - planet->x,
                ship->y - planet->y);

            ship->desired_course = dtrek_course_to(ship->x, ship->y,
                planet->x, planet->y);
            if (distance <= DTREK_ENTER_ORBIT_DISTANCE
                && ship->speed <= DTREK_ORBIT_SPEED) {
                dtrek_orbit(engine, ship_slot, patrol_planet);
            }
        }
    }

    return DTREK_OK;
}

int32_t DTREK_CALL dtrek_spawn_robot(dtrek_engine *engine, const char *name,
                                     int32_t team, int32_t x, int32_t y,
                                     const DtrekRobotConfig *config)
{
    int slot;
    int rc;

    if (engine == NULL) return DTREK_ERR_ARGUMENT;
    if (!dtrek_robot_config_valid(config)) return DTREK_ERR_ARGUMENT;

    slot = dtrek_spawn_training_ship(engine, name, (int)team,
        (int)x, (int)y);
    if (slot < 0) return slot;

    rc = dtrek_robot_configure(engine, slot, config);
    if (rc != DTREK_OK) {
        memset(&engine->ships[slot], 0, sizeof(engine->ships[slot]));
        engine->ship_count--;
        return rc;
    }

    return slot;
}

int32_t DTREK_CALL dtrek_set_robot(dtrek_engine *engine, int32_t ship_slot,
                                   const DtrekRobotConfig *config)
{
    if (engine == NULL) return DTREK_ERR_ARGUMENT;
    if (ship_slot < 0 || ship_slot >= DTREK_MAX_PLAYERS) {
        return DTREK_ERR_SHIP;
    }
    if (!engine->ships[ship_slot].in_use) return DTREK_ERR_SHIP;
    if (!engine->ships[ship_slot].is_robot) return DTREK_ERR_STATE;
    return dtrek_robot_configure(engine, (int)ship_slot, config);
}

int32_t DTREK_CALL dtrek_clear_robot(dtrek_engine *engine, int32_t ship_slot)
{
    int i;

    if (engine == NULL) return DTREK_ERR_ARGUMENT;
    if (ship_slot < 0 || ship_slot >= DTREK_MAX_PLAYERS) {
        return DTREK_ERR_SHIP;
    }
    if (!engine->ships[ship_slot].in_use) return DTREK_ERR_SHIP;
    if (!engine->ships[ship_slot].is_robot) return DTREK_ERR_STATE;

    for (i = 0; i < DTREK_MAX_TORPEDOES; ++i) {
        if (engine->torpedoes[i].in_use
            && engine->torpedoes[i].owner_slot == ship_slot) {
            memset(&engine->torpedoes[i], 0,
                sizeof(engine->torpedoes[i]));
        }
    }

    memset(&engine->ships[ship_slot], 0, sizeof(engine->ships[ship_slot]));
    if (engine->ship_count > 0) engine->ship_count--;
    return DTREK_OK;
}

void dtrek_robot_step(DtrekEngine *engine, int ship_slot)
{
    DtrekShip *ship;
    RobotTarget defensive;
    RobotTarget offensive;
    int has_defensive;
    int has_offensive;
    int moved;

    if (engine == NULL) return;
    if (ship_slot < 0 || ship_slot >= DTREK_MAX_PLAYERS) return;

    ship = &engine->ships[ship_slot];
    if (!ship->in_use || !ship->is_robot) return;
    if (!robot_alive(ship)) return;

    has_defensive = robot_find_nearest(engine, ship, 0, &defensive);
    has_offensive = robot_find_nearest(engine, ship, 1, &offensive);
    if (!has_offensive) offensive.slot = -1;

    if (robot_resolve_target(engine, ship, &offensive) < 0) {
        moved = 0;
        if (has_defensive) {
            if (robot_avoid_torps(engine, ship_slot, ship, 0)) {
                moved = 1;
            }
            else if (robot_run_away(engine, ship, &defensive)) {
                moved = 1;
            }
        }

        if (!moved && robot_repair(engine, ship_slot, ship)) {
            moved = 1;
        }

        if (!moved) robot_patrol(engine, ship_slot, ship);

        robot_check_course(engine, ship_slot, ship);
        if (robot_is_starbase(ship)) ship->desired_speed = 0;
        return;
    }

    robot_analyze_target(engine, ship, &offensive);

    if (!robot_is_orbiter(ship) && ship->orbiting) {
        dtrek_leave_orbit(engine, ship_slot);
    }

    if (robot_is_orbiter(ship)) {
        robot_fire_weapons(engine, ship_slot, ship, &offensive);
        if (!robot_avoid_torps(engine, ship_slot, ship,
                offensive.beam_ready && robot_can_beam(ship))) {
            if (!robot_repair(engine, ship_slot, ship)) {
                robot_patrol(engine, ship_slot, ship);
            }
        }
    }
    else if (ship->robot_offense > 70) {
        robot_fire_weapons(engine, ship_slot, ship, &offensive);
        if (!robot_avoid_torps(engine, ship_slot, ship,
                offensive.beam_ready && robot_can_beam(ship))) {
            if (!robot_run_away(engine, ship, &defensive)) {
                if (!robot_repair(engine, ship_slot, ship)) {
                    if (!robot_attack(engine, ship_slot, ship,
                            &offensive)) {
                        robot_patrol(engine, ship_slot, ship);
                    }
                }
            }
        }
    }
    else if (ship->robot_offense > 30) {
        moved = robot_avoid_torps(engine, ship_slot, ship,
            offensive.beam_ready && robot_can_beam(ship));
        robot_fire_weapons(engine, ship_slot, ship, &offensive);
        if (!moved) {
            if (!robot_run_away(engine, ship, &defensive)) {
                if (!robot_repair(engine, ship_slot, ship)) {
                    if (!robot_attack(engine, ship_slot, ship,
                            &offensive)) {
                        robot_patrol(engine, ship_slot, ship);
                    }
                }
            }
        }
    }
    else {
        moved = robot_avoid_torps(engine, ship_slot, ship,
            offensive.beam_ready && robot_can_beam(ship));
        robot_fire_weapons(engine, ship_slot, ship, &offensive);
        if (!moved) {
            if (!robot_run_away(engine, ship, &defensive)) {
                if (!robot_repair(engine, ship_slot, ship)) {
                    robot_patrol(engine, ship_slot, ship);
                }
            }
        }
    }

    robot_check_course(engine, ship_slot, ship);
    if (robot_is_starbase(ship)) ship->desired_speed = 0;
}
