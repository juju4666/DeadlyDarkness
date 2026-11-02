//
// Begotten: grind rails. See bgrail.h. Licensed under the GNU GPL v2 or later.
//
#include <stdio.h>
#include <stdlib.h>

#include "doomstat.h"
#include "d_player.h"
#include "m_fixed.h"
#include "p_maputl.h"
#include "p_map.h"
#include "p_mobj.h"
#include "p_setup.h"
#include "r_state.h"
#include "s_sound.h"
#include "sounds.h"
#include "tables.h"
#include "dsda/excmd.h"
#include "dsda/bgclass.h"
#include "dsda/bgrail.h"

extern int validcount;

#define RAIL_HEIGHT   (24 * FRACUNIT)
#define RAIL_SPEED    (26 * FRACUNIT)
#define RAIL_FASTER   (4 * FRACUNIT)
#define RAIL_SLOWER   (6 * FRACUNIT)
#define RAIL_TOPSPEED (29 * FRACUNIT + FRACUNIT / 2)   // the engine moves a thing 30 units a tic at most
#define RAIL_REGRAB   25
#define RAIL_JUMPREF  8                                 // below this speed a jump off a rail is the stock jump
#define RAIL_JUMPDIV  12                                // every 12 units a tic above that adds a whole jump height

#define ST_LINE(s2)   (((s2) & 0xffffff) - 1)
#define ST_REVERSE    0x1000000
#define ST_JUMPHELD   0x2000000

static int swordsman_class(void)
{
  static int cls = -2;

  if (cls == -2)
    cls = bg_ClassFromWord("swordsman");
  return cls;
}

static dboolean is_rail(const line_t* l)
{
  return l->special == BG_RAIL_SPECIAL;
}

dboolean bgrail_Grinding(const player_t* player)
{
  return player->mo && (player->mo->special2.i & 0xffffff) != 0 && (player->mo->flags & MF_NOGRAVITY) &&
         swordsman_class() >= 0 && player->bgclass == swordsman_class();
}

static int iclamp(int v, int lo, int hi)
{
  return v < lo ? lo : v > hi ? hi : v;
}

static fixed_t rail_z(const line_t* l)
{
  fixed_t floor = l->frontsector->floorheight;
  fixed_t h = l->special_args[0] ? l->special_args[0] * FRACUNIT : RAIL_HEIGHT;

  if (l->backsector && l->backsector->floorheight > floor)
    floor = l->backsector->floorheight;
  return floor + h;
}

static fixed_t rail_base_speed(const line_t* l)
{
  return l->special_args[1] ? iclamp(l->special_args[1], 8, 29) * FRACUNIT : RAIL_SPEED;
}

static unsigned long long isqrt64(unsigned long long n)
{
  unsigned long long r = 0, bit = 1ULL << 62;

  while (bit > n)
    bit >>= 2;
  while (bit)
  {
    if (n >= r + bit)
    {
      n -= r + bit;
      r = (r >> 1) + bit;
    }
    else
      r >>= 1;
    bit >>= 2;
  }
  return r;
}

// the start, end and unit direction (16.16) of a rail in the direction of travel
typedef struct
{
  fixed_t ax, ay, bx, by, ux, uy;
  fixed_t len;
} rseg_t;

static void seg_of(const line_t* l, dboolean reverse, rseg_t* s)
{
  long long dx, dy;
  unsigned long long len;

  s->ax = reverse ? l->v2->x : l->v1->x;
  s->ay = reverse ? l->v2->y : l->v1->y;
  s->bx = reverse ? l->v1->x : l->v2->x;
  s->by = reverse ? l->v1->y : l->v2->y;
  dx = (long long) s->bx - s->ax;
  dy = (long long) s->by - s->ay;
  len = isqrt64((unsigned long long) (dx * dx + dy * dy));
  if (len == 0)
    len = 1;
  s->len = (fixed_t) len;
  s->ux = (fixed_t) (dx * FRACUNIT / (long long) len);
  s->uy = (fixed_t) (dy * FRACUNIT / (long long) len);
}

// ---------------------------------------------------------------- hopping on

static mobj_t* hop_mo;
static line_t* hop_line;
static fixed_t hop_dist;

