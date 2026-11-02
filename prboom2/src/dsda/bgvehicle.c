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

enum { KIND_NONE, KIND_MOTO, KIND_MECHA, KIND_COUNT };

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
};

static const kind_t* kind_of(const mobj_t* v)
{
  int k = bgvox_VehicleKind(v);

  return (k > KIND_NONE && k < KIND_COUNT) ? &kinds[k] : NULL;
}

static dboolean is_live(const mobj_t* m)
{
  return m && m->thinker.function == P_MobjThinker;
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
  return shooter && vehicle && shooter->tracer == vehicle && vehicle->tracer == shooter && shooter->player &&
         kind_of(vehicle);
}

dboolean bgveh_HideFromView(const mobj_t* thing, const player_t* viewer)
{
  return viewer && viewer->mo && bgveh_OwnVehicle(viewer->mo, thing);
}

mobj_t* bgveh_Redirect(mobj_t* target)
{
  mobj_t* v = bgveh_VehicleOf(target);

  return v ? v : target;
}

// ---------------------------------------------------------------- getting in and out

static player_t* use_player;
static mobj_t* use_best;
static fixed_t use_dist;

static dboolean PIT_FindVehicle(mobj_t* t)
{
  fixed_t d;

  if (!kind_of(t) || t->health <= 0 || t->tracer || !is_live(t))
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

static void put_rider(mobj_t* rider, const mobj_t* v)
{
  P_UnsetThingPosition(rider);
  rider->x = v->x;
  rider->y = v->y;
  rider->z = v->z;
  P_SetThingPosition(rider);
  rider->floorz = v->floorz;
  rider->ceilingz = v->ceilingz;
}

static void mount(player_t* player, mobj_t* v)
{
  mobj_t* rider = player->mo;
  const kind_t* k = kind_of(v);

  P_SetTarget(&rider->tracer, v);
  P_SetTarget(&v->tracer, rider);
  rider->angle = v->angle;
  rider->flags &= ~(MF_SOLID | MF_SHOOTABLE);
  rider->flags |= MF_NOGRAVITY;
  rider->flags2 |= MF2_DONTDRAW;           // he is inside the vehicle
  rider->radius = v->radius;
  rider->height = v->height;
  rider->momx = rider->momy = rider->momz = 0;
  player->momx = player->momy = 0;
  put_rider(rider, v);
  player->viewheight = k->seat * FRACUNIT;
  player->deltaviewheight = 0;
  S_StartMobjSound(v, sfx_swtchn);
  {
    char msg[96];

    snprintf(msg, sizeof msg, "You get on the %s. Use gets off.", k->name);
    dsda_AddPlayerMessage(msg, player);
  }
}

static void dismount(player_t* player, dboolean destroyed)
{
  mobj_t* rider = player->mo;
  mobj_t* v = bgveh_VehicleOf(rider);
  int i;
  dboolean placed = false;
  fixed_t dist;

  rider->flags |= MF_SOLID | MF_SHOOTABLE;
  rider->flags &= ~MF_NOGRAVITY;
  rider->flags2 &= ~MF2_DONTDRAW;
  rider->radius = rider->info->radius;
  rider->height = rider->info->height;
  P_SetTarget(&rider->tracer, NULL);
  if (!v)
    return;
  P_SetTarget(&v->tracer, NULL);
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
    put_rider(rider, v);
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
  mount(player, use_best);
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
    if (player->playerstate != PST_LIVE || rider->health <= 0)
    {
      dismount(player, false);
      continue;
    }
    if (v->health <= 0)
    {
      dismount(player, true);
      continue;
    }
    // carry the rider; walking over a line, picking things up and sector effects happen to him as to any player
    rider->z = v->z;
    if (!P_TryMove(rider, v->x, v->y, false))
      put_rider(rider, v);
    rider->z = v->z;
    rider->floorz = v->floorz;
    rider->ceilingz = v->ceilingz;
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

// developer aid: BEGOTTEN_VEHTEST="20:use 30:fwd=50 90:turn=300 150:fwd=0 160:use" plays player 1's controls by tic (use is
// one tic; fwd, side, turn (angleturn), fire, alt, jump stay until changed: fire=0 ...) and, with BEGOTTEN_VEHDEBUG=1,
// prints the player's and the vehicle's state every 10 tics
void bgveh_TestCmd(player_t* player)
{
  static int init, ntok;
  static struct { int tic; char what[16]; int val; } tok[256];
  static int next, fwd, side, turn, fire, alt, jump;
  const char* e;
  int one_use = 0;

  if (player != &players[0])
    return;
  if (!init)
  {
    char buf[2048], *c;

    init = 1;
    e = getenv("BEGOTTEN_VEHTEST");
    if (e)
    {
      snprintf(buf, sizeof buf, "%s", e);
      for (c = strtok(buf, " ,"); c && ntok < 256; c = strtok(NULL, " ,"))
      {
        int t, v = 1;
        char w[16];

        if (sscanf(c, "%d:%15[a-z]=%d", &t, w, &v) >= 2 || sscanf(c, "%d:%15[a-z]", &t, w) == 2)
        {
          tok[ntok].tic = t;
          snprintf(tok[ntok].what, sizeof tok[ntok].what, "%s", w);
          tok[ntok].val = v;
          ++ntok;
        }
      }
    }
  }
  if (!ntok)
    return;
  while (next < ntok && tok[next].tic <= leveltime)
  {
    const char* w = tok[next].what;
    int v = tok[next].val;

    if (!strcmp(w, "use")) one_use = 1;
    else if (!strcmp(w, "fwd")) fwd = v;
    else if (!strcmp(w, "side")) side = v;
    else if (!strcmp(w, "turn")) turn = v;
    else if (!strcmp(w, "fire")) fire = v;
    else if (!strcmp(w, "alt")) alt = v;
    else if (!strcmp(w, "jump")) jump = v;
    else if (!strcmp(w, "hurt")) P_DamageMobj(player->mo, NULL, NULL, v);       // a blow at the rider (lands on the vehicle)
    ++next;
  }
  player->cmd.forwardmove = (signed char) fwd;
  player->cmd.sidemove = (signed char) side;
  player->cmd.angleturn = (short) turn;
  if (fire) player->cmd.buttons |= BT_ATTACK;
  if (one_use) player->cmd.buttons |= BT_USE;
  else player->cmd.buttons &= ~BT_USE;
  player->cmd.ex.actions &= ~(XC_ALT | XC_JUMP);
  if (alt) player->cmd.ex.actions |= XC_ALT;
  if (jump) player->cmd.ex.actions |= XC_JUMP;
  if (getenv("BEGOTTEN_VEHDEBUG") && leveltime % 10 == 0)
  {
    mobj_t* v = bgveh_VehicleOf(player->mo);

    fprintf(stderr, "VEH t=%d rider %d,%d,%d ang %d mounted %d", leveltime, player->mo->x >> 16, player->mo->y >> 16,
            player->mo->z >> 16, (int) (player->mo->angle / (ANG1 + 0.0)), v ? 1 : 0);
    if (v)
      fprintf(stderr, " | veh %d,%d vel %.1f,%.1f hp %d", v->x >> 16, v->y >> 16, v->momx / 65536.0, v->momy / 65536.0, v->health);
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
  speed = P_AproxDistance(v->momx, v->momy);
  snprintf(buf, len, "%s %d  SPEED %d", k->name, v->health > 0 ? v->health : 0, (int) ((long long) speed * 394 / 100 >> FRACBITS));
  return true;
}
