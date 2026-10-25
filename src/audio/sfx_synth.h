#ifndef _AUDIO_SFX_SYNTH_H
#define _AUDIO_SFX_SYNTH_H

#include "common/common.h"

// Software renderer for the original game's four PC-speaker routines
// (pulse_, white_noise, tone and the sweep at 43ae). Classic reproduces what
// the routines sent to the speaker, step by step; enhanced plays the same
// pitches and timing with smooth tones, filtered noise and a little reverb.

#define SFXSYN_RATE 44100

typedef struct SfxBuffer
{
    float* samples;  // mono, SFXSYN_RATE, roughly -1..1
    int count;
    int busy;        // samples the original speaker was busy (no reverb tail)
} SfxBuffer;

bool SFXSYN_Pulse(SfxBuffer* out, bool enhanced, int freq, int delay, int dur, int pulseWidth, int pulseInc);
bool SFXSYN_Noise(SfxBuffer* out, bool enhanced, int rate, int dur, int limit);
bool SFXSYN_Tone(SfxBuffer* out, bool enhanced, int freq, int dur);
bool SFXSYN_Sweep(SfxBuffer* out, bool enhanced, int startFreq, int endFreq, int tickStep, int dur);
// A seamless loop of running water: a fountain's trickle or a waterfall's rush.
bool SFXSYN_Water(SfxBuffer* out, bool waterfall);

// A seamless loop for the background of a place (the same numbers as
// AMBIENCE_* in audio.h).
#define SFXSYN_AMBIENCE_NONE 0
#define SFXSYN_AMBIENCE_SURF 1
#define SFXSYN_AMBIENCE_BIRDS 2
#define SFXSYN_AMBIENCE_CRICKETS 3
#define SFXSYN_AMBIENCE_DRIPS 4
#define SFXSYN_AMBIENCE_HEARTH 5
#define SFXSYN_AMBIENCE_RAIN 6
bool SFXSYN_Ambience(SfxBuffer* out, int kind);

// A roll of thunder, loudness 0-100: a crack first when it's close.
bool SFXSYN_Thunder(SfxBuffer* out, int loudness);

void SFXSYN_Free(SfxBuffer* buffer);

#endif
