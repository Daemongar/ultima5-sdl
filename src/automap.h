#ifndef _AUTOMAP_H
#define _AUTOMAP_H

#if defined(ENABLE_AUTOMAP)

#define AUTOMAP_DUNGEONS 8
#define AUTOMAP_LEVELS 8
#define AUTOMAP_SIZE 8 // cells across and down a dungeon level

// The current dungeon (0-7), or -1 outside one.
int AUTOMAP_CurrentDungeon(void);

// Marks a cell of the current dungeon level as seen.
void AUTOMAP_See(int x, int y);

// Writes what was seen since the last call to SAVEGAME/AUTOMAP.DAT.
void AUTOMAP_Flush(void);

bool AUTOMAP_IsSeen(int dungeon, int level, int x, int y);
bool AUTOMAP_LevelSeen(int dungeon, int level);

// Forget every map; a new character starts with none.
void AUTOMAP_Clear(void);

#endif

#endif
