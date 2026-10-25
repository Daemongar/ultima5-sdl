#include "sfx_synth.h"

#include <math.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// The original routines (ULTIMA.EXE 2192, 223c, 22c0, 43ae) are busy loops
// scaled by the timer calibration in D_5356, so their real speed depended on
// the machine. These are the lengths of one unit of each loop, chosen so the
// longest effects (the trapdoor fall, the whirlpool) and the shortest (clock
// ticks, footsteps) come out at sensible lengths.
#define PULSE_STEP_SEC 40e-6  // pulse_(): one step with Delay=1 (~25 kHz)
#define NOISE_UNIT_SEC 52e-6  // white_noise(): one unit of Rate and Dura
#define DELAY_UNIT_SEC 206e-6 // tone() and the sweep: one unit of 20c8's delay

#define PIT_CLOCK 1193182.0
#define MAX_SECONDS 8.0

#define CLASSIC_GAIN 0.35f
#define ENHANCED_GAIN 0.4f
#define CLASSIC_TAIL_SEC 0.03
#define REVERB_TAIL_SEC 0.7
#define REVERB_WET 0.3f

static int Samples(double seconds)
{
    if (seconds > MAX_SECONDS)
        seconds = MAX_SECONDS;
    if (seconds < 0)
        seconds = 0;
    return (int)(seconds * SFXSYN_RATE + 0.5);
}

static bool Allocate(SfxBuffer* out, int busy, double tailSeconds)
{
    out->busy = busy;
    out->count = busy + (int)(tailSeconds * SFXSYN_RATE);
    out->samples = (float*)calloc(out->count > 0 ? out->count : 1, sizeof(float));
    return out->samples != NULL;
}

void SFXSYN_Free(SfxBuffer* buffer)
{
    free(buffer->samples);
    buffer->samples = NULL;
    buffer->count = 0;
    buffer->busy = 0;
}

// The frequency the timer actually produced for a requested one: the
// routines program channel 2 with 1193182 / freq.
static double PitFrequency(int freq)
{
    if (freq < 19)
        freq = 19;
    if (freq > 0xffff)
        freq = 0xffff;
    u16 divisor = (u16)(PIT_CLOCK / freq);
    return PIT_CLOCK / divisor;
}

// white_noise()'s generator (223c-2265), kept across calls like D_545c.
static u16 s_noiseSeed;

static int NextNoiseFreq(int limit)
{
    u16 ax = (u16)(s_noiseSeed + 0x9248);
    ax = (u16)((ax >> 3) | (ax << 13));
    ax ^= 0x9248;
    ax = (u16)(ax + 0x11);
    s_noiseSeed = ax;

    u16 range = (u16)(limit - 100 + 1);
    if (range == 0)
        range = 1;
    return 100 + ax % range;
}

static int NoiseSteps(int rate, int dur)
{
    // counter += Rate until it reaches Dura; the first step always plays.
    int steps = 0;
    s16 counter = 0;
    do
    {
        counter = (s16)(counter + rate);
        steps++;
    } while (counter < (s16)dur && steps < 100000);
    return steps;
}

// ---------------------------------------------------------------------------
// Classic: the speaker as the routines drove it

typedef struct Speaker
{
    float* buf;
    int count;
    double t;     // position in samples
    double phase; // timer square wave, in cycles
} Speaker;

// Hold the speaker at a level (0 or 1) for len samples, adding the exact area
// to each output sample so fast switching doesn't alias.
static void SpeakerHold(Speaker* s, float level, double len)
{
    while (len > 0 && s->t < s->count)
    {
        int i = (int)s->t;
        double take = i + 1 - s->t;
        if (take > len)
            take = len;
        s->buf[i] += (float)(level * take);
        s->t += take;
        len -= take;
    }
    if (len > 0)
        s->t += len;
}

// The timer's square wave at freqHz for len samples. The phase carries over
// between calls, as reprogramming the timer doesn't restart its wave.
static void SpeakerSquare(Speaker* s, double freqHz, double len)
{
    double perSample = freqHz / SFXSYN_RATE;
    while (len > 0 && s->t < s->count)
    {
        bool high = s->phase < 0.5;
        double take = ((high ? 0.5 : 1.0) - s->phase) / perSample;
        if (take <= 0)
            take = 1e-9;
        if (take > len)
            take = len;
        SpeakerHold(s, high ? 1.0f : 0.0f, take);
        s->phase += take * perSample;
        if (s->phase >= 1.0)
            s->phase -= 1.0;
        len -= take;
    }
}

// A small speaker's response: rolls off the top end and doesn't pass DC.
static void SpeakerFinish(SfxBuffer* out)
{
    double lowpass = exp(-2.0 * M_PI * 5000.0 / SFXSYN_RATE);
    double dcBlock = exp(-2.0 * M_PI * 40.0 / SFXSYN_RATE);
    double lp1 = 0, lp2 = 0, prev = 0, hp = 0;

    for (int i = 0; i < out->count; i++)
    {
        lp1 = out->samples[i] + lowpass * (lp1 - out->samples[i]);
        lp2 = lp1 + lowpass * (lp2 - lp1);
        hp = lp2 - prev + dcBlock * hp;
        prev = lp2;
        out->samples[i] = (float)hp * CLASSIC_GAIN;
    }
}

