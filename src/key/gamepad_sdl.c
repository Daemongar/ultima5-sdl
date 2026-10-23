#include "common/common.h"

#include "gamepad.h"

#if defined(ENABLE_GAMEPAD)

// Gamepads, through SDL's gamepad mapping, as the keys the game already reads:
//
//   D-pad, left stick   arrow keys (held, they repeat like a key)
//   A                   Enter
//   B                   Esc
//   X                   Space (pass a turn)
//   Y                   the key menu: letters, digits, Space, Delete, Enter
//   Start               Options (F1)
//   Back                Keyboard shortcuts (F2)
//   Left shoulder       Journal (F4)
//   Right shoulder      Dungeon map (F10)
//
// Every command letter, a conversation keyword or a spell's syllables are
// picked from the key menu (options_menu.c).

extern void KEY_SDL_ProcessKeyDown(SDL_KeyboardEvent ev);
extern void KEY_SDL_RequestPadMenu(void);

#define REPEAT_DELAY_MS 350
#define REPEAT_INTERVAL_MS 150
#define STICK_PRESS 20000   // of 32767: past this the stick counts as pressed
#define STICK_RELEASE 12000 // and back under this as released

enum
{
    DIR_NONE = -1,
    DIR_PAD_UP,
    DIR_PAD_DOWN,
    DIR_PAD_LEFT,
    DIR_PAD_RIGHT,
};

static const SDL_Keycode s_dirKeys[4] = {SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT};

static int s_heldDir = DIR_NONE; // the direction repeating, from the d-pad or stick
static Uint64 s_nextRepeat;
static Sint16 s_stickX, s_stickY;
static int s_stickDir = DIR_NONE;

static void PressKey(SDL_Keycode key)
{
    SDL_KeyboardEvent ev;
    SDL_zero(ev);
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.down = true;
    ev.key = key;
    KEY_SDL_ProcessKeyDown(ev);
}

static void StartDirection(int dir)
{
    s_heldDir = dir;
    s_nextRepeat = SDL_GetTicks() + REPEAT_DELAY_MS;
    PressKey(s_dirKeys[dir]);
}

static void StopDirection(int dir)
{
    if (s_heldDir == dir)
        s_heldDir = DIR_NONE;
}

void GAMEPAD_Button(SDL_GamepadButton button, bool down)
{
    int dir = DIR_NONE;

    switch (button)
    {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: dir = DIR_PAD_UP; break;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: dir = DIR_PAD_DOWN; break;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: dir = DIR_PAD_LEFT; break;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: dir = DIR_PAD_RIGHT; break;
    default: break;
    }

    if (dir != DIR_NONE)
    {
        if (down)
            StartDirection(dir);
        else
            StopDirection(dir);
        return;
    }

    if (!down)
        return;

    switch (button)
    {
    case SDL_GAMEPAD_BUTTON_SOUTH: PressKey(SDLK_RETURN); break;
    case SDL_GAMEPAD_BUTTON_EAST: PressKey(SDLK_ESCAPE); break;
    case SDL_GAMEPAD_BUTTON_WEST: PressKey(SDLK_SPACE); break;
    case SDL_GAMEPAD_BUTTON_NORTH: KEY_SDL_RequestPadMenu(); break;
    case SDL_GAMEPAD_BUTTON_START: PressKey(SDLK_F1); break;
    case SDL_GAMEPAD_BUTTON_BACK: PressKey(SDLK_F2); break;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: PressKey(SDLK_F4); break;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: PressKey(SDLK_F10); break;
    default: break;
    }
}

// The left stick as a fifth d-pad: whichever axis is pushed furthest.
static void UpdateStick(void)
{
    int dir = DIR_NONE;
    int x = s_stickX, y = s_stickY;
    int ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    int threshold = s_stickDir == DIR_NONE ? STICK_PRESS : STICK_RELEASE;

    if (ax >= threshold || ay >= threshold)
    {
        if (ax > ay)
            dir = x < 0 ? DIR_PAD_LEFT : DIR_PAD_RIGHT;
        else
            dir = y < 0 ? DIR_PAD_UP : DIR_PAD_DOWN;
    }

    if (dir == s_stickDir)
        return;
    if (s_stickDir != DIR_NONE)
        StopDirection(s_stickDir);
    s_stickDir = dir;
    if (dir != DIR_NONE)
        StartDirection(dir);
}

void GAMEPAD_HandleEvent(const SDL_Event* ev)
{
    switch (ev->type)
    {
    case SDL_EVENT_GAMEPAD_ADDED:
        SDL_OpenGamepad(ev->gdevice.which);
        break;

    case SDL_EVENT_GAMEPAD_REMOVED:
    {
        SDL_Gamepad* pad = SDL_GetGamepadFromID(ev->gdevice.which);
        if (pad)
            SDL_CloseGamepad(pad);
        s_heldDir = s_stickDir = DIR_NONE;
        break;
    }

    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        GAMEPAD_Button((SDL_GamepadButton)ev->gbutton.button, ev->gbutton.down);
        break;

    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (ev->gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX)
            s_stickX = ev->gaxis.value;
        else if (ev->gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY)
            s_stickY = ev->gaxis.value;
        else
            break;
        UpdateStick();
        break;
    }
}

void GAMEPAD_Tick(void)
{
    Uint64 now = SDL_GetTicks();

    if (s_heldDir != DIR_NONE && now >= s_nextRepeat)
    {
        s_nextRepeat = now + REPEAT_INTERVAL_MS;
        PressKey(s_dirKeys[s_heldDir]);
    }
}

#endif
