//
// Begotten: weapon slots with primary / secondary fire, seven ammo types.
// Licensed under the GNU GPL v2 or later, like the rest of the engine.
//
// See begotten.h for the overview.  The numbers that designers care about
// (slots, ammo types, ammo per shot) live in the BGWEAPON lump; the text
// below is the built-in default and is read first, then every BGWEAPON lump
// in the loaded WADs, later ones overriding earlier ones.
//

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _MSC_VER
#include <strings.h>
#endif
#include "doomtype.h"

#include "doomstat.h"
#include "v_video.h"
#include "m_random.h"
#include "d_items.h"
#include "info.h"
#include "lprintf.h"
#include "p_map.h"
#include "p_mobj.h"
#include "p_pspr.h"
#include "p_enemy.h"
#include "p_tick.h"
#include "r_main.h"
#include "d_event.h"
#include <SDL.h>
#include "dsda/messenger.h"
#include "p_inter.h"
#include "g_game.h"
#include "smooth.h"
#include "dsda/sprite.h"
#include "s_sound.h"
#include "sounds.h"
#include "tables.h"
#include "p_user.h"
#include "p_maputl.h"
#include "p_spec.h"
#include "w_wad.h"
#include "z_zone.h"

#include "dsda/bgroster.h"
#include "dsda/bgpickup.h"
#include "dsda/death.h"
#include "dsda/excmd.h"
#include "dsda/line_special.h"
#include "dsda/mobjinfo.h"
#include "dsda/name.h"
#include "dsda/state.h"

#include "begotten.h"
#include "dsda/configuration.h"
#include "dsda/damage_kind.h"
#include "dsda/bgclass.h"
#include "dsda/bgcoop.h"
#include "dsda/bgnet.h"
#include "dsda/bglight.h"
#include "dsda/bgvoxel.h"
#include "dsda/bgprogress.h"
#include "dsda/bgsplit.h"
#include "dsda/key_frame.h"
#include "dsda/save.h"

extern dboolean bg_made_netgame;

dboolean P_GiveWeapon(player_t* player, weapontype_t weapon, dboolean dropped);

dboolean bg_weapons = false;

weapontype_t bg_slot_primary[BG_SLOTS + 1];
weapontype_t bg_slot_alt[BG_SLOTS + 1];

const int bg_ammo_order[7] = {
  am_clip, am_shell, am_belt, am_misl, am_cell, am_spirit, am_hellfire
};

static const char* ammo_names[7] = {
  "Bullets", "Shells", "Tempest Runes", "Fire Runes", "Belts", "Spirit Runes", "Hellfire"
};

// the words used in BGWEAPON, in ammo_t order
static const char* ammo_words[7] = {
  "bullets", "shells", "tempest", "fire", "belts", "spirit", "hellfire"
};

static const char* weapon_words[NUMWEAPONS] = {
  "fist", "pistol", "shotgun", "chaingun", "missile", "plasma", "bfg", "chainsaw",
  "supershotgun", "dualpistols", "chainfire", "tornado", "arclightning", "snap",
  "hellfire", "spare"
};

static const char* weapon_display[NUMWEAPONS] = {
  "Knife", "Pistol", "Rifle", "Machinegun", "Fireball", "Devil's Breath", "Thunderclap",
  "Chainsaw", "Scattergun", "Revolver", "Chaingun", "Tornado", "Arc Lightning", "Snap",
  "Hellfire", "Hellfire"
};

// Built-in defaults; identical to data/lumps/bgweapon.lmp.
static const char bg_default_text[] =
  "slot 1 primary fist       alt chainsaw\n"
  "slot 2 primary pistol     alt dualpistols\n"
  "slot 3 primary shotgun    alt supershotgun\n"
  "slot 4 primary chaingun   alt chainfire\n"
  "slot 5 primary plasma     alt missile\n"
  "slot 6 primary tornado    alt arclightning\n"
  "slot 7 primary snap       alt bfg\n"
  "slot 8 primary hellfire   alt none\n"
  "ammo bullets  max 200 clip 20\n"
  "ammo shells   max 60  clip 6\n"
  "ammo belts    max 300 clip 30\n"
  "ammo fire     max 200 clip 20\n"
  "ammo tempest  max 200 clip 20\n"
  "ammo spirit   max 100 clip 10\n"
  "ammo hellfire max 50  clip 5\n"
  "mode fist         ammo none\n"
  "mode chainsaw     ammo none\n"
  "mode pistol       ammo bullets  use 1\n"
  "mode dualpistols  ammo bullets  use 1\n"
  "pistolshot damage 10 mod 3 spread 40\n"
  "pierce damage 6 mod 3 hits 5 falloff 85 spread 0\n"
  "mode shotgun      ammo shells   use 1\n"
  "mode supershotgun ammo shells   use 4\n"
  "mode chaingun     ammo belts    use 1\n"
  "mode chainfire    ammo belts    use 1\n"
  "mode missile      ammo fire     use 4\n"
  "mode plasma       ammo fire     use 1\n"
  "mode tornado      ammo tempest  use 5\n"
  "mode arclightning ammo tempest  use 4\n"
  "mode snap         ammo spirit   use 5\n"
  "mode bfg          ammo spirit   use 25\n"
  "mode hellfire     ammo hellfire use 1\n"
  "mode spare        ammo none     use 0\n";

//
// States
//

#define BG_STATE_BASE 4000
#define NEXT (-1)

static int bg_state_next = BG_STATE_BASE;

int bg_AllocStates(int n)
{
  int idx = bg_state_next;

  bg_state_next += n;
  dsda_ExtendStates(bg_state_next - 1);
  return idx;
}

int bg_StateTop(void)
{
  return bg_state_next - 1;
}

typedef void (*bg_action_t)(player_t*, pspdef_t*);

static int bg_S(int spr, int frame, int tics, bg_action_t act, int next,
                long long a0, long long a1, long long a2, long long a3, long long a4)
{
  int idx = bg_state_next++;
  state_t* st = dsda_ExtendStates(idx);

  memset(st, 0, sizeof(*st));
  st->sprite = spr;
  st->frame = frame;
  st->tics = tics;
  st->action = (actionf_t) act;
  st->nextstate = (next == NEXT) ? idx + 1 : next;
  st->args[0] = a0;
  st->args[1] = a1;
  st->args[2] = a2;
  st->args[3] = a3;
  st->args[4] = a4;

  return idx;
}

#define S0(spr, frame, tics, act, next) bg_S(spr, frame, tics, act, next, 0, 0, 0, 0, 0)
#define DEG(x) ((long long) ((x) * 65536.0))

//
// Custom weapon actions
//

// launch speeds are divided by the target's mass (an imp has 100)
#define BG_SNAP_UP   1300
#define BG_SNAP_BACK 600

static void A_BGSnap(player_t* player, pspdef_t* psp)
{
  mobj_t* t;
  mobj_t* fire;
  angle_t an;
  fixed_t power;
  int damage;

  P_AimLineAttack(player->mo, player->mo->angle, 1024 * FRACUNIT, 0);
  if (!linetarget)
    return;

  t = linetarget;

  fire = P_SpawnMobj(t->x, t->y, t->z, MT_FIRE);
  P_SetTarget(&fire->target, player->mo);
  // no tracer: A_Fire would slide it to a spot in front of where the victim faces. It stays on the body
  // and is drawn nudged toward whoever is looking (bg_ViewShift), so it is always in front for the viewer
  fire->bg_viewshift = 1;
  P_SetMobjState(fire, S_FIRE1 + 23); // join the burst near its end: a short flash, not a long burn

  damage = 20 + (P_Random(pr_mbf21) % 11);
  P_DamageMobj(t, player->mo, player->mo, damage);

  // Like the arch-vile blast: tossed up and back in an arc (heavier things move less)
  if (t->info->mass < 10000000)
  {
    an = R_PointToAngle2(player->mo->x, player->mo->y, t->x, t->y) >> ANGLETOFINESHIFT;
    power = (BG_SNAP_BACK * FRACUNIT) / t->info->mass;
    if (!(t->flags & MF_NOGRAVITY))
      t->momz = (BG_SNAP_UP * FRACUNIT) / t->info->mass;
    t->momx += FixedMul(power, finecosine[an]);
    t->momy += FixedMul(power, finesine[an]);
  }
}

// The snap flame is drawn a little toward the viewer so it covers the body whichever way she faces
void bg_ViewShift(const mobj_t* thing, fixed_t vx, fixed_t vy, fixed_t* x, fixed_t* y)
{
  fixed_t dx, dy, d;

  if (!thing->bg_viewshift)
    return;
  dx = vx - *x;
  dy = vy - *y;
  d = P_AproxDistance(dx, dy);
  if (d < 32 * FRACUNIT)
    return;
  *x += FixedMul(FixedDiv(dx, d), 20 * FRACUNIT);
  *y += FixedMul(FixedDiv(dy, d), 20 * FRACUNIT);
}

// Recoil and a hint of screen shake, once per shot (called from the ammo subtraction every attack goes through).
// push: units of speed thrown at the player's back; shake: hundredths of a degree the view is turned, alternately
// left and right on successive shots (about a pixel). Class: the witch is moved more than the soldier.
typedef struct { int weapon, push100, shake100; } bg_kick_t;

static const bg_kick_t bg_kicks[] = {
  { wp_dualpistols, 150, 0 },     // revolver (alt of the pistol): a little kick back
  { wp_shotgun, 200, 12 },        // rifle: a little kick
  { wp_supershotgun, 900, 25 },   // scattergun (alt): a heavy shove and a shake
  { wp_chainfire, 80, 25 },       // chaingun (alt): pushes back on every round, the view jitters
  { wp_missile, 120, 15 },        // fireball: subtle, but felt
};

void bg_WeaponKick(player_t* player)
{
  mobj_t* mo = player->mo;
  int i, pct;

  if (!bg_weapons || !mo)
    return;
  for (i = 0; i < (int) (sizeof bg_kicks / sizeof bg_kicks[0]); ++i)
    if (bg_kicks[i].weapon == player->readyweapon)
    {
      pct = player->bgclass == bg_ClassFromWord("witch") ? 160 : 100;
      if (!(mo->flags & MF_NOCLIP) && bg_kicks[i].push100)
        P_Thrust(player, mo->angle + ANG180, FixedMul(bg_kicks[i].push100 * FRACUNIT / 100, pct * FRACUNIT / 100));
      if (bg_kicks[i].shake100)
      {
        angle_t a = (angle_t) ((long long) ANG1 * bg_kicks[i].shake100 / 100);

        player->bgshake ^= 1;
        mo->angle += player->bgshake ? a : (angle_t) (0 - a);
      }
      return;
    }
}

int bg_WeaponPickupClips(weapontype_t weapon, int clips)
{
  // a found thunderclap weapon (slot 7) gives 30 spirit: one clap and a little over
  return weapon == wp_snap ? clips * 3 / 2 : clips;
}

// Performance log: every ten seconds of play, the frame rate (average and worst) and how much texture memory
// the renderer has been asked for, so a slow machine's log says where the time goes.
extern void gld_TextureStats(int* count, int* megabytes);

void bg_PerfFrame(void)
{
  static unsigned last, window_start, frames, worst;
  unsigned now = SDL_GetTicks();

  if (!bg_weapons || gamestate != GS_LEVEL)
  {
    last = window_start = 0;
    frames = worst = 0;
    return;
  }
  if (!last)
  {
    last = window_start = now;
    return;
  }
  if (now - last > worst)
    worst = now - last;
  last = now;
  ++frames;
  if (now - window_start >= 10000)
  {
    int count = 0, mb = 0;

    if (V_IsOpenGLMode())
      gld_TextureStats(&count, &mb);
    lprintf(LO_INFO, "PERF: %.1f fps average, worst frame %u ms, %s, %d textures built (%d MB)\n",
            frames * 1000.0 / (now - window_start), worst, V_IsOpenGLMode() ? "OpenGL" : "software", count, mb);
    window_start = now;
    frames = worst = 0;
  }
}

// Hellfire kicks the player back whether or not anything is hit.
static void A_BGRecoil(player_t* player, pspdef_t* psp)
{
  if (!(player->mo->flags & MF_NOCLIP))
    P_Thrust(player, player->mo->angle + ANG180, 2300 * 330);
}

static int bg_tornado_type = -1;
static int bg_trng_ready_state;  // the ready state of slot 6 (TRNG hands)
static int bg_hellfire_type = -1;
static int bg_arcbolt_type = -1;
static int bg_fireball_type = -1;   // slot 5 primary: a big ball of fire
static int bg_breath_type = -1;     // slot 5 alt, devil's breath: a tossed flame
static int bg_clap_type = -1;       // the thunderclap's expanding shock ring (visual only)
static int bg_torn_end_state = -1;  // the tornado dying down


// Fireball (slot 5 primary): its own projectile (a bigger fireball), not the rocket
static void A_BGFireball(player_t* player, pspdef_t* psp)
{
  P_SubtractAmmo(player, 1);
  P_SpawnPlayerMissile(player->mo, bg_fireball_type);
}

// Devil's breath (slot 5 alt): a small tossed flame, fired rapidly
static void A_BGBreath(player_t* player, pspdef_t* psp)
{
  P_SubtractAmmo(player, 1);
  P_SetPsprite(player, ps_flash, doom_weaponinfo[player->readyweapon].flashstate + (P_Random(pr_plasma) & 1));
  P_SpawnPlayerMissile(player->mo, bg_breath_type);
}

// Tornado: the projectile is launched without the autoaim target as its tracer (the stock
// A_WeaponProjectile sets one); the tracer only ever means "the monster it hit".
static void A_BGTornadoFire(player_t* player, pspdef_t* psp)
{
  P_SpawnPlayerMissile(player->mo, bg_tornado_type);
}

// Arc lightning: a wide fan of small plasma bolts, like a shotgun blast with missiles.
#define BG_ARC_BOLTS 9
#define BG_ARC_HALF_ARC_DEG 22

static void A_BGArcFire(player_t* player, pspdef_t* psp)
{
  int i;

  for (i = 0; i < BG_ARC_BOLTS; ++i)
  {
    mobj_t* m = P_SpawnPlayerMissile(player->mo, bg_arcbolt_type);
    int fan, jitter, dz;
    angle_t an;

    if (!m)
      continue;

    // evenly across the arc, plus a little randomness
    fan = (2 * BG_ARC_HALF_ARC_DEG * 1000 / (BG_ARC_BOLTS - 1)) * i - BG_ARC_HALF_ARC_DEG * 1000; // milli-degrees
    jitter = ((P_Random(pr_mbf21) - P_Random(pr_mbf21)) * 2000) / 255;                            // +-2 degrees
    dz = P_Random(pr_mbf21) - P_Random(pr_mbf21);

    m->angle += (angle_t) (((long long) (fan + jitter) * (long long) ANG1 * bg_ClassSpreadPct(player)) / 100000);
    an = m->angle >> ANGLETOFINESHIFT;
    m->momx = FixedMul(m->info->speed, finecosine[an]);
    m->momy = FixedMul(m->info->speed, finesine[an]);
    m->momz += dz * 200;
  }
}

// Hellfire rocket with a random spread (about 3 degrees sideways, 1.5 up/down).
static void A_BGHellfire(player_t* player, pspdef_t* psp)
{
  mobj_t* m = P_SpawnPlayerMissile(player->mo, bg_hellfire_type);
  int dx, dz;
  unsigned int an;

  if (!m)
    return;

  dx = P_Random(pr_mbf21) - P_Random(pr_mbf21);
  dz = P_Random(pr_mbf21) - P_Random(pr_mbf21);
  m->angle += (unsigned int) (dx * bg_ClassSpreadPct(player) / 100) << 17;
  an = m->angle >> ANGLETOFINESHIFT;
  m->momx = FixedMul(m->info->speed, finecosine[an]);
  m->momy = FixedMul(m->info->speed, finesine[an]);
  m->momz += dz * 308 * bg_ClassSpreadPct(player) / 100;
}

// Tornado: what is left of the projectile after it hit something.
static int bg_stick_state = -1;
static int bg_stick_state2 = -1;
static int bg_torn_death_state = -1;

#define BG_STICK_TICKS 16 // times 5 tics
#define BG_TORNADO_STUN 12 // tics of stun per application (it is re-applied every 5 tics while stuck)

static dboolean bg_StunMobjRoll(mobj_t* mobj, int tics, dboolean roll)
{
  if (!mobj || mobj->health <= 0 || !(mobj->flags & MF_SHOOTABLE) || mobj->player)
    return false;
  if ((mobj->flags2 & MF2_BOSS) || mobj->info->painchance <= 0)
    return false;
  if (roll && (mobj->flags & MF_SKULLFLY))
    return false;
  if (roll && P_Random(pr_painchance) >= mobj->info->painchance)
    return false; // painchance is the chance that a stun takes hold
  if (mobj->flags & MF_SKULLFLY)
  {
    // a parried charge (the lost soul) ends on the spot
    mobj->flags &= ~MF_SKULLFLY;
    mobj->momx = mobj->momy = mobj->momz = 0;
  }

  if (mobj->bg_stun > 0)
  {
    if (tics > mobj->bg_stun)
      mobj->bg_stun = tics; // refresh the running stun
    return true;
  }
  if (mobj->bg_stunimmune > 0)
    return false;

  // flinch, then hold the pose
  if (mobj->info->painstate)
    P_SetMobjState(mobj, mobj->info->painstate);
  if (mobj->health > 0)
    mobj->bg_stun = tics;
  return mobj->health > 0;
}

dboolean bg_StunMobj(mobj_t* mobj, int tics)
{
  return bg_StunMobjRoll(mobj, tics, true);
}

//
// Parry
//

#define BG_PARRY_STUN 40        // tics a parried melee attacker stays stunned
#define BG_PARRY_SPEED_PCT 200  // reflected projectiles: speed
#define BG_PARRY_DAMAGE_PCT 200 // reflected projectiles: damage
#define BG_PARRY_PUSH (6 * FRACUNIT)

static unsigned int bg_Isqrt(unsigned long long n)
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
  return (unsigned int) r;
}

static fixed_t bg_Length3(fixed_t x, fixed_t y, fixed_t z)
{
  return (fixed_t) bg_Isqrt((unsigned long long) ((long long) x * x + (long long) y * y + (long long) z * z));
}

static dboolean bg_ParryFacing(const mobj_t* victim, fixed_t x, fixed_t y)
{
  angle_t a = R_PointToAngle2(victim->x, victim->y, x, y) - victim->angle;

  return a + ANG90 < ANG180; // within 90 degrees of where the player looks
}

// the shove of a parried blow: 1 unit per tic at least, plus 0.04 per point of damage, at most 8
// (a pistol-class bullet about 1.4, a 90 damage rocket about 4.6)
#define BG_PARRY_KNOCK_MIN FRACUNIT
#define BG_PARRY_KNOCK_PER_DAMAGE (FRACUNIT / 25)
#define BG_PARRY_KNOCK_MAX (8 * FRACUNIT)

// push_angle: the direction the blow was travelling, i.e. where the player is shoved
static int bg_parry_swing_state[2]; // the bat-away animations (right, left), set up in bg_DefineStates

static void bg_ParryEffect(player_t* player, fixed_t x, fixed_t y, fixed_t z, angle_t push_angle, int damage)
{
  fixed_t knock = BG_PARRY_KNOCK_MIN + (fixed_t) ((long long) damage * BG_PARRY_KNOCK_PER_DAMAGE);

  if (knock > BG_PARRY_KNOCK_MAX)
    knock = BG_PARRY_KNOCK_MAX;
  S_StartMobjSound(player->mo, sfx_tink);
  P_SpawnMobj(x, y, z, MT_PUFF);
  player->bonuscount += 2; // a little flash

  // every parried blow shoves the player back, harder for harder blows (this is body momentum
  // only: it does not make the view bob)
  player->mo->momx += FixedMul(knock, finecosine[push_angle >> ANGLETOFINESHIFT]);
  player->mo->momy += FixedMul(knock, finesine[push_angle >> ANGLETOFINESHIFT]);
  player->bgparryhit++; // counts the blows parried since the guard went up

  // she bats it away: the swing alternates right, left, right ... with every parried blow
  if (player->readyweapon == wp_spare && bg_parry_swing_state[0])
    P_SetPsprite(player, ps_weapon, bg_parry_swing_state[(player->bgparryhit - 1) & 1]);

  // every parried blow renews the window: a burst of shots is parried shot after shot
  player->bgparry = bg_ClassParryWindow(player);
}

// what the player is aiming at: the monster closest to the line of sight within a narrow cone
// the host's setting in a network game, ours otherwise
static dboolean bg_FriendlyFireSetting(void)
{
  return bgnet_InGame() ? bgnet_FriendlyFire() : dsda_IntConfig(dsda_config_begotten_friendly_fire) != 0;
}

// Co-op friendly fire.  Off (the default): players cannot hurt one another, autoaim and
// shots ignore them, and their projectiles pass through each other.  On: all of it applies, and
// a parrying swordsman can turn another player's shots (the reflected shot is doubled as usual).
dboolean bg_CoopShield(const mobj_t* a, const mobj_t* b)
{
  if (!bg_weapons || !a || !b || a == b || !a->player || !b->player || deathmatch)
    return false;
  return !bg_FriendlyFireSetting();
}

static dboolean bg_FriendlyFireOn(void)
{
  return !deathmatch && bg_FriendlyFireSetting();
}

// the autoaim cone of a parry: monsters only, a few degrees either side of the crosshair
#define BG_PARRY_AIM_DEG 8

static mobj_t* bg_ParryAimTarget(const mobj_t* victim, const mobj_t* ignore, int degrees)
{
  thinker_t* th;
  mobj_t* best = NULL;
  angle_t best_err = ANG1 * degrees; // either side of the crosshair

  for (th = thinkercap.next; th != &thinkercap; th = th->next)
  {
    mobj_t* m;
    angle_t err;

    if (th->function != P_MobjThinker)
      continue;
    m = (mobj_t*) th;
    if (m == ignore || m == victim || m->player || !(m->flags & MF_SHOOTABLE) || m->health <= 0)
      continue;
    err = R_PointToAngle2(victim->x, victim->y, m->x, m->y) - victim->angle;
    if (err > ANG180)
      err = (angle_t) (0 - err);
    if (err < best_err && P_CheckSight((mobj_t*) victim, m))
    {
      best = m;
      best_err = err;
    }
  }
  return best;
}

// the closest monster the player can see (other than `ignore`), within 2048 units
static mobj_t* bg_ParryNearest(const mobj_t* victim, const mobj_t* ignore)
{
  thinker_t* th;
  mobj_t* best = NULL;
  fixed_t best_dist = 2048 * FRACUNIT;

  for (th = thinkercap.next; th != &thinkercap; th = th->next)
  {
    mobj_t* m;
    fixed_t d;

    if (th->function != P_MobjThinker)
      continue;
    m = (mobj_t*) th;
    if (m == ignore || m == victim || m->player || !(m->flags & MF_SHOOTABLE) ||
        m->health <= 0 || !(m->flags & MF_COUNTKILL))
      continue;
    d = P_AproxDistance(m->x - victim->x, m->y - victim->y);
    if (d < best_dist && P_CheckSight((mobj_t*) victim, m))
    {
      best = m;
      best_dist = d;
    }
  }
  return best;
}

// called once the step that parried it is over: the missile starts from the aim line in front of the player
void bg_PlaceRedirected(mobj_t* mo)
{
  P_UnsetThingPosition(mo);
  mo->x = mo->bg_rx;
  mo->y = mo->bg_ry;
  mo->z = mo->bg_rz;
  P_SetThingPosition(mo);
}

// the line a player aims along, as a unit horizontal direction and a slope (her own missiles' pitch with mouse look)
static void bg_AimLine(const mobj_t* victim, fixed_t* dx, fixed_t* dy, fixed_t* dz)
{
  int an = victim->angle >> ANGLETOFINESHIFT;

  *dx = finecosine[an];
  *dy = finesine[an];
  *dz = 0;
  if (dsda_FreeAim())
  {
    fixed_t h = finecosine[victim->pitch >> ANGLETOFINESHIFT];

    *dz = FixedDiv(-finesine[victim->pitch >> ANGLETOFINESHIFT], h ? h : 1);   // slope
  }
}

#define BG_PARRY_GUIDE_TICS 12   // a parried ball keeps following the crosshair for this long (about a third of a second)

// For a short while after it is parried, the ball stays on its owner's crosshair line: every tic it steers to
// the point on her current aim line that is as far out as the ball is. Whatever the lag between what she sees
// and the tic it was parried in, or a flick of the mouse right after, the ball ends up where the crosshair is.
void bg_GuideMissile(mobj_t* mo)
{
  mobj_t* owner = mo->target;
  fixed_t dx, dy, dz, rx, ry, rz, out, speed, len, tx, ty, tz, vx, vy, vz;

  if (mo->bg_guide)
    --mo->bg_guide;
  if (!owner || !owner->player || owner->health <= 0 || !(mo->flags & MF_MISSILE) || mo->health <= 0)
  {
    mo->bg_guide = 0;
    return;
  }
  bg_AimLine(owner, &dx, &dy, &dz);
  rx = mo->x - owner->x;
  ry = mo->y - owner->y;
  rz = mo->z - (owner->z + 32 * FRACUNIT);
  out = FixedMul(rx, dx) + FixedMul(ry, dy);               // how far along her aim the ball is
  if (out < owner->radius)
    out = owner->radius;
  speed = bg_Length3(mo->momx, mo->momy, mo->momz);
  // the point it should be at one tic from now, on the aim line
  out += FixedDiv(speed, bg_Length3(FRACUNIT, 0, dz));
  tx = owner->x + FixedMul(dx, out);
  ty = owner->y + FixedMul(dy, out);
  tz = owner->z + 32 * FRACUNIT + FixedMul(dz, out);
  vx = tx - mo->x; vy = ty - mo->y; vz = tz - mo->z;
  len = bg_Length3(vx, vy, vz);
  if (len < FRACUNIT)
    return;
  mo->momx = FixedMul(FixedDiv(vx, len), speed);
  mo->momy = FixedMul(FixedDiv(vy, len), speed);
  mo->momz = FixedMul(FixedDiv(vz, len), speed);
  mo->angle = R_PointToAngle2(0, 0, mo->momx, mo->momy);
  if (!mo->bg_guide)
    lprintf(LO_INFO, "PARRY: ball released, her aim turned %.1f degrees since the parry\n",
            (double) (int) (owner->angle - mo->bg_guide_start) / (double) ANG1);
}

dboolean bg_ParryMissile(mobj_t* missile, mobj_t* victim)
{
  player_t* player = victim->player;
  mobj_t* shooter = missile->target;
  mobj_t* aim;
  fixed_t dx, dy, dz, dist, speed;
  angle_t push_angle;
  int an;

  if (!player || player->bgparry <= 0 || !(missile->flags & MF_MISSILE))
    return false;
  if (shooter == victim || (shooter && shooter->player && !bg_FriendlyFireOn()))
    return false;
  if (!bg_ParryFacing(victim, victim->x - missile->momx, victim->y - missile->momy))
    return false;

  push_angle = R_PointToAngle2(0, 0, missile->momx, missile->momy);

  // reflected shots are twice as fast and hit twice as hard
  speed = bg_Length3(missile->momx, missile->momy, missile->momz) * BG_PARRY_SPEED_PCT / 100;

  // They go exactly where the player aims: along the crosshair, starting on that line just in front of her (not
  // wherever the missile happened to touch her), with the same pitch her own shots would have. The only help is a
  // little autoaim to a monster close to the crosshair, never to another player, friendly fire or not.
  aim = bg_ParryAimTarget(victim, shooter, BG_PARRY_AIM_DEG);
  if (!aim)
    aim = bg_ParryAimTarget(victim, NULL, BG_PARRY_AIM_DEG);
  an = victim->angle >> ANGLETOFINESHIFT;
  dx = finecosine[an];
  dy = finesine[an];
  dz = 0;
  if (aim)
  {
    dx = aim->x - victim->x;
    dy = aim->y - victim->y;
    dist = P_AproxDistance(dx, dy);
    if (dist < FRACUNIT)
      dist = FRACUNIT;
    dz = FixedDiv(aim->z + (aim->height >> 1) - (victim->z + 32 * FRACUNIT), dist);   // slope
    dx = FixedDiv(dx, dist);
    dy = FixedDiv(dy, dist);
  }
  else if (dsda_FreeAim())
  {
    // mouse look: up and down as well, exactly as her own missiles
    fixed_t h = finecosine[victim->pitch >> ANGLETOFINESHIFT];

    dz = FixedDiv(-finesine[victim->pitch >> ANGLETOFINESHIFT], h ? h : 1);   // slope
  }
  // start just in front of her, on the aim line
  missile->bg_rx = victim->x + FixedMul(dx, victim->radius + missile->radius + 2 * FRACUNIT);
  missile->bg_ry = victim->y + FixedMul(dy, victim->radius + missile->radius + 2 * FRACUNIT);
  missile->bg_rz = victim->z + 32 * FRACUNIT;
  {
    fixed_t len = bg_Length3(FRACUNIT, 0, dz);    // |(1, slope)| scales the horizontal part to the speed
    fixed_t hs = FixedDiv(speed, len);

    missile->momx = FixedMul(dx, hs);
    missile->momy = FixedMul(dy, hs);
    missile->momz = FixedMul(dz, hs);
  }
  dist = FRACUNIT;
  missile->angle = R_PointToAngle2(0, 0, missile->momx, missile->momy);
  missile->bg_redirected = 1;
  missile->bg_guide = aim ? 0 : BG_PARRY_GUIDE_TICS;   // a monster under the crosshair already takes it exactly
  missile->bg_guide_start = victim->angle;
  lprintf(LO_INFO, "PARRY: ball turned at tic %d, facing %.1f, pitch %.1f, %s\n", leveltime, (double) victim->angle / (double) ANG1,
          (double) (int) victim->pitch / (double) ANG1, aim ? "autoaim on a monster" : "along the crosshair");
  if (missile->tracer)
  {
    // a homing missile (revenant) seeks the monster under the crosshair, else the nearest other
    // monster it can see, and only if there is none does it go back to the one that fired it
    mobj_t* seek = aim ? aim : bg_ParryNearest(victim, shooter);

    P_SetTarget(&missile->tracer, seek ? seek : shooter);
  }
  P_SetTarget(&missile->target, victim);
  missile->bg_dmgscale = BG_PARRY_DAMAGE_PCT;

  // she is shoved straight back from where she looks, never sideways: a sideways shove would slide the crosshair
  // line off the one the ball left on
  (void) push_angle;
  bg_ParryEffect(player, missile->x, missile->y, missile->z, victim->angle + ANG180,
                 missile->info->damage * 4); // 4 = a typical roll of the missile's damage multiplier
  return true;
}

// A parried hitscan shot is fired again by the player at the start of the next tic, exactly like a
// bullet from a gun: it goes to the monster under the crosshair, or straight ahead, and it hits
// walls too, so it can trigger shootable switches. Twice the damage.
void bg_FireDeflected(player_t* player)
{
  mobj_t* mo = player->mo;
  int damage = player->bgdeflect * BG_PARRY_DAMAGE_PCT / 100;
  fixed_t range = 16 * 64 * FRACUNIT;
  fixed_t slope;

  player->bgdeflect = 0;
  if (!mo || mo->health <= 0)
    return;

  slope = P_AimLineAttack(mo, mo->angle, range, 0);
  P_LineAttack(mo, mo->angle, range, slope, damage);
}

dboolean bg_ParryDamage(mobj_t* target, mobj_t* inflictor, mobj_t* source, int damage)
{
  player_t* player = target->player;
  fixed_t dist;
  dboolean melee;

  if (!player || player->bgparry <= 0 || !source || source == target || (source->player && !bg_FriendlyFireOn()))
    return false;
  if (inflictor && (inflictor->flags & MF_MISSILE))
    return false; // missiles are handled when they touch the player
  if (inflictor && inflictor != source)
    return false; // barrels, crushers and the like
  if (!bg_ParryFacing(target, source->x, source->y))
    return false;

  dist = P_AproxDistance(target->x - source->x, target->y - source->y);
  melee = dist <= 80 * FRACUNIT + source->radius;

  if (melee)
  {
    // the attacker is thrown off balance: pushed back and stunned (bosses are only pushed)
    angle_t ang = R_PointToAngle2(target->x, target->y, source->x, source->y);

    bg_StunMobjRoll(source, BG_PARRY_STUN, false);
    if (!(source->flags & MF_NOCLIP))
    {
      source->momx += FixedMul(BG_PARRY_PUSH, finecosine[ang >> ANGLETOFINESHIFT]);
      source->momy += FixedMul(BG_PARRY_PUSH, finesine[ang >> ANGLETOFINESHIFT]);
    }
  }
  else
  {
    // bullets are fired back where the player aims (see bg_FireDeflected); several pellets in one
    // tic add up into one shot
    player->bgdeflect += damage > 0 ? damage : 1;
  }

  bg_ParryEffect(player, (target->x + source->x) / 2, (target->y + source->y) / 2,
                 target->z + (target->height >> 1),
                 player->mo->angle + ANG180, damage);   // straight back from where she looks, as for missiles
  return true;
}

void bg_MissileHit(mobj_t* missile, mobj_t* target)
{
  if (missile->type == bg_tornado_type && (target->flags & MF_SHOOTABLE) && target->health > 0)
  {
    P_SetTarget(&missile->tracer, target);
    bg_StunMobj(target, BG_TORNADO_STUN);
  }
}

