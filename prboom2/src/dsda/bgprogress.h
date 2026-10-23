//
// Begotten: campaign progress and unlockable classes.
// Licensed under the GNU GPL v2 or later, like the rest of the engine.
//
// The engine remembers which maps the player has finished (single player or co-op alike, any
// difficulty) in a small text file in the user's data folder. A class marked "unlock campaign" in
// BGCLASS becomes available when every required map has been finished. The required maps are the
// non-secret maps of every episode: the chain of "Next" maps from each episode's first map
// (UMAPINFO), or, if the WAD has a BGPROGRESS lump, exactly the maps listed there.
//

#ifndef __DSDA_BGPROGRESS__
#define __DSDA_BGPROGRESS__

#include "doomtype.h"

#define BG_PROGRESS_MAX_MAPS 512

void dsda_InitBgProgress(void);                       // loads the profile (after the WADs are loaded)
void bg_RecordMapCompleted(const char* lump);         // called when a map is exited; ignores demos and cheaters
dboolean bg_MapIsRequired(const char* lump);
dboolean bg_MapCompleted(const char* lump);
void bg_CampaignCounts(int* done, int* total);        // required maps finished / required maps
dboolean bg_CampaignComplete(void);

// the maps listed in UMAPINFO, in order (for the network level select)
int bg_ListMapCount(void);
const char* bg_ListMapName(int index);
const char* bg_ListMapTitle(int index);
dboolean bg_ListMapCompleted(int index);

// self test support
void bg_ProgressSetPath(const char* path);            // profile file (NULL: the default location)
void bg_ProgressReset(void);                          // forget everything (does not touch the file)
void dsda_BgProgressSelfTest(void);

#endif
