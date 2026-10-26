#ifndef _GRAPHICS_GLIDE_H
#define _GRAPHICS_GLIDE_H

#if defined(ENABLE_GLIDE)

#include "common/common.h"

// Called by ULTIMA_56ac_DrawMap before it draws: people, monsters and
// (in combat) the party who have moved a square since the last drawing
// start to glide there.
void GLIDE_MapDrawn(void);

// Draws the glides into a composed frame (ARGB pixels, pitch in pixels):
// each mover's new square shows its ground, and the mover is drawn on the
// way there. Returns true while any is gliding.
bool GLIDE_Apply(u32* frame, int pitch, const u32* palette);

#endif

#endif