static void A_BGTornadoStick(mobj_t* mo)
{
  mobj_t* t = mo->tracer;

  if (!t || t->player || t->health <= 0 || !(t->flags & MF_SHOOTABLE))
    return; // nothing to stick to: it just fizzles out in the normal explosion

  mo->flags &= ~MF_MISSILE;
  mo->flags |= MF_NOCLIP | MF_NOGRAVITY;
  mo->momx = mo->momy = mo->momz = 0;
  mo->reactiontime = BG_STICK_TICKS;
  if (getenv("BEGOTTEN_TRACE")) { fprintf(stderr, "STICK on type %d\n", t->type); fflush(stderr); }
  P_SetMobjState(mo, bg_stick_state);
}

static void A_BGTornadoTick(mobj_t* mo)
{
  mobj_t* t = mo->tracer;

  if (!t || t->health <= 0 || --mo->reactiontime <= 0)
  {
    P_SetTarget(&mo->tracer, NULL);
    P_SetMobjState(mo, bg_torn_end_state);
    return;
  }

  P_UnsetThingPosition(mo);
  mo->x = t->x;
  mo->y = t->y;
  mo->z = t->z + (t->height >> 1);
  P_SetThingPosition(mo);

  // weak, continuous damage; the target's pain chance decides whether the stun holds
  {
    // the damage is not a push: keep the target's momentum as it was
    fixed_t mx = t->momx, my = t->momy, mz = t->momz;
    P_DamageMobj(t, mo, mo->target, 1 + (P_Random(pr_mbf21) & 1));
    if (t->health > 0)
    {
      t->momx = mx;
      t->momy = my;
      t->momz = mz;
    }
  }
  bg_StunMobj(t, BG_TORNADO_STUN);
}

static int bg_parry_recover_state, bg_parry_ready_state;

// raising the guard: the player plants his feet, the window opens
static void A_BGParry(player_t* player, pspdef_t* psp)
{
  player->bgparry = bg_ClassParryWindow(player);
  player->bgparryhit = 0;
  player->mo->momx = player->mo->momy = 0;
  player->momx = player->momy = 0; // the walking motion that drives the view bob
}

// the guard stays up while the window is open (every parried blow renews it); when it closes
// the player recovers, unless a blow was parried, which spares the recovery
static void A_BGParryHold(player_t* player, pspdef_t* psp)
{
  if (player->bgparry > 0)
    return;
  P_SetPsprite(player, ps_weapon, player->bgparryhit ? bg_parry_ready_state : bg_parry_recover_state);
}


//
// Swordsman: slash, charged stab, riposte
//
// The attack button on the blade works like this (classes with BGCLASS "charge"; other classes just punch):
//  - a click (released within BG_TAP_TICS) is the normal blade attack;
//  - held longer, the sword is drawn back (only the tip shows) and charges until it is released;
//  - on release the player is locked in place for a moment, then propelled forward with the stab. Where he
//    was already moving (a run, a jump) the lunge adds to it, so a mid-air stab goes further. A target hit
//    is pushed away with the player's momentum;
//  - a click on a stunned monster is a riposte instead: step back, then a hard stab that ends the stun.
//

#define BG_TAP_TICS 9                    // a release before this is a click
#define BG_STAB_LOCK 3                   // tics the player is held still before a stab
#define BG_STAB_TICKS 9                  // tics the lunge looks for something to hit
#define BG_STAB_REACH (96 * FRACUNIT)    // how far ahead a stab hits
#define BG_RIPOSTE_SEEK (192 * FRACUNIT) // how far away a stunned monster can be to riposte it
#define BG_RIPOSTE_WIND 8                // tics of the riposte's wind-up
#define BG_RIPOSTE_STEP (4 * FRACUNIT)   // the small step back
#define BG_STAB_MIN_PCT 25               // a stab that lands at a crawl still does this much of its damage
#define BG_STAB_KEEP 25                  // percent of the player's speed left after a hit (the rest went into the target)
#define BG_STUN_DAMAGE_PCT 150           // damage multiplier on a stunned monster, from any player

static int bg_slash_state, bg_blade_hold_state, bg_blade_charge_state, bg_stab_lock_state;
static int bg_stab_go_state, bg_rip_wind_state, bg_rip_go_state, bg_stab_recover_state;

int bg_StunDamage(const mobj_t* target, const mobj_t* source, int damage)
{
  if (damage > 0 && target && source && source->player && target->bg_stun > 0)
    return damage * BG_STUN_DAMAGE_PCT / 100;
  return damage;
}

// the monster in front of the player (friends last, as the punch does); sets *slope
static mobj_t* bg_BladeAim(mobj_t* mo, fixed_t range, fixed_t* slope)
{
  *slope = P_AimLineAttack(mo, mo->angle, range, MF_FRIEND);
  if (!linetarget)
    *slope = P_AimLineAttack(mo, mo->angle, range, 0);
  return linetarget;
}

static void bg_Lunge(mobj_t* mo, fixed_t speed, dboolean from_rest)
{
  int an = mo->angle >> ANGLETOFINESHIFT;

  if (from_rest && mo->z <= mo->floorz)
    mo->momx = mo->momy = 0; // on the ground a lunge starts from a standstill; in the air it adds to the jump
  mo->momx += FixedMul(speed, finecosine[an]);
  mo->momy += FixedMul(speed, finesine[an]);
}

static void A_BGBladeStart(player_t* player, pspdef_t* psp)
{
  int rip = bg_ClassRiposte(player);

  player->bgcharge = 0;
  if (bg_ClassChargeTics(player) <= 0)
  {
    P_SetPsprite(player, ps_weapon, bg_slash_state);
    return;
  }
  if (rip > 0)
  {
    fixed_t slope;
    mobj_t* t = bg_BladeAim(player->mo, BG_RIPOSTE_SEEK, &slope);

    if (t && t->bg_stun > 0 && t->health > 0 && !t->player)
    {
      player->bgstabdmg = rip;
      P_SetPsprite(player, ps_weapon, bg_rip_wind_state);
      return;
    }
  }
  P_SetPsprite(player, ps_weapon, bg_blade_hold_state);
}

// the first moments: still a click until the button has been down for BG_TAP_TICS
static void A_BGBladeHold(player_t* player, pspdef_t* psp)
{
  if (!(player->cmd.buttons & BT_ATTACK))
  {
    P_SetPsprite(player, ps_weapon, bg_slash_state);
    return;
  }
  if (++player->bgcharge >= BG_TAP_TICS)
    P_SetPsprite(player, ps_weapon, bg_blade_charge_state);
}

// charging: the sword is drawn back; releasing the button stabs
static void A_BGBladeCharge(player_t* player, pspdef_t* psp)
{
  if (player->cmd.buttons & BT_ATTACK)
  {
    if (player->bgcharge < 10000)
      ++player->bgcharge;
    if (player->bgcharge - BG_TAP_TICS == bg_ClassChargeTics(player))
      S_StartMobjSound(player->mo, sfx_itemup); // fully charged
    return;
  }
  {
    int held = player->bgcharge - BG_TAP_TICS;

    player->bgstabdmg = bg_ClassChargeDamage(player, held);
    player->bgstabspd = bg_ClassLunge(player, held);
  }
  P_SetPsprite(player, ps_weapon, bg_stab_lock_state);
}

// planted before the stab (only on the ground: in the air there is nothing to plant)
static void A_BGStabLock(player_t* player, pspdef_t* psp)
{
  mobj_t* mo = player->mo;

  if (mo->z <= mo->floorz)
  {
    mo->momx = mo->momy = 0;
    player->momx = player->momy = 0;
  }
}

// one tic of the lunge: look for something to hit
static void bg_StabStrike(player_t* player, dboolean riposte)
{
  mobj_t* mo = player->mo;
  fixed_t slope, spd, push;
  mobj_t* t = bg_BladeAim(mo, BG_STAB_REACH, &slope);
  int an;

  if (!t)
    return;

  // the stab is as strong as the player is fast at this moment: full while the lunge still carries (and in the
  // air, where nothing slows it), less as the ground slows her, never less than BG_STAB_MIN_PCT
  spd = P_AproxDistance(mo->momx, mo->momy);
  {
    fixed_t ref = player->bgstabspd;
    int pct = 100, dmg = player->bgstabdmg;

    if (ref > 0 && !riposte)   // a riposte is always the full hit
    {
      pct = (int) ((long long) spd * 100 / ref);
      if (pct > 100)
        pct = 100;
      if (pct < BG_STAB_MIN_PCT)
        pct = BG_STAB_MIN_PCT;
    }
    dmg = dmg * pct / 100;
    if (dmg < 1 && player->bgstabdmg > 0)
      dmg = 1;
    S_StartMobjSound(mo, sfx_punch);
    P_LineAttack(mo, mo->angle, BG_STAB_REACH, slope, dmg);
  }

  // the player's momentum goes into the target: heavy things hardly move
  if (spd < 2 * FRACUNIT)
    spd = 2 * FRACUNIT;
  push = (fixed_t) ((long long) spd * 100 / (t->info->mass > 100 ? t->info->mass : 100));
  if (push > 40 * FRACUNIT)
    push = 40 * FRACUNIT;
  an = mo->angle >> ANGLETOFINESHIFT;
  t->momx = FixedMul(push, finecosine[an]);
  t->momy = FixedMul(push, finesine[an]);
  if (riposte && t->health > 0)
    t->bg_stun = 0; // the stun is spent

  mo->momx = mo->momx * BG_STAB_KEEP / 100;
  mo->momy = mo->momy * BG_STAB_KEEP / 100;
  mo->angle = R_PointToAngle2(mo->x, mo->y, t->x, t->y);
  R_SmoothPlaying_Reset(player);
  P_SetPsprite(player, ps_weapon, bg_stab_recover_state);
}

static void A_BGStabGo(player_t* player, pspdef_t* psp)
{
  bg_Lunge(player->mo, player->bgstabspd, true);
  bg_StabStrike(player, false);
}

static void A_BGStabThrust(player_t* player, pspdef_t* psp)
{
  bg_StabStrike(player, false);
}

// riposte: a small step back while the stun is held open, then the thrust
static void A_BGRipWind(player_t* player, pspdef_t* psp)
{
  fixed_t slope;
  mobj_t* t = bg_BladeAim(player->mo, BG_RIPOSTE_SEEK, &slope);

  if (t && t->bg_stun > 0)
    bg_StunMobjRoll(t, BG_RIPOSTE_WIND + BG_STAB_TICKS + 4, false);
  if (player->bgcharge == 0)
    bg_Lunge(player->mo, -BG_RIPOSTE_STEP, false);
  if (++player->bgcharge >= BG_RIPOSTE_WIND)
    P_SetPsprite(player, ps_weapon, bg_rip_go_state);
}

static void A_BGRipGo(player_t* player, pspdef_t* psp)
{
  player->bgstabspd = bg_ClassLunge(player, bg_ClassChargeTics(player));
  bg_Lunge(player->mo, player->bgstabspd, true);
  bg_StabStrike(player, true);
}

static void A_BGRipThrust(player_t* player, pspdef_t* psp)
{
  bg_StabStrike(player, true);
}

// What the swordsman's blade is doing, for the third-person sprites other players see:
// 0 nothing, 1 the sword is drawn back (charging, the pause before the lunge, the riposte's wind-up), 2 lunging.
int bg_StabPhase(const player_t* player)
{
  const state_t* st = player ? player->psprites[ps_weapon].state : NULL;
  int i;

  if (!st || !bg_blade_charge_state)
    return 0;
  i = (int) (st - states);
  if (i == bg_blade_charge_state || i == bg_rip_wind_state ||
      (i >= bg_stab_lock_state && i < bg_stab_lock_state + 3))
    return 1;
  if ((i >= bg_stab_go_state && i <= bg_stab_recover_state) ||
      (i >= bg_rip_go_state && i <= bg_rip_go_state + BG_STAB_TICKS))
    return 2;
  return 0;
}

// the weapon state of a phase (for the pose self test)
const state_t* bg_StabPhaseState(int phase)
{
  return phase == 1 ? &states[bg_blade_charge_state] : phase == 2 ? &states[bg_stab_go_state] : NULL;
}

// The thunderclap: no projectile and no ray. At the moment of the clap every shootable thing in front of the
// player (a wide arc, in sight, within range) is hurt and thrown away from her, each along the line from her
// to it, so the blast spreads out as a ring in front of her. It is worked out from the player's heading at
// that instant, so it follows her turning, and it cannot get lost in a big map.
#define BG_CLAP_RANGE (1280 * FRACUNIT)
#define BG_CLAP_ARC   (ANG1 * 55)       // either side of where she faces
#define BG_CLAP_BACK  1500              // launch speed, divided by the target's mass (an imp has 100)
#define BG_CLAP_UP    350

// how hard the clap pushes the clapper back, in percent of the soldier's
static int bg_ClapRecoilPct(const player_t* player)
{
  if (player->bgclass == bg_ClassFromWord("witch"))
    return 45;
  if (player->bgclass == bg_ClassFromWord("swordsman"))
    return 75;
  return 100;
}

int bg_ClapBlast(player_t* player)
{
  mobj_t* mo = player->mo;
  thinker_t* th;
  int hit = 0;

  for (th = thinkercap.next; th != &thinkercap; th = th->next)
  {
    mobj_t* m;
    angle_t err, an;
    fixed_t dist, power;
    int damage, pct;

    if (th->function != P_MobjThinker)
      continue;
    m = (mobj_t*) th;
    if (m == mo || !(m->flags & MF_SHOOTABLE) || m->health <= 0)
      continue;
    if (m->player && (!bg_FriendlyFireOn() || bg_CoopShield(mo, m)))
      continue;
    dist = P_AproxDistance(m->x - mo->x, m->y - mo->y);
    if (dist > BG_CLAP_RANGE + m->radius)
      continue;
    an = R_PointToAngle2(mo->x, mo->y, m->x, m->y);
    err = an - mo->angle;
    if (err > ANG180)
      err = (angle_t) (0 - err);
    if (dist > 64 * FRACUNIT && err > BG_CLAP_ARC)   // right beside her counts from any side of the front
      continue;
    if (!P_CheckSight(mo, m))
      continue;

    pct = 100 - 40 * (dist > BG_CLAP_RANGE ? FRACUNIT : FixedDiv(dist, BG_CLAP_RANGE)) / FRACUNIT;   // 100% close, 60% at the edge
    damage = (80 + (P_Random(pr_mbf21) % 41)) * pct / 100;
    P_DamageMobj(m, mo, mo, damage);
    ++hit;

    if (m->health > 0 || m->info->mass < 10000000)
    {
      if (m->info->mass < 10000000 && !(m->flags & MF_NOCLIP))
      {
        power = FixedMul((BG_CLAP_BACK * FRACUNIT) / m->info->mass, pct * FRACUNIT / 100);
        if (!(m->flags & MF_NOGRAVITY))
          m->momz += (BG_CLAP_UP * FRACUNIT) / m->info->mass;
        an >>= ANGLETOFINESHIFT;
        m->momx += FixedMul(power, finecosine[an]);
        m->momy += FixedMul(power, finesine[an]);
      }
    }
  }
  return hit;
}

static void A_BGClap(player_t* player, pspdef_t* psp)
{
  mobj_t* mo = player->mo;

  S_StartSound(mo, sfx_rxplod);
  if (bg_clap_type > 0)
    P_SpawnMobj(mo->x, mo->y, mo->z + 28 * FRACUNIT, bg_clap_type);   // the shock ring

  bg_ClapBlast(player);

  // she is thrown backwards by it
  if (!(mo->flags & MF_NOCLIP))
    P_Thrust(player, mo->angle + ANG180, FixedMul(2300 * 330, bg_ClapRecoilPct(player) * FRACUNIT / 100));
}

//
// Weapon mode tables
//

static int bg_pickup_type[6] = { -1, -1, -1, -1, -1, -1 };

typedef struct {
  int ammo;
  int clips;
  const char* message;
} bg_pickup_t;

static const bg_pickup_t bg_pickups[6] = {
  { am_belt,     1, "Picked up a belt of rounds." },
  { am_belt,     5, "Picked up a crate of belts." },
  { am_spirit,   1, "Picked up a spirit rune." },
  { am_spirit,   5, "Picked up a pack of spirit runes." },
  { am_hellfire, 1, "Picked up some hellfire." },
  { am_hellfire, 5, "Picked up a cask of hellfire." }
};

#define BG_CHAINGUN_WINDUP 12 // extra tics for the SS-sprite chaingun guy (84) on top of BG_HITSCAN_WINDUP (BGMOBJ "84 windup N" overrides the total)
#define BG_HITSCAN_WINDUP 2 // extra tics of wind-up before zombieman / shotgunner / chaingunner fire
#define BG_BURST_GAP 6      // zombieman: extra aiming tics between the shots of a burst of three

static int bg_MobjState(int spr, int frame, int tics, bg_action_t act, int next)
{
  return S0(spr, frame, tics, act, next);
}

//
// Volleys: chaingunners fire 30 shots and SS guards 10, then break off and wander for a while
//

#define BG_VOLLEY_CHAINGUNNER 30
#define BG_VOLLEY_SS 10

static void A_BGVolleyStart(mobj_t* actor)
{
  actor->bg_volley = 0;
  A_FaceTarget(actor);
}

static void A_BGCPosAttack(mobj_t* actor)
{
  A_CPosAttack(actor);
  ++actor->bg_volley;
}

static void A_BGCPosRefire(mobj_t* actor)
{
  int limit = actor->type == MT_WOLFSS ? BG_VOLLEY_SS : BG_VOLLEY_CHAINGUNNER;

  if (actor->bg_volley >= limit)
  {
    actor->bg_volley = 0;
    actor->reactiontime = 8 + (P_Random(pr_cposrefire) & 7); // no new attack for a second or so
    P_SetMobjState(actor, actor->info->seestate);
    return;
  }
  A_CPosRefire(actor);
}

static void bg_Volleys(int first, int last)
{
  int i;

  for (i = first; i <= last; ++i)
  {
    if (states[i].action == (actionf_t) A_CPosAttack)
      states[i].action = (actionf_t) A_BGCPosAttack;
    else if (states[i].action == (actionf_t) A_CPosRefire)
      states[i].action = (actionf_t) A_BGCPosRefire;
  }
  if (states[first].action == (actionf_t) A_FaceTarget)
    states[first].action = (actionf_t) A_BGVolleyStart;
}

static int bg_CloneState(const state_t* from, int next)
{
  int idx = bg_state_next++;
  state_t* st = dsda_ExtendStates(idx);

  *st = *from;
  st->nextstate = (next == NEXT) ? idx + 1 : next;
  return idx;
}

extern fixed_t bulletslope;
void P_BulletSlope(mobj_t* mo);

// Revolver primary: one bullet, damage = (1..mod) x damage (BGWEAPON "pistolshot" line). Zombie-type troopers use
// the same numbers (scaled by their foedamage), so the player and the pistol troopers share one rule.
static int bg_pistol_damage = 10, bg_pistol_mod = 3, bg_pistol_spread = 40; // spread: tenths of a degree

int bg_PistolRoll(int random)
{
  return (random % bg_pistol_mod + 1) * bg_pistol_damage;
}

static void A_BgPistolShot(player_t* player, pspdef_t* psp)
{
  int spread = (int) (((long long) bg_pistol_spread * FRACUNIT / 10) * bg_ClassSpreadPct(player) / 100);
  angle_t angle;

  (void) psp;
  P_BulletSlope(player->mo);
  angle = player->mo->angle + (angle_t) P_RandomHitscanAngle(pr_mbf21, spread);
  P_LineAttack(player->mo, angle, MISSILERANGE, bulletslope, bg_PistolRoll(P_Random(pr_mbf21)));
}

// Revolver secondary: one bullet that goes through monsters (BGWEAPON "pierce" line).
static int bg_pierce_damage = 6, bg_pierce_mod = 3, bg_pierce_hits = 5, bg_pierce_falloff = 85;
static int bg_pierce_spread = 0; // tenths of a degree



// portable line splitter (MSVC has no strtok_r): returns the next non-empty line of a writable buffer
static char* bg_NextLine(char** cursor)
{
  char* p = *cursor;
  char* start;

  while (*p == '\r' || *p == '\n')
    ++p;
  if (!*p)
  {
    *cursor = p;
    return NULL;
  }
  start = p;
  while (*p && *p != '\r' && *p != '\n')
    ++p;
  if (*p)
    *p++ = '\0';
  *cursor = p;
  return start;
}

// ---- music lines for Hexen-format maps ----
// Specials 212 (music change) and 213 (music stop) cannot carry a lump name in a binary map, so 212 takes a
// song NUMBER that the BGMUSIC lump maps to a lump name ("<number> <LUMPNAME>" per line, # comments).
// 212 args: 1 song number, 2 local (1 = only the player who crossed it hears the change), 3 play once (1) or loop (0)
// 213 args: 1 local (1 = only the player who crossed it)
// They are turned into Music_ChangeSong / Music_Stop when the map loads. A different song always starts from its
// beginning; crossing the line again while that song is already playing does nothing.
static int bg_MusicLumpForNumber(int number)
{
  int lump = W_CheckNumForName("BGMUSIC");
  char* text;
  char* line;
  char* save = NULL;
  int result = LUMP_NOT_FOUND;

  if (lump == LUMP_NOT_FOUND)
    return LUMP_NOT_FOUND;
  text = Z_Malloc(W_LumpLength(lump) + 1);
  memcpy(text, W_LumpByNum(lump), W_LumpLength(lump));
  text[W_LumpLength(lump)] = '\0';
  for (save = text; (line = bg_NextLine(&save)) != NULL;)
  {
    int n;
    char name[16];

    if (line[0] == '#' || line[0] == '/')
      continue;
    if (sscanf(line, "%d %15s", &n, name) == 2 && n == number)
      result = W_CheckNumForName(name); // later lines win
  }
  Z_Free(text);
  if (result == LUMP_NOT_FOUND)
    lprintf(LO_WARN, "BGMUSIC: no usable lump for song number %d\n", number);
  return result;
}

// BGMUSIC "fade <tics>" (default 35 = 1 s; 0 = no fade): how long the old song fades out before a music line changes or stops it
int bg_MusicFadeTics(void)
{
  int lump = W_CheckNumForName("BGMUSIC");
  int result = 35;
  char* text;
  char* line;
  char* save = NULL;

  if (lump == LUMP_NOT_FOUND)
    return result;
  text = Z_Malloc(W_LumpLength(lump) + 1);
  memcpy(text, W_LumpByNum(lump), W_LumpLength(lump));
  text[W_LumpLength(lump)] = '\0';
  for (save = text; (line = bg_NextLine(&save)) != NULL;)
  {
    int n;

    if (sscanf(line, "fade %d", &n) == 1 && n >= 0)
      result = n;
  }
  Z_Free(text);
  return result;
}

void bg_TranslateMusicLine(short* special, int* args)
{
  if (*special == zl_bg_music_change)
  {
    int lump = bg_MusicLumpForNumber(args[0]);

    args[0] = lump;
    args[1] = args[1] != 0;
    args[2] = args[2] == 0;
    *special = lump == LUMP_NOT_FOUND ? 0 : zl_music_change_song;
  }
  else
  {
    args[0] = args[0] != 0;
    *special = zl_music_stop;
  }
}

// --- Checkpoints -------------------------------------------------------------------------------------------
// A checkpoint line saves the game when you are alone. In a co-op game it will feed the ticket pool (phase 2).
// The save is never made from inside the line special: the world is in the middle of a thinker, so it is
// queued and written between tics.
static int bg_checkpoint_pending;
static int bg_checkpoint_count;      // how many checkpoints were crossed since the level started

void M_NamedAutoSave(const char* prefix, const char* message);

int bg_Checkpoint(mobj_t* mo, struct line_s* line)
{
  if (!mo || !mo->player)
    return 0;
  if (netgame)
  {
    bg_CoopCheckpoint(line);      // co-op: feeds the ticket pool (difficulty 4-5), never saves
    return 1;
  }
  bg_checkpoint_pending = 1;
  bg_checkpoint_count++;
  return 1;
}

void bg_CheckpointNewLevel(void)
{
  bg_checkpoint_pending = 0;
  bg_checkpoint_count = 0;
}

int bg_CheckpointCount(void)
{
  return bg_checkpoint_count;
}

void bg_CheckpointUpdate(void)
{
  if (!bg_checkpoint_pending)
    return;
  if (gamestate != GS_LEVEL || gameaction != ga_nothing)
    return;                          // try again on a later tic
  bg_checkpoint_pending = 0;
  if (demoplayback || demorecording || netgame)
    return;                          // co-op: handled by the ticket rules, never a save
  M_NamedAutoSave("checkpoint", "checkpoint");
}

int bg_PiercePercent(void)
{
  return bg_pierce_falloff;
}

static void A_BgPierceShot(player_t* player, pspdef_t* psp)
{
  int damage, slope;
  angle_t angle;
  int spread = (int) (((long long) bg_pierce_spread * FRACUNIT / 10) * bg_ClassSpreadPct(player) / 100);

  (void) psp;
  P_BulletSlope(player->mo);
  damage = (P_Random(pr_mbf21) % bg_pierce_mod + 1) * bg_pierce_damage;
  angle = player->mo->angle + (angle_t) P_RandomHitscanAngle(pr_mbf21, spread);
  slope = bulletslope;
  bg_PierceAttack(player->mo, angle, MISSILERANGE, slope, damage, bg_pierce_hits);
}

