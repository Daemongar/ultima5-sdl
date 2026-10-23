#ifndef _AUDIO_AUDIO_H
#define _AUDIO_AUDIO_H

#include "common/common.h"

#define BGM_ID_1 1
#define BGM_ID_2 2
#define BGM_ID_3 3
#define BGM_ID_4 4
#define BGM_ID_5 5
#define BGM_ID_6 6
#define BGM_ID_7 7
#define BGM_ID_8 8
#define BGM_ID_9 9
#define BGM_ID_10 10
#define BGM_ID_11 11
#define BGM_ID_12 12
#define BGM_ID_13 13
#define BGM_ID_14 14
#define BGM_ID_15 15
#define BGM_ID_16 16
#define BGM_ID_17 17

#define SFX_TYPE_NONE 0
#define SFX_TYPE_PCM 1
#define SFX_TYPE_SYNTH 2

#define AMBIENT_NONE 0
#define AMBIENT_WATERFALL 2
#define AMBIENT_FOUNTAIN 3

// The background of a place (see AUDIO_SetAmbience).
#define AMBIENCE_NONE 0
#define AMBIENCE_SURF 1
#define AMBIENCE_BIRDS 2
#define AMBIENCE_CRICKETS 3
#define AMBIENCE_DRIPS 4
#define AMBIENCE_HEARTH 5

extern void AUDIO_Initialize(void);
extern void AUDIO_Cleanup(void);

extern void AUDIO_PlayBgm(int id);
extern void AUDIO_QueueBgm(int id);
extern void AUDIO_StopBgm(void);

extern void AUDIO_PlaySfx(int id);
extern void AUDIO_StopSfx(void);
extern int AUDIO_GetSfxType(void);
extern void AUDIO_PlaySynthPulse(int freq, int delay, int dur, int pulseWidth, int pulseInc);
extern void AUDIO_PlaySynthNoise(int rate, int dur, int limit);
extern void AUDIO_PlaySynthTone(int freq, int dur);
extern void AUDIO_PlaySynthSweepTone(int param_1, int param_2, int param_3, int param_4);

// Running water near the party, called with every map update. closeness is
// 0-100. Returns true when the driver plays it as a continuous loop, in which
// case the original's short noise bursts should not be played.
extern bool AUDIO_SetAmbientWater(int kind, int closeness);

// The background sound of the place the party is in (AMBIENCE_*), called with
// every map update; level is 0-100. Fades out when the calls stop.
extern void AUDIO_SetAmbience(int kind, int level);

#endif
