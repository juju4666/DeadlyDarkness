//
// Begotten: drivable vehicles (a motorcycle and a mecha). Licensed under the GNU GPL v2 or later.
// See bgvehicle.h. All arithmetic is integer, so a demo or a network game runs the same on every machine.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomstat.h"
#include "d_player.h"
#include "d_event.h"
#include "info.h"
#include "m_fixed.h"
#include "m_random.h"
#include "p_inter.h"
#include "p_map.h"
#include "p_maputl.h"
#include "p_mobj.h"
#include "p_pspr.h"
#include "p_setup.h"
#include "p_tick.h"
#include "p_spec.h"
#include "r_main.h"
#include "s_sound.h"
#include "sounds.h"
#include "smooth.h"
#include "tables.h"
#include "dsda/excmd.h"
#include "dsda/messenger.h"
#include "dsda/bgvoxel.h"
#include "dsda/bgvehicle.h"

extern fixed_t bulletslope;
void P_BulletSlope(mobj_t* mo);

enum { KIND_NONE, KIND_MOTO, KIND_MECHA, KIND_TANK, KIND_TURRET, KIND_COUNT };

typedef struct
{
  const char* name;
  int seat;               // eye height above the floor the vehicle stands on, map units
  fixed_t maxfwd;         // top speed forwards, units per tic
  fixed_t maxback;        // ... backwards
  fixed_t maxside;        // ... sideways (the mecha strafes, the bike does not)
  fixed_t accel;          // motorcycle: acceleration per tic at full throttle; mecha: share of the gap closed per tic
  fixed_t brake;          // motorcycle: braking per tic
  fixed_t roll;           // speed kept per tic with no throttle
  fixed_t grip;           // share of the sideways speed kept per tic (the bike drifts; the mecha does not slide)
  int turnslow, turnfast; // most the heading can turn in a tic, degrees, standing still / at top speed
  int ramspeed;           // speed (units per tic) from which it hurts what it runs into
  int ramdamage;          // damage at top speed
} kind_t;

#define F(a, b) ((fixed_t) ((a) * FRACUNIT + (b) * FRACUNIT / 100))

