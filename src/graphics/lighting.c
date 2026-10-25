#include "common/common.h"
#include "common/options.h"
#include "grap_buf.h"
#include "macros.h"
#include "tiles.h"
#include "vars.h"

#include "lighting.h"

#if defined(ENABLE_LIGHTING)

#include <SDL3/SDL.h>
#include <math.h>
#include <string.h>

// Lighting, as the frame is presented: the game's own lighting is all or
// nothing (squares past the party's light are drawn as darkness), so this
// colors what it shows. By day nothing changes; dawn and dusk turn warm and
// dim, night dark blue. Around the party is a soft circle of its light,
// reaching as far as the game lets it see (warm for a torch, white for a
// light spell), and fires and lamps glow and flicker. Nothing hidden by the
// game is revealed. The overworld, the underworld and towns, in the map view.

extern int g_presentLayout;
extern int g_wideExtraLook[WIDE_EXTRA_ROWS][2 * WIDE_EXTRA_COLUMNS];

#define MAP_ROWS 11
#define BLOCK 4          // the light is worked out every BLOCK frame pixels
#define MAX_LIGHTS 64

typedef struct Color
{
    float r, g, b;
} Color;

typedef struct Light
{
    float x, y;      // center, in tiles from the map's top left corner
    float radius;    // in tiles
    Color color;
    bool flickers;
} Light;

static const Color s_day = {1.0f, 1.0f, 1.0f};
static const Color s_sunset = {0.96f, 0.62f, 0.46f};
static const Color s_night = {0.24f, 0.28f, 0.46f};
static const Color s_underworld = {0.22f, 0.20f, 0.26f};
static const Color s_torch = {1.0f, 0.78f, 0.50f};
static const Color s_lightSpell = {0.88f, 0.94f, 1.0f};
static const Color s_eyes = {0.78f, 0.82f, 0.96f}; // no light: what the party can make out
static const Color s_flame = {1.0f, 0.62f, 0.30f};
static const Color s_lava = {1.0f, 0.40f, 0.16f};
static const Color s_glow = {0.95f, 0.90f, 0.70f};

static float* s_map;     // the light at each block corner, r g b
static int s_mapSize;