static void bg_DefineStates(void)
{
  weaponinfo_t* w = doom_weaponinfo;
  int s;

  // ---- slot 2: the vanilla pistol states become the secondary (the strong revolver shot);
  // the primary is a weaker, slower pistol ----
  w[wp_dualpistols] = w[wp_pistol];

  s = S0(SPR_PISG, 0, 0, A_ConsumeAmmo, NEXT);
  bg_S(SPR_PISG, 0, 0, A_WeaponSound, NEXT, sfx_pistol, 0, 0, 0, 0);
  S0(SPR_PISG, 0, 0, A_GunFlash, NEXT);
  S0(SPR_PISG, 1, 6, A_BgPistolShot, NEXT);
  S0(SPR_PISG, 2, 6, NULL, NEXT);
  S0(SPR_PISG, 1, 1, A_ReFire, S_PISTOL);
  w[wp_pistol].atkstate = s;
  w[wp_pistol].flashstate = S_PISTOLFLASH;

  // ---- slot 2 secondary: revolver. One heavy bullet that goes through up to 5 monsters (each
  // takes less than the one before), slow to recover: line the targets up before you fire ----
  s = S0(SPR_PISG, 0, 0, A_ConsumeAmmo, NEXT);
  bg_S(SPR_PISG, 0, 0, A_WeaponSound, NEXT, sfx_pistol, 0, 0, 0, 0);
  S0(SPR_PISG, 0, 0, A_GunFlash, NEXT);
  S0(SPR_PISG, 1, 8, A_BgPierceShot, NEXT);
  S0(SPR_PISG, 2, 10, NULL, NEXT);
  S0(SPR_PISG, 1, 10, NULL, NEXT);
  S0(SPR_PISG, 0, 12, NULL, NEXT);
  S0(SPR_PISG, 0, 1, A_ReFire, S_PISTOL);
  w[wp_dualpistols].atkstate = s;
  w[wp_dualpistols].flashstate = S_PISTOLFLASH;

  // ---- slot 4 secondary: chaingun (fast, wide) ----
  s = S0(SPR_CHGG, 0, 0, A_ConsumeAmmo, NEXT);
  bg_S(SPR_CHGG, 0, 0, A_WeaponSound, NEXT, sfx_pistol, 0, 0, 0, 0);
  S0(SPR_CHGG, 0, 0, A_GunFlash, NEXT);
  bg_S(SPR_CHGG, 0, 2, A_WeaponBulletAttack, NEXT, DEG(9.0), DEG(2.0), 1, 5, 3);
  S0(SPR_CHGG, 1, 2, A_ReFire, S_CHAIN);
  w[wp_chainfire] = w[wp_chaingun];
  w[wp_chainfire].atkstate = s;
  w[wp_chainfire].flashstate = S_CHAINFLASH1;

  // ---- slot 6: tornado (primary): charge, then fire a projectile that sticks ----
  // Its own first-person sprite (TRNG: A hands ready, B hands apart with a small whirl, C the whirl grown, D released)
  // and no muzzle flash, like arc lightning: no devil's breath hands any more.
  {
    int trng = dsda_AddSpriteName("TRNG");
    int up, down, ready;

    up = S0(trng, 0, 1, A_Raise, NEXT);
    states[up].nextstate = up;
    down = S0(trng, 0, 1, A_Lower, NEXT);
    states[down].nextstate = down;
    ready = S0(trng, 0, 1, A_WeaponReady, NEXT);
    states[ready].nextstate = ready;
    bg_trng_ready_state = ready;

    s = bg_S(trng, 1, 4, A_WeaponSound, NEXT, sfx_plasma, 0, 0, 0, 0);
    S0(trng, 1, 3, NULL, NEXT);
    S0(trng, 2, 3, NULL, NEXT);
    S0(trng, 1, 3, NULL, NEXT);
    S0(trng, 2, 3, NULL, NEXT);
    S0(trng, 1, 3, NULL, NEXT);
    S0(trng, 2, 3, NULL, NEXT);
    S0(trng, 2, 0, A_ConsumeAmmo, NEXT);
    bg_S(trng, 2, 0, A_WeaponSound, NEXT, sfx_pistol, 0, 0, 0, 0);
    S0(trng, 3, 4, A_BGTornadoFire, NEXT);
    S0(trng, 0, 14, A_ReFire, ready);
    w[wp_tornado] = w[wp_plasma];
    w[wp_tornado].upstate = up;
    w[wp_tornado].downstate = down;
    w[wp_tornado].readystate = ready;
    w[wp_tornado].atkstate = s;
    w[wp_tornado].flashstate = S_NULL;
  }

  // the tornado projectile's death: stick to what it hit, or explode
  // The tornado: a spinning sphere (TRNP A-D) in flight; where it hits a monster it becomes a funnel (TRNF A-D)
  // wrapped round the target (like the arch-vile's flames) and dies down (TRNP E-F)
  {
    int trnp = dsda_AddSpriteName("TRNP");
    int trnf = dsda_AddSpriteName("TRNF");
    int k, spin;

    bg_torn_end_state = S0(trnp, 32772, 4, NULL, NEXT);
    S0(trnp, 32773, 4, NULL, S_NULL);
    spin = S0(trnp, 32768, 2, NULL, NEXT);
    S0(trnp, 32769, 2, NULL, NEXT);
    S0(trnp, 32770, 2, NULL, NEXT);
    S0(trnp, 32771, 2, NULL, spin);
    mobjinfo[bg_tornado_type].spawnstate = spin;
    bg_torn_death_state = S0(trnp, 32772, 0, (bg_action_t) A_BGTornadoStick, bg_torn_end_state);
    bg_stick_state = S0(trnf, 32768, 5, (bg_action_t) A_BGTornadoTick, NEXT);
    for (k = 1; k < 4; ++k)
      bg_stick_state2 = S0(trnf, 32768 + k, 5, (bg_action_t) A_BGTornadoTick, NEXT);
    states[bg_stick_state2].nextstate = bg_stick_state;
    mobjinfo[bg_tornado_type].deathstate = bg_torn_death_state;
  }

  // The other projectiles, one picture each: fireball (FBAL), devil's breath flame (DBRT), arc lightning bolt (ARCB),
  // hellfire's alien rocket (HLFR, 8 directions) and the thunderclap's ring (CLAP)
  {
    int fbal = dsda_AddSpriteName("FBAL");
    int dbrt = dsda_AddSpriteName("DBRT");
    int arcb = dsda_AddSpriteName("ARCB");
    int hlfr = dsda_AddSpriteName("HLFR");
    int clap = dsda_AddSpriteName("CLAP");
    int st;

    st = S0(fbal, 32768, 2, NULL, NEXT);
    S0(fbal, 32769, 2, NULL, st);
    mobjinfo[bg_fireball_type].spawnstate = st;
    mobjinfo[bg_fireball_type].deathstate = S0(fbal, 32770, 8, (bg_action_t) A_Explode, NEXT);
    S0(fbal, 32771, 6, NULL, NEXT);
    S0(fbal, 32772, 4, NULL, S_NULL);

    st = S0(dbrt, 32768, 2, NULL, NEXT);
    S0(dbrt, 32769, 2, NULL, st);
    mobjinfo[bg_breath_type].spawnstate = st;
    mobjinfo[bg_breath_type].deathstate = S0(dbrt, 32770, 3, NULL, NEXT);
    S0(dbrt, 32771, 3, NULL, S_NULL);

    st = S0(arcb, 32768, 1, NULL, NEXT);
    S0(arcb, 32769, 1, NULL, st);
    mobjinfo[bg_arcbolt_type].spawnstate = st;
    mobjinfo[bg_arcbolt_type].deathstate = S0(arcb, 32770, 3, NULL, NEXT);
    S0(arcb, 32771, 3, NULL, S_NULL);

    st = S0(hlfr, 32768, 1, NULL, NEXT);
    states[st].nextstate = st;
    mobjinfo[bg_hellfire_type].spawnstate = st;
    mobjinfo[bg_hellfire_type].deathstate = S0(hlfr, 32769, 8, (bg_action_t) A_Explode, NEXT);
    S0(hlfr, 32770, 6, NULL, NEXT);
    S0(hlfr, 32771, 4, NULL, S_NULL);

    st = S0(clap, 32768, 3, NULL, NEXT);
    S0(clap, 32769, 3, NULL, NEXT);
    S0(clap, 32770, 3, NULL, NEXT);
    S0(clap, 32771, 3, NULL, NEXT);
    S0(clap, 32772, 3, NULL, S_NULL);
    mobjinfo[bg_clap_type].spawnstate = st;
    mobjinfo[bg_clap_type].deathstate = S_NULL;

    // the fireball and the devil's breath no longer use the rocket's and the plasma bolt's attack codepointers
    states[S_PLASMA1].action = (actionf_t) A_BGBreath;
  }

  // ---- slot 6 secondary: arc lightning (a wide fan of small plasma bolts), cast from the bare hands: its own
  // first-person sprite (ARCG) and no muzzle flash, so no gun is drawn ----
  {
    int arcg = dsda_AddSpriteName("ARCG");

    s = S0(arcg, 0, 0, A_ConsumeAmmo, NEXT);
    bg_S(arcg, 0, 0, A_WeaponSound, NEXT, sfx_plasma, 0, 0, 0, 0);
    S0(arcg, 0, 0, A_GunFlash, NEXT);
    S0(arcg, 0, 4, A_BGArcFire, NEXT);
    S0(arcg, 1, 5, NULL, NEXT);
    S0(arcg, 2, 5, NULL, NEXT);
    S0(arcg, 1, 3, A_ReFire, bg_trng_ready_state);
    w[wp_arclightning] = w[wp_tornado];
    w[wp_arclightning].atkstate = s;
    w[wp_arclightning].flashstate = S_NULL;
  }

  // ---- slot 7: snap (primary) ----
  // The snap has its own first-person sprite (SNPG: ready / pinch / snap / open hand) so a class can draw
  // it as a hand gesture while its slot-1 weapon (the knife, the sword) keeps the PUNG sprite.
  {
    int snpg = dsda_AddSpriteName("SNPG");
    int up, down, ready;

    up = S0(snpg, 0, 1, A_Raise, NEXT);
    states[up].nextstate = up;
    down = S0(snpg, 0, 1, A_Lower, NEXT);
    states[down].nextstate = down;
    ready = S0(snpg, 0, 1, A_WeaponReady, NEXT);
    states[ready].nextstate = ready;

    s = S0(snpg, 1, 4, NULL, NEXT);
    S0(snpg, 2, 0, A_ConsumeAmmo, NEXT);
    S0(snpg, 2, 4, A_BGSnap, NEXT);
    S0(snpg, 3, 5, NULL, NEXT);
    S0(snpg, 2, 4, NULL, NEXT);
    S0(snpg, 1, 5, A_ReFire, ready);
    w[wp_snap] = w[wp_fist];
    w[wp_snap].upstate = up;
    w[wp_snap].downstate = down;
    w[wp_snap].readystate = ready;
    w[wp_snap].atkstate = s;
  }
  w[wp_snap].flashstate = S_NULL;
  w[wp_snap].flags = WPF_NOTHRUST;

  // ---- slot 7 secondary: thunderclap (the old BFG, now a blast in front of the player) ----
  // BFGG: A hands clasped (ready), B-G the hands drawn slowly apart with sparks (36 tics), H the clap, I after.
  // The third person pose (12 tics a frame) lines up with these: the clap lands on tic 36.
  s = S0(SPR_BFGG, 1, 6, A_BFGsound, NEXT);
  S0(SPR_BFGG, 2, 6, NULL, NEXT);
  S0(SPR_BFGG, 3, 6, NULL, NEXT);
  S0(SPR_BFGG, 4, 6, NULL, NEXT);
  S0(SPR_BFGG, 5, 6, NULL, NEXT);
  S0(SPR_BFGG, 6, 6, NULL, NEXT);
  S0(SPR_BFGG, 7, 0, A_ConsumeAmmo, NEXT);
  S0(SPR_BFGG, 7, 0, A_GunFlash, NEXT);
  S0(SPR_BFGG, 7, 6, A_BGClap, NEXT);
  S0(SPR_BFGG, 8, 10, NULL, NEXT);
  S0(SPR_BFGG, 8, 14, A_ReFire, S_BFG);
  w[wp_bfg].atkstate = s;

  // ---- slot 5 secondary: the fireball. A lead-up (18 tics: the orb forms between the hands), then the cast with a
  // light kick. MISG: A ready, B-D the orb growing, E the cast, F after. The third person pose is 6 tics a frame. ----
  s = bg_S(SPR_MISG, 1, 6, A_WeaponSound, NEXT, sfx_flamst, 0, 0, 0, 0);
  S0(SPR_MISG, 2, 6, NULL, NEXT);
  S0(SPR_MISG, 3, 6, NULL, NEXT);
  S0(SPR_MISG, 4, 0, A_GunFlash, NEXT);
  S0(SPR_MISG, 4, 6, A_BGFireball, NEXT);
  S0(SPR_MISG, 5, 10, NULL, NEXT);
  S0(SPR_MISG, 5, 14, A_ReFire, S_MISSILE);
  w[wp_missile].atkstate = s;

  // ---- slot 8: hellfire (the alien rocket launcher): its own first-person sprite (HLFG: ready, firing) and its own
  // muzzle flash (HLFF), so it no longer shares the fireball's hands ----
  {
    int hlfg = dsda_AddSpriteName("HLFG");
    int hlff = dsda_AddSpriteName("HLFF");
    int up, down, ready, fl;

    up = S0(hlfg, 0, 1, A_Raise, NEXT);
    states[up].nextstate = up;
    down = S0(hlfg, 0, 1, A_Lower, NEXT);
    states[down].nextstate = down;
    ready = S0(hlfg, 0, 1, A_WeaponReady, NEXT);
    states[ready].nextstate = ready;
    fl = S0(hlff, 32768, 3, A_Light1, NEXT);
    S0(hlff, 32769, 4, A_Light2, S_LIGHTDONE);

    s = S0(hlfg, 1, 0, A_ConsumeAmmo, NEXT);
    bg_S(hlfg, 1, 0, A_WeaponSound, NEXT, sfx_rlaunc, 0, 0, 0, 0);
    S0(hlfg, 1, 0, A_GunFlash, NEXT);
    S0(hlfg, 1, 3, A_BGHellfire, NEXT);
    S0(hlfg, 1, 0, A_BGRecoil, NEXT);
    // A_ReFire fires again at once, so the wait has to come before it: 3 + 4 tics = 5 shots a second
    S0(hlfg, 1, 4, NULL, NEXT);
    S0(hlfg, 1, 1, A_ReFire, ready);
    w[wp_hellfire] = w[wp_missile];
    w[wp_hellfire].upstate = up;
    w[wp_hellfire].downstate = down;
    w[wp_hellfire].readystate = ready;
    w[wp_hellfire].atkstate = s;
    w[wp_hellfire].flashstate = fl;
    w[wp_hellfire].flags = WPF_NOFLAG;
  }
  // ---- the knife's secondary for classes that can parry: the sword comes up in front of her at once (guard, frame D);
  // while the window is open (every parried blow renews it) she holds it there.
  //  - nothing is parried: the window closes and the sword swings slowly back to where it was (frames I, J) while she is open
  //  - a blow is parried: she bats it away, the sword whipping up and forwards (K L M), and ends up guarding on the
  //    other side (H, the guard mirrored); the next parried blow is batted the other way (N O P, mirrored) and so on.
  //    Ending the chain skips the recovery: just one frame back (J). ----
  {
    int hold_r, hold_l;

    s = S0(SPR_PUNG, 3, 1, A_BGParry, NEXT);
    hold_r = S0(SPR_PUNG, 3, 1, A_BGParryHold, NEXT);
    states[hold_r].nextstate = hold_r;
    hold_l = S0(SPR_PUNG, 7, 1, A_BGParryHold, NEXT);
    states[hold_l].nextstate = hold_l;
    bg_parry_swing_state[0] = S0(SPR_PUNG, 10, 2, NULL, NEXT);
    S0(SPR_PUNG, 11, 2, NULL, NEXT);
    S0(SPR_PUNG, 12, 4, NULL, hold_l);
    bg_parry_swing_state[1] = S0(SPR_PUNG, 13, 2, NULL, NEXT);
    S0(SPR_PUNG, 14, 2, NULL, NEXT);
    S0(SPR_PUNG, 15, 4, NULL, hold_r);
    bg_parry_recover_state = S0(SPR_PUNG, 8, 8, NULL, NEXT);
    S0(SPR_PUNG, 9, 8, NULL, S_PUNCH);
    bg_parry_ready_state = S0(SPR_PUNG, 9, 3, NULL, S_PUNCH);
  }
  w[wp_spare] = w[wp_fist];
  w[wp_spare].atkstate = s;
  w[wp_spare].flashstate = S_NULL;
  w[wp_spare].flags = WPF_NOTHRUST;

  // ---- the blade for classes with a held attack (BGCLASS "charge"): a click slashes, a held button
  // charges a stab, a click on a stunned monster is a riposte. Other classes go straight to the punch. ----
  bg_slash_state = w[wp_fist].atkstate;
  s = S0(SPR_PUNG, 1, 0, A_BGBladeStart, NEXT);
  bg_blade_hold_state = S0(SPR_PUNG, 1, 1, A_BGBladeHold, NEXT);
  states[bg_blade_hold_state].nextstate = bg_blade_hold_state;
  bg_blade_charge_state = S0(SPR_PUNG, 4, 1, A_BGBladeCharge, NEXT);
  states[bg_blade_charge_state].nextstate = bg_blade_charge_state;
  bg_stab_lock_state = S0(SPR_PUNG, 4, 1, A_BGStabLock, NEXT);
  S0(SPR_PUNG, 4, 1, A_BGStabLock, NEXT);
  S0(SPR_PUNG, 4, 1, A_BGStabLock, NEXT);
  bg_stab_go_state = S0(SPR_PUNG, 5, 1, A_BGStabGo, NEXT);
  {
    int k, last = bg_stab_go_state;

    for (k = 0; k < BG_STAB_TICKS; ++k)
      last = S0(SPR_PUNG, 5, 1, A_BGStabThrust, NEXT);
    bg_stab_recover_state = S0(SPR_PUNG, 5, 6, NULL, NEXT);
    S0(SPR_PUNG, 2, 4, NULL, NEXT);
    S0(SPR_PUNG, 1, 6, NULL, S_PUNCH);
    states[last].nextstate = bg_stab_recover_state;
  }
  bg_rip_wind_state = S0(SPR_PUNG, 6, 1, A_BGRipWind, NEXT);
  states[bg_rip_wind_state].nextstate = bg_rip_wind_state;
  bg_rip_go_state = S0(SPR_PUNG, 5, 1, A_BGRipGo, NEXT);
  {
    int k, last = bg_rip_go_state;

    for (k = 0; k < BG_STAB_TICKS; ++k)
      last = S0(SPR_PUNG, 5, 1, A_BGRipThrust, NEXT);
    states[last].nextstate = bg_stab_recover_state;
  }
  w[wp_fist].atkstate = s;

  // the old mod put "Unknown 1 = 1" on the pistol's raise state, which made the pistol come up instantly
  // (a state's first miscellaneous value sets the weapon sprite's position). Every class raises weapons at
  // its own speed now (BGCLASS swapspeed), so that glitch is switched off.
  states[S_PISTOLUP].misc1 = states[S_PISTOLUP].misc2 = 0;

  // ---- parry-friendly hitscanners: a longer wind-up before the first shot, and the zombieman
  // fires a burst of three with a gap wide enough to parry every shot ----
  states[S_POSS_ATK1].tics += BG_HITSCAN_WINDUP;
  states[S_SPOS_ATK1].tics += BG_HITSCAN_WINDUP;
  states[S_CPOS_ATK1].tics += BG_HITSCAN_WINDUP;
  states[S_SSWV_ATK1].tics += BG_HITSCAN_WINDUP + BG_CHAINGUN_WINDUP;
  bg_Volleys(S_CPOS_ATK1, S_CPOS_ATK4);
  bg_Volleys(S_SSWV_ATK1, S_SSWV_ATK6);

  {
    // one "shot" of the zombieman is whatever the WAD's DEHACKED gives it (states ATK2 + ATK3);
    // clone that twice, with an aiming pause (the gap) in front of each clone
    state_t shot2 = states[S_POSS_ATK2];
    state_t shot3 = states[S_POSS_ATK3];
    int run = shot3.nextstate;
    int f1 = bg_MobjState(SPR_POSS, 4, BG_BURST_GAP, (bg_action_t) A_FaceTarget, NEXT);
    int a1 = bg_CloneState(&shot2, NEXT);
    int a2 = bg_CloneState(&shot3, NEXT);
    int f2 = bg_MobjState(SPR_POSS, 4, BG_BURST_GAP, (bg_action_t) A_FaceTarget, NEXT);
    int b1 = bg_CloneState(&shot2, NEXT);
    int b2 = bg_CloneState(&shot3, run);

    (void) a1; (void) a2; (void) f2; (void) b1; (void) b2;
    states[S_POSS_ATK3].nextstate = f1;
  }

  // snap borrows the fist, tornado the plasma rifle for the ready/up/down frames
  for (s = 0; s < NUMWEAPONS; ++s)
    w[s].intflags |= WIF_ENABLEAPS;
}

static void bg_DefineMobjTypes(void)
{
  mobjinfo_t info;
  static const int pickup_source[6] = {
    MT_CLIP, MT_MISC17, MT_MISC20, MT_MISC21, MT_MISC18, MT_MISC19
  };
  int i;

  info = mobjinfo[MT_PLASMA];
  info.doomednum = -1;
  info.speed = 22 * FRACUNIT;
  info.damage = 1;
  bg_tornado_type = dsda_AppendMobjInfo(&info);

  info = mobjinfo[MT_PLASMA];
  info.doomednum = -1;
  info.speed = 26 * FRACUNIT;
  info.damage = 2;
  info.radius = 6 * FRACUNIT;
  info.height = 6 * FRACUNIT;
  info.seesound = sfx_None; // the weapon plays one sound for the whole fan
  bg_arcbolt_type = dsda_AppendMobjInfo(&info);

  info = mobjinfo[MT_ROCKET];
  info.doomednum = -1;
  info.speed = 45 * FRACUNIT;
  info.damage = 40;
  bg_hellfire_type = dsda_AppendMobjInfo(&info);

  info = mobjinfo[MT_ROCKET];
  info.doomednum = -1;
  info.seesound = sfx_firsht;
  info.deathsound = sfx_firxpl;
  bg_fireball_type = dsda_AppendMobjInfo(&info);

  info = mobjinfo[MT_PLASMA];
  info.doomednum = -1;
  info.seesound = sfx_flamst;
  bg_breath_type = dsda_AppendMobjInfo(&info);

  info = mobjinfo[MT_PUFF];
  info.doomednum = -1;
  info.flags = MF_NOBLOCKMAP | MF_NOGRAVITY;
  info.seesound = sfx_None;
  bg_clap_type = dsda_AppendMobjInfo(&info);

  for (i = 0; i < 6; ++i)
  {
    info = mobjinfo[pickup_source[i]];
    info.doomednum = 20001 + i;
    bg_pickup_type[i] = dsda_AppendMobjInfo(&info);
  }
}

//
// Lookup helpers
//

static int bg_FindWeapon(const char* name)
{
  int i;
  char* end;
  long n;

  for (i = 0; i < NUMWEAPONS; ++i)
    if (!strcasecmp(name, weapon_words[i]))
      return i;

  {
    // the in-game names
    static const struct { const char* word; int weapon; } alias[] = {
      { "knife", wp_fist }, { "rifle", wp_shotgun }, { "scattergun", wp_supershotgun },
      { "machinegun", wp_chaingun }, { "fireball", wp_missile }, { "revolver", wp_dualpistols },
      { "thunderclap", wp_bfg }
    };

    for (i = 0; i < (int) (sizeof alias / sizeof alias[0]); ++i)
      if (!strcasecmp(name, alias[i].word))
        return alias[i].weapon;
  }

  n = strtol(name, &end, 10);
  if (*name && !*end && n >= 0 && n < NUMWEAPONS)
    return (int) n;

  return -1;
}

static int bg_FindAmmo(const char* name)
{
  int i;

  if (!strcasecmp(name, "none"))
    return am_noammo;

  for (i = 0; i < 7; ++i)
    if (!strcasecmp(name, ammo_words[i]))
      return i;

  return -2;
}

int bg_WeaponFromWord(const char* word)
{
  return bg_FindWeapon(word);
}

int bg_AmmoFromWord(const char* word)
{
  int a = bg_FindAmmo(word);

  return a == -2 ? -1 : a;
}

static void bg_ParseLine(char* line, int lumpnum, int lineno)
{
  char* tok[16];
  int count = 0;
  char* p;
  char* hash;

  if ((hash = strchr(line, '#')))
    *hash = '\0';
  if ((hash = strstr(line, "//")))
    *hash = '\0';

  for (p = strtok(line, " \t\r=,"); p && count < 16; p = strtok(NULL, " \t\r=,"))
    tok[count++] = p;

  if (!count)
    return;

  if (!strcasecmp(tok[0], "slot") && count >= 4)
  {
    int slot = atoi(tok[1]);
    int i;

    if (slot >= 1 && slot <= BG_SLOTS)
    {
      for (i = 2; i + 1 < count; i += 2)
      {
        int w = !strcasecmp(tok[i + 1], "none") ? wp_nochange : bg_FindWeapon(tok[i + 1]);

        if (w == -1)
          goto bad;
        if (!strcasecmp(tok[i], "primary"))
          bg_slot_primary[slot] = (weapontype_t) w;
        else if (!strcasecmp(tok[i], "alt"))
          bg_slot_alt[slot] = (weapontype_t) w;
        else
          goto bad;
      }
      return;
    }
  }
  else if (!strcasecmp(tok[0], "pistolshot") && count >= 3)
  {
    int i;

    for (i = 1; i + 1 < count; i += 2)
    {
      int v = atoi(tok[i + 1]);

      if (!strcasecmp(tok[i], "damage")) bg_pistol_damage = v;
      else if (!strcasecmp(tok[i], "mod")) bg_pistol_mod = v > 0 ? v : 1;
      else if (!strcasecmp(tok[i], "spread")) bg_pistol_spread = v;
      else goto bad;
    }
    return;
  }
  else if (!strcasecmp(tok[0], "pierce") && count >= 3)
  {
    int i;

    for (i = 1; i + 1 < count; i += 2)
    {
      int v = atoi(tok[i + 1]);

      if (!strcasecmp(tok[i], "damage")) bg_pierce_damage = v;
      else if (!strcasecmp(tok[i], "mod")) bg_pierce_mod = v > 0 ? v : 1;
      else if (!strcasecmp(tok[i], "hits")) bg_pierce_hits = v > 0 ? v : 1;
      else if (!strcasecmp(tok[i], "falloff")) bg_pierce_falloff = v;
      else if (!strcasecmp(tok[i], "spread")) bg_pierce_spread = v;
      else goto bad;
    }
    return;
  }
  else if (!strcasecmp(tok[0], "ammo") && count >= 2)
  {
    int a = bg_FindAmmo(tok[1]);
    int i;

    if (a >= 0)
    {
      for (i = 2; i + 1 < count; i += 2)
      {
        if (!strcasecmp(tok[i], "max"))
          maxammo[a] = atoi(tok[i + 1]);
        else if (!strcasecmp(tok[i], "clip"))
          clipammo[a] = atoi(tok[i + 1]);
        else
          goto bad;
      }
      return;
    }
  }
  else if (!strcasecmp(tok[0], "mode") && count >= 4)
  {
    int w = bg_FindWeapon(tok[1]);
    int i;

    if (w >= 0)
    {
      for (i = 2; i + 1 < count; i += 2)
      {
        if (!strcasecmp(tok[i], "ammo"))
        {
          int a = bg_FindAmmo(tok[i + 1]);

          if (a == -2)
            goto bad;
          doom_weaponinfo[w].ammo = (ammotype_t) a;
        }
        else if (!strcasecmp(tok[i], "use"))
          doom_weaponinfo[w].ammopershot = atoi(tok[i + 1]);
        else
          goto bad;
      }
      return;
    }
  }

bad:
  lprintf(LO_WARN, "BGWEAPON (lump %d \"%s\", line %d): cannot understand \"%s\"\n",
          lumpnum, lumpnum >= 0 ? lumpinfo[lumpnum].name : "built-in", lineno, tok[0]);
}

static void bg_ParseText(const char* source, int lumpnum)
{
  char* text = Z_Strdup(source);
  char* line;
  char* next;
  int lineno = 0;

  for (line = text; line; line = next)
  {
    next = strchr(line, '\n');
    if (next)
      *next++ = '\0';
    bg_ParseLine(line, lumpnum, ++lineno);
  }

  Z_Free(text);
}

static void bg_RebuildSlotIndex(void);
static signed char bg_weapon_slot[NUMWEAPONS];

static void bg_RebuildSlotIndex(void)
{
  int s;

  memset(bg_weapon_slot, 0, sizeof(bg_weapon_slot));
  for (s = 1; s <= BG_SLOTS; ++s)
  {
    if (bg_slot_primary[s] != wp_nochange)
      bg_weapon_slot[bg_slot_primary[s]] = s;
    if (bg_slot_alt[s] != wp_nochange)
      bg_weapon_slot[bg_slot_alt[s]] = s;
  }
}

void dsda_InitBegotten(void)
{
  int i;

  if (raven)
    return;

  bg_weapons = true;

  bg_DefineMobjTypes();
  bg_DefineStates();
  dsda_InitBgRoster();
  bgvox_Init();
  bg_InitPickups();
  bg_LightsInit();

  for (i = 0; i <= BG_SLOTS; ++i)
    bg_slot_primary[i] = bg_slot_alt[i] = wp_nochange;

  // built-in defaults first, then every BGWEAPON lump in load order
  bg_ParseText(bg_default_text, -1);

  for (i = 0; i < numlumps; ++i)
  {
    const char* data;
    int size;
    char* text;

    if (strncasecmp(lumpinfo[i].name, "BGWEAPON", 8))
      continue;

    size = W_LumpLength(i);
    data = W_LumpByNum(i);
    text = Z_Malloc(size + 1);
    memcpy(text, data, size);
    text[size] = '\0';
    bg_ParseText(text, i);
    Z_Free(text);
  }

  bg_RebuildSlotIndex();

  // the up/down/ready frames of the slot's alternative are those of its primary
  for (i = 1; i <= BG_SLOTS; ++i)
  {
    weapontype_t p = bg_slot_primary[i];
    weapontype_t a = bg_slot_alt[i];

    if (p != wp_nochange && a != wp_nochange)
    {
      doom_weaponinfo[a].upstate = doom_weaponinfo[p].upstate;
      doom_weaponinfo[a].downstate = doom_weaponinfo[p].downstate;
      doom_weaponinfo[a].readystate = doom_weaponinfo[p].readystate;
    }
  }
}

//
// Slot queries
//

int bg_WeaponSlot(weapontype_t weapon)
{
  if (!bg_weapons || weapon < 0 || weapon >= NUMWEAPONS)
    return 0;
  return bg_weapon_slot[weapon];
}

weapontype_t bg_PrimaryOf(weapontype_t weapon)
{
  int slot = bg_WeaponSlot(weapon);

  return slot ? bg_slot_primary[slot] : weapon;
}

weapontype_t bg_SelectedWeapon(const player_t* player)
{
  return player->altactive ? player->primaryweapon : player->readyweapon;
}

weapontype_t bg_SlotSelectable(const player_t* player, int slot)
{
  weapontype_t w;

  if (slot < 1 || slot > BG_SLOTS)
    return wp_nochange;
  w = bg_slot_primary[slot];
  if (w == wp_nochange || !player->weaponowned[w])
    return wp_nochange;
  return w;
}

//
// Fire mode switching
//

void bg_RestorePrimary(player_t* player)
{
  if (player->altactive)
  {
    player->readyweapon = player->primaryweapon;
    player->altactive = 0;
  }
}

static dboolean bg_ModeUsable(const player_t* player, weapontype_t w)
{
  const weaponinfo_t* info = &doom_weaponinfo[w];

  return info->ammo == am_noammo || (player->cheats & CF_INFINITE_AMMO) ||
         player->ammo[info->ammo] >= info->ammopershot;
}

dboolean bg_AltUsable(const player_t* player, weapontype_t primary)
{
  int slot = bg_WeaponSlot(primary);

  if (!slot || bg_slot_alt[slot] == wp_nochange)
    return false;
  return bg_ModeUsable(player, bg_slot_alt[slot]);
}

dboolean bg_BeginFire(player_t* player)
{
  dboolean want = (player->cmd.ex.actions & XC_ALT) != 0;

  if (!bg_weapons)
    return true;

  if (player->altactive)
  {
    if (!want)
      bg_RestorePrimary(player);
    return true;
  }

  if (want)
  {
    weapontype_t primary = player->readyweapon;
    int slot = bg_WeaponSlot(primary);

    // a class that can parry guards instead of using the knife's usual secondary
    if (primary == wp_fist && bg_ClassParryWindow(player) > 0)
    {
      // the button has to be pressed again for every parry
      if (player->bgaltprev)
        return false;
      player->primaryweapon = primary;
      player->readyweapon = wp_spare;
      player->altactive = 1;
      return true;
    }

    if (!slot || bg_slot_primary[slot] != primary || bg_slot_alt[slot] == wp_nochange)
      return true; // no secondary mode: alt fire fires the primary mode

    if (!bg_AltUsable(player, primary))
    {
      if (!player->bgaltprev) // once per press, so the player knows why nothing happened
        dsda_AddPlayerMessage("Not enough ammo for the secondary fire", player);
      return false;
    }

    player->primaryweapon = primary;
    player->readyweapon = bg_slot_alt[slot];
    player->altactive = 1;
  }

  return true;
}

//
// Weapon selection
//

weapontype_t bg_SwitchWeapon(player_t* player)
{
  // best-first: machinegun, rifle, ... knife last
  static const int order[BG_SLOTS] = { 4, 3, 6, 5, 2, 8, 7, 1 };
  weapontype_t current = bg_PrimaryOf(player->readyweapon);
  int i;

  if (player->altactive)
    current = player->primaryweapon;

  for (i = 0; i < BG_SLOTS; ++i)
  {
    int slot = order[i];
    weapontype_t p = bg_SlotSelectable(player, slot);

    if (p == wp_nochange || p == current)
      continue;

    if (bg_ModeUsable(player, p) ||
        (bg_slot_alt[slot] != wp_nochange && bg_ModeUsable(player, bg_slot_alt[slot])))
      return p;
  }

  return current;
}

weapontype_t bg_NextWeapon(const player_t* player, int direction)
{
  weapontype_t cur = player->pendingweapon == wp_nochange ? bg_SelectedWeapon(player)
                                                          : player->pendingweapon;
  int slot = bg_WeaponSlot(cur);
  int i;

  if (!slot)
    slot = 1;

  for (i = 0; i < BG_SLOTS; ++i)
  {
    weapontype_t p;

    slot += direction;
    if (slot > BG_SLOTS) slot = 1;
    if (slot < 1) slot = BG_SLOTS;

    p = bg_SlotSelectable(player, slot);
    if (p != wp_nochange)
      return p;
  }

  return cur;
}

//
// Pickups
//

weapontype_t bg_PickupWeapon(weapontype_t stock)
{
  switch (stock)
  {
    case wp_missile:      { int sl = bg_WeaponSlot(wp_missile); return sl ? bg_slot_primary[sl] : stock; }   // slot 5 (the launcher gives its primary)
    case wp_plasma:       return wp_tornado;   // slot 6
    case wp_bfg:          return wp_snap;      // slot 7
    case wp_supershotgun: return wp_hellfire;  // slot 8
    default:              return stock;
  }
}

const char* bg_PickupMessage(weapontype_t primary)
{
  static char text[96];
  int slot = bg_WeaponSlot(primary);

  if (slot && bg_slot_alt[slot] != wp_nochange)
    snprintf(text, sizeof(text), "Picked up: %s / %s.", bg_WeaponName(primary),
             bg_WeaponName(bg_slot_alt[slot]));
  else
    snprintf(text, sizeof(text), "Picked up: %s.", bg_WeaponName(primary));

  return text;
}

dboolean bg_AmmoPickup(int mobjtype, int* ammo, int* clips, const char** message)
{
  int i;

  if (!bg_weapons)
    return false;

  for (i = 0; i < 6; ++i)
    if (mobjtype == bg_pickup_type[i])
    {
      *ammo = bg_pickups[i].ammo;
      *clips = bg_pickups[i].clips;
      *message = bg_pickups[i].message;
      return true;
    }

  return false;
}

const char* bg_AmmoName(int ammo)
{
  return (ammo >= 0 && ammo < 7) ? ammo_names[ammo] : "";
}

const char* bg_WeaponName(weapontype_t weapon)
{
  return (weapon >= 0 && weapon < NUMWEAPONS) ? weapon_display[weapon] : "";
}

int bg_PickupType(int i)
{
  return (i >= 0 && i < 6) ? bg_pickup_type[i] : -1;
}

int bg_NamedMobjType(const char* name)
{
  if (!strcasecmp(name, "tornado"))
    return bg_tornado_type;
  if (!strcasecmp(name, "arcbolt"))
    return bg_arcbolt_type;
  if (!strcasecmp(name, "hellfire"))
    return bg_hellfire_type;
  return -1;
}

//
// Self tests
//

static int selftest_failures;

#define CHECK(cond, what) \
  do { \
    if (!(cond)) { ++selftest_failures; lprintf(LO_INFO, "FAIL: %s\n", what); } \
    else lprintf(LO_INFO, "ok:   %s\n", what); \
  } while (0)

void dsda_BegottenSelfTest(void)
{
  int s, w;
  player_t p;

  selftest_failures = 0;

  CHECK(bg_weapons, "begotten weapons enabled");
  CHECK(NUMWEAPONS == 16, "16 weapon modes");
  CHECK(DOOM_NUMAMMO == 7, "7 ammo types");

  for (s = 1; s <= BG_SLOTS; ++s)
    CHECK(bg_slot_primary[s] != wp_nochange, "every slot has a primary weapon");

  for (s = 1; s <= BG_SLOTS; ++s)
  {
    CHECK(bg_WeaponSlot(bg_slot_primary[s]) == s, "primary maps back to its slot");
    if (bg_slot_alt[s] != wp_nochange)
    {
      CHECK(bg_WeaponSlot(bg_slot_alt[s]) == s, "secondary maps back to its slot");
      CHECK(bg_PrimaryOf(bg_slot_alt[s]) == bg_slot_primary[s], "secondary's primary");
    }
  }

  for (w = 0; w < NUMWEAPONS; ++w)
  {
    const weaponinfo_t* i = &doom_weaponinfo[w];

    if (!bg_WeaponSlot((weapontype_t) w) && w != wp_spare)
      continue;
    CHECK(i->atkstate > 0 && i->atkstate < num_states, "attack state in range");
    CHECK(i->readystate > 0 && i->readystate < num_states, "ready state in range");
    CHECK(i->upstate > 0 && i->downstate > 0, "up/down states set");
    CHECK(i->ammo == am_noammo || (i->ammo >= 0 && i->ammo < 7), "ammo type valid");
  }

  CHECK(bg_tornado_type > 0 && bg_hellfire_type > 0, "new projectile types exist");
  {
    int ammo, clips;
    const char* msg;
    int i, ok = 0;

    for (i = 0; i < 6; ++i)
      ok += bg_AmmoPickup(bg_pickup_type[i], &ammo, &clips, &msg);
    CHECK(ok == 6, "six new ammo pickups registered");
    CHECK(!bg_AmmoPickup(MT_CLIP, &ammo, &clips, &msg), "stock clip is not a new pickup");
  }

  // weapon selection helpers
  memset(&p, 0, sizeof(p));
  p.pendingweapon = wp_nochange;
  p.weaponowned[wp_fist] = true;
  p.weaponowned[wp_pistol] = true;
  p.ammo[am_clip] = 50;
  p.readyweapon = wp_pistol;
  CHECK(bg_SwitchWeapon(&p) == wp_pistol || bg_SwitchWeapon(&p) == wp_fist,
        "switch only to owned weapons");
  p.weaponowned[wp_shotgun] = true;
  p.ammo[am_shell] = 10;
  CHECK(bg_SwitchWeapon(&p) == wp_shotgun, "switch to rifle when it has ammo");
  CHECK(bg_NextWeapon(&p, 1) == wp_shotgun, "next weapon from slot 2 is slot 3");
  CHECK(bg_NextWeapon(&p, -1) == wp_fist, "previous weapon from slot 2 is slot 1");

  // alt-fire switching
  p.cmd.ex.actions = XC_ALT;
  p.cmd.buttons = BT_ATTACK;
  p.readyweapon = wp_pistol;
  CHECK(bg_BeginFire(&p) && p.readyweapon == wp_dualpistols && p.altactive &&
        p.primaryweapon == wp_pistol, "alt fire enters the revolver mode");
  p.cmd.ex.actions = 0;
  CHECK(bg_BeginFire(&p) && p.readyweapon == wp_pistol && !p.altactive,
        "releasing alt fire returns to the pistol");
  p.ammo[am_clip] = 0;
  p.cmd.ex.actions = XC_ALT;
  CHECK(!bg_BeginFire(&p) && p.readyweapon == wp_pistol && !p.altactive,
        "no ammo: alt fire does nothing");

  selftest_failures += bg_PickupSelfTest();
  lprintf(LO_INFO, "BEGOTTEN SELFTEST (weapons): %s (%d failures)\n",
          selftest_failures ? "FAILED" : "PASSED", selftest_failures);
  dsda_BgClassSelfTest(); // exits by itself on failure
  if (selftest_failures)
    exit(1);
  // the progress test runs after the map info is loaded (d_main.c) and ends the run
}


//
// Live test: real level, real ticks, real attacks.
//

static int bg_render;

static void bg_Tics(int n)
{
  while (n-- > 0)
  {
    P_Ticker();
    if (bg_render)
      R_RenderPlayerView(&players[displayplayer]);
  }
}

static int bg_CountType(int type)
{
  thinker_t* th;
  int n = 0;

  for (th = thinkercap.next; th != &thinkercap; th = th->next)
    if (th->function == P_MobjThinker && ((mobj_t*) th)->type == type)
      ++n;
  return n;
}


