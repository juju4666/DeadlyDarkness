//
// Begotten: network co-op (lockstep over UDP).  See bgnet.h for the overview.
//
// Tic numbering: when a network game starts every machine sets gametic to 0 and runs tic 0 with
// empty commands for everybody.  From tic 1 on, a tic runs when the commands of all players for
// it are known.  A player's commands are written into local_cmds[player][tic % BACKUPTICS], which
// is also where G_Ticker reads them.
//
// Packets (UDP, little endian):  'B' 'G' version type ...
//   HELLO   client -> host   hash, class                     (repeated until the lobby answers)
//   LOBBY   host -> client   your slot, 4 x (present, class), friendly fire
//   REJECT  host -> client   reason
//   START   host -> client   slot, skill, episode, map, ff, complevel, 4 x (present, class), options
//   STARTACK client -> host
//   TICS    both ways        sender, 4 x ack, hash tic, hash, 4 x drop tic, runs of commands
//   BYE     both ways
//
// A client that leaves (or goes silent for 10 s) is removed from the game at the tic after its last
// command that reached the host; the host announces that tic in every TICS packet, so everyone removes
// the player at the same tic.  If the host goes, the game ends for everybody.
//
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET bg_sock_t;
#define BG_BADSOCK INVALID_SOCKET
#define bg_closesock closesocket
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
typedef int bg_sock_t;
#define BG_BADSOCK (-1)
#define bg_closesock close
#endif

#include "SDL.h"

#include "doomstat.h"
#include "r_fps.h"
#include "d_player.h"
#include "d_main.h"
#include "d_ticcmd.h"
#include "g_game.h"
#include "m_menu.h"
#include "m_random.h"
#include "p_mobj.h"
#include "p_maputl.h"
#include "p_tick.h"
#include "r_state.h"
#include "r_main.h"
#include "p_pspr.h"
#include "lprintf.h"
#include "i_system.h"
#include "sounds.h"
#include "w_wad.h"

#include "dsda/bgclass.h"
#include "dsda/begotten.h"
#include "dsda/bgcoop.h"
#include "dsda/key_frame.h"
#include "z_zone.h"
#include <zlib.h>
#include "dsda/bgprogress.h"
#include "doomdef.h"
#include "v_video.h"
#include "dsda/bgnet.h"
#include "dsda/configuration.h"
#include "dsda/episode.h"
#include "dsda/options.h"

extern int force_singletics_to;
extern dboolean bg_made_netgame;
extern int solo_net;
extern void bg_ShowMessage(const char* text);
extern void bg_WriteText(int x, int y, const char* string, int cm);
extern void doom_printf(const char* msg, ...);

#define PKT_VERSION 6

// Raise this whenever a change alters the simulation (movement, damage, weapons...). Machines with
// different numbers cannot play together: the number is part of the content check at joining.
#define BGNET_SIM_REV 12
#define PKT_HELLO 1
#define PKT_LOBBY 2
#define PKT_REJECT 3
#define PKT_START 4
#define PKT_STARTACK 5
#define PKT_TICS 6
#define PKT_BYE 7
#define PKT_DIAG 8
#define PKT_LATE 9       // host -> joiner: the game's settings, and the size of the snapshot once it exists
#define PKT_SNAP 10      // host -> joiner: one piece of the snapshot
#define PKT_SNAPACK 11   // joiner -> host: how much of it has arrived
#define PKT_JOINREQ 12   // observer -> host: let me play, as this character

#define NONE32 0xFFFFFFFFu
#define MAXPKT 1200
#define SPAN (BACKUPTICS - 2)       // most tics in flight between two machines
#define HASH_INTERVAL 35
#define LOBBY_TIMEOUT 6000
#define GAME_TIMEOUT 6000

typedef struct
{
  dboolean used;
  struct sockaddr_in addr;
  unsigned last_recv, last_send;
  int cls;
  byte progress[64];               // finished maps, one bit per map in UMAPINFO order
  unsigned ack[BGNET_MAX_PLAYERS]; // what this peer still needs from each player (next tic)
  dboolean start_acked;
  dboolean dropped;
  dboolean late;                   // host: joined a running game and is still receiving the snapshot
  int nlocal;                      // players on that machine: consecutive slots starting at its own
  int lcls[BGNET_MAX_PLAYERS];     // their classes
  unsigned last_match;             // host only: latest tic at which this peer's state equalled ours
  unsigned bad_tic;                // host only: first tic at which it did not (0: none)
} peer_t;

typedef struct
{
  byte d[MAXPKT];
  int len, pos;
  dboolean bad;
} pkt_t;

static bg_sock_t sock = BG_BADSOCK;
static bgnet_state_t state = BGNET_IDLE;
static peer_t peers[BGNET_MAX_PLAYERS];
static int my_slot;
static int my_nlocal = 1;          // players on this machine: slots my_slot .. my_slot + my_nlocal - 1
static int local_cls[BGNET_MAX_PLAYERS];  // the classes chosen for them
#define IsMine(p) ((p) >= my_slot && (p) < my_slot + my_nlocal)
static int port = BGNET_DEFAULT_PORT;
static char errtext[96];
static int my_class;
static dboolean ff_setting;
static int lobby_level = -1, lobby_skill, lobby_done;   // host: chosen map (map list index), difficulty; per-slot 'finished it' bits
static dboolean host_start_pending;
static unsigned content_hash;

// the game being started
static struct
{
  int skill, episode, map;
  int present[BGNET_MAX_PLAYERS];
  int cls[BGNET_MAX_PLAYERS];
  int ff;
  int complevel;
  int optsize;
  byte opts[80];
  byte sim[16];   // local settings that change the simulation (weapon switching, corpses...)
} game;

static unsigned next_tic[BGNET_MAX_PLAYERS];  // commands known, per player (tics 0..next-1)
static unsigned drop_at[BGNET_MAX_PLAYERS];   // NONE32, or the first tic the player is gone

// Joining a game in progress. Three events, each at a tic that every machine reaches in the same state:
//  attach: the "barrier". Everybody stops at tic att_at[s] (the host's command counter when it accepted the new
//          machine); the host takes a snapshot of the game there and sends it; the new machine puts it in and
//          starts as an observer; the host lifts the barrier (att_rel[s] = att_at[s]).
//  spawn:  the observer chose a character: at tic spawn_at[s] its player enters the game (an ordinary reborn).
//  drop:   (existing) the player is gone from the tic drop_at[s].
// The host announces them in every TICS packet; the others adopt a value only if it is newer than theirs.
static unsigned att_at[BGNET_MAX_PLAYERS], att_rel[BGNET_MAX_PLAYERS], spawn_at[BGNET_MAX_PLAYERS];
static int spawn_cls[BGNET_MAX_PLAYERS];
static dboolean att_applied[BGNET_MAX_PLAYERS];   // the slot has been reset for its new machine (every machine does this at the barrier)
#define SNAPCHUNK 1000
#define LATE_TIMEOUT_MS 40000u

static struct              // host: the machine being attached
{
  int slot;                // -1: nobody
  unsigned tic;            // the barrier
  byte* comp;              // the snapshot, compressed (NULL until the barrier is reached)
  int comp_len, raw_len, nchunks, acked;
  unsigned started, last_send, last_info;
} late = { -1 };

static struct              // a machine that joined a running game
{
  dboolean late_joiner;
  unsigned tic;            // the tic of the snapshot
  byte *comp, *got, *raw;
  int comp_len, raw_len, nchunks, have, ngot;
  unsigned last_ack, last_req;
  int cls;                 // the character Use will join as
  dboolean want;           // Use was pressed
  int last_side;
  unsigned attach_tic, joined_tic;
} lj;
static int NetTrace(void)
{
  static int on = -1;

  if (on < 0)
    on = getenv("BEGOTTEN_NETTEST_TRACE") != NULL;
  return on;
}
static unsigned test_joinat;     // automated test: an observer does not ask to join before this tic
int bgnet_tracing;
static int test_maxspeed;      // automated test: the fastest the player moved in one tic (map units)
static dboolean dirty;
static int last_make_seen;       // maketic when we last looked: new commands are sent at once
static unsigned last_hello, last_lobby, last_start;
static unsigned stall_since;                   // when the game began waiting for somebody
static int stall_player = -1;

// lobby view of a client
static int lobby_present[BGNET_MAX_PLAYERS];
static int lobby_class[BGNET_MAX_PLAYERS];
static struct sockaddr_in host_addr;

// desync check
#define SNAPN 20
typedef struct { unsigned v[SNAPN]; } snap_t;   // v[0] is the hash of everything, the rest say what differs
typedef struct { unsigned tic; snap_t snap; dboolean used; } hashrec_t;
static hashrec_t hashes[16];
static unsigned my_hash_tic = NONE32;
static snap_t my_snap;
static const char* const snap_names[SNAPN] = {
  "everything", "level time", "random numbers", "kill count", "number of things", "positions of things",
  "sector heights", "player health/ammo/weapons", "player 1 x", "player 2 x", "player 3 x", "player 4 x",
  "player 1 y", "player 2 y", "player 3 y", "player 4 y", "player 1 health", "player 2 health",
  "player 3 health", "player 4 health" };
static int desyncs;
static unsigned desync_tic;
static char desync_what[3][64];   // what differed: [0] first line, [1] the values

static void SetError(const char* fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vsnprintf(errtext, sizeof errtext, fmt, ap);
  va_end(ap);
  lprintf(LO_INFO, "bgnet: %s\n", errtext);
}

static unsigned NowMs(void)
{
  return SDL_GetTicks();
}

static unsigned long long Usec(void)
{
  // split the division: counter * 1000000 overflows 64 bits after a few hours of uptime
  unsigned long long c = SDL_GetPerformanceCounter(), f = SDL_GetPerformanceFrequency();

  return (c / f) * 1000000ull + (c % f) * 1000000ull / f;
}

#define TIC_US (1000000ull / 35)
static int stat_stalls, stat_true_stalls, stat_stall_lead, target_lead = 2;
static unsigned long long last_interval_us = 1000000ull / 35;
static unsigned long long next_due_us;   // when the next tic is due

//
// packets
//

static void PutInit(pkt_t* p, int type)
{
  memset(p, 0, sizeof *p);
  p->d[0] = 'B'; p->d[1] = 'G'; p->d[2] = PKT_VERSION; p->d[3] = (byte) type;
  p->len = 4;
}
static void Put8(pkt_t* p, unsigned v)
{
  if (p->len + 1 > MAXPKT) { p->bad = true; return; }
  p->d[p->len++] = (byte) v;
}
static void Put16(pkt_t* p, unsigned v) { Put8(p, v & 255); Put8(p, (v >> 8) & 255); }
static void Put32(pkt_t* p, unsigned v) { Put16(p, v & 0xffff); Put16(p, v >> 16); }

static int Get8(pkt_t* p)
{
  if (p->pos + 1 > p->len) { p->bad = true; return 0; }
  return p->d[p->pos++];
}
static int Get16(pkt_t* p) { int a = Get8(p); int b = Get8(p); return a | (b << 8); }
static unsigned Get32(pkt_t* p) { unsigned a = (unsigned) Get16(p); unsigned b = (unsigned) Get16(p); return a | (b << 16); }

#define NAMELEN 11
#define SERVERNAMELEN 20
static char server_name[SERVERNAMELEN + 1];          // the host's name for the game, as the lobby told us
static char names[BGNET_MAX_PLAYERS][NAMELEN + 1];   // player names as seen in the lobby / game

static void PutName(pkt_t* p, const char* s)
{
  int n = (int) strlen(s), i;

  if (n > NAMELEN - 1) n = NAMELEN - 1;
  Put8(p, n);
  for (i = 0; i < n; ++i)
    Put8(p, (byte) s[i]);
}

// names from the network are only ever printable ASCII, at most 10 characters
static void GetName(pkt_t* p, char* out)
{
  int n = Get8(p), i, k = 0;

  for (i = 0; i < n; ++i)
  {
    int c = Get8(p);

    if (c >= 32 && c < 127 && k < NAMELEN - 1)
      out[k++] = (char) c;
  }
  while (k > 0 && out[k - 1] == ' ')
    --k;
  out[k] = 0;
  if (n > 32)
    p->bad = true;
}

static void MyName(char* out)
{
  const char* n = dsda_StringConfig(dsda_config_begotten_player_name);

  snprintf(out, NAMELEN + 1, "%.10s", n ? n : "");
}

// the name of this machine's local player k (0 = the first); may be empty
static void LocalName(int k, char* out)
{
  const char* n = NULL;

  if (k == 0)
    n = dsda_StringConfig(dsda_config_begotten_player_name);
  else if (k >= 1 && k <= 3)
    n = dsda_StringConfig(dsda_config_begotten_player_name_p2 + k - 1);
  snprintf(out, NAMELEN + 1, "%.10s", n ? n : "");
}

const char* bgnet_PlayerName(int slot)
{
  static char fallback[BGNET_MAX_PLAYERS][16];

  if (slot < 0 || slot >= BGNET_MAX_PLAYERS)
    return "";
  if (names[slot][0])
    return names[slot];
  snprintf(fallback[slot], sizeof fallback[slot], "PLAYER %d", slot + 1);
  return fallback[slot];
}

static void PutCmd(pkt_t* p, const ticcmd_t* c)
{
  Put8(p, (byte) c->forwardmove);
  Put8(p, (byte) c->sidemove);
  Put16(p, (unsigned short) c->angleturn);
  Put8(p, c->buttons);
  Put8(p, c->lookfly);
  Put8(p, c->arti);
  Put8(p, c->ex.actions);
  Put8(p, c->ex.save_slot);
  Put8(p, c->ex.load_slot);
  Put16(p, (unsigned short) c->ex.look);
}

static void GetCmd(pkt_t* p, ticcmd_t* c)
{
  memset(c, 0, sizeof *c);
  c->forwardmove = (signed char) Get8(p);
  c->sidemove = (signed char) Get8(p);
  c->angleturn = (short) Get16(p);
  c->buttons = (byte) Get8(p);
  c->lookfly = (byte) Get8(p);
  c->arti = (byte) Get8(p);
  c->ex.actions = (byte) Get8(p);
  c->ex.save_slot = (byte) Get8(p);
  c->ex.load_slot = (byte) Get8(p);
  c->ex.look = (short) Get16(p);
  c->ex.save_slot = 0;   // nobody saves or loads another player's game
  c->ex.load_slot = 0;
}

static dboolean SameAddr(const struct sockaddr_in* a, const struct sockaddr_in* b)
{
  return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port;
}

static void SendTo(const struct sockaddr_in* to, pkt_t* p)
{
  if (sock == BG_BADSOCK || p->bad)
    return;
  sendto(sock, (const char*) p->d, p->len, 0, (const struct sockaddr*) to, sizeof *to);
}

static void SendBye(const struct sockaddr_in* to, int reason)
{
  pkt_t p;
  int i;

  PutInit(&p, PKT_BYE);
  Put8(&p, reason);          // 0: leaving, 1: removed because out of sync
  for (i = 0; i < 3; ++i)
    SendTo(to, &p);
}

static void SendReject(const struct sockaddr_in* to, int reason)
{
  pkt_t p;

  PutInit(&p, PKT_REJECT);
  Put8(&p, reason);
  SendTo(to, &p);
}

//
// small helpers
//

bgnet_state_t bgnet_State(void) { return state; }
dboolean bgnet_InGame(void) { return state == BGNET_STARTING || state == BGNET_ACTIVE || state == BGNET_LATE; }
dboolean bgnet_Lockstep(void) { return state == BGNET_ACTIVE || state == BGNET_LATE; }
dboolean bgnet_IsHost(void) { return (state == BGNET_HOSTING) || (bgnet_InGame() && my_slot == 0); }
dboolean bgnet_FriendlyFire(void) { return game.ff != 0; }
const char* bgnet_Error(void) { return errtext; }
int bgnet_MySlot(void) { return my_slot; }
dboolean bgnet_HostStartPending(void) { return host_start_pending; }
const char* bgnet_ServerName(void) { return server_name; }
dboolean bgnet_LobbyFriendlyFire(void) { return ff_setting; }

