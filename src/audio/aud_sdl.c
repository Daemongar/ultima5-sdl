#include "common/common.h"
#include "common/file.h"
#include "common/options.h"

#include "audio.h"
#include "aud_ops.h"
#include "sfx_map.h"
#include "sfx_synth.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// TODO: clean up

#include <SDL3_mixer/SDL_mixer.h>

static MIX_Mixer* s_mixer;

static MIX_Track* s_bgmTrack;
static MIX_Track* s_sfxTrack;

static SDL_PropertiesID s_bgmTrackProp;

static SDL_AudioSpec s_mixerSpec;

static MIX_Audio* s_bgm[20];
static MIX_Audio* s_sfx[256];

// Optional loop points from BGM/loops.txt, in sample frames of each file: play
// the intro once, then repeat [start, end) so a track never reaches the fade-out
// or restarts from the top. An end of 0 loops the whole file.
static Sint64 s_bgmLoopStart[20];
static Sint64 s_bgmLoopEnd[20];

static void AUDIO_SDL_LoadBgmLoops(void);

static int s_currentBgmId;
static int s_queuedBgmId;

// Sound effects come from the original's PC-speaker routines, rendered in
// software (sfx_synth.c). Each plays on its own voice so effects can overlap.
#define SFX_VOICES 12

// The original game waited for each effect to finish before going on, so an
// effect that starts while the last one is still playing waits its turn,
// unless that would leave it this far behind the game.
#define SPEAKER_QUEUE_NS 1000000000ull

static MIX_Track* s_sfxVoices[SFX_VOICES];
static MIX_Audio* s_sfxVoiceAudio[SFX_VOICES];
static Uint64 s_sfxVoiceStart[SFX_VOICES];
static Uint64 s_speakerFreeAt;

// Fountains and waterfalls with enhanced effects: a loop that keeps playing
// while the game keeps asking for it (every map update) and fades out a moment
// after it stops, such as when walking away or talking to someone.
#define AMBIENT_TIMEOUT_MS 1500
#define AMBIENT_FADE_IN_MS 400
#define AMBIENT_FADE_OUT_MS 800

static MIX_Track* s_ambientTrack;
static MIX_Audio* s_ambientAudio[2]; // waterfall, fountain; made on first use
static SDL_PropertiesID s_ambientPlayProp;
static SDL_Mutex* s_ambientLock;
static SDL_TimerID s_ambientTimer;
static int s_ambientKind;
static float s_ambientLevel;
static Uint64 s_ambientLastCall;

// The background of a place (surf, birds, crickets, drips, a hearth): a loop
// on one of two tracks, so moving from one kind of place to another
// cross-fades. Like the water, it plays while the game keeps asking for it.
#define AMBIENCE_KINDS (AMBIENCE_HEARTH + 1)
#define AMBIENCE_TIMEOUT_MS 2000
#define AMBIENCE_FADE_MS 1500

static MIX_Track* s_ambienceTracks[2];
static int s_ambienceCurrent;
static MIX_Audio* s_ambienceAudio[AMBIENCE_KINDS]; // made on first use
static SDL_PropertiesID s_ambiencePlayProp;
static int s_ambienceKind;
static float s_ambienceLevel;
static Uint64 s_ambienceLastCall;

static float AUDIO_SDL_SfxGain(void)
{
    return g_options.soundEffects ? 0.2f * g_options.soundVolume / 8.0f : 0.0f;
}

static void AUDIO_SDL_ApplyAmbienceGain(void)
{
    MIX_Track* track = s_ambienceTracks[s_ambienceCurrent];
    if (track != NULL)
        MIX_SetTrackGain(track, AUDIO_SDL_SfxGain() * s_ambienceLevel);
}

static void AUDIO_SDL_StopAmbience(void)
{
    if (s_ambienceKind != AMBIENCE_NONE)
        MIX_StopTrack(s_ambienceTracks[s_ambienceCurrent], MIX_MSToFrames(SFXSYN_RATE, AMBIENCE_FADE_MS));
    s_ambienceKind = AMBIENCE_NONE;
}