static dboolean PIT_FindRail(line_t* l)
{
  rseg_t s;
  long long dx, dy, t, px, py;
  fixed_t cx, cy, dist, rz, reach;

  if (!is_rail(l))
    return true;
  seg_of(l, false, &s);
  dx = (long long) s.bx - s.ax;
  dy = (long long) s.by - s.ay;
  px = (long long) hop_mo->x - s.ax;
  py = (long long) hop_mo->y - s.ay;
  t = (px * dx + py * dy) / ((dx * dx + dy * dy) / FRACUNIT + 1);      // 16.16 along the line
  if (t < 0)
    t = 0;
  if (t > FRACUNIT)
    t = FRACUNIT;
  cx = s.ax + (fixed_t) (dx * t / FRACUNIT);
  cy = s.ay + (fixed_t) (dy * t / FRACUNIT);
  dist = P_AproxDistance(hop_mo->x - cx, hop_mo->y - cy);
  reach = hop_mo->radius + 12 * FRACUNIT;
  if (dist > reach)
    return true;
  rz = rail_z(l);
  if (hop_mo->z < rz - 28 * FRACUNIT || hop_mo->z > rz + 6 * FRACUNIT)
    return true;
  if (!hop_line || dist < hop_dist)
  {
    hop_line = l;
    hop_dist = dist;
  }
  return true;
}

static void hop_on(player_t* player, line_t* l)
{
  mobj_t* mo = player->mo;
  rseg_t s;
  fixed_t along;
  fixed_t speed;
  int reverse;

  seg_of(l, false, &s);
  // the way he is going, or failing that the way he faces
  along = FixedMul(mo->momx, s.ux) + FixedMul(mo->momy, s.uy);
  if (P_AproxDistance(mo->momx, mo->momy) < 2 * FRACUNIT)
    along = FixedMul(finecosine[mo->angle >> ANGLETOFINESHIFT], s.ux) + FixedMul(finesine[mo->angle >> ANGLETOFINESHIFT], s.uy);
  reverse = along < 0;
  speed = rail_base_speed(l);
  mo->special1.i = speed;
  mo->special2.i = (int) ((l - lines) + 1) | (reverse ? ST_REVERSE : 0) |
                   ((player->cmd.ex.actions & XC_JUMP) ? ST_JUMPHELD : 0);
  mo->flags |= MF_NOCLIP | MF_NOGRAVITY;
  mo->momx = mo->momy = mo->momz = 0;
  S_StartMobjSound(mo, sfx_swtchn);
}

// ---------------------------------------------------------------- along the chain

// the rail that carries on from the end of this one, if any: of the rails meeting at that end, the one that bends
// the least
static line_t* next_rail(const line_t* cur, dboolean reverse, dboolean* next_reverse)
{
  const vertex_t* end = reverse ? cur->v1 : cur->v2;
  rseg_t here, there;
  line_t* best = NULL;
  fixed_t bestdot = -FRACUNIT;
  int i;

  seg_of(cur, reverse, &here);
  for (i = 0; i < numlines; ++i)
  {
    line_t* l = &lines[i];
    dboolean rev;
    fixed_t dot;

    if (l == cur || !is_rail(l))
      continue;
    if (l->v1 == end)
      rev = false;
    else if (l->v2 == end)
      rev = true;
    else
      continue;
    seg_of(l, rev, &there);
    dot = FixedMul(here.ux, there.ux) + FixedMul(here.uy, there.uy);
    if (dot > bestdot)
    {
      bestdot = dot;
      best = l;
      *next_reverse = rev;
    }
  }
  return bestdot > -(FRACUNIT / 2) ? best : NULL;
}

static void get_off(player_t* player, const rseg_t* s, fixed_t speed, fixed_t momz)
{
  mobj_t* mo = player->mo;

  mo->flags &= ~MF_NOGRAVITY;
  if (!(player->cheats & CF_NOCLIP))
    mo->flags &= ~MF_NOCLIP;
  mo->special1.i = mo->special2.i = 0;
  mo->bg_cd = RAIL_REGRAB;
  if (s)
  {
    mo->momx = FixedMul(s->ux, speed);
    mo->momy = FixedMul(s->uy, speed);
  }
  mo->momz = momz;
}