// a fingerprint of everything the game state holds that a save must bring back (players, every thing, the random numbers)
static unsigned bg_StateHash(void)
{
  unsigned h = 2166136261u;
  thinker_t* th;
  int i, k;

#define HM(v) do { h ^= (unsigned) (v); h *= 16777619u; } while (0)
  HM(bg_Tickets());
  for (i = 0; i < MAX_MAXPLAYERS; ++i)
  {
    const player_t* q = &players[i];

    if (!playeringame[i])
      continue;
    HM(q->health); HM(q->readyweapon); HM(q->pendingweapon); HM(q->bgclass); HM(q->bgparry);
    HM(q->bgcharge); HM(q->altactive); HM(q->refire);
    for (k = 0; k < NUMAMMO; ++k) HM(q->ammo[k]);
    for (k = 0; k < (int) (sizeof q->armorpoints / sizeof q->armorpoints[0]); ++k) HM(q->armorpoints[k]);
    for (k = 0; k < NUMWEAPONS; ++k) HM(q->weaponowned[k]);
    for (k = 0; k < NUMPOWERS; ++k) HM(q->powers[k]);
    for (k = 0; k < NUMPSPRITES; ++k)
    {
      HM(q->psprites[k].state ? q->psprites[k].state - states : -1);
      HM(q->psprites[k].tics);
    }
    if (q->mo)
    {
      HM(q->mo->x); HM(q->mo->y); HM(q->mo->z); HM(q->mo->angle);
    }
  }
  for (th = thinkercap.next; th != &thinkercap; th = th->next)
  {
    const mobj_t* m;

    if (th->function != P_MobjThinker)
      continue;
    m = (const mobj_t*) th;
    HM(m->type); HM(m->x); HM(m->y); HM(m->z); HM(m->momx); HM(m->momy); HM(m->momz); HM(m->health); HM(m->angle);
    HM(m->state - states); HM(m->tics); HM(m->flags); HM(m->movedir); HM(m->movecount); HM(m->reactiontime);
    HM(m->bg_stun); HM(m->bg_stunimmune); HM(m->bg_dmgscale); HM(m->bg_volley); HM(m->bg_flags); HM(m->bg_drain);
    HM(m->bg_cd); HM(m->bg_aux); HM(m->bg_taken); HM(m->bg_guide); HM(m->bg_rx); HM(m->bg_ry); HM(m->bg_rz);
    HM(m->target ? m->target->type : -1);
  }
  HM(rng.rndindex); HM(rng.prndindex);
  HM(leveltime);
#undef HM
  return h;
}

static void bg_ClearOthers(mobj_t* keep)
{
  thinker_t* th;
  thinker_t* next;

  for (th = thinkercap.next; th != &thinkercap; th = next)
  {
    next = th->next;
    if (th->function == P_MobjThinker)
    {
      mobj_t* m = (mobj_t*) th;
      if (m != keep && !m->player)
        P_RemoveMobj(m);
    }
  }
}

// move the player to the middle of the largest sector so there is room for a target
static void bg_GoToBigRoom(mobj_t* pmo)
{
  int i, li, best = -1;
  long long best_area = 0;

  for (i = 0; i < numsectors; ++i)
  {
    sector_t* sec = &sectors[i];
    fixed_t minx = 0x7fffffff, miny = 0x7fffffff, maxx = -0x7fffffff, maxy = -0x7fffffff;
    long long area;

    for (li = 0; li < sec->linecount; ++li)
    {
      vertex_t* v[2] = { sec->lines[li]->v1, sec->lines[li]->v2 };
      int k;

      for (k = 0; k < 2; ++k)
      {
        if (v[k]->x < minx) minx = v[k]->x;
        if (v[k]->x > maxx) maxx = v[k]->x;
        if (v[k]->y < miny) miny = v[k]->y;
        if (v[k]->y > maxy) maxy = v[k]->y;
      }
    }

    area = (long long) ((maxx - minx) >> FRACBITS) * ((maxy - miny) >> FRACBITS);
    if (area > best_area && sec->ceilingheight - sec->floorheight >= 64 * FRACUNIT &&
        R_PointInSubsector((minx + maxx) / 2, (miny + maxy) / 2)->sector == sec)
    {
      best = i;
      best_area = area;
    }
  }

  if (best >= 0)
  {
    sector_t* sec = &sectors[best];
    fixed_t minx = 0x7fffffff, miny = 0x7fffffff, maxx = -0x7fffffff, maxy = -0x7fffffff;

    for (li = 0; li < sec->linecount; ++li)
    {
      if (sec->lines[li]->v1->x < minx) minx = sec->lines[li]->v1->x;
      if (sec->lines[li]->v1->x > maxx) maxx = sec->lines[li]->v1->x;
      if (sec->lines[li]->v1->y < miny) miny = sec->lines[li]->v1->y;
      if (sec->lines[li]->v1->y > maxy) maxy = sec->lines[li]->v1->y;
    }
    dboolean moved;

    lprintf(LO_INFO, "live test: using sector %d (%d x %d)\n", best,
            (maxx - minx) >> FRACBITS, (maxy - miny) >> FRACBITS);
    // The middle of the sector's bounding box can be inside a pillar (the Gallery of Lights has one there): the move is
    // then refused and the player stays where he is (the player start, in a room big enough for the checks). That is
    // the arrangement the rest of the live test was written against, so it is kept on purpose.
    P_MapStart();
    moved = P_TeleportMove(pmo, (minx + maxx) / 2, (miny + maxy) / 2, false);
    P_MapEnd();
    lprintf(LO_INFO, "live test: %s at %d,%d\n", moved ? "standing in the middle" : "the middle is blocked, staying", pmo->x >> FRACBITS, pmo->y >> FRACBITS);
  }
}

// put a mobj exactly at x,y again and stop it (a test target that knock-back has pushed off its line)
static void bg_PlaceAt(mobj_t* t, fixed_t x, fixed_t y)
{
  P_UnsetThingPosition(t);
  t->x = x;
  t->y = y;
  P_SetThingPosition(t);
  t->momx = t->momy = t->momz = 0;
}

static int bg_target_type = MT_BARREL;

static mobj_t* bg_SpawnTarget(mobj_t* pmo)
{
  int a, d, ok;
  static const int dist[3] = { 130, 90, 70 };

  for (d = 0; d < 3; ++d)
   for (a = 0; a < 16; ++a)
  {
    angle_t angle = (angle_t) a * (ANG45 / 2);
    int an = angle >> ANGLETOFINESHIFT;
    mobj_t* t = P_SpawnMobj(pmo->x + FixedMul(dist[d] * FRACUNIT, finecosine[an]),
                            pmo->y + FixedMul(dist[d] * FRACUNIT, finesine[an]),
                            pmo->z, bg_target_type);

    P_MapStart();
    P_AimLineAttack(pmo, angle, 512 * FRACUNIT, 0);
    ok = linetarget == t && P_CheckPosition(t, t->x, t->y) && t->z >= t->floorz;
    P_MapEnd();
    if (ok)
    {
      pmo->angle = angle;
      if (bg_target_type == MT_BARREL)
      {
        t->health = 100000;
        t->radius = 24 * FRACUNIT;
        t->height = 56 * FRACUNIT;
      }
      return t;
    }
    P_RemoveMobj(t);
  }
  return NULL;
}

static void bg_Ready(player_t* p, weapontype_t w)
{
  int a;

  p->altactive = 0;
  p->readyweapon = w;
  p->pendingweapon = wp_nochange;
  p->refire = 0;
  p->attackdown = 0;
  p->cmd.buttons = 0;
  p->cmd.ex.actions = 0;
  for (a = 0; a < NUMAMMO; ++a)
    p->ammo[a] = p->maxammo[a];
  P_SetPsprite(p, ps_weapon, weaponinfo[w].readystate);
  p->psprites[ps_weapon].sy = 32 * FRACUNIT;
  p->mo->health = p->health = 200;
}

static char w_foe[160];


//
// Live test of the monster roster
//

static mobj_t* bg_RSpawn(mobj_t* pmo, int type, int dist)
{
  int a;

  P_MapEnd();
  for (a = 0; a < 16; ++a)
  {
    angle_t angle = (angle_t) a * (ANG45 / 2);
    int an = angle >> ANGLETOFINESHIFT;
    mobj_t* m = P_SpawnMobj(pmo->x + FixedMul(dist * FRACUNIT, finecosine[an]),
                            pmo->y + FixedMul(dist * FRACUNIT, finesine[an]), ONFLOORZ, type);

    if (m->z >= m->floorz && P_CheckSight(pmo, m))
    {
      m->angle = R_PointToAngle2(m->x, m->y, pmo->x, pmo->y);
      return m;
    }
    P_RemoveMobj(m);
  }
  return NULL;
}

// make it notice the player at once
static void bg_RWake(mobj_t* m, mobj_t* pmo)
{
  P_SetTarget(&m->target, pmo);
  m->movedir = 8; // DI_NODIR
  P_MapEnd();
  P_MapStart();
  P_SetMobjState(m, m->info->seestate);
  P_MapEnd();
}

static int bg_RType(const char* name)
{
  return bg_RosterTypeByName(name);
}

static int bg_CountLive(int type)
{
  thinker_t* th;
  int n = 0;

  for (th = thinkercap.next; th != &thinkercap; th = th->next)
    if (th->function == P_MobjThinker && ((mobj_t*) th)->type == type && ((mobj_t*) th)->health > 0)
      ++n;
  return n;
}

static void bg_RosterLiveTest(player_t* p)
{
  static const char* const names[] = {
    "rockettrooper", "shocktrooper", "werewolf", "fireelemental", "madwizard", "apprentice", "minister",
    "golem", "mindeater", "aegis", "bore", "conjurer", "hatred", "mecha", "beetle", "heart", "core"
  };
  int i, t, k;
  char w[160];
  mobj_t* m;
  mobj_t* m2;
  mobj_t* m3;

  // 0. every state shows a sprite frame that exists in the loaded wad
  {
    int bad = 0, st, first = -1;

    for (st = 1; st <= bg_StateTop(); ++st)
    {
      int spr = states[st].sprite, fr = states[st].frame & 0x7fff;

      if (spr < 0 || spr >= num_sprites || spr == SPR_TNT1)
        continue;
      if (spr == SPR_SHTG || (spr == SPR_HTRD && fr >= 11))
        continue; // stock: the unused shotgun frame and the beta lost soul's death frames
      if (!sprites[spr].spriteframes || fr >= sprites[spr].numframes || sprites[spr].spriteframes[fr].rotate == -1)
      {
        if (first < 0)
          first = st;
        ++bad;
        if (bad <= 12)
          lprintf(LO_INFO, "  missing frame: state %d sprite %s frame %d\n", st, doom_sprnames[spr], fr);
      }
    }
    snprintf(w, sizeof w, "sprites: every state has its frame (%d missing, first state %d)", bad, first);
    CHECK(bad == 0, w);
  }

  bg_ClearOthers(NULL);
  p->cheats |= CF_GODMODE;

  // 1. every roster monster exists, spawns in sight, and survives a short life with the player as its enemy
  for (i = 0; i < (int) (sizeof(names) / sizeof(names[0])); ++i)
  {
    int dead = 0, ok;

    t = bg_RType(names[i]);
    snprintf(w, sizeof w, "roster: %s has a mobj type", names[i]);
    CHECK(t > 0, w);
    if (t <= 0)
      continue;
    m = bg_RSpawn(p->mo, t, 300);
    snprintf(w, sizeof w, "roster: %s spawns in sight", names[i]);
    CHECK(m != NULL, w);
    if (!m)
      continue;
    snprintf(w, sizeof w, "roster: %s has its default health (%d)", names[i], m->health);
    CHECK(m->health == mobjinfo[t].spawnhealth && m->health > 0, w);
    p->cheats &= ~CF_NOTARGET;
    bg_RWake(m, p->mo);
    for (k = 0; k < 120; ++k)
    {
      bg_Tics(1);
      p->mo->health = p->health = 100;
      if (m->health <= 0)
        dead = 1;
    }
    ok = !dead || m->type == t;
    snprintf(w, sizeof w, "roster: %s ran 120 tics (alive: %d)", names[i], !dead);
    CHECK(ok, w);
    bg_ClearOthers(NULL);
  }

  bg_ClearOthers(NULL); bg_GoToBigRoom(p->mo); p->mo->momx = p->mo->momy = p->mo->momz = 0;   // (an earlier section may have left him at a wall)
  // 2. piercing shots go through, losing damage each time
  {
    int hits;
    int pa;
    fixed_t lx, ly, m2x, m2y, m3x, m3y;
    fixed_t slope = 0;
    angle_t ang;

    bg_target_type = MT_FIREELEM;
    m = bg_SpawnTarget(p->mo);
    bg_target_type = MT_BARREL;
    CHECK(m != NULL, "pierce: spawned the first target");
    if (m)
    {
      ang = p->mo->angle;
      pa = ang >> ANGLETOFINESHIFT;
      m2 = P_SpawnMobj(m->x + FixedMul(50 * FRACUNIT, finecosine[pa]), m->y + FixedMul(50 * FRACUNIT, finesine[pa]), m->z, MT_FIREELEM);
      m3 = P_SpawnMobj(m->x + FixedMul(100 * FRACUNIT, finecosine[pa]), m->y + FixedMul(100 * FRACUNIT, finesine[pa]), m->z, MT_FIREELEM);
      m->health = m2->health = m3->health = 1000;
      m->bg_stun = m2->bg_stun = m3->bg_stun = 1000;
      m->flags |= MF_NOGRAVITY; m2->flags |= MF_NOGRAVITY; m3->flags |= MF_NOGRAVITY;
      // A hitscan only meets things whose centre is in a block its line passes through (Doom links a thing into a single
      // block), and the first shot's knock-back pushes the targets sideways: each shot starts with all three back on the line.
      lx = m->x; ly = m->y; m2x = m2->x; m2y = m2->y; m3x = m3->x; m3y = m3->y;
      hits = bg_PierceAttack(p->mo, ang, 2048 * FRACUNIT, slope, 40, 5);
      snprintf(w, sizeof w, "pierce: 3 in a row, all hit (%d), damage falls off (%d %d %d)", hits,
               1000 - m->health, 1000 - m2->health, 1000 - m3->health);
      CHECK(hits == 3 && m->health < m2->health + 1 && 1000 - m->health > 1000 - m2->health && m2->health < m3->health &&
            m3->health < 1000, w);
      m->health = m2->health = m3->health = 1000;
      bg_PlaceAt(m, lx, ly); bg_PlaceAt(m2, m2x, m2y); bg_PlaceAt(m3, m3x, m3y);
      hits = bg_PierceAttack(p->mo, ang, 2048 * FRACUNIT, slope, 40, 2);
      snprintf(w, sizeof w, "pierce: stops after the hit limit (hits %d, health %d %d %d; z %d %d %d, flags %x %x %x, pos %d,%d %d,%d %d,%d)", hits,
               m->health, m2->health, m3->health, m->z >> 16, m2->z >> 16, m3->z >> 16, (unsigned) m->flags, (unsigned) m2->flags,
               (unsigned) m3->flags, m->x >> 16, m->y >> 16, m2->x >> 16, m2->y >> 16, m3->x >> 16, m3->y >> 16);
      CHECK(hits == 2 && m3->health == 1000 && m->health < 1000 && m2->health < 1000, w);
      m->health = m2->health = m3->health = 1000;
      bg_PlaceAt(m, lx, ly); bg_PlaceAt(m2, m2x, m2y); bg_PlaceAt(m3, m3x, m3y);
      hits = bg_PierceAttack(p->mo, ang, 2048 * FRACUNIT, slope, 40, 1);
      CHECK(hits == 1 && m2->health == 1000, "pierce: a limit of 1 is an ordinary shot");
    }
    bg_ClearOthers(NULL);
  }

  bg_ClearOthers(NULL); bg_GoToBigRoom(p->mo); p->mo->momx = p->mo->momy = p->mo->momz = 0;   // (an earlier section may have left him at a wall)
  // 3. the werewolf leaps
  {
    int fast = 0, maxspeed = 0, still = 0, maxstill = 0;
    const state_t* last_state = NULL;

    t = bg_RType("werewolf");
    m = bg_RSpawn(p->mo, t, 400);
    if (m)
    {
      p->cheats &= ~CF_NOTARGET;
      bg_RWake(m, p->mo);
      for (k = 0; k < 150 || (!fast && k < 600); ++k)    // the leap is a random choice: wait for it
      {
        int sp;

        bg_Tics(1);
        p->mo->health = p->health = 100;
        sp = P_AproxDistance(m->momx, m->momy) >> FRACBITS;
        if (sp > maxspeed)
          maxspeed = sp;
        if (sp > 16)
          fast = 1;
        // the longest it stays in one pose: the crouch before the leap (9 tics), and no pause on landing
        if (m->state == last_state && m->z <= m->floorz)
          ++still;
        else
          still = 1;
        last_state = m->state;
        if (still > maxstill)
          maxstill = still;
      }
      snprintf(w, sizeof w, "werewolf: leaps (top speed %d/tic)", maxspeed);
      CHECK(fast, w);
      snprintf(w, sizeof w, "werewolf: crouches 9 tics, no pause on landing (longest pose %d tics)", maxstill);
      CHECK(maxstill <= 9, w);
    }
    else
      CHECK(0, "werewolf: spawned");
    bg_ClearOthers(NULL);
  }

  bg_ClearOthers(NULL); bg_GoToBigRoom(p->mo); p->mo->momx = p->mo->momy = p->mo->momz = 0;   // (an earlier section may have left him at a wall)
  // 4. the golem lobs rocks that arc
  {
    int rock = bg_RType("golem_rock"), seen = 0, grav = 0;

    m = bg_RSpawn(p->mo, bg_RType("golem"), 300);
    if (m && rock > 0)
    {
      p->cheats &= ~CF_NOTARGET;
      bg_RWake(m, p->mo);
      for (k = 0; k < 200 && !seen; ++k)
      {
        bg_Tics(1);
        p->mo->health = p->health = 100;
        if (bg_CountType(rock) > 0)
        {
          thinker_t* th;

          seen = 1;
          for (th = thinkercap.next; th != &thinkercap; th = th->next)
            if (th->function == P_MobjThinker && ((mobj_t*) th)->type == rock)
              grav = !(((mobj_t*) th)->flags & MF_NOGRAVITY);
        }
      }
      CHECK(seen, "golem: throws rocks");
      CHECK(grav, "golem: the rocks are lobbed (they have gravity)");
    }
    else
      CHECK(0, "golem: spawned");
    bg_ClearOthers(NULL);
  }

  bg_ClearOthers(NULL); bg_GoToBigRoom(p->mo); p->mo->momx = p->mo->momy = p->mo->momz = 0;   // (an earlier section may have left him at a wall)
  // 5. summoning: Hatreds and Aegises share the cap of 60
  {
    int hat = bg_RType("hatred"), aeg = bg_RType("aegis"), total;
    int cnj = bg_RType("conjurer");

    p->cheats |= CF_NOTARGET;
    m = bg_RSpawn(p->mo, cnj, 200);
    if (m)
    {
      m->target = p->mo;
      P_SetMobjState(m, mobjinfo[cnj].missilestate);
      total = 0;
      for (k = 0; k < 30; ++k)
      {
        int n;

        bg_Tics(1);
        n = bg_CountLive(hat) + bg_CountLive(aeg);
        if (n > total)
          total = n;
      }
      snprintf(w, sizeof w, "conjurer: summons (%d at most)", total);
      CHECK(total > 0, w);
      for (k = 0; k < 70; ++k)
      {
        mobj_t* h = P_SpawnMobj(p->mo->x, p->mo->y, p->mo->z, hat);

        h->flags |= MF_NOCLIP; // pile of them in one place
        h->bg_stun = 30000;
      }
      m->target = p->mo;
      for (k = 0; k < 5; ++k)
      {
        P_SetMobjState(m, mobjinfo[cnj].missilestate);
        bg_Tics(20);
      }
      total = bg_CountLive(hat) + bg_CountLive(aeg);
      snprintf(w, sizeof w, "conjurer: does not summon above the cap (%d live, 70 were forced in)", total);
      CHECK(total <= 71 + 3, w);
    }
    else
      CHECK(0, "conjurer: spawned");
    bg_ClearOthers(NULL);
    p->cheats &= ~CF_NOTARGET;
  }

  bg_ClearOthers(NULL); bg_GoToBigRoom(p->mo); p->mo->momx = p->mo->momy = p->mo->momz = 0;   // (an earlier section may have left him at a wall)
  // 6. the Aegis raises the dead with doubled health and damage, and is used up; with no corpse it drifts in
  {
    int aeg = bg_RType("aegis");
    int d0, d1, revived = 0, aegis_gone = 0, boosted = 0, hp = 0;
    thinker_t* th;

    p->cheats |= CF_NOTARGET;
    m = bg_RSpawn(p->mo, aeg, 400);
    if (m)
    {
      bg_RWake(m, p->mo);
      d0 = P_AproxDistance(m->x - p->mo->x, m->y - p->mo->y) >> FRACBITS;
      for (k = 0; k < 100; ++k)
      {
        bg_Tics(1);
      }
      d1 = P_AproxDistance(m->x - p->mo->x, m->y - p->mo->y) >> FRACBITS;
      snprintf(w, sizeof w, "aegis: with no corpse it drifts toward the player (%d -> %d)", d0, d1);
      CHECK(d1 < d0 - 60, w);
    }
    bg_ClearOthers(NULL);

    m2 = bg_RSpawn(p->mo, MT_POSSESSED, 250);
    if (m2)
    {
      m2->health = 1;
      P_DamageMobj(m2, p->mo, p->mo, 100);
      bg_Tics(40); // dead and still
      {
        angle_t to = R_PointToAngle2(m2->x, m2->y, p->mo->x, p->mo->y) >> ANGLETOFINESHIFT;

        m = P_SpawnMobj(m2->x + FixedMul(90 * FRACUNIT, finecosine[to]), m2->y + FixedMul(90 * FRACUNIT, finesine[to]), m2->z + 10 * FRACUNIT, aeg);
      }
      bg_RWake(m, p->mo);
      for (k = 0; k < 400 && !revived; ++k)
      {
        bg_Tics(1);
        if (m2->health > 0)
        {
          revived = 1;
          hp = m2->health;
          boosted = (m2->bg_flags & BG_BOOST) != 0;
        }
      }
      for (th = thinkercap.next, aegis_gone = 1; th != &thinkercap; th = th->next)
        if (th->function == P_MobjThinker && ((mobj_t*) th)->type == aeg && ((mobj_t*) th)->health > 0)
          aegis_gone = 0;
      CHECK(revived, "aegis: raises a corpse");
      snprintf(w, sizeof w, "aegis: the revived monster has doubled health (%d)", hp);
      CHECK(revived && hp >= mobjinfo[MT_POSSESSED].spawnhealth * 2 - 1, w);
      CHECK(boosted, "aegis: the revived monster is boosted");
      CHECK(aegis_gone, "aegis: used up when it revives");
    }
    else
      CHECK(0, "aegis: corpse spawned");
    bg_ClearOthers(NULL);
    p->cheats &= ~CF_NOTARGET;
  }

  CHECK(dsda_ActorNameToType("beetle") == bg_RType("beetle") && bg_RType("beetle") > 0, "UMAPINFO bossaction can name the beetle");
  CHECK(dsda_ActorNameToType("heart") == bg_RType("heart") && bg_RType("heart") > 0, "UMAPINFO bossaction can name the heart");

  // 6b. a boosted monster moves twice as far
  {
    int pass, closed[2] = { 0, 0 };

    for (pass = 0; pass < 2; ++pass)
    {
      mobj_t* a = bg_RSpawn(p->mo, MT_WEREWOLF, 400);
      int d0, d1;

      if (!a)
        break;
      if (pass)
        a->bg_flags |= BG_BOOST;
      p->cheats &= ~CF_NOTARGET;
      bg_RWake(a, p->mo);
      d0 = P_AproxDistance(a->x - p->mo->x, a->y - p->mo->y) >> FRACBITS;
      bg_Tics(30);
      d1 = P_AproxDistance(a->x - p->mo->x, a->y - p->mo->y) >> FRACBITS;
      closed[pass] = d0 - d1;
      bg_ClearOthers(NULL);
    }
    snprintf(w, sizeof w, "boost: a boosted demon closes %d units in 30 tics, a normal one %d", closed[1], closed[0]);
    CHECK(closed[0] > 20 && closed[1] > closed[0] * 3 / 2, w);
    p->cheats |= CF_NOTARGET;
  }

  bg_ClearOthers(NULL); bg_GoToBigRoom(p->mo); p->mo->momx = p->mo->momy = p->mo->momz = 0;   // (an earlier section may have left him at a wall)
  // 7. drain: monsters heal on a hit; a parried bolt heals the swordsman at half rate
  {
    int appr = bg_RType("apprentice"), bolt = bg_RType("drain_bolt");
    int cls0 = p->bgclass, hp0, mh0;
    mobj_t* b;

    m = bg_RSpawn(p->mo, appr, 200);
    if (m && bolt > 0)
    {
      b = P_SpawnMobj(m->x, m->y, m->z + 20 * FRACUNIT, bolt);
      b->target = m;
      b->bg_drain = 100;
      m->health = 100;
      p->mo->health = p->health = 100;
      p->cheats &= ~CF_GODMODE;
      P_DamageMobj(p->mo, b, m, 20);
      p->cheats |= CF_GODMODE;
      snprintf(w, sizeof w, "drain: the apprentice heals on a hit (100 -> %d)", m->health);
      CHECK(m->health > 100, w);

      p->bgclass = bg_ClassFromWord("swordsman");
      b->target = p->mo;
      b->bg_dmgscale = 200;
      p->mo->health = p->health = 50;
      m->health = 400;
      mh0 = m->health;
      hp0 = p->health;
      P_DamageMobj(m, b, p->mo, 20); // the doubled damage of a parried bolt
      snprintf(w, sizeof w, "drain: parried bolt: monster %d -> %d, swordsman %d -> %d (half the damage taken)", mh0,
               m->health, hp0, p->health);
      CHECK(m->health < mh0 && p->health > hp0 && (p->health - hp0) * 2 >= (mh0 - m->health) - 2 &&
            (p->health - hp0) * 2 <= (mh0 - m->health) + 2, w);
      p->bgclass = cls0;
    }
    else
      CHECK(0, "drain: apprentice spawned");
    bg_ClearOthers(NULL);
  }

  bg_ClearOthers(NULL); bg_GoToBigRoom(p->mo); p->mo->momx = p->mo->momy = p->mo->momz = 0;   // (an earlier section may have left him at a wall)
  // 8. a Hatred is used up when it hits (explodes) or burns out
  {
    int hat = bg_RType("hatred"), exploded = 0, n0;

    p->cheats &= ~CF_NOTARGET;
    for (k = 0; k < 8; ++k)
    {
      mobj_t* h = bg_RSpawn(p->mo, hat, 150);

      if (h)
        bg_RWake(h, p->mo);
    }
    n0 = bg_CountLive(hat);
    bg_Tics(300);
    p->mo->health = p->health = 100;
    exploded = bg_CountLive(hat) < n0;
    snprintf(w, sizeof w, "hatred: they charge and are used up (%d -> %d)", n0, bg_CountLive(hat));
    CHECK(n0 > 0 && exploded, w);
    bg_ClearOthers(NULL);
  }

  p->cheats |= CF_NOTARGET;
  p->mo->health = p->health = 100;
}

// the test removes things with bg_ClearOthers too, so a pointer it holds may already be gone: only remove live ones
static void bg_RemoveIfLive(mobj_t* mo)
{
  thinker_t* th;

  for (th = thinkercap.next; th != &thinkercap; th = th->next)
    if (th == (thinker_t*) mo)
    {
      P_RemoveMobj(mo);
      return;
    }
}