// host: what the game setup screen has selected right now (sent to the players waiting in the lobby)
void bgnet_SetLobbyInfo(int level_index, int skill)
{
  lobby_level = level_index;
  lobby_skill = skill;
}

int bgnet_LobbyLevel(void) { return lobby_level; }
int bgnet_LobbySkill(void) { return lobby_skill; }
dboolean bgnet_LobbyDone(int slot) { return slot >= 0 && slot < BGNET_MAX_PLAYERS && ((lobby_done >> slot) & 1); }
void bgnet_SetHostStartPending(dboolean on) { host_start_pending = on; }

void bgnet_SetMyClass(int cls)
{
  my_class = cls;
  local_cls[0] = cls;
}

void bgnet_SetLocalClass(int index, int cls)
{
  if (index >= 0 && index < BGNET_MAX_PLAYERS)
    local_cls[index] = cls;
  if (index == 0)
    my_class = cls;
}

int bgnet_LocalCount(void) { return bgnet_InGame() ? my_nlocal : 1; }
int bgnet_LocalFirstSlot(void) { return bgnet_InGame() ? my_slot : 0; }
dboolean bgnet_IsMineSlot(int slot) { return state != BGNET_IDLE && my_slot >= 0 && IsMine(slot); }

// which machine (its first slot) plays slot s; the host is 0
static int OwnerOf(int s)
{
  int q;

  if (my_slot != 0)
    return IsMine(s) ? my_slot : 0;
  if (s < my_nlocal)
    return 0;
  for (q = 1; q < BGNET_MAX_PLAYERS; ++q)
    if (peers[q].used && s >= q && s < q + peers[q].nlocal)
      return q;
  return -1;
}

// host: who plays slot s, which of that machine's players it is, and its class
static dboolean HostSlot(int s, int* owner, int* index, int* cls)
{
  int o = OwnerOf(s);

  if (o < 0)
    return false;
  if (owner) *owner = o;
  if (index) *index = s - o;
  if (cls)
    *cls = o == 0 ? local_cls[s] : peers[o].lcls[s - o];
  return true;
}

// host: the names of the host's own local players (the others' names came with their HELLO)
static void FillSubNames(void)
{
  int k;

  for (k = 0; k < my_nlocal && k < BGNET_MAX_PLAYERS; ++k)
    LocalName(k, names[k]);
}

static unsigned ContentHash(void)
{
  unsigned h = 2166136261u;
  int i;

  if (content_hash)
    return content_hash;
  for (i = 0; i < numlumps; ++i)
  {
    const char* n = lumpinfo[i].name;
    int k;

    for (k = 0; k < 8 && n[k]; ++k)
      h = (h ^ (byte) n[k]) * 16777619u;
    h = (h ^ (unsigned) lumpinfo[i].size) * 16777619u;
    if (lumpinfo[i].size > 0 && lumpinfo[i].size <= 262144)   // maps, scripts, definitions: the contents too
    {
      static byte buf[262144];
      int sz = lumpinfo[i].size, j;

      W_ReadLump(i, buf);
      for (j = 0; j < sz; ++j)
        h = (h ^ buf[j]) * 16777619u;
    }
  }
  h = (h ^ (unsigned) BGNET_SIM_REV) * 16777619u;
  content_hash = h ? h : 1;
  lprintf(LO_INFO, "bgnet: build %s, simulation revision %d, content hash %08x (%d lumps)\n", PROJECT_VERSION, BGNET_SIM_REV, content_hash, numlumps);
  return content_hash;
}

static void CloseSocket(void)
{
  if (sock != BG_BADSOCK)
    bg_closesock(sock);
  sock = BG_BADSOCK;
}

// undo what a network game did to the engine's global state

//
// Local settings that change the simulation. They are the host's on every machine during a game
// (otherwise e.g. two different "switch weapon on pickup" settings would drift apart).
//
extern int weapon_preferences[2][NUM_PREF_WEAPONS + 1];
static dboolean sim_saved;
static int sim_orig_switch, sim_orig_corpses, sim_orig_pref[NUM_PREF_WEAPONS + 1];
static dboolean sim_orig_pistol, sim_orig_coop;

static void SimConfigGather(byte* out)
{
  int i;

  memset(out, 0, 16);
  out[0] = (byte) dsda_IntConfig(dsda_config_switch_weapon_on_pickup);
  i = dsda_IntConfig(dsda_config_max_player_corpse);
  out[1] = (byte) (i < 0 ? 255 : i > 254 ? 254 : i);
  out[2] = pistolstart ? 1 : 0;
  out[3] = coop_spawns ? 1 : 0;
  for (i = 0; i <= NUM_PREF_WEAPONS && i < 12; ++i)
    out[4 + i] = (byte) weapon_preferences[0][i];
}

static void SimConfigApply(const byte* in)
{
  int i;

  if (!sim_saved)
  {
    sim_saved = true;
    sim_orig_switch = dsda_IntConfig(dsda_config_switch_weapon_on_pickup);
    sim_orig_corpses = dsda_IntConfig(dsda_config_max_player_corpse);
    sim_orig_pistol = pistolstart;
    sim_orig_coop = coop_spawns;
    for (i = 0; i <= NUM_PREF_WEAPONS; ++i)
      sim_orig_pref[i] = weapon_preferences[0][i];
  }
  dsda_UpdateIntConfig(dsda_config_switch_weapon_on_pickup, in[0], false);
  dsda_UpdateIntConfig(dsda_config_max_player_corpse, in[1] == 255 ? -1 : in[1], false);
  pistolstart = in[2] != 0;
  coop_spawns = in[3] != 0;
  for (i = 0; i <= NUM_PREF_WEAPONS && i < 12; ++i)
    weapon_preferences[0][i] = in[4 + i];
}

static void SimConfigRestore(void)
{
  int i;

  if (!sim_saved)
    return;
  sim_saved = false;
  dsda_UpdateIntConfig(dsda_config_switch_weapon_on_pickup, sim_orig_switch, false);
  dsda_UpdateIntConfig(dsda_config_max_player_corpse, sim_orig_corpses, false);
  pistolstart = sim_orig_pistol;
  coop_spawns = sim_orig_coop;
  for (i = 0; i <= NUM_PREF_WEAPONS; ++i)
    weapon_preferences[0][i] = sim_orig_pref[i];
}

static void LateReset(void)
{
  memset(att_at, 0xff, sizeof att_at);
  memset(att_rel, 0xff, sizeof att_rel);
  memset(spawn_at, 0xff, sizeof spawn_at);
  memset(spawn_cls, 0, sizeof spawn_cls);
  memset(att_applied, 0, sizeof att_applied);
}

static void LateFree(void)
{
  free(late.comp);
  free(lj.comp);
  free(lj.got);
  free(lj.raw);
  memset(&lj, 0, sizeof lj);
  memset(&late, 0, sizeof late);
  late.slot = -1;
}

static void Cleanup(void)
{
  int i;
  dboolean was_game = bgnet_InGame();

  LateFree();
  LateReset();

  bgnet_tracing = 0;
  CloseSocket();
  state = BGNET_IDLE;
  host_start_pending = false;
  memset(peers, 0, sizeof peers);
  if (was_game)
  {
    SimConfigRestore();
    netgame = solo_net;
    for (i = 1; i < MAX_MAXPLAYERS; ++i)
      playeringame[i] = false;
    playeringame[0] = true;
    consoleplayer = displayplayer = 0;
    bg_made_netgame = false;
  }
}

void bgnet_Leave(void)
{
  int i;

  if (state == BGNET_IDLE)
    return;
  if (my_slot == 0 && state != BGNET_JOINING && state != BGNET_LOBBY)
  {
    for (i = 1; i < BGNET_MAX_PLAYERS; ++i)
      if (peers[i].used)
        SendBye(&peers[i].addr, 0);
  }
  else if (state == BGNET_JOINING || state == BGNET_LOBBY || state == BGNET_STARTING || state == BGNET_ACTIVE ||
           state == BGNET_SYNC || state == BGNET_LATE)
    SendBye(&host_addr, 0);
  Cleanup();
}

static void EndSession(const char* why)
{
  dboolean was_game = state == BGNET_ACTIVE || state == BGNET_STARTING || state == BGNET_LATE;

  SetError("%s", why);
  Cleanup();
  if (was_game)
  {
    D_StartTitle();
    bg_ShowMessage(why);
  }
}

//
// sockets
//

static dboolean OpenSocket(int bind_port)
{
  struct sockaddr_in a;
  int one = 1;

#ifdef _WIN32
  static dboolean wsa_started;
  WSADATA wsa;

  if (!wsa_started)
  {
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
      SetError("network unavailable");
      return false;
    }
    wsa_started = true;
  }
#endif

  sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock == BG_BADSOCK)
  {
    SetError("cannot create a socket");
    return false;
  }
#ifdef _WIN32
  {
    u_long nb = 1;
    ioctlsocket(sock, FIONBIO, &nb);
  }
#else
  fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
#endif
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*) &one, sizeof one);

  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  a.sin_port = htons((unsigned short) bind_port);
  if (bind(sock, (struct sockaddr*) &a, sizeof a) != 0)
  {
    SetError("port %d is in use", bind_port);
    CloseSocket();
    return false;
  }
  return true;
}

static int ConfigPort(void)
{
  int p = dsda_IntConfig(dsda_config_begotten_net_port);

  return p >= 1024 && p <= 65535 ? p : BGNET_DEFAULT_PORT;
}

dboolean bgnet_Host(int nlocal)
{
  if (state != BGNET_IDLE)
    bgnet_Leave();
  errtext[0] = 0;
  port = ConfigPort();
  if (!OpenSocket(port))
    return false;
  memset(peers, 0, sizeof peers);
  memset(&game, 0, sizeof game);
  LateFree();
  LateReset();
  my_slot = 0;
  my_nlocal = nlocal < 1 ? 1 : nlocal > BGNET_MAX_PLAYERS ? BGNET_MAX_PLAYERS : nlocal;
  state = BGNET_HOSTING;
  host_start_pending = false;
  return true;
}

dboolean bgnet_Join(const char* address, int nlocal)
{
  char host[128];
  char service[16];
  const char* colon;
  struct addrinfo hints, *res = NULL;
  int rc;

  if (state != BGNET_IDLE)
    bgnet_Leave();
  errtext[0] = 0;
  if (!address || !address[0])
  {
    SetError("type the host's address first");
    return false;
  }

  colon = strrchr(address, ':');
  if (colon && colon != address && strchr(address, ':') == colon)
  {
    size_t n = (size_t) (colon - address);

    if (n >= sizeof host) n = sizeof host - 1;
    memcpy(host, address, n);
    host[n] = 0;
    snprintf(service, sizeof service, "%d", atoi(colon + 1));
  }
  else
  {
    snprintf(host, sizeof host, "%s", address);
    snprintf(service, sizeof service, "%d", ConfigPort());
  }

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
#ifdef _WIN32
  {
    static dboolean wsa_started;
    WSADATA wsa;

    if (!wsa_started && WSAStartup(MAKEWORD(2, 2), &wsa) == 0)
      wsa_started = true;
  }
#endif
  rc = getaddrinfo(host, service, &hints, &res);
  if (rc != 0 || !res)
  {
    SetError("cannot find %s", host);
    return false;
  }
  memcpy(&host_addr, res->ai_addr, sizeof host_addr);
  freeaddrinfo(res);

  if (!OpenSocket(0))
    return false;

  memset(peers, 0, sizeof peers);
  memset(&game, 0, sizeof game);
  LateFree();
  LateReset();
  memset(lobby_present, 0, sizeof lobby_present);
  peers[0].used = true;
  peers[0].addr = host_addr;
  peers[0].last_recv = NowMs();
  my_slot = -1;
  my_nlocal = nlocal < 1 ? 1 : nlocal > BGNET_MAX_PLAYERS - 0 ? BGNET_MAX_PLAYERS : nlocal;
  state = BGNET_JOINING;
  last_hello = 0;
  return true;
}

int bgnet_LobbyPlayers(void)
{
  int i, n = 0;

  if (state == BGNET_HOSTING)
  {
    for (i = 0; i < BGNET_MAX_PLAYERS; ++i)
      n += HostSlot(i, NULL, NULL, NULL);
    return n;
  }
  for (i = 0; i < BGNET_MAX_PLAYERS; ++i)
    n += lobby_present[i] != 0;
  return n;
}

dboolean bgnet_LobbySlot(int slot, int* cls)
{
  if (slot < 0 || slot >= BGNET_MAX_PLAYERS)
    return false;
  if (state == BGNET_HOSTING)
  {
    int c = 0;
    dboolean r = HostSlot(slot, NULL, NULL, &c);

    if (cls) *cls = c;
    return r;
  }
  if (cls) *cls = lobby_class[slot];
  return lobby_present[slot] != 0;
}

// host only: has the player in this slot finished map number `index` of the map list?
dboolean bgnet_SlotCompleted(int slot, int index)
{
  int o;

  if (slot < 0 || slot >= BGNET_MAX_PLAYERS || index < 0 || index >= 512)
    return false;
  if (!HostSlot(slot, &o, NULL, NULL))
    return false;
  if (o == 0)
    return bg_ListMapCompleted(index);
  return (peers[o].progress[index / 8] >> (index % 8)) & 1;
}

static void LocalAddress(char* out, int size)
{
  struct sockaddr_in to, me;
  socklen_t len = sizeof me;
  bg_sock_t s;

  out[0] = 0;
  s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s == BG_BADSOCK)
    return;
  memset(&to, 0, sizeof to);
  to.sin_family = AF_INET;
  to.sin_port = htons(9);
  to.sin_addr.s_addr = inet_addr("8.8.8.8");
  if (connect(s, (struct sockaddr*) &to, sizeof to) == 0 &&
      getsockname(s, (struct sockaddr*) &me, &len) == 0 && me.sin_addr.s_addr != 0)
    snprintf(out, size, "%s", inet_ntoa(me.sin_addr));
  bg_closesock(s);
}

void bgnet_LocalIP(char* out, int size)
{
  LocalAddress(out, size);
}

void bgnet_StatusLine(char* out, int size)
{
  char ip[32];

  switch (state)
  {
    case BGNET_HOSTING:
      LocalAddress(ip, sizeof ip);
      snprintf(out, size, "HOSTING %s:%d", ip[0] ? ip : "THIS COMPUTER", port);
      break;
    case BGNET_JOINING: snprintf(out, size, "CONTACTING THE HOST..."); break;
    case BGNET_LOBBY: snprintf(out, size, "CONNECTED - WAITING FOR THE HOST"); break;
    case BGNET_SYNC: snprintf(out, size, "JOINING THE GAME IN PROGRESS..."); break;
    default: snprintf(out, size, "%s", errtext); break;
  }
}

//
// lockstep bookkeeping
//

// the lowest tic anybody still needs from player p; commands before it can be overwritten
static unsigned Horizon(int p)
{
  unsigned h = (unsigned) gametic;
  int q;

  // A slot nobody plays (yet) is not waited for, so its commands can be far behind the game: what this machine
  // needs of it is what the game will use, from the tic it enters, or nothing before the commands known so far.
  if (!IsMine(p) && !playeringame[p])
  {
    if (spawn_at[p] != NONE32 && spawn_at[p] >= (unsigned) gametic)
      h = spawn_at[p];
    else
      h = next_tic[p];
    if (h > next_tic[p])
      h = next_tic[p];
  }

  if (my_slot == 0)
  {
    int owner = OwnerOf(p);

    for (q = 1; q < BGNET_MAX_PLAYERS; ++q)
      if (q != owner && peers[q].used && !peers[q].dropped && !peers[q].late && peers[q].ack[p] < h)
        h = peers[q].ack[p];
  }
  else if (IsMine(p) && peers[0].ack[p] < h)
    h = peers[0].ack[p];
  return h;
}

