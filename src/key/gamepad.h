#ifndef _KEY_GAMEPAD_H
#define _KEY_GAMEPAD_H

#if defined(ENABLE_GAMEPAD)

#include <SDL3/SDL.h>

// Gamepad events from the SDL event loop (added, removed, buttons, sticks).
void GAMEPAD_HandleEvent(const SDL_Event* ev);

// A button pressed or released: the path real events take, also used by
// scripted tests (playtest.c), which have no gamepad.
void GAMEPAD_Button(SDL_GamepadButton button, bool down);

// Repeats a held direction, as a keyboard does; called while the game waits
// for a key.
void GAMEPAD_Tick(void);

#endif

#endif