void dsda_BegottenLiveTest(void)
{
  static int done;
  player_t* p = &players[0];
  mobj_t* target;
  int slot, alt, i, w;
  int failures = 0;

  if (done)
    return;
  done = 1;

  p->cheats |= CF_NOTARGET | CF_GODMODE;
  for (w = 0; w < NUMWEAPONS; ++w)
    p->weaponowned[w] = true;

  bg_GoToBigRoom(p->mo);
  target = bg_SpawnTarget(p->mo);
  CHECK(target != NULL, "spawned a target in sight");
  if (!target)
    exit(1);

  for (slot = 1; slot <= BG_SLOTS; ++slot)
  {
    for (alt = 0; alt <= 1; ++alt)
    {
      weapontype_t primary = bg_slot_primary[slot];
      weapontype_t mode = alt ? bg_slot_alt[slot] : primary;
      weaponinfo_t* info;
      int type, before, hp_before, fired = -1, back = -1;
      char what[96];
      int tornado = bg_NamedMobjType("tornado"), hell = bg_NamedMobjType("hellfire"), arcb = bg_NamedMobjType("arcbolt");
      int saw_projectile = 0;

      if (mode == wp_nochange)
        continue;
      info = &doom_weaponinfo[mode];
      type = info->ammo;

      bg_Ready(p, primary);
      bg_Tics(3);
      bg_Ready(p, primary);
      target->health = 100000;
      target->momx = target->momy = target->momz = 0;
      before = type == am_noammo ? 0 : p->ammo[type];
      hp_before = target->health;

      p->cmd.buttons = BT_ATTACK;
      p->cmd.ex.actions = alt ? XC_ALT : 0;

      for (i = 0; i < 90; ++i)
      {
        bg_Tics(1);
        if (bg_CountType(tornado) || bg_CountType(arcb) || bg_CountType(hell) || bg_CountType(MT_ROCKET) ||
            bg_CountType(MT_PLASMA) || bg_CountType(bg_fireball_type) || bg_CountType(bg_breath_type))
          saw_projectile = 1;
        if (type == am_noammo ? p->psprites[ps_weapon].state->action != (actionf_t) A_WeaponReady
                              : p->ammo[type] < before)
        {
          fired = i;
          break;
        }
      }

      snprintf(what, sizeof what, "%s %s: fires", alt ? "alt" : "primary", bg_WeaponName(mode));
      CHECK(fired >= 0, what);

      if (type != am_noammo && fired >= 0)
      {
        snprintf(what, sizeof what, "%s: first shot costs %d of the right ammo", bg_WeaponName(mode),
                 info->ammopershot);
        CHECK(before - p->ammo[type] == info->ammopershot || mode == wp_chaingun, what);
      }

      snprintf(what, sizeof what, "%s: mode state (alt=%d)", bg_WeaponName(mode), alt);
      CHECK(alt ? (p->altactive && p->readyweapon == mode && p->primaryweapon == primary)
                : (!p->altactive && p->readyweapon == primary), what);

      // let the attack play out, then see what happened
      for (i = 0; i < 60; ++i)
      {
        bg_Tics(1);
        if (bg_CountType(tornado) || bg_CountType(arcb) || bg_CountType(hell) || bg_CountType(MT_ROCKET) ||
            bg_CountType(MT_PLASMA) || bg_CountType(bg_fireball_type) || bg_CountType(bg_breath_type))
          saw_projectile = 1;
      }

      // release the buttons: the weapon must return to the primary mode and the ready state
      p->cmd.buttons = 0;
      p->cmd.ex.actions = 0;
      for (i = 0; i < 200; ++i)
      {
        bg_Tics(1);
        if (!p->altactive && p->readyweapon == primary &&
            p->psprites[ps_weapon].state->action == (actionf_t) A_WeaponReady)
        {
          back = i;
          break;
        }
      }
      snprintf(what, sizeof what, "%s: returns to the ready %s", bg_WeaponName(mode),
               bg_WeaponName(primary));
      CHECK(back >= 0, what);

      // what did the attack do?
      switch (mode)
      {
        case wp_pistol: case wp_dualpistols: case wp_shotgun: case wp_supershotgun:
        case wp_chaingun: case wp_chainfire: case wp_snap: case wp_bfg:
          snprintf(what, sizeof what, "%s: hurt the target", bg_WeaponName(mode));
          CHECK(target->health < hp_before, what);
          break;
        case wp_missile: case wp_plasma: case wp_tornado: case wp_arclightning: case wp_hellfire:
          snprintf(what, sizeof what, "%s: launched a projectile", bg_WeaponName(mode));
          CHECK(saw_projectile, what);
          break;
        default:
          break;
      }

      lprintf(LO_INFO, "  [%s] target health %d -> %d, player health %d, ammo %d -> %d\n", bg_WeaponName(mode), hp_before, target->health, p->health, before, type == am_noammo ? 0 : p->ammo[type]);
      if (mode == wp_bfg)
        CHECK(p->health == 200, "thunderclap does not hurt the player");
    }
  }

  // thunderclap: a blast in front of her, worked out when she claps, each target thrown away along its own bearing
  {
    static const int bearing[4] = { 0, 40, 180, 0 };       // front, corner of the screen, behind, too far
    static const int dist[4] = { 200, 220, 200, 1500 };
    mobj_t* m[4];
    int i, hp[4], cls1 = p->bgclass;
    angle_t pa = p->mo->angle;

    bg_RemoveIfLive(target); bg_ClearOthers(NULL); bg_GoToBigRoom(p->mo); p->mo->momx = p->mo->momy = 0;
    pa = p->mo->angle;
    for (i = 0; i < 4; ++i)
    {
      angle_t a = pa + bearing[i] * ANG1;

      m[i] = P_SpawnMobj(p->mo->x + FixedMul(dist[i] * FRACUNIT, finecosine[a >> ANGLETOFINESHIFT]),
                         p->mo->y + FixedMul(dist[i] * FRACUNIT, finesine[a >> ANGLETOFINESHIFT]), p->mo->z, MT_FIREELEM);
      m[i]->health = 5000; m[i]->bg_stun = 1000;
      m[i]->flags |= MF_NOGRAVITY;
      hp[i] = m[i]->health;
    }
    P_MapStart();
    i = bg_ClapBlast(p);
    P_MapEnd();
    CHECK(i >= 1, "thunderclap: the blast finds targets");
    CHECK(m[0]->health < hp[0], "thunderclap: hits the one in front");
    CHECK(m[1]->health < hp[1], "thunderclap: hits the one at the corner of the screen");
    CHECK(m[2]->health == hp[2], "thunderclap: does not hit the one behind her");
    CHECK(m[3]->health == hp[3], "thunderclap: does not reach past its range");
    {
      angle_t push = R_PointToAngle2(0, 0, m[1]->momx, m[1]->momy), want = pa + 40 * ANG1, err = push - want;

      if (err > ANG180) err = (angle_t) (0 - err);
      CHECK(m[1]->momx != 0 || m[1]->momy != 0, "thunderclap: the one at the corner is thrown");
      CHECK(err < ANG1 * 8, "thunderclap: ... away from her along its own bearing, not along where she faces");
    }
    for (i = 0; i < 4; ++i) hp[i] = m[i]->health;
    p->mo->angle = pa + ANG180;      // she turns round before clapping: the blast follows her
    P_MapStart();
    bg_ClapBlast(p);
    P_MapEnd();
    CHECK(m[2]->health < hp[2], "thunderclap: after turning round it hits the one that was behind her");
    CHECK(m[0]->health == hp[0], "thunderclap: ... and not the one now behind her");
    {
      player_t pl2 = *p;     // recoil: the soldier is thrown back harder than the witch
      int ws, ww;

      p->mo->momx = p->mo->momy = 0; p->bgclass = bg_ClassFromWord("soldier");
      P_Thrust(p, p->mo->angle + ANG180, FixedMul(2300 * 330, 100 * FRACUNIT / 100));
      ws = P_AproxDistance(p->mo->momx, p->mo->momy);
      p->mo->momx = p->mo->momy = 0; p->bgclass = bg_ClassFromWord("witch");
      P_Thrust(p, p->mo->angle + ANG180, FixedMul(2300 * 330, 45 * FRACUNIT / 100));
      ww = P_AproxDistance(p->mo->momx, p->mo->momy);
      CHECK(ww < ws, "thunderclap: the witch is thrown back less than the soldier");
      (void) pl2;
    }
    p->bgclass = cls1; p->mo->momx = p->mo->momy = 0; p->mo->angle = pa;
    for (i = 0; i < 4; ++i) P_RemoveMobj(m[i]);
    bg_ClearOthers(NULL);
    target = bg_SpawnTarget(p->mo);
    if (!target) exit(1);
    target->health = 3000;
  }

  // recoil and view shake: pushed back, the witch more than the soldier; the view turns a hair, alternately left and right
  {
    int cls1 = p->bgclass, rdy1 = p->readyweapon, sol, wit;
    angle_t a0 = p->mo->angle, a1, a2;
    static const weapontype_t kicked[4] = { wp_chainfire, wp_supershotgun, wp_dualpistols, wp_missile };
    int k;

    p->mo->momx = p->mo->momy = 0;
    for (k = 0; k < 4; ++k)
    {
      char w[120];

      p->readyweapon = kicked[k];
      p->bgclass = bg_ClassFromWord("soldier"); p->mo->momx = p->mo->momy = 0; bg_WeaponKick(p);
      sol = P_AproxDistance(p->mo->momx, p->mo->momy);
      p->bgclass = bg_ClassFromWord("witch"); p->mo->momx = p->mo->momy = 0; bg_WeaponKick(p);
      wit = P_AproxDistance(p->mo->momx, p->mo->momy);
      snprintf(w, sizeof w, "kick: %s pushes the player back, the witch (%d) more than the soldier (%d)", bg_WeaponName(kicked[k]), wit, sol);
      CHECK(sol > 0 && wit > sol, w);
    }
    p->readyweapon = wp_chainfire; p->bgclass = bg_ClassFromWord("soldier"); p->mo->angle = a0;
    bg_WeaponKick(p); a1 = p->mo->angle;
    bg_WeaponKick(p); a2 = p->mo->angle;
    CHECK(a1 != a0 && (a1 - a0 < ANG1 || a0 - a1 < ANG1), "kick: the first round turns the view by less than a degree");
    CHECK(a2 == a0, "kick: the second round turns it back, so it does not drift");
    p->readyweapon = wp_pistol; p->mo->momx = p->mo->momy = 0; bg_WeaponKick(p);
    CHECK(p->mo->momx == 0 && p->mo->momy == 0 && p->mo->angle == a0, "kick: the pistol (primary) has none");
    p->bgclass = cls1; p->readyweapon = rdy1; p->mo->angle = a0; p->mo->momx = p->mo->momy = 0;
  }

  // tornado: charges, then sticks to the target and keeps hurting it
  {
    int hp0, steps, stuck_seen = 0;
    bg_RemoveIfLive(target); bg_ClearOthers(NULL); bg_GoToBigRoom(p->mo); p->mo->momx = p->mo->momy = 0; target = bg_SpawnTarget(p->mo);
    CHECK(target != NULL, "respawned target for tornado");
    if (!target) exit(1);
    bg_Ready(p, wp_tornado);
    target->health = 100000; hp0 = target->health;
    p->cmd.buttons = BT_ATTACK;
    for (steps = 0; steps < 12; ++steps) { bg_Tics(1); }
    CHECK(bg_CountType(bg_tornado_type) == 0, "tornado: still charging after 12 tics");
    for (steps = 0; steps < 40; ++steps)
    {
      bg_Tics(1);
      if (bg_CountType(bg_tornado_type)) break;
    }
    CHECK(bg_CountType(bg_tornado_type) > 0, "tornado: projectile fired after the charge");
    p->cmd.buttons = 0;
    for (steps = 0; steps < 60; ++steps)
    {
      thinker_t* th;
      bg_Tics(1);
      for (th = thinkercap.next; th != &thinkercap; th = th->next)
        if (th->function == P_MobjThinker && ((mobj_t*) th)->type == bg_tornado_type &&
            ((mobj_t*) th)->tracer == target)
          stuck_seen = 1;
    }
    CHECK(stuck_seen, "tornado: sticks to the target");
    CHECK(hp0 - target->health >= 4, "tornado: keeps damaging the stuck target");
    { char w2[80]; snprintf(w2, sizeof w2, "tornado: damage stays weak (%d total)", hp0 - target->health); CHECK(hp0 - target->health < 60, w2); }
  }

  // tornado against a real monster (pain states, death while stuck)
  {
    int round, steps;
    bg_render = 1;
    for (round = 0; round < 3; ++round)
    {
      bg_RemoveIfLive(target); bg_ClearOthers(NULL); bg_GoToBigRoom(p->mo); p->mo->momx = p->mo->momy = 0;
      bg_target_type = round == 0 ? MT_FIREELEM : round == 1 ? MT_WEREWOLF : MT_FIREELEM;
      target = bg_SpawnTarget(p->mo);
      bg_target_type = MT_BARREL;
      CHECK(target != NULL, "respawned a monster for tornado");
      if (!target) exit(1);
      bg_Ready(p, wp_tornado);
      if (round == 1) target->health = 1500; // survives the whole stick
      if (round == 2) target->health = 12;   // dies while stuck
      p->cmd.buttons = BT_ATTACK;
      {
        int stuck = 0, hp_start = target->health;
        mobj_t* tg = target;
        for (steps = 0; steps < 400; ++steps)
        {
          thinker_t* th;
          bg_Tics(1);
          if (steps == 70) p->cmd.buttons = 0;
          for (th = thinkercap.next; th != &thinkercap; th = th->next)
            if (th->function == P_MobjThinker && ((mobj_t*) th)->type == bg_tornado_type &&
                ((mobj_t*) th)->tracer == tg)
              ++stuck;
        }
        {
          char w2[100];
          snprintf(w2, sizeof w2, "tornado vs monster %d: no crash (stuck tics %d, hp %d -> %d, player state %d hp %d ammo %d weapon %d)", round, stuck,
                   hp_start, tg->health, p->playerstate, p->health, p->ammo[am_cell], p->readyweapon);
          CHECK(stuck > 0, w2);
        }
      }
    }
    bg_render = 0;
  }

  // stun: a stuck tornado freezes a painable monster; barrels and bosses are immune
  {
    int k, stunned = 0, moved = 0, p0 = 0;
    fixed_t x0, y0;
    char w5[120];
    bg_RemoveIfLive(target); bg_ClearOthers(NULL); bg_GoToBigRoom(p->mo); p->mo->momx = p->mo->momy = 0;
    bg_target_type = MT_FIREELEM;
    target = bg_SpawnTarget(p->mo);
    bg_target_type = MT_BARREL;
    CHECK(target != NULL, "respawned an imp for the stun test");
    if (!target) exit(1);
    target->health = 3000;
    bg_Ready(p, wp_tornado);
    p->cmd.buttons = BT_ATTACK;
    x0 = target->x; y0 = target->y;
    for (k = 0; k < 200; ++k)
    {
      bg_Tics(1);
      if (k == 60) p->cmd.buttons = 0;
      if (target->bg_stun > 0)
      {
        ++stunned;
        // with no momentum left, a stunned thing must not walk anywhere
        if (p0 && !target->momx && !target->momy &&
            P_AproxDistance(target->x - x0, target->y - y0) > 0) ++moved;
        p0 = 1;
      }
      else
        p0 = 0;
      x0 = target->x; y0 = target->y;
    }
    snprintf(w5, sizeof w5, "stun: imp frozen by the tornado for %d tics (%d moved)", stunned, moved);
    CHECK(stunned >= 20 && moved <= 5, w5); // a few tics of leftover sliding are fine
    bg_Tics(100);
    CHECK(target->bg_stun == 0, "stun: it wears off");
    CHECK(!bg_StunMobj(NULL, 10), "stun: NULL is harmless");
    {
      mobj_t* barrel = P_SpawnMobj(p->mo->x + 400 * FRACUNIT, p->mo->y, p->mo->z, MT_BARREL);
      int hit = 0, tries;
      for (tries = 0; tries < 50; ++tries) hit |= bg_StunMobj(barrel, 10);
      CHECK(!hit, "stun: barrels (no painchance) are immune");
      barrel->health = 1000; barrel->flags2 |= MF2_BOSS; barrel->info = &mobjinfo[MT_FIREELEM];
      hit = 0;
      for (tries = 0; tries < 50; ++tries) hit |= bg_StunMobj(barrel, 10);
      CHECK(!hit, "stun: bosses are immune");
      barrel->info = &mobjinfo[MT_BARREL];
      P_RemoveMobj(barrel);
    }
  }

  // tornado: no autoaim tracer on launch (it must not stick to anything it did not hit);
  // arc lightning: a wide fan of small plasma bolts
  {
    int k, n = 0;
    int tracer_set = 0, fan_ok = 0;
    char w6[120];
    bg_RemoveIfLive(target); bg_ClearOthers(NULL); bg_GoToBigRoom(p->mo); p->mo->momx = p->mo->momy = 0;
    bg_target_type = MT_FIREELEM;
    target = bg_SpawnTarget(p->mo); // straight ahead: autoaim has something to lock onto
    bg_target_type = MT_BARREL;
    target->health = 3000;
    bg_Ready(p, wp_tornado);
    p->cmd.buttons = BT_ATTACK;
    for (k = 0; k < 60 && !tracer_set; ++k)
    {
      thinker_t* th;
      bg_Tics(1);
      for (th = thinkercap.next; th != &thinkercap; th = th->next)
        if (th->function == P_MobjThinker && ((mobj_t*) th)->type == bg_tornado_type)
        {
          if (((mobj_t*) th)->tracer) tracer_set = 1;
          n = 1;
        }
      if (n) break;
    }
    p->cmd.buttons = 0;
    CHECK(n && !tracer_set, "tornado: launched without a tracer");

    bg_Tics(80);
    bg_ClearOthers(target);
    bg_Ready(p, wp_tornado);
    p->cmd.buttons = BT_ATTACK;
    p->cmd.ex.actions = XC_ALT;
    for (k = 0; k < 40; ++k)
    {
      thinker_t* th;
      angle_t lo = 0, hi = 0;
      int cnt = 0;
      bg_Tics(1);
      for (th = thinkercap.next; th != &thinkercap; th = th->next)
        if (th->function == P_MobjThinker && ((mobj_t*) th)->type == bg_arcbolt_type)
        {
          angle_t rel = ((mobj_t*) th)->angle - p->mo->angle;
          int deg = (int) ((int) rel / (int) ANG1);
          if (!cnt || deg < (int) lo) lo = (angle_t) deg;
          if (!cnt || deg > (int) hi) hi = (angle_t) deg;
          ++cnt;
        }
      if (cnt)
      {
        snprintf(w6, sizeof w6, "arc lightning: %d bolts fanned over %d degrees", cnt, (int) hi - (int) lo);
        fan_ok = cnt >= 8 && (int) hi - (int) lo >= 36;
        CHECK(fan_ok, w6);
        break;
      }
    }
    CHECK(k < 40, "arc lightning: bolts appeared");
    p->cmd.buttons = 0;
    p->cmd.ex.actions = 0;
    bg_Tics(100);
  }

  // snap: knockback mostly backwards, little upwards
  {
    int hp0, i2;
    bg_RemoveIfLive(target); bg_ClearOthers(NULL); bg_GoToBigRoom(p->mo); p->mo->momx = p->mo->momy = 0; target = bg_SpawnTarget(p->mo);
    CHECK(target != NULL, "respawned target for snap");
    if (!target) exit(1);
    bg_Ready(p, wp_snap);
    target->health = 100000; hp0 = target->health;
    target->momx = target->momy = target->momz = 0;
    p->cmd.buttons = BT_ATTACK;
    for (i2 = 0; i2 < 90 && target->health == hp0; ++i2) bg_Tics(1);
    {
      fixed_t h = P_AproxDistance(target->momx, target->momy);
      lprintf(LO_INFO, "  snap knockback: horizontal %d, vertical %d (fracunits)\n", h >> FRACBITS, target->momz >> FRACBITS); fflush(stdout);
      { char w2[100]; snprintf(w2, sizeof w2, "snap: pushes the target away (h=%d z=%d hp %d)", h >> FRACBITS, target->momz >> FRACBITS, hp0 - target->health); CHECK(h > 4 * FRACUNIT, w2); }
      CHECK(target->momz > 8 * FRACUNIT && h > 4 * FRACUNIT && h < 10 * FRACUNIT, "snap: tossed up and back");
      {
        fixed_t x1 = target->x, y1 = target->y, maxz = 0, dist;
        char w3[100];
        int k;
        p->cmd.buttons = 0;
        for (k = 0; k < 60; ++k)
        {
          bg_Tics(1);
          if (target->z - target->floorz > maxz) maxz = target->z - target->floorz;
        }
        dist = P_AproxDistance(target->x - x1, target->y - y1);
        snprintf(w3, sizeof w3, "snap: arc peaks %d units up (moved %d)", maxz >> FRACBITS, dist >> FRACBITS);
        CHECK(maxz > 50 * FRACUNIT && maxz < 120 * FRACUNIT, w3);
      }
    }
    p->cmd.buttons = 0;
    bg_Tics(60);
  }

  // parry: the swordsman's knife secondary
  {
    int k, window = 0, cls0 = p->bgclass, hp0, i;
    char w9[140];
    mobj_t* m;
    angle_t pa;

    int god0 = p->cheats & CF_GODMODE;
    p->cheats &= ~CF_GODMODE; // so that damage can be seen
    p->bgclass = bg_ClassFromWord("swordsman");
    CHECK(states[S_PISTOLUP].misc1 == 0 && states[S_PISTOLUP].misc2 == 0, "the pistol is not brought up instantly");
    CHECK(bg_PistolRoll(0) == 10 && bg_PistolRoll(2) == 30, "revolver primary: 10 x (1-3)");
    {
      short sp = zl_bg_music_change; int a[5] = {9999, 1, 0, 0, 0};
      bg_TranslateMusicLine(&sp, a);
      CHECK(sp == 0, "music line: an unknown song number disables the line");
      sp = zl_bg_music_stop; a[0] = 1;
      bg_TranslateMusicLine(&sp, a);
      CHECK(sp == zl_music_stop && a[0] == 1, "music line: 213 becomes Music_Stop");
    }
    CHECK(bg_ClassParryWindow(p) == 7, "parry: the swordsman has a 7 tic window (test value)");
    p->bgclass = bg_ClassFromWord("soldier");
    CHECK(bg_ClassParryWindow(p) == 0, "parry: the soldier cannot parry");
    p->bgclass = bg_ClassFromWord("swordsman");

    bg_RemoveIfLive(target); bg_ClearOthers(NULL); bg_GoToBigRoom(p->mo); p->mo->momx = p->mo->momy = 0;
    bg_target_type = MT_FIREELEM;
    target = bg_SpawnTarget(p->mo);
    bg_target_type = MT_BARREL;
    CHECK(target != NULL, "parry: spawned an imp");
    if (!target) exit(1);
    target->health = 3000;
    pa = p->mo->angle;

#define BG_PLACE(dist) do { P_MapStart(); \
      P_TeleportMove(target, p->mo->x + FixedMul((dist) * FRACUNIT, finecosine[pa >> ANGLETOFINESHIFT]), \
                     p->mo->y + FixedMul((dist) * FRACUNIT, finesine[pa >> ANGLETOFINESHIFT]), false); P_MapEnd(); \
      target->bg_stun = 1000; target->momx = target->momy = 0; p->mo->angle = pa; p->mo->momx = p->mo->momy = 0; p->bgparryhit = 0; } while (0)

    // the real input path: alt fire on the knife, with no ammunition at all
    BG_PLACE(300);
    bg_Ready(p, wp_fist);
    bg_Tics(1); // let the previous tic's alt state settle
    for (k = 0; k < NUMAMMO; ++k) p->ammo[k] = 0;
    p->cmd.buttons = BT_ATTACK;
    p->cmd.ex.actions = XC_ALT;
    for (k = 0; k < 60; ++k)
    {
      bg_Tics(1);
      if (k == 0) p->cmd.buttons = 0, p->cmd.ex.actions = 0; // a tap
      if (p->bgparry > 0) ++window;
    }
    snprintf(w9, sizeof w9, "parry: window lasts %d tics", window);
    CHECK(window == bg_ClassParryWindow(p), w9);
    {
      int h = 0;
      /* the guard is not a gun: the ammo panel must not light up for it */
      CHECK(doom_weaponinfo[wp_spare].ammo == am_noammo, "parry: uses no ammunition");
      (void) h;
    }
    CHECK(p->readyweapon == wp_fist && !p->altactive, "parry: back to the knife afterwards");

    // holding the button does not repeat the parry: one window only
    {
      int rises = 0, was = 0;
      bg_Ready(p, wp_fist);
      bg_Tics(1);
      p->cmd.buttons = BT_ATTACK;
      p->cmd.ex.actions = XC_ALT;
      for (k = 0; k < 120; ++k)
      {
        bg_Tics(1);
        if (p->bgparry > 0 && !was) ++rises;
        was = p->bgparry > 0;
      }
      snprintf(w9, sizeof w9, "parry: holding the button gives one parry, not a stream (%d)", rises);
      CHECK(rises == 1, w9);
      p->cmd.buttons = 0; p->cmd.ex.actions = 0;
      bg_Tics(60);
      CHECK(p->readyweapon == wp_fist && !p->altactive, "parry: released, back to the knife");
    }

    // planted stance: no walking while guarding
    {
      fixed_t x0, y0;
      bg_Ready(p, wp_fist);
      bg_Tics(1);
      p->mo->momx = p->mo->momy = 0;
      p->mo->momx = 8 * FRACUNIT; // running when the guard goes up
      p->cmd.buttons = BT_ATTACK;
      p->cmd.ex.actions = XC_ALT;
      bg_Tics(1);
      p->cmd.buttons = 0; p->cmd.ex.actions = 0;
      bg_Tics(1);
      CHECK(P_AproxDistance(p->mo->momx, p->mo->momy) < FRACUNIT, "parry: raising the guard stops a running player");
      CHECK(p->momx == 0 && p->momy == 0, "parry: ... and the walking bob stops with it");
      x0 = p->mo->x; y0 = p->mo->y;
      p->cmd.forwardmove = 50; p->cmd.sidemove = 40;
      bg_Tics(6);
      p->cmd.forwardmove = 0; p->cmd.sidemove = 0;
      snprintf(w9, sizeof w9, "parry: the player is planted while guarding (moved %d)", P_AproxDistance(p->mo->x - x0, p->mo->y - y0) >> 16);
      CHECK(P_AproxDistance(p->mo->x - x0, p->mo->y - y0) < 2 * FRACUNIT, w9);
      bg_Tics(80);
    }

    // a parried blow renews the window and is followed by the next one; the first one shoves back
    {
      fixed_t d0;
      int hpn;
      BG_PLACE(200);
      bg_Ready(p, wp_fist);
      bg_Tics(1);
      p->cmd.buttons = BT_ATTACK;
      p->cmd.ex.actions = XC_ALT;
      bg_Tics(1);
      p->cmd.buttons = 0; p->cmd.ex.actions = 0;
      bg_Tics(3);
      hpn = p->mo->health;
      P_DamageMobj(p->mo, target, target, 10);          // tic 4 of the window: parried
      bg_Tics(5);                                        // past where the first window would have closed
      CHECK(p->bgparry > 0, "parry: a parried blow renews the window");
      P_DamageMobj(p->mo, target, target, 10);          // would be a hit without the renewal
      CHECK(p->mo->health == hpn, "parry: a second blow after the first window's end is parried as well");
      bg_Tics(40);
      CHECK(p->readyweapon == wp_fist && !p->altactive, "parry: after a chain the guard drops without recovery");

      // the shove grows with the blow: a 1 point blow is still felt, a 100 point blow is much harder
      {
        fixed_t small, big;
        BG_PLACE(60);
        bg_Ready(p, wp_spare); p->altactive = 1; p->primaryweapon = wp_fist;
        target->bg_stun = 0; target->bg_stunimmune = 0;
        p->bgparry = 10;
        P_DamageMobj(p->mo, target, target, 1);
        small = P_AproxDistance(p->mo->momx, p->mo->momy);
        p->mo->momx = p->mo->momy = 0; target->bg_stun = 0; target->bg_stunimmune = 0;
        p->bgparry = 10;
        P_DamageMobj(p->mo, target, target, 100);
        big = P_AproxDistance(p->mo->momx, p->mo->momy);
        snprintf(w9, sizeof w9, "parry: shove %d/100 units for a 1 point blow, %d/100 for a 100 point blow", small * 100 >> 16, big * 100 >> 16);
        CHECK(small >= FRACUNIT && small < 2 * FRACUNIT && big > 3 * small, w9);
        p->mo->momx = p->mo->momy = 0;
      }

      // shove: melee parry moves the player away from the attacker
      BG_PLACE(60);
      bg_Ready(p, wp_spare);
      p->altactive = 1; p->primaryweapon = wp_fist;
      p->bgparry = 10; p->bgparryhit = 0;
      target->bg_stun = 0; target->bg_stunimmune = 0;
      d0 = P_AproxDistance(target->x - p->mo->x, target->y - p->mo->y);
      P_DamageMobj(p->mo, target, target, 10);
      bg_Tics(10);
      snprintf(w9, sizeof w9, "parry: the player is shoved back (%d -> %d)", d0 >> 16, P_AproxDistance(target->x - p->mo->x, target->y - p->mo->y) >> 16);
      CHECK(P_AproxDistance(target->x - p->mo->x, target->y - p->mo->y) > d0 + 8 * FRACUNIT, w9);
      p->bgparry = 0;
    }

    // a lost soul's charge is parried too: it stops and is stunned
    {
      mobj_t* skull;
      p->bgparry = 0;
      bg_RemoveIfLive(target); bg_ClearOthers(NULL);
      skull = P_SpawnMobj(p->mo->x + FixedMul(50 * FRACUNIT, finecosine[pa >> ANGLETOFINESHIFT]),
                          p->mo->y + FixedMul(50 * FRACUNIT, finesine[pa >> ANGLETOFINESHIFT]), p->mo->z, MT_HATRED);
      skull->flags |= MF_SKULLFLY;
      skull->momx = -FixedMul(20 * FRACUNIT, finecosine[pa >> ANGLETOFINESHIFT]);
      skull->momy = -FixedMul(20 * FRACUNIT, finesine[pa >> ANGLETOFINESHIFT]);
      p->mo->angle = pa;
      p->bgparry = 10; p->bgparryhit = 0;
      hp0 = p->mo->health;
      P_DamageMobj(p->mo, skull, skull, 6);
      CHECK(p->mo->health == hp0, "parry: the lost soul does no damage");
      CHECK(skull->bg_stun > 20 && !(skull->flags & MF_SKULLFLY), "parry: the lost soul's charge ends and it is stunned");
      P_RemoveMobj(skull);
      bg_target_type = MT_FIREELEM;
      target = bg_SpawnTarget(p->mo);
      bg_target_type = MT_BARREL;
      if (!target) exit(1);
      target->health = 3000;
      p->bgparry = 0;
    }

    // the swordsman's blade: click = slash, held = charged stab, click on a stunned monster = riposte
    {
      int hpt, dmg, base, kk;
      fixed_t sp1, sp2;
      int cls0 = p->bgclass, inf0 = p->cheats & CF_INFINITE_AMMO;

      p->bgclass = bg_ClassFromWord("swordsman");
      p->cheats |= CF_INFINITE_AMMO;

      // an inert target (a barrel with a lot of health): the blade tests need it to stay where it is put
      bg_RemoveIfLive(target); bg_ClearOthers(NULL);
      bg_target_type = MT_BARREL;
      target = bg_SpawnTarget(p->mo);
      if (!target) exit(1);

#define BG_BLADE_PLACE(dist, stun) do { bg_GoToBigRoom(p->mo); BG_PLACE(dist); target->bg_stun = (stun); target->bg_stunimmune = 0; target->health = 3000; \
      bg_Ready(p, wp_fist); bg_Tics(2); p->mo->z = p->mo->floorz; } while (0)
#define BG_BLADE_CLICK(n) do { p->cmd.buttons = BT_ATTACK; bg_Tics(n); p->cmd.buttons = 0; bg_Tics(60); } while (0)

      // a click is a slash: 45 .. 55 damage
      BG_BLADE_PLACE(52, 0);
      hpt = target->health;
      BG_BLADE_CLICK(3);
      dmg = hpt - target->health;
      snprintf(w9, sizeof w9, "blade: a click slashes for 45-55 (%d)", dmg);
      CHECK(dmg >= 45 && dmg <= 55, w9);

      // held to the limit and landing at full speed: a stab for 100
      BG_BLADE_PLACE(80, 0);
      hpt = target->health;
      p->cmd.buttons = BT_ATTACK;
      bg_Tics(9 + 70 + 20);
      p->cmd.buttons = 0;
      bg_Tics(2);
      sp1 = P_AproxDistance(p->mo->momx, p->mo->momy);
      bg_Tics(1);
      bg_Tics(60);
      dmg = hpt - target->health;
      snprintf(w9, sizeof w9, "blade: a full charge landing at full speed stabs for 100 (%d)", dmg);
      CHECK(dmg == 100, w9);

      // the same stab landing late, after the ground slowed her: weaker, but still something
      BG_BLADE_PLACE(160, 0);
      hpt = target->health;
      p->cmd.buttons = BT_ATTACK;
      bg_Tics(9 + 70 + 20);
      p->cmd.buttons = 0;
      bg_Tics(80);
      dmg = hpt - target->health;
      snprintf(w9, sizeof w9, "blade: a stab that lands slowed does less, not nothing (%d)", dmg);
      CHECK(dmg >= 25 && dmg < 90, w9);

      // in the air nothing slows the lunge: the same distance, full damage
      BG_BLADE_PLACE(160, 0);
      hpt = target->health;
      p->cmd.buttons = BT_ATTACK;
      bg_Tics(9 + 70 + 20);
      p->cmd.buttons = 0;
      bg_Tics(1);
      p->mo->z = p->mo->floorz + 90 * FRACUNIT; // jumped just before the stab
      bg_Tics(80);
      dmg = hpt - target->health;
      snprintf(w9, sizeof w9, "blade: a lunge through the air keeps full damage (%d)", dmg);
      CHECK(dmg == 100, w9);

      // the player was held still before the lunge, and the target moved away from him
      BG_BLADE_PLACE(160, 0);
      {
        fixed_t px = p->mo->x, py = p->mo->y, tx = target->x, ty = target->y;
        p->cmd.buttons = BT_ATTACK;
        bg_Tics(9 + 70);
        p->cmd.buttons = 0;
        bg_Tics(1); // released: the lock begins
        p->mo->momx = 5 * FRACUNIT; // trying to run during the lock
        bg_Tics(1);
        snprintf(w9, sizeof w9, "blade: the player is planted before the stab (speed %d/100)", P_AproxDistance(p->mo->momx, p->mo->momy) * 100 >> 16);
        CHECK(P_AproxDistance(p->mo->momx, p->mo->momy) < FRACUNIT, w9);
        bg_Tics(80);
        snprintf(w9, sizeof w9, "blade: the stab propels the player forward (moved %d)", P_AproxDistance(p->mo->x - px, p->mo->y - py) >> 16);
        CHECK(P_AproxDistance(p->mo->x - px, p->mo->y - py) > 60 * FRACUNIT, w9);
        snprintf(w9, sizeof w9, "blade: the target is pushed back (moved %d)", P_AproxDistance(target->x - tx, target->y - ty) >> 16);
        CHECK(P_AproxDistance(target->x - tx, target->y - ty) > 40 * FRACUNIT, w9);
      }
      (void) sp1;

      // a split-second hold stabs for almost nothing: weaker than a slash
      BG_BLADE_PLACE(60, 0);
      hpt = target->health;
      p->cmd.buttons = BT_ATTACK;
      bg_Tics(9 + 1);
      p->cmd.buttons = 0;
      bg_Tics(80);
      dmg = hpt - target->health;
      snprintf(w9, sizeof w9, "blade: a short hold stabs for 1-5 (%d)", dmg);
      CHECK(dmg >= 1 && dmg <= 5, w9);

      // half a charge: about half the damage
      BG_BLADE_PLACE(60, 0);
      hpt = target->health;
      p->cmd.buttons = BT_ATTACK;
      bg_Tics(9 + 35);
      p->cmd.buttons = 0;
      bg_Tics(80);
      dmg = hpt - target->health;
      snprintf(w9, sizeof w9, "blade: half a charge stabs for about half (%d)", dmg);
      CHECK(dmg >= 40 && dmg <= 60, w9);

      // a lunge from the air goes further than one from the ground
      {
        fixed_t x0, y0, d_ground, d_air;
        int air;

        for (air = 0; air < 2; ++air)
        {
          BG_BLADE_PLACE(100, 0);
          P_MapStart(); // the target out of the way, behind the player
          P_TeleportMove(target, p->mo->x - 200 * FRACUNIT, p->mo->y, false);
          P_MapEnd();
          x0 = p->mo->x; y0 = p->mo->y;
          p->cmd.buttons = BT_ATTACK; bg_Tics(9 + 20); p->cmd.buttons = 0;
          bg_Tics(1); // released: locked
          if (air)
          {
            p->mo->z = p->mo->floorz + 60 * FRACUNIT; // jumped just before the stab
            p->mo->momx = 10 * FRACUNIT; p->mo->momy = 0;
          }
          bg_Tics(14);
          if (air) d_air = p->mo->x - x0; else d_ground = p->mo->x - x0;
          bg_Tics(80);
        }
        snprintf(w9, sizeof w9, "blade: a stab keeps the momentum of a jump (ground %d, air %d)", d_ground >> 16, d_air >> 16);
        CHECK(d_ground > 40 * FRACUNIT && d_air > d_ground + 20 * FRACUNIT, w9);
      }

      // stunned monsters take more damage from a player
      BG_BLADE_PLACE(60, 0);
      hpt = target->health;
      P_DamageMobj(target, p->mo, p->mo, 10);
      base = hpt - target->health;
      target->bg_stun = 1000;
      hpt = target->health;
      P_DamageMobj(target, p->mo, p->mo, 10);
      dmg = hpt - target->health;
      snprintf(w9, sizeof w9, "stun: 10 damage becomes %d on a stunned monster (normal %d)", dmg, base);
      CHECK(base == 10 && dmg == 15, w9);
      CHECK(target->bg_stun > 0, "stun: damage does not end the stun");
      hpt = target->health;
      P_DamageMobj(target, target, target, 10);
      CHECK(hpt - target->health == 10, "stun: only players get the bonus");

      // riposte: a click on a stunned monster
      BG_BLADE_PLACE(110, 1000);
      hpt = target->health;
      p->cmd.buttons = BT_ATTACK;
      bg_Tics(3);
      p->cmd.buttons = 0;
      bg_Tics(100);
      dmg = hpt - target->health;
      snprintf(w9, sizeof w9, "riposte: a click on a stunned monster hits for 300 (200 x 1.5) (%d)", dmg);
      CHECK(dmg == 300, w9);
      CHECK(target->bg_stun == 0, "riposte: the stun is spent");

      // ... and a click on one that is not stunned is a plain slash
      BG_BLADE_PLACE(52, 0);
      hpt = target->health;
      BG_BLADE_CLICK(3);
      dmg = hpt - target->health;
      CHECK(dmg >= 45 && dmg <= 55, "riposte: only for stunned monsters");

      // other classes keep their knife
      p->bgclass = bg_ClassFromWord("soldier");
      BG_BLADE_PLACE(52, 0);
      hpt = target->health;
      BG_BLADE_CLICK(3);
      dmg = hpt - target->health;
      snprintf(w9, sizeof w9, "blade: the soldier's knife is the stock punch (%d)", dmg);
      CHECK(dmg >= 2 && dmg <= 20, w9);

      // air control: a little for the soldier, more for the witch, a lot for the swordsman
      {
        fixed_t spd[3];
        const char* names[3] = { "soldier", "witch", "swordsman" };

        for (kk = 0; kk < 3; ++kk)
        {
          p->bgclass = bg_ClassFromWord(names[kk]);
          BG_BLADE_PLACE(300, 0);
          p->mo->momx = p->mo->momy = p->mo->momz = 0;
          p->mo->z = p->mo->floorz + 90 * FRACUNIT;
          p->cmd.forwardmove = 50;
          bg_Tics(8);
          p->cmd.forwardmove = 0;
          CHECK(p->mo->z > p->mo->floorz, "air: still airborne");
          spd[kk] = P_AproxDistance(p->mo->momx, p->mo->momy);
        }
        snprintf(w9, sizeof w9, "air control: speeds after 8 tics %d / %d / %d (hundredths)", spd[0] * 100 >> 16, spd[1] * 100 >> 16, spd[2] * 100 >> 16);
        CHECK(spd[0] > FRACUNIT && spd[0] < spd[1] && spd[1] < spd[2] && spd[2] > 6 * FRACUNIT, w9);

        // steering keeps a speed above the running limit but cannot add to it
        p->bgclass = bg_ClassFromWord("swordsman");
        BG_BLADE_PLACE(300, 0);
        p->mo->z = p->mo->floorz + 90 * FRACUNIT; p->mo->momz = 0;
        p->mo->momx = 25 * FRACUNIT; p->mo->momy = 0;
        p->cmd.sidemove = 50; p->cmd.forwardmove = 50;
        bg_Tics(6);
        p->cmd.sidemove = 0; p->cmd.forwardmove = 0;
        sp2 = P_AproxDistance(p->mo->momx, p->mo->momy);
        snprintf(w9, sizeof w9, "air control: a lunge's speed is kept but not exceeded (%d/100)", sp2 * 100 >> 16);
        CHECK(sp2 <= 26 * FRACUNIT && sp2 > 20 * FRACUNIT, w9);
      }

      p->cmd.forwardmove = p->cmd.sidemove = 0;
      p->bgclass = cls0;
      p->cheats = (p->cheats & ~CF_INFINITE_AMMO) | inf0;
      bg_Tics(100);
#undef BG_BLADE_PLACE
#undef BG_BLADE_CLICK
      bg_RemoveIfLive(target); bg_ClearOthers(NULL); bg_GoToBigRoom(p->mo);
      p->mo->momx = p->mo->momy = 0;
      bg_target_type = MT_FIREELEM;
      target = bg_SpawnTarget(p->mo);
      bg_target_type = MT_BARREL;
      if (!target) exit(1);
      target->health = 3000;
    }

    {
      int had = p->weaponowned[wp_shotgun];
      p->weaponowned[wp_shotgun] = false;
      CHECK(!P_GiveWeapon(p, wp_shotgun, false) && !p->weaponowned[wp_shotgun], "swordsman: cannot pick up guns");
      p->weaponowned[wp_shotgun] = had;
    }

    // outside the window a blow lands
    BG_PLACE(60);
    bg_Ready(p, wp_fist);
    p->bgparry = 0;
    hp0 = p->mo->health;
    P_DamageMobj(p->mo, target, target, 10);
    CHECK(p->mo->health < hp0, "parry: no window, the blow hurts");

    // melee inside the window: no damage, attacker stunned and pushed
    bg_Ready(p, wp_spare);
    p->altactive = 1; p->primaryweapon = wp_fist;
    BG_PLACE(60);
    target->bg_stun = 0; target->bg_stunimmune = 0;
    p->bgparry = 10;
    hp0 = p->mo->health;
    P_DamageMobj(p->mo, target, target, 10);
    CHECK(p->mo->health == hp0, "parry: melee blow inside the window does no damage");
    CHECK(target->bg_stun > 20, "parry: the melee attacker is stunned");
    CHECK(p->bgparry == bg_ClassParryWindow(p), "parry: success renews the window");

    // bullets from afar are fired back where the player aims, with twice the damage
    BG_PLACE(200);
    target->bg_stun = 1000;
    p->bgparry = 10;
    hp0 = p->mo->health;
    i = target->health;
    P_DamageMobj(p->mo, target, target, 10);
    CHECK(p->mo->health == hp0, "parry: the bullet does not hurt the player");
    bg_Tics(2);
    snprintf(w9, sizeof w9, "parry: the bullet came back as a 20 damage shot (30 on the stunned test dummy) at what the player aims at (%d, dist %d, bgdeflect %d)", i - target->health, P_AproxDistance(target->x - p->mo->x, target->y - p->mo->y) >> 16, p->bgdeflect);
    CHECK(i - target->health == bg_StunDamage(target, p->mo, 20), w9); // the dummy is stunned: players hit it harder

    // not facing the attacker: no parry
    BG_PLACE(60);
    p->mo->angle = pa + ANG180;
    p->bgparry = 10;
    hp0 = p->mo->health;
    P_DamageMobj(p->mo, target, target, 10);
    CHECK(p->mo->health < hp0, "parry: only works against what the player faces");
    p->mo->angle = pa;

    // projectiles: they go where the player aims, twice as fast and twice as hard.
    // imp 1 (the shooter) is straight ahead, imp 2 is 30 degrees to the side and the
    // player turns to face it.
    {
      mobj_t* imp2 = P_SpawnMobj(p->mo->x, p->mo->y, p->mo->z, MT_FIREELEM);
      angle_t a2 = pa + 30 * ANG1;
      int h2, h1, speed_ok;

      BG_PLACE(150);
      target->flags |= MF_NOGRAVITY;
      target->z = p->mo->z;
      P_MapStart();
      P_TeleportMove(imp2, p->mo->x + FixedMul(150 * FRACUNIT, finecosine[a2 >> ANGLETOFINESHIFT]),
                     p->mo->y + FixedMul(150 * FRACUNIT, finesine[a2 >> ANGLETOFINESHIFT]), false);
      P_MapEnd();
      imp2->flags |= MF_NOGRAVITY;
      imp2->z = p->mo->z;
      imp2->health = 3000;
      imp2->bg_stun = 1000;
      p->mo->angle = a2; // aim at imp 2
      p->bgparry = 10;
      m = P_SpawnMobj(p->mo->x + FixedMul(40 * FRACUNIT, finecosine[pa >> ANGLETOFINESHIFT]),
                      p->mo->y + FixedMul(40 * FRACUNIT, finesine[pa >> ANGLETOFINESHIFT]),
                      p->mo->z + 32 * FRACUNIT, MT_TROOPSHOT);
      P_SetTarget(&m->target, target);
      m->angle = pa + ANG180;
      m->momx = -FixedMul(10 * FRACUNIT, finecosine[pa >> ANGLETOFINESHIFT]);
      m->momy = -FixedMul(10 * FRACUNIT, finesine[pa >> ANGLETOFINESHIFT]);
      m->momz = 0;
      hp0 = p->mo->health;
      h1 = target->health;
      h2 = imp2->health;
      speed_ok = 0;
      for (k = 0; k < 40 && imp2->health == h2; ++k)
      {
        p->bgparry = k < 3 ? 10 : 0;
        bg_Tics(1);
        if (m->target == p->mo && P_AproxDistance(m->momx, m->momy) >= 18 * FRACUNIT) speed_ok = 1;
      }
      CHECK(p->mo->health == hp0, "parry: the fireball does not hurt the player");
      CHECK(imp2->health < h2, "parry: the fireball goes where the player aims (imp 2)");
      CHECK(target->health == h1, "parry: ... and not back to the shooter");
      CHECK(h2 - imp2->health >= 6, "parry: the deflected fireball hits twice as hard");
      CHECK(speed_ok, "parry: the deflected fireball is twice as fast");
      P_RemoveMobj(imp2);

      // nothing under the crosshair: straight ahead, never back at the shooter
      p->mo->angle = pa + ANG180;
      p->bgparry = 10;
      m = P_SpawnMobj(p->mo->x + FixedMul(40 * FRACUNIT, finecosine[pa >> ANGLETOFINESHIFT]),
                      p->mo->y + FixedMul(40 * FRACUNIT, finesine[pa >> ANGLETOFINESHIFT]),
                      p->mo->z + 32 * FRACUNIT, MT_TROOPSHOT);
      P_SetTarget(&m->target, target);
      m->momx = -FixedMul(10 * FRACUNIT, finecosine[pa >> ANGLETOFINESHIFT]);
      m->momy = -FixedMul(10 * FRACUNIT, finesine[pa >> ANGLETOFINESHIFT]);
      m->momz = 0;
      p->mo->angle = pa;
      bg_Tics(1);
      /* player faces the shooter again, so it IS the target under the crosshair: it comes back */
      p->bgparry = 0;
      p->mo->angle = pa;
    }
    target->flags &= ~MF_NOGRAVITY;

    // bosses: projectiles are reflected, melee is blocked, but they are never stunned
    target->flags2 |= MF2_BOSS;
    BG_PLACE(60);
    target->bg_stun = 0; target->bg_stunimmune = 0;
    p->bgparry = 10;
    hp0 = p->mo->health;
    P_DamageMobj(p->mo, target, target, 10);
    CHECK(p->mo->health == hp0, "parry: a boss's melee blow is blocked too");
    CHECK(target->bg_stun == 0, "parry: bosses are not stunned");
    target->flags2 &= ~MF2_BOSS;

    p->bgparry = 0;
    p->cheats |= god0;
    p->bgclass = cls0;
    bg_ClearOthers(target);
    target->bg_stun = 0;
