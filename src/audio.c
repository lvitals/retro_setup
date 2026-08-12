#include "audio.h"
#include "config.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLE_RATE 44100
#define MAX_SAMPLES 44100

typedef struct {
    float freq1;
    float freq2;
    float duration; // in seconds
    int wave_type;  // 0: square, 1: triangle, 2: noise
} SoundTone;

static SDL_AudioDeviceID g_audio_device = 0;
static float g_sound_buffer[MAX_SAMPLES];
static int g_sound_length = 0;
static int g_sound_pos = 0;
static SDL_mutex* g_audio_mutex = NULL;

static void generate_tone(SoundEffect sound) {
    if (!g_audio_mutex) return;
    SDL_LockMutex(g_audio_mutex);

    g_sound_pos = 0;
    g_sound_length = 0;

    int total_samples = 0;
    float t = 0.0f;

    switch (sound) {
        case SOUND_MOVE: { // Quick retro high beep
            int count = (int)(SAMPLE_RATE * 0.04f);
            if (count > MAX_SAMPLES) count = MAX_SAMPLES;
            for (int i = 0; i < count; i++) {
                t = (float)i / SAMPLE_RATE;
                float freq = 600.0f - (i * 300.0f / count);
                float val = (sinf(2.0f * M_PI * freq * t) > 0.0f) ? 0.15f : -0.15f;
                // Fade out
                val *= (1.0f - (float)i / count);
                g_sound_buffer[i] = val;
            }
            total_samples = count;
            break;
        }
        case SOUND_SELECT: { // Upward dual-tone chirp (Super Mario style jump/select)
            int count = (int)(SAMPLE_RATE * 0.08f);
            if (count > MAX_SAMPLES) count = MAX_SAMPLES;
            int mid = count / 2;
            for (int i = 0; i < count; i++) {
                t = (float)i / SAMPLE_RATE;
                float freq = (i < mid) ? 523.25f : 659.25f; // C5 to E5
                float val = (sinf(2.0f * M_PI * freq * t) > 0.0f) ? 0.2f : -0.2f;
                val *= (1.0f - (float)i / count);
                g_sound_buffer[i] = val;
            }
            total_samples = count;
            break;
        }
        case SOUND_TOGGLE: { // Snappy metallic click
            int count = (int)(SAMPLE_RATE * 0.05f);
            if (count > MAX_SAMPLES) count = MAX_SAMPLES;
            for (int i = 0; i < count; i++) {
                t = (float)i / SAMPLE_RATE;
                float freq = 880.0f + (i * 400.0f / count);
                float val = (sinf(2.0f * M_PI * freq * t) > 0.0f) ? 0.18f : -0.18f;
                val *= (1.0f - (float)i / count);
                g_sound_buffer[i] = val;
            }
            total_samples = count;
            break;
        }
        case SOUND_BACK: { // Downward retro buzz
            int count = (int)(SAMPLE_RATE * 0.07f);
            if (count > MAX_SAMPLES) count = MAX_SAMPLES;
            for (int i = 0; i < count; i++) {
                t = (float)i / SAMPLE_RATE;
                float freq = 400.0f - (i * 250.0f / count);
                float val = (sinf(2.0f * M_PI * freq * t) > 0.0f) ? 0.2f : -0.2f;
                val *= (1.0f - (float)i / count);
                g_sound_buffer[i] = val;
            }
            total_samples = count;
            break;
        }
        case SOUND_ERROR: { // Low double buzz
            int count = (int)(SAMPLE_RATE * 0.15f);
            if (count > MAX_SAMPLES) count = MAX_SAMPLES;
            for (int i = 0; i < count; i++) {
                t = (float)i / SAMPLE_RATE;
                float freq = (i % 2000 < 1000) ? 150.0f : 120.0f;
                float val = (sinf(2.0f * M_PI * freq * t) > 0.0f) ? 0.25f : -0.25f;
                val *= (1.0f - (float)i / count);
                g_sound_buffer[i] = val;
            }
            total_samples = count;
            break;
        }
        case SOUND_FANFARE: { // Victory chord (Arpeggio: C5 -> E5 -> G5 -> C6)
            int count = (int)(SAMPLE_RATE * 0.30f);
            if (count > MAX_SAMPLES) count = MAX_SAMPLES;
            float freqs[] = { 523.25f, 659.25f, 783.99f, 1046.50f };
            int quarter = count / 4;
            for (int i = 0; i < count; i++) {
                t = (float)i / SAMPLE_RATE;
                int f_idx = i / quarter;
                if (f_idx > 3) f_idx = 3;
                float freq = freqs[f_idx];
                float val = (sinf(2.0f * M_PI * freq * t) > 0.0f) ? 0.2f : -0.2f;
                val *= (1.0f - (float)i / count);
                g_sound_buffer[i] = val;
            }
            total_samples = count;
            break;
        }
        default:
            break;
    }

    g_sound_length = total_samples;
    SDL_UnlockMutex(g_audio_mutex);
}

static void audio_callback(void* userdata, Uint8* stream, int len) {
    (void)userdata;
    memset(stream, 0, len);

    if (!g_config.audio_enabled || !g_audio_mutex) return;

    SDL_LockMutex(g_audio_mutex);
    if (g_sound_pos < g_sound_length) {
        float* fstream = (float*)stream;
        int num_samples = len / sizeof(float);

        for (int i = 0; i < num_samples; i++) {
            if (g_sound_pos < g_sound_length) {
                fstream[i] = g_sound_buffer[g_sound_pos++];
            } else {
                fstream[i] = 0.0f;
            }
        }
    }
    SDL_UnlockMutex(g_audio_mutex);
}

bool audio_init(void) {
    g_audio_mutex = SDL_CreateMutex();

    SDL_AudioSpec desired, obtained;
    SDL_zero(desired);
    desired.freq = SAMPLE_RATE;
    desired.format = AUDIO_F32SYS;
    desired.channels = 1;
    desired.samples = 1024;
    desired.callback = audio_callback;

    g_audio_device = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, 0);
    if (g_audio_device == 0) {
        return false;
    }

    SDL_PauseAudioDevice(g_audio_device, 0);
    return true;
}

void audio_cleanup(void) {
    if (g_audio_device != 0) {
        SDL_CloseAudioDevice(g_audio_device);
        g_audio_device = 0;
    }
    if (g_audio_mutex) {
        SDL_DestroyMutex(g_audio_mutex);
        g_audio_mutex = NULL;
    }
}

void audio_play_sound(SoundEffect sound) {
    if (!g_config.audio_enabled) return;
    generate_tone(sound);
}
