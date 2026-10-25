#include "common/common.h"
#include "common/options.h"
#include "grap_buf.h"
#include "macros.h"
#include "vars.h"
#include "time/time.h"

#include "spellfx.h"

#if defined(ENABLE_SPELL_FX)

#include <SDL3/SDL.h>
#include <math.h>

// Spell effects in full color, added to the picture after it is composed
// (as the lighting is), so the game's own screen is untouched.
//
// Everything is made of glows (soft discs of light added to the picture),
// rings and washes, each with a birth time and a life; SPELLFX_Apply draws
// those alive and asks for the next frame while any are. Positions are kept
// in the 640x400 render coordinates of the original layout and placed on
// the frame when drawn, so the wide view moves them with the map.

extern int g_presentLayout;

typedef struct Color
{
    float r, g, b;
} Color;

typedef struct Glow
{
    float x, y;   // render pixels at birth
    float vx, vy; // render pixels a second
    float rise;   // upward pull, render pixels a second squared
    float radius;
    Color color;
    Uint64 born;
    int life; // ms; 0 for a free slot
} Glow;

typedef struct Ring
{
    float x, y;
    float reach; // radius at the end, render pixels
    Color color;
    Uint64 born;
    int life;
} Ring;

typedef struct Bolt
{
    float x0, y0, x1, y1;
    Color color;
    Uint64 born;
    int flight; // ms to reach the target
    bool landed;
} Bolt;

#define MAX_GLOWS 4096
#define MAX_RINGS 8
#define BOLT_MS 260
#define TRAIL_MS 90

static Glow s_glows[MAX_GLOWS];
static int s_nextGlow;
static Ring s_rings[MAX_RINGS];
static int s_nextRing;
static Bolt s_bolt;
static bool s_boltActive;

// The light washing over the map after a flash.
static Color s_washColor;
static Uint64 s_washBorn;
static int s_washLife;

static Color s_castColor = {0.45f, 0.75f, 1.0f};
static Color s_stormColor;

// Its own random numbers, so the game's (and the playtests') stay as they were.
static Uint32 s_seed = 0x2545f491;

static float Random(void)
{
    s_seed = s_seed * 1664525 + 1013904223;
    return (s_seed >> 8) / 16777216.0f;
}

static float RandomRange(float low, float high)
{
    return low + (high - low) * Random();
}

static const Color GOLD = {1.0f, 0.82f, 0.45f};
static const Color AZURE = {0.45f, 0.75f, 1.0f};
static const Color FIRE = {1.0f, 0.5f, 0.15f};
static const Color POISON = {0.45f, 1.0f, 0.3f};
static const Color HEAL = {0.6f, 1.0f, 0.65f};
static const Color SLEEP = {0.7f, 0.45f, 1.0f};
static const Color ENERGY = {0.55f, 0.85f, 1.0f};
static const Color SIGHT = {0.4f, 1.0f, 0.95f};
static const Color WHITE = {1.0f, 1.0f, 1.0f};
static const Color DEATH = {0.85f, 0.25f, 0.9f};
static const Color BLOOD = {1.0f, 0.25f, 0.2f};
static const Color EARTH = {0.9f, 0.6f, 0.3f};
static const Color CHARM = {1.0f, 0.55f, 0.8f};

// Each spell's color, in the order cast.c numbers them.
static const Color* const s_spellColors[48] = {
    &GOLD,   &AZURE,  &GOLD,   &HEAL,   &HEAL,   &AZURE,  &GOLD,   &WHITE,  // In Lor .. An Xen Corp
    &SIGHT,  &SIGHT,  &SLEEP,  &GOLD,                                       // Rel Hur .. In Xen Mani
    &GOLD,   &FIRE,   &FIRE,   &POISON, &SLEEP,  &AZURE,                    // Vas Lor .. In Por
    &WHITE,  &AZURE,  &ENERGY, &AZURE,  &AZURE,  &SIGHT,                    // An Grav .. Wis Quas
    &POISON, &GOLD,   &GOLD,   &HEAL,   &SLEEP,  &SIGHT,                    // In Bet Xen .. Rel Tym
    &EARTH,  &SLEEP,  &WHITE,  &SIGHT,  &CHARM,  &POISON,                   // In Vas Por Ylem .. Rel Xen Bet
    &SLEEP,  &DEATH,  &SLEEP,  &SIGHT,  &POISON, &BLOOD,                    // Sanct Lor .. In Quas Corp
    &GOLD,   &BLOOD,  &ENERGY, &FIRE,   &AZURE,  &WHITE,                    // In Mani Corp .. An Tym
};

static void AddGlow(float x, float y, float vx, float vy, float rise, float radius, Color color, int life)
{
    Glow* glow = &s_glows[s_nextGlow];
    s_nextGlow = (s_nextGlow + 1) % MAX_GLOWS;
    *glow = (Glow){x, y, vx, vy, rise, radius, color, SDL_GetTicks(), life};
    GRAP_BUF_MarkDirty();
}

