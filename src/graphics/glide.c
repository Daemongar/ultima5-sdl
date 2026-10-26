#include "common/common.h"
#include "common/config.h"
#include "common/options.h"
#include "grap_buf.h"
#include "macros.h"
#include "vars.h"
#include "wide_view.h"

#include "glide.h"

#if defined(ENABLE_GLIDE)

#include <SDL3/SDL.h>
#include <string.h>

// Gliding: the game moves people and monsters a whole square at a time, so
// they jump. When one is drawn a square from where it was last drawn, it
// slides there over a moment instead: its new square is painted with the
// ground under it, and its tile is drawn part of the way from the old
// square. Only actors whose tiles have see-through pixels glide (the
// full-color tiles; the original's square tiles would slide a block of
// ground with them), and only within the 11x11 view, both squares in sight.

extern int g_presentLayout;

#define OBJECTS 32
#define VIEW 11

typedef struct Seen
{
    bool present;
    int map, level, x, y, z;
    int shape; // the base tile without its animation frame
} Seen;

typedef struct Glide
{
    bool active;
    int fromX, fromY, toX, toY; // view squares
    int tile;                   // as drawn at toX, toY
    int ground, terrain;        // what its new square shows without it
    Uint64 start;
} Glide;

static Seen s_seen[OBJECTS];
static Glide s_glides[OBJECTS];
static int s_glideMs = -1;
static int s_viewMap = -1, s_viewX, s_viewY; // where the view was last drawn

// Squares from one coordinate to another; the overworld wraps at 256.
static int Step(int from, int to, bool wraps)
{
    int step = to - from;
    if (wraps)
    {
        step &= 0xff;
        if (step >= 0x80)
            step -= 0x100;
    }
    return step;
}

void GLIDE_MapDrawn(void)
{
    bool combat = D_5893_map_id >= 0x80;
    Uint64 now = SDL_GetTicks();

    if (s_glideMs < 0)
    {
        // How long a glide takes (glide_ms; not on the Options screen).
        s_glideMs = CONFIG_GetInt("display", "glide_ms", 140);
        if (s_glideMs < 1)
            s_glideMs = 1;
    }

    // The view moved: glides under way are in its old squares.
    if (D_5893_map_id != s_viewMap || (!combat && (D_5896_map_x != s_viewX || D_5897_map_y != s_viewY)))
    {
        for (int i = 0; i < OBJECTS; i++)
            s_glides[i].active = false;
        s_viewMap = D_5893_map_id;
        s_viewX = D_5896_map_x;
        s_viewY = D_5897_map_y;
    }

    for (int i = 0; i < OBJECTS; i++)
    {
        ActorFmt* object = &D_5c5a[i];
        Seen now_ = {object->_0_tile != 0, D_5893_map_id, D_5895_map_level, object->_2_x, object->_3_y, object->_4_z,
                     object->_0_tile & 0xfc};
        Seen* was = &s_seen[i];

        // The party glides only in combat: elsewhere it stays in the middle
        // and the map scrolls under it.
        bool moved = g_options.glide && (combat || i != 0) && now_.present && was->present && was->map == now_.map &&
                     was->level == now_.level && was->z == now_.z && was->shape == now_.shape;
        int dx = moved ? Step(was->x, now_.x, now_.map == 0) : 0;
        int dy = moved ? Step(was->y, now_.y, now_.map == 0) : 0;

        if (moved && (dx != 0 || dy != 0) && abs(dx) <= 1 && abs(dy) <= 1)
        {
            // In view squares: combat's are the battlefield's, elsewhere the
            // party is in the middle.
            int toX = combat ? now_.x : Step(D_5896_map_x, now_.x, now_.map == 0) + 5;
            int toY = combat ? now_.y : Step(D_5897_map_y, now_.y, now_.map == 0) + 5;
            int fromX = toX - dx, fromY = toY - dy;
            bool inView = toX >= 0 && toX < VIEW && toY >= 0 && toY < VIEW && fromX >= 0 && fromX < VIEW &&
                          fromY >= 0 && fromY < VIEW;

            Glide* glide = &s_glides[i];
            glide->active = false;
            if (inView && GetMapViewport(toX, toY) == 0 && GetActorMap(toX, toY) != 0x16 &&
                GetMapViewport(fromX, fromY) != 0xff)
            {
                int tile = 0x100 + GetActorMap(toX, toY);
                if (GRAP_BUF_TileHasClearPixels(tile))
                {
                    WIDE_TilesUnderActor(toX, toY, &glide->ground, &glide->terrain);
                    if (glide->terrain >= 0)
                    {
                        *glide = (Glide){true, fromX, fromY, toX, toY, tile, glide->ground, glide->terrain, now};
                        debug("glide: %d from %d,%d to %d,%d", i, fromX, fromY, toX, toY);
                    }
                }
            }
        }
        else if (moved && (dx != 0 || dy != 0))
        {
            s_glides[i].active = false; // a jump: no glide
        }

        *was = now_;
    }
}