static int stat_trimmed;     // commands not made because too many were already waiting (see bgnet_CanMake)

dboolean bgnet_CanMake(void)
{
  int k;

  if (!bgnet_Lockstep())
    return true;
  // Commands are made at 35 per second by the wall clock, but tics only run as fast as the slowest machine. After a
  // hitch on any machine the others' counters end up ahead of the game by several tics for good (the speed-up in
  // AfterTic cannot drain it: the game cannot pass the slowest machine's commands), which is a constant extra
  // input delay for everybody except the machine that hitched. So no command is made while more than `target_lead`
  // are already waiting (one more than the target, which is the slack that keeps tics even): the surplus is dropped.
  if (maketic - gametic > target_lead + 1)
  {
    ++stat_trimmed;
    return false;
  }
  {
    // a lead that stays above the target (every command is waiting one tic longer than it has to) is trimmed
    // slowly: one command is skipped after two seconds of it
    static int above;

    if (maketic - gametic > target_lead)
    {
      if (++above >= 70)
      {
        above = 0;
        ++stat_trimmed;
        return false;
      }
    }
    else
      above = 0;
  }
  for (k = 0; k < my_nlocal; ++k)
    if ((unsigned) maketic - Horizon(my_slot + k) >= (unsigned) SPAN)
      return false;
  return true;
}

static unsigned Limit(int p)
{
  if (drop_at[p] != NONE32 && next_tic[p] >= drop_at[p])
    return NONE32;
  return next_tic[p];
}

// is a join barrier up (a machine is being attached)?
static dboolean BarrierUp(void)
{
  int s;

  for (s = 0; s < BGNET_MAX_PLAYERS; ++s)
    if (att_at[s] != NONE32 && att_rel[s] != att_at[s])
      return true;
  return false;
}

int bgnet_ReadyTics(void)
{
  unsigned n = (unsigned) maketic;
  int p, waiting = -1;

  if (state == BGNET_LATE)
    return 0;                                  // the snapshot has not been put in yet
  for (p = 0; p < BGNET_MAX_PLAYERS; ++p)
  {
    unsigned l;

    if (att_at[p] != NONE32 && att_rel[p] != att_at[p] && n > att_at[p])
      n = att_at[p];                           // the join barrier: nobody runs tic att_at before it is lifted
    if (IsMine(p))
      continue;
    if (!playeringame[p])
    {
      // a player whose entry is announced is waited for from that tic on
      if (spawn_at[p] != NONE32 && spawn_at[p] >= (unsigned) gametic && (drop_at[p] == NONE32 || drop_at[p] > spawn_at[p]))
      {
        l = Limit(p);
        if (l < spawn_at[p])
          l = spawn_at[p];
        if (l < n)
        {
          n = l;
          waiting = p;
        }
      }
      continue;
    }
    l = Limit(p);
    if (l < n)
    {
      n = l;
      waiting = p;
    }
  }

  if (n <= (unsigned) gametic)
  {
    if (!stall_since)
    {
      stall_since = NowMs();
      if (getenv("BEGOTTEN_NETTEST_TRACE") && stat_stalls < 40)
        lprintf(LO_INFO, "TRACE stall at %u ms: gametic %d maketic %d next_tic[0..3] %u %u %u %u spawn_at[2] %u playeringame %d%d%d%d waiting %d due-in %lld us\n", stall_since % 100000, gametic, maketic, next_tic[0], next_tic[1], next_tic[2], next_tic[3], spawn_at[2], playeringame[0], playeringame[1], playeringame[2], playeringame[3], waiting, (long long) next_due_us - (long long) Usec());
      ++stat_stalls;
      if ((long long) Usec() >= (long long) next_due_us)
        ++stat_true_stalls;
      stat_stall_lead += maketic - gametic;
    }
    stall_player = waiting;
    return 0;
  }
  stall_since = 0;

  // The commands are here, but tics still run at an even 35 per second, not whenever a packet
  // happens to arrive: that is what keeps movement smooth when the network is not.
  {
    unsigned long long now = Usec();
    int avail = (int) (n - (unsigned) gametic), due;

    if (now < next_due_us)
      return 0;
    due = (now - next_due_us > 2 * TIC_US) ? 2 : 1;   // no bursts: speed up a little instead (see AfterTic)
    return avail < due ? avail : due;
  }
}

// how far between the last tic and the next one the picture is (for smooth uncapped rendering)
static int stat_frac_back, stat_frac_calls, stat_wdraws;

fixed_t bgnet_Frac(void)
{
  static fixed_t last_frac;
  static int last_gametic = -1;
  long long now = (long long) Usec(), since;
  fixed_t frac;

  ++stat_frac_calls;
  since = now - ((long long) next_due_us - (long long) last_interval_us);   // signed: can be negative
  if (since < 0)
    frac = 0;
  else if (since >= (long long) last_interval_us)
    frac = FRACUNIT;
  else
    frac = (fixed_t) (since * FRACUNIT / (long long) last_interval_us);

  // within one tic the picture must never move backwards (the interval changes while catching up, which
  // would otherwise make the view step back and forth between frames)
  if (last_gametic == gametic && frac < last_frac)
  {
    ++stat_frac_back;
    frac = last_frac;
  }
  last_frac = frac;
  last_gametic = gametic;
  return frac;
}

void bgnet_PreTic(void)
{
  int p;

  if (!bgnet_Lockstep())
    return;
  for (p = 0; p < BGNET_MAX_PLAYERS; ++p)
  {
    // a player enters the game (after the observer chose a character)
    if (spawn_at[p] == (unsigned) gametic && !playeringame[p] && (drop_at[p] == NONE32 || drop_at[p] > spawn_at[p]))
    {
      bg_CoopJoin(p, spawn_cls[p]);
      if (IsMine(p))
      {
        lj.want = false;
        lj.joined_tic = (unsigned) gametic;
      }
    }
    if (IsMine(p) || !playeringame[p] || drop_at[p] == NONE32 || drop_at[p] > (unsigned) gametic)
      continue;
    if (players[p].mo)
    {
      players[p].mo->player = NULL;
      P_RemoveMobj(players[p].mo);
      players[p].mo = NULL;
    }
    playeringame[p] = false;
    if (displayplayer == p)
      displayplayer = consoleplayer;
    doom_printf("%s left the game", bgnet_PlayerName(p));
  }

  // an observer chooses his character (strafe) and asks to enter (Use)
  if (bgnet_IsObserver() && spawn_at[my_slot] == NONE32 && att_applied[my_slot])
  {
    const ticcmd_t* c = &local_cmds[my_slot][gametic % BACKUPTICS];
    int side = c->sidemove > 20 ? 1 : c->sidemove < -20 ? -1 : 0;

    if (side && side != lj.last_side && bg_ClassCount() > 1)
    {
      int k, cls = lj.cls;

      for (k = 0; k < bg_ClassCount(); ++k)
      {
        cls = (cls + side + bg_ClassCount()) % bg_ClassCount();
        if (bg_ClassAvailable(cls))
          break;
      }
      lj.cls = cls;
    }
    lj.last_side = side;
    if ((c->buttons & BT_USE) && !lj.want && (unsigned) gametic >= test_joinat)
      lj.want = true;
  }
}

dboolean bgnet_IsObserver(void)
{
  return lj.late_joiner && state == BGNET_ACTIVE && my_slot > 0 && my_slot < MAX_MAXPLAYERS && !playeringame[my_slot];
}

int bgnet_ObserverClass(void) { return lj.cls; }
dboolean bgnet_ObserverJoining(void) { return lj.want; }


//
// Desync diagnostics: every tic leaves a small record (hash of the commands used, hash of the players'
// state afterwards, and the first random-number calls the game made).  When a desync is found the two
// machines swap the last 35 records, and the first tic where they differ is reported.
//
#define TRACEN 64
#define DIAGN 35
#define TRACECLS 6
typedef struct
{
  int tic;                    // -1: unused
  unsigned short cmdh, posh, cnt;
  byte cls[TRACECLS];
} trace_t;
static trace_t bgtrace[TRACEN];
static trace_t my_diag[DIAGN];
static unsigned diag_tic = NONE32, diag_until, diag_last_send;
static dboolean diag_compared;

void bgnet_RngTrace(int pr_class)
{
  trace_t* t = &bgtrace[gametic & (TRACEN - 1)];

  if (t->tic != gametic)
  {
    memset(t, 0, sizeof *t);
    t->tic = gametic;
  }
  if (t->cnt < TRACECLS)
    t->cls[t->cnt] = (byte) pr_class;
  if (t->cnt < 65535)
    ++t->cnt;
}

static void TraceAfterTic(int g)      // tic g has just been run
{
  trace_t* t = &bgtrace[g & (TRACEN - 1)];
  unsigned hc = 2166136261u, hp = 2166136261u;
  int p;

  if (t->tic != g)
  {
    memset(t, 0, sizeof *t);
    t->tic = g;
  }
  for (p = 0; p < BGNET_MAX_PLAYERS; ++p)
  {
    const ticcmd_t* c = &local_cmds[p][g % BACKUPTICS];

    if (!playeringame[p])
      continue;
    hc = (hc ^ (unsigned) (c->forwardmove & 255)) * 16777619u;
    hc = (hc ^ (unsigned) (c->sidemove & 255)) * 16777619u;
    hc = (hc ^ (unsigned) (c->angleturn & 65535)) * 16777619u;
    hc = (hc ^ c->buttons) * 16777619u;
    hc = (hc ^ c->lookfly) * 16777619u;
    hc = (hc ^ c->arti) * 16777619u;
    hc = (hc ^ c->ex.actions) * 16777619u;
    hc = (hc ^ (unsigned) (c->ex.look & 65535)) * 16777619u;
    if (players[p].mo)
    {
      hp = (hp ^ (unsigned) players[p].mo->x) * 16777619u;
      hp = (hp ^ (unsigned) players[p].mo->y) * 16777619u;
      hp = (hp ^ (unsigned) players[p].mo->z) * 16777619u;
      hp = (hp ^ (unsigned) players[p].mo->angle) * 16777619u;
      hp = (hp ^ (unsigned) players[p].mo->health) * 16777619u;
    }
  }
  {
    static fixed_t px, py;
    static int have;

    if (bgnet_InGame() && players[my_slot].mo && getenv("BEGOTTEN_NETTEST_WALLRUN"))
    {
      fixed_t x = players[my_slot].mo->x, y = players[my_slot].mo->y;

      if (have && g > 40)
      {
        int d = P_AproxDistance(x - px, y - py) >> FRACBITS;

        if (d > test_maxspeed)
          test_maxspeed = d;
      }
      px = x; py = y; have = 1;
    }
  }
  t->cmdh = (unsigned short) (hc ^ (hc >> 16));
  t->posh = (unsigned short) (hp ^ (hp >> 16));
}

static void DiagCapture(unsigned tic)  // my records for tics tic-35 .. tic-1
{
  int i;

  for (i = 0; i < DIAGN; ++i)
  {
    int g = (int) tic - DIAGN + i;
    const trace_t* t = &bgtrace[g & (TRACEN - 1)];

    if (g >= 0 && t->tic == g)
      my_diag[i] = *t;
    else
    {
      memset(&my_diag[i], 0, sizeof my_diag[i]);
      my_diag[i].tic = -1;
    }
  }
  diag_tic = tic;
  diag_compared = false;
}

static void SendDiag(int q)
{
  pkt_t p;
  int i, k;

  PutInit(&p, PKT_DIAG);
  Put8(&p, my_slot);
  Put32(&p, diag_tic);
  for (i = 0; i < DIAGN; ++i)
  {
    Put8(&p, my_diag[i].tic >= 0);
    Put16(&p, my_diag[i].cmdh);
    Put16(&p, my_diag[i].posh);
    Put16(&p, my_diag[i].cnt);
    for (k = 0; k < TRACECLS; ++k)
      Put8(&p, my_diag[i].cls[k]);
  }
  SendTo(&peers[q].addr, &p);
}

static void ClassList(char* out, int size, const trace_t* t)
{
  int k, n = 0;

  out[0] = 0;
  for (k = 0; k < TRACECLS && k < (int) t->cnt && n + 4 < size; ++k)
    n += snprintf(out + n, size - n, "%d ", t->cls[k]);
}

static void HandleDiag(pkt_t* p, int from_peer)
{
  int sender = Get8(p), i, k, first = -1;
  unsigned tic = Get32(p);
  trace_t theirs[DIAGN];
  char a[48], b[48];

  for (i = 0; i < DIAGN; ++i)
  {
    memset(&theirs[i], 0, sizeof theirs[i]);
    theirs[i].tic = Get8(p) ? (int) tic - DIAGN + i : -1;
    theirs[i].cmdh = (unsigned short) Get16(p);
    theirs[i].posh = (unsigned short) Get16(p);
    theirs[i].cnt = (unsigned short) Get16(p);
    for (k = 0; k < TRACECLS; ++k)
      theirs[i].cls[k] = (byte) Get8(p);
  }
  if (p->bad || from_peer < 0 || tic == NONE32 || tic == 0)
    return;
  if (diag_tic != tic)
    DiagCapture(tic);          // they found it first
  if (diag_compared)
    return;
  diag_compared = true;
  if (desyncs == 0)
  {
    desync_tic = tic;
    ++desyncs;
  }

  for (i = 0; i < DIAGN && first < 0; ++i)
  {
    if (my_diag[i].tic < 0 || theirs[i].tic < 0)
      continue;
    if (my_diag[i].cmdh != theirs[i].cmdh || my_diag[i].posh != theirs[i].posh || my_diag[i].cnt != theirs[i].cnt ||
        memcmp(my_diag[i].cls, theirs[i].cls, TRACECLS) != 0)
      first = i;
  }
  if (first < 0)
  {
    snprintf(desync_what[0], sizeof desync_what[0], "no difference in the last %d tics", DIAGN);
    desync_what[1][0] = desync_what[2][0] = 0;
    lprintf(LO_ERROR, "bgnet: diag: no difference in tics %d..%u (it started earlier)\n", (int) tic - DIAGN, tic - 1);
    return;
  }
  {
    int g = (int) tic - DIAGN + first;
    const char* what = my_diag[first].cmdh != theirs[first].cmdh ? "COMMANDS" :
                       (my_diag[first].cnt != theirs[first].cnt ||
                        memcmp(my_diag[first].cls, theirs[first].cls, TRACECLS)) ? "RANDOM CALLS" : "PLAYER STATE";

    ClassList(a, sizeof a, &my_diag[first]);
    ClassList(b, sizeof b, &theirs[first]);
    snprintf(desync_what[0], sizeof desync_what[0], "%s DIFFER AT TIC %d", what, g);
    snprintf(desync_what[1], sizeof desync_what[1], "MINE %d: %s", (int) my_diag[first].cnt, a);
    snprintf(desync_what[2], sizeof desync_what[2], "THEIRS %d: %s", (int) theirs[first].cnt, b);
    lprintf(LO_ERROR, "bgnet: diag: first difference at tic %d (%s)\n", g, what);
    lprintf(LO_ERROR, "bgnet: diag:   mine   cmd %04x pos %04x rng calls %d: %s\n", my_diag[first].cmdh, my_diag[first].posh, (int) my_diag[first].cnt, a);
    lprintf(LO_ERROR, "bgnet: diag:   theirs cmd %04x pos %04x rng calls %d: %s\n", theirs[first].cmdh, theirs[first].posh, (int) theirs[first].cnt, b);
  }
  (void) sender;
}