#undef BG_PLACE
  }

  // a revenant's homing missile, parried, seeks the monster under the crosshair, else the nearest
  // other monster in sight, else (nothing else around) the one that fired it
  {
    int round;
    int cls1 = p->bgclass, god1 = p->cheats & CF_GODMODE;
    angle_t pa2;
    mobj_t* imp1;
    mobj_t* imp2;
    mobj_t* mis;
    char wh[140];

    p->bgclass = bg_ClassFromWord("swordsman");
    bg_RemoveIfLive(target); bg_ClearOthers(NULL); bg_GoToBigRoom(p->mo); p->mo->momx = p->mo->momy = 0;
    pa2 = p->mo->angle;
    for (round = 0; round < 2; ++round)
    {
      angle_t a1 = pa2 + 80 * ANG1, a2 = pa2 - 60 * ANG1;

      bg_ClearOthers(NULL);
      imp1 = P_SpawnMobj(p->mo->x + FixedMul(200 * FRACUNIT, finecosine[a1 >> ANGLETOFINESHIFT]),
                         p->mo->y + FixedMul(200 * FRACUNIT, finesine[a1 >> ANGLETOFINESHIFT]), p->mo->z, MT_FIREELEM);
      imp1->flags |= MF_NOGRAVITY; imp1->bg_stun = 1000;
      imp2 = NULL;
      if (round == 0)
      {
        imp2 = P_SpawnMobj(p->mo->x + FixedMul(150 * FRACUNIT, finecosine[a2 >> ANGLETOFINESHIFT]),
                           p->mo->y + FixedMul(150 * FRACUNIT, finesine[a2 >> ANGLETOFINESHIFT]), p->mo->z, MT_FIREELEM);
        imp2->flags |= MF_NOGRAVITY; imp2->bg_stun = 1000;
      }
      p->mo->angle = pa2;
      p->bgparry = 10; p->bgparryhit = 0;
      mis = P_SpawnMobj(p->mo->x + FixedMul(40 * FRACUNIT, finecosine[pa2 >> ANGLETOFINESHIFT]),
                        p->mo->y + FixedMul(40 * FRACUNIT, finesine[pa2 >> ANGLETOFINESHIFT]),
                        p->mo->z + 32 * FRACUNIT, MT_TRACER);
      P_SetTarget(&mis->target, imp1);
      P_SetTarget(&mis->tracer, imp1);
      mis->momx = -FixedMul(10 * FRACUNIT, finecosine[pa2 >> ANGLETOFINESHIFT]);
      mis->momy = -FixedMul(10 * FRACUNIT, finesine[pa2 >> ANGLETOFINESHIFT]);
      CHECK(bg_ParryMissile(mis, p->mo), "parry: the revenant's missile is parried");
      snprintf(wh, sizeof wh, round == 0 ? "parry: a homing missile seeks the other monster" : "parry: with no other monster it returns to the shooter");
      CHECK(mis->tracer == (round == 0 ? imp2 : imp1), wh);
    }
    bg_ClearOthers(NULL);
    p->bgparry = 0;
    p->bgclass = cls1;
    p->cheats |= god1;
    bg_target_type = MT_FIREELEM;
    target = bg_SpawnTarget(p->mo);
    bg_target_type = MT_BARREL;
    if (!target) exit(1);
    target->health = 3000;
  }

  // a fast missile (the fireball, speed 20) arriving from the side is parried toward what the player aims at
  {
    int side, k, hit;
    int cls1 = p->bgclass, god1 = p->cheats & CF_GODMODE;
    angle_t pa3;
    mobj_t* shooter;
    mobj_t* aimed;
    mobj_t* fb;
    char wf[160];

    p->bgclass = bg_ClassFromWord("swordsman");
    p->cheats |= CF_GODMODE;
    for (side = 0; side < 4; ++side)
    {
      static const int bearing[4] = { 50, -50, 50, -40 };   // where the shooter stands, relative to where she looks
      static const int ahead[4] = { 0, 8, 70, 70 };        // where the monster she aims at stands (70: none near the crosshair)
      angle_t as, aa;
      int hp_before, redirected = 0;
      fixed_t x0 = 0, y0 = 0, cross = 0;
      angle_t err = 0, flick = 0;
      int kred = -1, flick_done = 0, flick_err = 0, flick_cross = 0;

      bg_RemoveIfLive(target); bg_ClearOthers(NULL); bg_GoToBigRoom(p->mo); p->mo->momx = p->mo->momy = 0;
      pa3 = p->mo->angle;
      as = pa3 + bearing[side] * ANG1;
      aa = pa3 + ahead[side] * ANG1;
      shooter = P_SpawnMobj(p->mo->x + FixedMul(320 * FRACUNIT, finecosine[as >> ANGLETOFINESHIFT]),
                            p->mo->y + FixedMul(320 * FRACUNIT, finesine[as >> ANGLETOFINESHIFT]), p->mo->z, MT_FIREELEM);
      shooter->flags |= MF_NOGRAVITY; shooter->health = 5000; shooter->bg_stun = 1000;
      aimed = P_SpawnMobj(p->mo->x + FixedMul(260 * FRACUNIT, finecosine[aa >> ANGLETOFINESHIFT]),
                          p->mo->y + FixedMul(260 * FRACUNIT, finesine[aa >> ANGLETOFINESHIFT]), p->mo->z, MT_FIREELEM);
      aimed->flags |= MF_NOGRAVITY; aimed->health = 5000; aimed->bg_stun = 1000;
      target = shooter;
      p->mo->angle = pa3;
      p->bgparry = 10;
      P_MapStart();
      fb = P_SpawnMissile(shooter, p->mo, bg_fireball_type);
      P_MapEnd();
      hit = 0;
      hp_before = aimed->health;
      x0 = p->mo->x; y0 = p->mo->y;
      for (k = 0; k < 60 && !hit; ++k)
      {
        fixed_t px = p->mo->x, py = p->mo->y;

        if (fb && fb->health > 0 && fb->flags & MF_MISSILE && fb->target != p->mo)
          p->bgparry = 10;
        bg_Tics(1);
        if (!redirected && fb && fb->target == p->mo && fb->health > 0)
        {
          // the tic it was parried: how far off the crosshair line it flies, and which way
          fixed_t dx = fb->x - px, dy = fb->y - py;
          fixed_t cs = finecosine[pa3 >> ANGLETOFINESHIFT], sn = finesine[pa3 >> ANGLETOFINESHIFT];

          redirected = 1;
          x0 = px; y0 = py;
          cross = FixedMul(dx, sn) - FixedMul(dy, cs);
          err = R_PointToAngle2(0, 0, fb->momx, fb->momy) - pa3;
          if (err > ANG180) err = (angle_t) (0 - err);
        }
        if (redirected && ahead[side] >= 20)
        {
          // she flicks the mouse right after the parry: the ball follows the crosshair, not the old line
          if (kred < 0) kred = k;
          if (k == kred + 2) { p->mo->angle += 10 * ANG1; flick = p->mo->angle; }
          if (k == kred + 6 && fb && fb->health > 0 && (fb->flags & MF_MISSILE))
          {
            fixed_t dx = fb->x - p->mo->x, dy = fb->y - p->mo->y;
            fixed_t cs = finecosine[flick >> ANGLETOFINESHIFT], sn = finesine[flick >> ANGLETOFINESHIFT];
            angle_t fe = R_PointToAngle2(0, 0, fb->momx, fb->momy) - flick;

            if (fe > ANG180) fe = (angle_t) (0 - fe);
            flick_cross = abs((FixedMul(dx, sn) - FixedMul(dy, cs)) >> 16);
            flick_err = (int) (fe / (ANG1 / 10));   // tenths of a degree
            flick_done = 1;
          }
        }
        if (aimed->health < hp_before) hit = 1;
      }
      if (ahead[side] >= 20)
      {
        snprintf(wf, sizeof wf, "parry: a flick of the mouse right after is followed (%d.%d degrees off, %d units to the side)", flick_err / 10, flick_err % 10, flick_cross);
        CHECK(flick_done && flick_err <= 10 && flick_cross <= 8, wf);
      }
      {
        // the shove must keep her on the line she aimed along, or the crosshair slides off where the ball went
        fixed_t ddx = p->mo->x - x0, ddy = p->mo->y - y0;
        fixed_t side_shift = FixedMul(ddx, finesine[pa3 >> ANGLETOFINESHIFT]) - FixedMul(ddy, finecosine[pa3 >> ANGLETOFINESHIFT]);
        fixed_t back = -(FixedMul(ddx, finecosine[pa3 >> ANGLETOFINESHIFT]) + FixedMul(ddy, finesine[pa3 >> ANGLETOFINESHIFT]));

        snprintf(wf, sizeof wf, "parry: the shove from %d degrees goes straight back along the aim line (%d to the side, %d back)", bearing[side], side_shift >> 16, back >> 16);
        CHECK(abs(side_shift >> 16) <= 1 && (back >> 16) > 3, wf);
      }
      if (ahead[side] < 20)
      {
        snprintf(wf, sizeof wf, "parry: a fireball from %d degrees goes to the monster she aims at (%d off), tics %d", bearing[side], ahead[side], k);
        CHECK(hit, wf);
      }
      else
      {
        snprintf(wf, sizeof wf, "parry: with nothing near the crosshair the fireball from %d degrees flies exactly along it (%.2f degrees off, %d units to the side)",
                 bearing[side], (double) (int) err / (double) ANG1, cross >> 16);
        CHECK(redirected && err < ANG1 / 4 && abs(cross >> 16) <= 2, wf);
      }
      p->mo->angle = pa3;
      CHECK(shooter->health == 5000, "parry: ... and not back to the shooter");
      CHECK(p->mo->health == 200 || (p->cheats & CF_GODMODE), "parry: ... and the player is not hurt");
    }
    p->bgparry = 0;
    p->bgclass = cls1;
    if (!god1) p->cheats &= ~CF_GODMODE;
    bg_ClearOthers(NULL);
    bg_target_type = MT_BARREL;
    target = bg_SpawnTarget(p->mo);
    if (!target) exit(1);
    target->health = 3000;
  }

  // lights: every shape lights what it should and nothing else
  {
    int lamp = bg_LightTypeFor(20102), spot = bg_LightTypeFor(20103), strip = bg_LightTypeFor(20104);
    double up[3] = { 0, 0, 1 }, away[3] = { 0, 1, 0 }, toward[3] = { 0, -1, 0 };
    double ceil_z, v1, v2, v3, vc, ve, vb;
    mobj_t* m;
    int cfg = dsda_IntConfig(dsda_config_begotten_lantern);
    int light_ok = lamp >= 0 && spot >= 0 && strip >= 0;

    CHECK(light_ok, "lights: the lamp, spot and strip things exist");
    if (light_ok)
    {
      bg_RemoveIfLive(target); bg_ClearOthers(NULL); bg_GoToBigRoom(p->mo); p->mo->momx = p->mo->momy = 0;
      dsda_UpdateIntConfig(dsda_config_begotten_lantern, 0, false);
      ceil_z = (double) p->mo->subsector->sector->ceilingheight / 65536.0;
      bg_render = 1;

      // a lamp at (256, 376), 32 up, radius 480
      m = P_SpawnMobj(256 * FRACUNIT, 376 * FRACUNIT, ONFLOORZ, lamp);
      bg_Tics(1);
      CHECK(bg_LightCount() == 1, "lights: the lamp is in the frame's light list");
      v1 = bg_LightProbe(256, 386, 32, NULL);
      v2 = bg_LightProbe(256, 676, 32, NULL);
      v3 = bg_LightProbe(256, 880, 32, NULL);
      {
        char w[120];
        snprintf(w, sizeof w, "lights: a lamp is bright close (%.2f), softer at 300 units (%.2f), nothing past its radius (%.2f)", v1, v2, v3);
        CHECK(v1 > 0.8 && v2 > 0.2 && v2 < 0.5 && v3 == 0, w);
      }
      CHECK(bg_LightProbe(256, 476, 32, away) == 0 && bg_LightProbe(256, 476, 32, toward) > 0.3,
            "lights: a surface facing away from the lamp stays dark, one facing it is lit");
      P_RemoveMobj(m);

      // a spot hanging from the ceiling, pointing down: a cone, not a ball
      m = P_SpawnMobj(256 * FRACUNIT, 256 * FRACUNIT, ONFLOORZ, spot);
      bg_Tics(1);
      vc = bg_LightProbe(256, 256, 0, up);
      ve = bg_LightProbe(256 + 400, 256, 0, up);
      {
        char w[120];
        snprintf(w, sizeof w, "lights: a ceiling spot lights the floor under it (%.2f) and not far to the side (%.2f)", vc, ve);
        CHECK(vc > 0.3 && ve == 0, w);
      }
      P_RemoveMobj(m);

      // a strip lamp 128 long along the x axis: the floor under its ends is lit as much as under its middle
      m = P_SpawnMobj(256 * FRACUNIT, 256 * FRACUNIT, ONFLOORZ, strip);
      m->angle = 0;
      bg_Tics(1);
      vc = bg_LightProbe(256, 256, 0, up);
      ve = bg_LightProbe(256 + 60, 256, 0, up);
      vb = bg_LightProbe(256 + 60 + 400, 256, 0, up);
      {
        char w[140];
        snprintf(w, sizeof w, "lights: a strip lamp lights along its whole length (middle %.2f, end %.2f) and not far beyond (%.2f)", vc, ve, vb);
        CHECK(vc > 0.1 && fabs(vc - ve) < 0.03 && vb == 0, w);
      }
      (void) ceil_z;
      {
        // the strip lamp is cool white, the torch warm: their tints come through, and a tinted map really is tinted
        int cool = bg_LightProbeTint(256, 256, 0);
        int warm_type = bg_LightTypeFor(20101), t_warm, t_cool = cool, grey = 0, i, gl = -1;
        const unsigned char* pal = V_GetPlaypal();
        char w[140];

        P_RemoveMobj(m);
        m = P_SpawnMobj(256 * FRACUNIT, 256 * FRACUNIT, ONFLOORZ, warm_type);
        bg_Tics(1);
        t_warm = bg_LightProbeTint(256, 280, 32);
        snprintf(w, sizeof w, "lights: the strip lamp and the torch have different tints (cool %d, warm %d)", t_cool, t_warm);
        CHECK(t_cool > 0 && t_warm > 0 && t_cool != t_warm, w);
        // the lightest grey of the palette, in a mid-dark row, lit by each tint at half strength
        for (i = 0; i < 256; ++i)
        {
          int v = pal[i * 3] + pal[i * 3 + 1] + pal[i * 3 + 2];

          if (abs(pal[i * 3] - pal[i * 3 + 1]) < 8 && abs(pal[i * 3 + 1] - pal[i * 3 + 2]) < 8 && v > gl)
          {
            gl = v;
            grey = i;
          }
        }
        if (pal && t_warm > 0 && t_cool > 0)
        {
          int wi = bg_LightTestMap(t_warm, 6, 20, grey), ci = bg_LightTestMap(t_cool, 6, 20, grey);

          snprintf(w, sizeof w, "lights: a warm light turns grey orange (r %d b %d) and a cool one blue (r %d b %d)",
                   pal[wi * 3], pal[wi * 3 + 2], pal[ci * 3], pal[ci * 3 + 2]);
          CHECK(pal[wi * 3] > pal[wi * 3 + 2] + 10 && pal[ci * 3 + 2] > pal[ci * 3] + 10, w);
        }
      }
      P_RemoveMobj(m);

      {
        char sm[160], w[220];
        int sf = bg_SunSelfTest(sm, sizeof sm);

        snprintf(w, sizeof w, "sun: rays behave%s%s", sf ? " - failed: " : "", sm);
        CHECK(sf == 0, w);
      }
      {
        char fm[220], w[300];
        int ff = bg_FogSelfTest(fm, sizeof fm);

        snprintf(w, sizeof w, "fog: %s", fm);
        CHECK(ff == 0, w);
      }
      // shadows: rays against made-up geometry (pillar, step, closed door, low ceiling), then a real bake
      {
        char lm[160], w[220];
        int lf = bg_LightLosSelfTest(lm, sizeof lm);

        snprintf(w, sizeof w, "lights: shadow rays behave%s%s", lf ? " - failed: " : "", lm);
        CHECK(lf == 0, w);
      }
      {
        char w[200];
        double in_room, outside, under_ceil;
        mobj_t* lamp = P_SpawnMobj(256 * FRACUNIT, 256 * FRACUNIT, ONFLOORZ, bg_LightTypeFor(20102));

        bg_LightsBake();
        bg_Tics(1);
        in_room = bg_LightBakedAt(256 + 100, 256, false);
        outside = bg_LightBakedAt(600, 256, false);
        under_ceil = bg_LightBakedAt(256 + 100, 256, true);
        snprintf(w, sizeof w, "lights: a baked lamp lights the floor (%.2f) and ceiling (%.2f) of its room, not beyond the wall (%.2f)",
                 in_room, under_ceil, outside);
        CHECK(bg_LightBakedCount() >= 1 && in_room > 0.2 && under_ceil > 0.05 && outside == 0, w);
        P_RemoveMobj(lamp);
        bg_Tics(1);
        snprintf(w, sizeof w, "lights: a baked lamp whose thing is gone stops lighting (%.2f)", bg_LightBakedAt(256 + 100, 256, false));
        CHECK(bg_LightBakedAt(256 + 100, 256, false) == 0, w);
        {
          mobj_t* mv = P_SpawnMobj(256 * FRACUNIT, 256 * FRACUNIT, ONFLOORZ, bg_LightTypeFor(20202));

          bg_Tics(1);
          snprintf(w, sizeof w, "lights: a movable lamp is live, not baked (%.2f)", bg_LightProbe(256 + 100, 256, 32, NULL));
          CHECK(bg_LightProbe(256 + 100, 256, 32, NULL) > 0.2, w);
          P_RemoveMobj(mv);
        }
      }

      // the warning for a stationary light near a door or lift (Door_Open 11 on sector tag 7), and none for one far away
      {
        char w[200];
        int near_n = bg_LightWarnSelfTest(200, 11, 7), far_n = bg_LightWarnSelfTest(-300, 11, 7), none_n = bg_LightWarnSelfTest(200, 0, 7);

        snprintf(w, sizeof w, "lights: a stationary light near a door is warned about (%d), one far away (%d) or near a plain wall (%d) is not",
                 near_n, far_n, none_n);
        CHECK(near_n == 1 && far_n == 0 && none_n == 0, w);
      }

      // a light placed in the editor can be tuned with its args: radius (units of 8), strength %, colour
      {
        char w[160];
        double vdef, vsmall, vcol;
        mobj_t* t1 = P_SpawnMobj(256 * FRACUNIT, 256 * FRACUNIT, ONFLOORZ, bg_LightTypeFor(20101));

        bg_Tics(1);
        vdef = bg_LightProbe(256 + 150, 256, 48, NULL);
        t1->special_args[0] = 10;   // radius 80
        bg_Tics(1);
        vsmall = bg_LightProbe(256 + 150, 256, 48, NULL);
        t1->special_args[0] = 0;
        t1->special_args[2] = 0; t1->special_args[3] = 0; t1->special_args[4] = 255;   // blue
        bg_Tics(1);
        vcol = bg_LightProbeTint(256 + 100, 256, 48);
        P_RemoveMobj(t1);
        snprintf(w, sizeof w, "lights: args set the radius (%.2f at 150 units by default, %.2f with radius 80) and the colour (tint %d)",
                 vdef, vsmall, (int) vcol);
        CHECK(vdef > 0.05 && vsmall == 0 && vcol > 0, w);
      }

      // lights that follow the action: a projectile in flight, its explosion, a gun's flash
      {
        char w[160];
        double vm, vm_gone, vx, vf0, vf1;
        player_t* pl = &players[0];
        double pxw = (double) pl->mo->x / 65536.0, pyw = (double) pl->mo->y / 65536.0, pzw = (double) pl->mo->z / 65536.0;

        m = P_SpawnMobj(256 * FRACUNIT, 256 * FRACUNIT, ONFLOORZ, MT_ROCKET);
        m->momx = m->momy = m->momz = 0;
        m->flags |= MF_NOCLIP;
        bg_Tics(1);
        vm = bg_LightProbe(256 + 80, 256, (double) m->z / 65536.0 + 8, NULL);
        m->flags &= ~MF_MISSILE;
        m->state = &states[mobjinfo[MT_ROCKET].deathstate + 1];   // not through P_SetMobjState: no blast damage to the test's monsters
        m->tics = -1;
        bg_Tics(1);
        vx = bg_LightProbe(256 + 80, 256, (double) m->z / 65536.0 + 8, NULL);
        P_RemoveMobj(m);
        bg_Tics(1);
        vm_gone = bg_LightProbe(256 + 80, 256, 40, NULL);
        snprintf(w, sizeof w, "lights: a projectile lights its surroundings (%.2f at 80 units), its explosion does (%.2f), nothing once gone (%.2f)",
                 vm, vx, vm_gone);
        CHECK(vm > 0.15 && vx > 0.15 && vm_gone == 0, w);

        vf0 = bg_LightProbe(pxw, pyw, pzw + 40, NULL);
        pl->extralight = 0;
        P_SetPsprite(pl, ps_flash, S_PISTOLFLASH);
        bg_Tics(1);
        vf1 = bg_LightProbe(pxw, pyw, pzw + 40, NULL);
        P_SetPsprite(pl, ps_flash, S_NULL);
        snprintf(w, sizeof w, "lights: firing lights the shooter's surroundings (%.2f before, %.2f during the flash)", vf0, vf1);
        CHECK(vf1 > vf0 + 0.15, w);
        CHECK(pl->extralight == 0, "lights: a gun flash no longer brightens the whole sector (extralight stays 0)");

        // looking up or down stops at the limit and stays there (it used to bounce)
        {
          extern void P_HandleExCmdLook(player_t* player);
          int k2, bounced = 0, keep_look = pl->cmd.ex.look;
          angle_t keep_pitch2 = pl->mo->pitch;

          pl->mo->pitch = 0;
          for (k2 = 0; k2 < 40; ++k2) { pl->cmd.ex.look = 3000; P_HandleExCmdLook(pl); }
          CHECK(pl->mo->pitch == raven_angle_down_limit, "look: looking all the way down stops at the limit");
          for (k2 = 0; k2 < 20; ++k2)
          {
            pl->cmd.ex.look = 700;
            P_HandleExCmdLook(pl);
            bounced |= pl->mo->pitch != raven_angle_down_limit;
          }
          CHECK(!bounced, "look: it stays at the down limit while the mouse keeps pushing");
          for (k2 = 0; k2 < 80; ++k2) { pl->cmd.ex.look = -3000; P_HandleExCmdLook(pl); }
          CHECK(pl->mo->pitch == raven_angle_up_limit, "look: looking all the way up stops at the limit");
          for (k2 = 0; k2 < 20; ++k2)
          {
            pl->cmd.ex.look = -700;
            P_HandleExCmdLook(pl);
            bounced |= pl->mo->pitch != raven_angle_up_limit;
          }
          CHECK(!bounced, "look: it stays at the up limit while the mouse keeps pushing");
          pl->cmd.ex.look = keep_look;
          pl->mo->pitch = keep_pitch2;
        }

        // the light amplification pickup is a flashlight: a cone along the view, the screen is not turned full bright
        {
          double ca = cos((double) pl->mo->angle * (2.0 * M_PI / 4294967296.0)), sa = sin((double) pl->mo->angle * (2.0 * M_PI / 4294967296.0));
          double vfront, vback;
          int keep_p = pl->powers[pw_infrared], keep_pitch = (int) pl->mo->pitch;

          pl->mo->pitch = 0;
          pl->powers[pw_infrared] = 1000;
          bg_Tics(1);
          vfront = bg_LightProbe(pxw + 120 * ca, pyw + 120 * sa, pzw + 41, NULL);
          vback = bg_LightProbe(pxw - 120 * ca, pyw - 120 * sa, pzw + 41, NULL);
          snprintf(w, sizeof w, "lights: the flashlight lights ahead (%.2f) and not behind (%.2f); the screen is not full bright (colormap %d)",
                   vfront, vback, (int) pl->fixedcolormap);
          CHECK(vfront > 0.15 && vback < 0.05 && pl->fixedcolormap == 0, w);
          pl->powers[pw_infrared] = keep_p;
          pl->mo->pitch = (angle_t) keep_pitch;
          bg_Tics(1);
        }

        // power-ups on the floor glow until picked up
        {
          double vp, vp_gone;
          mobj_t* pu = P_SpawnMobj(256 * FRACUNIT, 256 * FRACUNIT, ONFLOORZ, MT_MISC12);   // soul sphere

          bg_Tics(1);
          vp = bg_LightProbe(256 + 60, 256, (double) pu->z / 65536.0 + 24, NULL);
          P_RemoveMobj(pu);
          bg_Tics(1);
          vp_gone = bg_LightProbe(256 + 60, 256, 24, NULL);
          double vq, vq_gone;
          mobj_t* pf = P_SpawnMobj(256 * FRACUNIT, 256 * FRACUNIT, ONFLOORZ, MT_PUFF);

          pf->tics = 100;
          bg_Tics(1);
          vq = bg_LightProbe(256 + 50, 256, (double) pf->z / 65536.0, NULL);
          P_RemoveMobj(pf);
          bg_Tics(1);
          vq_gone = bg_LightProbe(256 + 50, 256, 8, NULL);
          snprintf(w, sizeof w, "lights: a bullet puff lights its surroundings (%.2f at 50 units, %.2f once gone)", vq, vq_gone);
          CHECK(vq > 0.05 && vq_gone == 0, w);
          snprintf(w, sizeof w, "lights: a power-up on the floor glows (%.2f at 60 units), and does not once picked up (%.2f)", vp, vp_gone);
          CHECK(vp > 0.1 && vp_gone == 0, w);
        }
      }
      bg_render = 0;
    }
    dsda_UpdateIntConfig(dsda_config_begotten_lantern, cfg, false);
  }

  // chaingunners fire volleys of 30, SS guards 10, then break off for a while
  {
    int which, k, shots;
    int types[2] = { MT_CHAINGUY, MT_WOLFSS };
    int limits[2] = { 30, 10 };
    int god2 = p->cheats & CF_GODMODE;
    char wv[140];

    p->cheats |= CF_GODMODE;
    for (which = 0; which < 2; ++which)
    {
      mobj_t* z;
      int prev = -1, broke = -1;
      bg_RemoveIfLive(target); bg_ClearOthers(NULL); bg_GoToBigRoom(p->mo); p->mo->momx = p->mo->momy = 0;
      bg_target_type = types[which];
      z = bg_SpawnTarget(p->mo);
      bg_target_type = MT_BARREL;
      CHECK(z != NULL, "volley: spawned the shooter");
      if (!z) exit(1);
      target = z;
      z->health = 5000;
      P_SetTarget(&z->target, p->mo);
      P_SetMobjState(z, z->info->missilestate);
      shots = 0;
      for (k = 0; k < 500; ++k)
      {
        int st;
        bg_Tics(1);
        st = (int) (z->state - states);
        if (st != prev && states[st].action == (actionf_t) A_BGCPosAttack)
          ++shots;
        if (broke < 0 && shots > 0 && z->state->action == (actionf_t) A_Chase)
          broke = k;
        prev = st;
        if (broke >= 0) break;
      }
      snprintf(wv, sizeof wv, "volley: %s fired %d shots before breaking off (limit %d)",
               which ? "SS guard" : "chaingunner", shots, limits[which]);
      CHECK(shots == limits[which] && broke >= 0, wv);
      CHECK(z->reactiontime > 0, "volley: and waits before attacking again");
    }
    if (!god2) p->cheats &= ~CF_GODMODE;
    bg_RemoveIfLive(target); bg_ClearOthers(NULL);
    bg_target_type = MT_FIREELEM;
    target = bg_SpawnTarget(p->mo);
    bg_target_type = MT_BARREL;
    if (!target) exit(1);
    target->health = 3000;
  }

  // monsters aim dead on at the swordsman, normally at everybody else
  {
    int which, k, hits[2];
    int cls2 = p->bgclass, god3 = p->cheats & CF_GODMODE;
    mobj_t* z;

    p->cheats &= ~CF_GODMODE;
    bg_RemoveIfLive(target); bg_ClearOthers(NULL); bg_GoToBigRoom(p->mo); p->mo->momx = p->mo->momy = 0;
    bg_target_type = MT_POSSESSED;
    z = bg_SpawnTarget(p->mo);
    bg_target_type = MT_BARREL;
    if (!z) exit(1);
    target = z;
    z->health = 5000;
    z->bg_stun = 1000; // frozen: only the attack is called by hand
    P_SetTarget(&z->target, p->mo);
    for (which = 0; which < 2; ++which)
    {
      p->bgclass = bg_ClassFromWord(which ? "soldier" : "swordsman");
      hits[which] = 0;
      for (k = 0; k < 40; ++k)
      {
        int hp;
        p->mo->health = p->health = 100000;
        hp = p->mo->health;
        A_PosAttack(z);
        if (p->mo->health < hp) ++hits[which];
      }
    }
    snprintf(w_foe, sizeof w_foe, "foe aim: zombieman hits the swordsman %d/40, the soldier %d/40", hits[0], hits[1]);
    CHECK(hits[0] == 40 && hits[1] < 40, w_foe);
    p->mo->health = p->health = 200;
    p->bgclass = cls2;
    p->cheats |= god3;
    z->bg_stun = 0;
  }

  // zombieman: a burst of three shots, 14 tics apart, after a longer wind-up
  {
    int k, shots = 0, last = -1, gap_ok = 1, first = -1, prev_state = -1, shots_at_k[3] = { -1, -1, -1 };
    mobj_t* z;
    char wz[140];

    bg_RemoveIfLive(target); bg_ClearOthers(NULL); bg_GoToBigRoom(p->mo); p->mo->momx = p->mo->momy = 0;
    bg_target_type = MT_POSSESSED;
    z = bg_SpawnTarget(p->mo);
    bg_target_type = MT_BARREL;
    CHECK(z != NULL, "zombieman: spawned");
    if (!z) exit(1);
    target = z;
    z->health = 5000;
    P_SetTarget(&z->target, p->mo);
    P_SetMobjState(z, S_POSS_ATK1);
    for (k = 0; k < 60; ++k) // the first burst only
    {
      int st;
      bg_Tics(1);
      st = (int) (z->state - states);
      if (st != prev_state && states[st].action == (actionf_t) A_PosAttack)
      {
        if (last < 0 || k - last > 4) // a new shot (a shot may be several bullets a tic or two apart)
        {
          if (first < 0) first = k;
          if (shots_at_k[0] >= 0 && shots == 1 && k - shots_at_k[0] < 12) gap_ok = 0;
          if (shots < 3) shots_at_k[shots] = k;
          ++shots;
        }
        last = k;
      }
      prev_state = st;
    }
    snprintf(wz, sizeof wz, "zombieman: %d shots (first at %d, then %d, %d)", shots, first, shots_at_k[1], shots_at_k[2]);
    CHECK(shots == 3 && gap_ok && first >= 11 && shots_at_k[1] - shots_at_k[0] >= 12 && shots_at_k[2] - shots_at_k[1] >= 12, wz);
  }

  // co-op friendly fire (a second synthetic player stands in front of the first)
  {
    mobj_t* p2mo;
    mobj_t* shot;
    int hp0, ff_saved = dsda_IntConfig(dsda_config_begotten_friendly_fire), dm_saved = deathmatch;
    angle_t pa2 = p->mo->angle;
    int k2;

    bg_RemoveIfLive(target);
    target = NULL;
    bg_ClearOthers(NULL);
    bg_GoToBigRoom(p->mo);
    p->mo->momx = p->mo->momy = 0;
    pa2 = p->mo->angle;
    deathmatch = 0;

    p2mo = P_SpawnMobj(p->mo->x + FixedMul(200 * FRACUNIT, finecosine[pa2 >> ANGLETOFINESHIFT]),
                       p->mo->y + FixedMul(200 * FRACUNIT, finesine[pa2 >> ANGLETOFINESHIFT]),
                       ONFLOORZ, MT_PLAYER);
    p2mo->player = &players[1];
    players[1].mo = p2mo;
    players[1].health = p2mo->health = 1000;
    players[1].playerstate = PST_LIVE;
    players[1].bgclass = 0;
    playeringame[1] = true;
    p2mo->angle = pa2 + ANG180;
    p2mo->flags &= ~MF_NOGRAVITY;
    CHECK(p2mo != NULL, "friendly fire: second player spawned");

    // off: no damage between players, in either direction, nor from splash
    dsda_UpdateIntConfig(dsda_config_begotten_friendly_fire, 0, false);
    CHECK(bg_CoopShield(p->mo, p2mo) && bg_CoopShield(p2mo, p->mo), "friendly fire off: players are shielded");
    CHECK(!bg_CoopShield(p->mo, p->mo) && !bg_CoopShield(p->mo, NULL), "friendly fire: self and nothing are not shielded");
    hp0 = p2mo->health;
    P_DamageMobj(p2mo, p->mo, p->mo, 40);
    CHECK(p2mo->health == hp0, "friendly fire off: a player's damage does nothing to another player");

    // off: a fireball flies through the other player
    P_MapStart();
    shot = P_SpawnMissile(p->mo, p2mo, MT_TROOPSHOT);
    P_MapEnd();
    CHECK(shot != NULL, "friendly fire: fireball spawned");
    if (shot)
    {
      shot->target = p->mo;
      for (k2 = 0; k2 < 40; ++k2) bg_Tics(1);
      CHECK(p2mo->health == hp0, "friendly fire off: a fireball passes through the other player");
    }
    bg_CountType(MT_TROOPSHOT);

    // on: damage lands, the fireball hits
    dsda_UpdateIntConfig(dsda_config_begotten_friendly_fire, 1, false);
    CHECK(!bg_CoopShield(p->mo, p2mo), "friendly fire on: players are not shielded");
    P_DamageMobj(p2mo, p->mo, p->mo, 40);
    CHECK(p2mo->health < hp0, "friendly fire on: damage reaches the other player");
    hp0 = p2mo->health;
    P_MapStart();
    shot = P_SpawnMissile(p->mo, p2mo, MT_TROOPSHOT);
    P_MapEnd();
    if (shot)
    {
      shot->target = p->mo;
      for (k2 = 0; k2 < 40; ++k2) bg_Tics(1);
      CHECK(p2mo->health < hp0, "friendly fire on: a fireball hurts the other player");
    }

    // on: a guarding player turns another player's fireball; off: nothing to turn
    dsda_UpdateIntConfig(dsda_config_begotten_friendly_fire, 1, false);
    players[1].bgparry = 1000;
    P_MapStart();
    shot = P_SpawnMissile(p->mo, p2mo, MT_TROOPSHOT);
    P_MapEnd();
    if (shot)
    {
      mobj_t* seen = NULL;
      shot->target = p->mo;
      hp0 = p2mo->health;
      for (k2 = 0; k2 < 40; ++k2)
      {
        bg_Tics(1);
        if (shot->target == p2mo) { seen = shot; break; }
      }
      CHECK(seen != NULL && p2mo->health == hp0, "friendly fire on: a parrying player turns another player's fireball");
    }
    players[1].bgparry = 0;

    // three players: the witch's fireball is parried by the swordsman, who aims at the soldier
    {
      static const int face_off[5] = { 40, -40, 60, 20, 20 };    // degrees her guard is turned away from the shooter
      static const int aim_off[5] = { 0, 3, -3, 24, 40 };        // degrees the soldier is off her crosshair
      int round3, k3;
      mobj_t* p3mo;
      int hs0, hw0;
      fixed_t shot_end_dist = 0;

      players[1].bgclass = bg_ClassFromWord("swordsman");
      for (round3 = 0; round3 < 5; ++round3)
      {
        angle_t f = pa2 + ANG180 + face_off[round3] * ANG1;     // where the swordsman looks
        angle_t t = f + aim_off[round3] * ANG1;                 // where the soldier stands from her
        int hit = 0, hurt3 = 0;

        bg_ClearOthers(NULL);
        p->mo->momx = p->mo->momy = 0;
        p2mo->momx = p2mo->momy = 0;
        P_MapStart();
        P_TeleportMove(p2mo, p->mo->x + FixedMul(140 * FRACUNIT, finecosine[pa2 >> ANGLETOFINESHIFT]),
                       p->mo->y + FixedMul(140 * FRACUNIT, finesine[pa2 >> ANGLETOFINESHIFT]), false);
        P_MapEnd();
        p2mo->angle = f;
        p3mo = P_SpawnMobj(p2mo->x + FixedMul(110 * FRACUNIT, finecosine[t >> ANGLETOFINESHIFT]),
                           p2mo->y + FixedMul(110 * FRACUNIT, finesine[t >> ANGLETOFINESHIFT]), ONFLOORZ, MT_PLAYER);
        p3mo->player = &players[2];
        players[2].mo = p3mo;
        players[2].health = p3mo->health = 1000;
        players[2].playerstate = PST_LIVE;
        playeringame[2] = true;
        hs0 = p3mo->health;
        hw0 = p->mo->health;
        p2mo->health = players[1].health = 1000;
        p->cheats |= CF_GODMODE;
        P_MapStart();
        shot = P_SpawnMissile(p->mo, p2mo, bg_fireball_type);
        P_MapEnd();
        for (k3 = 0; shot && k3 < 60 && !hit && !hurt3; ++k3)
        {
          players[1].bgparry = 1000;
          bg_Tics(1);
          shot_end_dist = P_AproxDistance(shot->x - p3mo->x, shot->y - p3mo->y);
          // splash from the explosion also hurts her, so only a ball that ends on top of her counts as a hit
          if (p3mo->health < hs0) { hit = (shot_end_dist <= p3mo->radius + 16 * FRACUNIT); hurt3 = 1; break; }
        }
        {
          char w3[150];

          // the other player is never auto-targeted: the ball flies along the crosshair, so it hits her only if she stands on it
          dboolean on_line = abs(aim_off[round3]) <= 3;

          snprintf(w3, sizeof w3, "three players: the swordsman (guard %d off the witch), soldier %d off the crosshair: %s (tics %d, soldier hp %d -> %d)", face_off[round3], aim_off[round3], on_line ? "the ball along the crosshair hits her" : "the ball flies past her, no autoaim on players", k3, hs0, p3mo->health);
          CHECK(hit == on_line, w3);
        }
        CHECK(p->mo->health == hw0, "three players: ... and the witch is not hit by her own fireball");
        playeringame[2] = false;
        players[2].mo = NULL;
        p3mo->player = NULL;
        P_RemoveMobj(p3mo);
        players[1].bgparry = 0;
      }
      players[1].bgclass = 0;
    }

    // autoaim: skips the other player unless friendly fire is on
    bg_ClearOthers(NULL);
    {
      fixed_t sl;
      dsda_UpdateIntConfig(dsda_config_begotten_friendly_fire, 0, false);
      p->mo->angle = pa2;
      sl = 0;
      P_AimLineAttack(p->mo, pa2, 2048 * FRACUNIT, 0);
      CHECK(linetarget != p2mo, "friendly fire off: autoaim ignores the other player");
      dsda_UpdateIntConfig(dsda_config_begotten_friendly_fire, 1, false);
      P_AimLineAttack(p->mo, pa2, 2048 * FRACUNIT, 0);
      CHECK(linetarget == p2mo, "friendly fire on: autoaim finds the other player");
      (void) sl;
    }

    playeringame[1] = false;
    players[1].mo = NULL;
    p2mo->player = NULL;
    P_RemoveMobj(p2mo);
    dsda_UpdateIntConfig(dsda_config_begotten_friendly_fire, ff_saved, false);
    deathmatch = dm_saved;
    bg_ClearOthers(NULL);
    target = bg_SpawnTarget(p->mo); // the next test removes it again
  }

  // hellfire: recoil moves the player even with nothing nearby
  {
    mobj_t* farthest = NULL;
    fixed_t x0, y0, moved;
    int i3;
    bg_RemoveIfLive(target);
    target = NULL;
    bg_ClearOthers(NULL);
    bg_GoToBigRoom(p->mo);
    bg_Ready(p, wp_hellfire);
    p->mo->momx = p->mo->momy = 0;
    x0 = p->mo->x; y0 = p->mo->y;
    p->cmd.buttons = BT_ATTACK;
    for (i3 = 0; i3 < 30; ++i3)
    {
      bg_Tics(1);
      if (bg_CountType(bg_hellfire_type)) { break; }
    }
    p->cmd.buttons = 0;
    bg_Tics(10);
    moved = P_AproxDistance(p->mo->x - x0, p->mo->y - y0);
    lprintf(LO_INFO, "  hellfire recoil moved the player %d units\n", moved >> FRACBITS);
    CHECK(moved > 8 * FRACUNIT, "hellfire: recoil pushes the player without any target");
    (void) farthest;
  }

  // fire rates: shots per second while holding fire (35 tics = 1 second)
  {
    static const struct { weapontype_t w; int alt; const char* name; } modes[] = {
      { wp_pistol, 0, "pistol" }, { wp_pistol, 1, "revolver (alt)" }, { wp_hellfire, 0, "hellfire" } };
    int m;
    for (m = 0; m < 3; ++m)
    {
      weapontype_t primary = modes[m].w;
      weapontype_t mode = modes[m].alt ? bg_slot_alt[bg_WeaponSlot(primary)] : primary;
      int type = doom_weaponinfo[mode].ammo, use = doom_weaponinfo[mode].ammopershot, before, after, k;
      char w4[120];
      bg_ClearOthers(NULL);
      bg_Ready(p, primary);
      bg_Tics(3);
      bg_Ready(p, primary);
      before = p->ammo[type];
      p->cmd.buttons = BT_ATTACK;
      p->cmd.ex.actions = modes[m].alt ? XC_ALT : 0;
      for (k = 0; k < 140; ++k)
        bg_Tics(1);
      after = p->ammo[type];
      p->cmd.buttons = 0;
      p->cmd.ex.actions = 0;
      snprintf(w4, sizeof w4, "fire rate %s: %.2f shots per second", modes[m].name,
               (before - after) / (double) use / 4.0);
      CHECK(1, w4);
      bg_Tics(60);
    }
  }

  // classes: reborn inventory, damage, fire rate
  {
    int cls, rate[2] = { 0, 0 };
    char w7[140];
    for (cls = 0; cls < 2; ++cls)
    {
      int before, k;
      mobj_t* dummy;

      bg_player_class[0] = cls;
      bg_ClassReborn(p);
      snprintf(w7, sizeof w7, "class %s: reborn gives health %d, bullets %d, weapon %d", bg_ClassName(cls),
               p->health, p->ammo[am_clip], p->readyweapon);
      CHECK(p->bgclass == cls && p->health > 0 && p->weaponowned[wp_fist] && (p->weaponowned[wp_pistol] || bg_ClassMeleeOnly(p)), w7);
      CHECK(cls == 0 ? p->health == 100 : p->health == 90, "class: starting health comes from the class");
      CHECK(cls == 0 ? p->maxammo[am_cell] == maxammo[am_cell] : p->maxammo[am_cell] > maxammo[am_cell],
            "class: ammo capacity comes from the class");

      dummy = P_SpawnMobj(p->mo->x + 400 * FRACUNIT, p->mo->y, p->mo->z, MT_BARREL);
      p->readyweapon = wp_pistol;
      snprintf(w7, sizeof w7, "class %s: pistol damage 100 -> %d", bg_ClassName(cls),
               dsda_ScaleDamage(dummy, NULL, p->mo, 100));
      CHECK(cls == 0 ? dsda_ScaleDamage(dummy, NULL, p->mo, 100) > 100 : dsda_ScaleDamage(dummy, NULL, p->mo, 100) < 100, w7);
      P_RemoveMobj(dummy);

      bg_ClearOthers(NULL);
      bg_Ready(p, wp_pistol);
      bg_Tics(3);
      bg_Ready(p, wp_pistol);
      before = p->ammo[am_clip];
      p->cmd.buttons = BT_ATTACK;
      for (k = 0; k < 140; ++k)
        bg_Tics(1);
      p->cmd.buttons = 0;
      rate[cls] = before - p->ammo[am_clip];
      bg_Tics(40);
    }
    snprintf(w7, sizeof w7, "class fire rate: soldier %d vs witch %d bullets with the pistol", rate[0], rate[1]);
    CHECK(rate[0] > rate[1], w7);
    bg_player_class[0] = 0;
    bg_ClassReborn(p);
    for (cls = 0; cls < NUMWEAPONS; ++cls)
      p->weaponowned[cls] = true; // the tests after this one need everything
  }

  // alt fire released while fire is still held: continue with the primary mode
  bg_Ready(p, wp_pistol);
  p->cmd.buttons = BT_ATTACK;
  p->cmd.ex.actions = XC_ALT;
  bg_Tics(8);
  CHECK(p->altactive && p->readyweapon == wp_dualpistols, "holding alt: revolver");
  p->cmd.ex.actions = 0;
  for (i = 0; i < 40 && p->altactive; ++i)
    bg_Tics(1);
  CHECK(!p->altactive && p->readyweapon == wp_pistol, "alt released, fire held: pistol again");

  // alt fire with no ammo for the secondary: nothing fires
  bg_Ready(p, wp_pistol);
  p->ammo[am_clip] = 0;
  p->cmd.buttons = BT_ATTACK;
  p->cmd.ex.actions = XC_ALT;
  bg_Tics(20);
  CHECK(!p->altactive && p->ammo[am_clip] == 0, "no ammo: alt fire does nothing");

  // both revolver modes cost one bullet from the same pool
  CHECK(doom_weaponinfo[wp_dualpistols].ammopershot == doom_weaponinfo[wp_pistol].ammopershot,
        "revolver secondary costs the same as the primary");

  // weapon change while a secondary mode is firing
  bg_Ready(p, wp_shotgun);
  p->cmd.buttons = BT_ATTACK;
  p->cmd.ex.actions = XC_ALT;
  bg_Tics(5);
  CHECK(p->altactive && p->readyweapon == wp_supershotgun, "scattergun alt fire");
  p->cmd.buttons = BT_CHANGE | (wp_chaingun << BT_WEAPONSHIFT);
  p->cmd.ex.actions = 0;
  bg_Tics(1);
  p->cmd.buttons = 0;
  for (i = 0; i < 200; ++i)
    bg_Tics(1);
  CHECK(!p->altactive && p->readyweapon == wp_chaingun, "weapon change during the alt attack works");

  // pickups
  {
    int k;
    extern dboolean P_GiveWeapon(player_t*, weapontype_t, dboolean);

    for (k = 0; k < 6; ++k)
    {
      int ammo, clips;
      const char* msg;
      mobj_t* item;
      char what[96];

      CHECK(bg_AmmoPickup(bg_PickupType(k), &ammo, &clips, &msg), "pickup type known");
      item = P_SpawnMobj(p->mo->x, p->mo->y, p->mo->z, bg_PickupType(k));
      p->ammo[ammo] = 0;
      p->maxammo[ammo] = bg_slot_primary[1] != wp_nochange ? p->maxammo[ammo] : 0;
      P_TouchSpecialThing(item, p->mo);
      snprintf(what, sizeof what, "pickup %d gives %d x clip of %s (%d)", k, clips, bg_AmmoName(ammo), p->ammo[ammo]);
      CHECK(p->ammo[ammo] == clips * clipammo[ammo] || p->ammo[ammo] == p->maxammo[ammo], what);
    }

    for (k = 0; k < NUMWEAPONS; ++k)
      p->weaponowned[k] = false;
    CHECK(P_GiveWeapon(p, bg_PickupWeapon(wp_plasma), false) && p->weaponowned[wp_tornado],
          "plasma pickup gives slot 6 (tornado)");
    CHECK(P_GiveWeapon(p, bg_PickupWeapon(wp_bfg), false) && p->weaponowned[wp_snap],
          "bfg pickup gives slot 7 (snap)");
    p->ammo[am_spirit] = 0;
    P_GiveWeapon(p, bg_PickupWeapon(wp_bfg), false);
    CHECK(p->ammo[am_spirit] >= doom_weaponinfo[wp_bfg].ammopershot,
          "a found bfg gives enough spirit for one thunderclap (the secondary)");
    p->readyweapon = wp_snap;
    p->cmd.ex.actions |= XC_ALT; p->bgaltprev = 1;
    CHECK(bg_AltUsable(p, wp_snap), "snap's secondary (thunderclap) is usable with that spirit");
    p->cmd.ex.actions &= ~XC_ALT; p->bgaltprev = 0;
    CHECK(P_GiveWeapon(p, bg_PickupWeapon(wp_supershotgun), false) && p->weaponowned[wp_hellfire],
          "super shotgun pickup gives slot 8 (hellfire)");
    CHECK(p->ammo[am_cell] > 0 && p->ammo[am_spirit] > 0 && p->ammo[am_hellfire] > 0,
          "weapon pickups give their ammo");
  }

  bg_RosterLiveTest(p);

  {
    // the fourteen items (0.10.13)
    static const struct { int type; const char* name; } its[] = {
      {MT_MISC11, "first aid kit"}, {MT_MISC10, "health potion"}, {MT_MISC2, "health vial"}, {MT_MISC3, "shield charge"},
      {MT_MISC0, "bulletproof vest"}, {MT_MISC1, "shield potion"}, {MT_MISC12, "health spell"}, {MT_MEGA, "life and protection spell"},
      {MT_MISC13, "adrenaline"}, {MT_INV, "immortality"}, {MT_MISC14, "boots"}, {MT_MISC15, "cartography"}, {MT_MISC16, "goggles"},
      {MT_MISC24, "backpack"}};
    int i;
    mobj_t* it;
    char what[96];

#define TOUCH(t) do { it = P_SpawnMobj(p->mo->x, p->mo->y, p->mo->z, (t)); P_TouchSpecialThing(it, p->mo); } while (0)
    memset(p->powers, 0, sizeof p->powers);
    p->armorpoints[ARMOR_ARMOR] = 0; p->armortype = 0;
    p->health = p->mo->health = 20;
    TOUCH(MT_MISC11); CHECK(p->health == 35, "first aid kit: +15");
    TOUCH(MT_MISC10); CHECK(p->health == 75, "health potion: +40");
    TOUCH(MT_MISC10); CHECK(p->health == 100, "health potion: capped at 100");
    TOUCH(MT_MISC2); CHECK(p->health == 102, "health vial: +2, over 100");
    p->health = p->mo->health = 199; TOUCH(MT_MISC2); CHECK(p->health == 200, "health vial: capped at 200");
    p->health = p->mo->health = 50; TOUCH(MT_MISC12); CHECK(p->health == 150, "health spell: +100");
    TOUCH(MT_MISC12); CHECK(p->health == 200, "health spell: capped at 200");
    TOUCH(MT_MISC3); CHECK(p->armorpoints[ARMOR_ARMOR] == 2 && p->armortype == 1, "shield charge: +2 shield");
    p->armorpoints[ARMOR_ARMOR] = 199; TOUCH(MT_MISC3); CHECK(p->armorpoints[ARMOR_ARMOR] == 200, "shield charge: capped at 200");
    p->armorpoints[ARMOR_ARMOR] = 0; p->armortype = 0;
    TOUCH(MT_MISC0); CHECK(p->armorpoints[ARMOR_ARMOR] == 100 && p->armortype == 1, "bulletproof vest: 100 shield, 1/3");
    TOUCH(MT_MISC1); CHECK(p->armorpoints[ARMOR_ARMOR] == 200 && p->armortype == 2, "shield potion: 200 shield, 1/2");
    p->armorpoints[ARMOR_ARMOR] = 0; p->armortype = 0; p->health = p->mo->health = 30;
    TOUCH(MT_MEGA); CHECK(p->health == 200 && p->armortype == 2 && p->armorpoints[ARMOR_ARMOR] == 200, "life and protection spell: 200 health and the blue shield");
    p->health = p->mo->health = 30;
    TOUCH(MT_MISC13);
    CHECK(p->health == 100 && p->powers[pw_strength] > 0, "adrenaline: heals to 100 and starts");
    {
      mobj_t* victim = P_SpawnMobj(p->mo->x + 64 * FRACUNIT, p->mo->y, p->mo->z, MT_POSSESSED);
      int before = victim->health;
      victim->flags &= ~MF_SHOOTABLE; victim->flags |= MF_SHOOTABLE;
      P_DamageMobj(victim, p->mo, p->mo, 10);
      CHECK(before - victim->health == 20 || victim->health <= 0, "adrenaline: double damage");
    }
    p->powers[pw_strength] = BG_ADRENALINE_TICS;
    p->mo->momx = p->mo->momy = 0;
    P_PlayerThink(p);
    CHECK(p->powers[pw_strength] == 0, "adrenaline: ends after 120 seconds");
    TOUCH(MT_INV); CHECK(p->powers[pw_invulnerability] == 60 * 35, "immortality: 60 seconds");
    TOUCH(MT_MISC14); CHECK(p->powers[pw_ironfeet] == 60 * 35, "boots: 60 seconds");
    TOUCH(MT_MISC15); CHECK(p->powers[pw_allmap], "cartography: whole level");
    TOUCH(MT_MISC16); CHECK(p->powers[pw_infrared] == 120 * 35, "goggles: 120 seconds");
    p->backpack = false;
    for (i = 0; i < NUMAMMO; ++i) p->maxammo[i] = 100;
    TOUCH(MT_MISC24); CHECK(p->backpack && p->maxammo[0] == 200, "backpack: doubles the ammo capacity");
    // flashes: half of the damage tint, blue for health, yellow for shield
    p->powers[pw_invulnerability] = 0; p->powers[pw_ironfeet] = 0; p->powers[pw_strength] = 0;
    p->bonuscount = 0; p->damagecount = 0; p->health = p->mo->health = 100;
    bg_PickupFlash(p, 0, false);
    CHECK(bg_FlashPalette(p) >= BG_PAL_BLUE && bg_FlashPalette(p) < BG_PAL_YELLOW, "health pickup flashes blue");
    bg_PickupFlash(p, 1, true);
    CHECK(bg_FlashPalette(p) >= BG_PAL_YELLOW && bg_FlashPalette(p) < BG_PAL_PURPLE, "shield pickup flashes yellow");
    for (i = 0; i < 40; ++i) bg_FlashTick(p);
    CHECK(bg_FlashPalette(p) == 0, "the flash ends");
    (void) its;
    (void) what;
#undef TOUCH
  }

  {
    // co-op pickups (0.10.14)
    player_t saved1 = players[1];
    dboolean was_in = playeringame[1];
    int saved_skill = gameskill;
    mobj_t* m1 = P_SpawnMobj(p->mo->x, p->mo->y, p->mo->z, MT_PLAYER);
    mobj_t* it;
    int slot = bg_KeySlot(0), a0;
    player_t* q;

#define GONE(m) ((m)->thinker.function != P_MobjThinker)
#define SPAWN(t) P_SpawnMobj(p->mo->x, p->mo->y, p->mo->z, (t))
    players[1] = *p;
    q = &players[1];
    q->mo = m1; m1->player = q; m1->health = q->health = 100;
    playeringame[1] = true;
    CHECK(bg_CoopItems(), "two players: co-op item rules apply");

    // difficulty 1: a placed weapon is taken once by each player, then gone
    gameskill = 0;
    p->weaponowned[wp_shotgun] = q->weaponowned[wp_shotgun] = false;
    p->weaponowned[wp_supershotgun] = q->weaponowned[wp_supershotgun] = false;
    it = SPAWN(MT_SHOTGUN);
    P_TouchSpecialThing(it, p->mo);
    CHECK(!GONE(it) && bg_TakenBy(it, p) && !bg_TakenBy(it, q), "easy: the first player's weapon pickup stays for the other");
    a0 = p->ammo[am_shell];
    P_TouchSpecialThing(it, p->mo);
    CHECK(p->ammo[am_shell] == a0, "easy: the same player cannot take it twice");
    P_TouchSpecialThing(it, m1);
    CHECK(GONE(it), "easy: gone once everybody has taken it");
    it = SPAWN(MT_MISC11);
    p->health = p->mo->health = 100; q->health = m1->health = 50;
    P_TouchSpecialThing(it, p->mo);
    CHECK(!GONE(it) && !bg_TakenBy(it, p), "easy: a first aid kit nobody needs yet stays");
    P_TouchSpecialThing(it, m1);
    CHECK(!GONE(it) && q->health == 65 && bg_TakenBy(it, q), "easy: the hurt player takes it, it stays for the other");
    p->health = p->mo->health = 40;
    P_TouchSpecialThing(it, p->mo);
    CHECK(GONE(it) && p->health == 55, "easy: both took it, so it is gone");

    // difficulty 4: power-ups are one shared pool, ammo is not
    gameskill = 3;
    p->ammo[am_clip] = q->ammo[am_clip] = 0;
    it = SPAWN(MT_MISC11);
    q->health = m1->health = 50;
    P_TouchSpecialThing(it, m1);
    CHECK(GONE(it) && q->health == 65, "ultra-violence: a power-up is gone for everybody once taken");
    it = SPAWN(MT_CLIP);
    P_TouchSpecialThing(it, p->mo);
    CHECK(!GONE(it), "ultra-violence: ammo still exists for the other player");
    P_TouchSpecialThing(it, m1);
    CHECK(GONE(it), "ultra-violence: ammo gone once both took it");

    // difficulty 5: everything but keys and placed weapons is shared
    gameskill = 4;
    p->ammo[am_clip] = q->ammo[am_clip] = 0;
    it = SPAWN(MT_CLIP);
    P_TouchSpecialThing(it, p->mo);
    CHECK(GONE(it), "nightmare: ammo is one shared pool");
    it = SPAWN(MT_SHOTGUN);
    p->weaponowned[wp_shotgun] = q->weaponowned[wp_shotgun] = false;
    P_TouchSpecialThing(it, p->mo);
    CHECK(!GONE(it), "nightmare: a placed weapon is still for each player");
    P_TouchSpecialThing(it, m1);
    CHECK(GONE(it), "nightmare: a placed weapon is gone once both took it");
    it = SPAWN(MT_SHOTGUN);
    it->flags |= MF_DROPPED;
    p->weaponowned[wp_shotgun] = q->weaponowned[wp_shotgun] = false;
    P_TouchSpecialThing(it, p->mo);
    CHECK(GONE(it), "nightmare: a dropped weapon is shared");

    // keys
    gameskill = 1;
    p->cards[slot] = q->cards[slot] = false;
    it = SPAWN(bg_KeyType(0));
    P_TouchSpecialThing(it, p->mo);
    CHECK(p->cards[slot] && q->cards[slot] && GONE(it), "easy: a key is given to everybody and removed");
    gameskill = 3;
    p->cards[slot] = q->cards[slot] = false;
    it = SPAWN(bg_KeyType(0));
    P_TouchSpecialThing(it, p->mo);
    CHECK(p->cards[slot] && !q->cards[slot] && !GONE(it), "ultra-violence: only the one who takes the key has it");
    P_TouchSpecialThing(it, m1);
    CHECK(q->cards[slot] && !GONE(it), "ultra-violence: the key stays after both took it");
    G_PlayerReborn(1);
    CHECK(players[1].cards[slot], "ultra-violence: dying keeps the keys");
    gameskill = 4;
    G_PlayerReborn(1);
    CHECK(!players[1].cards[slot] && !bg_TakenBy(it, &players[1]) && bg_TakenBy(it, p), "nightmare: dying wipes the keys, and the key can be taken again");
    players[1].mo = m1;
    P_TouchSpecialThing(it, m1);
    CHECK(players[1].cards[slot], "nightmare: the key is taken again");

    P_RemoveMobj(m1);
    players[1] = saved1;
    playeringame[1] = was_in;
    gameskill = saved_skill;
    p->cards[slot] = false;
#undef GONE
#undef SPAWN
  }

  {
    // the nine keys: touching one gives exactly its colour (kept for everyone only in a netgame), and its lock opens for it
    int c, d;

    for (c = 0; c < BG_KEYS; ++c)
      p->cards[bg_KeySlot(c)] = false;
    for (c = 0; c < BG_KEYS; ++c)
    {
      mobj_t* key = P_SpawnMobj(p->mo->x, p->mo->y, p->mo->z, bg_KeyType(c));
      char what[96];
      int frames = 0, st = (int) (key->state - states), first = st;

      do { st = states[st].nextstate; ++frames; } while (st != first && frames < 16);
      snprintf(what, sizeof what, "%s key loops over 8 frames (%d)", bg_KeyName(c), frames);
      CHECK(frames == 8, what);
      CHECK(!P_CheckKeys(p->mo, (zdoom_lock_t) bg_KeyLockNumber(c), false), "locked before the key is picked up");
      P_TouchSpecialThing(key, p->mo);
      snprintf(what, sizeof what, "%s key is given", bg_KeyName(c));
      CHECK(p->cards[bg_KeySlot(c)], what);
      for (d = 0; d < BG_KEYS; ++d)
        if (d != c && d < c + 0)
          CHECK(p->cards[bg_KeySlot(d)], "earlier keys are kept");
      snprintf(what, sizeof what, "%s lock opens", bg_KeyName(c));
      CHECK(P_CheckKeys(p->mo, (zdoom_lock_t) bg_KeyLockNumber(c), false), what);
      CHECK(P_CheckKeys(p->mo, zk_any, false), "any key opens");
      CHECK(c == BG_KEYS - 1 ? P_CheckKeys(p->mo, zk_all, false) : !P_CheckKeys(p->mo, zk_all, false),
            "all-keys lock opens only with all nine");
    }
  }


  {
    // saving: everything Begotten adds must come back from a save (the quick save and the key frames use the same code).
    // Store, run 150 tics, restore, check the state is what it was, run the same 150 tics: it has to end up identical.
    int k, ni;
    unsigned h0, ha, hr, hb;
    static const char* const who[4] = { "imp", "beetle", "golem", "heart" };
    mobj_t* pm;

    bg_ClearOthers(NULL);
    bg_GoToBigRoom(p->mo);
    for (ni = 0; ni < 4; ++ni)
    {
      int t = bg_RType(who[ni]);
      mobj_t* mm = t > 0 ? bg_RSpawn(p->mo, t, 150 + 40 * ni) : NULL;

      if (mm)
        bg_RWake(mm, p->mo);
    }
    p->cheats &= ~CF_NOTARGET;
    for (k = 0; k < 40; ++k)
    {
      bg_Tics(1);
      p->mo->health = p->health = 200;
    }
    h0 = bg_StateHash();
    dsda_StoreQuickKeyFrame();
    for (k = 0; k < 150; ++k)
    {
      bg_Tics(1);
      players[0].mo->health = players[0].health = 200;
    }
    ha = bg_StateHash();
    dsda_RestoreQuickKeyFrame();
    p = &players[0];
    pm = p->mo;
    hr = bg_StateHash();
    CHECK(pm != NULL, "save: the player has a body after loading");
    {
      char w[120];

      snprintf(w, sizeof w, "save: loading brings back the state exactly (%08x, was %08x)", hr, h0);
      CHECK(hr == h0, w);
    }
    for (k = 0; k < 150; ++k)
    {
      bg_Tics(1);
      players[0].mo->health = players[0].health = 200;
    }
    hb = bg_StateHash();
    {
      char w[120];

      snprintf(w, sizeof w, "save: the game after loading goes the same way as the first time (%08x, was %08x)", hb, ha);
      CHECK(hb == ha, w);
    }
    bg_ClearOthers(NULL);
    p = &players[0];
  }

  lprintf(LO_INFO, "BEGOTTEN LIVETEST: %s (%d failures)\n", selftest_failures ? "FAILED" : "PASSED",
          selftest_failures);
  (void) failures;
  exit(selftest_failures ? 1 : 0);
}

