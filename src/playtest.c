#include "common/common.h"

#include "playtest.h"

#if defined(ENABLE_PLAYTEST)

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "funcs.h"
#include "hover.h"
#include "key/gamepad.h"

// Scripted play for tools/playtest.py. A script is a list of tokens separated
// by spaces or new lines; '#' starts a comment. Each token is one key press,
// given to the game the next time it looks for a key (so none are lost while
// it's busy), or "wait N" to let N milliseconds pass first, or "mouse X Y"
// to put the mouse pointer at X, Y in the window (for the names under it),
// or "click X Y" to click the left button at X, Y in the game's 640x400
// picture (the map's squares are 32 pixels, from 16, 16), or "pad BUTTON"
// to press and release a gamepad button, given like a key:
//
//   a-z 0-9    that key
//   ENTER ESC SPACE BACKSPACE TAB UP DOWN LEFT RIGHT PGUP PGDN F1-F10
//   pad: A B X Y UP DOWN LEFT RIGHT START BACK LB RB
//
// When the script runs out the game exits with status 0.

extern void KEY_SDL_ProcessKeyDown(SDL_KeyboardEvent ev);
extern SDL_FRect g_presentRect;
extern int g_presentWidth;

#define MAX_TOKENS 4096

enum
{
    STEP_KEY,
    STEP_WAIT,
    STEP_MOUSE,
    STEP_CLICK,
    STEP_PAD,
};

typedef struct ScriptStep
{
    int kind;
    SDL_Keycode key;
    Uint64 waitMs;
    float mouseX, mouseY;
    int button; // SDL_GamepadButton, for STEP_PAD
} ScriptStep;

