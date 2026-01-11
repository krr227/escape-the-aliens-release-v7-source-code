#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

#include "audio.h"

/*
 * SDL2-only audio mixer:
 * - One audio device (callback)
 * - Looped BGM (bgm.wav)
 * - Multiple overlapping one-shot SFX
 *
 * All files are loaded from DATA/ASSETS/ next to the executable.
 */

typedef struct {
    Uint8 *buf;
    Uint32 len;
} Sound;

typedef struct {
    const Sound *sound;
    Uint32 pos;
    int active;
} Channel;

#define MAX_CHANNELS 16

static int clampi(int v, int lo, int hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }

static SDL_AudioDeviceID g_dev = 0;
static SDL_AudioSpec g_have;

static Sound g_bgm = {0};
static Uint32 g_bgm_pos = 0;
static int g_bgm_enabled = 1;
static int g_sfx_enabled = 1;

/* Volume controls (0..SDL_MIX_MAXVOLUME). */
static int g_master_volume = SDL_MIX_MAXVOLUME;
static int g_bgm_volume    = SDL_MIX_MAXVOLUME;
static int g_sfx_volume    = SDL_MIX_MAXVOLUME;

static Sound g_sfx[SFX_COUNT];
static Channel g_channels[MAX_CHANNELS];

static void build_asset_path(char *out, size_t out_sz, const char *file)
{
    char *base = SDL_GetBasePath();
    if (base) {
        snprintf(out, out_sz, "%sDATA/ASSETS/%s", base, file);
        SDL_free(base);
    } else {
        snprintf(out, out_sz, "DATA/ASSETS/%s", file);
    }
}

static void sound_free(Sound *s)
{
    if (!s) return;
    if (s->buf) {
        SDL_free(s->buf);
        s->buf = NULL;
    }
    s->len = 0;
}

static int sound_load_converted(Sound *out, const char *file)
{
    if (!out || !file) return -1;

    char path[512];
    build_asset_path(path, sizeof path, file);

    SDL_AudioSpec src;
    Uint8 *src_buf = NULL;
    Uint32 src_len = 0;

    if (!SDL_LoadWAV(path, &src, &src_buf, &src_len)) {
        fprintf(stderr, "AUDIO: failed to load %s: %s\n", path, SDL_GetError());
        return -1;
    }

    SDL_AudioCVT cvt;
    if (SDL_BuildAudioCVT(&cvt, src.format, src.channels, src.freq,
                          g_have.format, g_have.channels, g_have.freq) < 0) {
        fprintf(stderr, "AUDIO: SDL_BuildAudioCVT failed for %s: %s\n", path, SDL_GetError());
        SDL_FreeWAV(src_buf);
        return -1;
    }

    Uint8 *dst = NULL;
    Uint32 dst_len = 0;

    if (cvt.needed) {
        cvt.len = (int)src_len;
        cvt.buf = (Uint8 *)SDL_malloc((size_t)src_len * (size_t)cvt.len_mult);
        if (!cvt.buf) {
            fprintf(stderr, "AUDIO: out of memory converting %s\n", path);
            SDL_FreeWAV(src_buf);
            return -1;
        }
        SDL_memcpy(cvt.buf, src_buf, src_len);
        if (SDL_ConvertAudio(&cvt) < 0) {
            fprintf(stderr, "AUDIO: SDL_ConvertAudio failed for %s: %s\n", path, SDL_GetError());
            SDL_free(cvt.buf);
            SDL_FreeWAV(src_buf);
            return -1;
        }
        dst = cvt.buf;
        dst_len = (Uint32)cvt.len_cvt;
    } else {
        dst = (Uint8 *)SDL_malloc(src_len);
        if (!dst) {
            fprintf(stderr, "AUDIO: out of memory copying %s\n", path);
            SDL_FreeWAV(src_buf);
            return -1;
        }
        SDL_memcpy(dst, src_buf, src_len);
        dst_len = src_len;
    }

    SDL_FreeWAV(src_buf);

    sound_free(out);
    out->buf = dst;
    out->len = dst_len;
    return 0;
}

