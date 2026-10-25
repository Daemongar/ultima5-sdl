#include "common/common.h"
#include "common/options.h"
#include "grap_buf.h"
#include "macros.h"
#include "vars.h"

#include "weather.h"

#if defined(ENABLE_WEATHER)

#include <SDL3/SDL.h>
#include <math.h>

#include "audio/audio.h"

// Weather on the map, drawn over the composed frame after the lighting.
//
// It changes every three hours of game time and follows from the date, so
// it needs nothing saved: each three hours is clear, overcast, rainy, stormy
// or (early in the morning) foggy, and the last 45 minutes blend into the
// next. Overcast skies dim and grey the land under drifting cloud shadows;
// rain falls in streaks, slanted by the game's wind, and splashes; storms
// flash with lightning and thunder follows; fog drifts in patches. Outdoors
// and in towns, not underground or in battles. It changes nothing in play.

extern int g_presentLayout;

#define SLOT_HOURS 3
#define BLEND_MINUTES 45
#define BLOCK 8          // clouds and fog are worked out every BLOCK frame pixels
#define MAX_DROPS 700

enum
{
    SKY_CLEAR,
    SKY_OVERCAST,
    SKY_RAIN,
    SKY_STORM,
    SKY_FOG,
};

typedef struct Sky
{
    float cloud; // dimming and cloud shadows, 0-1
    float rain;  // 0-1
    float fog;   // 0-1
    float storm; // lightning, 0-1
} Sky;

static const Sky s_skies[] = {
    {0.0f, 0.0f, 0.0f, 0.0f},   // clear
    {0.55f, 0.0f, 0.0f, 0.0f},  // overcast
    {0.8f, 0.6f, 0.1f, 0.0f},   // rain
    {1.0f, 1.0f, 0.1f, 1.0f},   // storm
    {0.35f, 0.0f, 0.85f, 0.0f}, // fog
};

static float* s_grid; // shade and fog at each block corner
static int s_gridSize;

// Lightning: when the next flash comes, when the last one came, and the
// thunder still to be heard.
static Uint64 s_nextFlash;
static Uint64 s_flashAt;
static Uint64 s_thunderAt;
static int s_thunderLoudness;

static Uint32 Hash(Uint32 x)
{
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;
    return x;
}

static float Hash01(Uint32 x)
{
    return (Hash(x) & 0xffffff) / 16777216.0f;
}

// Value noise: smooth, about 1 unit across a bump.
static float Lattice(int x, int y, int seed)
{
    return Hash01((Uint32)x * 73856093u ^ (Uint32)y * 19349663u ^ (Uint32)seed * 83492791u);
}

