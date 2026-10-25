//
// Begotten: the monster roster.  See bgroster.h.
// Licensed under the GNU GPL v2 or later, like the rest of the engine.
//
// The roster's mobj types and state chains are built at start-up from copies of Doom's own
// (after DEHACKED has been applied, so the WAD's DEHACKED edits carry over to the copies).
// Every tunable number is a named value that the BGMOBJ lump can override.
//

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _MSC_VER
#include <strings.h>
#endif
#include "doomtype.h"

#include "doomstat.h"
#include "info.h"
#include "lprintf.h"
#include "m_fixed.h"
#include "m_random.h"
#include "p_enemy.h"
#include "p_inter.h"
#include "p_map.h"
#include "p_maputl.h"
#include "p_mobj.h"
#include "p_tick.h"
#include "r_main.h"
#include "s_sound.h"
#include "sounds.h"
#include "tables.h"
#include "w_wad.h"
#include "z_zone.h"

#include "dsda.h"
#include "dsda/begotten.h"
#include "dsda/bgclass.h"
#include "dsda/bgroster.h"
#include "dsda/mobjinfo.h"
#include "dsda/sprite.h"
#include "dsda/state.h"

#define NEXT_ (-1)
#define FF_FB 0x8000 // full-bright frame bit (FF_FULLBRIGHT)
#define DEGF(x) ((long long) ((x) * 65536.0)) // degrees in fixed point

//
// BGMOBJ: named values
//

#define MAX_OV 768

typedef struct
{
  char key[40];
  char field[32];
  int value;
} ov_t;

static ov_t ovs[MAX_OV];
static int num_ov;

static void ov_set(const char* key, const char* field, int value)
{
  ov_t* o;

  if (num_ov >= MAX_OV)
    return;
  o = &ovs[num_ov++];
  strncpy(o->key, key, sizeof(o->key) - 1);
  strncpy(o->field, field, sizeof(o->field) - 1);
  o->key[sizeof(o->key) - 1] = o->field[sizeof(o->field) - 1] = '\0';
  o->value = value;
}

static void ov_parse(const char* text, int lumpnum)
{
  const char* p = text;
  int lineno = 0;

  while (*p)
  {
    char line[256];
    char* tok[24];
    int n = 0, i = 0;
    char* c;

    ++lineno;
    while (*p && *p != '\n' && i < (int) sizeof(line) - 1)
      line[i++] = *p++;
    line[i] = '\0';
    if (*p == '\n')
      ++p;

    for (c = line; *c; ++c)
      if (*c == '#' || (c[0] == '/' && c[1] == '/') || *c == '=' || *c == ',')
      {
        if (*c == '=' || *c == ',')
          *c = ' ';
        else
        {
          *c = '\0';
          break;
        }
      }
    for (c = line; *c;)
    {
      while (*c && isspace((unsigned char) *c))
        ++c;
      if (!*c)
        break;
      if (n < 24)
        tok[n++] = c;
      while (*c && !isspace((unsigned char) *c))
      {
        *c = (char) tolower((unsigned char) *c);
        ++c;
      }
      if (*c)
        *c++ = '\0';
    }
    if (n == 0)
      continue;
    if (n < 3 || (n % 2) == 0)
    {
      lprintf(LO_WARN, "BGMOBJ (lump %d, line %d): expected \"<name> <field> <value> ...\"\n", lumpnum, lineno);
      continue;
    }
    for (i = 1; i + 1 < n; i += 2)
    {
      char* end;
      long v = strtol(tok[i + 1], &end, 10);

      if (*end)
      {
        lprintf(LO_WARN, "BGMOBJ (lump %d, line %d): \"%s\" is not a whole number\n", lumpnum, lineno, tok[i + 1]);
        continue;
      }
      ov_set(tok[0], tok[i], (int) v);
    }
  }
}

// the value of key.field: the last BGMOBJ line wins, else the built-in default
static int V(const char* key, const char* field, int dflt)
{
  int i;

  for (i = num_ov - 1; i >= 0; --i)
    if (!strcmp(ovs[i].key, key) && !strcmp(ovs[i].field, field))
      return ovs[i].value;
  return dflt;
}

//
// States
//

static int st_add(int spr, int frame, int tics, actionf_t act, int next)
{
  int idx = bg_AllocStates(1);
  state_t* st = &states[idx];

  memset(st, 0, sizeof(*st));
  st->sprite = spr;
  st->frame = frame;
  st->tics = tics;
  st->action = act;
  st->nextstate = (next == NEXT_) ? idx + 1 : next;
  return idx;
}

static void st_args(int idx, long long a0, long long a1, long long a2, long long a3,
                    long long a4, long long a5, long long a6, long long a7)
{
  state_t* st = &states[idx];

  st->args[0] = a0;
  st->args[1] = a1;
  st->args[2] = a2;
  st->args[3] = a3;
  st->args[4] = a4;
  st->args[5] = a5;
  st->args[6] = a6;
  st->args[7] = a7;
}

#define ACT(f) ((actionf_t) (f))

// state copies: old stock state -> new state
static int* cmap;
static int cmap_n;

// in place: the monster's own states are edited instead of copied
static dboolean clone_inplace;

static int clone_state(int old, int ospr, int nspr)
{
  state_t tmp;
  int idx;

  if (old <= 0 || old >= cmap_n)
    return old;
  if (cmap[old])
    return cmap[old];
  if (clone_inplace)
  {
    cmap[old] = old;
    clone_state(states[old].nextstate, ospr, nspr);
    return old;
  }
  idx = bg_AllocStates(1);
  cmap[old] = idx;
  tmp = states[old];
  if (tmp.sprite == ospr && nspr >= 0)
    tmp.sprite = nspr;
  tmp.nextstate = (tmp.nextstate == old) ? idx : clone_state(tmp.nextstate, ospr, nspr);
  states[idx] = tmp;
  return idx;
}

static void clone_begin(void)
{
  cmap_n = bg_StateTop() + 1;
  cmap = calloc(cmap_n, sizeof(*cmap));
}

static void clone_end(void)
{
  free(cmap);
  cmap = NULL;
  cmap_n = 0;
}

static int CL(int old)
{
  return (cmap && old > 0 && old < cmap_n && cmap[old]) ? cmap[old] : old;
}

// the copy of the stock state `old`, or 0 when it was not copied
static void clone_chains(mobjinfo_t* info, int ospr, int nspr)
{
  info->spawnstate = clone_state(info->spawnstate, ospr, nspr);
  info->seestate = clone_state(info->seestate, ospr, nspr);
  info->painstate = clone_state(info->painstate, ospr, nspr);
  info->meleestate = clone_state(info->meleestate, ospr, nspr);
  info->missilestate = clone_state(info->missilestate, ospr, nspr);
  info->deathstate = clone_state(info->deathstate, ospr, nspr);
  info->xdeathstate = clone_state(info->xdeathstate, ospr, nspr);
  info->raisestate = clone_state(info->raisestate, ospr, nspr);
  info->crashstate = clone_state(info->crashstate, ospr, nspr);
}

// every copied state running `from` now runs `to` (returns how many)
static int replace_action(actionf_t from, actionf_t to)
{
  int o, n = 0;

  for (o = 1; o < cmap_n; ++o)
    if (cmap[o] && states[cmap[o]].action == from)
    {
      states[cmap[o]].action = to;
      ++n;
    }
  return n;
}

// the first copied state running `act` (0 if none)
static int find_action(actionf_t act)
{
  int o;

  for (o = 1; o < cmap_n; ++o)
    if (cmap[o] && states[cmap[o]].action == act)
      return cmap[o];
  return 0;
}

// make the end of a death chain vanish instead of leaving a corpse
static void vanish_death(int deathstate)
{
  int s = deathstate, guard = 0;

  while (s > 0 && guard++ < 64)
  {
    int next = states[s].nextstate;

    if (states[s].tics < 0)
    {
      states[s].tics = 8;
      states[s].nextstate = 0;
      return;
    }
    if (next == s || next <= 0)
      return;
    s = next;
  }
}