static bool ClassicPulse(SfxBuffer* out, int freq, int delay, int dur, int pulseWidth, int pulseInc)
{
    double stepLen = PULSE_STEP_SEC * delay * SFXSYN_RATE;
    if (!Allocate(out, Samples(dur * PULSE_STEP_SEC * delay), CLASSIC_TAIL_SEC))
        return false;

    // 21f1-222c: a 16-bit phase advances by Freq each step and the speaker is
    // on while it is above a threshold that starts at P_Wid and moves by P_Inc.
    // "On" lets the timer's ~20 kHz carrier through, which the cone can only
    // follow as half its travel.
    Speaker s = {out->samples, out->busy, 0, 0};
    u16 phase = 0;
    u16 threshold = (u16)pulseWidth;
    for (int i = 0; i < dur && s.t < s.count; i++)
    {
        phase = (u16)(phase + freq);
        SpeakerHold(&s, phase > threshold ? 1.0f : 0.0f, stepLen);
        threshold = (u16)(threshold + pulseInc);
    }

    SpeakerFinish(out);
    return true;
}

static bool ClassicNoise(SfxBuffer* out, int rate, int dur, int limit)
{
    int steps = NoiseSteps(rate, dur);
    double stepLen = rate * NOISE_UNIT_SEC * SFXSYN_RATE;
    if (!Allocate(out, Samples(steps * rate * NOISE_UNIT_SEC), CLASSIC_TAIL_SEC))
        return false;

    // A tone at a random pitch between 100 Hz and Limit for each step.
    Speaker s = {out->samples, out->busy, 0, 0};
    for (int i = 0; i < steps && s.t < s.count; i++)
    {
        SpeakerSquare(&s, PitFrequency(NextNoiseFreq(limit)), stepLen);
    }

    SpeakerFinish(out);
    return true;
}

static bool ClassicTone(SfxBuffer* out, int freq, int dur)
{
    if (!Allocate(out, Samples(dur * DELAY_UNIT_SEC), CLASSIC_TAIL_SEC))
        return false;

    Speaker s = {out->samples, out->busy, 0, 0};
    SpeakerSquare(&s, PitFrequency(freq), out->busy);

    SpeakerFinish(out);
    return true;
}

// 43ae: the pitch moves in steps of (end - start) * TickStep / Dura, each held
// for TickStep units. The product is kept to 16 bits, as in the original.
static int SweepStepHz(int startFreq, int endFreq, int tickStep, int dur)
{
    s16 product = (s16)((s16)(endFreq - startFreq) * (s16)tickStep);
    return product / (s16)dur;
}

static bool ClassicSweep(SfxBuffer* out, int startFreq, int endFreq, int tickStep, int dur)
{
    int steps = (dur + tickStep - 1) / tickStep;
    double stepLen = tickStep * DELAY_UNIT_SEC * SFXSYN_RATE;
    if (!Allocate(out, Samples(steps * tickStep * DELAY_UNIT_SEC), CLASSIC_TAIL_SEC))
        return false;

    int stepHz = SweepStepHz(startFreq, endFreq, tickStep, dur);
    s16 freq = (s16)startFreq;

    Speaker s = {out->samples, out->busy, 0, 0};
    for (int i = 0; i < steps && s.t < s.count; i++)
    {
        SpeakerSquare(&s, PitFrequency((u16)freq), stepLen);
        freq = (s16)(freq + stepHz);
    }

    SpeakerFinish(out);
    return true;
}

// ---------------------------------------------------------------------------
// Enhanced: the same pitches and timing with softer sounds

// Rises over attack samples and falls over release samples at the end of len.
static float Envelope(int i, int len, int attack, int release)
{
    float gain = 1.0f;
    if (attack > 0 && i < attack)
        gain = (float)i / attack;
    if (release > 0 && i > len - release)
        gain *= (float)(len - i) / release;
    return gain < 0 ? 0 : gain;
}