static void AddRing(float x, float y, float reach, Color color, int life)
{
    s_rings[s_nextRing] = (Ring){x, y, reach, color, SDL_GetTicks(), life};
    s_nextRing = (s_nextRing + 1) % MAX_RINGS;
    GRAP_BUF_MarkDirty();
}

// A little brighter or paler, so sparks of one spell differ.
static Color Vary(Color c)
{
    float k = RandomRange(0.8f, 1.15f);
    float white = RandomRange(0.0f, 0.3f);
    return (Color){c.r * k + white, c.g * k + white, c.b * k + white};
}

static Color Dim(Color c, float k)
{
    return (Color){c.r * k, c.g * k, c.b * k};
}

// A square's center in render pixels: the combat map's, or the map's.
static void SquareCenter(int x, int y, float* outX, float* outY)
{
    *outX = (float)MAP_RENDER_LEFT + x * tilePixelSize + tilePixelSize / 2;
    *outY = (float)MAP_RENDER_TOP + y * tilePixelSize + tilePixelSize / 2;
}

// Where the caster stands: their combat square, or the party's in the
// middle of the map (or of the dungeon view).
static void CasterCenter(float* x, float* y)
{
    if (D_5893_map_id > 0x7f)
        SquareCenter(D_ba14[D_589e].x, D_ba14[D_589e].y, x, y);
    else
        SquareCenter(5, 5, x, y);
}

static void Sparks(float x, float y, Color color, int count, float speed, float radius, int life)
{
    for (int i = 0; i < count; i++)
    {
        float angle = RandomRange(0.0f, 6.2832f);
        float v = RandomRange(0.3f, 1.0f) * speed;
        AddGlow(x + RandomRange(-4.0f, 4.0f), y + RandomRange(-4.0f, 4.0f), cosf(angle) * v, sinf(angle) * v,
                -RandomRange(20.0f, 70.0f), RandomRange(0.6f, 1.0f) * radius, Vary(color),
                (int)(RandomRange(0.6f, 1.0f) * life));
    }
}

void SPELLFX_Cast(int spell)
{
    float x, y;

    if (spell < 0 || spell >= 48)
        return;
    s_castColor = *s_spellColors[spell];
    if (!g_options.spellEffects)
        return;

    // A swirl of sparks rising around the caster, and a soft glow on them.
    CasterCenter(&x, &y);
    for (int i = 0; i < 26; i++)
    {
        float angle = i * 6.2832f / 26 + RandomRange(-0.15f, 0.15f);
        float r = RandomRange(10.0f, 20.0f);
        // Sideways round the caster, so the sparks spiral outward and up.
        AddGlow(x + cosf(angle) * r, y + sinf(angle) * r * 0.6f, -sinf(angle) * 55.0f + cosf(angle) * 20.0f,
                cosf(angle) * 30.0f - 10.0f, -90.0f, RandomRange(3.0f, 5.0f), Vary(s_castColor),
                (int)RandomRange(500.0f, 800.0f));
    }
    AddGlow(x, y, 0, 0, 0, 30.0f, Dim(s_castColor, 0.6f), 450);
}

bool SPELLFX_Flash(int kind)
{
    float x, y;

    if (!g_options.spellEffects)
        return false;

    // The level changing spells (kind 4) swirl violet whatever was cast.
    Color color = kind == 4 ? SLEEP : s_castColor;
    CasterCenter(&x, &y);
    AddGlow(x, y, 0, 0, 0, 70.0f, Dim(color, 0.5f), 600);
    AddRing(x, y, 150.0f, color, 550);
    Sparks(x, y, color, 40, 170.0f, 6.0f, 800);

    s_washColor = color;
    s_washBorn = SDL_GetTicks();
    s_washLife = 450;
    return true;
}

static Color BoltColor(int bolt)
{
    switch (bolt)
    {
    case 0x31: return FIRE;
    case 0x32: return DEATH;
    case 0x33: return POISON;
    case 0x34: return SLEEP;
    case 0x35: return FIRE;
    case 0x36: return ENERGY;
    default: return AZURE;
    }
}

void SPELLFX_Bolt(int fromX, int fromY, int toX, int toY, int bolt)
{
    if (!g_options.spellEffects)
        return;

    SquareCenter(fromX, fromY, &s_bolt.x0, &s_bolt.y0);
    SquareCenter(toX, toY, &s_bolt.x1, &s_bolt.y1);
    s_bolt.color = BoltColor(bolt);
    s_bolt.born = SDL_GetTicks();
    s_bolt.flight = BOLT_MS;
    s_bolt.landed = false;
    s_boltActive = true;
    GRAP_BUF_MarkDirty();

    // The hit is told once the bolt lands.
    TIME_SleepMs(BOLT_MS + 20);
}