dboolean bgrail_Control(player_t* player)
{
  mobj_t* mo = player->mo;
  const ticcmd_t* cmd = &player->cmd;
  line_t* l;
  rseg_t s;
  dboolean reverse, jump_now, jump_held;
  fixed_t speed, want, remaining, px, py, tx, ty, tz;
  int fwd, hops = 0;

  if (swordsman_class() < 0 || player->bgclass != swordsman_class() || !mo)
    return false;
  if (mo->bg_cd > 0)
    --mo->bg_cd;

  if (!(mo->special2.i & 0xffffff))
  {
    // not on a rail: is there one to hop onto?
    int xl, xh, yl, yh, x, y;
    fixed_t reach = mo->radius + 16 * FRACUNIT;

    if (player->playerstate != PST_LIVE || mo->bg_cd > 0 || mo->z <= mo->floorz || !dsda_AllowExCmd())
      return false;
    hop_mo = mo;
    hop_line = NULL;
    hop_dist = 0;
    yh = P_GetSafeBlockY(mo->y + reach - bmaporgy);
    yl = P_GetSafeBlockY(mo->y - reach - bmaporgy);
    xh = P_GetSafeBlockX(mo->x + reach - bmaporgx);
    xl = P_GetSafeBlockX(mo->x - reach - bmaporgx);
    validcount++;
    for (y = yl; y <= yh; ++y)
      for (x = xl; x <= xh; ++x)
        P_BlockLinesIterator(x, y, PIT_FindRail);
    if (!hop_line)
      return false;
    hop_on(player, hop_line);
  }

  l = &lines[ST_LINE(mo->special2.i)];
  reverse = (mo->special2.i & ST_REVERSE) != 0;
  jump_now = (cmd->ex.actions & XC_JUMP) != 0;
  jump_held = (mo->special2.i & ST_JUMPHELD) != 0;
  seg_of(l, reverse, &s);

  if (player->playerstate != PST_LIVE)
  {
    get_off(player, NULL, 0, 0);
    return false;
  }

  // speed: a little faster forward, a little slower back, never stopped
  fwd = iclamp(cmd->forwardmove, -50, 50);
  want = rail_base_speed(l);
  if (fwd > 0)
    want += RAIL_FASTER * fwd / 50;
  else if (fwd < 0)
    want += RAIL_SLOWER * fwd / 50;
  speed = mo->special1.i;
  if (speed < want)
    speed = speed + FRACUNIT / 2 > want ? want : speed + FRACUNIT / 2;
  else if (speed > want)
    speed = speed - FRACUNIT / 2 < want ? want : speed - FRACUNIT / 2;
  if (speed > RAIL_TOPSPEED)
    speed = RAIL_TOPSPEED;

  // off with a jump: the faster he goes, the higher and further
  if (jump_now && !jump_held)
  {
    int cls = player->bgclass, base = bg_ClassJumpHeight(cls);
    int units = speed >> FRACBITS;
    int height = base > 0 ? base : 41;
    fixed_t momz;

    if (units > RAIL_JUMPREF)
      height = height * (RAIL_JUMPDIV + units - RAIL_JUMPREF) / RAIL_JUMPDIV;
    momz = bg_JumpSpeedFor(height);
    get_off(player, &s, speed, momz);
    mo->flags2 &= ~MF2_ONMOBJ;
    player->jumpTics = 18;
    if (bg_ClassJumpSfx(player))
      S_StartMobjSound(mo, bg_ClassJumpSfx(player));
    if (getenv("BEGOTTEN_VEHDEBUG"))
      fprintf(stderr, "RAIL jump off at %.1f: height %d momz %.1f\n", speed / 65536.0, height, momz / 65536.0);
    return false;
  }

  // move along the chain, exactly `speed` units this tic
  px = mo->x;
  py = mo->y;
  remaining = speed;
  for (;;)
  {
    // he rides the line itself, not the spot beside it he hopped on from
    long long along = ((long long) (px - s.ax) * s.ux + (long long) (py - s.ay) * s.uy) / FRACUNIT;
    long long to_end;
    dboolean next_rev = false;
    line_t* nl;

    if (along < 0)
      along = 0;
    if (along > s.len)
      along = s.len;
    px = s.ax + (fixed_t) (along * s.ux / FRACUNIT);
    py = s.ay + (fixed_t) (along * s.uy / FRACUNIT);
    to_end = s.len - along;
    if (remaining <= to_end)
    {
      px += (fixed_t) ((long long) s.ux * remaining / FRACUNIT);
      py += (fixed_t) ((long long) s.uy * remaining / FRACUNIT);
      break;
    }
    remaining -= (fixed_t) to_end;
    px = s.bx;
    py = s.by;
    nl = ++hops < 8 ? next_rail(l, reverse, &next_rev) : NULL;
    if (!nl)
    {
      // the end of the line: off he goes, the way he was heading, with a little hop
      if (getenv("BEGOTTEN_VEHDEBUG"))
        fprintf(stderr, "RAIL end of the line at %.1f\n", speed / 65536.0);
      get_off(player, &s, speed, 4 * FRACUNIT);
      return false;
    }
    l = nl;
    reverse = next_rev;
    seg_of(l, reverse, &s);
  }
  mo->special1.i = speed;
  mo->special2.i = (int) ((l - lines) + 1) | (reverse ? ST_REVERSE : 0) | (jump_now ? ST_JUMPHELD : 0);

  tx = px;
  ty = py;
  tz = rail_z(l);
  mo->momx = tx - mo->x;
  mo->momy = ty - mo->y;
  mo->momz = tz - mo->z;
  if (!(mo->flags & MF_NOCLIP))
    mo->flags |= MF_NOCLIP;
  if (!(leveltime & 7))
    S_StartMobjSound(mo, sfx_stnmov);
  if (getenv("BEGOTTEN_VEHDEBUG") && !(leveltime & 7))
    fprintf(stderr, "RAIL on line %d at %d %d z %d speed %.1f\n", (int) (l - lines), mo->x >> 16, mo->y >> 16, mo->z >> 16, speed / 65536.0);
  return true;
}