// A short Schroeder reverb (four damped combs into two allpasses), mixed into
// the buffer, whose tail has room for it.
static void AddReverb(SfxBuffer* out, float wet)
{
    static const int combLen[4] = {1116, 1188, 1277, 1356};
    static const int allpassLen[2] = {556, 441};
    float* comb[4];
    float* allpass[2];
    float combStore[4] = {0};
    int combPos[4] = {0};
    int allpassPos[2] = {0};

    float* memory = (float*)calloc(1116 + 1188 + 1277 + 1356 + 556 + 441, sizeof(float));
    if (!memory)
        return;

    float* p = memory;
    for (int c = 0; c < 4; c++)
    {
        comb[c] = p;
        p += combLen[c];
    }
    for (int a = 0; a < 2; a++)
    {
        allpass[a] = p;
        p += allpassLen[a];
    }

    for (int i = 0; i < out->count; i++)
    {
        float in = out->samples[i] * 0.06f;
        float sum = 0;

        for (int c = 0; c < 4; c++)
        {
            float y = comb[c][combPos[c]];
            combStore[c] = y * 0.7f + combStore[c] * 0.3f;
            comb[c][combPos[c]] = in + combStore[c] * 0.82f;
            if (++combPos[c] == combLen[c])
                combPos[c] = 0;
            sum += y;
        }

        for (int a = 0; a < 2; a++)
        {
            float delayed = allpass[a][allpassPos[a]];
            allpass[a][allpassPos[a]] = sum + delayed * 0.5f;
            if (++allpassPos[a] == allpassLen[a])
                allpassPos[a] = 0;
            sum = delayed - sum;
        }

        out->samples[i] += sum * wet;
    }

    free(memory);
}

static void EnhancedFinish(SfxBuffer* out, float wet)
{
    if (wet > 0)
        AddReverb(out, wet);

    float peak = 0;
    for (int i = 0; i < out->count; i++)
    {
        float v = fabsf(out->samples[i]);
        if (v > peak)
            peak = v;
    }
    float scale = peak > 0.9f ? 0.9f / peak : 1.0f;

    // Fade the very end so a cut-off reverb tail can't click.
    int fade = SFXSYN_RATE / 100;
    for (int i = 0; i < out->count; i++)
    {
        float gain = scale;
        if (i > out->count - fade)
            gain *= (float)(out->count - i) / fade;
        out->samples[i] *= gain;
    }
}

// The pulse wave the original made, rebuilt from its first few harmonics.
// A pulse with duty d has harmonic k at sin(pi*k*d)/k, so as P_Inc moves the
// threshold the tone fades and changes colour exactly as it did, without the
// buzz of the harmonics above.
static bool EnhancedPulse(SfxBuffer* out, int freq, int delay, int dur, int pulseWidth, int pulseInc)
{
    double stepSec = PULSE_STEP_SEC * delay;
    if (!Allocate(out, Samples(dur * stepSec), REVERB_TAIL_SEC))
        return false;

    double f0 = (freq / 65536.0) / stepSec;
    int harmonics = (int)(8000.0 / f0);
    if (harmonics > 6)
        harmonics = 6;
    if (harmonics < 1)
        harmonics = 1;

    double stepLen = stepSec * SFXSYN_RATE;
    double smoothing = exp(-1.0 / (0.002 * SFXSYN_RATE));
    double duty = -1;
    double phase = 0;
    int attack = SFXSYN_RATE * 4 / 1000;
    int release = SFXSYN_RATE * 25 / 1000;
    if (release > out->busy / 3)
        release = out->busy / 3;

    for (int i = 0; i < out->busy; i++)
    {
        int step = (int)(i / stepLen);
        u16 threshold = (u16)(pulseWidth + pulseInc * step);
        double target = (65535 - threshold) / 65536.0;
        duty = duty < 0 ? target : target + smoothing * (duty - target);

        double sum = 0;
        for (int k = 1; k <= harmonics; k++)
        {
            // An extra 1/sqrt(k) rolloff takes the edge off the upper harmonics.
            sum += sin(M_PI * k * duty) / (k * sqrt((double)k)) * sin(2.0 * M_PI * k * phase);
        }
        phase += f0 / SFXSYN_RATE;
        phase -= floor(phase);

        out->samples[i] = (float)(sum * 0.8) * ENHANCED_GAIN * Envelope(i, out->busy, attack, release);
    }

    EnhancedFinish(out, REVERB_WET);
    return true;
}

// A two-pole state-variable lowpass, for shaping white noise.
typedef struct Lowpass
{
    float f, q, low, band;
} Lowpass;

static void LowpassInit(Lowpass* lp, double cutoff, double resonance)
{
    if (cutoff > SFXSYN_RATE / 6)
        cutoff = SFXSYN_RATE / 6;
    lp->f = (float)(2.0 * sin(M_PI * cutoff / SFXSYN_RATE));
    lp->q = (float)(1.0 / resonance);
    lp->low = lp->band = 0;
}

static float LowpassRun(Lowpass* lp, float in)
{
    lp->low += lp->f * lp->band;
    float high = in - lp->low - lp->q * lp->band;
    lp->band += lp->f * high;
    return lp->low;
}

static u32 s_whiteSeed = 0x12345678u;

static float White(void)
{
    s_whiteSeed ^= s_whiteSeed << 13;
    s_whiteSeed ^= s_whiteSeed >> 17;
    s_whiteSeed ^= s_whiteSeed << 5;
    return (float)((s_whiteSeed >> 8) / 8388608.0 - 1.0);
}

