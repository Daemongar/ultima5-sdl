#include "common/common.h"

#include "graphics/grap_buf.h"
#include "options_menu.h"
#include "vars.h"
#include "macros.h"

#include <SDL3/SDL.h>

#define KBD_HOME   0x47
#define KBD_UP     0x48
#define KBD_PGUP   0x49
#define KBD_LEFT   0x4B
#define KBD_RIGHT  0x4D
#define KBD_END    0x4f
#define KBD_DOWN   0x50
#define KBD_PGDN   0x51

void KEY_Initialize(void)
{}

void KEY_Cleanup(void)
{}

static u16 s_lastDownKeycode = 0;

static u16 KeyboardEventToUltimaKeycode(SDL_KeyboardEvent ev)
{
    if (ev.mod & SDL_KMOD_CTRL)
    {
        switch (ev.key)
        {
        case SDLK_B: return 2;
        case SDLK_E: return U5_KEY_CTRL_E;
        case SDLK_K: return U5_KEY_CTRL_K;
        case SDLK_M: return U5_KEY_CTRL_M; // same as CR
        case SDLK_S: return U5_KEY_CTRL_S;
        case SDLK_V: return U5_KEY_CTRL_V;
        }

        if (!(ev.key & (SDLK_SCANCODE_MASK | SDLK_EXTENDED_MASK)))
            return 0x1a; // return not used keycode
    }

#if defined(ENABLE_SAVE_SLOTS)
    switch (ev.key)
    {
    case SDLK_F5: return U5_KEY_QUICKSAVE;
    case SDLK_F9: return U5_KEY_QUICKLOAD;
    case SDLK_F6: return U5_KEY_SAVE_SLOT;
    case SDLK_F7: return U5_KEY_LOAD_SLOT;
    }
#endif

    if (!(ev.key & (SDLK_SCANCODE_MASK | SDLK_EXTENDED_MASK)))
    {
        // ascii/unicode keypoint

        bool shift = (ev.mod & SDL_KMOD_SHIFT) != 0;
        bool caps = (ev.mod & SDL_KMOD_CAPS) != 0;

        if (shift ^ caps)
        {
            // TODO: symbols
            return SDL_toupper((u8)ev.key);
        }

        return (u8)ev.key;
    }

    u8 extendedKeycode;
    switch (ev.key)
    {
    case SDLK_HOME: extendedKeycode = KBD_HOME; break;
    case SDLK_UP: extendedKeycode = KBD_UP; break;
    case SDLK_PAGEUP: extendedKeycode = KBD_PGUP; break;
    case SDLK_LEFT: extendedKeycode = KBD_LEFT; break;
    case SDLK_RIGHT: extendedKeycode = KBD_RIGHT; break;
    case SDLK_END: extendedKeycode = KBD_END; break;
    case SDLK_DOWN: extendedKeycode = KBD_DOWN; break;
    case SDLK_PAGEDOWN: extendedKeycode = KBD_PGDN; break;
    default:
        return 0;
    }

    // based on ULTIMA_1d5e

    if (extendedKeycode <= 0x44)
    {
        // F1..F10 -> c9..d2
        return extendedKeycode + 0x8e;
    }

    // Arrow keys
    // l, r, d, u, home, end, pgup, pgdn -> 1, 2, 3, 4, d3, d4, d5, d6
    int i;
    for (i = 0; i < 8; i++)
    {
        if (extendedKeycode == D_540e[i])
            break;
    }

    if (i == 8)
    {
        return 0;
    }

    u16 key = D_5416[i];
    key |= 0x100; // special keystroke
    return key;
}

// F1 opens the Options screen, F2 the shortcuts screen, F3 the spells screen
// and F4 the journal the next time the game waits for a key.
static int s_screenRequested;

extern void GRAP_SDL_ReloadColorTiles(void);

void KEY_SDL_ProcessKeyDown(SDL_KeyboardEvent ev)
{
    if (ev.key == SDLK_F1 || ev.key == SDLK_F2 || ev.key == SDLK_F3 || ev.key == SDLK_F4)
    {
        s_screenRequested = ev.key;
        return;
    }

    // F8 reloads TILES.BMP, for trying out changes to the tile sheet.
    if (ev.key == SDLK_F8)
    {
        GRAP_SDL_ReloadColorTiles();
        return;
    }

    s_lastDownKeycode = KeyboardEventToUltimaKeycode(ev);
}

