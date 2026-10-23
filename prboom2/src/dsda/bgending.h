//
// Begotten: the ending of an episode. A picture shown at full brightness, then dimmed, with a
// text scrolling over it, then back to the title. Driven by lumps, the same for every episode:
//     BGPIC<n>  picture of episode n  (BGPIC if there is none for n): a patch or a raw 320x200 screen
//     BGTXT<n>  text of episode n     (BGTXT if there is none for n): plain text, one line per line
//                                     a line starting with '#' is a heading, an empty line is a gap
//     BGMUS<n>  music of episode n    (BGMUS if there is none for n): a music lump, played looping
//                                     (no music lump: the stock intermission music keeps playing)
// If the wad has none of the picture / text lumps the stock finale plays unchanged.
//

#ifndef __DSDA_BGENDING__
#define __DSDA_BGENDING__

#include "doomtype.h"

dboolean bg_EndingStart(int episode);  // takes over the finale when the lumps exist
dboolean bg_EndingActive(void);
void bg_EndingTicker(void);
void bg_EndingDrawer(void);
void dsda_BgEndingSelfTest(void);

#endif