// white_noise() has two characters. With long steps (spells) it plays a run of
// random notes, kept here as soft chimes on the same notes. With short steps
// (footsteps, hits, water, quakes) it is noise up to Limit, played here as
// filtered noise; the shortest bursts become small taps.
static bool EnhancedNoise(SfxBuffer* out, int rate, int dur, int limit)
{
    int steps = NoiseSteps(rate, dur);
    double stepSec = rate * NOISE_UNIT_SEC;
    int busy = Samples(steps * stepSec);

    if (stepSec >= 0.008)
    {
        if (!Allocate(out, busy, REVERB_TAIL_SEC))
            return false;

        int stepLen = (int)(stepSec * SFXSYN_RATE);
        int ring = SFXSYN_RATE * 120 / 1000;
        int attack = SFXSYN_RATE * 3 / 1000;
        double decay = stepSec * 1.2 * SFXSYN_RATE;

        for (int n = 0; n < steps; n++)
        {
            double freqHz = PitFrequency(NextNoiseFreq(limit));
            int start = n * stepLen;
            for (int i = 0; i < stepLen + ring && start + i < out->count; i++)
            {
                double t = (double)i / SFXSYN_RATE;
                double env = exp(-i / decay) * (i < attack ? (double)i / attack : 1.0);
                double v = sin(2.0 * M_PI * freqHz * t) + 0.25 * sin(4.0 * M_PI * freqHz * t);
                out->samples[start + i] += (float)(v * env * 0.6) * ENHANCED_GAIN;
            }
        }

        EnhancedFinish(out, REVERB_WET);
        return true;
    }

    // Give the shortest bursts enough length to be heard as a tap.
    bool tap = busy < SFXSYN_RATE * 20 / 1000;
    int len = tap ? SFXSYN_RATE * 35 / 1000 : busy;
    if (!Allocate(out, len, REVERB_TAIL_SEC))
        return false;
    out->busy = busy;

    // Longer steps between random pitches make a coarser, duller noise.
    double cutoff = limit * 0.6 / sqrt((double)rate);
    if (cutoff < 150)
        cutoff = 150;
    if (cutoff > 5000)
        cutoff = 5000;

    Lowpass lp;
    LowpassInit(&lp, cutoff, 1.2);

    int attack = SFXSYN_RATE * 2 / 1000;
    int release = len * 3 / 10;
    double tapDecay = SFXSYN_RATE * 8 / 1000;

    double sum = 0;
    for (int i = 0; i < len; i++)
    {
        float env = tap ? (float)exp(-i / tapDecay) * (i < attack ? (float)i / attack : 1.0f)
                        : Envelope(i, len, attack, release);
        out->samples[i] = LowpassRun(&lp, White()) * env;
        sum += out->samples[i] * out->samples[i];
    }

    // Low cutoffs pass little of the noise's energy, so bring every burst to
    // the same loudness: quieter for taps and short scuffs that come with
    // every step, fuller for hits, bursts and quakes.
    double rms = sqrt(sum / len);
    double target = tap ? 0.15 : (len < SFXSYN_RATE * 60 / 1000 ? 0.2 : 0.35);
    float level = rms > 0 ? (float)(target / rms) * ENHANCED_GAIN : 0;
    for (int i = 0; i < len; i++)
    {
        out->samples[i] *= level;
    }

    EnhancedFinish(out, tap ? REVERB_WET * 0.5f : REVERB_WET);
    return true;
}

// A rounded tone: the fundamental with a little of the 2nd and 3rd harmonics.
static double SoftWave(double phase)
{
    return sin(2.0 * M_PI * phase) + 0.3 * sin(4.0 * M_PI * phase) + 0.12 * sin(6.0 * M_PI * phase);
}

static bool EnhancedTone(SfxBuffer* out, int freq, int dur)
{
    double freqHz = PitFrequency(freq);
    int busy = Samples(dur * DELAY_UNIT_SEC);

    // The clock's tick is a fraction of a millisecond: make it a soft knock.
    bool tick = busy < SFXSYN_RATE * 15 / 1000;
    int release = SFXSYN_RATE * 20 / 1000;
    int len = tick ? SFXSYN_RATE * 40 / 1000 : busy + release;
    if (!Allocate(out, len, REVERB_TAIL_SEC))
        return false;
    out->busy = busy;

    int attack = SFXSYN_RATE * 3 / 1000;
    double tickDecay = SFXSYN_RATE * 6 / 1000;
    double phase = 0;

    for (int i = 0; i < len; i++)
    {
        float env = tick ? (float)exp(-i / tickDecay) * (i < 40 ? i / 40.0f : 1.0f)
                         : Envelope(i, len, attack, release);
        out->samples[i] = (float)(SoftWave(phase) * 0.6) * env * ENHANCED_GAIN;
        phase += freqHz / SFXSYN_RATE;
        phase -= floor(phase);
    }

    EnhancedFinish(out, tick ? REVERB_WET * 0.5f : REVERB_WET);
    return true;
}

