#ifndef _POINTCLICK_H
#define _POINTCLICK_H

#if defined(ENABLE_POINT_CLICK)

#include <SDL3/SDL.h>

// Called by the main loops (mainout.c, town.c) around reading a command key:
// clicks act, and walks take their steps, only then.
void POINT_SetAtPrompt(bool atPrompt);

// A mouse button pressed at (x, y) in window coordinates; true if it was a
// click on the map that this handled.
bool POINT_MouseDown(float x, float y, int button);

// The game wants a key and has none: the next step of a walk, if one is due.
void POINT_Tick(void);

// A key was pressed: stop walking.
void POINT_Cancel(void);

// Marks where the party is walking to, over the presented frame.
void POINT_Draw(SDL_Renderer* renderer);

#endif

#endif