static Color Mix(Color a, Color b, float t)
{
    return (Color){a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

// The sky's light for the time of day, through a warm sunset color at dawn
// (4:30 to 7:00) and dusk (17:00 to 19:30). The game's own light (D_6a80)
// rises from 5:00 and falls from 19:00, hiding all but the party's
// surroundings, so the sunset is at its deepest before then.
static Color SkyLight(void)
{
    if (D_5893_map_id == 0 && D_5895_map_level > 0x7f)
        return s_underworld;

    float hour = D_587f + D_5881 / 60.0f;
    if (hour >= 7.0f && hour < 17.0f)
        return s_day;
    if (hour >= 4.5f && hour < 5.5f)
        return Mix(s_night, s_sunset, hour - 4.5f);
    if (hour >= 5.5f && hour < 7.0f)
        return Mix(s_sunset, s_day, (hour - 5.5f) / 1.5f);
    if (hour >= 17.0f && hour < 18.5f)
        return Mix(s_day, s_sunset, (hour - 17.0f) / 1.5f);
    if (hour >= 18.5f && hour < 19.5f)
        return Mix(s_sunset, s_night, hour - 18.5f);
    return s_night;
}

// How a map tile glows, if it does: radius in tiles and color.
static bool TileLight(int tile, float* radius, Color* color, bool* flickers)
{
    *flickers = true;
    switch (tile)
    {
    case TILE_MAP_LAVA:
        *radius = 1.6f;
        *color = s_lava;
        return true;
    // The game's own light sources (D_6a9a) light the squares within a
    // squared distance of 10 at night (ULTIMA_5e4a); the glow reaches as far.
    case 0xb0: // flickering torches
    case 0xb1:
    case 0xb2: // brazier
    case 0xb3:
    case TILE_MAP_FIREPLACE:
    case 0xbd: // street lamp
    case 0xbe: // candelabrum
    case 0xbf: // stove
    case 0xde: // a shrine's flame
        *radius = 3.3f;
        *color = s_flame;
        return true;
    case 0x2a: // a bright light
        *radius = 2.6f;
        *color = s_glow;
        *flickers = false;
        return true;
    case 0x1b: // lighthouse
        *radius = 3.0f;
        *color = s_glow;
        return true;
    }
    return false;
}

// A slow, uneven flicker for the light at x, y.
static float Flicker(float x, float y)
{
    float t = SDL_GetTicks() / 1000.0f;
    float seed = x * 12.9898f + y * 78.233f;
    return 0.85f + 0.08f * sinf(t * 7.3f + seed) + 0.07f * sinf(t * 13.1f + seed * 1.7f);
}

static int AddMapLight(Light* lights, int count, int tile, float x, float y)
{
    float radius;
    Color color;
    bool flickers;

    if (count < MAX_LIGHTS && tile >= 0 && tile < 0x100 && TileLight(tile, &radius, &color, &flickers))
        lights[count++] = (Light){x + 0.5f, y + 0.5f, radius, color, flickers};
    return count;
}

bool LIGHTING_Apply(u32* frame, int pitch, int slideX, int slideY)
{
    int map = D_5893_map_id;
    if (!g_options.lighting || map > 0x20)
        return false;

    Color sky = SkyLight();
    float dark = 1.0f - (sky.r + sky.g + sky.b) / 3.0f; // 0 by day
    if (dark <= 0.0f)
        return false;

    bool wide = g_presentLayout == PRESENT_LAYOUT_WIDE_MAP;
    int columns = wide ? MAP_ROWS + 2 * WIDE_EXTRA_COLUMNS : MAP_ROWS;
    int firstColumn = wide ? WIDE_EXTRA_COLUMNS : 0; // where the 11x11 view starts
    int tile = tilePixelSize * g_colorScale;
    int left = MAP_RENDER_LEFT * g_colorScale;
    int top = MAP_RENDER_TOP * g_colorScale;
    int width = columns * tile;
    int height = MAP_ROWS * tile;

    // The lights: fires and lamps in squares the game shows (it draws the
    // rest as darkness), moved with the map while it slides.
    Light lights[MAX_LIGHTS];
    int count = 0;
    float shiftX = (float)slideX / tile, shiftY = (float)slideY / tile;
    for (int y = 0; y < MAP_ROWS; y++)
        for (int x = 0; x < MAP_ROWS; x++)
            count = AddMapLight(lights, count, GetMapViewport(x, y), firstColumn + x + shiftX, y + shiftY);
    if (wide)
    {
        for (int y = 0; y < WIDE_EXTRA_ROWS; y++)
            for (int c = 0; c < 2 * WIDE_EXTRA_COLUMNS; c++)
            {
                int x = c < WIDE_EXTRA_COLUMNS ? c : c + MAP_ROWS;
                count = AddMapLight(lights, count, g_wideExtraLook[y][c], x + shiftX, y + shiftY);
            }
    }

    bool flickering = false;
    for (int i = 0; i < count; i++)
    {
        if (lights[i].flickers)
        {
            float f = Flicker(lights[i].x - shiftX, lights[i].y - shiftY);
            lights[i].color = (Color){lights[i].color.r * f, lights[i].color.g * f, lights[i].color.b * f};
            flickering = true;
        }
    }

    // The party's light, as far as the game lets it see (D_58a5 is the
    // squared distance it lights): a torch, a light spell, or only what the
    // party can make out in the dark.
    Color own = D_58a6 != 0 ? s_torch : D_58a7 != 0 ? s_lightSpell : s_eyes;
    float reach = sqrtf((float)D_58a5);
    float centerX = firstColumn + 5.5f, centerY = 5.5f;

    // Work the light out at block corners, then spread it over the pixels.
    int blocksX = width / BLOCK + 1, blocksY = height / BLOCK + 1;
    int needed = blocksX * blocksY * 3;
    if (needed > s_mapSize)
    {
        SDL_free(s_map);
        s_map = SDL_malloc(needed * sizeof(float));
        s_mapSize = s_map ? needed : 0;
        if (!s_map)
            return false;
    }

    for (int by = 0; by < blocksY; by++)
    {
        for (int bx = 0; bx < blocksX; bx++)
        {
            float tx = (float)(bx * BLOCK) / tile, ty = (float)(by * BLOCK) / tile;
            Color light = sky;

            // The party's circle: full near, fading to the sky at its edge.
            float d = sqrtf((tx - centerX) * (tx - centerX) + (ty - centerY) * (ty - centerY));
            float edge = reach + 0.2f;
            if (d < edge)
            {
                float t = d < reach - 1.5f ? 1.0f : (edge - d) / 1.7f;
                t = t * t * (3 - 2 * t);
                light.r = fmaxf(light.r, own.r * t);
                light.g = fmaxf(light.g, own.g * t);
                light.b = fmaxf(light.b, own.b * t);
            }

            // Glows add to it, more strongly the darker it is.
            for (int i = 0; i < count; i++)
            {
                float dx = tx - lights[i].x, dy = ty - lights[i].y;
                float r2 = lights[i].radius * lights[i].radius;
                float d2 = dx * dx + dy * dy;
                if (d2 >= r2)
                    continue;
                float t = 1.0f - d2 / r2;
                t = t * t * dark;
                light.r += lights[i].color.r * t;
                light.g += lights[i].color.g * t;
                light.b += lights[i].color.b * t;
            }

            float* cell = &s_map[(by * blocksX + bx) * 3];
            cell[0] = fminf(light.r, 1.25f);
            cell[1] = fminf(light.g, 1.25f);
            cell[2] = fminf(light.b, 1.25f);
        }
    }

    for (int y = 0; y < height; y++)
    {
        int by = y / BLOCK;
        float fy = (float)(y % BLOCK) / BLOCK;
        const float* row0 = &s_map[by * blocksX * 3];
        const float* row1 = &s_map[SDL_min(by + 1, blocksY - 1) * blocksX * 3];
        u32* out = &frame[(top + y) * pitch + left];

        for (int x = 0; x < width; x++)
        {
            int bx = x / BLOCK;
            int bx1 = SDL_min(bx + 1, blocksX - 1);
            float fx = (float)(x % BLOCK) / BLOCK;
            float l[3];
            for (int k = 0; k < 3; k++)
            {
                float a = row0[bx * 3 + k] + (row0[bx1 * 3 + k] - row0[bx * 3 + k]) * fx;
                float b = row1[bx * 3 + k] + (row1[bx1 * 3 + k] - row1[bx * 3 + k]) * fx;
                l[k] = a + (b - a) * fy;
            }

            u32 p = out[x];
            int r = (int)(((p >> 16) & 0xff) * l[0]);
            int g = (int)(((p >> 8) & 0xff) * l[1]);
            int b = (int)((p & 0xff) * l[2]);
            out[x] = (p & 0xff000000) | (u32)(SDL_min(r, 255) << 16) | (u32)(SDL_min(g, 255) << 8) | (u32)SDL_min(b, 255);
        }
    }

    return flickering;
}

#endif