// A missile's color: a spell's bolt, or a monster's magic by the original's
// style (COMSUBS_0f4a draws 3-6 as sparks); false for arrows and rocks.
static bool MissileColor(int style, int bolt, Color* color)
{
    static const Color* const styles[4] = {&FIRE, &ENERGY, &POISON, &SLEEP};

    if (bolt >= 0x30 && bolt <= 0x36)
        *color = BoltColor(bolt);
    else if (style >= 3 && style <= 6)
        *color = *styles[style - 3];
    else
        return false;
    return true;
}

void SPELLFX_MissilePoint(int x, int y, int style, int bolt)
{
    Color c;

    if (!g_options.spellEffects || !MissileColor(style, bolt, &c))
        return;
    AddGlow(x * 2.0f, y * 2.0f, 0, 0, 0, 10.0f, c, 220);
    AddGlow(x * 2.0f, y * 2.0f, RandomRange(-40.0f, 40.0f), RandomRange(-40.0f, 40.0f), 0, 3.0f, Vary(c), 400);
}

void SPELLFX_MissileLanded(int x, int y, int style, int bolt)
{
    Color c;

    if (!g_options.spellEffects || !MissileColor(style, bolt, &c))
        return;
    AddGlow(x * 2.0f, y * 2.0f, 0, 0, 0, 30.0f, c, 380);
    Sparks(x * 2.0f, y * 2.0f, c, 14, 110.0f, 4.0f, 450);
}

void SPELLFX_BeginStorm(int storm)
{
    switch (storm)
    {
    case 1: s_stormColor = SLEEP; break;
    case 2: s_stormColor = POISON; break;
    case 3: s_stormColor = FIRE; break;
    default: s_stormColor = ENERGY; break;
    }
}

void SPELLFX_StormPoint(int x, int y)
{
    // Every other point is plenty: the rays are drawn a pixel at a time.
    if (!g_options.spellEffects || ((x + y) & 1))
        return;
    AddGlow(x * 2.0f, y * 2.0f, RandomRange(-8.0f, 8.0f), RandomRange(-8.0f, 8.0f), -15.0f, RandomRange(4.0f, 7.0f),
            Vary(s_stormColor), (int)RandomRange(500.0f, 900.0f));
}

// Drawing, in frame pixels.

typedef struct Frame
{
    u32* pixels;
    int pitch;
    int left, top, right, bottom; // the map, where effects may draw
    int scale;
    int shift; // the map's move right in the wide view, render pixels
} Frame;

static void AddLight(u32* pixel, Color c, float k)
{
    u32 p = *pixel;
    int r = ((p >> 16) & 0xff) + (int)(c.r * k * 255.0f);
    int g = ((p >> 8) & 0xff) + (int)(c.g * k * 255.0f);
    int b = (p & 0xff) + (int)(c.b * k * 255.0f);
    *pixel = (p & 0xff000000) | ((r > 255 ? 255 : r) << 16) | ((g > 255 ? 255 : g) << 8) | (b > 255 ? 255 : b);
}

// A soft disc of light at a render position.
static void DrawGlow(Frame* f, float x, float y, float radius, Color c, float strength)
{
    float cx = (x + f->shift) * f->scale, cy = y * f->scale, r = radius * f->scale;
    int x0 = SDL_max((int)(cx - r), f->left), x1 = SDL_min((int)(cx + r) + 1, f->right);
    int y0 = SDL_max((int)(cy - r), f->top), y1 = SDL_min((int)(cy + r) + 1, f->bottom);
    float r2 = r * r;

    for (int py = y0; py < y1; py++)
    {
        u32* row = f->pixels + py * f->pitch;
        float dy = py + 0.5f - cy;
        for (int px = x0; px < x1; px++)
        {
            float dx = px + 0.5f - cx;
            float t = 1.0f - (dx * dx + dy * dy) / r2;
            if (t > 0.0f)
                AddLight(&row[px], c, t * t * strength);
        }
    }
}

static void DrawRing(Frame* f, float x, float y, float radius, float width, Color c, float strength)
{
    float cx = (x + f->shift) * f->scale, cy = y * f->scale;
    float r = radius * f->scale, w = width * f->scale, outer = r + w;
    int x0 = SDL_max((int)(cx - outer), f->left), x1 = SDL_min((int)(cx + outer) + 1, f->right);
    int y0 = SDL_max((int)(cy - outer), f->top), y1 = SDL_min((int)(cy + outer) + 1, f->bottom);

    for (int py = y0; py < y1; py++)
    {
        u32* row = f->pixels + py * f->pitch;
        float dy = py + 0.5f - cy;
        for (int px = x0; px < x1; px++)
        {
            float dx = px + 0.5f - cx;
            float t = 1.0f - fabsf(sqrtf(dx * dx + dy * dy) - r) / w;
            if (t > 0.0f)
                AddLight(&row[px], c, t * t * strength);
        }
    }
}

