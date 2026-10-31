/*
 * Begotten: line portals, see portal.h.
 */
#include <string.h>

#include "doomstat.h"
#include "lprintf.h"
#include "p_spec.h"
#include "p_map.h"
#include "p_mobj.h"
#include "p_maputl.h"
#include "g_game.h"
#include "info.h"
#include "d_player.h"
#include "r_main.h"
#include "z_zone.h"
#include "dsda/id_list.h"
#include "dsda/line_special.h"
#include "dsda/portal.h"

int bg_numportals;
line_t** bg_portal_lines;
static int portal_lines_alloc;
static bg_portal_t* store;          // one per line, whether it is a portal or not
static int store_lines;

static uint64_t isqrt64(uint64_t n)
{
  uint64_t r = 0, b = (uint64_t) 1 << 62;

  while (b > n)
    b >>= 2;
  while (b)
  {
    if (n >= r + b)
    {
      n -= r + b;
      r = (r >> 1) + b;
    }
    else
      r >>= 1;
    b >>= 2;
  }
  return r;
}

void bg_PortalsReset(void)
{
  int i;

  bg_numportals = 0;
  if (store_lines != numlines || !store)
  {
    Z_Free(store);
    store = numlines > 0 ? Z_Calloc(numlines, sizeof(*store)) : NULL;
    store_lines = numlines;
  }
  for (i = 0; i < numlines; ++i)
    lines[i].portal = NULL;
}

static void Count(void)
{
  int i;

  bg_numportals = 0;
  for (i = 0; i < numlines; ++i)
    if (lines[i].portal)
      ++bg_numportals;
  if (bg_numportals > portal_lines_alloc)
  {
    Z_Free(bg_portal_lines);
    portal_lines_alloc = bg_numportals + 16;
    bg_portal_lines = Z_Malloc(portal_lines_alloc * sizeof(*bg_portal_lines));
  }
  bg_numportals = 0;
  for (i = 0; i < numlines; ++i)
    if (lines[i].portal)
      bg_portal_lines[bg_numportals++] = &lines[i];
}

// the turn that takes the direction of src to the opposite of the direction of dst (the front of src is the back of
// dst, so whoever leaves src's front arrives at dst's front)
static void Prepare(bg_portal_t* p)
{
  int64_t sx = p->src->dx, sy = p->src->dy, ex = -(int64_t) p->dst->dx, ey = -(int64_t) p->dst->dy;
  int64_t big, dot, cross, n;
  int64_t c, s;

  big = sx < 0 ? -sx : sx;
  if ((sy < 0 ? -sy : sy) > big) big = sy < 0 ? -sy : sy;
  if ((ex < 0 ? -ex : ex) > big) big = ex < 0 ? -ex : ex;
  if ((ey < 0 ? -ey : ey) > big) big = ey < 0 ? -ey : ey;
  while (big >= (1 << 16))          // keep the products below 2^34 so that the division below fits in 64 bits
  {
    sx /= 2; sy /= 2; ex /= 2; ey /= 2;
    big /= 2;
  }
  dot = sx * ex + sy * ey;
  cross = sx * ey - sy * ex;
  n = (int64_t) (isqrt64((uint64_t) (sx * sx + sy * sy)) * isqrt64((uint64_t) (ex * ex + ey * ey)));
  if (n <= 0)
  {
    c = (int64_t) 1 << 30;
    s = 0;
  }
  else
  {
    c = dot * ((int64_t) 1 << 30) / n;
    s = cross * ((int64_t) 1 << 30) / n;
  }
  // snap the nearly exact (a 90 degree room is the common case): no drift in either direction
  {
    const int64_t one = (int64_t) 1 << 30, eps = 1 << 14;

    if (s > -eps && s < eps) { s = 0; c = c < 0 ? -one : one; }
    else if (c > -eps && c < eps) { c = 0; s = s < 0 ? -one : one; }
  }
  p->c = c;
  p->s = s;
  if (s == 0)
    p->dangle = c < 0 ? ANG180 : 0;
  else if (c == 0)
    p->dangle = s < 0 ? ANG270 : ANG90;
  else
    p->dangle = R_PointToAngle2(0, 0, (fixed_t) (c >> 14), (fixed_t) (s >> 14));
}

static void PrepareHeight(bg_portal_t* p)
{
  if (p->src->frontsector && p->dst->frontsector)
    p->dz = p->dst->frontsector->floorheight - p->src->frontsector->floorheight;
  else
    p->dz = 0;
}