static float Noise(float x, float y, int seed)
{
    float fx0 = floorf(x), fy0 = floorf(y);
    int ix = (int)fx0, iy = (int)fy0;
    float fx = x - fx0, fy = y - fy0;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    float a = Lattice(ix, iy, seed), b = Lattice(ix + 1, iy, seed);
    float c = Lattice(ix, iy + 1, seed), d = Lattice(ix + 1, iy + 1, seed);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

// What a three-hour slot of the calendar brings.
static int SlotSky(Uint32 slot)
{
    int roll = (int)(Hash(slot * 2654435761u + 0x5eed) % 100);
    int hour = (int)(slot % (24 / SLOT_HOURS)) * SLOT_HOURS;

    if (roll < 58)
        return SKY_CLEAR;
    if (roll < 75)
        return SKY_OVERCAST;
    if (roll < 88)
        return SKY_RAIN;
    if (roll < 94)
        return SKY_STORM;
    // Fog lies in the early morning; later it's only grey.
    return hour >= 3 && hour <= 6 ? SKY_FOG : SKY_OVERCAST;
}

static bool Outside(void)
{
    int map = D_5893_map_id;

    if (!g_options.weather)
        return false;
    if (map == 0)
        return D_5895_map_level < 0x80; // not the underworld
    return map >= 1 && map <= 0x20 && map != 0x19;
}

static Sky CurrentSky(void)
{
    // Days since the calendar began: 13 months of 28 days.
    Uint32 day = (Uint32)D_5874 * 13 * 28 + (Uint32)(D_587d > 0 ? D_587d - 1 : 0) * 28 + (D_587e > 0 ? D_587e - 1 : 0);
    Uint32 slot = day * (24 / SLOT_HOURS) + D_587f / SLOT_HOURS;
    int minutes = (D_587f % SLOT_HOURS) * 60 + D_5881;
    Sky now = s_skies[SlotSky(slot)], next = s_skies[SlotSky(slot + 1)];

    float t = (float)(minutes - (SLOT_HOURS * 60 - BLEND_MINUTES)) / BLEND_MINUTES;
    if (t <= 0.0f)
        return now;
    t = t * t * (3 - 2 * t);
    return (Sky){now.cloud + (next.cloud - now.cloud) * t, now.rain + (next.rain - now.rain) * t,
                 now.fog + (next.fog - now.fog) * t, now.storm + (next.storm - now.storm) * t};
}

float WEATHER_RainLevel(void)
{
    return Outside() ? CurrentSky().rain : 0.0f;
}

// How much daylight there is, so rain and fog darken at night.
static float Daylight(void)
{
    float h = D_587f + D_5881 / 60.0f;
    if (h >= 7.0f && h < 17.0f)
        return 1.0f;
    if (h < 4.5f || h >= 19.5f)
        return 0.3f;
    return h < 7.0f ? 0.3f + 0.7f * (h - 4.5f) / 2.5f : 0.3f + 0.7f * (19.5f - h) / 2.5f;
}

// Lightning: a bright flash, a flicker, a second flash, fading.
static float FlashLevel(Uint64 now)
{
    if (s_flashAt == 0)
        return 0.0f;
    Uint64 ms = now - s_flashAt;
    if (ms < 60)
        return 1.0f;
    if (ms < 120)
        return 0.25f;
    if (ms < 200)
        return 0.8f;
    if (ms < 500)
        return 0.8f * (1.0f - (ms - 200) / 300.0f);
    s_flashAt = 0;
    return 0.0f;
}

static void Lightning(Uint64 now, float storm)
{
    if (storm < 0.5f)
    {
        s_nextFlash = 0;
        return;
    }
    if (s_nextFlash == 0)
        s_nextFlash = now + 3000 + Hash((Uint32)now) % 8000;
    if (now >= s_nextFlash)
    {
        // Close strikes are heard sooner and louder.
        Uint32 roll = Hash((Uint32)now * 31u);
        int delay = 250 + (int)(roll % 2200);
        s_flashAt = now;
        s_thunderAt = now + delay;
        s_thunderLoudness = 100 - delay * 60 / 2450;
        s_nextFlash = now + 5000 + Hash((Uint32)now * 17u) % 11000;
    }
    if (s_thunderAt != 0 && now >= s_thunderAt)
    {
        if (g_options.ambientSounds)
            AUDIO_PlayThunder(s_thunderLoudness);
        s_thunderAt = 0;
    }
}

typedef struct Area
{
    u32* frame;
    int pitch;
    int left, top, width, height; // the map, frame pixels
    int scale;
} Area;

static void Blend(u32* pixel, float r, float g, float b, float a)
{
    u32 p = *pixel;
    float pr = (float)((p >> 16) & 0xff), pg = (float)((p >> 8) & 0xff), pb = (float)(p & 0xff);
    int nr = (int)(pr + (r * 255.0f - pr) * a);
    int ng = (int)(pg + (g * 255.0f - pg) * a);
    int nb = (int)(pb + (b * 255.0f - pb) * a);
    *pixel = (p & 0xff000000) | (u32)(SDL_clamp(nr, 0, 255) << 16) | (u32)(SDL_clamp(ng, 0, 255) << 8) |
             (u32)SDL_clamp(nb, 0, 255);
}

// Clouds and fog: shade and haze worked out at block corners from noise
// fixed to the land (so it moves with the map) and drifting with the wind.
static void CloudsAndFog(Area* a, Sky sky, float daylight, float flash, double seconds, float windX, float windY,
                         float worldX, float worldY)
{
    int blocksX = a->width / BLOCK + 2, blocksY = a->height / BLOCK + 2;
    int needed = blocksX * blocksY * 2;
    if (needed > s_gridSize)
    {
        SDL_free(s_grid);
        s_grid = SDL_malloc(needed * sizeof(float));
        s_gridSize = s_grid ? needed : 0;
        if (!s_grid)
            return;
    }

    // Clouds drift with the wind (and a little east when it's calm); fog slower.
    float cloudX = (float)fmod(seconds * (10.0 + 14.0 * windX), 1.0e5);
    float cloudY = (float)fmod(seconds * 14.0 * windY, 1.0e5);
    float fogX = (float)fmod(seconds * (5.0 + 6.0 * windX), 1.0e5);
    float fogY = (float)fmod(seconds * 6.0 * windY, 1.0e5);

    for (int by = 0; by < blocksY; by++)
    {
        for (int bx = 0; bx < blocksX; bx++)
        {
            // Render pixels on the land.
            float wx = worldX + (float)(bx * BLOCK) / a->scale;
            float wy = worldY + (float)(by * BLOCK) / a->scale;
            float* cell = &s_grid[(by * blocksX + bx) * 2];

            float n = 0.65f * Noise((wx - cloudX) / 170.0f, (wy - cloudY) / 170.0f, 1) +
                      0.35f * Noise((wx - cloudX) / 70.0f, (wy - cloudY) / 70.0f, 2);
            cell[0] = 1.0f - sky.cloud * (0.2f + 0.3f * n);

            float f = 0.6f * Noise((wx - fogX) / 110.0f, (wy - fogY) / 110.0f, 3) +
                      0.4f * Noise((wx + fogX) / 45.0f, (wy - fogY) / 45.0f, 4);
            cell[1] = sky.fog * (0.35f + 0.55f * f);
        }
    }

    float grey = 0.45f * sky.cloud;
    float fogLight = 0.78f * daylight + 0.5f * flash;
    for (int y = 0; y < a->height; y++)
    {
        int by = y / BLOCK;
        float fy = (float)(y % BLOCK) / BLOCK;
        const float* row0 = &s_grid[by * blocksX * 2];
        const float* row1 = &s_grid[(by + 1) * blocksX * 2];
        u32* out = &a->frame[(a->top + y) * a->pitch + a->left];

        for (int x = 0; x < a->width; x++)
        {
            int bx = x / BLOCK;
            float fx = (float)(x % BLOCK) / BLOCK;
            float v[2];
            for (int k = 0; k < 2; k++)
            {
                float top = row0[bx * 2 + k] + (row0[(bx + 1) * 2 + k] - row0[bx * 2 + k]) * fx;
                float bottom = row1[bx * 2 + k] + (row1[(bx + 1) * 2 + k] - row1[bx * 2 + k]) * fx;
                v[k] = top + (bottom - top) * fy;
            }

            u32 p = out[x];
            float r = (float)((p >> 16) & 0xff), g = (float)((p >> 8) & 0xff), b = (float)(p & 0xff);
            // Greyer and darker under cloud, a little blue.
            float l = 0.3f * r + 0.59f * g + 0.11f * b;
            r = (r + (l - r) * grey) * v[0];
            g = (g + (l - g) * grey) * v[0];
            b = (b + (l * 1.08f - b) * grey) * v[0];
            // Fog: toward a pale grey, as light as the day.
            r += (fogLight * 205.0f - r) * v[1];
            g += (fogLight * 210.0f - g) * v[1];
            b += (fogLight * 218.0f - b) * v[1];
            out[x] = (p & 0xff000000) | (u32)(SDL_clamp((int)r, 0, 255) << 16) |
                     (u32)(SDL_clamp((int)g, 0, 255) << 8) | (u32)SDL_clamp((int)b, 0, 255);
        }
    }
}

static void Flash(Area* a, float flash)
{
    for (int y = 0; y < a->height; y++)
    {
        u32* out = &a->frame[(a->top + y) * a->pitch + a->left];
        for (int x = 0; x < a->width; x++)
            Blend(&out[x], 0.9f, 0.93f, 1.0f, 0.45f * flash);
    }
}

// A dot of rain: two frame pixels wide, so it survives the picture being
// scaled down to the window.
static void Plot(Area* a, float x, float y, float r, float g, float b, float alpha)
{
    int px = (int)(x * a->scale), py = (int)(y * a->scale);
    if (px < 0 || py < 0 || px + 1 >= a->width || py >= a->height)
        return;
    u32* pixel = &a->frame[(a->top + py) * a->pitch + a->left + px];
    Blend(pixel, r, g, b, alpha);
    Blend(pixel + 1, r, g, b, alpha * 0.6f);
}

// Rain: each drop falls a random distance, slanted by the wind, then
// splashes, and starts again somewhere else. Render pixels within the map.
static void Rain(Area* a, Sky sky, float daylight, float flash, double seconds, float windX, float windY)
{
    float w = (float)a->width / a->scale, h = (float)a->height / a->scale;
    int drops = (int)(MAX_DROPS * sky.rain * (w * h) / (544.0f * 352.0f));
    float vy = 760.0f + 260.0f * windY, vx = 40.0f + 300.0f * windX;
    float light = 0.45f + 0.45f * daylight + 0.4f * flash;
    float cr = 0.85f * light, cg = 0.9f * light, cb = 1.0f * light;
    float alpha = 0.35f + 0.2f * sky.rain;
    float length = 18.0f;
    float speed = sqrtf(vx * vx + vy * vy);
    float stepX = vx / speed / a->scale, stepY = vy / speed / a->scale;

    for (int i = 0; i < drops; i++)
    {
        float fall = 50.0f + Hash01(i * 7u + 1) * (h - 40.0f); // how far it falls, render pixels
        float pace = 0.8f + 0.4f * Hash01(i * 7u + 2);
        double period = fall / (vy * pace) + 0.12;               // the fall and the splash
        double t = seconds + Hash01(i * 7u + 3) * period;
        Uint32 cycle = (Uint32)(t / period);
        float u = (float)((t - cycle * period) * vy * pace / fall); // 0-1 falling, past 1 the splash

        Uint32 key = i * 7919u + cycle * 104729u;
        float x0 = Hash01(key) * (w + 120.0f) - 60.0f - vx * fall / vy * 0.5f;
        float y0 = Hash01(key + 1) * (h - fall + 30.0f) - 30.0f;
        float dx = vx / vy * fall;

        if (u <= 1.0f)
        {
            float x = x0 + dx * u, y = y0 + fall * u;
            for (int s = 0; s < (int)(length * a->scale); s++)
            {
                float k = 1.0f - (float)s / (length * a->scale);
                Plot(a, x - stepX * s, y - stepY * s, cr, cg, cb, alpha * k);
            }
        }
        else
        {
            // A splash: a little ring opening out where it fell.
            float age = (u - 1.0f) * fall / (vy * pace) / 0.12f;
            if (age > 1.0f)
                continue;
            float x = x0 + dx, y = y0 + fall;
            float radius = 1.0f + 3.0f * age;
            for (int s = 0; s < 10; s++)
            {
                float angle = s * 0.628f;
                Plot(a, x + cosf(angle) * radius, y + sinf(angle) * radius * 0.5f, cr, cg, cb, 0.5f * (1.0f - age));
            }
        }
    }
}

bool WEATHER_Apply(u32* frame, int pitch, int slideX, int slideY)
{
    if (!Outside())
        return false;

    Sky sky = CurrentSky();
    if (sky.cloud < 0.01f && sky.rain < 0.01f && sky.fog < 0.01f)
    {
        s_nextFlash = 0;
        return false;
    }

    Uint64 now = SDL_GetTicks();
    double seconds = now / 1000.0;
    Lightning(now, sky.storm);
    float flash = FlashLevel(now);

    // The map, in frame pixels, and the land under its top left corner in
    // render pixels (a square is tilePixelSize), moved with a slide.
    bool wide = g_presentLayout == PRESENT_LAYOUT_WIDE_MAP;
    Area a;
    a.frame = frame;
    a.pitch = pitch;
    a.scale = g_colorScale;
    a.left = MAP_RENDER_LEFT * a.scale;
    a.top = MAP_RENDER_TOP * a.scale;
    a.width = (MAP_RENDER_RIGHT - MAP_RENDER_LEFT + (wide ? 2 * WIDE_EXTRA_WIDTH : 0)) * a.scale;
    a.height = (MAP_RENDER_BOTTOM - MAP_RENDER_TOP) * a.scale;
    int partyColumn = 5 + (wide ? WIDE_EXTRA_COLUMNS : 0);
    float worldX = (float)(D_5896_map_x - partyColumn) * tilePixelSize - (float)slideX / a.scale;
    float worldY = (float)(D_5897_map_y - 5) * tilePixelSize - (float)slideY / a.scale;

    // The way the wind blows: North winds blow south, East winds west.
    static const float s_windX[5] = {0, 0, 0, -1, 1};
    static const float s_windY[5] = {0, 1, -1, 0, 0};
    int wind = D_5892_wind_dir <= 4 ? D_5892_wind_dir : 0;

    float daylight = Daylight();
    CloudsAndFog(&a, sky, daylight, flash, seconds, s_windX[wind], s_windY[wind], worldX, worldY);
    if (flash > 0.0f)
        Flash(&a, flash);
    if (sky.rain > 0.01f)
        Rain(&a, sky, daylight, flash, seconds, s_windX[wind], s_windY[wind]);
    return true;
}

#endif
