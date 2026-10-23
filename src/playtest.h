#ifndef _PLAYTEST_H
#define _PLAYTEST_H

#if defined(ENABLE_PLAYTEST)

// --script FILE: play the keys in FILE instead of reading the keyboard, for
// tools/playtest.py. Call before anything else in main.
void PLAYTEST_Init(int argc, char** argv);

// True when a script is playing. The game then uses a fixed random seed and
// doesn't remember the data folder it was given.
bool PLAYTEST_Active(void);

// Called when the game looks for a key and none is waiting: presses the
// script's next key, or exits once the script is done.
void PLAYTEST_FeedKey(void);

#endif

#endif
