#include "common/common.h"
#include "common/config.h"
#include "macros.h"

#include "keybind.h"

#if defined(ENABLE_KEYBIND)

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

// Rebindable keys: each command and each direction can be given another
// key (Options, Keys page; [keys] in ultima5.ini). A key bound to an action
// gives the game that action's own key, but only where the game reads a
// command or a direction, so typing a name, a word or a spell is never
// changed. The arrow keys always move unless bound to something else, and an
// action's own key does nothing once the action has another (A no longer
// attacks after Attack moves to another key).

#define SPECIAL 0x100 // keys without a character: 0x100 + U5_KEY_*

typedef struct Action
{
    const char* id;    // in ultima5.ini
    const char* label; // on the Options screen and the shortcuts page
    u16 original;
    u16 bound;
} Action;

static Action s_actions[] =
{
    {"north", "Move north", SPECIAL | U5_KEY_UP, 0},
    {"south", "Move south", SPECIAL | U5_KEY_DOWN, 0},
    {"east", "Move east", SPECIAL | U5_KEY_RIGHT, 0},
    {"west", "Move west", SPECIAL | U5_KEY_LEFT, 0},
    {"pass", "Pass", ' ', 0},
    {"attack", "Attack", 'A', 0},
    {"board", "Board", 'B', 0},
    {"cast", "Cast", 'C', 0},
    {"enter", "Enter", 'E', 0},
    {"fire", "Fire", 'F', 0},
    {"get", "Get", 'G', 0},
    {"hole_up", "Hole up & camp", 'H', 0},
    {"ignite", "Ignite torch", 'I', 0},
    {"jimmy", "Jimmy lock", 'J', 0},
    {"klimb", "Klimb", 'K', 0},
    {"look", "Look", 'L', 0},
    {"mix", "Mix reagents", 'M', 0},
    {"new_order", "New order", 'N', 0},
    {"open", "Open", 'O', 0},
    {"push", "Push", 'P', 0},
    {"quit", "Quit & save", 'Q', 0},
    {"ready", "Ready gear", 'R', 0},
    {"search", "Search", 'S', 0},
    {"talk", "Talk", 'T', 0},
    {"use", "Use item", 'U', 0},
    {"view", "View gem", 'V', 0},
    {"xit", "X-it", 'X', 0},
    {"yell", "Yell", 'Y', 0},
    {"ztats", "Z-stats", 'Z', 0},
};

#define ACTION_COUNT ((int)ARRAYSIZE(s_actions))
#define MOVES 4 // the first four
#define PASS 4

static int s_context = KEYBIND_NONE;
static bool s_ready;

static const struct
{
    u16 key;
    const char* name;
} s_specialNames[] =
{
    {SPECIAL | U5_KEY_UP, "Up"},
    {SPECIAL | U5_KEY_DOWN, "Down"},
    {SPECIAL | U5_KEY_LEFT, "Left"},
    {SPECIAL | U5_KEY_RIGHT, "Right"},
    {SPECIAL | U5_KEY_HOME, "Home"},
    {SPECIAL | U5_KEY_END, "End"},
    {SPECIAL | U5_KEY_PGUP, "PgUp"},
    {SPECIAL | U5_KEY_PGDN, "PgDn"},
    {' ', "Space"},
};

static u16 Normal(u16 key)
{
    return key >= 'a' && key <= 'z' ? (u16)(key - 'a' + 'A') : key;
}

static void Ready(void)
{
    if (s_ready)
        return;
    s_ready = true;
    for (int i = 0; i < ACTION_COUNT; i++)
        s_actions[i].bound = s_actions[i].original;
}

void KEYBIND_SetContext(int context)
{
    s_context = context;
}

static bool IsArrow(u16 key)
{
    return key == (SPECIAL | U5_KEY_UP) || key == (SPECIAL | U5_KEY_DOWN) || key == (SPECIAL | U5_KEY_LEFT) ||
           key == (SPECIAL | U5_KEY_RIGHT);
}

u16 KEYBIND_Translate(u16 key)
{
    Ready();
    if (s_context == KEYBIND_NONE)
        return key;

    u16 normal = Normal(key);
    for (int i = 0; i < ACTION_COUNT; i++)
    {
        // Asked for a direction, only the moves (and Pass) count.
        if (s_context == KEYBIND_DIRECTION && i > PASS)
            continue;
        if (s_actions[i].bound == normal)
            return s_actions[i].original;
    }

    if (IsArrow(normal))
        return key;
    for (int i = 0; i < ACTION_COUNT; i++)
    {
        if (s_context == KEYBIND_DIRECTION && i > PASS)
            continue;
        if (s_actions[i].original == normal)
            return 0; // its action has another key now
    }
    return key;
}

int KEYBIND_Count(void)
{
    return ACTION_COUNT;
}

const char* KEYBIND_Label(int action)
{
    return s_actions[action].label;
}

static const char* KeyName(u16 key)
{
    static char text[2];

    for (int i = 0; i < (int)ARRAYSIZE(s_specialNames); i++)
    {
        if (s_specialNames[i].key == key)
            return s_specialNames[i].name;
    }
    text[0] = key > 0x20 && key < 0x7f ? (char)key : '?';
    text[1] = '\0';
    return text;
}

const char* KEYBIND_KeyName(int action)
{
    Ready();
    return KeyName(s_actions[action].bound);
}

static bool Bindable(u16 key)
{
    for (int i = 0; i < (int)ARRAYSIZE(s_specialNames); i++)
    {
        if (s_specialNames[i].key == key)
            return true;
    }
    // Letters, digits and punctuation.
    return key > 0x20 && key < 0x7f;
}

bool KEYBIND_Bind(int action, u16 key)
{
    Ready();
    key = Normal(key);
    if (action < 0 || action >= ACTION_COUNT || !Bindable(key))
        return false;

    for (int i = 0; i < ACTION_COUNT; i++)
    {
        if (i != action && s_actions[i].bound == key)
            s_actions[i].bound = s_actions[action].bound;
    }
    s_actions[action].bound = key;
    return true;
}

void KEYBIND_Reset(int action)
{
    KEYBIND_Bind(action, s_actions[action].original);
}

void KEYBIND_ResetAll(void)
{
    for (int i = 0; i < ACTION_COUNT; i++)
        s_actions[i].bound = s_actions[i].original;
    s_ready = true;
}

static bool ParseKey(const char* text, u16* key)
{
    for (int i = 0; i < (int)ARRAYSIZE(s_specialNames); i++)
    {
        if (SDL_strcasecmp(text, s_specialNames[i].name) == 0)
        {
            *key = s_specialNames[i].key;
            return true;
        }
    }
    if (text[0] != '\0' && text[1] == '\0')
    {
        *key = Normal((u8)text[0]);
        return Bindable(*key);
    }
    return false;
}

void KEYBIND_Load(void)
{
    char text[16];

    KEYBIND_ResetAll();
    for (int i = 0; i < ACTION_COUNT; i++)
    {
        u16 key;
        CONFIG_GetString("keys", (char*)s_actions[i].id, "", text, sizeof(text));
        if (ParseKey(text, &key))
            KEYBIND_Bind(i, key);
    }
}

void KEYBIND_Save(void)
{
    Ready();
    for (int i = 0; i < ACTION_COUNT; i++)
        CONFIG_SetString("keys", (char*)s_actions[i].id, (char*)KeyName(s_actions[i].bound));
}

#endif
