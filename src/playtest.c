#include "common/common.h"

#include "playtest.h"

#if defined(ENABLE_PLAYTEST)

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "funcs.h"
#include "hover.h"

// Scripted play for tools/playtest.py. A script is a list of tokens separated
// by spaces or new lines; '#' starts a comment. Each token is one key press,
// given to the game the next time it looks for a key (so none are lost while
// it's busy), or "wait N" to let N milliseconds pass first, or "mouse X Y"
// to put the mouse pointer at X, Y in the window (for the names under it):
//
//   a-z 0-9    that key
//   ENTER ESC SPACE BACKSPACE TAB UP DOWN LEFT RIGHT PGUP PGDN F1-F10
//
// When the script runs out the game exits with status 0.

extern void KEY_SDL_ProcessKeyDown(SDL_KeyboardEvent ev);

#define MAX_TOKENS 4096

enum
{
    STEP_KEY,
    STEP_WAIT,
    STEP_MOUSE,
};

typedef struct ScriptStep
{
    int kind;
    SDL_Keycode key;
    Uint64 waitMs;
    float mouseX, mouseY;
} ScriptStep;

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
    int mouseArgs = 0; // coordinates still to read for "mouse"

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
                step->kind = STEP_MOUSE;
                step->mouseY = value;
                mouseArgs = 0;
            }
            else if (SDL_strcasecmp(token, "wait") == 0)
            {
                waitNext = true;
                continue;
            }
            else if (SDL_strcasecmp(token, "mouse") == 0)
            {
                mouseArgs = 2;
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

    while (s_next < s_stepCount && s_steps[s_next].kind != STEP_KEY)
    {
        ScriptStep* step = &s_steps[s_next++];

        if (step->kind == STEP_MOUSE)
        {
#if defined(ENABLE_HOVER_NAMES)
            HOVER_MouseMoved(step->mouseX, step->mouseY);
#endif
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

    SDL_KeyboardEvent ev;
    SDL_zero(ev);
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.down = true;
    ev.key = s_steps[s_next++].key;
    KEY_SDL_ProcessKeyDown(ev);
}

#endif
