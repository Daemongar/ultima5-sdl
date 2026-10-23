#ifndef _AMBIENCE_H
#define _AMBIENCE_H

#if defined(ENABLE_AMBIENCE)
// Picks the background sound for where the party is and asks the audio
// driver for it; called with every map update and dungeon view.
void AMBIENCE_Update(void);
#endif

#endif