//
// Roster table
//

typedef struct spec_s
{
  const char* key;    // BGMOBJ / UMAPINFO name
  const char* title;
  int ednum;          // -1: a projectile
  int base;           // the Doom type that is copied
  const char* sprite; // new sprite name (copies of the base's sprite lumps), NULL: keep the base's
  int health, speed, radius, height, mass, pain, reaction, damage; // 0 = as the base
  uint64_t flags_on, flags_off, flags2_on;
  void (*build)(struct spec_s* sp);
  int type;           // the resulting mobj type
} spec_t;

static void b_rockettrooper(spec_t* sp);
static void b_shocktrooper(spec_t* sp);
static void b_werewolf(spec_t* sp);
static void b_fireelemental(spec_t* sp);
static void b_madwizard(spec_t* sp);
static void b_apprentice(spec_t* sp);
static void b_minister(spec_t* sp);
static void b_golem(spec_t* sp);
static void b_mindeater(spec_t* sp);
static void b_aegis(spec_t* sp);
static void b_bore(spec_t* sp);
static void b_conjurer(spec_t* sp);
static void b_hatred(spec_t* sp);
static void b_mecha(spec_t* sp);
static void b_beetle(spec_t* sp);
static void b_heart(spec_t* sp);
static void b_core(spec_t* sp);
static void b_trooper_rocket(spec_t* sp);
static void b_golem_rock(spec_t* sp);
static void b_mecha_flame(spec_t* sp);

#define MSL (MF_NOBLOCKMAP | MF_MISSILE | MF_DROPOFF | MF_NOGRAVITY)

static spec_t roster[] = {
  // ---- projectiles (first: the monsters refer to them) ----
  // key              title                 ednum base          sprite  hp spd rad hgt mass pain react dmg
  { "trooper_rocket", "Trooper rocket",       -1, MT_ROCKET,    "TRKT",  0, 14,  11,  8,  100, 0, 0,  6, 0, 0, 0, b_trooper_rocket },
  { "shock_bolt",     "Shock bolt",           -1, MT_ARACHPLAZ, "SBLT",  0, 22,  13,  8,  100, 0, 0,  3, 0, 0, 0, NULL },
  { "elem_fireball",  "Fire elemental bolt",  -1, MT_TROOPSHOT, "EFBL",  0, 16,   6,  8,  100, 0, 0,  5, 0, 0, 0, NULL },
  { "wizard_bolt",    "Mad wizard bolt",      -1, MT_FATSHOT,   "WZBL",  0, 18,   6,  8,  100, 0, 0,  8, 0, 0, 0, NULL },
  { "wizard_seeker",  "Mad wizard seeker",    -1, MT_TRACER,    "WZSK",  0, 12,  11,  8,  100, 0, 0, 10, 0, 0, 0, NULL },
  { "drain_bolt",     "Apprentice drain bolt",-1, MT_BRUISERSHOT, "DRNB", 0, 18,   6,  8,  100, 0, 0,  8, 0, 0, 0, NULL },
  { "minister_bolt",  "Minister drain bolt",  -1, MT_BRUISERSHOT, "DRNB", 0, 20,   6,  8,  100, 0, 0, 10, 0, 0, 0, NULL },
  { "golem_rock",     "Golem rock",           -1, MT_TROOPSHOT, "GRCK",  0, 14,   8, 16,  100, 0, 0,  7, 0, MF_NOGRAVITY, 0, b_golem_rock },
  { "bore_ball",      "Bore bolt",            -1, MT_HEADSHOT,  "BRBL",  0, 30,   6,  8,  100, 0, 0,  6, 0, 0, 0, NULL },
  { "mecha_rocket",   "Mecha rocket",         -1, MT_ROCKET,    "MCRK",  0, 25,  11,  8,  100, 0, 0, 20, 0, 0, 0, NULL },
  { "mecha_seeker",   "Mecha heat-seeker",    -1, MT_TRACER,    "MCSK",  0, 14,  11,  8,  100, 0, 0, 10, 0, 0, 0, NULL },
  { "mecha_flame",    "Mecha flame",          -1, MT_TROOPSHOT, "MCFL",  0, 14,   8, 12,  100, 0, 0,  2, 0, 0, 0, b_mecha_flame },
  { "beetle_acid",    "Giant beetle acid",    -1, MT_TROOPSHOT, "BTAC",  0, 16,   8, 12,  100, 0, 0,  5, 0, 0, 0, NULL },
  { "heart_fireball", "Heart fireball",       -1, MT_TROOPSHOT, "HFBL",  0, 14,   6,  8,  100, 0, 0,  6, 0, 0, 0, NULL },

  // ---- Modern ----
  { "rockettrooper",  "Rocket trooper",    21001, MT_SHOTGUY,   "RTRP", 80,  6,  20, 56,  100, 100, 8, 0, 0, 0, 0, b_rockettrooper },
  { "shocktrooper",   "Shock trooper",     21002, MT_WOLFSS,    "STRP", 60, 14,  20, 56,  100, 120, 6, 0, 0, 0, 0, b_shocktrooper },
  // ---- Medieval ----
  { "werewolf",       "Werewolf",          21003, MT_WEREWOLF,  "WWLF", 140, 12, 30, 56,  400, 120, 8, 0, 0, 0, 0, b_werewolf },
  { "fireelemental",  "Fire elemental",    21004, MT_FIREELEM,     "FELM", 80, 10,  20, 56,  100, 160, 6, 0, 0, 0, 0, b_fireelemental },
  { "madwizard",      "Mad wizard",        21005, MT_MADWIZARD,    "MWIZ", 700, 12, 20, 56,  500, 40, 8, 0, 0, 0, 0, b_madwizard },
  { "apprentice",     "Apprentice",        21006, MT_APPRENTICE,    "APPR", 450,  6, 24, 64, 1000, 50, 8, 0, 0, 0, 0, b_apprentice },
  { "minister",       "Minister",          21007, MT_MINISTER,   "MINI", 900,  6, 24, 64, 1000, 30, 8, 0, MF_FLOAT | MF_NOGRAVITY, 0, 0, b_minister },
  { "golem",          "Golem",             21008, MT_GOLEM,     "GLEM", 700,  5, 36, 72, 1000, 60, 8, 0, 0, 0, 0, b_golem },
  // ---- Alien ----
  { "mindeater",      "Mindeater",         21009, MT_MINDEATER,      "MNDE", 500, 14, 20, 56,  500, 20, 8, 0, 0, 0, 0, b_mindeater },
  { "aegis",          "Aegis",             21010, MT_HATRED,     "AEGS",  30, 10, 14, 28,   50, 220, 4, 0, MF_COUNTKILL, 0, 0, b_aegis },
  { "bore",           "Bore",              21011, MT_BORE,      "BORE", 300, 12, 26, 44,  300, 128, 8, 0, 0, 0, 0, b_bore },
  { "hatred",         "Hatred",            21013, MT_HATRED,     "HTRD",  60, 14, 14, 40,   40, 256, 0, 3, 0, 0, 0, b_hatred },
  { "conjurer",       "Conjurer",          21012, MT_CONJURER,      "CNJR", 300, 10, 31, 56,  400, 128, 8, 0, 0, 0, 0, b_conjurer },
  // ---- bosses ----
  { "mecha",          "Mecha",             21014, MT_MECHA,    "MECH", 4000, 16, 40, 110, 1000, 20, 8, 0, 0, 0, 0, b_mecha },
  { "beetle",         "Giant beetle",      21015, MT_BEETLE,    "BEET", 1500,  6, 80, 90, 1000, 20, 8, 0, 0, 0, 0, b_beetle },
  { "heart",          "Articulator heart", 21016, MT_CORE, "HART", 400,  0, 24, 56, 10000000, 100, 8, 0, 0, 0, 0, b_heart },
  { "core",           "Articulator core",  21017, MT_CORE, "CORE", 1200, 0, 32, 72, 10000000, 100, 8, 0, 0, 0, 0, b_core },
};