static MIX_Audio* AUDIO_SDL_AmbienceAudio(int kind)
{
    if (s_ambienceAudio[kind] == NULL)
    {
        SfxBuffer loop;
        if (SFXSYN_Ambience(&loop, kind))
        {
            Sint16* pcm = (Sint16*)malloc((size_t)loop.count * sizeof(Sint16));
            if (pcm)
            {
                for (int i = 0; i < loop.count; i++)
                    pcm[i] = (Sint16)(loop.samples[i] * 32767.0f);
                SDL_AudioSpec spec = { SDL_AUDIO_S16, 1, SFXSYN_RATE };
                s_ambienceAudio[kind] = MIX_LoadRawAudio(s_mixer, pcm, (size_t)loop.count * sizeof(Sint16), &spec);
                free(pcm);
            }
        }
        SFXSYN_Free(&loop);
    }
    return s_ambienceAudio[kind];
}

static void AUDIO_SDL_ApplyAmbientGain(void)
{
    if (s_ambientTrack != NULL)
        MIX_SetTrackGain(s_ambientTrack, AUDIO_SDL_SfxGain() * s_ambientLevel);
}

static void AUDIO_SDL_StopAmbient(void)
{
    if (s_ambientKind != AMBIENT_NONE)
        MIX_StopTrack(s_ambientTrack, MIX_MSToFrames(SFXSYN_RATE, AMBIENT_FADE_OUT_MS));
    s_ambientKind = AMBIENT_NONE;
}

static Uint32 SDLCALL AUDIO_SDL_AmbientTimer(void* userdata, SDL_TimerID timerID, Uint32 interval)
{
    SDL_LockMutex(s_ambientLock);
    if (s_ambientKind != AMBIENT_NONE && SDL_GetTicks() - s_ambientLastCall > AMBIENT_TIMEOUT_MS)
        AUDIO_SDL_StopAmbient();
    if (s_ambienceKind != AMBIENCE_NONE && SDL_GetTicks() - s_ambienceLastCall > AMBIENCE_TIMEOUT_MS)
        AUDIO_SDL_StopAmbience();
    SDL_UnlockMutex(s_ambientLock);
    return interval;
}

static bool AUDIO_SDL_SetAmbientWater(int kind, int closeness)
{
    if (!g_options.enhancedSounds || s_ambientTrack == NULL)
        kind = AMBIENT_NONE;

    SDL_LockMutex(s_ambientLock);
    s_ambientLastCall = SDL_GetTicks();

    if (kind != s_ambientKind)
    {
        AUDIO_SDL_StopAmbient();

        int slot = kind == AMBIENT_WATERFALL ? 0 : 1;
        if (kind != AMBIENT_NONE && s_ambientAudio[slot] == NULL)
        {
            SfxBuffer water;
            if (SFXSYN_Water(&water, kind == AMBIENT_WATERFALL))
            {
                Sint16* pcm = (Sint16*)malloc((size_t)water.count * sizeof(Sint16));
                if (pcm)
                {
                    for (int i = 0; i < water.count; i++)
                        pcm[i] = (Sint16)(water.samples[i] * 32767.0f);
                    SDL_AudioSpec spec = { SDL_AUDIO_S16, 1, SFXSYN_RATE };
                    s_ambientAudio[slot] = MIX_LoadRawAudio(s_mixer, pcm, (size_t)water.count * sizeof(Sint16), &spec);
                    free(pcm);
                }
            }
            SFXSYN_Free(&water);
        }

        if (kind != AMBIENT_NONE && s_ambientAudio[slot] != NULL)
        {
            MIX_SetTrackAudio(s_ambientTrack, s_ambientAudio[slot]);
            MIX_PlayTrack(s_ambientTrack, s_ambientPlayProp);
            s_ambientKind = kind;
        }
    }

    // The fountain's trickle is quieter than a waterfall's rush to begin with.
    float base = kind == AMBIENT_FOUNTAIN ? 2.5f : 1.0f;
    s_ambientLevel = base * (0.3f + 0.7f * closeness / 100.0f);
    AUDIO_SDL_ApplyAmbientGain();

    bool playing = s_ambientKind != AMBIENT_NONE;
    SDL_UnlockMutex(s_ambientLock);
    return playing;
}

