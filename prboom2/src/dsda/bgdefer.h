// Begotten: wall columns and floor/ceiling spans are not drawn as the BSP walk finds them but queued, and then drawn on
// several threads at once (a strip of columns or a band of rows to a task). Pixels of a wall and of a plane never
// overlap, so the order between them does not matter; what is drawn over them (sprites, masked textures) waits for the flush.
#ifndef __BGDEFER_H__
#define __BGDEFER_H__

#include "r_draw.h"

// is this frame's wall and plane drawing queued? (software renderer, more than one thread)
dboolean bgd_Active(void);

// a wall column, as `bg_LitColumn(colfunc, dcvars, scale)` or `colfunc(dcvars)` would draw it now. `wall` is the
// bg_WallSnapshot() of its seg (NULL when the seg is not lit).
void bgd_Column(R_DrawColumn_f colfunc, const draw_column_vars_t* dcvars, fixed_t scale, const void* wall);

// a span of the plane whose bg_LightSetPlane(height, lightlevel) was last called
void bgd_Plane(fixed_t height, int lightlevel);
void bgd_Span(const draw_span_vars_t* dsvars);

// draws what is queued (does nothing when nothing is)
void bgd_Flush(void);

#endif