#define NUM_ROSTER ((int) (sizeof(roster) / sizeof(roster[0])))

static spec_t* spec_by_key(const char* key)
{
  int i;

  for (i = 0; i < NUM_ROSTER; ++i)
    if (!strcasecmp(roster[i].key, key))
      return &roster[i];
  return NULL;
}

// the mobj type of a roster key
static int T(const char* key)
{
  spec_t* sp = spec_by_key(key);

  return sp ? sp->type : -1;
}

int bg_RosterTypeByName(const char* name)
{
  spec_t* sp = spec_by_key(name);

  return (sp && sp->type > 0) ? sp->type : -1;
}

//
// Foe tables (stock hitscan monsters): damage and spread in percent
//

static short* foe_damage;
static short* foe_spread;
static int foe_n;

int bg_FoeDamage(const mobj_t* attacker, int damage)
{
  int pct, d;

  if (!attacker || attacker->type < 0 || attacker->type >= foe_n || !foe_damage)
    return damage;
  pct = foe_damage[attacker->type];
  if (!pct || pct == 100)
    return damage;
  d = damage * pct / 100;
  return d < 1 ? 1 : d;
}

int bg_FoeSpreadPct(const mobj_t* attacker)
{
  int pct;

  if (!attacker || attacker->type < 0 || attacker->type >= foe_n || !foe_spread)
    return 100;
  pct = foe_spread[attacker->type];
  return pct ? pct : 100;
}

//
// Actions.  They read their numbers from the state's args.
//

static void set_missile_owner(mobj_t* mo, mobj_t* actor)
{
  P_SetTarget(&mo->target, actor);
  P_SetTarget(&mo->tracer, actor->target);
}

// A_BgAttack: a melee blow when the target is in reach, else projectiles.
//   0 projectile type (-1: none)   1,2 melee damage base and dice sides (0: no melee)
//   3 drain percent                4 projectile count          5 fan in degrees (fixed point)
//   6 alternative type (-1: none)  7 chance (of 256) to fire the alternative instead
static void A_BgAttack(mobj_t* actor)
{
  const statearg_t* a = actor->state->args;
  int type = (int) a[0], mbase = (int) a[1], msides = (int) a[2], drain = (int) a[3];
  int count = (int) a[4], alt = (int) a[6], altchance = (int) a[7];
  long long fan = a[5];
  int i;

  if (!actor->target)
    return;
  A_FaceTarget(actor);
  if (mbase > 0 && msides > 0 && bg_CheckMeleeRange(actor))
  {
    int damage = (P_Random(pr_mbf21) % msides + 1) * mbase;

    S_StartMobjSound(actor, sfx_claw);
    P_DamageMobj(actor->target, actor, actor, damage);
    return;
  }
  if (type < 0)
    return;
  if (count < 1)
    count = 1;
  for (i = 0; i < count; ++i)
  {
    int t = type;
    mobj_t* mo;

    if (alt >= 0 && P_Random(pr_mbf21) < altchance)
      t = alt;
    mo = P_SpawnMissile(actor, actor->target, t);
    if (!mo)
      continue;
    if (count > 1 && fan)
    {
      long long off = fan * (2 * i - (count - 1)) / (2 * (count - 1));
      int an;

      mo->angle += (unsigned int) (((int64_t) off << 16) / 360);
      an = mo->angle >> ANGLETOFINESHIFT;
      mo->momx = FixedMul(mo->info->speed, finecosine[an]);
      mo->momy = FixedMul(mo->info->speed, finesine[an]);
    }
    mo->bg_drain = (short) drain;
    P_SetTarget(&mo->tracer, actor->target); // homing missiles follow what was fired at
  }
}

// A_BgLob: lobs rocks in an arc.  The projectile has gravity; its speed is worked out so that it
// comes down on the target.
//   0 type   1 horizontal speed (map units per tic)   2 random scatter in degrees (fixed point)
//   3 sideways offset of the arm (map units, fixed point)   4 count (rocks go to alternate arms)
static void A_BgLob(mobj_t* actor)
{
  const statearg_t* a = actor->state->args;
  int type = (int) a[0], hspeed = (int) a[1], count = (int) a[4], i;
  long long scatter = a[2];
  fixed_t side = (fixed_t) a[3];

  if (!actor->target || type < 0 || hspeed < 1)
    return;
  A_FaceTarget(actor);
  if (count < 1)
    count = 1;
  for (i = 0; i < count; ++i)
  {
    mobj_t* mo;
    angle_t an = actor->angle;
    fixed_t ox = 0, oy = 0, dx, dy, dz, dist, g, v0;
    int flight, fan;
    angle_t aim;
    int sgn = (i & 1) ? -1 : 1;

    if (count > 1 || side)
    {
      unsigned int perp = (an - ANG90) >> ANGLETOFINESHIFT;

      ox = FixedMul(side * sgn, finecosine[perp]);
      oy = FixedMul(side * sgn, finesine[perp]);
    }
    mo = P_SpawnMobj(actor->x + ox, actor->y + oy, actor->z + (actor->height * 3 / 4), type);
    if (!mo)
      continue;
    set_missile_owner(mo, actor);
    if (mo->info->seesound)
      S_StartMobjSound(mo, mo->info->seesound);

    dx = actor->target->x - mo->x;
    dy = actor->target->y - mo->y;
    dz = actor->target->z + (actor->target->height >> 1) - mo->z;
    if (actor->target->flags & MF_SHADOW)
      dx += (P_Random(pr_shadow) - P_Random(pr_shadow)) << 20;
    if (scatter)
    {
      int r = P_Random(pr_mbf21) - P_Random(pr_mbf21);

      aim = R_PointToAngle2(0, 0, dx, dy) + (angle_t) (((int64_t) (scatter * r / 255) << 16) / 360);
      dist = P_AproxDistance(dx, dy);
      dx = FixedMul(dist, finecosine[aim >> ANGLETOFINESHIFT]);
      dy = FixedMul(dist, finesine[aim >> ANGLETOFINESHIFT]);
    }
    dist = P_AproxDistance(dx, dy);
    flight = (dist / FRACUNIT) / hspeed;
    if (flight < 6)
      flight = 6;
    if (flight > 40)
      flight = 40;
    fan = flight; // tics in the air
    mo->momx = dx / fan;
    mo->momy = dy / fan;
    g = P_MobjGravity(mo);
    // z(n) = z0 + n*v0 - g*n*(n-1)/2   =>   v0 = (dz + g*n*(n-1)/2) / n
    v0 = (dz + (fixed_t) (((long long) g * fan * (fan - 1)) / 2)) / fan;
    mo->momz = v0;
    mo->angle = R_PointToAngle2(0, 0, mo->momx, mo->momy);
  }
}

// A_BgFlame: one tongue of the Mecha's flamethrower, with a little scatter.
static void A_BgFlame(mobj_t* actor)
{
  mobj_t* mo;
  int type = (int) actor->state->args[0];
  int an;

  if (!actor->target || type < 0)
    return;
  A_FaceTarget(actor);
  mo = P_SpawnMissile(actor, actor->target, type);
  if (!mo)
    return;
  mo->angle += (angle_t) ((P_Random(pr_mbf21) - P_Random(pr_mbf21)) * 0x50000);
  an = mo->angle >> ANGLETOFINESHIFT;
  mo->momx = FixedMul(mo->info->speed, finecosine[an]);
  mo->momy = FixedMul(mo->info->speed, finesine[an]);
  mo->momz += (P_Random(pr_mbf21) - P_Random(pr_mbf21)) * 4000;
}

// A_BgBlast: a radius attack.   0 damage   1 radius   2 source: 0 = this thing, 1 = what fired it
// 3: 1 = also stop being a target (the explosion of a Hatred)
static void A_BgBlast(mobj_t* actor)
{
  const statearg_t* a = actor->state->args;
  mobj_t* source = (a[2] && actor->target) ? actor->target : actor;

  if (a[3])
    actor->flags &= ~(MF_SOLID | MF_SHOOTABLE | MF_FLOAT);
  S_StartMobjSound(actor, sfx_barexp);
  P_RadiusAttack(actor, source, (int) a[0], (int) a[1], 0);
}