static const kind_t kinds[KIND_COUNT] =
{
  { "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  // motorcycle: fast, loose, turns wide at speed
  { "MOTORCYCLE", 26, F(30, 0), F(6, 0), 0, F(0, 85), F(3, 0), ((fixed_t) (986 * FRACUNIT / 1000)), F(0, 80), 5, 2, 12, 70 },
  // mecha: slow, heavy, strafes, stops almost at once, turns slowly
  { "MECHA", 84, F(9, 50), F(6, 0), F(6, 50), F(0, 28), F(0, 0), F(0, 0), F(0, 0), 4, 4, 5, 25 },
  // tank hull (the driver's seat): see tank_drive. Wheels style: heavy car (accel, brake, roll, grip, turn); the
  // maxside of 0 only means "no strafing"
  { "TANK", 46, F(7, 50), F(3, 50), 0, F(0, 20), F(0, 45), ((fixed_t) (985 * FRACUNIT / 1000)), F(0, 86), 1, 2, 3, 60 },
  // tank turret (the gunner's seat): see tank_gun
  { "TANK GUNNER", 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};

#define TANK_DECK 22       // height of the turret ring above the hull's floor, map units
#define TANK_DRIVER 38     // the driver's eye is this far ahead of the hull's centre
#define TANK_LOOK 12743    // furthest he can look away from straight ahead (70 degrees, in angleturn units)

static const kind_t* kind_of(const mobj_t* v)
{
  int k = bgvox_VehicleKind(v);

  return (k > KIND_NONE && k < KIND_COUNT) ? &kinds[k] : NULL;
}

static dboolean is_live(const mobj_t* m)
{
  return m && m->thinker.function == P_MobjThinker;
}

static const kind_t* const K_TANK = &kinds[KIND_TANK];
static const kind_t* const K_TURRET = &kinds[KIND_TURRET];

// the body of a vehicle seat: a tank's turret belongs to its hull; everything else is its own body
static mobj_t* body_of(mobj_t* v)
{
  mobj_t* h;

  if (!v || kind_of(v) != K_TURRET)
    return v;
  h = v->target;
  return (is_live(h) && kind_of(h) == K_TANK) ? h : NULL;
}

static mobj_t* turret_of(const mobj_t* hull)
{
  mobj_t* t = hull->lastenemy;

  return (is_live(t) && kind_of(t) == K_TURRET && t->target == hull) ? t : NULL;
}

mobj_t* bgveh_VehicleOf(const mobj_t* rider)
{
  mobj_t* v;

  if (!rider || !rider->player || !rider->tracer)
    return NULL;
  v = rider->tracer;
  if (!is_live(v) || v->tracer != rider || !kind_of(v))
    return NULL;
  return v;
}

dboolean bgveh_Mounted(const player_t* player)
{
  return player && player->mo && bgveh_VehicleOf(player->mo) != NULL;
}

dboolean bgveh_OwnVehicle(const mobj_t* shooter, const mobj_t* vehicle)
{
  const mobj_t* sv;

  if (!shooter || !vehicle || !shooter->player || !shooter->tracer)
    return false;
  sv = shooter->tracer;
  if (!is_live(sv) || sv->tracer != shooter || !kind_of(sv))
    return false;
  if (sv == vehicle)
    return true;
  // the two halves of a tank are one vehicle
  if (kind_of(sv) == K_TANK && vehicle == sv->lastenemy)
    return true;
  if (kind_of(sv) == K_TURRET && vehicle == sv->target)
    return true;
  return false;
}

// the vehicle is not drawn in its rider's view (the gunner sees the hull but not the turret he sits in; the driver
// sees the whole tank around him)
dboolean bgveh_HideFromView(const mobj_t* thing, const player_t* viewer)
{
  return viewer && viewer->mo && bgveh_OwnVehicle(viewer->mo, thing) && kind_of(thing) != K_TANK;
}

mobj_t* bgveh_Redirect(mobj_t* target)
{
  mobj_t* v = bgveh_VehicleOf(target);
  mobj_t* b = body_of(v);

  return b ? b : v ? v : target;
}

// ---------------------------------------------------------------- getting in and out

static player_t* use_player;
static mobj_t* use_best;
static fixed_t use_dist;

static dboolean PIT_FindVehicle(mobj_t* t)
{
  fixed_t d;

  if (!kind_of(t) || kind_of(t) == K_TURRET || t->health <= 0 || !is_live(t))
    return true;
  if (kind_of(t) == K_TANK)
  {
    mobj_t* tu = turret_of(t);

    if (t->tracer && (!tu || tu->tracer))
      return true;                     // both seats are taken
  }
  else if (t->tracer)
    return true;
  d = P_AproxDistance(t->x - use_player->mo->x, t->y - use_player->mo->y) - t->radius;
  if (d < 0)
    d = 0;
  if (d > 56 * FRACUNIT)
    return true;
  if (use_player->mo->z > t->z + t->height || use_player->mo->z + 40 * FRACUNIT < t->z)
    return true;
  if (!use_best || d < use_dist)
  {
    use_best = t;
    use_dist = d;
  }
  return true;
}

// where the rider of a seat sits: the driver of a tank is ahead of the hull's centre, everybody else in the middle
static void seat_pos(mobj_t* seat, const mobj_t* body, fixed_t* x, fixed_t* y)
{
  *x = body->x;
  *y = body->y;
  if (kind_of(seat) == K_TANK)
  {
    *x += TANK_DRIVER * finecosine[body->angle >> ANGLETOFINESHIFT];
    *y += TANK_DRIVER * finesine[body->angle >> ANGLETOFINESHIFT];
  }
}

static void put_rider(mobj_t* rider, mobj_t* seat)
{
  mobj_t* body = body_of(seat);
  fixed_t x, y;

  if (!body)
    body = seat;
  seat_pos(seat, body, &x, &y);
  P_UnsetThingPosition(rider);
  rider->x = x;
  rider->y = y;
  rider->z = body->z;
  P_SetThingPosition(rider);
  rider->floorz = body->floorz;
  rider->ceilingz = body->ceilingz;
}

// the seat of a tank to take: the front half of it is the driver's, the back half the gunner's; when that one is
// taken, the other
static mobj_t* pick_seat(const player_t* player, mobj_t* body)
{
  mobj_t* tu;
  fixed_t fwd;
  dboolean driver_free, gunner_free;

  if (kind_of(body) != K_TANK)
    return body;
  tu = turret_of(body);
  fwd = FixedMul(player->mo->x - body->x, finecosine[body->angle >> ANGLETOFINESHIFT]) +
        FixedMul(player->mo->y - body->y, finesine[body->angle >> ANGLETOFINESHIFT]);
  driver_free = !body->tracer;
  gunner_free = tu && !tu->tracer;
  if (fwd >= 0 && driver_free)
    return body;
  if (fwd < 0 && gunner_free)
    return tu;
  return driver_free ? body : tu;
}

static void mount(player_t* player, mobj_t* v)
{
  mobj_t* rider = player->mo;
  const kind_t* k = kind_of(v);
  mobj_t* body = body_of(v);

  if (!body)
    body = v;
  P_SetTarget(&rider->tracer, v);
  P_SetTarget(&v->tracer, rider);
  rider->angle = v->angle;
  rider->flags &= ~(MF_SOLID | MF_SHOOTABLE);
  rider->flags |= MF_NOGRAVITY;
  rider->flags2 |= MF2_DONTDRAW;           // he is inside the vehicle
  // the driver of a tank sits ahead of its centre: a small body, so a wall near the tank does not keep him from following
  rider->radius = k == K_TANK ? 8 * FRACUNIT : body->radius;
  rider->height = body->height;
  rider->momx = rider->momy = rider->momz = 0;
  player->momx = player->momy = 0;
  put_rider(rider, v);
  player->viewheight = k->seat * FRACUNIT;
  player->deltaviewheight = 0;
  S_StartMobjSound(v, sfx_swtchn);
  {
    char msg[96];

    if (k == K_TANK)
      snprintf(msg, sizeof msg, "You drive the tank. The style key switches wheels and tracks; Use gets out.");
    else if (k == K_TURRET)
      snprintf(msg, sizeof msg, "You man the turret: fire is the cannon, alt fire the machine gun. Use gets out.");
    else
      snprintf(msg, sizeof msg, "You get on the %s. Use gets off.", k->name);
    dsda_AddPlayerMessage(msg, player);
  }
}

static void dismount(player_t* player, dboolean destroyed)
{
  mobj_t* rider = player->mo;
  mobj_t* seat = bgveh_VehicleOf(rider);
  mobj_t* v = body_of(seat);              // where he lands
  int i;
  dboolean placed = false;
  fixed_t dist;

  rider->flags |= MF_SOLID | MF_SHOOTABLE;
  rider->flags &= ~MF_NOGRAVITY;
  rider->flags2 &= ~MF2_DONTDRAW;
  rider->radius = rider->info->radius;
  rider->height = rider->info->height;
  P_SetTarget(&rider->tracer, NULL);
  if (!seat)
    return;
  P_SetTarget(&seat->tracer, NULL);
  if (kind_of(seat) == K_TANK)
  {
    seat->special1.i = seat->special2.i = 0;      // the tracks stop pulling; the look goes back to the front
    seat->bg_aux = 0;
  }
  if (!v)
    v = seat;
  dist = v->radius + rider->radius + 4 * FRACUNIT;
  for (i = 0; i < 8 && !placed; ++i)
  {
    // the left side first, then the right, behind and in front
    static const int order[8] = { 2, 6, 4, 0, 3, 5, 1, 7 };
    angle_t a = v->angle + (angle_t) order[i] * ANG45;
    fixed_t x = v->x + FixedMul(dist, finecosine[a >> ANGLETOFINESHIFT]);
    fixed_t y = v->y + FixedMul(dist, finesine[a >> ANGLETOFINESHIFT]);

    rider->z = v->z;
    placed = P_TryMove(rider, x, y, false);
  }
  if (!placed)
    put_rider(rider, seat);
  rider->momx = v->momx / 2;
  rider->momy = v->momy / 2;
  if (destroyed)
  {
    rider->momz = 5 * FRACUNIT;
    P_DamageMobj(rider, NULL, NULL, 15 + P_Random(pr_misc) % 15);
  }
  S_StartMobjSound(rider, sfx_swtchn);
}

dboolean bgveh_Use(player_t* player)
{
  int xl, xh, yl, yh, x, y;
  fixed_t reach = 128 * FRACUNIT;

  if (!player->mo || player->playerstate != PST_LIVE)
    return false;
  if (bgveh_VehicleOf(player->mo))
  {
    dismount(player, false);
    return true;
  }
  use_player = player;
  use_best = NULL;
  yh = P_GetSafeBlockY(player->mo->y + reach - bmaporgy);
  yl = P_GetSafeBlockY(player->mo->y - reach - bmaporgy);
  xh = P_GetSafeBlockX(player->mo->x + reach - bmaporgx);
  xl = P_GetSafeBlockX(player->mo->x - reach - bmaporgx);
  for (y = yl; y <= yh; ++y)
    for (x = xl; x <= xh; ++x)
      P_BlockThingsIterator(x, y, PIT_FindVehicle);
  if (getenv("BEGOTTEN_VEHDEBUG"))
    fprintf(stderr, "VEH use: player at %d %d z %d angle %u, vehicle %s\n", player->mo->x >> 16, player->mo->y >> 16,
            player->mo->z >> 16, (unsigned) player->mo->angle, use_best ? "found" : "none near");
  if (!use_best)
    return false;
  mount(player, pick_seat(player, use_best));
  return true;
}

// ---------------------------------------------------------------- driving

static int iclamp(int v, int lo, int hi)
{
  return v < lo ? lo : v > hi ? hi : v;
}

static fixed_t fclamp(fixed_t v, fixed_t lo, fixed_t hi)
{
  return v < lo ? lo : v > hi ? hi : v;
}

static fixed_t fabsx(fixed_t v)
{
  return v < 0 ? -v : v;
}

static mobj_t* ram_v;
static mobj_t* ram_rider;
static fixed_t ram_speed, ram_fc, ram_fs;
static int ram_damage;
static dboolean ram_hit;

static dboolean PIT_Ram(mobj_t* t)
{
  fixed_t dx, dy, reach;

  if (t == ram_v || t == ram_rider || !(t->flags & MF_SHOOTABLE) || t->health <= 0 || t->player || kind_of(t))
    return true;
  dx = t->x - ram_v->x;
  dy = t->y - ram_v->y;
  reach = ram_v->radius + t->radius + 4 * FRACUNIT + ram_speed;     // what it will cover this tic
  if (P_AproxDistance(dx, dy) > reach)
    return true;
  if (FixedMul(dx, ram_fc) + FixedMul(dy, ram_fs) <= 0)
    return true;
  if (ram_v->z >= t->z + t->height || t->z >= ram_v->z + ram_v->height)
    return true;
  if (getenv("BEGOTTEN_VEHDEBUG"))
    fprintf(stderr, "VEH ram: type %d hp %d damage %d speed %.1f\n", (int) t->type, t->health, ram_damage, ram_speed / 65536.0);
  P_DamageMobj(t, ram_v, ram_rider, ram_damage);
  // thrown ahead of the vehicle
  t->momx += ram_fc * (ram_speed >> FRACBITS) / 4;
  t->momy += ram_fs * (ram_speed >> FRACBITS) / 4;
  ram_hit = true;
  return true;
}

static void ram(mobj_t* v, mobj_t* rider, const kind_t* k, fixed_t speed, fixed_t fc, fixed_t fs)
{
  int xl, xh, yl, yh, x, y;
  fixed_t reach = v->radius + 64 * FRACUNIT + speed;

  if (speed < k->ramspeed * FRACUNIT)
    return;
  ram_v = v;
  ram_rider = rider;
  ram_speed = speed;
  ram_fc = fc;
  ram_fs = fs;
  ram_damage = k->ramdamage * (speed >> 8) / (k->maxfwd >> 8);
  if (ram_damage < 1)
    ram_damage = 1;
  ram_hit = false;
  yh = P_GetSafeBlockY(v->y + reach - bmaporgy);
  yl = P_GetSafeBlockY(v->y - reach - bmaporgy);
  xh = P_GetSafeBlockX(v->x + reach - bmaporgx);
  xl = P_GetSafeBlockX(v->x - reach - bmaporgx);
  for (y = yl; y <= yh; ++y)
    for (x = xl; x <= xh; ++x)
      P_BlockThingsIterator(x, y, PIT_Ram);
  if (ram_hit)
  {
    v->momx = FixedMul(v->momx, F(0, 60));
    v->momy = FixedMul(v->momy, F(0, 60));
    S_StartMobjSound(v, sfx_oof);
  }
}

// the mecha's weapons: a cannon on the primary fire, a rocket on the alt fire
static void mecha_fire(player_t* player, mobj_t* v, mobj_t* rider)
{
  const ticcmd_t* cmd = &player->cmd;
  dboolean alt = (cmd->ex.actions & XC_ALT) != 0;

  if (v->special1.i > 0)
    --v->special1.i;
  if (v->special2.i > 0)
    --v->special2.i;
  if (alt && v->special2.i == 0)
  {
    fixed_t oldz = rider->z;

    v->special2.i = 32;
    rider->z = v->z + 52 * FRACUNIT;
    P_SpawnPlayerMissile(rider, MT_ROCKET);
    rider->z = oldz;
    S_StartMobjSound(rider, sfx_rlaunc);
    if (getenv("BEGOTTEN_VEHDEBUG"))
      fprintf(stderr, "VEH rocket fired\n");
  }
  else if (!alt && (cmd->buttons & BT_ATTACK) && v->special1.i == 0)
  {
    int damage = 10 + 5 * (P_Random(pr_gunshot) % 3);
    int t = P_Random(pr_misfire);
    angle_t angle = rider->angle + ((t - P_Random(pr_misfire)) << 17);
    fixed_t oldz = rider->z;

    v->special1.i = 3;
    rider->z = v->z + 52 * FRACUNIT;
    P_BulletSlope(rider);
    P_LineAttack(rider, angle, MISSILERANGE, bulletslope, damage);
    rider->z = oldz;
    S_StartMobjSound(rider, sfx_chgun);
    if (getenv("BEGOTTEN_VEHDEBUG"))
      fprintf(stderr, "VEH cannon fired slope %d\n", bulletslope);
  }
}


// ---------------------------------------------------------------- the tank

//
// Two seats. The hull is the vehicle the driver sits in; the turret is a second, bodiless thing that turns on top
// of it and carries the gunner. The hull finds its turret in `lastenemy` (its `threshold` stays above 0, which keeps
// the damage code from using that field), the turret finds its hull in `target`. Everything else lives in the hull:
//   special1 / special2   speed of the left / right track (tracks style), in fixed point map units per tic
//   bg_aux                how far the driver has looked away from straight ahead (wheels style), angleturn units
//   pursuecount           the control style, 0 wheels / 1 tracks
//   movecount, strafecount   tics until the cannon / the machine gun can fire again
//

#define TANK_TRACKMAX F(7, 0)       // a track's speed at full power, units per tic
#define TANK_TRACKBASE 300          // distance between the tracks in map units: the heading turns by the speed difference over it
#define TANK_CANNON 50              // tics between two cannon shots
#define BAM_PER_RADIAN 683565276LL  // 2^32 / (2 pi)

dboolean bgveh_IsTankDriver(const player_t* player)
{
  mobj_t* v = player && player->mo ? bgveh_VehicleOf(player->mo) : NULL;

  return v && kind_of(v) == K_TANK;
}

dboolean bgveh_WantsTracks(const player_t* player)
{
  return bgveh_IsTankDriver(player) && bgveh_VehicleOf(player->mo)->pursuecount == 1;
}

// one track's speed moves toward what the driver asks for: slowly when it builds up, faster when it lets go,
// fastest when the track has to change direction (that is what makes a tank turn on the spot)
static fixed_t track_step(fixed_t t, fixed_t want)
{
  fixed_t d = want - t, a;

  if (want == 0)
    a = F(0, 22);
  else if (t == 0 || (t > 0) == (want > 0))
    a = fabsx(want) > fabsx(t) ? F(0, 35) : F(0, 60);
  else
    a = F(1, 20);
  return d > a ? t + a : d < -a ? t - a : want;
}

static void tank_fire(player_t* player, mobj_t* hull, mobj_t* rider)
{
  const ticcmd_t* cmd = &player->cmd;
  dboolean alt = (cmd->ex.actions & XC_ALT) != 0;
  fixed_t ox = rider->x, oy = rider->y, oz = rider->z;
  fixed_t fc = finecosine[rider->angle >> ANGLETOFINESHIFT], fs = finesine[rider->angle >> ANGLETOFINESHIFT];
  mobj_t* tu = turret_of(hull);

  if (!tu || !(cmd->buttons & BT_ATTACK))
    return;
  // from the muzzle, not from the seat
  rider->x = hull->x + 56 * fc;
  rider->y = hull->y + 56 * fs;
  if (alt)
  {
    if (hull->strafecount == 0)
    {
      int damage = 8 + 4 * (P_Random(pr_gunshot) % 3);
      int t = P_Random(pr_misfire);
      angle_t angle = rider->angle + ((t - P_Random(pr_misfire)) << 17);

      hull->strafecount = 3;
      rider->z = hull->z + 10 * FRACUNIT;
      P_BulletSlope(rider);
      P_LineAttack(rider, angle, MISSILERANGE, bulletslope, damage);
      S_StartMobjSound(tu, sfx_chgun);
    }
  }
  else if (hull->movecount == 0)
  {
    hull->movecount = TANK_CANNON;
    rider->z = hull->z + 2 * FRACUNIT;
    P_SpawnPlayerMissile(rider, MT_ROCKET);
    S_StartMobjSound(tu, sfx_rlaunc);
    // the recoil
    hull->momx -= fc / 2;
    hull->momy -= fs / 2;
    if (getenv("BEGOTTEN_VEHDEBUG"))
      fprintf(stderr, "VEH cannon fired\n");
  }
  rider->x = ox;
  rider->y = oy;
  rider->z = oz;
}

static void tank_drive(player_t* player, mobj_t* hull, mobj_t* rider)
{
  const kind_t* k = K_TANK;
  const ticcmd_t* cmd = &player->cmd;
  mobj_t* tu = turret_of(hull);
  dboolean ground = hull->z <= hull->floorz;
  dboolean tracks;
  fixed_t vx = hull->momx, vy = hull->momy, vf, vl, fc, fs, speed;
  long long turn64 = 0;
  int dturn, look, newlook, fwd, steer;
  angle_t oldview;

  if ((cmd->ex.actions & XC_EXT) && (cmd->ex.actions2 & XC2_STYLE))
  {
    hull->pursuecount ^= 1;
    hull->special1.i = hull->special2.i = 0;
    hull->bg_aux = 0;
    dsda_AddPlayerMessage(hull->pursuecount ? "Tank: tracks" : "Tank: wheels", player);
  }
  tracks = hull->pursuecount == 1;
  if (ground && hull->friction > 0)
  {
    vx = FixedDiv(vx, hull->friction);
    vy = FixedDiv(vy, hull->friction);
  }
  oldview = rider->angle;
  look = hull->bg_aux;

  if (tracks)
  {
    int l = 0, r = 0;
    fixed_t tl, tr;

    if ((cmd->ex.actions & XC_EXT) && (cmd->ex.actions2 & XC2_TRACKS))
    {
      l = iclamp(cmd->ex.track_l, -127, 127);
      r = iclamp(cmd->ex.track_r, -127, 127);
    }
    tl = hull->special1.i = track_step(hull->special1.i, FixedMul(TANK_TRACKMAX, l * FRACUNIT / 127));
    tr = hull->special2.i = track_step(hull->special2.i, FixedMul(TANK_TRACKMAX, r * FRACUNIT / 127));
    // the left track faster than the right one turns the tank to the right
    if (ground)
      turn64 = -((long long) (tl - tr) * BAM_PER_RADIAN) / ((long long) TANK_TRACKBASE * FRACUNIT);
    newlook = 0;                     // the view stays straight ahead
  }
  else
  {
    // wheels: it steers like a car, so it needs to be moving to turn; the mouse and the turn keys look around
    fixed_t along = FixedMul(vx, finecosine[hull->angle >> ANGLETOFINESHIFT]) +
                    FixedMul(vy, finesine[hull->angle >> ANGLETOFINESHIFT]);
    fixed_t sfac = F(0, 25) + FixedMul(F(0, 75), FixedDiv(fclamp(fabsx(along), 0, 3 * FRACUNIT), 3 * FRACUNIT));

    steer = iclamp(cmd->sidemove, -50, 50);
    if (ground)
    {
      turn64 = -((long long) steer * (ANG1 * 18 / 10) / 50) * sfac / FRACUNIT;
      if (along < 0)
        turn64 = -turn64;
    }
    newlook = iclamp(look + cmd->angleturn, -TANK_LOOK, TANK_LOOK);
  }
  hull->bg_aux = (short) newlook;
  dturn = (int) turn64;
  hull->angle += (angle_t) dturn;

  fc = finecosine[hull->angle >> ANGLETOFINESHIFT];
  fs = finesine[hull->angle >> ANGLETOFINESHIFT];
  vf = FixedMul(vx, fc) + FixedMul(vy, fs);        // along the heading
  vl = FixedMul(vx, fs) - FixedMul(vy, fc);        // to the right of it
  fwd = iclamp(cmd->forwardmove, -50, 50);

  if (ground)
  {
    if (tracks)
    {
      // the tracks pull the hull to their mean speed; the hull's own sideways speed dies away slowly, so a tank that
      // turns at speed slides on in its old direction for a moment
      fixed_t want = (hull->special1.i + hull->special2.i) / 2;

      vf += FixedMul(want - vf, F(0, 30));
      vl = FixedMul(vl, F(0, 88));
    }
    else
    {
      if (fwd > 0)
        vf = fclamp(vf + FixedMul(k->accel, fwd * FRACUNIT / 50), -k->maxback, k->maxfwd);
      else if (fwd < 0)
      {
        if (vf > 0)
          vf = vf > FixedMul(k->brake, -fwd * FRACUNIT / 50) ? vf - FixedMul(k->brake, -fwd * FRACUNIT / 50) : 0;
        else
          vf = fclamp(vf + FixedMul(k->accel * 6 / 10, fwd * FRACUNIT / 50), -k->maxback, k->maxfwd);
      }
      else
        vf = FixedMul(vf, k->roll);
      vl = FixedMul(vl, k->grip);
    }
    vf = fclamp(vf, -k->maxback, k->maxfwd);
  }
  vx = FixedMul(vf, fc) + FixedMul(vl, fs);
  vy = FixedMul(vf, fs) - FixedMul(vl, fc);
  hull->momx = vx;
  hull->momy = vy;
  speed = P_AproxDistance(vx, vy);
  if (ground)
    ram(hull, rider, k, speed, fc, fs);

  // the driver's view; the turret follows his eyes when nobody is in it
  rider->angle = hull->angle + ((angle_t) newlook << 16);
  if (tu && !tu->tracer)
    tu->angle = rider->angle;
  if (player == &players[displayplayer] && (dturn || newlook != look))
    R_SmoothPlaying_Add((int) (rider->angle - oldview));
  // and the weapons are his only when there is no gunner
  if (tu && !tu->tracer)
    tank_fire(player, hull, rider);
}

static void tank_gun(player_t* player, mobj_t* tu, mobj_t* rider)
{
  mobj_t* hull = body_of(tu);
  angle_t turn = (angle_t) ((int) player->cmd.angleturn << 16);

  tu->angle += turn;
  rider->angle = tu->angle;
  if (player == &players[displayplayer] && turn)
    R_SmoothPlaying_Add((int) turn);
  if (hull)
    tank_fire(player, hull, rider);
}

// the hull's thinker (its idle state): make the turret, keep it on the hull, count the guns down
void bgveh_A_TankThink(mobj_t* hull)
{
  mobj_t* tu = turret_of(hull);

  if (!tu)
  {
    int type = bgvox_TypeOfKind(KIND_TURRET);

    if (type < 0)
      return;
    tu = P_SpawnMobj(hull->x, hull->y, hull->z + TANK_DECK * FRACUNIT, type);
    P_SetTarget(&tu->target, hull);
    P_SetTarget(&hull->lastenemy, tu);
    tu->angle = hull->angle;
    hull->threshold = 1;                // a thing with a threshold is not made to remember an enemy
  }
  if (tu->x != hull->x || tu->y != hull->y)
  {
    P_UnsetThingPosition(tu);
    tu->x = hull->x;
    tu->y = hull->y;
    P_SetThingPosition(tu);
  }
  tu->z = hull->z + TANK_DECK * FRACUNIT;
  tu->floorz = hull->floorz;
  tu->ceilingz = hull->ceilingz;
  if (hull->movecount > 0)
    --hull->movecount;
  if (hull->strafecount > 0)
    --hull->strafecount;
}

// the wreck keeps no turret
void bgveh_A_TankWreck(mobj_t* mo)
{
  mobj_t* tu = turret_of(mo);

  if (tu)
  {
    P_SetTarget(&mo->lastenemy, NULL);
    P_RemoveMobj(tu);
  }
  mo->flags &= ~(MF_CORPSE | MF_DROPOFF);
  mo->flags |= MF_SOLID;
  if (mo->info)
    mo->height = mo->info->height;
}

dboolean bgveh_Control(player_t* player)
{
  mobj_t* rider = player->mo;
  mobj_t* v = bgveh_VehicleOf(rider);
  const kind_t* k;
  const ticcmd_t* cmd = &player->cmd;
  fixed_t vx, vy, vf, vl, fc, fs, speed, topspeed;
  int fwd, side, dturn, maxturn;
  long long turn64;
  dboolean ground;

  if (!v)
    return false;
  k = kind_of(v);

  rider->flags &= ~(MF_SOLID | MF_SHOOTABLE);
  rider->flags |= MF_NOGRAVITY;
  rider->flags2 |= MF2_DONTDRAW;
  rider->momx = rider->momy = rider->momz = 0;
  player->momx = player->momy = 0;

  if (k == K_TANK || k == K_TURRET)
  {
    if (k == K_TANK)
      tank_drive(player, v, rider);
    else
      tank_gun(player, v, rider);
    player->attackdown = 0;
    player->pendingweapon = wp_nochange;
    return true;
  }

  ground = v->z <= v->floorz;
  // the vehicle's speed: the engine multiplies a thing's momentum by the floor friction after every move
  vx = v->momx;
  vy = v->momy;
  if (ground && v->friction > 0)
  {
    vx = FixedDiv(vx, v->friction);
    vy = FixedDiv(vy, v->friction);
  }

  // the motorcycle is hurt by hitting something hard: the speed it was given last tic against the speed it kept
  if (k->maxside == 0 && ground)
  {
    fixed_t lost = v->special1.i - P_AproxDistance(vx, vy);

    if (lost > 9 * FRACUNIT)
    {
      P_DamageMobj(v, NULL, NULL, (lost >> FRACBITS) * 2);
      S_StartMobjSound(v, sfx_oof);
    }
  }

  fwd = iclamp(cmd->forwardmove, -50, 50);
  side = iclamp(cmd->sidemove, -50, 50);

  // turning: the mouse and the turn keys, and the side keys on the motorcycle
  turn64 = (long long) (int) ((unsigned) cmd->angleturn << 16);
  if (k->maxside == 0)
    turn64 -= (long long) side * (3 * ANG1) / 50;
  speed = P_AproxDistance(vx, vy);
  topspeed = k->maxfwd;
  maxturn = (int) ((long long) k->turnslow * ANG1 +
                   ((long long) (k->turnfast - k->turnslow) * ANG1 * fclamp(speed, 0, topspeed)) / topspeed);
  if (k->maxside == 0 && speed < FRACUNIT)
    maxturn /= 3;                         // a motorcycle that is not moving cannot swing round
  dturn = (int) (turn64 < -maxturn ? -maxturn : turn64 > maxturn ? maxturn : turn64);
  v->angle += (angle_t) dturn;
  rider->angle = v->angle;
  if (player == &players[displayplayer] && dturn)
    R_SmoothPlaying_Add(dturn);

  fc = finecosine[v->angle >> ANGLETOFINESHIFT];
  fs = finesine[v->angle >> ANGLETOFINESHIFT];
  vf = FixedMul(vx, fc) + FixedMul(vy, fs);        // along the heading
  vl = FixedMul(vx, fs) - FixedMul(vy, fc);        // to the right of it

  if (k->maxside == 0)
  {
    // motorcycle
    if (ground)
    {
      fixed_t lat = vl;

      if (fwd > 0)
        vf = fclamp(vf + FixedMul(k->accel, fwd * FRACUNIT / 50), -k->maxback, k->maxfwd);
      else if (fwd < 0)
      {
        if (vf > 0)
          vf = vf > k->brake ? vf - k->brake : 0;
        else
          vf = fclamp(vf + FixedMul(k->accel / 2, fwd * FRACUNIT / 50), -k->maxback, k->maxfwd);
      }
      else
        vf = FixedMul(vf, k->roll);
      // tyres: the sideways speed dies away (the handbrake, the jump key, lets the back end slide) and part of it
      // is turned into forward speed, so the bike carves a corner instead of just stopping
      vl = FixedMul(vl, (cmd->ex.actions & XC_JUMP) ? F(0, 96) : k->grip);
      if (vf != 0)
      {
        fixed_t kept = (fabsx(lat) - fabsx(vl)) * 55 / 100;

        vf += vf > 0 ? kept : -kept;
        vf = fclamp(vf, -k->maxback, k->maxfwd);
      }
    }
  }
  else
  {
    // mecha: it goes where the keys say, and stops when they are let go
    fixed_t wf = fwd >= 0 ? FixedMul(k->maxfwd, fwd * FRACUNIT / 50) : FixedMul(k->maxback, fwd * FRACUNIT / 50);
    fixed_t wl = FixedMul(k->maxside, side * FRACUNIT / 50);

    if (ground)
    {
      vf += FixedMul(wf - vf, k->accel);
      vl += FixedMul(wl - vl, k->accel);
    }
  }

  vx = FixedMul(vf, fc) + FixedMul(vl, fs);
  vy = FixedMul(vf, fs) - FixedMul(vl, fc);
  v->momx = vx;
  v->momy = vy;
  if (k->maxside == 0)
    v->special1.i = P_AproxDistance(vx, vy);

  if (ground)
    ram(v, rider, k, P_AproxDistance(vx, vy), fc, fs);
  if (kind_of(v) == &kinds[KIND_MECHA])
    mecha_fire(player, v, rider);

  // the weapon in the hands is out of the way
  player->attackdown = 0;
  player->pendingweapon = wp_nochange;
  return true;
}

// ---------------------------------------------------------------- after the things have moved

void bgveh_PostTick(void)
{
  int i;

  for (i = 0; i < g_maxplayers; ++i)
  {
    player_t* player = &players[i];
    mobj_t* rider;
    mobj_t* v;
    mobj_t* body;
    fixed_t sx, sy;

    if (!playeringame[i] || !player->mo)
      continue;
    rider = player->mo;
    v = bgveh_VehicleOf(rider);
    if (!v)
    {
      // a vehicle that went away under its rider
      if (rider->tracer && (rider->flags & MF_NOGRAVITY) && !(rider->flags & MF_SOLID) && player->playerstate == PST_LIVE)
      {
        P_SetTarget(&rider->tracer, NULL);
        dismount(player, false);
      }
      continue;
    }
    body = body_of(v);
    if (player->playerstate != PST_LIVE || rider->health <= 0)
    {
      dismount(player, false);
      continue;
    }
    if (!body || body->health <= 0)
    {
      dismount(player, true);
      continue;
    }
    // carry the rider; walking over a line, picking things up and sector effects happen to him as to any player
    seat_pos(v, body, &sx, &sy);
    rider->z = body->z;
    if (!P_TryMove(rider, sx, sy, false))
      put_rider(rider, v);
    rider->z = body->z;
    rider->floorz = body->floorz;
    rider->ceilingz = body->ceilingz;
    rider->momx = rider->momy = rider->momz = 0;
  }
}

dboolean bgveh_Camera(player_t* player)
{
  mobj_t* v = bgveh_VehicleOf(player->mo);
  const kind_t* k;

  if (!v)
    return false;
  k = kind_of(v);
  player->bob = 0;
  player->viewheight = k->seat * FRACUNIT;
  player->deltaviewheight = 0;
  player->viewz = player->mo->z + player->viewheight;
  if (player->viewz > player->mo->ceilingz - 4 * FRACUNIT)
    player->viewz = player->mo->ceilingz - 4 * FRACUNIT;
  return true;
}

// developer aid: BEGOTTEN_VEHTEST="20:use 30:fwd=50 90:turn=300 150:fwd=0 160:use" plays player 1's controls by tic (use and style
// are one tic; fwd, side, turn (angleturn), fire, alt, jump, tl and tr (the tank's tracks) stay until changed: fire=0 ...);
// BEGOTTEN_VEHTEST2 does the same for player 2 (with BEGOTTEN_LOCALPLAYERS=2). With BEGOTTEN_VEHDEBUG=1 it prints the
// player's and the vehicle's state every 10 tics
void bgveh_TestCmd(player_t* player)
{
  static struct
  {
    int init, ntok, next, fwd, side, turn, fire, alt, jump, tl, tr;
    struct { int tic; char what[16]; int val; } tok[256];
  } st[2];
  const char* e;
  int one_use = 0, one_style = 0, n = (int) (player - players);

  if (n < 0 || n > 1)
    return;
  if (!st[n].init)
  {
    char buf[2048], *c;

    st[n].init = 1;
    e = getenv(n ? "BEGOTTEN_VEHTEST2" : "BEGOTTEN_VEHTEST");
    if (e)
    {
      snprintf(buf, sizeof buf, "%s", e);
      for (c = strtok(buf, " ,"); c && st[n].ntok < 256; c = strtok(NULL, " ,"))
      {
        int t, v = 1;
        char w[16];

        if (sscanf(c, "%d:%15[a-z]=%d", &t, w, &v) >= 2 || sscanf(c, "%d:%15[a-z]", &t, w) == 2)
        {
          st[n].tok[st[n].ntok].tic = t;
          snprintf(st[n].tok[st[n].ntok].what, sizeof st[n].tok[0].what, "%s", w);
          st[n].tok[st[n].ntok].val = v;
          ++st[n].ntok;
        }
      }
    }
  }
  if (!st[n].ntok)
    return;
  while (!demoplayback && st[n].next < st[n].ntok && st[n].tok[st[n].next].tic <= leveltime)
  {
    const char* w = st[n].tok[st[n].next].what;
    int v = st[n].tok[st[n].next].val;

    if (!strcmp(w, "use")) one_use = 1;
    else if (!strcmp(w, "style")) one_style = 1;
    else if (!strcmp(w, "fwd")) st[n].fwd = v;
    else if (!strcmp(w, "side")) st[n].side = v;
    else if (!strcmp(w, "turn")) st[n].turn = v;
    else if (!strcmp(w, "fire")) st[n].fire = v;
    else if (!strcmp(w, "alt")) st[n].alt = v;
    else if (!strcmp(w, "jump")) st[n].jump = v;
    else if (!strcmp(w, "tl")) st[n].tl = v;
    else if (!strcmp(w, "tr")) st[n].tr = v;
    else if (!strcmp(w, "hurt")) P_DamageMobj(player->mo, NULL, NULL, v);       // a blow at the rider (lands on the vehicle)
    ++st[n].next;
  }
  if (demoplayback)
    goto debug;                  // a demo plays its own commands; only the printing is wanted
  player->cmd.forwardmove = (signed char) st[n].fwd;
  player->cmd.sidemove = (signed char) st[n].side;
  player->cmd.angleturn = (short) st[n].turn;
  if (st[n].fire) player->cmd.buttons |= BT_ATTACK;
  if (one_use) player->cmd.buttons |= BT_USE;
  else player->cmd.buttons &= ~BT_USE;
  player->cmd.ex.actions &= ~(XC_ALT | XC_JUMP | XC_EXT);
  player->cmd.ex.actions2 = 0;
  player->cmd.ex.track_l = player->cmd.ex.track_r = 0;
  if (st[n].alt) player->cmd.ex.actions |= XC_ALT;
  if (st[n].jump) player->cmd.ex.actions |= XC_JUMP;
  if (bgveh_WantsTracks(player) || st[n].tl || st[n].tr)
  {
    player->cmd.ex.actions |= XC_EXT;
    player->cmd.ex.actions2 |= XC2_TRACKS;
    player->cmd.ex.track_l = (signed char) st[n].tl;
    player->cmd.ex.track_r = (signed char) st[n].tr;
  }
  if (one_style)
  {
    player->cmd.ex.actions |= XC_EXT;
    player->cmd.ex.actions2 |= XC2_STYLE;
  }
debug:
  if (getenv("BEGOTTEN_VEHDEBUG") && leveltime % 10 == 0)
  {
    mobj_t* v = bgveh_VehicleOf(player->mo);

    fprintf(stderr, "VEH p%d t=%d rider %d,%d,%d ang %d mounted %d", n + 1, leveltime, player->mo->x >> 16,
            player->mo->y >> 16, player->mo->z >> 16, (int) (player->mo->angle / (ANG1 + 0.0)), v ? 1 : 0);
    if (v)
    {
      mobj_t* b = body_of(v);

      if (!b)
        b = v;
      fprintf(stderr, " | veh %d,%d ang %d vel %.1f,%.1f hp %d", b->x >> 16, b->y >> 16, (int) (b->angle / (ANG1 + 0.0)),
              b->momx / 65536.0, b->momy / 65536.0, b->health);
      if (kind_of(v) == K_TANK)
        fprintf(stderr, " tracks %.1f %.1f style %d", v->special1.i / 65536.0, v->special2.i / 65536.0, v->pursuecount);
      if (kind_of(v) == K_TURRET)
        fprintf(stderr, " turret ang %d", (int) (v->angle / (ANG1 + 0.0)));
    }
    fprintf(stderr, "\n");
  }
}

dboolean bgveh_StatusText(const player_t* player, char* buf, size_t len)
{
  mobj_t* v = player && player->mo ? bgveh_VehicleOf(player->mo) : NULL;
  const kind_t* k;
  fixed_t speed;

  if (!v)
    return false;
  k = kind_of(v);
  {
    mobj_t* b = body_of(v);

    if (!b)
      b = v;
    speed = P_AproxDistance(b->momx, b->momy);
    if (k == K_TANK)
      snprintf(buf, len, "TANK %d  SPEED %d  %s", b->health > 0 ? b->health : 0,
               (int) ((long long) speed * 394 / 100 >> FRACBITS), b->pursuecount ? "TRACKS" : "WHEELS");
    else if (k == K_TURRET)
      snprintf(buf, len, "TANK %d  %s", b->health > 0 ? b->health : 0, b->movecount ? "RELOADING" : "CANNON READY");
    else
      snprintf(buf, len, "%s %d  SPEED %d", k->name, v->health > 0 ? v->health : 0,
               (int) ((long long) speed * 394 / 100 >> FRACBITS));
  }
  return true;
}