static void Snapshot(snap_t* o)
{
  unsigned h = 2166136261u, ht = 2166136261u, hs = 2166136261u, hp = 2166136261u;
  int i, k, n = 0;
  thinker_t* th;
#define MIXH(hh, v) ((hh) = ((hh) ^ (unsigned) (v)) * 16777619u)

  memset(o, 0, sizeof *o);
  MIXH(h, leveltime);
  // only the game's random numbers: M_Random() (= pr_misc, rng.prndindex) is also used by the screen wipe,
  // sound pitch, screen shake and the status bar, so it differs between machines by design
  MIXH(h, rng.seed[pr_all_in_one]);
  MIXH(h, rng.rndindex);
  for (i = 0; i < MAX_MAXPLAYERS; ++i)
  {
    if (!playeringame[i] || !players[i].mo)
      continue;
    MIXH(h, players[i].mo->x);
    MIXH(h, players[i].mo->y);
    MIXH(h, players[i].mo->z);
    MIXH(h, players[i].mo->angle);
    MIXH(h, players[i].mo->health);
    MIXH(h, players[i].readyweapon);
    MIXH(hp, players[i].mo->health);
    MIXH(hp, players[i].health);
    MIXH(hp, players[i].readyweapon);
    for (k = 0; k < NUMAMMO; ++k)
      MIXH(hp, players[i].ammo[k]);
    for (k = 0; k < NUMWEAPONS; ++k)
      MIXH(hp, players[i].weaponowned[k]);
    if (i < BGNET_MAX_PLAYERS)
    {
      o->v[8 + i] = (unsigned) players[i].mo->x;
      o->v[12 + i] = (unsigned) players[i].mo->y;
      o->v[16 + i] = (unsigned) players[i].mo->health;
    }
  }
  MIXH(h, totalkills);
  for (th = thinkercap.next; th != &thinkercap; th = th->next)
  {
    mobj_t* mo;

    if (th->function != P_MobjThinker)
      continue;
    mo = (mobj_t*) th;
    ++n;
    MIXH(ht, mo->x); MIXH(ht, mo->y); MIXH(ht, mo->z); MIXH(ht, mo->type); MIXH(ht, mo->health);
  }
  for (i = 0; i < numsectors; ++i)
  {
    MIXH(hs, sectors[i].floorheight);
    MIXH(hs, sectors[i].ceilingheight);
  }
  o->v[0] = h;
  o->v[1] = (unsigned) leveltime;
  o->v[2] = rng.seed[pr_all_in_one] ^ ((unsigned) rng.rndindex << 24);
  o->v[3] = (unsigned) totalkills;
  o->v[4] = (unsigned) n;
  o->v[5] = ht;
  o->v[6] = hs;
  o->v[7] = hp;
#undef MIXH
}

static unsigned long long Usec(void);
static unsigned long long gap_last_us;
static int gap_count, gap_long, gap_max_ms, gap_burst;

void bgnet_AfterTic(void)
{
  if (!bgnet_Lockstep())
    return;

  {
    unsigned long long now = Usec();

    if (gap_last_us)
    {
      int ms = (int) ((now - gap_last_us) / 1000);

      ++gap_count;
      if (ms > 45) ++gap_long;
      if (ms < 5) ++gap_burst;
      if (ms > gap_max_ms) gap_max_ms = ms;
    }
    gap_last_us = now;

    // Keep our own commands a few tics ahead of the game (that is what protects the other players
    // from our jitter): run a bit faster when the lead is too big, a bit slower when it is used up.
    {
      long long lead = (long long) maketic - gametic - target_lead;
      long long interval = (long long) TIC_US - lead * (long long) TIC_US / 25;

      if (interval < (long long) (TIC_US * 85 / 100)) interval = (long long) (TIC_US * 85 / 100);
      if (interval > (long long) (TIC_US * 120 / 100)) interval = (long long) (TIC_US * 120 / 100);
      last_interval_us = (unsigned long long) interval;
    }
    TraceAfterTic(gametic - 1);
    {
      // field diagnostics (begotten.log): odd weapon-sprite movement of my player, and the network timing every 10 s
      static int pw_ready = -1, pw_sx, pw_sy, pw_state, logged_odd;
      player_t* mp = my_slot >= 0 && my_slot < MAX_MAXPLAYERS ? &players[my_slot] : NULL;

      if (mp && mp->mo && playeringame[my_slot])
      {
        pspdef_t* wp = &mp->psprites[ps_weapon];
        int ssx = wp->sx >> FRACBITS, ssy = wp->sy >> FRACBITS;
        int st = wp->state ? (int) (wp->state - states) : -1;

        if (pw_ready >= 0 && logged_odd < 200 &&
            (abs(ssx - pw_sx) > 12 || abs(ssy - pw_sy) > 12))
        {
          ++logged_odd;
          lprintf(LO_INFO, "WEAPONODD lt %d gt %d: ready %d->%d pending %d state %d->%d sx %d->%d sy %d->%d bob %d momx %d alt %d class %d\n",
                  leveltime, gametic, pw_ready, mp->readyweapon, mp->pendingweapon, pw_state, st, pw_sx, ssx, pw_sy, ssy,
                  mp->bob >> FRACBITS, mp->momx >> FRACBITS, mp->altactive, mp->bgclass);
        }
        pw_ready = mp->readyweapon; pw_sx = ssx; pw_sy = ssy; pw_state = st;
      }
      {
        // every player: how often the weapon is put away and brought up again, and why
        static int was_lowering[MAX_MAXPLAYERS], n_lower[MAX_MAXPLAYERS], logged_lower;
        int s;

        for (s = 0; s < BGNET_MAX_PLAYERS && s < MAX_MAXPLAYERS; ++s)
        {
          player_t* q = &players[s];
          dboolean lowering;

          if (!playeringame[s] || !q->mo)
            continue;
          lowering = q->psprites[ps_weapon].state && q->psprites[ps_weapon].state->action == A_Lower;
          if (lowering && !was_lowering[s])
          {
            ++n_lower[s];
            if (logged_lower < 60)
            {
              ++logged_lower;
              lprintf(LO_INFO, "WEAPONLOWER slot %d%s lt %d gt %d: ready %d pending %d health %d class %d attackdown %d buttons %d\n",
                      s, s == my_slot ? " (me)" : "", leveltime, gametic, q->readyweapon, q->pendingweapon, q->health,
                      q->bgclass, q->attackdown, q->cmd.buttons);
            }
          }
          was_lowering[s] = lowering;
        }
        if (gametic % 350 == 0)
          lprintf(LO_INFO, "WEAPONSTAT gt %d: weapon put-aways slot0 %d slot1 %d; consoleplayer %d displayplayer %d my_slot %d\n",
                  gametic, n_lower[0], n_lower[1], consoleplayer, displayplayer, my_slot);
      }
      if (gametic % 350 == 0)
        lprintf(LO_INFO, "NETSTAT gt %d: frac-back %d/%d true-stalls %d stalls %d gaps %d long %d burst %d max %d ms, interval %d us, lead %d (target %d), trimmed %d\n",
                gametic, stat_frac_back, stat_frac_calls, stat_true_stalls, stat_stalls, gap_count, gap_long, gap_burst, gap_max_ms, (int) last_interval_us, maketic - gametic, target_lead, stat_trimmed);
    }
    next_due_us += last_interval_us;
    if (now > next_due_us + TIC_US / 2)
    {
      // We ran late (waited for the other player, or a slow frame). Do not run the missed tics in a burst:
      // two tics in one frame make the camera lurch while the weapon stays put, which looks like the weapon
      // jittering. Start the next tic a full interval from now; the lead adjustment above makes the lost time
      // up gradually (up to 15% faster) instead.
      next_due_us = now + last_interval_us;
    }
  }

  if (gamestate == GS_DEMOSCREEN && gametic > 2)
  {
    EndSession("The network game ended.");
    return;
  }

  if (gamestate == GS_LEVEL && gametic > 0 && gametic % HASH_INTERVAL == 0)
  {
    hashrec_t* r = &hashes[(gametic / HASH_INTERVAL) % 16];

    r->used = true;
    r->tic = (unsigned) gametic;
    Snapshot(&r->snap);
    my_snap = r->snap;
    my_hash_tic = r->tic;
  }
}

static void DropPeer(int q, const char* why);

// Host, three players or more: when one player's state differs from ours while another player's
// matches, the odd one out is the one that went wrong. It is removed and the game goes on.
static void RemoveOutliers(void)
{
  int q, o;

  if (my_slot != 0 || state != BGNET_ACTIVE)
    return;
  for (q = 1; q < BGNET_MAX_PLAYERS; ++q)
  {
    if (!peers[q].used || peers[q].dropped || !peers[q].bad_tic)
      continue;
    for (o = 1; o < BGNET_MAX_PLAYERS; ++o)
      if (o != q && peers[o].used && !peers[o].dropped && peers[o].last_match >= peers[q].bad_tic)
      {
        lprintf(LO_ERROR, "bgnet: player %d is out of sync (from tic %u) while player %d is not: removing player %d\n",
                q + 1, peers[q].bad_tic, o + 1, q + 1);
        SendBye(&peers[q].addr, 1);
        DropPeer(q, "out of sync");
        peers[q].bad_tic = 0;
        desyncs = 0;                       // the rest of the game is in agreement
        desync_what[0][0] = desync_what[1][0] = desync_what[2][0] = 0;
        bg_ShowMessage("A player fell out of sync and was removed.");
        break;
      }
  }
}

static void CheckPeerHash(int sender, unsigned tic, const snap_t* theirs)
{
  hashrec_t* r;
  int i, first = -1;

  if (tic == NONE32 || tic == 0)
    return;
  r = &hashes[(tic / HASH_INTERVAL) % 16];
  if (!r->used || r->tic != tic)
    return;
  if (r->snap.v[0] == theirs->v[0])
  {
    if (my_slot == 0 && sender > 0 && tic > peers[sender].last_match)
    {
      peers[sender].last_match = tic;
      RemoveOutliers();
    }
    return;
  }
  if (my_slot == 0 && sender > 0 && !peers[sender].bad_tic)
    peers[sender].bad_tic = tic;
  if (desyncs == 0)
  {
    desync_tic = tic;
    DiagCapture(tic);
    diag_until = NowMs() + 4000;
    diag_last_send = 0;
    lprintf(LO_ERROR, "bgnet: out of sync with player %d at tic %u (%08x vs %08x)\n", sender + 1, tic, r->snap.v[0], theirs->v[0]);
    for (i = 1; i < SNAPN; ++i)
      if (r->snap.v[i] != theirs->v[i])
      {
        lprintf(LO_ERROR, "bgnet:   %s differs: mine %d, theirs %d\n", snap_names[i], (int) r->snap.v[i], (int) theirs->v[i]);
        if (first < 0)
          first = i;
      }
    if (first >= 0)
    {
      snprintf(desync_what[0], sizeof desync_what[0], "%s differs", snap_names[first]);
      snprintf(desync_what[1], sizeof desync_what[1], "mine %d theirs %d", (int) r->snap.v[first], (int) theirs->v[first]);
    }
  }
  ++desyncs;
  RemoveOutliers();
}

// a line at the top of the screen while a lobby is open (also behind the menus)
void bgnet_DrawBanner(void)
{
  const char* text = NULL;
  char buf[48];

  if (state == BGNET_HOSTING)
  {
    int others = bgnet_LobbyPlayers() - my_nlocal;

    if (others > 0)
    {
      snprintf(buf, sizeof buf, "%d PLAYER%s CONNECTED", others, others > 1 ? "S" : "");
      text = buf;
    }
    else
      text = "WAITING FOR PLAYERS";
  }
  else if (state == BGNET_JOINING)
    text = "CONNECTING TO THE HOST...";
  else if (state == BGNET_LOBBY)
    text = "CONNECTED - WAITING FOR THE HOST";
  else if (state == BGNET_SYNC)
    text = "JOINING THE GAME IN PROGRESS...";
  if (text)
    bg_WriteText(160 - (int) strlen(text) * 4, 4, text, CR_BLUE);
}

void bgnet_DrawStatus(void)
{
  char buf[64];

  if (!bgnet_Lockstep())
    return;
  if (desyncs)
  {
    snprintf(buf, sizeof buf, "OUT OF SYNC AT TIC %u", desync_tic);
    bg_WriteText(160 - (int) strlen(buf) * 4, 4, buf, CR_RED);
    if (desync_what[0][0])
    {
      bg_WriteText(160 - (int) strlen(desync_what[0]) * 4, 14, desync_what[0], CR_RED);
      bg_WriteText(160 - (int) strlen(desync_what[1]) * 4, 24, desync_what[1], CR_RED);
      if (desync_what[2][0])
        bg_WriteText(160 - (int) strlen(desync_what[2]) * 4, 34, desync_what[2], CR_RED);
    }
  }
  else if (BarrierUp() && gametic >= 0)
  {
    int s;

    for (s = 0; s < BGNET_MAX_PLAYERS; ++s)
      if (att_at[s] != NONE32 && att_rel[s] != att_at[s] && gametic >= (int) att_at[s])
      {
        snprintf(buf, sizeof buf, "%s IS JOINING...", late.slot == s || names[s][0] ? bgnet_PlayerName(s) : "A PLAYER");
        {
          int k;

          for (k = 0; buf[k]; ++k)
            if (buf[k] >= 'a' && buf[k] <= 'z')
              buf[k] = (char) (buf[k] - 32);
        }
        bg_WriteText(160 - (int) strlen(buf) * 4, 4, buf, CR_GOLD);
        break;
      }
  }
  else if (stall_since && NowMs() - stall_since > 1000)
  {
    if (stall_player >= 0)
      snprintf(buf, sizeof buf, "WAITING FOR PLAYER %d...", stall_player + 1);
    else
      snprintf(buf, sizeof buf, "WAITING...");
    bg_WriteText(160 - (int) strlen(buf) * 4, 4, buf, CR_GOLD);
  }
}

//
// sending
//

static void SendTics(int q)
{
  pkt_t p;
  int i, runs = 0, runs_at;
  struct sockaddr_in* to = &peers[q].addr;

  PutInit(&p, PKT_TICS);
  Put8(&p, my_slot);
  for (i = 0; i < BGNET_MAX_PLAYERS; ++i)
    Put32(&p, IsMine(i) ? (unsigned) maketic : next_tic[i]);
  Put32(&p, my_hash_tic);
  for (i = 0; i < SNAPN; ++i)
    Put32(&p, my_snap.v[i]);
  for (i = 0; i < BGNET_MAX_PLAYERS; ++i)
    Put32(&p, my_slot == 0 ? drop_at[i] : NONE32);
  for (i = 0; i < BGNET_MAX_PLAYERS; ++i)
  {
    Put32(&p, my_slot == 0 ? att_at[i] : NONE32);
    Put32(&p, my_slot == 0 ? att_rel[i] : NONE32);
    Put32(&p, my_slot == 0 ? spawn_at[i] : NONE32);
    Put8(&p, spawn_cls[i]);
    if (my_slot == 0 && att_at[i] != NONE32)
      PutName(&p, names[i]);
    else
      Put8(&p, 0);
  }
  runs_at = p.len;
  Put8(&p, 0);

  for (i = 0; i < BGNET_MAX_PLAYERS; ++i)
  {
    unsigned first, last, count, t;

    if (OwnerOf(i) == q)
      continue;                       // that machine made them itself
    if (my_slot != 0 && !IsMine(i))
      continue;                       // a client sends only its own commands
    if (!game.present[i] || (!IsMine(i) && !playeringame[i] && drop_at[i] == NONE32 && !att_applied[i]))
      continue;
    first = peers[q].ack[i];
    last = IsMine(i) ? (unsigned) maketic : next_tic[i];
    if (last <= first)
      continue;
    count = last - first;
    if (count > (unsigned) SPAN)
    {
      first = last - SPAN;
      count = SPAN;
    }
    Put8(&p, i);
    Put32(&p, first);
    Put8(&p, count);
    for (t = first; t < last; ++t)
      PutCmd(&p, &local_cmds[i][t % BACKUPTICS]);
    ++runs;
  }
  p.d[runs_at] = (byte) runs;
  SendTo(to, &p);
  peers[q].last_send = NowMs();
}