// The sweep as a smooth glide between the same pitches. Very short sweeps are
// stretched so the glide can be heard (the healing chirp, water drips).
static bool EnhancedSweep(SfxBuffer* out, int startFreq, int endFreq, int tickStep, int dur)
{
    int steps = (dur + tickStep - 1) / tickStep;
    int busy = Samples(steps * tickStep * DELAY_UNIT_SEC);
    int len = busy;
    int shortest = SFXSYN_RATE * 50 / 1000;
    if (len < shortest)
        len = shortest;

    if (!Allocate(out, len, REVERB_TAIL_SEC))
        return false;
    out->busy = busy;

    double fromHz = PitFrequency(startFreq);
    double toHz = PitFrequency(startFreq + SweepStepHz(startFreq, endFreq, tickStep, dur) * (steps - 1));
    int attack = SFXSYN_RATE * 3 / 1000;
    int release = SFXSYN_RATE * 30 / 1000;
    if (release > len / 3)
        release = len / 3;
    double phase = 0;

    for (int i = 0; i < len; i++)
    {
        double t = (double)i / len;
        double freqHz = fromHz + (toHz - fromHz) * t;
        out->samples[i] = (float)(SoftWave(phase) * 0.6) * Envelope(i, len, attack, release) * ENHANCED_GAIN;
        phase += freqHz / SFXSYN_RATE;
        phase -= floor(phase);
    }

    EnhancedFinish(out, REVERB_WET);
    return true;
}

// ---------------------------------------------------------------------------
// Ambient water for fountains and waterfalls, as a seamless loop

#define WATER_LOOP_SEC 6.0
#define WATER_SEAM_SEC 0.5

static double Random01(void)
{
    return (White() + 1.0) * 0.5;
}

// A bubble rings at a pitch set by its size, rising as it nears the surface,
// and dies away within a few hundredths of a second. It is written around the
// end of the buffer so the loop has no seam.
static void AddBubble(float* buf, int count, int start, double freqHz, double amp)
{
    double decay = 0.043 * freqHz + 0.0014 * pow(freqHz, 1.5);
    int len = (int)(5.0 / decay * SFXSYN_RATE);
    double phase = 0;

    for (int i = 0; i < len; i++)
    {
        double t = (double)i / SFXSYN_RATE;
        phase += freqHz * (1.0 + 0.15 * decay * t) / SFXSYN_RATE;
        double attack = i < 20 ? i / 20.0 : 1.0;
        buf[(start + i) % count] += (float)(sin(2.0 * M_PI * phase) * exp(-decay * t) * attack * amp);
    }
}

// Pitches spread evenly on a log scale, as small bubbles outnumber big ones.
static double BubblePitch(double low, double high)
{
    return low * pow(high / low, Random01());
}