static const struct
{
    const char* name;
    int button;
} s_padNames[] =
{
    {"A", SDL_GAMEPAD_BUTTON_SOUTH}, {"B", SDL_GAMEPAD_BUTTON_EAST},
    {"X", SDL_GAMEPAD_BUTTON_WEST}, {"Y", SDL_GAMEPAD_BUTTON_NORTH},
    {"UP", SDL_GAMEPAD_BUTTON_DPAD_UP}, {"DOWN", SDL_GAMEPAD_BUTTON_DPAD_DOWN},
    {"LEFT", SDL_GAMEPAD_BUTTON_DPAD_LEFT}, {"RIGHT", SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
    {"START", SDL_GAMEPAD_BUTTON_START}, {"BACK", SDL_GAMEPAD_BUTTON_BACK},
    {"LB", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER}, {"RB", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
};

static ScriptStep s_steps[MAX_TOKENS];
static int s_stepCount;
static int s_next;
static bool s_active;
static Uint64 s_waitUntil;

static const struct
{
    const char* name;
    SDL_Keycode key;
} s_keyNames[] =
{
    {"ENTER", SDLK_RETURN}, {"ESC", SDLK_ESCAPE}, {"SPACE", SDLK_SPACE},
    {"BACKSPACE", SDLK_BACKSPACE}, {"TAB", SDLK_TAB},
    {"UP", SDLK_UP}, {"DOWN", SDLK_DOWN}, {"LEFT", SDLK_LEFT}, {"RIGHT", SDLK_RIGHT},
    {"PGUP", SDLK_PAGEUP}, {"PGDN", SDLK_PAGEDOWN},
    {"F1", SDLK_F1}, {"F2", SDLK_F2}, {"F3", SDLK_F3}, {"F4", SDLK_F4}, {"F5", SDLK_F5},
    {"F6", SDLK_F6}, {"F7", SDLK_F7}, {"F8", SDLK_F8}, {"F9", SDLK_F9}, {"F10", SDLK_F10},
};

static void Fail(const char* path, const char* message, const char* token)
{
    fprintf(stderr, "playtest: %s: %s '%s'\n", path, message, token);
    exit(2);
}

static SDL_Keycode TokenKey(const char* token)
{
    if (token[1] == '\0')
    {
        char c = (char)SDL_tolower((unsigned char)token[0]);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            return (SDL_Keycode)c;
    }

    for (int i = 0; i < ARRAYSIZE(s_keyNames); i++)
    {
        if (SDL_strcasecmp(token, s_keyNames[i].name) == 0)
            return s_keyNames[i].key;
    }
    return 0;
}

static void LoadScript(const char* path)
{
    char line[512];
    FILE* stream = fopen(path, "r");
    bool waitNext = false;
    bool padNext = false;
    int mouseArgs = 0; // coordinates still to read for "mouse" or "click"
    int mouseKind = STEP_MOUSE;

    if (!stream)
        Fail(path, "can't open", path);

    while (fgets(line, sizeof(line), stream))
    {
        char* comment = strchr(line, '#');
        if (comment)
            *comment = '\0';

        for (char* token = strtok(line, " \t\r\n"); token; token = strtok(NULL, " \t\r\n"))
        {
            ScriptStep* step;

            if (s_stepCount == MAX_TOKENS)
                Fail(path, "too many steps at", token);
            step = &s_steps[s_stepCount];

            if (waitNext)
            {
                char* end;
                step->kind = STEP_WAIT;
                step->waitMs = strtoul(token, &end, 10);
                if (*end != '\0')
                    Fail(path, "wait needs milliseconds, not", token);
                waitNext = false;
            }
            else if (mouseArgs > 0)
            {
                char* end;
                float value = strtof(token, &end);
                if (*end != '\0')
                    Fail(path, "mouse needs X and Y, not", token);
                if (mouseArgs == 2)
                {
                    step->mouseX = value;
                    mouseArgs--;
                    continue;
                }
                step->kind = mouseKind;
                step->mouseY = value;
                mouseArgs = 0;
            }
            else if (SDL_strcasecmp(token, "wait") == 0)
            {
                waitNext = true;
                continue;
            }
            else if (padNext)
            {
                int i;
                for (i = 0; i < ARRAYSIZE(s_padNames); i++)
                {
                    if (SDL_strcasecmp(token, s_padNames[i].name) == 0)
                        break;
                }
                if (i == ARRAYSIZE(s_padNames))
                    Fail(path, "unknown gamepad button", token);
                step->kind = STEP_PAD;
                step->button = s_padNames[i].button;
                padNext = false;
            }
            else if (SDL_strcasecmp(token, "mouse") == 0 || SDL_strcasecmp(token, "click") == 0)
            {
                mouseArgs = 2;
                mouseKind = SDL_strcasecmp(token, "click") == 0 ? STEP_CLICK : STEP_MOUSE;
                continue;
            }
            else if (SDL_strcasecmp(token, "pad") == 0)
            {
                padNext = true;
                continue;
            }
            else if ((step->key = TokenKey(token)) == 0)
            {
                Fail(path, "unknown key", token);
            }
            else
            {
                step->kind = STEP_KEY;
            }
            s_stepCount++;
        }
    }

    fclose(stream);
}

void PLAYTEST_Init(int argc, char** argv)
{
    for (int i = 1; i + 1 < argc; i++)
    {
        if (strcmp(argv[i], "--script") == 0)
        {
            LoadScript(argv[i + 1]);
            s_active = true;
            return;
        }
    }
}

bool PLAYTEST_Active(void)
{
    return s_active;
}

void PLAYTEST_FeedKey(void)
{
    if (!s_active || SDL_GetTicks() < s_waitUntil)
        return;

    while (s_next < s_stepCount && s_steps[s_next].kind != STEP_KEY && s_steps[s_next].kind != STEP_PAD)
    {
        ScriptStep* step = &s_steps[s_next++];

        if (step->kind == STEP_MOUSE)
        {
#if defined(ENABLE_HOVER_NAMES)
            HOVER_MouseMoved(step->mouseX, step->mouseY);
#endif
            continue;
        }

        if (step->kind == STEP_CLICK)
        {
            // From the game's picture to the window, then as the mouse would.
            SDL_Event click;
            SDL_zero(click);
            click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            click.button.button = SDL_BUTTON_LEFT;
            click.button.down = true;
            click.button.clicks = 1;
            click.button.x = g_presentRect.x + (step->mouseX + 0.5f) * g_presentRect.w / g_presentWidth;
            click.button.y = g_presentRect.y + (step->mouseY + 0.5f) * g_presentRect.h / 400.0f;
            SDL_PushEvent(&click);
            continue;
        }

        s_waitUntil = SDL_GetTicks() + step->waitMs;
        if (s_waitUntil > SDL_GetTicks())
            return;
    }

    if (s_next == s_stepCount)
    {
        fprintf(stderr, "playtest: script finished\n");
        ULTIMA_0878_RestoreVideoMode();
        exit(0);
    }

    ScriptStep* step = &s_steps[s_next++];
    if (step->kind == STEP_PAD)
    {
#if defined(ENABLE_GAMEPAD)
        GAMEPAD_Button((SDL_GamepadButton)step->button, true);
        GAMEPAD_Button((SDL_GamepadButton)step->button, false);
#endif
        return;
    }

    SDL_KeyboardEvent ev;
    SDL_zero(ev);
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.down = true;
    ev.key = step->key;
    KEY_SDL_ProcessKeyDown(ev);
}

#endif