static void SendLobby(int q)
{
  pkt_t p;
  int i;

  PutInit(&p, PKT_LOBBY);
  Put8(&p, q);
  FillSubNames();
  for (i = 0; i < BGNET_MAX_PLAYERS; ++i)
  {
    int c = 0;
    dboolean here = HostSlot(i, NULL, NULL, &c);

    Put8(&p, here);
    Put8(&p, c);
    PutName(&p, here ? names[i] : "");
  }
  Put8(&p, dsda_IntConfig(dsda_config_begotten_friendly_fire) != 0);
  {
    const char* sn = dsda_StringConfig(dsda_config_begotten_server_name);
    int n = (int) strlen(sn ? sn : ""), k;

    if (n > SERVERNAMELEN) n = SERVERNAMELEN;
    Put8(&p, n);
    for (k = 0; k < n; ++k)
      Put8(&p, (byte) sn[k]);
  }
  {
    int bits = 0;

    Put8(&p, lobby_level >= 0 ? (lobby_level & 255) : 255);
    Put8(&p, lobby_level >= 0 ? (lobby_level >> 8) : 255);
    Put8(&p, lobby_skill);
    if (lobby_level >= 0)
      for (i = 0; i < BGNET_MAX_PLAYERS; ++i)
        if (bgnet_SlotCompleted(i, lobby_level))
          bits |= 1 << i;
    Put8(&p, bits);
  }
  SendTo(&peers[q].addr, &p);
  peers[q].last_send = NowMs();
}

static void SendStart(int q)
{
  pkt_t p;
  int i;

  PutInit(&p, PKT_START);
  Put8(&p, q);
  Put8(&p, game.skill);
  Put8(&p, game.episode);
  Put8(&p, game.map);
  Put8(&p, game.ff);
  Put8(&p, game.complevel);
  for (i = 0; i < BGNET_MAX_PLAYERS; ++i)
  {
    Put8(&p, game.present[i]);
    Put8(&p, game.cls[i]);
    PutName(&p, names[i]);
  }
  Put8(&p, game.optsize);
  for (i = 0; i < game.optsize; ++i)
    Put8(&p, game.opts[i]);
  for (i = 0; i < 16; ++i)
    Put8(&p, game.sim[i]);
  SendTo(&peers[q].addr, &p);
  peers[q].last_send = NowMs();
}

//
// receiving
//

static int PeerFor(const struct sockaddr_in* from)
{
  int i;

  for (i = 0; i < BGNET_MAX_PLAYERS; ++i)
    if (peers[i].used && SameAddr(&peers[i].addr, from))
      return i;
  return -1;
}

//
// Joining a game in progress (see the comment at att_at). Host side first.
//

static void SendLate(int q)
{
  pkt_t p;
  int i;

  PutInit(&p, PKT_LATE);
  Put8(&p, q);
  Put8(&p, late.comp != NULL);
  Put32(&p, late.tic);
  Put32(&p, (unsigned) late.raw_len);
  Put32(&p, (unsigned) late.comp_len);
  Put8(&p, game.skill);
  Put8(&p, game.episode);
  Put8(&p, game.map);
  Put8(&p, game.ff);
  Put8(&p, game.complevel);
  for (i = 0; i < BGNET_MAX_PLAYERS; ++i)
  {
    Put8(&p, game.present[i]);
    Put8(&p, game.cls[i]);
    PutName(&p, names[i]);
  }
  Put8(&p, game.optsize);
  for (i = 0; i < game.optsize; ++i)
    Put8(&p, game.opts[i]);
  for (i = 0; i < 16; ++i)
    Put8(&p, game.sim[i]);
  SendTo(&peers[q].addr, &p);
  peers[q].last_send = NowMs();
}

static void SendSnapChunk(int q, int idx)
{
  pkt_t p;
  int n = idx == late.nchunks - 1 ? late.comp_len - idx * SNAPCHUNK : SNAPCHUNK;

  PutInit(&p, PKT_SNAP);
  Put8(&p, q);
  Put16(&p, (unsigned) idx);
  if (p.len + n > MAXPKT)
    return;
  memcpy(p.d + p.len, late.comp + (size_t) idx * SNAPCHUNK, (size_t) n);
  p.len += n;
  SendTo(&peers[q].addr, &p);
}

// the host gives up on the machine being attached (it went away, was too slow, or the level ended): the
// barrier is lifted and the game goes on without it
static void LateCancel(int reason)
{
  int s = late.slot;

  if (s >= 0)
  {
    if (peers[s].used && reason)
      SendReject(&peers[s].addr, reason);
    memset(&peers[s], 0, sizeof peers[s]);
    att_rel[s] = att_at[s];
    lprintf(LO_INFO, "bgnet: the machine joining as player %d was not attached (reason %d)\n", s + 1, reason);
  }
  free(late.comp);
  memset(&late, 0, sizeof late);
  late.slot = -1;
  dirty = true;
}

// the joiner has the game: it is a player of the lockstep now (as an observer)
static void LateRelease(void)
{
  int s = late.slot, p;

  peers[s].late = false;
  peers[s].last_recv = NowMs();
  for (p = 0; p < BGNET_MAX_PLAYERS; ++p)
    peers[s].ack[p] = late.tic;
  att_rel[s] = att_at[s];
  lprintf(LO_INFO, "bgnet: player %d attached at tic %u (snapshot %d bytes, %d sent)\n", s + 1, late.tic, late.raw_len, late.comp_len);
  free(late.comp);
  memset(&late, 0, sizeof late);
  late.slot = -1;
  dirty = true;
}

// every machine, when its game reaches the barrier tic: the slot starts afresh for the new machine
static void ApplyAttach(int s)
{
  int q;

  att_applied[s] = true;
  drop_at[s] = NONE32;
  spawn_at[s] = NONE32;
  next_tic[s] = att_at[s];
  if (!IsMine(s))
    memset(local_cmds[s], 0, sizeof local_cmds[s]);
  if (my_slot == 0)
    for (q = 1; q < BGNET_MAX_PLAYERS; ++q)
      if (peers[q].used && q != s)
        peers[q].ack[s] = att_at[s];
  game.present[s] = true;
}

static void TakeSnapshot(void)
{
  byte* raw = NULL;
  int rawlen = 0;
  uLongf cl;

  dsda_StoreSnapshot(&raw, &rawlen);
  cl = compressBound((uLong) rawlen);
  late.comp = malloc(cl);
  if (!late.comp || compress2(late.comp, &cl, raw, (uLong) rawlen, 6) != Z_OK)
  {
    Z_Free(raw);
    free(late.comp);
    late.comp = NULL;
    LateCancel(5);
    return;
  }
  Z_Free(raw);
  late.raw_len = rawlen;
  late.comp_len = (int) cl;
  late.nchunks = (late.comp_len + SNAPCHUNK - 1) / SNAPCHUNK;
  late.acked = 0;
  late.last_send = 0;
  late.last_info = 0;
  lprintf(LO_INFO, "bgnet: snapshot for player %d at tic %u: %d bytes, %d compressed, %d packets\n", late.slot + 1, late.tic, rawlen, late.comp_len, late.nchunks);
}

static void LateHello(const struct sockaddr_in* from, int q, int nl, int cls, const char* name)
{
  int s, i;

  if (q >= 0)
  {
    if (peers[q].late)
    {
      peers[q].last_recv = NowMs();
      SendLate(q);
    }
    return;
  }
  if (nl != 1)
  {
    SendReject(from, 4);
    return;
  }
  if (late.slot >= 0 || gamestate != GS_LEVEL || gameaction != ga_nothing || !bg_weapons)
  {
    SendReject(from, 5);
    return;
  }
  for (s = my_nlocal; s < BGNET_MAX_PLAYERS; ++s)
    if (OwnerOf(s) < 0 && !playeringame[s] && (drop_at[s] == NONE32 || drop_at[s] < (unsigned) gametic) &&
        (att_at[s] == NONE32 || att_rel[s] == att_at[s]))
      break;
  if (s >= BGNET_MAX_PLAYERS)
  {
    SendReject(from, 1);
    return;
  }
  memset(&peers[s], 0, sizeof peers[s]);
  peers[s].used = true;
  peers[s].addr = *from;
  peers[s].nlocal = 1;
  peers[s].late = true;
  peers[s].start_acked = true;
  peers[s].lcls[0] = peers[s].cls = cls >= 0 && cls < bg_ClassCount() ? cls : 0;
  peers[s].last_recv = NowMs();
  snprintf(names[s], sizeof names[s], "%s", name);
  memset(&late, 0, sizeof late);
  late.slot = s;
  late.tic = (unsigned) maketic;
  late.started = NowMs();
  att_at[s] = late.tic;
  att_rel[s] = NONE32;
  att_applied[s] = false;
  game.present[s] = true;
  game.cls[s] = peers[s].cls;
  dirty = true;
  (void) i;
  lprintf(LO_INFO, "bgnet: a machine joins the running game as player %d from %s (barrier at tic %u, now %d)\n", s + 1, inet_ntoa(from->sin_addr), late.tic, gametic);
  SendLate(s);
}

static void HandleJoinReq(pkt_t* p, const struct sockaddr_in* from)
{
  int q = PeerFor(from), slot, cls;

  if (state != BGNET_ACTIVE || my_slot != 0 || q <= 0)
    return;
  slot = Get8(p);
  cls = Get8(p);
  if (p->bad || slot != q || peers[q].late || !att_applied[q] || att_rel[q] != att_at[q])
    return;
  peers[q].last_recv = NowMs();
  if (playeringame[q] || spawn_at[q] != NONE32)
    return;
  if (cls < 0 || cls >= bg_ClassCount())
    cls = 0;
  spawn_at[q] = (unsigned) maketic;
  spawn_cls[q] = cls;
  game.cls[q] = cls;
  dirty = true;
  lprintf(LO_INFO, "bgnet: player %d enters the game at tic %u as class %d\n", q + 1, spawn_at[q], cls);
}

static void HandleSnapAck(pkt_t* p, const struct sockaddr_in* from)
{
  int q = PeerFor(from);
  unsigned have;

  if (state != BGNET_ACTIVE || my_slot != 0 || q <= 0 || !peers[q].late)
    return;
  (void) Get8(p);
  have = Get32(p);
  if (p->bad)
    return;
  peers[q].last_recv = NowMs();
  if (q == late.slot && late.comp && (int) have > late.acked && (int) have <= late.nchunks)
    late.acked = (int) have;
}

// host, every frame between tics
static void LateHostTick(void)
{
  int s = late.slot;
  unsigned now = NowMs();

  if (s < 0)
    return;
  if (!peers[s].used || peers[s].dropped)
  {
    LateCancel(0);
    return;
  }
  if (now - late.started > LATE_TIMEOUT_MS)
  {
    LateCancel(6);
    return;
  }
  if (!late.comp && gametic >= (int) late.tic)
  {
    if (gametic != (int) late.tic || gamestate != GS_LEVEL || gameaction != ga_nothing || bg_CoopWiping())
      LateCancel(5);
    else
      TakeSnapshot();
  }
}

// host, in the packet loop: the snapshot goes out in a window of pieces, the joiner says how many it has
static void LateHostSend(unsigned now)
{
  int s = late.slot, i, last;

  if (s < 0 || !peers[s].late)
    return;
  if (now - late.last_info >= 250)
  {
    late.last_info = now;
    SendLate(s);
  }
  if (!late.comp || late.acked >= late.nchunks || now - late.last_send < 15)
    return;
  late.last_send = now;
  last = late.acked + 24 < late.nchunks ? late.acked + 24 : late.nchunks;
  for (i = late.acked; i < last; ++i)
    SendSnapChunk(s, i);
}

//
// the joining machine
//

static void SendSnapAck(void)
{
  pkt_t p;

  PutInit(&p, PKT_SNAPACK);
  Put8(&p, my_slot);
  Put32(&p, (unsigned) lj.have);
  SendTo(&host_addr, &p);
  lj.last_ack = NowMs();
}

static void LateFailed(const char* why)
{
  SetError("%s", why);
  SendBye(&host_addr, 0);
  Cleanup();
}

static void LateInflate(void)
{
  uLongf rl = (uLongf) lj.raw_len;

  lj.raw = malloc((size_t) lj.raw_len);
  if (!lj.raw || uncompress(lj.raw, &rl, lj.comp, (uLong) lj.comp_len) != Z_OK || rl != (uLongf) lj.raw_len)
  {
    LateFailed("the game sent by the host was damaged");
    return;
  }
  free(lj.comp);
  lj.comp = NULL;
  free(lj.got);
  lj.got = NULL;
  lj.have = lj.nchunks;
  lj.late_joiner = true;
  SendSnapAck();
  state = BGNET_STARTING;
  G_DeferedInitNew(game.skill, game.episode, game.map);
  M_ClearMenus();
}

static void HandleLate(pkt_t* p, const struct sockaddr_in* from)
{
  int slot, ready, i, n;
  unsigned tic, raw, comp;

  if (!SameAddr(from, &host_addr))
    return;
  peers[0].last_recv = NowMs();
  if (state != BGNET_JOINING && state != BGNET_LOBBY && state != BGNET_SYNC)
    return;

  slot = Get8(p);
  ready = Get8(p);
  tic = Get32(p);
  raw = Get32(p);
  comp = Get32(p);
  game.skill = Get8(p);
  game.episode = Get8(p);
  game.map = Get8(p);
  game.ff = Get8(p);
  game.complevel = Get8(p);
  for (i = 0; i < BGNET_MAX_PLAYERS; ++i)
  {
    game.present[i] = Get8(p);
    game.cls[i] = Get8(p);
    GetName(p, names[i]);
  }
  n = Get8(p);
  if (p->bad || n > (int) sizeof game.opts || slot < 1 || slot >= BGNET_MAX_PLAYERS)
    return;
  for (i = 0; i < n; ++i)
    game.opts[i] = (byte) Get8(p);
  game.optsize = n;
  for (i = 0; i < 16; ++i)
    game.sim[i] = (byte) Get8(p);
  if (p->bad)
    return;
  if (game.complevel != compatibility_level || !mbf21 || n != dsda_GameOptionSize())
  {
    LateFailed("the host plays with different rules");
    return;
  }
  my_slot = slot;
  game.present[slot] = true;
  if (state != BGNET_SYNC)
  {
    state = BGNET_SYNC;
    lj.cls = my_class;
    lj.last_ack = 0;
  }
  if (ready && !lj.comp)
  {
    if (comp == 0 || comp > (64u << 20) || raw == 0 || raw > (256u << 20))
    {
      LateFailed("the host's game is too big to join");
      return;
    }
    lj.comp = malloc(comp);
    lj.nchunks = (int) ((comp + SNAPCHUNK - 1) / SNAPCHUNK);
    lj.got = calloc((size_t) lj.nchunks, 1);
    if (!lj.comp || !lj.got)
    {
      LateFailed("out of memory");
      return;
    }
    lj.comp_len = (int) comp;
    lj.raw_len = (int) raw;
    lj.tic = tic;
    lj.have = lj.ngot = 0;
  }
}

static void HandleSnap(pkt_t* p, const struct sockaddr_in* from)
{
  int idx, n, expect;

  if (state != BGNET_SYNC || !lj.comp || !SameAddr(from, &host_addr))
    return;
  (void) Get8(p);
  idx = Get16(p);
  n = p->len - p->pos;
  if (p->bad || idx < 0 || idx >= lj.nchunks)
    return;
  expect = idx == lj.nchunks - 1 ? lj.comp_len - idx * SNAPCHUNK : SNAPCHUNK;
  if (n != expect)
    return;
  peers[0].last_recv = NowMs();
  if (!lj.got[idx])
  {
    memcpy(lj.comp + (size_t) idx * SNAPCHUNK, p->d + p->pos, (size_t) n);
    lj.got[idx] = 1;
    ++lj.ngot;
    while (lj.have < lj.nchunks && lj.got[lj.have])
      ++lj.have;
  }
  if (lj.ngot == lj.nchunks)
    LateInflate();
  else if (NowMs() - lj.last_ack >= 30)
    SendSnapAck();
}