static void A_BgStop(mobj_t* actor)
{
  actor->momx = actor->momy = 0;
}

//
// Werewolf: runs like a demon, and leaps to close the distance.
//

// run states: 0 min distance, 1 max distance (map units), 2 -, 3 chance of 256 per call, 5 the leap state
static void A_BgWolfChase(mobj_t* actor)
{
  const statearg_t* a = actor->state->args;

  if (actor->bg_cd > 0)
    --actor->bg_cd;
  if (actor->bg_cd == 0 && actor->target && actor->target->health > 0 && a[5] > 0 && !actor->reactiontime)
  {
    fixed_t dist = P_AproxDistance(actor->target->x - actor->x, actor->target->y - actor->y);
    fixed_t dz = actor->target->z - actor->z;

    if (dz < 0)
      dz = -dz;
    if (dist >= (fixed_t) a[0] * FRACUNIT && dist <= (fixed_t) a[1] * FRACUNIT && dz < 120 * FRACUNIT &&
        actor->z <= actor->floorz && P_CheckSight(actor, actor->target) && P_Random(pr_mbf21) < (int) a[3])
    {
      P_SetMobjState(actor, (int) a[5]);
      return;
    }
  }
  A_Chase(actor);
}

// 0 horizontal speed (units/tic)   1 longest flight (tics)   2 cooldown (tics)
static void A_BgLeap(mobj_t* actor)
{
  const statearg_t* a = actor->state->args;
  int hspeed = (int) a[0], maxflight = (int) a[1];
  fixed_t dx, dy, dz, dist, g;
  int flight;
  angle_t an;

  if (!actor->target || hspeed < 1)
    return;
  dx = actor->target->x - actor->x;
  dy = actor->target->y - actor->y;
  dz = actor->target->z - actor->z;
  dist = P_AproxDistance(dx, dy);
  flight = (dist / FRACUNIT) / hspeed;
  if (flight < 8)
    flight = 8;
  if (flight > maxflight)
    flight = maxflight;
  an = R_PointToAngle2(actor->x, actor->y, actor->target->x, actor->target->y);
  actor->angle = an;
  an >>= ANGLETOFINESHIFT;
  {
    fixed_t h = dist / flight;

    if (h > hspeed * FRACUNIT)
      h = hspeed * FRACUNIT;
    actor->momx = FixedMul(h, finecosine[an]);
    actor->momy = FixedMul(h, finesine[an]);
  }
  g = P_MobjGravity(actor);
  actor->momz = (dz + (fixed_t) (((long long) g * flight * (flight - 1)) / 2)) / flight;
  if (actor->momz < 4 * FRACUNIT)
    actor->momz = 4 * FRACUNIT;
  if (actor->momz > 14 * FRACUNIT)
    actor->momz = 14 * FRACUNIT;
  actor->bg_cd = (short) a[2];
  actor->bg_aux = 0;
  S_StartMobjSound(actor, actor->info->attacksound ? actor->info->attacksound : sfx_sgtatk);
}

// touching down: no pause - bite at once when the target is in reach, otherwise straight on with the chase
static void A_BgLand(mobj_t* actor)
{
  A_BgStop(actor);
  if (actor->target && actor->target->health > 0 && actor->info->meleestate && P_CheckMeleeRange(actor))
    P_SetMobjState(actor, actor->info->meleestate);
}

// in the air: wait for the ground.   0 the landing state
static void A_BgLeapAir(mobj_t* actor)
{
  ++actor->bg_aux;
  if (actor->bg_aux > 3 && (actor->z <= actor->floorz || actor->bg_aux > 70))
  {
    A_BgStop(actor);
    P_SetMobjState(actor, (int) actor->state->args[0]);
  }
}

//
// Aegis: floats to a corpse, climbs into it and is gone; the corpse gets up with double health, damage
// and speed.  Without a corpse it drifts at the player and nips at him.
//

static dboolean aegis_candidate(const mobj_t* corpse)
{
  return (corpse->flags & MF_CORPSE) && corpse->tics == -1 && corpse->health <= 0 &&
         corpse->info->raisestate > 0 && (corpse->info->flags & MF_COUNTKILL) &&
         !(corpse->flags & MF_SOLID) && corpse->info->height > 0;
}

static dboolean aegis_fits(mobj_t* corpse, mobj_t* actor)
{
  int height = corpse->height, radius = corpse->radius;
  uint64_t aflags = actor ? actor->flags : 0;
  dboolean ok;

  if (actor)
    actor->flags &= ~(MF_SOLID | MF_SHOOTABLE); // the Aegis itself is no obstacle

  corpse->height = corpse->info->height;
  corpse->radius = corpse->info->radius;
  corpse->flags |= MF_SOLID;
  ok = P_CheckPosition(corpse, corpse->x, corpse->y);
  corpse->height = height;
  corpse->radius = radius;
  corpse->flags &= ~MF_SOLID;
  if (actor)
    actor->flags = aflags;
  return ok;
}

static mobj_t* aegis_find(mobj_t* actor, fixed_t range)
{
  thinker_t* th;
  mobj_t* best = NULL;
  fixed_t best_dist = range;

  for (th = thinkercap.next; th != &thinkercap; th = th->next)
  {
    mobj_t* m;
    fixed_t d;

    if (th->function != P_MobjThinker)
      continue;
    m = (mobj_t*) th;
    if (m == actor || !aegis_candidate(m))
      continue;
    d = P_AproxDistance(m->x - actor->x, m->y - actor->y);
    if (d >= best_dist)
      continue;
    if (!P_CheckSight(actor, m) || !aegis_fits(m, actor))
      continue;
    best = m;
    best_dist = d;
  }
  return best;
}

// run states: 0 search every this many tics   1 range (map units)   2 the seek state
static void A_BgAegisChase(mobj_t* actor)
{
  const statearg_t* a = actor->state->args;

  if (actor->bg_cd > 0)
    --actor->bg_cd;
  if (actor->bg_cd == 0 && a[2] > 0)
  {
    mobj_t* corpse;

    actor->bg_cd = (short) (a[0] > 0 ? a[0] : 12);
    corpse = aegis_find(actor, (fixed_t) a[1] * FRACUNIT);
    if (corpse)
    {
      P_SetTarget(&actor->tracer, corpse);
      actor->bg_aux = 0;
      P_SetMobjState(actor, (int) a[2]);
      return;
    }
  }
  A_Chase(actor);
}

static void aegis_revive(mobj_t* actor, mobj_t* corpse)
{
  mobjinfo_t* info = corpse->info;

  corpse->momx = corpse->momy = 0;
  S_StartMobjSound(corpse, sfx_slop);
  P_SetMobjState(corpse, info->raisestate);
  corpse->height = info->height;
  corpse->radius = info->radius;
  corpse->flags = (info->flags & ~MF_FRIEND) | (actor->flags & MF_FRIEND) | MF_RESSURECTED;
  dsda_WatchResurrection(corpse, actor);
  if (dsda_IsCountedKill(corpse))
    ++totallive;
  corpse->health = P_MobjSpawnHealth(corpse) * 2;
  corpse->bg_flags |= BG_BOOST;
  corpse->color = 0;
  P_SetTarget(&corpse->target, NULL);
  P_SetTarget(&corpse->lastenemy, NULL);
  corpse->flags &= ~MF_JUSTHIT;
  P_UpdateThinker(&corpse->thinker);

  // the Aegis is spent: it simply goes (it does not count as a kill)
  P_SpawnMobj(actor->x, actor->y, actor->z, MT_IFOG);
  P_SetTarget(&actor->tracer, NULL);
  P_SetMobjState(actor, S_NULL);
}