static void DrawWash(Frame* f, Color c, float strength)
{
    for (int py = f->top; py < f->bottom; py++)
    {
        u32* row = f->pixels + py * f->pitch;
        for (int px = f->left; px < f->right; px++)
            AddLight(&row[px], c, strength);
    }
}

bool SPELLFX_Apply(u32* frame, int pitch)
{
    Uint64 now = SDL_GetTicks();
    bool active = false;
    Frame f;

    f.pixels = frame;
    f.pitch = pitch;
    f.scale = g_colorScale;
    f.shift = g_presentLayout == PRESENT_LAYOUT_NORMAL ? 0 : WIDE_EXTRA_WIDTH;
    f.left = (MAP_RENDER_LEFT + f.shift) * f.scale;
    f.top = MAP_RENDER_TOP * f.scale;
    f.right = (MAP_RENDER_RIGHT + f.shift) * f.scale;
    f.bottom = MAP_RENDER_BOTTOM * f.scale;
    if (g_presentLayout == PRESENT_LAYOUT_WIDE_MAP)
    {
        // The whole widened map.
        f.left = MAP_RENDER_LEFT * f.scale;
        f.right = (MAP_RENDER_RIGHT + 2 * WIDE_EXTRA_WIDTH) * f.scale;
    }

    if (s_washLife > 0)
    {
        float age = (float)(now - s_washBorn) / s_washLife;
        if (age < 1.0f)
        {
            // Quick to come, slow to go.
            float k = age < 0.15f ? age / 0.15f : 1.0f - (age - 0.15f) / 0.85f;
            DrawWash(&f, s_washColor, 0.35f * k);
            active = true;
        }
        else
        {
            s_washLife = 0;
        }
    }

    for (int i = 0; i < MAX_RINGS; i++)
    {
        Ring* ring = &s_rings[i];
        if (ring->life == 0)
            continue;
        float age = (float)(now - ring->born) / ring->life;
        if (age >= 1.0f)
        {
            ring->life = 0;
            continue;
        }
        // Fast at first, easing out.
        float grow = 1.0f - (1.0f - age) * (1.0f - age);
        DrawRing(&f, ring->x, ring->y, ring->reach * grow, 6.0f + 6.0f * age, ring->color, 0.9f * (1.0f - age));
        active = true;
    }

    if (s_boltActive)
    {
        float t = (float)(now - s_bolt.born) / s_bolt.flight;
        Color c = s_bolt.color;
        if (t < 1.0f)
        {
            // The head and a tail behind it.
            for (int i = 0; i < 8; i++)
            {
                float back = t - (float)TRAIL_MS / s_bolt.flight * i / 8.0f;
                if (back < 0.0f)
                    break;
                float x = s_bolt.x0 + (s_bolt.x1 - s_bolt.x0) * back;
                float y = s_bolt.y0 + (s_bolt.y1 - s_bolt.y0) * back;
                DrawGlow(&f, x, y, 9.0f - i * 0.8f, c, 0.9f - i * 0.1f);
            }
            float x = s_bolt.x0 + (s_bolt.x1 - s_bolt.x0) * t;
            float y = s_bolt.y0 + (s_bolt.y1 - s_bolt.y0) * t;
            DrawGlow(&f, x, y, 4.0f, WHITE, 0.8f);
            if (Random() < 0.6f)
                AddGlow(x, y, RandomRange(-30.0f, 30.0f), RandomRange(-30.0f, 30.0f), 0, 3.0f, Vary(c), 300);
            active = true;
        }
        else
        {
            // It lands: a burst on the target.
            s_boltActive = false;
            AddGlow(s_bolt.x1, s_bolt.y1, 0, 0, 0, 34.0f, c, 400);
            AddRing(s_bolt.x1, s_bolt.y1, 40.0f, c, 350);
            Sparks(s_bolt.x1, s_bolt.y1, c, 18, 120.0f, 4.0f, 500);
            active = true;
        }
    }

    for (int i = 0; i < MAX_GLOWS; i++)
    {
        Glow* g = &s_glows[i];
        if (g->life == 0)
            continue;
        float ms = (float)(now - g->born);
        if (ms >= g->life)
        {
            g->life = 0;
            continue;
        }
        float s = ms / 1000.0f, age = ms / g->life;
        float x = g->x + g->vx * s;
        float y = g->y + g->vy * s + 0.5f * g->rise * s * s;
        DrawGlow(&f, x, y, g->radius * (1.0f - 0.4f * age), g->color, 1.0f - age);
        active = true;
    }

    return active;
}

#endif