// The placeholder level is up: put the host's game in. The snapshot is the state at the start of tic lj.tic, so
// that is the tic this machine continues from; its own commands start there.
static void LateRestore(void)
{
  int i, n;

  gametic = (int) lj.tic;
  dsda_RestoreSnapshot(lj.raw);
  free(lj.raw);
  lj.raw = NULL;
  netgame = true;
  deathmatch = 0;
  consoleplayer = displayplayer = my_slot;
  for (i = 0; i < MAX_MAXPLAYERS; ++i)
    if (!playeringame[i])
      players[i].mo = NULL;                 // (the placeholder level's bodies are gone)
  for (i = 0; i < MAX_MAXPLAYERS; ++i)
    if (playeringame[i] && players[i].mo)
    {
      displayplayer = i;                    // watch somebody from the first frame
      break;
    }

  memset(local_cmds, 0, sizeof(ticcmd_t) * MAX_MAXPLAYERS * BACKUPTICS);
  n = dsda_IntConfig(dsda_config_begotten_net_delay);
  if (n < 0) n = 0;
  if (n > 4) n = 4;
  target_lead = n < 1 ? 1 : n;
  last_interval_us = TIC_US;
  maketic = (int) lj.tic + n;
  force_singletics_to = 0;
  if (S_sfx)
    for (i = 0; i < num_sfx; ++i)
      S_sfx[i].parallel_tic = 0;
  LateReset();
  for (i = 0; i < BGNET_MAX_PLAYERS; ++i)
  {
    int k;

    next_tic[i] = lj.tic;
    drop_at[i] = NONE32;
    for (k = 0; k < BGNET_MAX_PLAYERS; ++k)
      peers[i].ack[k] = lj.tic;
  }
  att_at[my_slot] = att_rel[my_slot] = lj.tic;
  att_applied[my_slot] = true;
  // machines that were here before the barrier are already part of the snapshot: their old attach announcements
  // (still repeated in every packet) must not reset their slot again
  for (i = 0; i < BGNET_MAX_PLAYERS; ++i)
    if (i != my_slot && (playeringame[i] || game.present[i]))
    {
      att_at[i] = att_rel[i] = lj.tic;
      att_applied[i] = true;
    }
  lj.attach_tic = lj.tic;
  memset(hashes, 0, sizeof hashes);
  last_make_seen = 0;
  { int k; for (k = 0; k < TRACEN; ++k) bgtrace[k].tic = -1; }
  bgnet_tracing = 1;
  diag_tic = NONE32;
  diag_until = 0;
  gap_last_us = 0;
  gap_count = gap_long = gap_burst = gap_max_ms = 0;
  next_due_us = Usec();
  my_hash_tic = NONE32;
  desyncs = 0;
  desync_what[0][0] = desync_what[1][0] = desync_what[2][0] = 0;
  stall_since = 0;
  dirty = true;
  state = BGNET_ACTIVE;
  lprintf(LO_INFO, "bgnet: joined the running game as player %d, observing from tic %u\n", my_slot + 1, lj.tic);
}

// once per frame, between tics
void bgnet_LateTick(void)
{
  int s;

  if (state == BGNET_LATE)
  {
    if (gamestate == GS_LEVEL && gameaction == ga_nothing)
      LateRestore();
    return;
  }
  if (state != BGNET_ACTIVE)
    return;
  for (s = 0; s < BGNET_MAX_PLAYERS; ++s)
    if (att_at[s] != NONE32 && !att_applied[s] && gametic >= (int) att_at[s])
      ApplyAttach(s);
  if (my_slot == 0)
    LateHostTick();
}

static void HandleHello(pkt_t* p, const struct sockaddr_in* from)
{
  unsigned hash = Get32(p);
  int cls = Get8(p), q = PeerFor(from), i;
  char nm[NAMELEN + 1];
  int nb, nl, lc[BGNET_MAX_PLAYERS];
  char nms[BGNET_MAX_PLAYERS][NAMELEN + 1];
  byte prog[64];

  nl = Get8(p);
  if (nl < 1 || nl > BGNET_MAX_PLAYERS)
    return;
  for (i = 0; i < nl; ++i)
    lc[i] = Get8(p);
  for (i = 0; i < nl; ++i)
  {
    nms[i][0] = 0;
    GetName(p, nms[i]);
  }
  (void) nm;
  nb = Get8(p);
  if (nb > 64)
    return;
  memset(prog, 0, sizeof prog);
  for (i = 0; i < nb; ++i)
    prog[i] = (byte) Get8(p);
  if (p->bad)
    return;
  if (state != BGNET_HOSTING && !(state == BGNET_ACTIVE && my_slot == 0))
  {
    if (q < 0)
      SendReject(from, 2); // game already running
    return;
  }
  if (hash != ContentHash())
  {
    SendReject(from, 3);
    return;
  }
  if (state == BGNET_ACTIVE)
  {
    // a game in progress: the machine joins as an observer
    LateHello(from, q, nl, lc[0], nms[0]);
    return;
  }
  if (q < 0)
  {
    // the first run of free slots that is long enough
    for (i = my_nlocal; i + nl <= BGNET_MAX_PLAYERS; ++i)
    {
      int k, free_run = 1;

      for (k = 0; k < nl; ++k)
        if (OwnerOf(i + k) >= 0)
          free_run = 0;
      if (free_run)
        break;
    }
    if (i + nl > BGNET_MAX_PLAYERS)
    {
      SendReject(from, 1);
      return;
    }
    q = i;
    memset(&peers[q], 0, sizeof peers[q]);
    peers[q].used = true;
    peers[q].addr = *from;
    peers[q].nlocal = nl;
    lprintf(LO_INFO, "bgnet: player %d joined from %s (%d local player%s)\n", q + 1, inet_ntoa(from->sin_addr), nl, nl > 1 ? "s" : "");
  }
  if (nl != peers[q].nlocal)
    return;
  for (i = 0; i < nl; ++i)
    peers[q].lcls[i] = lc[i] >= 0 && lc[i] < bg_ClassCount() ? lc[i] : 0;
  peers[q].cls = peers[q].lcls[0];
  memcpy(peers[q].progress, prog, sizeof prog);
  for (i = 0; i < nl; ++i)
    snprintf(names[q + i], sizeof names[q + i], "%s", nms[i]);
  peers[q].last_recv = NowMs();
  SendLobby(q);
}

static void HandleStart(pkt_t* p, const struct sockaddr_in* from)
{
  int slot, i, n;

  if (!SameAddr(from, &host_addr))
    return;
  slot = Get8(p);
  if (state == BGNET_STARTING || state == BGNET_ACTIVE)
  {
    pkt_t a;                // already started: the host did not get our ack
    PutInit(&a, PKT_STARTACK);
    Put8(&a, my_slot);
    SendTo(&host_addr, &a);
    return;
  }
  if (state != BGNET_LOBBY)
    return;

  game.skill = Get8(p);
  game.episode = Get8(p);
  game.map = Get8(p);
  game.ff = Get8(p);
  game.complevel = Get8(p);
  for (i = 0; i < BGNET_MAX_PLAYERS; ++i)
  {
    game.present[i] = Get8(p);
    game.cls[i] = Get8(p);
    GetName(p, names[i]);
  }
  n = Get8(p);
  if (p->bad || n > (int) sizeof game.opts || slot < 1 || slot + my_nlocal > BGNET_MAX_PLAYERS)
    return;
  for (i = 0; i < n; ++i)
    game.opts[i] = (byte) Get8(p);
  game.optsize = n;
  for (i = 0; i < 16; ++i)
    game.sim[i] = (byte) Get8(p);
  if (p->bad)
    return;
  if (game.complevel != compatibility_level || !mbf21 || n != dsda_GameOptionSize())
  {
    pkt_t a;

    SetError("the host plays with different rules");
    SendBye(&host_addr, 0);
    Cleanup();
    (void) a;
    return;
  }

  my_slot = slot;
  state = BGNET_STARTING;
  {
    pkt_t a;

    PutInit(&a, PKT_STARTACK);
    Put8(&a, my_slot);
    SendTo(&host_addr, &a);
  }
  G_DeferedInitNew(game.skill, game.episode, game.map);
  M_ClearMenus();
}

static void DropPeer(int q, const char* why)
{
  if (state == BGNET_HOSTING)
  {
    memset(&peers[q], 0, sizeof peers[q]);
    return;
  }
  if (peers[q].dropped)
    return;
  peers[q].dropped = true;
  peers[q].used = false;
  {
    int k;

    for (k = 0; k < (peers[q].nlocal > 0 ? peers[q].nlocal : 1) && q + k < BGNET_MAX_PLAYERS; ++k)
    {
      drop_at[q + k] = next_tic[q + k];
      lprintf(LO_INFO, "bgnet: player %d dropped (%s), gone at tic %u\n", q + k + 1, why, drop_at[q + k]);
    }
  }
  dirty = true;
}

static void HandleTics(pkt_t* p, const struct sockaddr_in* from)
{
  int sender, i, nruns, r;
  unsigned acks[BGNET_MAX_PLAYERS], htic, drops[BGNET_MAX_PLAYERS];
  unsigned t_att[BGNET_MAX_PLAYERS], t_rel[BGNET_MAX_PLAYERS], t_spawn[BGNET_MAX_PLAYERS];
  int t_cls[BGNET_MAX_PLAYERS];
  char t_name[BGNET_MAX_PLAYERS][NAMELEN + 1];
  snap_t theirs;
  int from_peer = PeerFor(from);

  if (state == BGNET_STARTING)
  {
    if (from_peer >= 0)
      peers[from_peer].last_recv = NowMs();
    return;             // the first tics wait until our game is set up
  }
  if (state != BGNET_ACTIVE || from_peer < 0)
    return;

  sender = Get8(p);
  for (i = 0; i < BGNET_MAX_PLAYERS; ++i)
    acks[i] = Get32(p);
  htic = Get32(p);
  for (i = 0; i < SNAPN; ++i)
    theirs.v[i] = Get32(p);
  for (i = 0; i < BGNET_MAX_PLAYERS; ++i)
    drops[i] = Get32(p);
  for (i = 0; i < BGNET_MAX_PLAYERS; ++i)
  {
    t_att[i] = Get32(p);
    t_rel[i] = Get32(p);
    t_spawn[i] = Get32(p);
    t_cls[i] = Get8(p);
    t_name[i][0] = 0;
    GetName(p, t_name[i]);
  }
  nruns = Get8(p);
  if (p->bad || sender != (my_slot == 0 ? from_peer : 0) || (my_slot == 0 && sender == 0))
    return;

  peers[from_peer].last_recv = NowMs();
  peers[from_peer].start_acked = true;
  for (i = 0; i < BGNET_MAX_PLAYERS; ++i)
    if (acks[i] > peers[from_peer].ack[i])
      peers[from_peer].ack[i] = acks[i];

  CheckPeerHash(sender, htic, &theirs);

  if (my_slot != 0)
    for (i = 0; i < BGNET_MAX_PLAYERS; ++i)
    {
      // (an announcement older than the machine now in that slot belongs to the one before: a packet that was
      // already on its way when the slot was reused must not take the new player out again)
      if (drops[i] != NONE32 && !IsMine(i) && drop_at[i] == NONE32 &&
          !(att_applied[i] && att_at[i] != NONE32 && drops[i] < att_at[i]))
      {
        drop_at[i] = drops[i];
        dirty = true;
      }
      // the entry of a player into the game (our own included: the host decides when an observer comes in)
      if (t_spawn[i] != NONE32 && (spawn_at[i] == NONE32 || t_spawn[i] > spawn_at[i]) &&
          !(att_applied[i] && att_at[i] != NONE32 && t_spawn[i] < att_at[i]))
      {
        spawn_at[i] = t_spawn[i];
        spawn_cls[i] = t_cls[i];
        dirty = true;
      }
      if (IsMine(i))
        continue;
      // a machine is being attached to the game, or has been: adopt what is newer than what we know
      if (t_att[i] != NONE32 && (att_at[i] == NONE32 || t_att[i] > att_at[i]))
      {
        att_at[i] = t_att[i];
        att_rel[i] = NONE32;
        att_applied[i] = false;
        game.present[i] = true;
        if (t_name[i][0])
          snprintf(names[i], sizeof names[i], "%s", t_name[i]);
        dirty = true;
      }
      if (t_rel[i] != NONE32 && t_rel[i] == att_at[i] && att_rel[i] != t_rel[i])
      {
        att_rel[i] = t_rel[i];
        dirty = true;
      }
    }
  if (my_slot == 0 && late.slot == from_peer && peers[from_peer].late && late.comp && late.acked >= late.nchunks)
    LateRelease();                 // the joiner has put the game in and is running: the barrier comes down

  for (r = 0; r < nruns; ++r)
  {
    int pl = Get8(p), k, count;
    unsigned first = Get32(p);

    count = Get8(p);
    if (p->bad || pl >= BGNET_MAX_PLAYERS)
      return;
    for (k = 0; k < count; ++k)
    {
      ticcmd_t c;
      unsigned t = first + (unsigned) k;

      GetCmd(p, &c);
      if (p->bad)
        return;
      if (IsMine(pl) || !game.present[pl] || (my_slot == 0 && OwnerOf(pl) != sender))
        continue;
      if (peers[from_peer].dropped || (pl < BGNET_MAX_PLAYERS && drop_at[pl] != NONE32 && t >= drop_at[pl]))
        continue;
      if (t != next_tic[pl])
      {
        if (NetTrace() && pl >= 2)
        {
          static int nlog;
          if (nlog++ < 30)
            lprintf(LO_INFO, "TRACE slot %d: got tic %u, expected %u (run starts %u count %d), gametic %d\n", pl, t, next_tic[pl], first, count, gametic);
        }
        continue;                              // duplicate or a gap: wait for the resend
      }
      if (t - Horizon(pl) >= (unsigned) SPAN)
      {
        if (NetTrace() && pl >= 2)
        {
          static int nlog2;
          if (nlog2++ < 30)
            lprintf(LO_INFO, "TRACE slot %d: tic %u held back, horizon %u gametic %d\n", pl, t, Horizon(pl), gametic);
        }
        continue;                              // would overwrite a command somebody still needs
      }
      local_cmds[pl][t % BACKUPTICS] = c;
      next_tic[pl] = t + 1;
      dirty = true;
    }
  }
}

static void HandlePacket(pkt_t* p, const struct sockaddr_in* from)
{
  int type;

  if (p->len < 4 || p->d[0] != 'B' || p->d[1] != 'G' || p->d[2] != PKT_VERSION)
    return;
  type = p->d[3];
  p->pos = 4;

  switch (type)
  {
    case PKT_HELLO:
      HandleHello(p, from);
      break;

    case PKT_LOBBY:
      if ((state == BGNET_JOINING || state == BGNET_LOBBY) && SameAddr(from, &host_addr))
      {
        int slot = Get8(p), i;

        for (i = 0; i < BGNET_MAX_PLAYERS; ++i)
        {
          lobby_present[i] = Get8(p);
          lobby_class[i] = Get8(p);
          GetName(p, names[i]);
        }
        ff_setting = Get8(p) != 0;
        {
          int n = Get8(p), k, c, m = 0;

          server_name[0] = 0;
          for (k = 0; k < n; ++k)
          {
            c = Get8(p);
            if (c >= 32 && c < 127 && m < SERVERNAMELEN)
              server_name[m++] = (char) c;
          }
          server_name[m] = 0;
        }
        {
          int lo = Get8(p), hi = Get8(p);

          lobby_skill = Get8(p);
          lobby_done = Get8(p);
          lobby_level = (lo == 255 && hi == 255) ? -1 : lo | (hi << 8);
        }
        if (!p->bad)
        {
          my_slot = slot;
          peers[0].last_recv = NowMs();
          state = BGNET_LOBBY;
        }
      }
      break;

    case PKT_LATE:
      HandleLate(p, from);
      break;

    case PKT_SNAP:
      HandleSnap(p, from);
      break;

    case PKT_SNAPACK:
      HandleSnapAck(p, from);
      break;

    case PKT_JOINREQ:
      HandleJoinReq(p, from);
      break;

    case PKT_REJECT:
      if ((state == BGNET_JOINING || state == BGNET_LOBBY || state == BGNET_SYNC) && SameAddr(from, &host_addr))
      {
        int why = Get8(p);

        SetError(why == 1 ? "the game is full" :
                 why == 2 ? "the game has already started" :
                 why == 3 ? "the host has a different game version or begotten.wad" :
                 why == 4 ? "a game in progress takes one player per computer" :
                 why == 5 ? "the host cannot take a new player right now: try again in a moment" :
                 why == 6 ? "joining the game took too long" : "refused by the host");
        Cleanup();
      }
      break;

    case PKT_START:
      HandleStart(p, from);
      break;

    case PKT_STARTACK:
      {
        int q = PeerFor(from);

        if (q > 0)
        {
          peers[q].start_acked = true;
          peers[q].last_recv = NowMs();
        }
      }
      break;

    case PKT_TICS:
      HandleTics(p, from);
      break;

    case PKT_DIAG:
      if (state == BGNET_ACTIVE)
        HandleDiag(p, PeerFor(from));
      break;

    case PKT_BYE:
      {
        int q = PeerFor(from);
        int reason = Get8(p);

        if (q < 0)
          break;
        if (my_slot == 0 || state == BGNET_HOSTING)
        {
          lprintf(LO_INFO, "bgnet: player %d left\n", q + 1);
          DropPeer(q, "left");
        }
        else if (q == 0)
          EndSession(reason == 1 ? "You fell out of sync with the other players and were removed from the game."
                                 : "The host left the game.");
      }
      break;
  }
}