// seek states: 0 reach (map units)   1 give up after this many calls   2 the state to return to
static void A_BgAegisSeek(mobj_t* actor)
{
  const statearg_t* a = actor->state->args;
  mobj_t* c = actor->tracer;
  fixed_t dx, dy, dz, dist, speed;
  int an;

  if (!c || !aegis_candidate(c) || ++actor->bg_aux > (int) a[1])
  {
    P_SetTarget(&actor->tracer, NULL);
    actor->momx = actor->momy = actor->momz = 0;
    actor->bg_cd = 60;
    P_SetMobjState(actor, (int) a[2]);
    return;
  }
  dx = c->x - actor->x;
  dy = c->y - actor->y;
  dz = c->z + (c->info->height >> 2) - actor->z;
  dist = P_AproxDistance(P_AproxDistance(dx, dy), dz);
  if (dist <= (fixed_t) a[0] * FRACUNIT + c->info->radius)
  {
    if (aegis_fits(c, actor))
      aegis_revive(actor, c);
    else
    {
      P_SetTarget(&actor->tracer, NULL);
      actor->momx = actor->momy = actor->momz = 0;
      actor->bg_cd = 60;
      P_SetMobjState(actor, (int) a[2]);
    }
    return;
  }
  speed = actor->info->speed * FRACUNIT;
  an = R_PointToAngle2(0, 0, dx, dy) >> ANGLETOFINESHIFT;
  actor->angle = R_PointToAngle2(0, 0, dx, dy);
  {
    fixed_t flat = P_AproxDistance(dx, dy);
    fixed_t len = P_AproxDistance(flat, dz);

    if (len < FRACUNIT)
      len = FRACUNIT;
    actor->momx = FixedMul(FixedDiv(flat, len), FixedMul(speed, finecosine[an]));
    actor->momy = FixedMul(FixedDiv(flat, len), FixedMul(speed, finesine[an]));
    actor->momz = FixedMul(FixedDiv(dz, len), speed);
  }
}

//
// Conjurer: sends out Hatreds (usually) or Aegises.   0 type A   1 type B   2 chance of A (of 256)
// 3 cap on A and B together   4 A charges at once   5 B charges at once
//

static int bg_charge_speed = 28;
static int bg_charge_explode = 50;

static void bg_Charge(mobj_t* skull, int speed, int explode_pct)
{
  mobj_t* dest = skull->target;
  angle_t an;
  fixed_t dist;
  int tics;

  if (!dest)
    return;
  skull->flags |= MF_SKULLFLY;
  S_StartMobjSound(skull, skull->info->attacksound);
  A_FaceTarget(skull);
  an = skull->angle >> ANGLETOFINESHIFT;
  skull->momx = FixedMul(speed * FRACUNIT, finecosine[an]);
  skull->momy = FixedMul(speed * FRACUNIT, finesine[an]);
  dist = P_AproxDistance(dest->x - skull->x, dest->y - skull->y);
  tics = dist / (speed * FRACUNIT);
  if (tics < 1)
    tics = 1;
  skull->momz = (dest->z + (dest->height >> 1) - skull->z) / tics;
  if (explode_pct > 0 && (P_Random(pr_skullfly) % 100) < explode_pct)
    skull->bg_flags |= BG_KAMIKAZE;
  else
    skull->bg_flags &= ~BG_KAMIKAZE;
}

// 0 charge speed (map units/tic)   1 chance (percent) that the charge ends in an explosion
static void A_BgSkullAttack(mobj_t* actor)
{
  bg_Charge(actor, (int) actor->state->args[0], (int) actor->state->args[1]);
}

dboolean bg_SkullImpact(mobj_t* skull)
{
  if (!(skull->bg_flags & BG_KAMIKAZE))
    return false;
  skull->bg_flags &= ~BG_KAMIKAZE;
  skull->flags &= ~MF_SKULLFLY;
  skull->momx = skull->momy = skull->momz = 0;
  if (skull->info->raisestate <= 0)
    return false;
  P_SetMobjState(skull, skull->info->raisestate);
  return true;
}

static void A_BgSummon(mobj_t* actor)
{
  const statearg_t* a = actor->state->args;
  int ta = (int) a[0], tb = (int) a[1], cap = (int) a[3];
  int type, count = 0;
  thinker_t* th;
  mobj_t* mo;
  angle_t an;
  fixed_t x, y, z;
  int prestep;

  if (!actor->target || ta < 0)
    return;
  A_FaceTarget(actor);
  for (th = thinkercap.next; th != &thinkercap; th = th->next)
    if (th->function == P_MobjThinker)
    {
      const mobj_t* m = (const mobj_t*) th;

      if ((m->type == ta || m->type == tb) && m->health > 0)
        ++count;
    }
  if (count >= cap)
    return;

  type = (tb >= 0 && P_Random(pr_mbf21) >= (int) a[2]) ? tb : ta;
  an = actor->angle >> ANGLETOFINESHIFT;
  prestep = 4 * FRACUNIT + 3 * (actor->info->radius + mobjinfo[type].radius) / 2;
  x = actor->x + FixedMul(prestep, finecosine[an]);
  y = actor->y + FixedMul(prestep, finesine[an]);
  z = actor->z + 8 * FRACUNIT;
  if (Check_Sides(actor, x, y))
    return;
  mo = P_SpawnMobj(x, y, z, type);
  if (mo->z > mo->subsector->sector->ceilingheight - mo->height || mo->z < mo->subsector->sector->floorheight)
  {
    P_DamageMobj(mo, actor, actor, 10000);
    return;
  }
  mo->flags = (mo->flags & ~MF_FRIEND) | (actor->flags & MF_FRIEND);
  P_UpdateThinker(&mo->thinker);
  if (!P_TryMove(mo, mo->x, mo->y, false))
  {
    P_DamageMobj(mo, actor, actor, 10000);
    return;
  }
  P_SetTarget(&mo->target, actor->target);
  if ((type == ta && a[4]) || (type == tb && a[5]))
    bg_Charge(mo, bg_charge_speed, bg_charge_explode);
  else
    P_SetMobjState(mo, mo->info->seestate);
}

//
// Mecha: picks flames when you are close, else rockets or heat-seekers.
//   0 flame range (map units)   1 chance (of 256) of a seeker volley   2 rocket volley   3 seeker volley   4 flames
//

static void A_BgMechaPick(mobj_t* actor)
{
  const statearg_t* a = actor->state->args;
  fixed_t dist;

  if (!actor->target)
    return;
  A_FaceTarget(actor);
  dist = P_AproxDistance(actor->target->x - actor->x, actor->target->y - actor->y);
  if (dist <= (fixed_t) a[0] * FRACUNIT)
    P_SetMobjState(actor, (int) a[4]);
  else if (P_Random(pr_mbf21) < (int) a[1])
    P_SetMobjState(actor, (int) a[3]);
  else
    P_SetMobjState(actor, (int) a[2]);
}

//
// Draining shots
//

void bg_DrainHeal(mobj_t* target, mobj_t* inflictor, mobj_t* source, int damage)
{
  int dealt = damage < target->health ? damage : target->health;
  int heal;

  if (dealt <= 0 || source->health <= 0)
    return;
  heal = dealt * inflictor->bg_drain / 100;
  if (inflictor->bg_dmgscale)
    heal /= 2; // a parried bolt heals the player who turned it, at half the rate
  if (heal <= 0)
    return;
  if (source->player)
  {
    player_t* p = source->player;
    int cap = bg_ClassHealthCap(p, BG_HP_NORMAL, 100);

    if (p->health < cap)
    {
      p->health += heal;
      if (p->health > cap)
        p->health = cap;
      source->health = p->health;
    }
  }
  else
  {
    int cap = P_MobjSpawnHealth(source) * ((source->bg_flags & BG_BOOST) ? 2 : 1);

    if (source->health < cap)
    {
      source->health += heal;
      if (source->health > cap)
        source->health = cap;
    }
  }
}

//
// Builders
//