// the lines of a polyobject move: every tic the turn of each portal is worked out again
void bg_PortalsUpdate(void)
{
  int i;

  if (leveltime == 10 && getenv("BEGOTTEN_PORTALSPIN"))     // developer: polyobject 1 starts to turn (for screenshots)
  {
    int args[5] = {1, 8, 255, 0, 0};

    P_ExecuteZDoomLineSpecial(zl_polyobj_rotate_left, args, NULL, 0, players[0].mo);
  }

  for (i = 0; i < bg_numportals; ++i)
  {
    bg_portal_t* p = bg_portal_lines[i]->portal;
    const line_t* l = p->src;

    Prepare(p);
    if (p->ox1 != l->v1->x || p->oy1 != l->v1->y || p->ox2 != l->v2->x || p->oy2 != l->v2->y)
    {
      if (p->ox1 | p->oy1 | p->ox2 | p->oy2)         // (not the first time)
        P_PortalSweep(p);
      p->ox1 = l->v1->x; p->oy1 = l->v1->y; p->ox2 = l->v2->x; p->oy2 = l->v2->y;
    }
  }
}

dboolean bg_PortalLink(line_t* src, line_t* dst)
{
  if (!src)
    return false;
  if (!dst || dst == src)
  {
    src->portal = NULL;
    Count();
    return true;
  }
  if (!src->frontsector || !dst->frontsector)
  {
    lprintf(LO_WARN, "portal: line %d or %d has no front sector\n", (int) (src - lines), (int) (dst - lines));
    return false;
  }
  if (!src->portal)
  {
    if (!store || src < lines || src >= lines + numlines)
      return false;
    src->portal = &store[src - lines];
  }
  src->portal->src = src;
  src->portal->dst = dst;
  Prepare(src->portal);
  PrepareHeight(src->portal);
  src->portal->ox1 = src->v1->x; src->portal->oy1 = src->v1->y; src->portal->ox2 = src->v2->x; src->portal->oy2 = src->v2->y;
  {
    int64_t ls = isqrt64((uint64_t) ((int64_t) src->dx >> 4) * (src->dx >> 4) + (uint64_t) ((int64_t) src->dy >> 4) * (src->dy >> 4));
    int64_t ld = isqrt64((uint64_t) ((int64_t) dst->dx >> 4) * (dst->dx >> 4) + (uint64_t) ((int64_t) dst->dy >> 4) * (dst->dy >> 4));

    if (ls - ld > 16 * 16 || ld - ls > 16 * 16)   // more than a unit apart (the length is in 1/16 of a fixed unit)
      lprintf(LO_WARN, "portal: lines %d and %d differ in length (%d and %d)\n", (int) (src - lines), (int) (dst - lines),
              (int) (ls >> 12), (int) (ld >> 12));
  }
  Count();
  return true;
}

static line_t* FirstLine(int id, const line_t* not_this)
{
  const int* id_p;

  if (id == 0)
    return NULL;
  FIND_LINES(id_p, id)
    if (&lines[*id_p] != not_this)
      return &lines[*id_p];
  return NULL;
}

void bg_PortalSpawn(line_t* l)
{
  int target = l->special_args[0], thisid = l->special_args[1];

  if (thisid == 0)
  {
    line_t* dst = FirstLine(target, l);

    if (!dst)
      lprintf(LO_WARN, "portal: line %d points to line id %d, which does not exist\n", (int) (l - lines), target);
    else
      bg_PortalLink(l, dst);
  }
  else
  {
    const int* id_p;

    FIND_LINES(id_p, thisid)
    {
      line_t* dst = FirstLine(target, &lines[*id_p]);

      if (dst)
        bg_PortalLink(&lines[*id_p], dst);
    }
  }
  // the special itself has done its job; it must not fire when somebody crosses or uses the line
  l->special = 0;
}

dboolean bg_PortalExecute(line_t* l, int special, const int* args)
{
  const int* id_p;
  dboolean did = false;

  if (special == zl_line_set_portal_target)           // (thisline, targetline)
  {
    FIND_LINES(id_p, args[0])
    {
      line_t* dst = FirstLine(args[1], &lines[*id_p]);

      if (dst || args[1] == 0)
        did |= bg_PortalLink(&lines[*id_p], dst);
    }
    return did;
  }
  if (special == zl_line_set_portal)                 // (targetline, thisline, type, planeanchor)
  {
    if (args[1] == 0)
    {
      line_t* dst = FirstLine(args[0], l);

      return l && dst && bg_PortalLink(l, dst);
    }
    FIND_LINES(id_p, args[1])
    {
      line_t* dst = FirstLine(args[0], &lines[*id_p]);

      if (dst)
        did |= bg_PortalLink(&lines[*id_p], dst);
    }
  }
  return did;
}

