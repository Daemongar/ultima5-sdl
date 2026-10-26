#include "common/common.h"

#include "graphics/grap_buf.h"
#include "options_menu.h"
#include "playtest.h"
#include "gamepad.h"
#include "pointclick.h"

#include <string.h>
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

// F1 opens the Options screen, F2 the shortcuts screen, F3 the spells screen,
// F4 the journal and F10 the dungeon map the next time the game waits for a key.
static int s_screenRequested;

#if defined(ENABLE_GAMEPAD)
// The gamepad's key menu (Y), opened like the screens above; it isn't an SDL
// key code, so it can't clash with one.
#define SCREEN_PAD_KEYS 1

// Keys picked from the key menu, given to the game one at a time.
static u16 s_padQueue[32];
static int s_padQueueCount;

void KEY_SDL_RequestPadMenu(void)
{
    s_screenRequested = SCREEN_PAD_KEYS;
}

// Keys for the game to read one at a time, as if pressed (point and click
// gives its commands and steps this way).
void KEY_SDL_QueueKeys(const SDL_Keycode* keys, int count)
{
    for (int i = 0; i < count && s_padQueueCount < (int)ARRAYSIZE(s_padQueue); i++)
    {
        SDL_KeyboardEvent ev;
        SDL_zero(ev);
        ev.key = keys[i];
        s_padQueue[s_padQueueCount++] = KeyboardEventToUltimaKeycode(ev);
    }
}
#endif

extern void GRAP_SDL_ReloadColorTiles(void);

void KEY_SDL_ProcessKeyDown(SDL_KeyboardEvent ev)
{
#if defined(ENABLE_POINT_CLICK)
    POINT_Cancel();
#endif
    if (ev.key == SDLK_F1 || ev.key == SDLK_F2 || ev.key == SDLK_F3 || ev.key == SDLK_F4 ||
        ev.key == SDLK_F10)
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

extern bool GRAP_SDL_WindowToRender(float windowX, float windowY, int* renderX, int* renderY);

static bool MousePosToTile(float mx, float my, int* outTileX, int* outTileY)
{
    int renderX, renderY;

    // Mouse events are in window coordinates, which differ from the picture's
    // pixels on a scaled display.
    if (!GRAP_SDL_WindowToRender(mx, my, &renderX, &renderY))
        return false;

    // Back to the game's own 640x400 coordinates.
    if (g_presentLayout == PRESENT_LAYOUT_WIDE_MAP)
        renderX = WideToGameX(renderX);
    else if (g_presentLayout == PRESENT_LAYOUT_CENTERED)
        renderX -= WIDE_EXTRA_WIDTH;

    // The battlefield starts inside the frame, where the crosshair is drawn
    // (ULTIMA_56ac_DrawMap); without this the left and top half of each
    // square picked the one before it.
    renderX -= MAP_RENDER_LEFT;
    renderY -= MAP_RENDER_TOP;
    if (renderX < 0 || renderY < 0)
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

#if defined(ENABLE_GAMEPAD)
    // The next key picked from the gamepad's key menu.
    if (s_screenRequested == 0 && s_lastDownKeycode == 0 && s_padQueueCount > 0)
    {
        s_lastDownKeycode = s_padQueue[0];
        memmove(s_padQueue, s_padQueue + 1, --s_padQueueCount * sizeof(s_padQueue[0]));
    }

    // A held direction repeats.
    if (s_lastDownKeycode == 0)
    {
        GAMEPAD_Tick();
    }
#endif

#if defined(ENABLE_POINT_CLICK)
    // A walk's next step.
    if (s_screenRequested == 0 && s_lastDownKeycode == 0 && s_padQueueCount == 0)
    {
        POINT_Tick();
    }
#endif

#if defined(ENABLE_PLAYTEST)
    // A script's next key, when the game has none waiting.
    if (s_screenRequested == 0 && s_lastDownKeycode == 0)
    {
        PLAYTEST_FeedKey();
    }
#endif

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
        else if (screen == SDLK_F4)
            OPTIONS_MENU_ShowJournalInGame();
#endif
#if defined(ENABLE_AUTOMAP)
        else if (screen == SDLK_F10)
            OPTIONS_MENU_ShowAutomapInGame();
#endif
#if defined(ENABLE_GAMEPAD)
        else if (screen == SCREEN_PAD_KEYS)
            s_padQueueCount = OPTIONS_MENU_ChooseKeys(s_padQueue, ARRAYSIZE(s_padQueue));
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