static void st_actions_to_args(int s, long long a0, long long a1, long long a2, long long a3,
                               long long a4, long long a5, long long a6, long long a7)
{
  st_args(s, a0, a1, a2, a3, a4, a5, a6, a7);
}

// a shared "fire" state: projectile `type`, optional melee, drain, count/fan, alternative
static void make_attack_args(int s, int type, int mbase, int msides, int drain, int count, long long fan,
                             int alt, int altchance)
{
  states[s].action = ACT(A_BgAttack);
  st_actions_to_args(s, type, mbase, msides, drain, count, fan, alt, altchance);
}

// ---- projectiles ----

static void b_trooper_rocket(spec_t* sp)
{
  // the explosion is smaller than a Cyberdemon's and hurts its shooter's enemies only by what it says
  int s = find_action(ACT(A_Explode));

  if (s)
  {
    states[s].action = ACT(A_BgBlast);
    st_args(s, V(sp->key, "blast", 60), V(sp->key, "blastradius", 72), 1, 0, 0, 0, 0, 0);
  }
}

static void b_golem_rock(spec_t* sp)
{
  // a rock has weight: gravity is on and it comes down where it was aimed
  mobjinfo[sp->type].flags &= ~MF_NOGRAVITY;
}

static void b_mecha_flame(spec_t* sp)
{
  // a tongue of flame lasts about 18 tics and then burns out
  mobjinfo_t* info = &mobjinfo[sp->type];
  int life = V(sp->key, "life", 18), n = life / 3, i, first = 0, prev = 0;
  int spr = states[info->spawnstate].sprite;

  if (n < 1)
    n = 1;
  for (i = 0; i < n; ++i)
  {
    int s = st_add(spr, (i & 1) | FF_FB, 3, NULL, NEXT_);

    if (!first)
      first = s;
    prev = s;
  }
  states[prev].nextstate = info->deathstate;
  info->spawnstate = first;
}

// ---- Modern ----

static void b_rockettrooper(spec_t* sp)
{
  // slow, tough, and it takes its time: one rocket at a time
  int a1 = CL(S_SPOS_ATK1), a2 = CL(S_SPOS_ATK2), a3 = CL(S_SPOS_ATK3);

  states[a1].tics = V(sp->key, "windup", 40);
  make_attack_args(a2, T("trooper_rocket"), 0, 0, 0, 1, 0, -1, 0);
  states[a2].tics = 10;
  states[a3].tics = V(sp->key, "recover", 28);
}

static void b_shocktrooper(spec_t* sp)
{
  // the fastest human: a volley of plasma bolts
  mobjinfo_t* info = &mobjinfo[sp->type];
  int shots = V(sp->key, "volley", 5), gap = V(sp->key, "gap", 4), i;
  int spr = states[CL(S_SSWV_ATK1)].sprite;
  int a1 = CL(S_SSWV_ATK1), a2 = CL(S_SSWV_ATK2);
  int last = a2, s;

  info->firerange = V(sp->key, "firerange", 640);   // walks up to this close before it starts shooting
  states[a1].tics = V(sp->key, "windup", 14);
  states[a2].tics = 6;
  states[a2].nextstate = 0;
  for (i = 0; i < shots; ++i)
  {
    s = st_add(spr, 6 | FF_FB, 3, NULL, NEXT_);
    make_attack_args(s, T("shock_bolt"), 0, 0, 0, 1, 0, -1, 0);
    states[last].nextstate = s;
    last = s;
    s = st_add(spr, 5, gap, ACT(A_FaceTarget), NEXT_);
    states[last].nextstate = s;
    last = s;
  }
  states[last].nextstate = info->seestate;
  states[last].tics = V(sp->key, "recover", 20);
  states[last].action = NULL;
}

// ---- Medieval ----

static void b_werewolf(spec_t* sp)
{
  mobjinfo_t* info = &mobjinfo[sp->type];
  int spr = states[CL(S_WWLF_ATK1)].sprite;
  int crouch, launch, air, land, o;

  land = st_add(spr, 4, V(sp->key, "landing", 0), ACT(A_BgLand), info->seestate);
  air = st_add(spr, 2, 1, ACT(A_BgLeapAir), NEXT_);
  states[air].nextstate = air;
  st_args(air, land, 0, 0, 0, 0, 0, 0, 0);
  launch = st_add(spr, 5, 2, ACT(A_BgLeap), air);
  st_args(launch, V(sp->key, "leapspeed", 20), V(sp->key, "leapflight", 24), V(sp->key, "leapcooldown", 70), 0, 0, 0, 0, 0);
  crouch = st_add(spr, 4, V(sp->key, "crouch", 9), ACT(A_FaceTarget), launch);

  replace_action(ACT(A_Chase), ACT(A_BgWolfChase));
  for (o = 1; o < cmap_n; ++o)
    if (cmap[o] && states[cmap[o]].action == ACT(A_BgWolfChase))
      st_args(cmap[o], V(sp->key, "leapmin", 224), V(sp->key, "leapmax", 640), 0,
              V(sp->key, "leapchance", 96), 0, crouch, 0, 0);
}

static void b_fireelemental(spec_t* sp)
{
  mobjinfo_t* info = &mobjinfo[sp->type];
  int shots = V(sp->key, "volley", 3), gap = V(sp->key, "gap", 5), i;
  int spr = states[CL(S_FELM_ATK1)].sprite;
  int a1 = CL(S_FELM_ATK1), a2 = CL(S_FELM_ATK2), a3 = CL(S_FELM_ATK3);
  int last = a2, s;
  int melee_aim, melee_hit;

  // melee when close (a claw like an imp's)
  melee_hit = st_add(spr, 6, 6, NULL, info->seestate);
  make_attack_args(melee_hit, -1, V(sp->key, "clawbase", 3), 8, 0, 1, 0, -1, 0);
  melee_aim = st_add(spr, 5, 8, ACT(A_FaceTarget), melee_hit);
  states[a1].tics = V(sp->key, "windup", 10);
  info->meleestate = st_add(spr, 4, 8, ACT(A_FaceTarget), melee_aim);

  // a volley of fireballs
  states[a2].tics = 5;
  for (i = 0; i < shots; ++i)
  {
    s = st_add(spr, 6 | 0, 6, NULL, NEXT_);
    make_attack_args(s, T("elem_fireball"), 0, 0, 0, 1, 0, -1, 0);
    states[last].nextstate = s;
    last = s;
    s = st_add(spr, 5, gap, ACT(A_FaceTarget), NEXT_);
    states[last].nextstate = s;
    last = s;
  }
  states[last].nextstate = info->seestate;
  states[last].action = NULL;
  states[last].tics = V(sp->key, "recover", 8);
  (void) a3;
}

static void b_madwizard(spec_t* sp)
{
  // ranged: fireballs, some of which follow the target.  The revenant's fists stay.
  int m2 = CL(S_MWIZ_MISS2), m3 = CL(S_MWIZ_MISS3);

  states[m2].tics = V(sp->key, "windup", 12);
  make_attack_args(m3, T("wizard_bolt"), 0, 0, 0, 1, 0, T("wizard_seeker"), V(sp->key, "seekerchance", 128));
  states[m3].tics = V(sp->key, "recover", 12);
}

static void drain_attack(spec_t* sp, int a3, const char* bolt)
{
  // the knight / baron attack becomes a claw when close and a draining bolt otherwise
  make_attack_args(a3, T(bolt), V(sp->key, "clawbase", 10), 8, V(sp->key, "drain", 100), 1, 0, -1, 0);
}

static void b_apprentice(spec_t* sp)
{
  drain_attack(sp, CL(S_APPR_ATK3), "drain_bolt");
}

static void b_minister(spec_t* sp)
{
  drain_attack(sp, CL(S_MINI_ATK3), "minister_bolt");
}

