#ifndef AUDIO_H
#define AUDIO_H

#include <stdbool.h>

typedef enum {
    SOUND_MOVE = 0,
    SOUND_SELECT,
    SOUND_TOGGLE,
    SOUND_BACK,
    SOUND_ERROR,
    SOUND_FANFARE,
    SOUND_COUNT
} SoundEffect;

bool audio_init(void);
void audio_cleanup(void);
void audio_play_sound(SoundEffect sound);

#endif // AUDIO_H
