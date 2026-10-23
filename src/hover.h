#ifndef _HOVER_H
#define _HOVER_H

#if defined(ENABLE_HOVER_NAMES)

#include <SDL3/SDL.h>

// Mouse moved to (x, y) in window coordinates, or left the window.
void HOVER_MouseMoved(float x, float y);
void HOVER_MouseLeft(void);

// A key was pressed: hide the label until the mouse moves again, so it
// doesn't sit over the map while the player uses the keyboard.
void HOVER_KeyPressed(void);

// Draws the name of what's under the mouse over the presented frame.
void HOVER_Draw(SDL_Renderer* renderer);

#endif

#endif