bool SFXSYN_Water(SfxBuffer* out, bool waterfall)
{
    int count = (int)(WATER_LOOP_SEC * SFXSYN_RATE);
    int seam = (int)(WATER_SEAM_SEC * SFXSYN_RATE);
    out->samples = (float*)calloc(count + seam, sizeof(float));
    if (!out->samples)
        return false;
    out->count = count;
    out->busy = count;

    // The rush and spray under the bubbles: filtered noise that swells and
    // eases. Rendered past the end and cross-faded into the start.
    double lowCut = waterfall ? 900.0 : 2200.0;
    double bandCut = waterfall ? 2500.0 : 4500.0;
    float bedLevel = waterfall ? 1.0f : 0.35f;
    Lowpass low, band;
    LowpassInit(&low, lowCut, 0.7);
    LowpassInit(&band, bandCut, 1.5);
    double swell = 0, swellTarget = 1;
    for (int i = 0; i < count + seam; i++)
    {
        if (i % (SFXSYN_RATE / 8) == 0)
            swellTarget = 0.6 + 0.4 * Random01();
        swell += (swellTarget - swell) * 0.0005;

        float w = White();
        LowpassRun(&band, w);
        float v = (waterfall ? LowpassRun(&low, w) : 0.0f) + band.band * 0.5f;
        out->samples[i] = v * bedLevel * (float)swell;
    }
    for (int i = 0; i < seam; i++)
    {
        float fade = (float)i / seam;
        out->samples[i] = out->samples[i] * fade + out->samples[count + i] * (1.0f - fade);
    }

    // The bed's level against the bubbles is set from its own loudness.
    double sum = 0;
    for (int i = 0; i < count; i++)
        sum += out->samples[i] * out->samples[i];
    double bedRms = sqrt(sum / count);
    float bedScale = bedRms > 0 ? (float)((waterfall ? 0.12 : 0.03) / bedRms) : 0;
    for (int i = 0; i < count; i++)
        out->samples[i] *= bedScale;

    int bubbles = (int)((waterfall ? 160 : 90) * WATER_LOOP_SEC);
    for (int b = 0; b < bubbles; b++)
    {
        double freqHz = waterfall ? BubblePitch(350, 2500) : BubblePitch(700, 3500);
        double amp = sqrt(1000.0 / freqHz) * (0.3 + 0.7 * Random01()) * (waterfall ? 0.05 : 0.08);
        AddBubble(out->samples, count, (int)(Random01() * count), freqHz, amp);
    }

    // Now and then a bigger, lower plop where the water lands.
    int plops = (int)((waterfall ? 6 : 3) * WATER_LOOP_SEC);
    for (int b = 0; b < plops; b++)
    {
        AddBubble(out->samples, count, (int)(Random01() * count), BubblePitch(250, 600), 0.12 * (0.5 + 0.5 * Random01()));
    }

    float peak = 0;
    for (int i = 0; i < count; i++)
    {
        float v = fabsf(out->samples[i]);
        if (v > peak)
            peak = v;
    }
    if (peak > 0.9f)
    {
        for (int i = 0; i < count; i++)
            out->samples[i] *= 0.9f / peak;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Ambience: seamless loops for the background of a place (SFXSYN_Ambience).
// Everything that sounds is written around the end of the buffer, and noise
// beds are rendered past it and cross-faded into the start, so the loops
// have no seam.

#define AMBIENCE_LOOP_SEC 12.0
#define AMBIENCE_SEAM_SEC 0.5

// Noise through a lowpass, cross-faded at the seam.
static void AddNoiseBed(float* buf, int count, double cutoff, float level, bool band)
{
    int seam = (int)(AMBIENCE_SEAM_SEC * SFXSYN_RATE);
    float* tmp = (float*)calloc(count + seam, sizeof(float));
    Lowpass lp;

    if (!tmp)
        return;
    LowpassInit(&lp, cutoff, band ? 1.5 : 0.7);
    for (int i = 0; i < count + seam; i++)
    {
        float low = LowpassRun(&lp, White());
        tmp[i] = band ? lp.band : low;
    }
    for (int i = 0; i < seam; i++)
    {
        float fade = (float)i / seam;
        tmp[i] = tmp[i] * fade + tmp[count + i] * (1.0f - fade);
    }
    for (int i = 0; i < count; i++)
        buf[i] += tmp[i] * level;
    free(tmp);
}

// A sine whose pitch glides from startHz to endHz, with a soft attack and
// release, written at start (wrapping around the loop).
static void AddGlide(float* buf, int count, int start, double seconds, double startHz, double endHz, double amp)
{
    int len = (int)(seconds * SFXSYN_RATE);
    int edge = len / 5 + 1;
    double phase = 0;

    for (int i = 0; i < len; i++)
    {
        double t = (double)i / len;
        phase += (startHz + (endHz - startHz) * t) / SFXSYN_RATE;
        double env = i < edge ? (double)i / edge : (i > len - edge ? (double)(len - i) / edge : 1.0);
        buf[(start + i) % count] += (float)(sin(2.0 * M_PI * phase) * env * amp);
    }
}

// Surf: low rolling noise and a brighter hiss that swell together as each
// wave breaks and fade as it draws back; two waves to a loop, each its own.
static void AmbienceSurf(float* buf, int count)
{
    float* roar = (float*)calloc(count, sizeof(float));
    float* hiss = (float*)calloc(count, sizeof(float));
    double breaks[2];

    if (!roar || !hiss)
    {
        free(roar);
        free(hiss);
        return;
    }
    AddNoiseBed(roar, count, 450.0, 1.0f, false);
    AddNoiseBed(hiss, count, 2500.0, 1.0f, true);

    breaks[0] = Random01() * 0.1;
    breaks[1] = 0.5 + Random01() * 0.1;
    for (int i = 0; i < count; i++)
    {
        double env = 0.22;
        for (int w = 0; w < 2; w++)
        {
            // Seconds since this wave broke, around the loop.
            double since = fmod((double)i / count - breaks[w] + 1.0, 1.0) * AMBIENCE_LOOP_SEC;
            double rise = since < 0.8 ? since / 0.8 : 1.0;
            env += rise * exp(-(since > 0.8 ? since - 0.8 : 0) / 1.6) * 0.8;
        }
        buf[i] += (float)((roar[i] * 1.6 + hiss[i] * 0.5) * env);
    }
    free(roar);
    free(hiss);
}

// Birdsong: a faint breeze and now and then a bird, a few quick notes that
// glide up or down.
static void AmbienceBirds(float* buf, int count)
{
    AddNoiseBed(buf, count, 300.0, 0.15f, false);

    int songs = (int)(0.6 * AMBIENCE_LOOP_SEC);
    for (int s = 0; s < songs; s++)
    {
        int start = (int)(Random01() * count);
        int notes = 2 + (int)(Random01() * 4);
        double pitch = 2600.0 + Random01() * 2200.0;
        double amp = 0.05 + Random01() * 0.06;
        bool falling = Random01() < 0.5;

        for (int n = 0; n < notes; n++)
        {
            double len = 0.04 + Random01() * 0.06;
            double sweep = pitch * (0.15 + Random01() * 0.25);
            AddGlide(buf, count, start, len, falling ? pitch + sweep : pitch - sweep, pitch, amp);
            start += (int)((len + 0.03 + Random01() * 0.05) * SFXSYN_RATE);
            pitch *= 0.92 + Random01() * 0.16;
        }
    }
}

// Crickets: a few insects, each repeating its chirp (a short burst of pulses
// at one pitch) at its own steady rate, a whole number of times per loop.
static void AmbienceCrickets(float* buf, int count)
{
    AddNoiseBed(buf, count, 250.0, 0.04f, false);

    for (int c = 0; c < 3; c++)
    {
        int chirps = 14 + (int)(Random01() * 10);
        double pitch = 3800.0 + Random01() * 1400.0;
        double amp = 0.03 + Random01() * 0.025;
        int offset = (int)(Random01() * count);
        int pulses = 3 + (int)(Random01() * 2);

        for (int k = 0; k < chirps; k++)
        {
            int start = offset + (int)((double)k * count / chirps);
            for (int p = 0; p < pulses; p++)
                AddGlide(buf, count, start + (int)(p * 0.033 * SFXSYN_RATE), 0.018, pitch, pitch * 0.98, amp);
        }
    }
}

// Dripping water in a cave: a low rumble and now and then a drop, a quick
// falling plink with a few fainter echoes.
static void AmbienceDrips(float* buf, int count)
{
    static const double s_echoSec[3] = {0.11, 0.24, 0.41};
    static const double s_echoLevel[3] = {0.35, 0.2, 0.1};

    AddNoiseBed(buf, count, 120.0, 0.25f, false);

    int drips = (int)(0.7 * AMBIENCE_LOOP_SEC);
    for (int d = 0; d < drips; d++)
    {
        int start = (int)(Random01() * count);
        double pitch = 900.0 + Random01() * 900.0;
        double amp = 0.08 + Random01() * 0.08;

        AddGlide(buf, count, start, 0.07, pitch * 1.6, pitch, amp);
        for (int e = 0; e < 3; e++)
            AddGlide(buf, count, start + (int)(s_echoSec[e] * SFXSYN_RATE), 0.07, pitch * 1.6, pitch, amp * s_echoLevel[e]);
    }
}

// A hearth: the low roar of the flames, flickering, and the snap of burning
// wood: short clicks, now and then a louder pop.
static void AmbienceHearth(float* buf, int count)
{
    float* roar = (float*)calloc(count, sizeof(float));
    Lowpass click;

    if (!roar)
        return;
    AddNoiseBed(roar, count, 350.0, 1.0f, false);

    // Flicker: a slow wobble, periodic over the loop.
    for (int i = 0; i < count; i++)
    {
        double t = (double)i / count;
        double flicker = 0.75 + 0.15 * sin(2.0 * M_PI * 7 * t) + 0.1 * sin(2.0 * M_PI * 17 * t + 1.3);
        buf[i] += (float)(roar[i] * flicker * 0.9);
    }
    free(roar);

    LowpassInit(&click, 3000.0, 1.2);
    int snaps = (int)(14 * AMBIENCE_LOOP_SEC);
    for (int s = 0; s < snaps; s++)
    {
        int start = (int)(Random01() * count);
        bool pop = Random01() < 0.08;
        int len = (int)((pop ? 0.012 : 0.003 + Random01() * 0.004) * SFXSYN_RATE);
        double amp = pop ? 0.35 : 0.04 + Random01() * 0.1;

        for (int i = 0; i < len; i++)
        {
            LowpassRun(&click, White());
            double env = exp(-6.0 * i / len);
            buf[(start + i) % count] += (float)(click.band * env * amp);
        }
    }
}

// Rain: the hiss of many drops (a bright noise and a softer one under it)
// and the patter of the nearer ones, short ticks at random.
static void AmbienceRain(float* buf, int count)
{
    Lowpass tick;

    AddNoiseBed(buf, count, 3200.0, 0.55f, true);
    AddNoiseBed(buf, count, 700.0, 0.6f, false);

    LowpassInit(&tick, 4500.0, 1.4);
    int drops = (int)(60 * AMBIENCE_LOOP_SEC);
    for (int d = 0; d < drops; d++)
    {
        int start = (int)(Random01() * count);
        int len = (int)((0.002 + Random01() * 0.004) * SFXSYN_RATE);
        double amp = 0.03 + Random01() * 0.08;

        for (int i = 0; i < len; i++)
        {
            LowpassRun(&tick, White());
            double env = exp(-5.0 * i / len);
            buf[(start + i) % count] += (float)(tick.band * env * amp);
        }
    }
}

bool SFXSYN_Thunder(SfxBuffer* out, int loudness)
{
    int count = (int)(4.0 * SFXSYN_RATE);
    Lowpass deep, low, crack;
    bool close = loudness > 70;

    out->samples = (float*)calloc(count, sizeof(float));
    if (!out->samples)
        return false;
    out->count = count;
    out->busy = 0;

    // The rumble swells a few times as the sound comes from farther along
    // the bolt, and dies away.
    double swells[4], swellAmp[4];
    for (int s = 0; s < 4; s++)
    {
        swells[s] = (close ? 0.05 : 0.15) + s * (0.35 + Random01() * 0.4);
        swellAmp[s] = (s == 0 ? 1.0 : 0.4 + Random01() * 0.6) * (1.0 - s * 0.15);
    }

    LowpassInit(&deep, 70.0, 0.9);
    LowpassInit(&low, 160.0, 0.8);
    LowpassInit(&crack, 1800.0, 1.0);
    float peak = 0;
    for (int i = 0; i < count; i++)
    {
        double t = (double)i / SFXSYN_RATE;
        double env = 0;
        for (int s = 0; s < 4; s++)
        {
            double since = t - swells[s];
            if (since > 0)
                env += swellAmp[s] * (since < 0.08 ? since / 0.08 : exp(-(since - 0.08) / 0.7));
        }
        float w = White();
        float v = (float)((LowpassRun(&deep, w) * 1.4 + LowpassRun(&low, w) * 0.6) * env);
        if (close && t < 0.25)
            v += (float)(LowpassRun(&crack, w) * exp(-t / 0.05) * 0.6);
        // The last half second fades out.
        if (t > 3.5)
            v *= (float)((4.0 - t) / 0.5);
        out->samples[i] = v;
        if (fabsf(v) > peak)
            peak = fabsf(v);
    }

    float scale = peak > 0 ? (float)(0.25 + 0.55 * loudness / 100.0) / peak : 0;
    for (int i = 0; i < count; i++)
        out->samples[i] *= scale;
    return true;
}

bool SFXSYN_Ambience(SfxBuffer* out, int kind)
{
    // Target loudness (RMS) of each loop before the track's gain.
    static const float s_levels[] = {0, 0.07f, 0.035f, 0.035f, 0.05f, 0.06f, 0.06f};
    int count = (int)(AMBIENCE_LOOP_SEC * SFXSYN_RATE);

    out->samples = NULL;
    if (kind <= SFXSYN_AMBIENCE_NONE || kind > SFXSYN_AMBIENCE_RAIN)
        return false;

    out->samples = (float*)calloc(count, sizeof(float));
    if (!out->samples)
        return false;
    out->count = count;
    out->busy = count;

    switch (kind)
    {
    case SFXSYN_AMBIENCE_SURF: AmbienceSurf(out->samples, count); break;
    case SFXSYN_AMBIENCE_BIRDS: AmbienceBirds(out->samples, count); break;
    case SFXSYN_AMBIENCE_CRICKETS: AmbienceCrickets(out->samples, count); break;
    case SFXSYN_AMBIENCE_DRIPS: AmbienceDrips(out->samples, count); break;
    case SFXSYN_AMBIENCE_HEARTH: AmbienceHearth(out->samples, count); break;
    case SFXSYN_AMBIENCE_RAIN: AmbienceRain(out->samples, count); break;
    }

    double sum = 0;
    for (int i = 0; i < count; i++)
        sum += out->samples[i] * out->samples[i];
    double rms = sqrt(sum / count);
    float scale = rms > 0 ? (float)(s_levels[kind] / rms) : 0;

    float peak = 0;
    for (int i = 0; i < count; i++)
    {
        out->samples[i] *= scale;
        if (fabsf(out->samples[i]) > peak)
            peak = fabsf(out->samples[i]);
    }
    if (peak > 0.9f)
    {
        for (int i = 0; i < count; i++)
            out->samples[i] *= 0.9f / peak;
    }
    return true;
}

// ---------------------------------------------------------------------------

bool SFXSYN_Pulse(SfxBuffer* out, bool enhanced, int freq, int delay, int dur, int pulseWidth, int pulseInc)
{
    out->samples = NULL;
    dur &= 0xffff;
    if (freq <= 0 || dur <= 0)
        return false;
    if (delay <= 0)
        delay = 1;

    return enhanced ? EnhancedPulse(out, freq, delay, dur, pulseWidth, pulseInc)
                    : ClassicPulse(out, freq, delay, dur, pulseWidth, pulseInc);
}

bool SFXSYN_Noise(SfxBuffer* out, bool enhanced, int rate, int dur, int limit)
{
    out->samples = NULL;
    if (rate <= 0 || dur <= 0)
        return false;

    return enhanced ? EnhancedNoise(out, rate, dur, limit) : ClassicNoise(out, rate, dur, limit);
}

bool SFXSYN_Tone(SfxBuffer* out, bool enhanced, int freq, int dur)
{
    out->samples = NULL;
    if (freq <= 0 || dur <= 0)
        return false;

    return enhanced ? EnhancedTone(out, freq, dur) : ClassicTone(out, freq, dur);
}

bool SFXSYN_Sweep(SfxBuffer* out, bool enhanced, int startFreq, int endFreq, int tickStep, int dur)
{
    out->samples = NULL;
    if (tickStep <= 0 || dur <= 0)
        return false;

    return enhanced ? EnhancedSweep(out, startFreq, endFreq, tickStep, dur)
                    : ClassicSweep(out, startFreq, endFreq, tickStep, dur);
}
