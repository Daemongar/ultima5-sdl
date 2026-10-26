#include "common/common.h"
#include "common/options.h"
#include "funcs.h"
#include "macros.h"
#include "tiles.h"
#include "vars.h"

#include "graphics/grap_buf.h"
#include "wide_view.h"

#if defined(TARGET_SDL)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/file.h"

// Wide view, phase 2: which tiles to show in the three extra columns on each
// side of the map (see ComposeFrame in graphics/grap_sdl.c). The game's own
// 11x11 view, with its line of sight and lighting, is left untouched; the extra
// squares get their own, simpler test using the same ingredients:
//   - lighting: a square is lit if its squared distance from the party is at
//     most the light level D_58a5 (as ULTIMA_6ff0/ULTIMA_5a28 do for 11x11).
//     In full daylight (50, which exactly covers the original 11x11) the extra
//     columns are treated as lit, or the wide view would be a circle.
//   - line of sight: no view-blocking tile (D_6a86, see ULTIMA_5dfe) on the
//     straight line between the party and the square.
// Only outdoors and in towns; dungeons and combat keep black strips. Past a
// town map's edge the stand-in tile is shown, as in the game's own view.

int g_wideExtraTiles[WIDE_EXTRA_ROWS][2 * WIDE_EXTRA_COLUMNS];
int g_wideExtraUnder[WIDE_EXTRA_ROWS][2 * WIDE_EXTRA_COLUMNS];
bool g_wideExtraTilesReady; // false until the map has been drawn once
// The same squares as LOOK2.DAT entries (map tile, or 0x100 + creature), or
// -1, for the names under the mouse (hover.c).
int g_wideExtraLook[WIDE_EXTRA_ROWS][2 * WIDE_EXTRA_COLUMNS];

#define DAYLIGHT 0x32

// The overworld is kept in memory as a 32x32 block around the party (see
// ULTIMA_4402_GetTileAddr and OUTSUBS_01b4); the extra columns reach past it
// near its edges. For those squares, read the whole world map once, in the
// same chunk layout as the files: BRIT.DAT holds the land chunks listed in
// D_3876 (0xff is open sea, tile 1), UNDER.DAT all 256 chunks in order.
static byte* s_worlds[2];

static const byte* LoadWorld(bool underworld)
{
    byte** world = &s_worlds[underworld ? 1 : 0];
    if (*world != NULL)
        return *world;

    byte* data = malloc(0x10000);
    if (data == NULL)
        return NULL;

    FILE* fp = FILE_Open(underworld ? "UNDER.DAT" : "BRIT.DAT", "rb");
    for (int chunk = 0; chunk < 0x100; chunk++)
    {
        byte* dst = &data[chunk * 0x100];
        int fileChunk = underworld ? chunk : D_3876[chunk];

        memset(dst, 1, 0x100);
        if (fp != NULL && fileChunk != 0xff && fseek(fp, (long)fileChunk * 0x100, SEEK_SET) == 0)
        {
            if (fread(dst, 1, 0x100, fp) != 0x100)
                memset(dst, 1, 0x100);
        }
    }
    if (fp != NULL)
        fclose(fp);

    *world = data;
    return data;
}

static int WorldTile(int x, int y)
{
    const byte* world = LoadWorld(D_5895_map_level > 0x7f);
    x &= 0xff;
    y &= 0xff;
    if (world == NULL)
        return -1;
    return world[((y >> 4) * 0x10 + (x >> 4)) * 0x100 + (y & 0x0f) * 0x10 + (x & 0x0f)];
}

static bool BlocksView(byte tile)
{
    return memchr(D_6a86, tile, sizeof(D_6a86)) != NULL;
}

// World tile at an offset from the party. Past a town map's edge this is the
// same stand-in the game's own view shows there (ULTIMA_4402_GetTileAddr
// returns D_6a07, the map's last square, usually grass).
static int TileAt(int dx, int dy)
{
    int x = (int)D_5896_map_x + dx;
    int y = (int)D_5897_map_y + dy;

    if (D_5893_map_id == 0)
    {
        // Outside the loaded 32x32 block, read the world map instead (the
        // block lookup would wrap around to the far side of the block).
        if (((x - D_589b) & 0xff) >= 0x20 || ((y - D_589c) & 0xff) >= 0x20)
            return WorldTile(x, y);
        x &= 0xff;
        y &= 0xff;
    }
    return *ULTIMA_4402_GetTileAddr(x, y);
}

