//
// Begotten: network co-op (lockstep over UDP).
//
// Every machine runs the whole simulation and only the players' commands cross the network, as in
// Doom's original netcode.  The host is the hub: clients talk to the host only, and the host relays
// everybody's commands, so only the host needs a reachable port (UDP, default 5029).
//
// Sequence:  host opens a lobby -> clients join (class is chosen in the lobby) -> the host picks
// the game in the usual New Game menus -> a START packet carries skill, map, friendly fire, the
// game options and the random seed -> everybody starts the same level at tic 0.
//
#ifndef __BG_NET__
#define __BG_NET__

#include "doomtype.h"
#include "d_ticcmd.h"
#include "m_fixed.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
  BGNET_IDLE,
  BGNET_HOSTING,   // host lobby
  BGNET_JOINING,   // asked a host, no answer yet
  BGNET_LOBBY,     // joined a host's lobby
  BGNET_STARTING,  // the game is about to be set up
  BGNET_ACTIVE,    // playing in lockstep
  BGNET_SYNC,      // joining a game in progress: receiving a snapshot of it from the host
  BGNET_LATE,      // ... and its level is set up, waiting for the snapshot to be put in
} bgnet_state_t;

#define BGNET_MAX_PLAYERS 4
#define BGNET_DEFAULT_PORT 5029

bgnet_state_t bgnet_State(void);
dboolean bgnet_InGame(void);       // starting or playing a network game
dboolean bgnet_Lockstep(void);     // tics are exchanged now
dboolean bgnet_IsHost(void);
dboolean bgnet_FriendlyFire(void); // the host's setting (valid in a network game)

// lobby
dboolean bgnet_Host(int nlocal);             // nlocal: players on this machine (split-screen); false: the port could not be opened (see bgnet_Error)
dboolean bgnet_Join(const char* address, int nlocal);    // "host", "host:port", "1.2.3.4:5029"
void bgnet_Leave(void);                      // leave the lobby or the game
const char* bgnet_Error(void);               // last problem, or ""
int bgnet_LobbyPlayers(void);                // players in the lobby, including me
dboolean bgnet_LobbySlot(int slot, int* cls); // is slot in the lobby, and its class
int bgnet_MySlot(void);
const char* bgnet_PlayerName(int slot);     // the name, or "PLAYER n" if none was typed
dboolean bgnet_SlotCompleted(int slot, int index); // host: has that player finished map #index (map list order)
void bgnet_SetMyClass(int cls);
void bgnet_SetLocalClass(int index, int cls);  // class of this machine's local player #index (0 = the first)
int bgnet_LocalCount(void);                  // players on this machine (1 outside network games)
int bgnet_LocalFirstSlot(void);              // the slot of the first one
dboolean bgnet_IsMineSlot(int slot);         // is that player on this machine
void bgnet_StatusLine(char* out, int size);  // one line for the lobby screen
const char* bgnet_ServerName(void);          // client: the name the host gave its game
void bgnet_SetLobbyInfo(int level_index, int skill);   // host: current game setup selection
int bgnet_LobbyLevel(void);        // client: the host's chosen map (map list index), -1 if none
int bgnet_LobbySkill(void);
dboolean bgnet_LobbyDone(int slot);  // client: has this slot finished the chosen map
dboolean bgnet_LobbyFriendlyFire(void);      // client: the host's friendly fire setting
void bgnet_LocalIP(char* out, int size);     // this computer's address in the local network, or ""
dboolean bgnet_HostStartPending(void);
void bgnet_SetHostStartPending(dboolean on);

// the host picked skill/episode/map: send everybody off and start
dboolean bgnet_HostStart(int skill, int episode, int map);

// called by the engine
void bgnet_Poll(void);                 // receive and send; called every frame
dboolean bgnet_PendingApply(void);     // G_InitNew is about to start the network game
void bgnet_ApplyNewGame(void);         // set players, classes, options, tic counters
void bgnet_LateTick(void);             // once per frame, between tics: join barrier, snapshot, restore
dboolean bgnet_IsObserver(void);       // joined a running game and not playing yet (watching)
int bgnet_ObserverClass(void);         // the character that pressing Use will join as
dboolean bgnet_ObserverJoining(void);  // Use was pressed: the host has been asked
dboolean bgnet_CanMake(void);          // room to build another local command
int bgnet_ReadyTics(void);
fixed_t bgnet_Frac(void);               // 0..FRACUNIT: progress between tics, for smooth rendering
void bgnet_PreTic(void);               // before each tic (removes players who left)
void bgnet_AfterTic(void);             // after each tic (state check)
void bgnet_DrawStatus(void);
void bgnet_DrawBanner(void);           // "waiting for players" while a lobby is open           // "waiting for player 2" etc.

void bgnet_TestCmd(int slot, ticcmd_t* cmd);     // scripted input for the automated test
void dsda_BgNetSelfTest(void);

#ifdef __cplusplus
}
#endif


void bgnet_NoteWeaponDraw(const void* psp_v, int draw_sx, int draw_sy, int lump);

#endif