static void b_golem(spec_t* sp)
{
  int s1 = CL(S_GLEM_ATK2), s2 = CL(S_GLEM_ATK5), s3 = CL(S_GLEM_ATK8);
  long long lob[8];

  lob[0] = T("golem_rock");
  lob[1] = V(sp->key, "rockspeed", 14);
  lob[2] = DEGF(V(sp->key, "rockscatter", 5));
  lob[3] = (long long) V(sp->key, "arm", 40) * FRACUNIT;
  lob[4] = 2;
  states[s1].action = ACT(A_BgLob);
  st_args(s1, lob[0], lob[1], lob[2], lob[3], lob[4], 0, 0, 0);
  states[s2].action = ACT(A_BgLob);
  st_args(s2, lob[0], lob[1], lob[2], lob[3], lob[4], 0, 0, 0);
  states[s3].action = V(sp->key, "volleys", 2) > 2 ? ACT(A_BgLob) : NULL; // the third volley is optional
  if (states[s3].action)
    st_args(s3, lob[0], lob[1], lob[2], lob[3], lob[4], 0, 0, 0);
}

// ---- Alien ----

static void b_mindeater(spec_t* sp)
{
  mobjinfo_t* info = &mobjinfo[sp->type];

  replace_action(ACT(A_VileChase), ACT(A_Chase)); // it does not raise the dead: that is the Aegis' job
  info->raisestate = 0;
  vanish_death(info->deathstate);
  (void) sp;
}

static void b_aegis(spec_t* sp)
{
  mobjinfo_t* info = &mobjinfo[sp->type];
  int spr = states[info->spawnstate].sprite;
  int seek1, seek2, o, m1, m2;

  // seeking a corpse: flies straight at it
  seek1 = st_add(spr, 0 | FF_FB, 2, ACT(A_BgAegisSeek), NEXT_);
  seek2 = st_add(spr, 1 | FF_FB, 2, ACT(A_BgAegisSeek), seek1);
  states[seek1].nextstate = seek2;
  st_args(seek1, V(sp->key, "reach", 24), V(sp->key, "giveup", 90), info->seestate, 0, 0, 0, 0, 0);
  st_args(seek2, V(sp->key, "reach", 24), V(sp->key, "giveup", 90), info->seestate, 0, 0, 0, 0, 0);

  replace_action(ACT(A_Chase), ACT(A_BgAegisChase));
  for (o = 1; o < cmap_n; ++o)
    if (cmap[o] && states[cmap[o]].action == ACT(A_BgAegisChase))
      st_args(cmap[o], V(sp->key, "search", 12), V(sp->key, "range", 1600), seek1, 0, 0, 0, 0, 0);

  // a weak nip when it has nothing to raise
  m2 = st_add(spr, 3 | FF_FB, 6, NULL, info->seestate);
  make_attack_args(m2, -1, V(sp->key, "nipbase", 2), 4, 0, 1, 0, -1, 0);
  m1 = st_add(spr, 2 | FF_FB, 6, ACT(A_FaceTarget), m2);
  info->meleestate = m1;
  info->missilestate = 0;
  info->meleerange = 40 * FRACUNIT;
}

static void b_bore(spec_t* sp)
{
  int s = CL(S_BORE_ATK3);

  make_attack_args(s, T("bore_ball"), V(sp->key, "bitebase", 10), 6, 0, 1, 0, -1, 0);
  vanish_death(mobjinfo[sp->type].deathstate);
}

static void b_conjurer(spec_t* sp)
{
  int s = CL(S_CNJR_ATK4);
  int dead = CL(S_CNJR_DIE5);

  states[s].action = ACT(A_BgSummon);
  st_args(s, T("hatred"), T("aegis"), V(sp->key, "hatredchance", 171), V(sp->key, "cap", 60), 1, 0, 0, 0);
  if (states[dead].action == ACT(A_PainDie))
    states[dead].action = NULL; // no skulls when it dies
  vanish_death(mobjinfo[sp->type].deathstate);
}

static void b_hatred(spec_t* sp)
{
  mobjinfo_t* info = &mobjinfo[sp->type];
  int atk = CL(S_HTRD_ATK2), wind = CL(S_HTRD_ATK1);
  int e1, e2, e3, spr = SPR_MISL;

  bg_charge_speed = V(sp->key, "chargespeed", 28);
  bg_charge_explode = V(sp->key, "explodepct", 50);
  states[atk].action = ACT(A_BgSkullAttack);
  st_args(atk, bg_charge_speed, bg_charge_explode, 0, 0, 0, 0, 0, 0);
  states[wind].tics = V(sp->key, "windup", 4);

  // the explosion (used when a charge ends in a blast)
  e3 = st_add(spr, 3 | FF_FB, 4, NULL, 0);
  e2 = st_add(spr, 2 | FF_FB, 4, NULL, e3);
  e1 = st_add(spr, 1 | FF_FB, 4, ACT(A_BgBlast), e2);
  st_args(e1, V(sp->key, "blast", 60), V(sp->key, "blastradius", 96), 0, 1, 0, 0, 0, 0);
  info->raisestate = e1;
}

// ---- bosses ----

static void b_mecha(spec_t* sp)
{
  mobjinfo_t* info = &mobjinfo[sp->type];
  int spr = states[CL(S_MECH_ATK1)].sprite;
  int pick, rocket, seeker, flame, a, i, last;
  int rockets = V(sp->key, "rockets", 2), seekers = V(sp->key, "seekers", 2), flames = V(sp->key, "flames", 8);
  int see = info->seestate;

  // flame
  last = st_add(spr, 4, 6, NULL, see);
  for (i = 0; i < flames; ++i)
  {
    a = st_add(spr, 5 | FF_FB, 3, ACT(A_BgFlame), last);
    st_args(a, T("mecha_flame"), 0, 0, 0, 0, 0, 0, 0);
    last = a;
  }
  flame = last;
  // rockets
  last = st_add(spr, 4, 8, NULL, see);
  for (i = 0; i < rockets; ++i)
  {
    int g = st_add(spr, 4, 12, ACT(A_FaceTarget), last);

    a = st_add(spr, 5 | FF_FB, 12, NULL, g);
    make_attack_args(a, T("mecha_rocket"), 0, 0, 0, 1, 0, -1, 0);
    last = a;
  }
  rocket = last;
  // heat-seekers: a pair at a time, from the shoulders
  last = st_add(spr, 4, 10, NULL, see);
  for (i = 0; i < seekers; ++i)
  {
    int g = st_add(spr, 4, 10, ACT(A_FaceTarget), last);

    a = st_add(spr, 5 | FF_FB, 8, NULL, g);
    make_attack_args(a, T("mecha_seeker"), 0, 0, 0, 2, DEGF(18), -1, 0);
    last = a;
  }
  seeker = last;
  // the choice
  pick = st_add(spr, 4, 4, ACT(A_BgMechaPick), see);
  st_args(pick, V(sp->key, "flamerange", 320), V(sp->key, "seekerchance", 100), rocket, seeker, flame, 0, 0, 0);
  info->missilestate = st_add(spr, 4, V(sp->key, "windup", 8), ACT(A_FaceTarget), pick);
}

static void b_beetle(spec_t* sp)
{
  mobjinfo_t* info = &mobjinfo[sp->type];
  int spr = states[CL(S_BEET_ATK1)].sprite;
  int volleys = V(sp->key, "volleys", 2), shots = V(sp->key, "shots", 3), i, last, a;
  int first;

  last = st_add(spr, 32768 & 0x7fff, V(sp->key, "recover", 20), NULL, info->seestate);
  for (i = 0; i < volleys; ++i)
  {
    int g = st_add(spr, 0, V(sp->key, "gap", 14), NULL, last);

    a = st_add(spr, 6 | FF_FB, 10, NULL, g);
    make_attack_args(a, T("beetle_acid"), 0, 0, 0, shots, DEGF(V(sp->key, "fan", 16)), -1, 0);
    last = a;
  }
  first = st_add(spr, FF_FB, V(sp->key, "windup", 24), ACT(A_FaceTarget), last);
  info->missilestate = first;
}