static void bg_SpawnShowcase(player_t* p);

// BEGOTTEN_SELFTEST=6: just the pickup row, nothing else (for comparing screenshots)
void dsda_BegottenShowcase(void)
{
  static int done;

  if (done)
    return;
  done = 1;
  players[0].cheats |= CF_GODMODE;
  bg_SpawnShowcase(&players[0]);
}


// BEGOTTEN_SELFTEST=7: a dark room with every kind of light, for looking at (and for screenshots)
void dsda_BegottenLightDemo(void)
{
  static int done;
  player_t* p = &players[0];
  int i;
  static const struct { int num; int fwd, side; angle_t yaw_off; } put[] = {
    { 20101, 150, -130, 0 },        // torch
    { 20102, 210, 70, 0 },          // lamp
    { 20103, 110, 150, 0 },         // spot from the ceiling
    { 20104, 215, -30, 0 },         // strip lamp along the facing direction
    { 20104, 70, -60, ANG90 },      // strip lamp across it
  };
  static const struct { int num; int fwd, side; angle_t yaw_off; } colours[] = {
    { 20101, 210, -170, 0 }, { 20106, 215, -60, 0 }, { 20107, 215, 60, 0 }, { 20108, 210, 170, 0 }, { 20105, 90, 0, 0 },
  };

  static mobj_t* dyn[4];

  if (done && getenv("BGLIGHT_DYN"))
  {
    if (leveltime % 35 == 0)
      lprintf(LO_INFO, "DYNDEMO: lights %d, flash %d, dyn %d %d %d %d\n", bg_LightCount(), p->psprites[ps_flash].state != NULL,
              dyn[0] != NULL, dyn[1] != NULL, dyn[2] != NULL, dyn[3] != NULL);
    // hold the moving things still so the picture can be taken
    for (i = 0; i < 4; ++i)
      if (dyn[i])
      {
        dyn[i]->momx = dyn[i]->momy = dyn[i]->momz = 0;
        if (i == 3)
        {
          dyn[i]->flags &= ~MF_MISSILE;
          P_SetMobjState(dyn[i], mobjinfo[MT_ROCKET].deathstate + 1);
          dyn[i]->tics = 20;
        }
      }
    if (getenv("BGLIGHT_FLASH"))
    {
      int fs = doom_weaponinfo[p->readyweapon].flashstate ? doom_weaponinfo[p->readyweapon].flashstate : S_PISTOLFLASH;

      P_SetPsprite(p, ps_flash, fs);
      p->psprites[ps_flash].tics = 50;
    }
  }
  if (done)
    return;
  done = 1;
  for (i = 0; i < numsectors; ++i)
    sectors[i].lightlevel = getenv("BGLIGHT_BASE") ? atoi(getenv("BGLIGHT_BASE")) : 70;
  if (getenv("BGLIGHT_NOLANTERN"))
    dsda_UpdateIntConfig(dsda_config_begotten_lantern, 0, false);
  if (getenv("BGLIGHT_DITHER"))
    dsda_UpdateIntConfig(dsda_config_begotten_light_dither, atoi(getenv("BGLIGHT_DITHER")), false);
  p->cheats |= CF_GODMODE;
  bg_GoToBigRoom(p->mo);
  if (!getenv("BGLIGHT_ONLY"))
    bg_SpawnShowcase(p);
  if (getenv("BGLIGHT_DYN"))
  {
    static const struct { int type; int fwd, side; } dm[4] = {
      { MT_TROOPSHOT, 170, -130 }, { MT_PLASMA, 200, 0 }, { MT_ROCKET, 170, 130 }, { MT_ROCKET, 260, -40 } };
    int an = p->mo->angle >> ANGLETOFINESHIFT, left = (p->mo->angle + ANG90) >> ANGLETOFINESHIFT;

    for (i = 0; i < 4; ++i)
    {
      dyn[i] = P_SpawnMobj(p->mo->x + dm[i].fwd * finecosine[an] + dm[i].side * finecosine[left],
                           p->mo->y + dm[i].fwd * finesine[an] + dm[i].side * finesine[left], p->mo->z + 30 * FRACUNIT, dm[i].type);
      dyn[i]->flags |= MF_NOCLIP;
    }
  }
  for (i = 0; i < (int) (sizeof put / sizeof put[0]); ++i)
  {
    int type = bg_LightTypeFor(getenv("BGLIGHT_COLORS") ? colours[i].num : put[i].num);
    int put_fwd, put_side;
    int an = p->mo->angle >> ANGLETOFINESHIFT, left = (p->mo->angle + ANG90) >> ANGLETOFINESHIFT;
    mobj_t* m;

    if (type < 0 || (getenv("BGLIGHT_ONLY") && atoi(getenv("BGLIGHT_ONLY")) != i))
      continue;
    if (getenv("BGLIGHT_FWD")) { put_fwd = atoi(getenv("BGLIGHT_FWD")); put_side = atoi(getenv("BGLIGHT_SIDE") ? getenv("BGLIGHT_SIDE") : "0"); }
    else if (getenv("BGLIGHT_COLORS")) { put_fwd = colours[i].fwd; put_side = colours[i].side; }
    else { put_fwd = put[i].fwd; put_side = put[i].side; }
    m = P_SpawnMobj(p->mo->x + put_fwd * finecosine[an] + put_side * finecosine[left],
                    p->mo->y + put_fwd * finesine[an] + put_side * finesine[left], ONFLOORZ, type);
    m->angle = p->mo->angle + put[i].yaw_off;
  }
  if (getenv("BGLIGHT_BAKE"))
  {
    bg_LightsBake();
    lprintf(LO_INFO, "BAKEDEMO: %d baked lights, map has %d sectors, %d lines, %d segs\n", bg_LightBakedCount(), numsectors, numlines, numsegs);
  }
}