// Straight line from the party to (dx, dy): visible unless a square strictly
// between them blocks the view.
static bool InLineOfSight(int dx, int dy)
{
    int steps = abs(dx) > abs(dy) ? abs(dx) : abs(dy);

    for (int i = 1; i < steps; i++)
    {
        // Nearest square to the point i/steps along the line.
        int x = (dx * i * 2 + (dx >= 0 ? steps : -steps)) / (2 * steps);
        int y = (dy * i * 2 + (dy >= 0 ? steps : -steps)) / (2 * steps);
        int tile = TileAt(x, y);

        if (tile < 0 || BlocksView((byte)tile))
            return false;
    }
    return true;
}

// Lamp light: at night the game also shows squares near its light sources
// (D_6a9a: lamps, torches, fires, moongates), as ULTIMA_5e4a works out for
// the 11x11 view: within a squared distance of LAMP_LIGHT of a source, with
// nothing blocking the view from it. The extra squares follow the same rule,
// or a lamp beside them would light only the half of the street inside the
// 11x11 view. Squares in offsets from the party.
#define LAMP_LIGHT 10   // the light level ULTIMA_5e4a gives each source
#define LAMP_SEARCH 3   // the furthest a source lights, in squares

// Nothing that blocks the view on the straight line strictly between two
// squares (the same steps as InLineOfSight).
static bool ClearBetween(int x0, int y0, int x1, int y1)
{
    int dx = x1 - x0, dy = y1 - y0;
    int steps = abs(dx) > abs(dy) ? abs(dx) : abs(dy);

    for (int i = 1; i < steps; i++)
    {
        int x = x0 + (dx * i * 2 + (dx >= 0 ? steps : -steps)) / (2 * steps);
        int y = y0 + (dy * i * 2 + (dy >= 0 ? steps : -steps)) / (2 * steps);
        int tile = TileAt(x, y);

        if (tile < 0 || BlocksView((byte)tile))
            return false;
    }
    return true;
}

static bool LitByLamp(int dx, int dy)
{
    for (int ly = dy - LAMP_SEARCH; ly <= dy + LAMP_SEARCH; ly++)
    {
        for (int lx = dx - LAMP_SEARCH; lx <= dx + LAMP_SEARCH; lx++)
        {
            int tile = TileAt(lx, ly);
            int d2 = (lx - dx) * (lx - dx) + (ly - dy) * (ly - dy);

            if (tile >= 0 && d2 < LAMP_LIGHT && memchr(D_6a9a, tile, sizeof(D_6a9a)) &&
                ClearBetween(lx, ly, dx, dy))
            {
                return true;
            }
        }
    }
    return false;
}

// The map tile at (dx, dy) from the party, or from the battlefield's top-left
// in combat; -1 if there is none.
static int MapTileAt(int dx, int dy)
{
    if (D_5893_map_id >= 0x80)
    {
        if (dx < 0 || dx > 10 || dy < 0 || dy > 10)
            return -1;
        return GetCombatMap(dx, dy);
    }
    return TileAt(dx, dy);
}

// A map tile drawn with see-through pixels (a sign, fountain or clock without
// its background) has nothing under it in the map; whatever was last on screen
// would show through. Take the ground from its neighbours instead: the tile
// most of the four beside it share (ties going to left, right, above, below),
// then the corners. Open ground such as floor or grass is preferred; walls and
// other tiles that block the view are used only when nothing else is near.
// Returns the tile to draw, or -1.
static int GroundUnder(int dx, int dy)
{
    static const int s_neighbours[8][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};

    for (int pass = 0; pass < 4; pass++)
    {
        bool allowWalls = pass >= 2;
        int ring = pass % 2;
        int tiles[4];
        int count = 0;

        for (int i = ring * 4; i < ring * 4 + 4; i++)
        {
            int tile = MapTileAt(dx + s_neighbours[i][0], dy + s_neighbours[i][1]);
            if (tile < 0 || GRAP_BUF_TileHasClearPixels(D_b11e[tile]))
                continue;
            if (!allowWalls && BlocksView((byte)tile))
                continue;
            tiles[count++] = D_b11e[tile];
        }

        int best = -1, bestVotes = 0;
        for (int i = 0; i < count; i++)
        {
            int votes = 0;
            for (int j = 0; j < count; j++)
                votes += tiles[j] == tiles[i];
            if (votes > bestVotes)
            {
                best = tiles[i];
                bestVotes = votes;
            }
        }
        if (best >= 0)
            return best;
    }
    return -1;
}