static void AUDIO_SDL_SetAmbience(int kind, int level)
{
    if (kind < AMBIENCE_NONE || kind >= AMBIENCE_KINDS || s_ambienceTracks[0] == NULL)
        kind = AMBIENCE_NONE;

    SDL_LockMutex(s_ambientLock);
    s_ambienceLastCall = SDL_GetTicks();

    if (kind != s_ambienceKind)
    {
        // The old loop fades out on its track while the new one fades in on
        // the other.
        debug("ambience: %d", kind); // checked by tools/playtest.py
        AUDIO_SDL_StopAmbience();
        MIX_Audio* audio = kind != AMBIENCE_NONE ? AUDIO_SDL_AmbienceAudio(kind) : NULL;
        if (audio != NULL)
        {
            s_ambienceCurrent ^= 1;
            MIX_SetTrackAudio(s_ambienceTracks[s_ambienceCurrent], audio);
            MIX_PlayTrack(s_ambienceTracks[s_ambienceCurrent], s_ambiencePlayProp);
            s_ambienceKind = kind;
        }
    }

    s_ambienceLevel = level / 100.0f;
    AUDIO_SDL_ApplyAmbienceGain();
    SDL_UnlockMutex(s_ambientLock);
}

static int AUDIO_SDL_PickVoice(void)
{
    int oldest = 0;
    for (int v = 0; v < SFX_VOICES; v++)
    {
        if (!MIX_TrackPlaying(s_sfxVoices[v]))
            return v;
        if (s_sfxVoiceStart[v] < s_sfxVoiceStart[oldest])
            oldest = v;
    }
    return oldest;
}

static void AUDIO_SDL_PlaySynth(SfxBuffer* sound)
{
    Uint64 now = SDL_GetTicksNS();
    Uint64 wait = 0;
    if (s_speakerFreeAt > now && s_speakerFreeAt - now < SPEAKER_QUEUE_NS)
        wait = s_speakerFreeAt - now;

    Uint64 busyNs = (Uint64)sound->busy * 1000000000ull / SFXSYN_RATE;
    if (now + wait + busyNs > s_speakerFreeAt)
        s_speakerFreeAt = now + wait + busyNs;

    // Waiting is done with silence at the start of the sound.
    int lead = (int)(wait * SFXSYN_RATE / 1000000000ull);
    int total = lead + sound->count;
    Sint16* pcm = (Sint16*)calloc(total, sizeof(Sint16));
    if (!pcm)
        return;

    for (int i = 0; i < sound->count; i++)
    {
        float v = sound->samples[i];
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        pcm[lead + i] = (Sint16)(v * 32767.0f);
    }

    SDL_AudioSpec spec = { SDL_AUDIO_S16, 1, SFXSYN_RATE };
    MIX_Audio* audio = MIX_LoadRawAudio(s_mixer, pcm, (size_t)total * sizeof(Sint16), &spec);
    free(pcm);
    if (!audio)
        return;

    int v = AUDIO_SDL_PickVoice();
    MIX_SetTrackAudio(s_sfxVoices[v], audio);
    MIX_PlayTrack(s_sfxVoices[v], 0);
    if (s_sfxVoiceAudio[v])
        MIX_DestroyAudio(s_sfxVoiceAudio[v]);
    s_sfxVoiceAudio[v] = audio;
    s_sfxVoiceStart[v] = now;
}

void AUDIO_SDL_LoadBgmTable(void);
void AUDIO_SDL_LoadSfxTable(void);
void AUDIO_SDL_PlayBgmSub(int id);
void SDLCALL AUDIO_SDL_OnBgmStopped(void* userdata, MIX_Track* track);

// Volume from the Options screen: 0-10, where the default of 8 gives the
// original levels; switching a category off silences its track.
void AUDIO_SDL_ApplySoundOptions(void)
{
    if (s_bgmTrack != NULL)
        MIX_SetTrackGain(s_bgmTrack, g_options.music ? 0.3f * g_options.musicVolume / 8.0f : 0.0f);
    float sfxGain = AUDIO_SDL_SfxGain();
    if (s_sfxTrack != NULL)
        MIX_SetTrackGain(s_sfxTrack, sfxGain);
    for (int v = 0; v < SFX_VOICES; v++)
    {
        if (s_sfxVoices[v] != NULL)
            MIX_SetTrackGain(s_sfxVoices[v], sfxGain);
    }
    AUDIO_SDL_ApplyAmbientGain();
    AUDIO_SDL_ApplyAmbienceGain();
}

