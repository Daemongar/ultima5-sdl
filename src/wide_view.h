#ifndef _WIDE_VIEW_H
#define _WIDE_VIEW_H

#if defined(TARGET_SDL)
// Work out the tiles for the wide view's extra columns (after the map is drawn).
void WIDE_UpdateExtraTiles(void);
void WIDE_PutTerrainUnderActor(int x, int y, int actorTile);
void WIDE_PutGroundUnderTerrain(int x, int y, int mapTile);
// The tiles the map draws under an actor at (x, y) in the 11x11 view: the
// terrain, and the ground under it when the terrain is see-through (or -1).
void WIDE_TilesUnderActor(int x, int y, int* ground, int* terrain);
#endif

#endif
