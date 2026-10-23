//
// Begotten: local split-screen (2-4 players on one machine).
//
// Players 1-4 are ordinary players (playeringame[]); this module decides how many there are
// (config begotten_local_players), where each one's view goes on the screen, and reads one game
// controller per extra player.  Player 1 keeps keyboard, mouse and the first controller.
//
#ifndef __BG_SPLIT__
#define __BG_SPLIT__

#include "doomtype.h"
#include "d_ticcmd.h"

#ifdef __cplusplus
extern "C" {
#endif

// set up playeringame[] for a new game (called from G_InitNew)
extern int bg_split_request;
void bg_SplitNewGame(void);

// more than one local player in the level
dboolean bg_SplitActive(void);
int bg_ViewCount(void);        // views drawn this frame (split-screen players, or the players a dead machine watches)
int bg_ViewSlot(int view);     // the player shown in that view
dboolean bg_SplitLocalGame(void);
int bg_SplitPadPlayer(void);          // 0, or the index (1-3) of the player the first controller drives
dboolean bg_SplitPadRoutedAway(void); // player 1 ignores the first controller
int bg_SplitViews(void);
int bg_SplitCount(void);              // local players on this machine
int bg_SplitFirst(void);              // slot of the first one (0 unless in a network game)

// the screen rectangle of view v (v = 0 is the first local player)
void bg_SplitRect(int v, int* x, int* y, int* w, int* h);

// extra local players' controllers: k is the local player number (1-3)
void bg_SplitBuildCmd(int k, ticcmd_t* cmd);

// controller setup screen
int bg_SplitPadCount(void);   // game controllers connected
void bg_SplitDeviceLabel(int k, int players, int first_pad_player, char* out, int size);

// compact status text in the player's view
void bg_SplitDrawHud(int v);

// the player's class as chosen for players 2-4
int bg_SplitClassFor(int player);

void dsda_BgSplitSelfTest(void);

#ifdef __cplusplus
}
#endif

#endif