void bg_PortalsFinish(void)
{
  Count();
}

void bg_PortalMapVector(const bg_portal_t* p, fixed_t x, fixed_t y, fixed_t* ox, fixed_t* oy)
{
  *ox = (fixed_t) (((int64_t) x * p->c - (int64_t) y * p->s) >> 30);
  *oy = (fixed_t) (((int64_t) x * p->s + (int64_t) y * p->c) >> 30);
}

void bg_PortalMapPoint(const bg_portal_t* p, fixed_t x, fixed_t y, fixed_t* ox, fixed_t* oy)
{
  fixed_t rx, ry;

  bg_PortalMapVector(p, x - p->src->v1->x, y - p->src->v1->y, &rx, &ry);
  *ox = p->dst->v2->x + rx;
  *oy = p->dst->v2->y + ry;
}

fixed_t bg_PortalCross(const line_t* l, fixed_t x0, fixed_t y0, fixed_t x1, fixed_t y1)
{
  // the front of a line is on its right: cross(d, p - v1) < 0
  int64_t a = (int64_t) l->dx * (y0 - l->v1->y) - (int64_t) l->dy * (x0 - l->v1->x);
  int64_t b = (int64_t) l->dx * (y1 - l->v1->y) - (int64_t) l->dy * (x1 - l->v1->x);
  int64_t t;
  int64_t px, py, along, len2;

  if (a >= 0 || b < 0)       // not (in front before, behind or on the line afterwards)
    return -1;
  t = (a * FRACUNIT) / (a - b);        // a < 0 <= b
  px = x0 + (((int64_t) x1 - x0) * t >> FRACBITS);
  py = y0 + (((int64_t) y1 - y0) * t >> FRACBITS);
  // within the extent of the line?
  len2 = ((int64_t) l->dx >> 8) * (l->dx >> 8) + ((int64_t) l->dy >> 8) * (l->dy >> 8);
  along = (((px - l->v1->x) >> 8) * (l->dx >> 8) + ((py - l->v1->y) >> 8) * (l->dy >> 8));
  if (along < 0 || along > len2)
    return -1;
  return (fixed_t) t;
}

#define CHECK(c, msg) do { if (c) lprintf(LO_INFO, "ok:   %s\n", msg); \
                           else { lprintf(LO_INFO, "FAIL: %s\n", msg); ++*failures; } } while (0)

void bg_PortalSelfTest(int* failures)
{
  // two lines made up on the spot: A from (0,0) to (0,64) (front is to the right: +x), B from (512,0) to (512,64)... turned
  vertex_t a1 = {0, 0}, a2 = {0, 64 * FRACUNIT}, b1 = {1000 * FRACUNIT, 100 * FRACUNIT}, b2 = {1064 * FRACUNIT, 100 * FRACUNIT};
  sector_t sa, sb;
  line_t A, B;
  bg_portal_t p;
  fixed_t x, y;

  memset(&A, 0, sizeof A); memset(&B, 0, sizeof B); memset(&sa, 0, sizeof sa); memset(&sb, 0, sizeof sb);
  A.v1 = &a1; A.v2 = &a2; A.dx = 0; A.dy = 64 * FRACUNIT; A.frontsector = &sa;
  B.v1 = &b1; B.v2 = &b2; B.dx = 64 * FRACUNIT; B.dy = 0; B.frontsector = &sb;
  sb.floorheight = 32 * FRACUNIT;
  memset(&p, 0, sizeof p);
  p.src = &A; p.dst = &B;
  Prepare(&p);
  // A's front is +x (right of an upward line); B runs +x so its front is -y.  Walking out through A's front (towards -x)
  // comes out of B's front moving -y
  bg_PortalMapVector(&p, -FRACUNIT, 0, &x, &y);
  CHECK(x == 0 && y == -FRACUNIT, "portal: the direction out of the front of A is the direction out of the front of B");
  bg_PortalMapPoint(&p, 0, 0, &x, &y);
  CHECK(x == b2.x && y == b2.y, "portal: the start of A is the end of B");
  bg_PortalMapPoint(&p, 0, 64 * FRACUNIT, &x, &y);
  CHECK(x == b1.x && y == b1.y, "portal: the end of A is the start of B");
  bg_PortalMapPoint(&p, 10 * FRACUNIT, 32 * FRACUNIT, &x, &y);
  CHECK(x == 1032 * FRACUNIT && y == 110 * FRACUNIT, "portal: a point 10 in front of A is 10 behind B");
  CHECK(p.dz == 32 * FRACUNIT, "portal: the height difference of the floors");
  CHECK(p.dangle == ANG90 || p.dangle == (angle_t) (ANG90 + 1) || p.dangle == (angle_t) (ANG90 - 1) || p.dangle == (angle_t) (ANG270 + 0),
        "portal: the turn is a quarter");
  CHECK(bg_PortalCross(&A, 10 * FRACUNIT, 32 * FRACUNIT, -10 * FRACUNIT, 32 * FRACUNIT) == FRACUNIT / 2,
        "portal: a move through the middle crosses at half way");
  CHECK(bg_PortalCross(&A, -10 * FRACUNIT, 32 * FRACUNIT, 10 * FRACUNIT, 32 * FRACUNIT) == -1,
        "portal: no crossing from behind");
  CHECK(bg_PortalCross(&A, 10 * FRACUNIT, 100 * FRACUNIT, -10 * FRACUNIT, 100 * FRACUNIT) == -1,
        "portal: no crossing past the end of the line");
}

