#ifndef _KEYBIND_H
#define _KEYBIND_H

#if defined(ENABLE_KEYBIND)

#include "common/common.h"

// Where the game is reading a key: remaps apply only to commands and
// directions, never to typing (names, conversation, spells).
enum
{
    KEYBIND_NONE,
    KEYBIND_COMMAND,   // a command, or a step
    KEYBIND_DIRECTION, // which way, after a command (or aiming)
};

void KEYBIND_SetContext(int context);

// A key as KEY_PollKey has it (letters, 0x100 + U5_KEY_* for arrows and the
// like) to the key the game should see: the original key of the action it's
// bound to, itself, or 0 to ignore it (an action's own key moved elsewhere).
u16 KEYBIND_Translate(u16 key);

// The actions, for the Options screen and the shortcuts page.
int KEYBIND_Count(void);
const char* KEYBIND_Label(int action);
// The key bound to an action, as text ("A", "Up", "Space").
const char* KEYBIND_KeyName(int action);
// True if the key can be bound; then binds it to the action, and the action
// that had it takes the action's old key.
bool KEYBIND_Bind(int action, u16 key);
void KEYBIND_Reset(int action); // back to its own key (swapping as Bind does)
void KEYBIND_ResetAll(void);

// From and to the [keys] section of ultima5.ini.
void KEYBIND_Load(void);
void KEYBIND_Save(void);

#endif

#endif
