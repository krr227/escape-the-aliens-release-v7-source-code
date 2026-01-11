#ifndef AUDIO_H
#define AUDIO_H

#include <SDL2/SDL.h>

/* Simple WAV SFX playback using only SDL2 (no SDL_mixer).
 *
 * All WAV files are loaded from: DATA/ASSETS/
 */

typedef enum {
    SFX_GUN = 0,
    SFX_SHOTGUN,
    SFX_PLASMA,
    SFX_RRG,
    SFX_ITEM,
    SFX_ENEMY_DIE,
    SFX_PLAYER_DIE,
    SFX_VICTORY,
    SFX_ENDING,
    SFX_COUNT
} SfxId;

/* Returns 0 on success. If init fails, the game still runs but audio calls become no-ops. */
int audio_init(void);
void audio_shutdown(void);

/* One-shot sound effects (can overlap). */
void audio_play_sfx(SfxId id);

/* Background music (bgm.wav) loop control. */
void audio_bgm_set_enabled(int enabled);
int  audio_bgm_get_enabled(void);

/* SFX enable switch. */
void audio_sfx_set_enabled(int enabled);
int  audio_sfx_get_enabled(void);

/* Volume controls (0..128). Master multiplies BGM/SFX. */
void audio_set_master_volume(int vol);
int  audio_get_master_volume(void);

void audio_set_bgm_volume(int vol);
int  audio_get_bgm_volume(void);

void audio_set_sfx_volume(int vol);
int  audio_get_sfx_volume(void);

#endif