#if defined(ENABLE_MOUSE_AIM)

// Combat's crosshair aiming (src/comsubs.c, COMSUBS_0504) is the only consumer
// of mouse input; these no-op unless it's currently active.
extern void COMSUBS_AimHover(int tileX, int tileY);
extern bool COMSUBS_AimClick(int tileX, int tileY);
extern bool COMSUBS_AimCancel(void);

extern SDL_FRect g_presentRect;
extern int g_presentWidth;
extern int g_presentLayout;

// The combat map viewport is an 11x11 grid of 32px tiles in the 640x400 render.
#define COMBAT_MAP_TILE_PX tilePixelSize
#define COMBAT_MAP_TILES 11

static bool MousePosToTile(float mx, float my, int* outTileX, int* outTileY)
{
    if (g_presentRect.w <= 0 || g_presentRect.h <= 0)
        return false;

    int renderX = (int)((mx - g_presentRect.x) * g_presentWidth / g_presentRect.w);
    int renderY = (int)((my - g_presentRect.y) * renderHeight / g_presentRect.h);
    if (renderX < 0 || renderY < 0)
        return false;

    // Back to the game's own 640x400 coordinates.
    if (g_presentLayout == PRESENT_LAYOUT_WIDE_MAP)
        renderX = WideToGameX(renderX);
    else if (g_presentLayout == PRESENT_LAYOUT_CENTERED)
        renderX -= WIDE_EXTRA_WIDTH;
    if (renderX < 0)
        return false;

    int tileX = renderX / COMBAT_MAP_TILE_PX;
    int tileY = renderY / COMBAT_MAP_TILE_PX;

    if (tileX < 0 || tileX >= COMBAT_MAP_TILES || tileY < 0 || tileY >= COMBAT_MAP_TILES)
        return false;

    *outTileX = tileX;
    *outTileY = tileY;
    return true;
}

void KEY_SDL_ProcessMouseMotion(SDL_MouseMotionEvent ev)
{
    int tileX, tileY;
    if (MousePosToTile(ev.x, ev.y, &tileX, &tileY))
    {
        COMSUBS_AimHover(tileX, tileY);
    }
}

void KEY_SDL_ProcessMouseButtonDown(SDL_MouseButtonEvent ev)
{
    int tileX, tileY;

    if (ev.button == SDL_BUTTON_LEFT && MousePosToTile(ev.x, ev.y, &tileX, &tileY))
    {
        if (COMSUBS_AimClick(tileX, tileY))
        {
            s_lastDownKeycode = U5_KEY_ENTER;
        }
    }
    else if (ev.button == SDL_BUTTON_RIGHT)
    {
        if (COMSUBS_AimCancel())
        {
            s_lastDownKeycode = U5_KEY_ESC;
        }
    }
}

#endif

extern void EVT_Yield(void);

int KEY_PollKey(void)
{
	int ret;

	D_538a = 0;

	EVT_Yield();

    if (s_screenRequested != 0)
    {
        int screen = s_screenRequested;
        s_screenRequested = 0;

        // Pressed again while a screen is up: close it, as Esc would.
        if (OPTIONS_MENU_IsOpen())
            return U5_KEY_ESC;

        if (screen == SDLK_F1)
            OPTIONS_MENU_RunInGame();
        else if (screen == SDLK_F2)
            OPTIONS_MENU_ShowShortcutsInGame();
        else if (screen == SDLK_F3)
            OPTIONS_MENU_ShowSpellsInGame();
#if defined(ENABLE_JOURNAL)
        else
            OPTIONS_MENU_ShowJournalInGame();
#endif
        return 0;
    }

    // special keystroke
    if (s_lastDownKeycode & 0x100)
    {
        s_lastDownKeycode &= 0xff;
        D_538a = 1;
    }

    ret = s_lastDownKeycode;
    s_lastDownKeycode = 0;

	return ret;
}
