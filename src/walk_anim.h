#ifndef _WALK_ANIM_H
#define _WALK_ANIM_H

#if defined(ENABLE_WALK_ANIM)
// The tile to draw for the party on foot at the middle of the map, as
// ULTIMA_56ac_DrawMap draws it: one of the Avatar's walking frames for a
// moment after each step, otherwise the actor tile it was given.
int WALK_AvatarTile(int actorTile);
#endif

#endif