static int ExtraTile(int dx, int dy, int* under, int* look)
{
    int light = D_58a5;

    *under = -1;
    *look = -1;
    // Past the party's light, a lamp may still light the square.
    if (light < DAYLIGHT && dx * dx + dy * dy > light && !(InLineOfSight(dx, dy) && LitByLamp(dx, dy)))
        return -1;
    if (!InLineOfSight(dx, dy))
        return -1;

    int tile = TileAt(dx, dy);
    if (tile < 0)
        return -1;

    // Far desert uses the plain sand tile, as ULTIMA_5394 does beyond 5.
    if (tile == TILE_MAP_DD)
        tile = TILE_MAP_1C;

    // Anyone standing there, drawn instead of the terrain like the 11x11 view.
    for (int i = 0; i < 0x20; i++)
    {
        ActorFmt* actor = &D_5c5a[i];
        if (actor->_0_tile != 0 && actor->_1_animTile != 0 && actor->_4_z == D_5895_map_level &&
            actor->_2_x == (byte)(D_5896_map_x + dx) && actor->_3_y == (byte)(D_5897_map_y + dy))
        {
            *under = D_b11e[tile];
            if (GRAP_BUF_TileHasClearPixels(*under))
                *under = GroundUnder(dx, dy);
            *look = 0x100 + actor->_1_animTile;
            return 0x100 + actor->_1_animTile;
        }
    }

    if (GRAP_BUF_TileHasClearPixels(D_b11e[tile]))
        *under = GroundUnder(dx, dy);
    *look = tile;
    return D_b11e[tile];
}

void WIDE_UpdateExtraTiles(void)
{
    // Outdoors (0) and towns (1..0x20) only.
    bool active = g_options.wideView && D_5893_map_id <= 0x20;

    for (int row = 0; row < WIDE_EXTRA_ROWS; row++)
    {
        int dy = row - WIDE_EXTRA_ROWS / 2;
        for (int col = 0; col < 2 * WIDE_EXTRA_COLUMNS; col++)
        {
            // Left strip: 8..6 squares left of the party; right strip: 6..8 right.
            int dx = col < WIDE_EXTRA_COLUMNS ? col - (WIDE_EXTRA_COLUMNS + 5)
                                              : col - WIDE_EXTRA_COLUMNS + 6;
            g_wideExtraUnder[row][col] = -1;
            g_wideExtraLook[row][col] = -1;
            g_wideExtraTiles[row][col] = active ? ExtraTile(dx, dy, &g_wideExtraUnder[row][col], &g_wideExtraLook[row][col]) : -1;
        }
    }
    g_wideExtraTilesReady = true;
}

// Transparent creature tiles: ULTIMA_56ac_DrawMap replaces the terrain in a
// creature's square with the creature, so its tile carries its own background.
// When the full-color tile has see-through pixels, draw the terrain there
// first: from the battlefield in combat, otherwise from the map.
void WIDE_PutTerrainUnderActor(int x, int y, int actorTile)
{
    if (!GRAP_BUF_TileHasClearPixels(0x100 + actorTile))
        return;

    int tile;
    if (D_5893_map_id >= 0x80)
        tile = GetCombatMap(x, y);
    else
        tile = TileAt(x - 5, y - 5);

    if (tile >= 0)
    {
        WIDE_PutGroundUnderTerrain(x, y, tile);
        ULTIMA_10e0_GRAP_51_PutTile(D_b11e[tile], x, y);
    }
}

void WIDE_TilesUnderActor(int x, int y, int* ground, int* terrain)
{
    int tile = D_5893_map_id >= 0x80 ? GetCombatMap(x, y) : TileAt(x - 5, y - 5);

    *ground = *terrain = -1;
    if (tile < 0)
        return;
    *terrain = D_b11e[tile];
    if (GRAP_BUF_TileHasClearPixels(*terrain))
        *ground = D_5893_map_id >= 0x80 ? GroundUnder(x, y) : GroundUnder(x - 5, y - 5);
}

// Before a map tile with see-through pixels is drawn at (x, y) in the 11x11
// view, draw the ground from its neighbours there (see GroundUnder).
void WIDE_PutGroundUnderTerrain(int x, int y, int mapTile)
{
    if (!GRAP_BUF_TileHasClearPixels(D_b11e[mapTile]))
        return;

    int ground = D_5893_map_id >= 0x80 ? GroundUnder(x, y) : GroundUnder(x - 5, y - 5);
    if (ground >= 0)
        ULTIMA_10e0_GRAP_51_PutTile(ground, x, y);
}

#endif