void AUDIO_SDL_Init(void)
{
    MIX_Init();
    s_mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
    MIX_GetMixerFormat(s_mixer, &s_mixerSpec);

    s_bgmTrack = MIX_CreateTrack(s_mixer);
    s_sfxTrack = MIX_CreateTrack(s_mixer);
    for (int v = 0; v < SFX_VOICES; v++)
        s_sfxVoices[v] = MIX_CreateTrack(s_mixer);

    s_ambientTrack = MIX_CreateTrack(s_mixer);
    s_ambientPlayProp = SDL_CreateProperties();
    SDL_SetNumberProperty(s_ambientPlayProp, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
    SDL_SetNumberProperty(s_ambientPlayProp, MIX_PROP_PLAY_FADE_IN_MILLISECONDS_NUMBER, AMBIENT_FADE_IN_MS);
    s_ambientLock = SDL_CreateMutex();
    s_ambientTimer = SDL_AddTimer(250, AUDIO_SDL_AmbientTimer, NULL);

    for (int i = 0; i < 2; i++)
        s_ambienceTracks[i] = MIX_CreateTrack(s_mixer);
    s_ambiencePlayProp = SDL_CreateProperties();
    SDL_SetNumberProperty(s_ambiencePlayProp, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
    SDL_SetNumberProperty(s_ambiencePlayProp, MIX_PROP_PLAY_FADE_IN_MILLISECONDS_NUMBER, AMBIENCE_FADE_MS);

    AUDIO_SDL_ApplySoundOptions();

    s_bgmTrackProp = SDL_CreateProperties();
    SDL_SetNumberProperty(s_bgmTrackProp, MIX_PROP_PLAY_LOOPS_NUMBER, -1);

    MIX_SetTrackStoppedCallback(s_bgmTrack, AUDIO_SDL_OnBgmStopped, NULL);

    AUDIO_SDL_LoadBgmTable();
    AUDIO_SDL_LoadSfxTable();
}

void AUDIO_SDL_Cleanup(void)
{
    // TODO: release audios

    SDL_RemoveTimer(s_ambientTimer);
    MIX_DestroyTrack(s_ambientTrack);
    s_ambientTrack = NULL;
    for (int i = 0; i < 2; i++)
    {
        if (s_ambientAudio[i])
            MIX_DestroyAudio(s_ambientAudio[i]);
        s_ambientAudio[i] = NULL;
    }
    SDL_DestroyProperties(s_ambientPlayProp);
    for (int i = 0; i < 2; i++)
    {
        MIX_DestroyTrack(s_ambienceTracks[i]);
        s_ambienceTracks[i] = NULL;
    }
    for (int i = 0; i < AMBIENCE_KINDS; i++)
    {
        if (s_ambienceAudio[i])
            MIX_DestroyAudio(s_ambienceAudio[i]);
        s_ambienceAudio[i] = NULL;
    }
    SDL_DestroyProperties(s_ambiencePlayProp);
    SDL_DestroyMutex(s_ambientLock);
    s_ambientLock = NULL;

    for (int v = 0; v < SFX_VOICES; v++)
    {
        MIX_DestroyTrack(s_sfxVoices[v]);
        s_sfxVoices[v] = NULL;
        if (s_sfxVoiceAudio[v])
            MIX_DestroyAudio(s_sfxVoiceAudio[v]);
        s_sfxVoiceAudio[v] = NULL;
    }

    MIX_SetTrackStoppedCallback(s_bgmTrack, NULL, NULL);
    MIX_DestroyTrack(s_bgmTrack);
    MIX_DestroyTrack(s_sfxTrack);
    MIX_DestroyMixer(s_mixer);
    s_mixer = NULL;
    MIX_Quit();
}

void AUDIO_SDL_LoadBgmTable(void)
{
    // Tracks should be .ogg. .mp3 is tried as a fallback but only loads when
    // SDL_mixer has MP3 support, which the vcpkg build for Windows lacks.
    static const char* const exts[] = { "ogg", "mp3" };

    for (int i = 1; i <= 15; i++)
    {
        char fileName[256] = {0,};

        s_bgm[i] = NULL;
        for (int e = 0; e < ARRAYSIZE(exts) && !s_bgm[i]; e++)
        {
            char resolved[256];
            sprintf(fileName, "BGM/%02d.%s", i, exts[e]);
            FILE_ResolvePath(fileName, resolved, sizeof(resolved));
            s_bgm[i] = MIX_LoadAudio(s_mixer, resolved, false);
        }

        if (!s_bgm[i])
        {
            printf("Couldn't load BGM/%02d.ogg or .mp3: %s\n", i, SDL_GetError());
        }
    }

    AUDIO_SDL_LoadBgmLoops();
}

// BGM/loops.txt lines: "<track> <loop start> <loop end>", '#' starts a comment.
// The frames belong to the exact files they were measured on, so they live
// beside the music rather than in the code.
static void AUDIO_SDL_LoadBgmLoops(void)
{
    char line[128];
    FILE* fp = FILE_Open("BGM/loops.txt", "r");

    for (int i = 0; i < ARRAYSIZE(s_bgmLoopEnd); i++)
    {
        s_bgmLoopStart[i] = s_bgmLoopEnd[i] = 0;
    }

    if (fp == NULL)
        return;

    while (fgets(line, sizeof(line), fp) != NULL)
    {
        int id;
        long long start, end;

        if (line[0] == '#' || sscanf(line, "%d %lld %lld", &id, &start, &end) != 3)
            continue;

        if (id > 0 && id < ARRAYSIZE(s_bgmLoopEnd) && start >= 0 && end > start)
        {
            s_bgmLoopStart[id] = start;
            s_bgmLoopEnd[id] = end;
        }
    }

    fclose(fp);
}

void AUDIO_SDL_LoadSfxTable(void)
{
    for (int i = 0; i < SFX_RULE_COUNT; i++)
    {
        const SfxRule* r = &g_sfxRules[i];
        char fileName[256] = {0,};
        if (s_sfx[r->sfxId] != NULL)
        {
            continue;
        }
        char resolved[256];
        sprintf(fileName, "SFX/%s", r->fileName);
        FILE_ResolvePath(fileName, resolved, sizeof(resolved));
        s_sfx[r->sfxId] = MIX_LoadAudio(s_mixer, resolved, true);
    }
}

void AUDIO_SDL_PlaySfx(int id)
{
    debug("AUDIO_SDL_PlaySfx(%d)", id);

    if (s_sfx[id] == NULL)
        return;

    MIX_SetTrackAudio(s_sfxTrack, s_sfx[id]);
    MIX_PlayTrack(s_sfxTrack, 0);
    const char* err = SDL_GetError();
    if (err) { puts(err); }
}

void AUDIO_SDL_StopSfx(void)
{
    Sint64 frames = MIX_MSToFrames(s_mixerSpec.freq, 10);
    MIX_StopTrack(s_sfxTrack, frames);
    for (int v = 0; v < SFX_VOICES; v++)
        MIX_StopTrack(s_sfxVoices[v], frames);
    s_speakerFreeAt = 0;
    const char* err = SDL_GetError();
    if (err) { puts(err); }
}

void AUDIO_SDL_PlayBgmSub(int id)
{
    s_currentBgmId = id;

    if (s_bgm[id] == NULL)
        return;

    MIX_SetTrackAudio(s_bgmTrack, s_bgm[id]);
    SDL_SetNumberProperty(s_bgmTrackProp, MIX_PROP_PLAY_LOOP_START_FRAME_NUMBER, s_bgmLoopStart[id]);
    SDL_SetNumberProperty(s_bgmTrackProp, MIX_PROP_PLAY_MAX_FRAME_NUMBER, s_bgmLoopEnd[id] > 0 ? s_bgmLoopEnd[id] : -1);
    MIX_PlayTrack(s_bgmTrack, s_bgmTrackProp);
    const char* err = SDL_GetError();
    if (err) { puts(err); }
}

void AUDIO_SDL_PlayBgm(int id)
{
    debug("AUDIO_SDL_PlayBgm(%d)", id);

    s_queuedBgmId = 0;

    if (s_currentBgmId == id)
    {
        MIX_SetTrackLoops(s_bgmTrack, -1);
        return;
    }

    AUDIO_SDL_PlayBgmSub(id);
}

void AUDIO_SDL_QueueBgm(int id)
{
    debug("AUDIO_SDL_QueueBgm(%d)", id);

    if (s_bgm[id] == NULL)
        return;

    if (s_currentBgmId == 0 || !MIX_TrackPlaying(s_bgmTrack))
    {
        s_queuedBgmId = 0;
        AUDIO_SDL_PlayBgmSub(id);
        return;
    }

    s_queuedBgmId = id;
    MIX_SetTrackLoops(s_bgmTrack, 0);
}

void SDLCALL AUDIO_SDL_OnBgmStopped(void* userdata, MIX_Track* track)
{
    int queuedBgmId = s_queuedBgmId;

    s_queuedBgmId = 0;
    s_currentBgmId = 0;

    if (queuedBgmId == 0)
    {
        return;
    }

    AUDIO_SDL_PlayBgmSub(queuedBgmId);
}

void AUDIO_SDL_StopBgm(void)
{
    debug("AUDIO_SDL_StopBgm()");

    s_currentBgmId = 0;
    s_queuedBgmId = 0;

    Sint64 frames = MIX_MSToFrames(s_mixerSpec.freq, 500);
    MIX_StopTrack(s_bgmTrack, frames);
    const char* err = SDL_GetError();
    if (err) { puts(err); }
}

void AUDIO_SDL_Noop(void)
{
    // no-op
}

static int AUDIO_SDL_GetSfxType(void)
{
    return SFX_TYPE_SYNTH;
}

static void AUDIO_SDL_PlaySynthPulse(int freq, int delay, int dur, int pulseWidth, int pulseInc)
{
    SfxBuffer sound;
    if (SFXSYN_Pulse(&sound, g_options.enhancedSounds, freq, delay, dur, pulseWidth, pulseInc))
        AUDIO_SDL_PlaySynth(&sound);
    SFXSYN_Free(&sound);
}

static void AUDIO_SDL_PlaySynthNoise(int rate, int dur, int limit)
{
    SfxBuffer sound;
    if (SFXSYN_Noise(&sound, g_options.enhancedSounds, rate, dur, limit))
        AUDIO_SDL_PlaySynth(&sound);
    SFXSYN_Free(&sound);
}

static void AUDIO_SDL_PlaySynthTone(int freq, int dur)
{
    SfxBuffer sound;
    if (SFXSYN_Tone(&sound, g_options.enhancedSounds, freq, dur))
        AUDIO_SDL_PlaySynth(&sound);
    SFXSYN_Free(&sound);
}

static void AUDIO_SDL_PlaySynthSweepTone(int startFreq, int endFreq, int tickStep, int dur)
{
    SfxBuffer sound;
    if (SFXSYN_Sweep(&sound, g_options.enhancedSounds, startFreq, endFreq, tickStep, dur))
        AUDIO_SDL_PlaySynth(&sound);
    SFXSYN_Free(&sound);
}

static AudioMusicDriverOps s_musicOps =
{
    .Initialize = AUDIO_SDL_Init,
    .Cleanup = AUDIO_SDL_Cleanup,
    .PlayBgm = AUDIO_SDL_PlayBgm,
    .QueueBgm = AUDIO_SDL_QueueBgm,
    .StopBgm = AUDIO_SDL_StopBgm
};

static AudioSfxDriverOps s_sfxOps =
{
    .Initialize = AUDIO_SDL_Noop,
    .Cleanup = AUDIO_SDL_Noop,
    .PlaySfx = AUDIO_SDL_PlaySfx,
    .StopSfx = AUDIO_SDL_StopSfx,
    .GetSfxType = AUDIO_SDL_GetSfxType,
    .PlaySynthPulse = AUDIO_SDL_PlaySynthPulse,
    .PlaySynthNoise = AUDIO_SDL_PlaySynthNoise,
    .PlaySynthTone = AUDIO_SDL_PlaySynthTone,
    .PlaySynthSweepTone = AUDIO_SDL_PlaySynthSweepTone,
    .SetAmbientWater = AUDIO_SDL_SetAmbientWater,
    .SetAmbience = AUDIO_SDL_SetAmbience
};

AudioMusicDriverOps* AUDIO_SDL_GetMusicOps(void)
{
    return &s_musicOps;
}

AudioSfxDriverOps* AUDIO_SDL_GetSfxOps(void)
{
    return &s_sfxOps;
}
