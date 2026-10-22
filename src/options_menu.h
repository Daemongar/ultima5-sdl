#ifndef _OPTIONS_MENU_H
#define _OPTIONS_MENU_H

#if defined(TARGET_SDL)
// Options screen opened from the title menu; saves to ultima5.ini on leaving.
void OPTIONS_MENU_Run(void);

// The same screen opened with F1 during play; restores the game screen after.
void OPTIONS_MENU_RunInGame(void);

// A page listing the game's keys, opened with F2 during play.
void OPTIONS_MENU_ShowShortcutsInGame(void);

// Pages of spells and their reagents, one circle each, opened with F3.
void OPTIONS_MENU_ShowSpellsInGame(void);

#if defined(ENABLE_JOURNAL)
// The journal of what people have said, opened with F4.
void OPTIONS_MENU_ShowJournalInGame(void);
#endif

#if defined(ENABLE_SAVE_SLOTS)
// Screen of save slots, opened with F6 (saving) or F7 (loading). Returns the
// chosen slot (SAVE_SLOT_QUICK or 1-9), or -1 if the player backed out.
int OPTIONS_MENU_ChooseSaveSlot(bool loading);
#endif

// True while the Options or shortcuts screen is showing.
bool OPTIONS_MENU_IsOpen(void);
#endif

#endif
