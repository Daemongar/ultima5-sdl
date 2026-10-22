#include "common/common.h"
#include "vars.h"

#include "automap.h"

#if defined(ENABLE_AUTOMAP)

#include <stdio.h>
#include <string.h>

#include "common/file.h"

#if defined(OS_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

// The dungeon map (F10): which cells of each dungeon level the party has
// seen. The dungeon code reports the cells its first-person view draws (so
// only with light), the cell the party stands on and any wall it walks into.
// The map screen draws seen cells from the dungeon as it is now.
//
// Kept in SAVEGAME/AUTOMAP.DAT, one bit per cell, outside the save slots:
// like the journal, it is what the player knows, so loading an older game
// doesn't take it back.

#define AUTOMAP_FILE "SAVEGAME" PATH_SEP "AUTOMAP.DAT"

// One byte per row; bit x is cell x.
static u8 s_seen[AUTOMAP_DUNGEONS][AUTOMAP_LEVELS][AUTOMAP_SIZE];
static bool s_loaded;
static bool s_dirty;

static void Load(void)
{
    FILE* stream;

    s_loaded = true;
    memset(s_seen, 0, sizeof(s_seen));

    stream = FILE_Open(AUTOMAP_FILE, "rb");
    if (!stream)
        return;
    if (fread(s_seen, 1, sizeof(s_seen), stream) != sizeof(s_seen))
        memset(s_seen, 0, sizeof(s_seen));
    fclose(stream);
}

static void EnsureLoaded(void)
{
    if (!s_loaded)
        Load();
}

static void Save(void)
{
    FILE* stream;

    u5_mkdir("SAVEGAME");
    stream = FILE_Open(AUTOMAP_FILE, "wb");
    if (!stream)
        return;
    fwrite(s_seen, 1, sizeof(s_seen), stream);
    fclose(stream);
    s_dirty = false;
}

int AUTOMAP_CurrentDungeon(void)
{
    if (D_5893_map_id < 0x21 || D_5893_map_id > 0x28)
        return -1;
    return D_5893_map_id - 0x21;
}

void AUTOMAP_See(int x, int y)
{
    int dungeon = AUTOMAP_CurrentDungeon();
    u8* row;
    u8 bit;

    if (dungeon < 0 || D_5895_map_level >= AUTOMAP_LEVELS)
        return;

    EnsureLoaded();

    row = &s_seen[dungeon][D_5895_map_level][y & 7];
    bit = (u8)(1 << (x & 7));
    if (!(*row & bit))
    {
        *row |= bit;
        s_dirty = true;
    }
}

void AUTOMAP_Flush(void)
{
    if (s_dirty)
        Save();
}

bool AUTOMAP_IsSeen(int dungeon, int level, int x, int y)
{
    EnsureLoaded();
    return (s_seen[dungeon][level][y & 7] & (1 << (x & 7))) != 0;
}

bool AUTOMAP_LevelSeen(int dungeon, int level)
{
    EnsureLoaded();
    for (int y = 0; y < AUTOMAP_SIZE; y++)
    {
        if (s_seen[dungeon][level][y])
            return true;
    }
    return false;
}

void AUTOMAP_Clear(void)
{
    s_loaded = true;
    memset(s_seen, 0, sizeof(s_seen));
    Save();
}

#endif