// A square of the frame from a tile buffer; with see-through, pixels left
// at 0 (no alpha) are skipped. Clipped to the map.
static void Blit(u32* frame, int pitch, const u32* tile, int size, int x, int y, bool seeThrough, int clipLeft,
                 int clipTop, int clipRight, int clipBottom)
{
    for (int ty = 0; ty < size; ty++)
    {
        int fy = y + ty;
        if (fy < clipTop || fy >= clipBottom)
            continue;
        for (int tx = 0; tx < size; tx++)
        {
            int fx = x + tx;
            u32 p = tile[ty * size + tx];
            if (fx < clipLeft || fx >= clipRight || (seeThrough && (p >> 24) == 0))
                continue;
            frame[fy * pitch + fx] = p;
        }
    }
}

bool GLIDE_Apply(u32* frame, int pitch, const u32* palette)
{
    static u32 s_ground[tilePixelSize * tilePixelSize * maxColorScale * maxColorScale];
    static u32 s_sprite[tilePixelSize * tilePixelSize * maxColorScale * maxColorScale];
    Uint64 now = SDL_GetTicks();
    bool active = false;
    int size = colorTileSize;
    int column0 = g_presentLayout == PRESENT_LAYOUT_WIDE_MAP ? WIDE_EXTRA_COLUMNS : 0;
    int left = MAP_RENDER_LEFT * g_colorScale + column0 * size;
    int top = MAP_RENDER_TOP * g_colorScale;

    if (!g_options.glide)
        return false;

    for (int i = 0; i < OBJECTS; i++)
    {
        Glide* glide = &s_glides[i];
        if (!glide->active)
            continue;
        Uint64 elapsed = now - glide->start;
        if (elapsed >= (Uint64)s_glideMs)
        {
            glide->active = false;
            continue;
        }

        // Its new square without it.
        if (glide->ground >= 0)
        {
            GRAP_BUF_RenderTileRGB(glide->ground, palette, s_ground);
            GRAP_BUF_RenderTileRGBOver(glide->terrain, palette, s_ground);
        }
        else
        {
            GRAP_BUF_RenderTileRGB(glide->terrain, palette, s_ground);
        }
        int toX = left + glide->toX * size, toY = top + glide->toY * size;
        Blit(frame, pitch, s_ground, size, toX, toY, false, left, top, left + VIEW * size, top + VIEW * size);

        // It, on the way: easing out, quick to leave and gentle to arrive.
        float t = (float)elapsed / s_glideMs;
        float left_ = (1.0f - t) * (1.0f - t);
        int x = toX + (int)((glide->fromX - glide->toX) * size * left_);
        int y = toY + (int)((glide->fromY - glide->toY) * size * left_);
        memset(s_sprite, 0, sizeof(u32) * size * size);
        GRAP_BUF_RenderTileRGBOver(glide->tile, palette, s_sprite);
        Blit(frame, pitch, s_sprite, size, x, y, true, left, top, left + VIEW * size, top + VIEW * size);
        active = true;
    }
    return active;
}

#endif