static void audio_callback(void *userdata, Uint8 *stream, int len)
{
    (void)userdata;
    if (!stream || len <= 0) return;

    SDL_memset(stream, 0, (size_t)len);

    /* Effective volumes (master multiplies BGM/SFX). */
    int bgmVol = (g_master_volume * g_bgm_volume) / SDL_MIX_MAXVOLUME;
    bgmVol = clampi(bgmVol, 0, SDL_MIX_MAXVOLUME);

    int sfxVol = (g_master_volume * g_sfx_volume) / SDL_MIX_MAXVOLUME;
    sfxVol = clampi(sfxVol, 0, SDL_MIX_MAXVOLUME);
    if (!g_sfx_enabled) sfxVol = 0;

    /* Mix BGM first (looped). */
    if (g_bgm_enabled && bgmVol > 0 && g_bgm.buf && g_bgm.len > 0) {
        Uint32 remaining = (Uint32)len;
        Uint8 *dst = stream;
        while (remaining > 0) {
            Uint32 chunk = g_bgm.len - g_bgm_pos;
            if (chunk > remaining) chunk = remaining;
            SDL_MixAudioFormat(dst, g_bgm.buf + g_bgm_pos, g_have.format, chunk, bgmVol);
            g_bgm_pos += chunk;
            if (g_bgm_pos >= g_bgm.len) g_bgm_pos = 0;
            dst += chunk;
            remaining -= chunk;
        }
    }

    /* Mix active SFX channels over the buffer. */
    for (int i = 0; i < MAX_CHANNELS; i++) {
        Channel *ch = &g_channels[i];
        if (!ch->active || !ch->sound || !ch->sound->buf) continue;

        const Sound *s = ch->sound;
        if (ch->pos >= s->len) {
            ch->active = 0;
            continue;
        }

        Uint32 avail = s->len - ch->pos;
        Uint32 mix_len = (avail > (Uint32)len) ? (Uint32)len : avail;

        if (sfxVol > 0) {
            SDL_MixAudioFormat(stream, s->buf + ch->pos, g_have.format, mix_len, sfxVol);
        }
        ch->pos += mix_len;

        if (ch->pos >= s->len) {
            ch->active = 0;
        }
    }
}

