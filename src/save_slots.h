#ifndef _SAVE_SLOTS_H
#define _SAVE_SLOTS_H

#if defined(ENABLE_SAVE_SLOTS)

#include <setjmp.h>
#include <time.h>

#define SAVE_SLOT_QUICK 0
#define SAVE_SLOT_COUNT 10 // the quicksave and slots 1-9

// F5, F9, F6 or F7 (U5_KEY_QUICKSAVE etc.) at an overworld, town or dungeon
// command prompt.
void SAVESLOTS_Command(int key);

// Two lines describing what a slot holds, for the slot screen. Returns when
// it was saved, or 0 if the slot is empty.
time_t SAVESLOTS_Describe(int slot, char* line1, char* line2);

// Loading a slot jumps back to the main loop in ultima.c, which then calls
// SAVESLOTS_ResumeLoadedGame to start the game from it.
extern jmp_buf g_saveSlotsLoadJump;
void SAVESLOTS_ResumeLoadedGame(void);

#endif

#endif