//
// automated two-instance test (environment: BEGOTTEN_NETTEST=host:<players> | join:<address>,
// BEGOTTEN_NETTEST_TICS=<tics>).  Plays scripted input, compares state hashes, prints the result.
//

static const char* test_spec;
static int test_tics = 700, test_init, test_started, test_done_tic = -1;
static unsigned test_final_hash;
static unsigned test_t0;
static int test_loss, test_leave = -1, test_desync = -1, test_delay, test_jitter, test_local = 1;

static void TestDriver(void)
{
  if (!test_init)
  {
    const char* t = getenv("BEGOTTEN_NETTEST_TICS");

    test_init = 1;
    test_spec = getenv("BEGOTTEN_NETTEST");
    if (t && atoi(t) > 0)
      test_tics = atoi(t);
    if (getenv("BEGOTTEN_NETTEST_LOCAL") && atoi(getenv("BEGOTTEN_NETTEST_LOCAL")) > 0)
      test_local = atoi(getenv("BEGOTTEN_NETTEST_LOCAL"));
    if (getenv("BEGOTTEN_NETTEST_LOSS"))
      test_loss = atoi(getenv("BEGOTTEN_NETTEST_LOSS"));
    if (getenv("BEGOTTEN_NETTEST_DESYNC") && my_slot == 1)
      test_desync = atoi(getenv("BEGOTTEN_NETTEST_DESYNC"));
    if (getenv("BEGOTTEN_NETTEST_DELAY"))
      test_delay = atoi(getenv("BEGOTTEN_NETTEST_DELAY"));
    if (getenv("BEGOTTEN_NETTEST_JITTER"))
      test_jitter = atoi(getenv("BEGOTTEN_NETTEST_JITTER"));
    if (getenv("BEGOTTEN_NETTEST_LEAVE"))
      test_leave = atoi(getenv("BEGOTTEN_NETTEST_LEAVE"));
    test_t0 = NowMs();
  }
  if (!test_spec)
    return;

  if (NowMs() - test_t0 > 150000u)
  {
    printf("NETTEST: timeout (state %d, tic %d)\n", (int) state, gametic);
    fflush(stdout);
    exit(3);
  }

  if (!test_started && state == BGNET_IDLE && gametic > 70 &&
      (strncmp(test_spec, "late:", 5) || NowMs() - test_t0 >= (getenv("BEGOTTEN_NETTEST_LATEMS") ? (unsigned) atoi(getenv("BEGOTTEN_NETTEST_LATEMS")) : 9000u)))
  {
    test_started = 1;
    if (getenv("BEGOTTEN_NETTEST_JOINAT"))
      test_joinat = (unsigned) atoi(getenv("BEGOTTEN_NETTEST_JOINAT"));
    if (!strncmp(test_spec, "host:", 5))
    {
      if (!bgnet_Host(test_local))
      {
        printf("NETTEST: host failed: %s\n", errtext);
        exit(2);
      }
    }
    else if (!strncmp(test_spec, "join:", 5))
    {
      bgnet_SetMyClass(1);
      if (!bgnet_Join(test_spec + 5, test_local))
      {
        printf("NETTEST: join failed: %s\n", errtext);
        exit(2);
      }
    }
    else if (!strncmp(test_spec, "late:", 5))
    {
      bgnet_SetMyClass(0);
      if (!bgnet_Join(test_spec + 5, 1))
      {
        printf("NETTEST: late join failed: %s\n", errtext);
        exit(2);
      }
    }
    return;
  }

  if (state == BGNET_HOSTING && !strncmp(test_spec, "host:", 5) && bgnet_LobbyPlayers() >= atoi(test_spec + 5))
  {
    int ep = 1, map = 1;
    if (num_episodes)
    {
      ep = episodes[0].start_episode;
      map = episodes[0].start_map;
    }
    bg_player_class[0] = 0;
    bgnet_HostStart(3, ep, map);
    return;
  }

  if (test_started && state == BGNET_IDLE && test_done_tic < 0)
  {
    printf("NETTEST: network session ended early: %s\n", errtext);
    fflush(stdout);
    exit(2);
  }

  if (state == BGNET_ACTIVE)
  {
    if (getenv("BEGOTTEN_NETTEST_EXIT") && gametic == atoi(getenv("BEGOTTEN_NETTEST_EXIT")) && gamestate == GS_LEVEL)
      G_ExitLevel(0);            // all machines leave the level at the same tic
    if (my_slot == 1 && getenv("BEGOTTEN_NETTEST_NOISE"))
      (void) P_Random(pr_misc);  // what rendering/sound/wipes do: must NOT cause a desync
    if (test_desync < 0 && getenv("BEGOTTEN_NETTEST_DESYNC") && my_slot == 1)
      test_desync = atoi(getenv("BEGOTTEN_NETTEST_DESYNC"));
    if (test_desync >= 0 && gametic == test_desync)
    {
      P_Random(pr_all_in_one);   // knock this machine out of sync on purpose
      lprintf(LO_INFO, "NETTEST: knocked out of sync at tic %d\n", gametic);
      test_desync = -1;
#ifndef _WIN32
      unsetenv("BEGOTTEN_NETTEST_DESYNC");
#endif
    }
    if (test_leave >= 0 && gametic >= test_leave)
    {
      printf("NETTEST: slot %d leaving at tic %d\n", my_slot, gametic);
      fflush(stdout);
      bgnet_Leave();
      exit(0);
    }
    if (test_done_tic < 0 && gametic >= test_tics)
    {
      test_done_tic = gametic;
      { snap_t sn; Snapshot(&sn); test_final_hash = sn.v[0]; }
    }
    if (test_done_tic >= 0 && gametic >= test_done_tic + 35)
    {
      {
        int i, n = 0;

        for (i = 0; i < MAX_MAXPLAYERS; ++i)
          n += playeringame[i];
          printf("NETTEST: waited for the other player %d times (own lead then %d tics on average)\n",
               stat_stalls, stat_stalls ? stat_stall_lead / stat_stalls : 0);
        printf("NETTEST: tic spacing: %d tics, %d over 45 ms, %d run back to back, longest %d ms\n",
               gap_count, gap_long, gap_burst, gap_max_ms);
        printf("NETTEST: slot %d tics %d hash %08x players %d desyncs %d pos %d,%d kills %d\n", my_slot, test_done_tic,
               test_final_hash, n, desyncs,
               players[my_slot].mo ? players[my_slot].mo->x >> 16 : -1,
               players[my_slot].mo ? players[my_slot].mo->y >> 16 : -1, players[my_slot].killcount);
        printf("NETTEST: fastest move in one tic: %d units\n", test_maxspeed);
        if (lj.late_joiner)
          printf("NETTEST: late joiner: attached at tic %u, entered the game at tic %u, class %d, late-flag %d\n", lj.attach_tic,
                 lj.joined_tic, players[my_slot].bgclass, bg_CoopJoinedLate(my_slot));
        printf("NETTEST: weapon drawn %d times, displayplayer %d consoleplayer %d movement_smooth %d frac calls %d\n", stat_wdraws, displayplayer, consoleplayer, (int) movement_smooth, stat_frac_calls);
      }
      fflush(stdout);
      exit(desyncs ? 1 : 0);
    }
  }
}

void bgnet_TestCmd(int slot, ticcmd_t* c)
{
  unsigned t = (unsigned) maketic;
  unsigned s = slot < 0 ? 0u : (unsigned) slot;

  if (!test_spec || !bgnet_InGame())
    return;
  if (getenv("BEGOTTEN_NETTEST_IDLE"))
  {
    c->forwardmove = c->sidemove = c->angleturn = 0;
    c->buttons = 0;
    c->ex.actions = 0;
    return;
  }
  {
    static int wallrun = -1;

    if (wallrun < 0)
      wallrun = getenv("BEGOTTEN_NETTEST_WALLRUN") != NULL;
    if (wallrun)
    {
      // run diagonally (forward + strafe at full speed), turning to a new direction every 20 tics:
      // sooner or later this runs along walls at all kinds of angles
      c->forwardmove = 50;
      c->sidemove = 40;
      c->angleturn = (t % 20 == 0) ? (short) (3000 + 700 * ((t / 20 + s) % 7)) : 0;
      c->buttons = 0;
      c->ex.actions = 0;
      return;
    }
  }
  if (getenv("BEGOTTEN_NETTEST_DUEL"))
  {
    // the players stand and shoot at each other (friendly fire must be on)
    int o;

    c->forwardmove = 0;
    c->sidemove = (((t / 30) + s) % 2) ? 20 : -20;
    c->angleturn = 0;
    c->buttons = BT_ATTACK;
    c->ex.actions = 0;
    for (o = 0; o < BGNET_MAX_PLAYERS; ++o)
      if (o != (int) s && playeringame[o] && players[o].mo && players[s].mo)
      {
        angle_t want = R_PointToAngle2(players[s].mo->x, players[s].mo->y, players[o].mo->x, players[o].mo->y);

        c->angleturn = (short) ((int) (want - players[s].mo->angle) >> 16);
        break;
      }
    if (t % 100 == 0)
      printf("DUEL t=%u slot %u hp %d/%d wp %d gs %d\n", t, s, players[0].health, players[1].health, players[s].readyweapon, (int) gamestate);
    if ((t + s) % 7 == 0)
      c->ex.actions |= 0x40;
    if (t % 50 == 25)
      c->ex.actions |= 0x100;    // weapon change, whatever that maps to
    return;
  }
  c->forwardmove = 50;
  c->sidemove = (((t / 40) + s) % 3) == 0 ? 30 : ((t / 40 + s) % 3) == 1 ? -30 : 0;
  c->angleturn = (short) ((((t / 14) + s) % 2) ? 900 : -700);
  c->buttons = 0;
  c->ex.actions = 0;
  if (t % 4 < 3)
    c->buttons |= BT_ATTACK;
  if ((t + s) % 9 == 0)
    c->ex.actions |= 0x40;      // alt fire
  if (t % 20 == 0)
    c->buttons |= BT_USE;
}

//
// per frame
//

typedef struct { pkt_t p; struct sockaddr_in from; unsigned due; int used; } delayed_t;
static delayed_t delayq[256];

void bgnet_Poll(void)
{
  unsigned now;
  int q;

  TestDriver();
  if (state == BGNET_IDLE || sock == BG_BADSOCK)
    return;

  // receive
  for (;;)
  {
    pkt_t p;
    struct sockaddr_in from;
    socklen_t flen = sizeof from;
    int n;

    n = (int) recvfrom(sock, (char*) p.d, MAXPKT, 0, (struct sockaddr*) &from, &flen);
    if (n <= 0)
      break;
    if (test_loss > 0 && rand() % 100 < test_loss)
      continue;                   // simulated packet loss (automated test only)
    p.len = n;
    p.pos = 0;
    p.bad = false;
    if (test_delay > 0)
    {
      int k;

      for (k = 0; k < 256; ++k)
        if (!delayq[k].used)
          break;
      if (k < 256)
      {
        delayq[k].p = p;
        delayq[k].from = from;
        delayq[k].due = NowMs() + (unsigned) test_delay + (unsigned) (test_jitter > 0 ? rand() % (test_jitter + 1) : 0);
        delayq[k].used = 1;
      }
      continue;               // simulated latency (automated test only)
    }
    HandlePacket(&p, &from);
    if (state == BGNET_IDLE)
      return;
  }

  if (test_delay > 0)
  {
    int k;

    for (k = 0; k < 256; ++k)
      if (delayq[k].used && delayq[k].due <= NowMs())
      {
        delayq[k].used = 0;
        HandlePacket(&delayq[k].p, &delayq[k].from);
        if (state == BGNET_IDLE)
          return;
      }
  }

  now = NowMs();

  if (state == BGNET_ACTIVE && stall_since && now - stall_since > 3000 && NetTrace())
  {
    static unsigned last_dump;

    if (now - last_dump > 2000)
    {
      int k;

      last_dump = now;
      lprintf(LO_INFO, "STALLDUMP slot %d: gametic %d maketic %d, stalled for %u ms, waiting for %d\n", my_slot, gametic, maketic, now - stall_since, stall_player);
      for (k = 0; k < BGNET_MAX_PLAYERS; ++k)
        lprintf(LO_INFO, "STALLDUMP   p%d ingame %d next %u drop %u spawn %u att %u rel %u applied %d present %d | ack[0..3] of peer: %u %u %u %u used %d late %d dropped %d\n",
                k, playeringame[k], next_tic[k], drop_at[k], spawn_at[k], att_at[k], att_rel[k], att_applied[k], game.present[k],
                peers[k].ack[0], peers[k].ack[1], peers[k].ack[2], peers[k].ack[3], peers[k].used, peers[k].late, peers[k].dropped);
      lprintf(LO_INFO, "STALLDUMP   late.slot %d barrier %d\n", late.slot, (int) BarrierUp());
    }
  }

  switch (state)
  {
    case BGNET_JOINING:
    case BGNET_LOBBY:
      if (now - last_hello >= 400)
      {
        pkt_t p;

        last_hello = now;
        PutInit(&p, PKT_HELLO);
        Put32(&p, ContentHash());
        Put8(&p, my_class);
        Put8(&p, my_nlocal);
        {
          int k;

          for (k = 0; k < my_nlocal; ++k)
            Put8(&p, k == 0 ? my_class : local_cls[k]);
        }
        {
          char nm[NAMELEN + 1];
          int k;

          for (k = 0; k < my_nlocal; ++k)
          {
            LocalName(k, nm);
            PutName(&p, nm);
          }
        }
        {
          int n = bg_ListMapCount(), i, nb = (n + 7) / 8;

          if (nb > 64) nb = 64;
          Put8(&p, nb);
          for (i = 0; i < nb; ++i)
          {
            int b, v = 0;

            for (b = 0; b < 8; ++b)
              if (i * 8 + b < n && bg_ListMapCompleted(i * 8 + b))
                v |= 1 << b;
            Put8(&p, v);
          }
        }
        SendTo(&host_addr, &p);
      }
      if (now - peers[0].last_recv > (state == BGNET_JOINING ? 6000u : (unsigned) LOBBY_TIMEOUT))
      {
        SetError(state == BGNET_JOINING ? "no answer from the host" : "lost the host");
        Cleanup();
      }
      break;

    case BGNET_HOSTING:
      for (q = 1; q < BGNET_MAX_PLAYERS; ++q)
      {
        if (!peers[q].used)
          continue;
        if (now - peers[q].last_recv > LOBBY_TIMEOUT)
        {
          lprintf(LO_INFO, "bgnet: player %d timed out in the lobby\n", q + 1);
          memset(&peers[q], 0, sizeof peers[q]);
        }
        else if (now - peers[q].last_send >= 500)
          SendLobby(q);
      }
      break;

    case BGNET_SYNC:
      if (now - lj.last_ack >= 100)
        SendSnapAck();
      if (now - peers[0].last_recv > 10000u)
      {
        SetError("lost the host");
        Cleanup();
      }
      break;

    case BGNET_STARTING:
    case BGNET_LATE:
    case BGNET_ACTIVE:
      if (my_slot != 0 && lj.late_joiner && state != BGNET_ACTIVE && now - lj.last_ack >= 100)
        SendSnapAck();
      if (my_slot != 0 && lj.late_joiner && state == BGNET_ACTIVE && lj.want && !playeringame[my_slot] &&
          spawn_at[my_slot] == NONE32 && now - lj.last_req >= 200)
      {
        pkt_t rq;

        lj.last_req = now;
        PutInit(&rq, PKT_JOINREQ);
        Put8(&rq, my_slot);
        Put8(&rq, lj.cls);
        SendTo(&host_addr, &rq);
      }
      if (maketic != last_make_seen)
      {
        last_make_seen = maketic;
        dirty = true;
      }
      if (my_slot == 0)
      {
        for (q = 1; q < BGNET_MAX_PLAYERS; ++q)
        {
          if (!peers[q].used || peers[q].dropped)
            continue;
          if (now - peers[q].last_recv > GAME_TIMEOUT)
          {
            DropPeer(q, "timed out");
            continue;
          }
          if (!peers[q].start_acked && now - last_start >= 100)
            SendStart(q);
          if (state == BGNET_ACTIVE && !peers[q].late && (dirty || now - peers[q].last_send >= 100))
            SendTics(q);
        }
        if (state == BGNET_ACTIVE)
          LateHostSend(now);
        if (now - last_start >= 100)
          last_start = now;
      }
      else
      {
        if (now - peers[0].last_recv > GAME_TIMEOUT)
        {
          EndSession("Lost the connection to the host.");
          return;
        }
        if (state == BGNET_ACTIVE && (dirty || now - peers[0].last_send >= 100))
          SendTics(0);
      }
      if (state == BGNET_ACTIVE && diag_until && now < diag_until && now - diag_last_send >= 250)
      {
        diag_last_send = now;
        if (my_slot == 0)
        {
          for (q = 1; q < BGNET_MAX_PLAYERS; ++q)
            if (peers[q].used && !peers[q].dropped && peers[q].bad_tic)   // only those that disagree with us
              SendDiag(q);
        }
        else
          SendDiag(0);
      }
      dirty = false;
      break;

    default:
      break;
  }
}

