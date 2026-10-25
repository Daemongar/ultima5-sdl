#ifndef _GRAPHICS_LIGHTING_H
#define _GRAPHICS_LIGHTING_H

#if defined(ENABLE_LIGHTING)

#include "common/common.h"

// Lights the map in a composed frame (ARGB pixels, pitch in pixels): the sky's
// color for the time of day, a soft circle of the party's light, and glows
// around fires and lamps. slideX/slideY is how far smooth scrolling has the
// map shifted, in frame pixels. Returns true if a light flickers, so the
// frame should be presented again soon.
bool LIGHTING_Apply(u32* frame, int pitch, int slideX, int slideY);
#endif

#endif