void dsda_BegottenArena(void)
{
  static int done;
  player_t* p = &players[0];
  int i, w, a;

  if (done)
    return;
  done = 1;
  for (w = 0; w < NUMWEAPONS; ++w)
    p->weaponowned[w] = true;
  for (a = 0; a < NUMAMMO; ++a)
    p->ammo[a] = p->maxammo[a];
  p->health = p->mo->health = 500;
  p->cheats |= CF_GODMODE;
  p->pendingweapon = wp_tornado;
  bg_GoToBigRoom(p->mo);
  for (i = 0; i < 6; ++i)
  {
    mobj_t* t;
    bg_target_type = i < 3 ? MT_FIREELEM : i < 5 ? MT_WEREWOLF : MT_APPRENTICE;
    t = bg_SpawnTarget(p->mo);
    (void) t;
  }
  bg_target_type = MT_BARREL;
  bg_SpawnShowcase(p);
}

static void bg_SpawnShowcase(player_t* p)
{
  int i;

  {
    // a row of pickups in front of the player: the six weapons, then the nine keys (to see the art and its scale)
    static const int weapons[] = {MT_SHOTGUN, MT_CHAINGUN, MT_MISC27, MT_MISC28, MT_MISC25, MT_SUPERSHOTGUN};
    angle_t an = p->mo->angle >> ANGLETOFINESHIFT;
    angle_t side = (p->mo->angle - ANG90) >> ANGLETOFINESHIFT;

    for (i = 0; i < 15; ++i)
    {
      int type = i < 6 ? weapons[i] : bg_KeyType(i - 6);
      fixed_t lateral = (i - 7) * 36 * FRACUNIT;
      fixed_t dist = 160 * FRACUNIT;

      P_SpawnMobj(p->mo->x + FixedMul(finecosine[an], dist) + FixedMul(finecosine[side], lateral),
                  p->mo->y + FixedMul(finesine[an], dist) + FixedMul(finesine[side], lateral), ONFLOORZ, type);
    }
  }
}


// ---------------------------------------------------------------------------------------------
// Pickup screen flashes (0.10.13): health flashes the screen blue, shields yellow; a 100-point
// pickup is stronger and a little longer, never blinding. Purely visual, so not saved.
// ---------------------------------------------------------------------------------------------
static int flash_kind[MAX_MAXPLAYERS], flash_tics[MAX_MAXPLAYERS], flash_total[MAX_MAXPLAYERS], flash_peak[MAX_MAXPLAYERS];

void bg_PickupFlash(player_t* player, int kind, dboolean big)
{
  int n = (int) (player - players);

  if (n < 0 || n >= MAX_MAXPLAYERS)
    return;
  flash_kind[n] = kind;
  flash_total[n] = big ? 22 : 9;
  flash_peak[n] = big ? 4 : 2;
  flash_tics[n] = flash_total[n];
}

void bg_FlashTick(player_t* player)
{
  int n = (int) (player - players);

  if (n >= 0 && n < MAX_MAXPLAYERS && flash_tics[n] > 0)
    --flash_tics[n];
}

int bg_FlashPalette(const player_t* player)
{
  int n = (int) (player - players), level;

  if (n < 0 || n >= MAX_MAXPLAYERS || flash_tics[n] <= 0)
    return 0;
  level = 1 + (flash_tics[n] * flash_peak[n] - 1) / (flash_total[n] + 1);
  if (level > flash_peak[n]) level = flash_peak[n];
  if (level < 1) level = 1;
  return (flash_kind[n] ? BG_PAL_YELLOW : BG_PAL_BLUE) + level - 1;
}


// ---------------------------------------------------------------------------------------------
// Co-op pickups (0.10.14). In a co-op game every pickup exists for every player: whoever takes it
// sees it turn translucent, the others still see it solid, and it is gone once all have taken it.
//   difficulty 4 (Ultra-Violence): power-ups are one shared pool
//   difficulty 5 (Nightmare):      everything but keys and the weapons the map places is one shared pool
// Keys: difficulty 1-3 a key taken by one is given to all (and removed); 4 and 5 only the one who
// takes it gets it and it never disappears; death keeps the keys (1-4) or wipes them (5).
// ---------------------------------------------------------------------------------------------
dboolean bg_CoopItems(void)
{
  int i, n = 0;

  if (!bg_weapons || deathmatch)
    return false;
  for (i = 0; i < MAX_MAXPLAYERS; ++i)
    n += playeringame[i] ? 1 : 0;
  return n > 1;
}

typedef enum { BG_CLS_OTHER, BG_CLS_POWERUP, BG_CLS_WEAPON, BG_CLS_KEY } bg_item_class_t;

static bg_item_class_t bg_ItemClass(const mobj_t* item)
{
  switch (item->sprite)
  {
    case SPR_MEDI: case SPR_STIM: case SPR_BON1: case SPR_BON2: case SPR_ARM1: case SPR_ARM2: case SPR_SOUL:
    case SPR_MEGA: case SPR_PSTR: case SPR_PINV: case SPR_PINS: case SPR_SUIT: case SPR_PMAP: case SPR_PVIS:
    case SPR_BPAK:
      return BG_CLS_POWERUP;
    case SPR_SHOT: case SPR_SGN2: case SPR_MGUN: case SPR_CSAW: case SPR_LAUN: case SPR_PLAS: case SPR_BFUG:
      return BG_CLS_WEAPON;
    default:
      break;
  }
  return bg_KeyForSprite(item->sprite) >= 0 ? BG_CLS_KEY : BG_CLS_OTHER;
}

dboolean bg_IsKeyItem(const mobj_t* item)
{
  return bg_ItemClass(item) == BG_CLS_KEY;
}

dboolean bg_ItemShared(const mobj_t* item)
{
  int difficulty = gameskill + 1;
  bg_item_class_t cls = bg_ItemClass(item);

  if (cls == BG_CLS_KEY)
    return difficulty <= 3;
  if (difficulty >= 5)
    return !(cls == BG_CLS_WEAPON && !(item->flags & MF_DROPPED));
  if (difficulty == 4)
    return cls == BG_CLS_POWERUP;
  return false;
}

dboolean bg_TakenBy(const mobj_t* item, const player_t* player)
{
  int n = (int) (player - players);

  return n >= 0 && n < 8 && (item->bg_taken & (1 << n));
}

void bg_MarkTaken(mobj_t* item, const player_t* player)
{
  int n = (int) (player - players);

  if (n >= 0 && n < 8)
    item->bg_taken |= 1 << n;
}

dboolean bg_AllTaken(const mobj_t* item)
{
  int i;

  for (i = 0; i < MAX_MAXPLAYERS && i < 8; ++i)
    if (playeringame[i] && !(item->bg_taken & (1 << i)))
      return false;
  return true;
}

void bg_ForgetKeys(int playernum)
{
  thinker_t* th;

  if (!thinkercap.next)
    return;
  for (th = thinkercap.next; th && th != &thinkercap; th = th->next)
    if (th->function == P_MobjThinker)
    {
      mobj_t* m = (mobj_t*) th;

      if ((m->flags & MF_SPECIAL) && bg_IsKeyItem(m))
        m->bg_taken &= ~(1 << playernum);
    }
}

// developer aid: BEGOTTEN_TELEPORT="x y degrees" moves player 1 (to look at a map from a given spot)
void dsda_BegottenDevTeleport(void)
{
  double x, y, a = 0, pitch = 0;
  mobj_t* mo = players[0].mo;

  if (sscanf(getenv("BEGOTTEN_TELEPORT"), "%lf %lf %lf %lf", &x, &y, &a, &pitch) < 2)
    return;
  if (getenv("BEGOTTEN_POWER_INFRARED"))
    players[0].powers[pw_infrared] = atoi(getenv("BEGOTTEN_POWER_INFRARED"));
  if (pitch != 0)
    dsda_UpdateIntConfig(dsda_config_freelook, 1, false);
  mo->pitch = (angle_t) (int) (pitch * (4294967296.0 / 360.0));
  P_UnsetThingPosition(mo);
  mo->x = mo->PrevX = (fixed_t) (x * FRACUNIT);
  mo->y = mo->PrevY = (fixed_t) (y * FRACUNIT);
  P_SetThingPosition(mo);
  mo->floorz = mo->subsector->sector->floorheight;
  mo->ceilingz = mo->subsector->sector->ceilingheight;
  mo->z = mo->PrevZ = mo->floorz;
  mo->angle = (angle_t) (a / 360.0 * 4294967296.0);
  players[0].viewz = mo->z + players[0].viewheight;
}


// BEGOTTEN_SELFTEST=8 (with BEGOTTEN_LOCALPLAYERS=n for a split-screen game): save, play on, load, and check the game is
// what it was when saved, players and split-screen included
void dsda_BegottenSaveTest(void)
{
  static int phase, fails, n_saved, made_saved, split_saved;
  static dboolean in_saved[MAX_MAXPLAYERS];
  static unsigned h0;
  static int load_tic;
  int i, n = 0;

#define SCK(c, msg) do { if (c) lprintf(LO_INFO, "ok:   %s\n", msg); else { lprintf(LO_INFO, "FAIL: %s\n", msg); ++fails; } } while (0)
  for (i = 0; i < MAX_MAXPLAYERS; ++i)
    n += playeringame[i] ? 1 : 0;
  {
    static int cp_slot0 = -2, cp_done;
    if (!cp_done && leveltime == 10)
    {
      cp_slot0 = dsda_LastSaveSlot();
      SCK(bg_Checkpoint(players[0].mo, NULL) == 1, "checkpoint: a player crossing the line counts");
      SCK(bg_Checkpoint(NULL, NULL) == 0, "checkpoint: a thing that is not a player does not");
    }
    if (!cp_done && leveltime == 14)
    {
      cp_done = 1;
      if (n == 1)
        SCK(dsda_LastSaveSlot() >= 0, "checkpoint: alone, crossing it saved the game");
      else
        SCK(dsda_LastSaveSlot() == cp_slot0, "checkpoint: in a co-op game it does not save");
    }
  }
  if (phase == 0 && leveltime >= 40)
  {
    n_saved = n;
    made_saved = bg_made_netgame ? 1 : 0;
    split_saved = bg_SplitActive() ? 1 : 0;
    memcpy(in_saved, playeringame, sizeof in_saved);
    h0 = bg_StateHash();
    G_SaveGame(7, "savetest");
    lprintf(LO_INFO, "SAVETEST: saved at tic %d with %d players (split %d, netgame %d)\n", leveltime, n, split_saved, netgame ? 1 : 0);
    phase = 1;
  }
  else if (phase == 1 && leveltime >= 110)
  {
    load_tic = leveltime;
    G_LoadGame(7, false);
    phase = 2;
  }
  else if (phase == 2 && gameaction == ga_nothing && leveltime < load_tic)
  {
    char w[160];
    unsigned h1 = bg_StateHash();

    snprintf(w, sizeof w, "save: %d players before and %d after loading", n_saved, n);
    SCK(n == n_saved, w);
    SCK(memcmp(in_saved, playeringame, sizeof in_saved) == 0, "save: the same players are in the game");
    SCK((bg_made_netgame ? 1 : 0) == made_saved && (n_saved < 2 || netgame), "save: a split-screen game is still one after loading");
    SCK((bg_SplitActive() ? 1 : 0) == split_saved, "save: the split-screen view is still there");
    snprintf(w, sizeof w, "save: the state is exactly what was saved (%08x, was %08x)", h1, h0);
    SCK(h1 == h0, w);
    phase = 3;
    load_tic = leveltime;
  }
  else if (phase == 3 && leveltime >= load_tic + 70)
  {
    SCK(players[0].mo && players[0].health > 0, "save: the game goes on after loading");
    lprintf(LO_INFO, "SAVETEST: %s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
    exit(fails ? 1 : 0);
  }
#undef SCK
}

static mobj_t* bg_FindRucksack(int owner)
{
  thinker_t* th;

  for (th = thinkercap.next; th && th != &thinkercap; th = th->next)
    if (th->function == P_MobjThinker && ((mobj_t*) th)->bg_owner == owner + 1)
      return (mobj_t*) th;
  return NULL;
}

// BEGOTTEN_SELFTEST=9 with BEGOTTEN_LOCALPLAYERS=2: the co-op rules, played through (run with -skill 4 and -skill 2)
void dsda_BegottenCoopTest(void)
{
  static int phase, fails, tickets0, clip_before, pack_clip, load_tic;
  static int ammo_after_respawn;
  int hard = gameskill + 1 >= 4;
  mobj_t* pack;
  char w[128];

#define CCK(c, msg) do { if (c) lprintf(LO_INFO, "ok:   %s\n", msg); else { lprintf(LO_INFO, "FAIL: %s\n", msg); ++fails; } } while (0)
  if (!playeringame[1] || !players[1].mo)
    return;
  if (phase == 0 && leveltime == 10)
  {
    tickets0 = bg_Tickets();
    snprintf(w, sizeof w, "coop: the level start gave one ticket each (%d)", tickets0);
    CCK(hard ? tickets0 == 2 : 1, w);
    CCK(bg_TicketDisplay() == (hard ? tickets0 : -1), "coop: the HUD shows tickets only on difficulty 4-5");
    players[1].weaponowned[wp_shotgun] = true;
    players[1].ammo[am_clip] = 77;
    clip_before = 77;
    P_DamageMobj(players[1].mo, NULL, NULL, 100000);
    phase = 1;
  }
  else if (phase == 1 && leveltime >= 13)
  {
    pack = bg_FindRucksack(1);
    CCK(players[1].playerstate == PST_DEAD, "coop: the player died");
    CCK(hard ? pack != NULL : pack == NULL, hard ? "coop: a rucksack was left" : "coop: no rucksack on difficulty 1-3");
    pack_clip = pack ? pack->bg_pack[am_clip] : 0;
    CCK(!hard || pack_clip == 77, "coop: the rucksack holds the ammo");
    CCK(bg_Tickets() == tickets0, "coop: dying costs nothing, respawning does");
    dsda_DeathUse(&players[1]);
    phase = 2;
  }
  else if (phase == 2 && leveltime >= 16)
  {
    CCK(players[1].playerstate == PST_LIVE && players[1].mo && players[1].mo->health > 0, "coop: the player is back");
    CCK(bg_Tickets() == (hard ? tickets0 - 1 : tickets0), hard ? "coop: a ticket was spent" : "coop: no ticket spent on difficulty 1-3");
    CCK(players[1].weaponowned[wp_shotgun], "coop: the weapons were kept");
    ammo_after_respawn = players[1].ammo[am_clip];
    if (hard)
    {
      snprintf(w, sizeof w, "coop: base ammo only (%d, had %d)", ammo_after_respawn, clip_before);
      CCK(ammo_after_respawn < clip_before, w);
    }
    else
      CCK(ammo_after_respawn == clip_before, "coop: the ammo was kept on difficulty 1-3");
    if (hard)
    {
      pack = bg_FindRucksack(1);
      if (pack)
      {
        if (gameskill + 1 == 5)
        {
          int before = players[0].ammo[am_clip];
          P_TouchSpecialThing(pack, players[0].mo);
          CCK(players[0].ammo[am_clip] > before || players[0].ammo[am_clip] == players[0].maxammo[am_clip], "coop: difficulty 5: anyone can take it");
          bg_TicketsAdd(5000);
          phase = 3;
          return;
        }
        P_TouchSpecialThing(pack, players[0].mo);
        CCK(bg_FindRucksack(1) != NULL, "coop: difficulty 4: only its owner can take it");
        {
          int room_ok = players[1].maxammo[am_clip] - players[1].ammo[am_clip] >= pack_clip;
          P_TouchSpecialThing(pack, players[1].mo);
          snprintf(w, sizeof w, "coop: the owner took the ammo back (%d -> %d)", ammo_after_respawn, players[1].ammo[am_clip]);
          CCK(players[1].ammo[am_clip] > ammo_after_respawn, w);
          CCK(!room_ok || bg_FindRucksack(1) == NULL, "coop: an emptied rucksack is gone");
        }
      }
    }
    bg_TicketsAdd(5000);
    phase = 3;
  }
  else if (phase == 3)
  {
    CCK(bg_Tickets() == 999, "coop: tickets are capped at 999");
    bg_TicketsSet(0);
    {
      int t0 = bg_Tickets();
      bg_CoopCheckpoint(NULL);
      CCK(bg_Tickets() == t0 + (hard ? 2 : 0), hard ? "coop: a checkpoint adds one ticket per player" : "coop: a checkpoint does nothing on difficulty 1-3");
    }
    bg_TicketsSet(0);
    if (hard)
    {
      P_DamageMobj(players[1].mo, NULL, NULL, 100000);
      load_tic = leveltime;
      phase = 7;
    }
    else
      phase = 6;
  }
  else if (phase == 7 && leveltime == load_tic + 2)
  {
    dsda_DeathUse(&players[1]);
  }
  else if (phase == 7 && leveltime >= load_tic + 5)
  {
    CCK(players[1].playerstate == PST_DEAD && bg_Tickets() == 0, "coop: no tickets, no respawn");
    P_DamageMobj(players[0].mo, NULL, NULL, 100000);
    load_tic = leveltime;
    phase = 4;
  }
  else if (phase == 4 && leveltime < load_tic)
  {
    CCK(players[0].playerstate == PST_LIVE && players[1].playerstate == PST_LIVE, "coop: everyone dead and no tickets: the level started over");
    CCK(!players[1].weaponowned[wp_shotgun], "coop: ... and everything was lost");
    CCK(bg_Tickets() == 2, "coop: ... with the new level's tickets");
    phase = 6;
  }
  else if (phase == 4 && leveltime > load_tic + 100)
  {
    CCK(0, "coop: the level never restarted");
    phase = 6;
  }
  if (phase == 6)
  {
    lprintf(LO_INFO, "COOPTEST: %s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
    exit(fails ? 1 : 0);
  }
#undef CCK
}

// BEGOTTEN_SELFTEST=S: spectating, in a network game of three players on two machines (the host has slot 0, the
// joiner has slots 1 and 2: BEGOTTEN_NETTEST=host:3 / join:<addr> and BEGOTTEN_NETTEST_LOCAL=2 on the joiner)
void dsda_BegottenSpectateTest(void)
{
  static int phase, fails, hold_until;
  int slots[4], n = bg_SpectateList(slots);
  int host = bgnet_LocalFirstSlot() == 0;     // this machine owns slot 0 only
  char w[128];

#define PCK(c, msg) do { if (c) lprintf(LO_INFO, "ok:   %s\n", msg); else { lprintf(LO_INFO, "FAIL: %s\n", msg); ++fails; } } while (0)
  if (!bgnet_InGame() || !playeringame[2] || !players[2].mo)
    return;
  if (hold_until && leveltime < hold_until)
    return;
  if (phase == 0 && leveltime == 40)
  {
    P_DamageMobj(players[0].mo, NULL, NULL, 100000);
    phase = 1;
  }
  else if (phase == 1 && leveltime == 120)
  {
    PCK(players[0].playerstate == PST_DEAD, "spectate: the host's player died");
    PCK(n == 0, "spectate: the death screen comes first (nothing watched after 2 s)");
    phase = 2;
  }
  else if (phase == 2 && leveltime == 40 + BG_DEATH_SCREEN_TICS + 5)
  {
    snprintf(w, sizeof w, "spectate: after 5 s the dead machine watches the two living players (%d)", n);
    PCK(host ? (n == 2 && slots[0] == 1 && slots[1] == 2 && bg_ViewCount() == 2 && displayplayer == 1) : n == 0, w);
    PCK(bg_ViewCount() == 2 && bg_ViewSlot(1) == 2, "spectate: two views, one per living player (the joiner's own two split views)");
    if (getenv("BEGOTTEN_SPECTEST_HOLD"))
    {
      lprintf(LO_INFO, "SPECTEST: holding for a screenshot (2 views)\n");
      hold_until = leveltime + 350;
    }
    phase = 3;
  }
  else if (phase == 3)
  {
    P_DamageMobj(players[1].mo, NULL, NULL, 100000);
    phase = 4;
    hold_until = leveltime + 4;
  }
  else if (phase == 4)
  {
    snprintf(w, sizeof w, "spectate: one player dies: the views shrink to one (list %d first %d, views %d, tic %d, p1 %d)", n, slots[0], bg_ViewCount(), leveltime, players[1].playerstate);
    PCK(host ? (n == 1 && slots[0] == 2 && bg_ViewCount() == 1) : n == 0, w);
    if (getenv("BEGOTTEN_SPECTEST_HOLD"))
    {
      lprintf(LO_INFO, "SPECTEST: holding for a screenshot (1 view)\n");
      hold_until = leveltime + 350;
    }
    phase = 5;
  }
  else if (phase == 5)
  {
    P_DamageMobj(players[2].mo, NULL, NULL, 100000);
    phase = 6;
    hold_until = leveltime + 4;
  }
  else if (phase == 6)
  {
    PCK(n == 0 || !host, "spectate: nobody left to watch: back to the cadaver view");
    dsda_DeathUse(&players[0]);
    phase = 7;
    hold_until = leveltime + 4;
  }
  else if (phase == 7)
  {
    snprintf(w, sizeof w, "spectate: Use respawns the player and ends it (state %d, tickets %d)", players[0].playerstate, bg_Tickets());
    PCK(players[0].playerstate == PST_LIVE && bg_SpectateList(slots) == 0, w);
    lprintf(LO_INFO, "SPECTEST: %s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
    exit(fails ? 1 : 0);
  }
#undef PCK
}

void dsda_BegottenDevUse(void)
{
  int at = atoi(getenv("BEGOTTEN_USE"));

  if (leveltime == at)
    P_UseLines(&players[0]);
  if (getenv("BEGOTTEN_USE_LOG") && (leveltime - at) % 35 == 0)
  {
    int i;

    for (i = 0; i < numsectors; ++i)
      if (sectors[i].tag == atoi(getenv("BEGOTTEN_USE_LOG")))
        lprintf(LO_INFO, "use dbg: tic %d sector %d (tag %d) floor %d ceiling %d\n", leveltime, i, sectors[i].tag,
                sectors[i].floorheight >> 16, sectors[i].ceilingheight >> 16);
  }
}