static int heart_build(spec_t* sp, int see_tics)
{
  mobjinfo_t* info = &mobjinfo[sp->type];
  int spr = states[info->spawnstate].sprite;
  int stand, see, pain, miss, shot, d1, d2, d3;
  int volley = V(sp->key, "volley", 1), i, last;

  d3 = st_add(spr, 1, -1, ACT(A_BossDeath), 0);
  d2 = st_add(spr, 1, 12, ACT(A_Fall), d3);
  d1 = st_add(spr, 1, 12, ACT(A_Scream), d2);
  pain = st_add(spr, 1, 6, ACT(A_Pain), 0);
  stand = st_add(spr, 0, 10, ACT(A_Look), NEXT_);
  states[stand].nextstate = stand;
  see = st_add(spr, 0, see_tics, ACT(A_Chase), NEXT_);
  states[see].nextstate = see;
  states[pain].nextstate = see;
  last = st_add(spr, 0, V(sp->key, "recover", 40), NULL, see);
  for (i = 0; i < volley; ++i)
  {
    shot = st_add(spr, 0 | FF_FB, 8, NULL, last);
    make_attack_args(shot, T("heart_fireball"), 0, 0, 0, 1, 0, -1, 0);
    last = st_add(spr, 0, 8, ACT(A_FaceTarget), shot);
  }
  miss = st_add(spr, 0, V(sp->key, "windup", 30), ACT(A_FaceTarget), last);
  info->spawnstate = stand;
  info->seestate = see;
  info->painstate = pain;
  info->missilestate = miss;
  info->meleestate = 0;
  info->deathstate = d1;
  info->xdeathstate = 0;
  info->raisestate = 0;
  info->crashstate = 0;
  return see;
}

static void b_heart(spec_t* sp)
{
  heart_build(sp, 6);
}

static void b_core(spec_t* sp)
{
  heart_build(sp, 6);
}

//
// Building the roster
//

static void apply_stat(mobjinfo_t* info, const char* key, const char* field, int value, dboolean missile)
{
  if (!strcmp(field, "health"))
    info->spawnhealth = value;
  else if (!strcmp(field, "speed"))
    info->speed = missile ? value * FRACUNIT : value;
  else if (!strcmp(field, "radius"))
    info->radius = value * FRACUNIT;
  else if (!strcmp(field, "height"))
    info->height = value * FRACUNIT;
  else if (!strcmp(field, "mass"))
    info->mass = value;
  else if (!strcmp(field, "pain"))
    info->painchance = value;
  else if (!strcmp(field, "reaction"))
    info->reactiontime = value;
  else if (!strcmp(field, "damage"))
    info->damage = value;
  (void) key;
}

static const char* stat_fields[] = { "health", "speed", "radius", "height", "mass", "pain", "reaction", "damage", NULL };

// The roster takes over Doom's own monsters (same editor numbers): key -> the mobj type it replaces.
// Things not listed here (rocket trooper, articulator heart) are new types.
static int slot_of(const char* key)
{
  static const struct { const char* key; int slot; } slots[] = {
    { "shocktrooper", MT_SHOCKTROOPER }, { "werewolf", MT_WEREWOLF }, { "fireelemental", MT_FIREELEM },
    { "madwizard", MT_MADWIZARD }, { "apprentice", MT_APPRENTICE }, { "minister", MT_MINISTER },
    { "golem", MT_GOLEM }, { "mindeater", MT_MINDEATER }, { "aegis", MT_AEGIS }, { "bore", MT_BORE },
    { "hatred", MT_HATRED }, { "conjurer", MT_CONJURER }, { "mecha", MT_MECHA }, { "beetle", MT_BEETLE },
    { "core", MT_CORE },
  };
  int i;

  for (i = 0; i < (int) (sizeof(slots) / sizeof(slots[0])); ++i)
    if (!strcmp(slots[i].key, key))
      return slots[i].slot;
  return -1;
}

static void build_spec(spec_t* sp)
{
  mobjinfo_t info = mobjinfo[sp->base];
  int ospr = states[info.spawnstate].sprite;
  int slot = slot_of(sp->key);
  int nspr;
  dboolean missile = (info.flags & MF_MISSILE) != 0;
  int i;

  clone_inplace = (slot >= 0 && slot == sp->base); // e.g. the demon slot becomes the werewolf, states and all
  nspr = (sp->sprite && !clone_inplace) ? dsda_AddSpriteName(sp->sprite) : -1;
  clone_begin();
  clone_chains(&info, ospr, nspr);
  info.doomednum = slot >= 0 ? mobjinfo[slot].doomednum : sp->ednum;
  info.flags |= sp->flags_on;
  info.flags &= ~sp->flags_off;
  info.flags2 |= sp->flags2_on;
  if (sp->health)
    info.spawnhealth = sp->health;
  if (sp->speed)
    info.speed = missile ? sp->speed * FRACUNIT : sp->speed;
  if (sp->radius)
    info.radius = sp->radius * FRACUNIT;
  if (sp->height)
    info.height = sp->height * FRACUNIT;
  if (sp->mass)
    info.mass = sp->mass;
  if (sp->pain)
    info.painchance = sp->pain;
  if (sp->reaction)
    info.reactiontime = sp->reaction;
  if (sp->damage)
    info.damage = sp->damage;
  for (i = 0; stat_fields[i]; ++i)
    if (V(sp->key, stat_fields[i], -1) != -1)
      apply_stat(&info, sp->key, stat_fields[i], V(sp->key, stat_fields[i], 0), missile);
  if (slot >= 0)
  {
    mobjinfo[slot] = info;
    sp->type = slot;
  }
  else
    sp->type = dsda_AppendMobjInfo(&info);
  clone_inplace = false;
  if (sp->build)
    sp->build(sp);
  clone_end();
}

// BGMOBJ lines for stock Doom monsters: "3004 health 30 foedamage 130"
static void apply_numeric_overrides(void)
{
  int i;

  foe_n = num_mobj_types;
  foe_damage = calloc(foe_n, sizeof(*foe_damage));
  foe_spread = calloc(foe_n, sizeof(*foe_spread));
  for (i = 0; i < foe_n; ++i)
    foe_damage[i] = foe_spread[i] = 100;

  for (i = 0; i < num_ov; ++i)
  {
    char* end;
    long ed = strtol(ovs[i].key, &end, 10);
    int t;

    if (*end || ovs[i].key[0] == '\0')
      continue;
    for (t = 0; t < num_mobj_types; ++t)
      if (mobjinfo[t].doomednum == (int) ed)
        break;
    if (t >= num_mobj_types)
    {
      lprintf(LO_WARN, "BGMOBJ: no thing with editor number %ld\n", ed);
      continue;
    }
    if (!strcmp(ovs[i].field, "foedamage"))
      foe_damage[t] = (short) ovs[i].value;
    else if (!strcmp(ovs[i].field, "foespread"))
      foe_spread[t] = (short) ovs[i].value;
    else if (!strcmp(ovs[i].field, "windup"))
    {
      if (mobjinfo[t].missilestate > 0)
        states[mobjinfo[t].missilestate].tics = (short) ovs[i].value;
    }
    else
      apply_stat(&mobjinfo[t], ovs[i].key, ovs[i].field, ovs[i].value, (mobjinfo[t].flags & MF_MISSILE) != 0);
  }
}

static const char bgmobj_empty[] = "";

void dsda_InitBgRoster(void)
{
  int i;

  num_ov = 0;
  // every BGMOBJ lump, in load order (later lines win)
  for (i = 0; i < numlumps; ++i)
  {
    char* text;
    int size;

    if (strncasecmp(lumpinfo[i].name, "BGMOBJ", 6))
      continue;
    size = W_LumpLength(i);
    text = Z_Malloc(size + 1);
    memcpy(text, W_LumpByNum(i), size);
    text[size] = '\0';
    ov_parse(text, i);
    Z_Free(text);
  }
  (void) bgmobj_empty;

  for (i = 0; i < NUM_ROSTER; ++i)
    build_spec(&roster[i]);

  apply_numeric_overrides();
}