//
// starting
//

dboolean bgnet_HostStart(int skill, int episode, int map)
{
  int i;
  byte* end;

  {
    int q, others = 0;

    for (q = 1; q < BGNET_MAX_PLAYERS; ++q)
      others += peers[q].used;
    if (state != BGNET_HOSTING || !others)
    {
      SetError("nobody has joined yet");
      return false;
    }
  }
  if (!mbf21)
  {
    SetError("network games need the MBF21 rules");
    return false;
  }

  FillSubNames();
  memset(&game, 0, sizeof game);
  game.skill = skill;
  game.episode = episode;
  game.map = map;
  game.ff = dsda_IntConfig(dsda_config_begotten_friendly_fire) != 0;
  game.complevel = compatibility_level;
  for (i = 0; i < BGNET_MAX_PLAYERS; ++i)
  {
    int c = 0;

    game.present[i] = HostSlot(i, NULL, NULL, &c);
    game.cls[i] = i == 0 ? bg_player_class[0] : c;
  }

  rngseed = I_GetRandomTimeSeed() ^ 0x5bd1e995u;
  end = dsda_WriteOptions21(game.opts);
  game.optsize = (int) (end - game.opts);
  SimConfigGather(game.sim);

  my_slot = 0;
  memset(drop_at, 0xff, sizeof drop_at);
  LateFree();
  LateReset();
  for (i = 1; i < BGNET_MAX_PLAYERS; ++i)
  {
    peers[i].start_acked = false;
    memset(peers[i].ack, 0, sizeof peers[i].ack);
    peers[i].last_recv = NowMs();
  }
  state = BGNET_STARTING;
  host_start_pending = false;
  last_start = 0;
  for (i = 1; i < BGNET_MAX_PLAYERS; ++i)
    if (peers[i].used)
      SendStart(i);
  G_DeferedInitNew(skill, episode, map);
  return true;
}

dboolean bgnet_PendingApply(void)
{
  return state == BGNET_STARTING;
}

void bgnet_ApplyNewGame(void)
{
  int i, k, n;
  ticcmd_t keep[BACKUPTICS];

  int old_row = consoleplayer;

  if (lj.late_joiner)
  {
    // joining a running game: this starts a placeholder level (the level the host is in, at its beginning) so that
    // everything is set up the usual way; the host's snapshot is put into it by bgnet_LateTick
    for (i = 0; i < MAX_MAXPLAYERS; ++i)
      playeringame[i] = i < BGNET_MAX_PLAYERS && game.present[i];
    for (i = 0; i < BGNET_MAX_PLAYERS; ++i)
      if (game.present[i])
        bg_player_class[i] = game.cls[i] < bg_ClassCount() ? game.cls[i] : 0;
    netgame = true;
    deathmatch = 0;
    bg_made_netgame = false;
    SimConfigApply(game.sim);
    consoleplayer = displayplayer = my_slot;
    {
      extern dboolean in_game;
      dboolean was_in_game = in_game;

      in_game = false;
      dsda_ReadOptions21(game.opts);
      in_game = was_in_game;
    }
    gametic = 0;
    maketic = 0;
    state = BGNET_LATE;
    return;
  }

  for (i = 0; i < MAX_MAXPLAYERS; ++i)
    playeringame[i] = i < BGNET_MAX_PLAYERS && game.present[i];
  for (i = 0; i < BGNET_MAX_PLAYERS; ++i)
    if (game.present[i])
      bg_player_class[i] = game.cls[i] < bg_ClassCount() ? game.cls[i] : 0;
  netgame = true;
  deathmatch = 0;
  bg_made_netgame = false;
  SimConfigApply(game.sim);
  consoleplayer = displayplayer = my_slot;

  {
    extern dboolean in_game;
    dboolean was_in_game = in_game;

    in_game = false;               // the options reader must not walk the old level's things
    dsda_ReadOptions21(game.opts); // the host's rules and random seed
    in_game = was_in_game;
  }

  // every machine counts tics from the same zero; commands already made become tics 1, 2, ...
  // Commands made before the start are dropped. Our own commands run `delay` tics after they were
  // made (a small buffer that absorbs network jitter), so the first `delay` tics are empty.
  (void) old_row; (void) keep; (void) k;
  memset(local_cmds, 0, sizeof(ticcmd_t) * MAX_MAXPLAYERS * BACKUPTICS);
  n = dsda_IntConfig(dsda_config_begotten_net_delay);
  if (n < 0) n = 0;
  if (n > 4) n = 4;
  target_lead = n < 1 ? 1 : n;
  last_interval_us = TIC_US;
  gametic = 0;
  maketic = 1 + n;
  boom_basetic = true_basetic = 0;
  force_singletics_to = 0;
  if (S_sfx)
    for (i = 0; i < num_sfx; ++i)
      S_sfx[i].parallel_tic = 0;

  for (i = 0; i < BGNET_MAX_PLAYERS; ++i)
  {
    next_tic[i] = 1;
    drop_at[i] = NONE32;
    LateReset();
    memset(peers[i].ack, 0, sizeof peers[i].ack);
    for (k = 0; k < BGNET_MAX_PLAYERS; ++k)
      peers[i].ack[k] = 1;
  }
  memset(hashes, 0, sizeof hashes);
  last_make_seen = 0;
  { int k; for (k = 0; k < TRACEN; ++k) bgtrace[k].tic = -1; }
  bgnet_tracing = 1;
  diag_tic = NONE32;
  diag_until = 0;
  gap_last_us = 0;
  gap_count = gap_long = gap_burst = gap_max_ms = 0;
  next_due_us = Usec();
  my_hash_tic = NONE32;
  desyncs = 0;
  desync_what[0][0] = desync_what[1][0] = desync_what[2][0] = 0;
  stall_since = 0;
  dirty = true;
  state = BGNET_ACTIVE;
  lprintf(LO_INFO, "bgnet: network game started, I am player %d\n", my_slot + 1);
}

//
// self test (the packet code and the lockstep arithmetic; the two-instance test covers the rest)
//

#define NCHECK(cond, msg) do { if (!(cond)) { lprintf(LO_ERROR, "SELFTEST FAIL: %s\n", msg); ++fails; } } while (0)

void dsda_BgNetSelfTest(void)
{
  int fails = 0;
  pkt_t p;
  ticcmd_t a, b;

  memset(&a, 0, sizeof a);
  a.forwardmove = -50; a.sidemove = 40; a.angleturn = -1234; a.buttons = 0x85;
  a.lookfly = 3; a.arti = 7; a.ex.actions = 0x40; a.ex.look = -300; a.ex.save_slot = 5;
  PutInit(&p, PKT_TICS);
  PutCmd(&p, &a);
  p.pos = 4;
  GetCmd(&p, &b);
  NCHECK(!p.bad, "net: a command fits in a packet");
  NCHECK(b.forwardmove == -50 && b.sidemove == 40 && b.angleturn == -1234 && b.buttons == 0x85, "net: move fields survive");
  NCHECK(b.lookfly == 3 && b.arti == 7 && b.ex.actions == 0x40 && b.ex.look == -300, "net: extra fields survive");
  NCHECK(b.ex.save_slot == 0, "net: save slots do not cross the network");

  p.pos = p.len;
  Get8(&p);
  NCHECK(p.bad, "net: reading past the packet is caught");

  {
    unsigned a32;

    PutInit(&p, PKT_TICS);
    Put32(&p, 0xdeadbeefu);
    p.pos = 4;
    a32 = Get32(&p);
    NCHECK(a32 == 0xdeadbeefu, "net: 32 bit values survive");
  }

  // lockstep arithmetic with made-up state
  {
    bgnet_state_t st = state;
    int slot = my_slot, gt = gametic, mt = maketic, ps0 = playeringame[0], ps1 = playeringame[1];
    unsigned nt0 = next_tic[0], nt1 = next_tic[1], d1 = drop_at[1];

    unsigned long long due0 = next_due_us;

    next_due_us = 0;               // the cadence clock says a tic is long overdue
    state = BGNET_ACTIVE;
    my_slot = 0;
    gametic = 10;
    maketic = 14;
    playeringame[0] = playeringame[1] = true;
    next_tic[1] = 12;
    drop_at[1] = NONE32;
    NCHECK(bgnet_ReadyTics() == 2, "net: ready tics are limited by the slowest player");
    next_tic[1] = 20;
    NCHECK(bgnet_ReadyTics() == 2, "net: at most two tics run at once, however many are ready");
    next_tic[1] = 10;
    NCHECK(bgnet_ReadyTics() == 0, "net: no tic runs without the other player's command");
    drop_at[1] = 10;
    NCHECK(bgnet_ReadyTics() == 2, "net: a player who left no longer holds the game");
    drop_at[1] = 12;
    next_tic[1] = 11;
    NCHECK(bgnet_ReadyTics() == 1, "net: a leaving player's last commands still count");

    next_due_us = due0;
    state = st; my_slot = slot; gametic = gt; maketic = mt;
    playeringame[0] = ps0; playeringame[1] = ps1;
    next_tic[0] = nt0; next_tic[1] = nt1; drop_at[1] = d1;
  }

  // which machine plays which slot (host with two local players, a client with two, a client with one)
  {
    bgnet_state_t st = state;
    int slot = my_slot, nl = my_nlocal;
    peer_t keep[BGNET_MAX_PLAYERS];

    memcpy(keep, peers, sizeof keep);
    memset(peers, 0, sizeof peers);
    state = BGNET_ACTIVE;
    my_slot = 0;
    my_nlocal = 2;
    peers[2].used = true; peers[2].nlocal = 2;
    NCHECK(OwnerOf(0) == 0 && OwnerOf(1) == 0, "net: the host owns its local players");
    NCHECK(OwnerOf(2) == 2 && OwnerOf(3) == 2, "net: a client owns all of its local players' slots");
    peers[2].nlocal = 1;
    NCHECK(OwnerOf(3) == -1, "net: a slot nobody plays has no owner");
    my_slot = 2;
    my_nlocal = 2;
    NCHECK(IsMine(2) && IsMine(3) && !IsMine(1) && OwnerOf(1) == 0, "net: a client sees its own slots and the host's");
    memcpy(peers, keep, sizeof keep);
    state = st; my_slot = slot; my_nlocal = nl;
  }

  if (fails)
  {
    lprintf(LO_ERROR, "net selftest: %d failure(s)\n", fails);
    exit(1);
  }
  lprintf(LO_INFO, "net selftest: ok\n");
}


// Render-side weapon diagnostics: remembers the last frames drawn for the weapon sprite and dumps them to the log
// when the drawn position jumps although the simulation's did not (the "weapon keeps being raised" report).
typedef struct
{
  unsigned ms;
  int gt, state, tics, sim_sx, sim_sy, draw_sx, draw_sy, lump, ready, pending, frac;
} bg_wdraw_t;

void bgnet_NoteWeaponDraw(const void* psp_v, int draw_sx, int draw_sy, int lump)
{
  static bg_wdraw_t ring[32];
  static int head, count, dumps, have_prev, prev_draw_sy, prev_draw_sx, prev_sim_sy, prev_gt;
  static unsigned last_ms;
  const pspdef_t* psp = (const pspdef_t*) psp_v;
  bg_wdraw_t* r;
  player_t* pl;
  int sim_sy;

  if (!netgame || !psp || !psp->state)
    return;
  pl = &players[displayplayer];
  ++stat_wdraws;
  sim_sy = psp->sy >> FRACBITS;
  r = &ring[head];
  r->ms = NowMs();
  r->gt = gametic;
  r->state = (int) (psp->state - states);
  r->tics = psp->tics;
  r->sim_sx = psp->sx >> FRACBITS;
  r->sim_sy = sim_sy;
  r->draw_sx = draw_sx >> FRACBITS;
  r->draw_sy = draw_sy >> FRACBITS;
  r->lump = lump;
  r->ready = pl->readyweapon;
  r->pending = pl->pendingweapon;
  r->frac = (int) (((long long) bgnet_Frac() * 100) >> FRACBITS);
  head = (head + 1) & 31;
  if (count < 32)
    ++count;

  if (have_prev && dumps < 8 &&
      ((abs(r->draw_sy - prev_draw_sy) > 10 && abs(sim_sy - prev_sim_sy) <= 2) ||
       (r->gt == prev_gt && (r->draw_sy != prev_draw_sy || r->draw_sx != prev_draw_sx))))
  {
    int i;

    ++dumps;
    lprintf(LO_INFO, "WEAPONDRAW jump #%d (displayplayer %d, frame gap %u ms): last %d frames\n", dumps, displayplayer, r->ms - last_ms, count);
    for (i = 0; i < count; ++i)
    {
      bg_wdraw_t* q = &ring[(head - count + i) & 31];

      lprintf(LO_INFO, "  ms %u gt %d frac %d state %d tics %d sim %d,%d draw %d,%d lump %d ready %d pending %d\n",
              q->ms % 100000, q->gt, q->frac, q->state, q->tics, q->sim_sx, q->sim_sy, q->draw_sx, q->draw_sy, q->lump, q->ready, q->pending);
    }
  }
  have_prev = 1;
  prev_draw_sy = r->draw_sy;
  prev_draw_sx = r->draw_sx;
  prev_gt = r->gt;
  prev_sim_sy = sim_sy;
  last_ms = r->ms;
}