// BEGOTTEN_SELFTEST=P with the portal test map (tools/make_portal_test_map.py): things are moved through the portals
static void PortalLiveTest(void)
{
  static int fails, phase, save_tic, load_tic;
  mobj_t* mo = players[0].mo;
  dboolean ok;
  fixed_t U = FRACUNIT;

#define PCK(c, msg) do { if (c) lprintf(LO_INFO, "ok:   %s\n", msg); else { lprintf(LO_INFO, "FAIL: %s\n", msg); ++fails; } } while (0)
#define PLACE(px, py, ang, mx, my) do { P_TeleportMove(mo, (px) * U, (py) * U, false); mo->angle = (ang); mo->momx = (mx) * U; mo->momy = (my) * U; \
                                       mo->z = mo->floorz; players[0].momx = players[0].momy = 0; } while (0)
  if (phase == 0 && leveltime >= 5)
  {
    char w[200];
    int args[5] = {1, 6, 0, 0, 0};

    PCK(bg_numportals == 5, "portals: the five portal lines of the test map are there");
    // straight through: A -> B (the floor of B is 32 higher)
    PLACE(400, 256, 0, 20, 0);
    ok = P_TryMove(mo, 520 * U, 256 * U, false);
    snprintf(w, sizeof w, "portals: walking east through S comes out of D (now %d,%d,%d)", mo->x >> 16, mo->y >> 16, mo->z >> 16);
    PCK(ok && mo->x >= 2000 * U && mo->x < 2012 * U && mo->y == 256 * U && mo->z == 32 * U, w);
    PCK(mo->angle == 0 && mo->momx == 20 * U && mo->momy == 0, "portals: the angle and the momentum are kept when the line is straight");
    PCK(mo->subsector->sector == &sectors[1], "portals: the thing is in B's sector");
    // and back
    ok = P_TryMove(mo, 1990 * U, 256 * U, false);
    snprintf(w, sizeof w, "portals: and back through D to S (now %d,%d,%d)", mo->x >> 16, mo->y >> 16, mo->z >> 16);
    PCK(ok && mo->x == 502 * U && mo->y == 256 * U && mo->z == 0, w);
    // a quarter turn: A's north wall (P) -> C's east wall (E)
    PLACE(256, 490, ANG90, 0, 20);
    ok = P_TryMove(mo, 256 * U, 520 * U, false);
    snprintf(w, sizeof w, "portals: north through P comes out of E, turned (now %d,%d,%d angle %u momentum %d,%d)", mo->x >> 16, mo->y >> 16,
             mo->z >> 16, (unsigned) (mo->angle >> 24), mo->momx >> 16, mo->momy >> 16);
    PCK(ok && mo->x == 4504 * U && mo->y == 256 * U && mo->z == -16 * U && mo->angle == ANG180 && mo->momx == -20 * U && mo->momy == 0, w);
    PCK(mo->subsector->sector == &sectors[2], "portals: the thing is in C's sector");
    // the switch: S now leads to F
    PCK(bg_PortalExecute(NULL, zl_line_set_portal_target, args), "portals: Line_SetPortalTarget re-points S");
    PLACE(400, 256, 0, 20, 0);
    ok = P_TryMove(mo, 520 * U, 256 * U, false);
    snprintf(w, sizeof w, "portals: S now leads to F, a quarter turn (now %d,%d angle %u)", mo->x >> 16, mo->y >> 16, (unsigned) (mo->angle >> 24));
    PCK(ok && mo->x == 4256 * U && mo->y == 8 * U && mo->angle == ANG90 && mo->momx == 0 && mo->momy == 20 * U, w);
    args[1] = 2;
    PCK(bg_PortalExecute(NULL, zl_line_set_portal_target, args), "portals: and back to D");
    PLACE(400, 256, 0, 20, 0);
    ok = P_TryMove(mo, 520 * U, 256 * U, false);
    PCK(ok && mo->x >= 2000 * U && mo->x < 2012 * U, "portals: S leads to B again");
    // a wall is still a wall
    PLACE(100, 256, 0, 0, 0);
    ok = P_TryMove(mo, 700 * U, 256 * U, false);
    PCK(!ok || mo->x < 520 * U, "portals: nothing but the portal lines is open");
    // shots: aim and hit a barrel in B through S, and one in C through P (a quarter turn)
    {
      mobj_t* b1 = P_SpawnMobj(2300 * U, 256 * U, ONFLOORZ, MT_BARREL);
      mobj_t* b2 = P_SpawnMobj(4400 * U, 256 * U, ONFLOORZ, MT_BARREL);
      fixed_t slope;
      int h1 = b1->health, h2 = b2->health;

      {
        mobj_t* b0 = P_SpawnMobj(480 * U, 100 * U, ONFLOORZ, MT_BARREL);
        int h0 = b0->health;

        PLACE(400, 100, 0, 0, 0);
        P_LineAttack(mo, 0, 4000 * U, P_AimLineAttack(mo, 0, 4000 * U, 0), 5);
        PCK(b0->health < h0, "portals: (control: a bullet at a barrel in the same room hits it)");
      }
      PLACE(400, 256, 0, 0, 0);
      slope = P_AimLineAttack(mo, 0, 4000 * U, 0);
      PCK(linetarget == b1, "portals: autoaim finds a target on the other side of S");
      P_LineAttack(mo, 0, 4000 * U, slope, 5);
      PCK(b1->health < h1, "portals: a bullet through S hits it");
      PLACE(256, 300, ANG90, 0, 0);
      slope = P_AimLineAttack(mo, ANG90, 4000 * U, 0);
      PCK(linetarget == b2, "portals: autoaim finds a target on the other side of P (turned)");
      P_LineAttack(mo, ANG90, 4000 * U, slope, 5);
      PCK(b2->health < h2, "portals: a bullet through P hits it");
    }
    // the real movement: fast, in halves
    PLACE(490, 256, 0, 40, 0);
    phase = 1;
    return;
  }
  if (phase == 1)
  {
    char w[200];

    snprintf(w, sizeof w, "portals: a thing moving fast through S ends up in B (at %d,%d)", mo->x >> 16, mo->y >> 16);
    PCK(mo->x >= 2000 * U && mo->x < 2100 * U && mo->y == 256 * U, w);
    // north through P again, as P_XYMovement does it: the rest of the move turns with the portal
    PLACE(256, 495, ANG90, 0, 40);
    phase = 2;
    return;
  }
  if (phase == 2)
  {
    char w[200];

    snprintf(w, sizeof w, "portals: a fast move through P turns: now at %d,%d angle %u momentum %d,%d", mo->x >> 16, mo->y >> 16,
             (unsigned) (mo->angle >> 24), mo->momx >> 16, mo->momy >> 16);
    PCK(mo->x <= 4504 * U && mo->x > 4400 * U && mo->y >= 250 * U && mo->y <= 262 * U && mo->angle == ANG180 && mo->momx < 0, w);
    // a saved game remembers where S leads
    G_SaveGame(7, "portaltest");
    save_tic = leveltime;
    phase = 3;
    return;
  }
  if (phase == 3 && leveltime >= save_tic + 3)
  {
    int args[5] = {1, 6, 0, 0, 0};

    bg_PortalExecute(NULL, zl_line_set_portal_target, args);       // S now leads to F ...
    PCK(FirstLine(1, NULL)->portal->dst == FirstLine(6, NULL), "portals: (S leads to F before loading)");
    load_tic = leveltime;
    phase = 4;
    return;
  }
  if (phase == 4 && leveltime >= load_tic + 2)
  {
    G_LoadGame(7, false);       // ... and the loaded game has it lead to D again
    load_tic = leveltime;
    phase = 5;
    return;
  }
  if (phase == 5 && gameaction == ga_nothing && leveltime < load_tic)
  {
    PCK(FirstLine(1, NULL)->portal && FirstLine(1, NULL)->portal->dst == FirstLine(2, NULL), "portals: a loaded game has S lead to D again");
    PCK(bg_numportals == 5, "portals: and the five portal lines");
    lprintf(LO_INFO, "PORTALTEST: %s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
    exit(fails ? 1 : 0);
  }
}

void bg_PortalLiveTest(void)
{
  P_MapStart();
  PortalLiveTest();
  P_MapEnd();
}