int audio_init(void)
{
    if (g_dev) return 0;

    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = 44100;
    want.format = AUDIO_S16LSB;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audio_callback;
    want.userdata = NULL;

    SDL_zero(g_have);
    g_dev = SDL_OpenAudioDevice(NULL, 0, &want, &g_have, 0);
    if (!g_dev) {
        fprintf(stderr, "AUDIO: SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        return -1;
    }

    for (int i = 0; i < MAX_CHANNELS; i++) {
        g_channels[i].sound = NULL;
        g_channels[i].pos = 0;
        g_channels[i].active = 0;
    }
    for (int i = 0; i < SFX_COUNT; i++) {
        g_sfx[i].buf = NULL;
        g_sfx[i].len = 0;
    }
    g_bgm.buf = NULL;
    g_bgm.len = 0;
    g_bgm_pos = 0;
    /* Keep g_bgm_enabled / volumes as-is (config may have set them). */

    /* Load audio assets (missing files are tolerated). */
    (void)sound_load_converted(&g_bgm, "bgm.wav");

    (void)sound_load_converted(&g_sfx[SFX_GUN],        "gun.wav");
    (void)sound_load_converted(&g_sfx[SFX_SHOTGUN],    "shotgun.wav");
    /* Load new plasma and RRG weapon sounds. */
    (void)sound_load_converted(&g_sfx[SFX_PLASMA],     "plasma.wav");
    (void)sound_load_converted(&g_sfx[SFX_RRG],        "RRG.wav");
    (void)sound_load_converted(&g_sfx[SFX_ITEM],       "item.wav");
    (void)sound_load_converted(&g_sfx[SFX_ENEMY_DIE],  "enemy_die.wav");
    (void)sound_load_converted(&g_sfx[SFX_PLAYER_DIE], "player_die.wav");
    (void)sound_load_converted(&g_sfx[SFX_VICTORY],    "victory.wav");
    (void)sound_load_converted(&g_sfx[SFX_ENDING],     "ending.wav");

    SDL_PauseAudioDevice(g_dev, 0);
    return 0;
}

void audio_shutdown(void)
{
    if (!g_dev) return;

    SDL_PauseAudioDevice(g_dev, 1);
    SDL_LockAudioDevice(g_dev);

    for (int i = 0; i < MAX_CHANNELS; i++) {
        g_channels[i].active = 0;
        g_channels[i].sound = NULL;
        g_channels[i].pos = 0;
    }

    sound_free(&g_bgm);
    for (int i = 0; i < SFX_COUNT; i++) {
        sound_free(&g_sfx[i]);
    }

    SDL_UnlockAudioDevice(g_dev);
    SDL_CloseAudioDevice(g_dev);
    g_dev = 0;
}

void audio_play_sfx(SfxId id)
{
    if (!g_dev) return;
    if (!g_sfx_enabled) return;
    if (g_master_volume <= 0 || g_sfx_volume <= 0) return;
    if (id < 0 || id >= SFX_COUNT) return;

    const Sound *s = &g_sfx[id];
    if (!s->buf || s->len == 0) return;

    SDL_LockAudioDevice(g_dev);
    int slot = -1;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (!g_channels[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        /* If all channels are busy, steal the oldest (slot 0). */
        slot = 0;
    }
    g_channels[slot].sound = s;
    g_channels[slot].pos = 0;
    g_channels[slot].active = 1;
    SDL_UnlockAudioDevice(g_dev);
}

void audio_bgm_set_enabled(int enabled)
{
    if (!g_dev) {
        g_bgm_enabled = enabled ? 1 : 0;
        if (!g_bgm_enabled) g_bgm_pos = 0;
        return;
    }
    SDL_LockAudioDevice(g_dev);
    g_bgm_enabled = enabled ? 1 : 0;
    if (!g_bgm_enabled) g_bgm_pos = 0;
    SDL_UnlockAudioDevice(g_dev);
}


int audio_bgm_get_enabled(void)
{
    return g_bgm_enabled ? 1 : 0;
}

void audio_sfx_set_enabled(int enabled)
{
    if (!g_dev) {
        g_sfx_enabled = enabled ? 1 : 0;
        return;
    }
    SDL_LockAudioDevice(g_dev);
    g_sfx_enabled = enabled ? 1 : 0;
    SDL_UnlockAudioDevice(g_dev);
}

int audio_sfx_get_enabled(void)
{
    return g_sfx_enabled ? 1 : 0;
}

void audio_set_master_volume(int vol)
{
    vol = clampi(vol, 0, SDL_MIX_MAXVOLUME);
    if (!g_dev) {
        g_master_volume = vol;
        return;
    }
    SDL_LockAudioDevice(g_dev);
    g_master_volume = vol;
    SDL_UnlockAudioDevice(g_dev);
}

int audio_get_master_volume(void)
{
    return g_master_volume;
}

void audio_set_bgm_volume(int vol)
{
    vol = clampi(vol, 0, SDL_MIX_MAXVOLUME);
    if (!g_dev) {
        g_bgm_volume = vol;
        return;
    }
    SDL_LockAudioDevice(g_dev);
    g_bgm_volume = vol;
    SDL_UnlockAudioDevice(g_dev);
}

int audio_get_bgm_volume(void)
{
    return g_bgm_volume;
}

void audio_set_sfx_volume(int vol)
{
    vol = clampi(vol, 0, SDL_MIX_MAXVOLUME);
    if (!g_dev) {
        g_sfx_volume = vol;
        return;
    }
    SDL_LockAudioDevice(g_dev);
    g_sfx_volume = vol;
    SDL_UnlockAudioDevice(g_dev);
}

int audio_get_sfx_volume(void)
{
    return g_sfx_volume;
}
