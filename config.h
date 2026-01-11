#ifndef CONFIG_H
#define CONFIG_H

#include <SDL2/SDL.h>

/* Persistent user settings stored at: DATA/config/config.json
 *
 * - Only SDL2 + libc are used (no external JSON library).
 * - If the config file doesn't exist, it is automatically created with defaults.
 */

#define CONFIG_VERSION 1

typedef enum {
    ACTION_MOVE_FORWARD = 0,
    ACTION_MOVE_BACK,
    ACTION_STRAFE_LEFT,
    ACTION_STRAFE_RIGHT,
    ACTION_INTERACT,
    ACTION_PAUSE,
    ACTION_WEAPON_1,
    ACTION_WEAPON_2,
    ACTION_WEAPON_3,
    ACTION_WEAPON_4,
    ACTION_WEAPON_5,
    ACTION_COUNT
} Action;

typedef struct {
    int version;
    int fullscreen;
    float mouse_sensitivity;

    int master_volume; /* 0..128 */
    int bgm_enabled;   /* 0/1 */
    int bgm_volume;    /* 0..128 */
    int sfx_enabled;   /* 0/1 */
    int sfx_volume;    /* 0..128 */

    SDL_Scancode binds[ACTION_COUNT];
} GameConfig;

int config_load_or_create(void);
int config_save(void);

const GameConfig *config_get(void);
GameConfig *config_mut(void);
void config_set_defaults(GameConfig *cfg);

const char *config_action_label(Action a);

SDL_Scancode config_get_bind(Action a);
void config_set_bind(Action a, SDL_Scancode sc);

int config_get_fullscreen(void);
void config_set_fullscreen(int v);

float config_get_mouse_sensitivity(void);
void config_set_mouse_sensitivity(float v);

int config_get_master_volume(void);
void config_set_master_volume(int v);

int config_get_bgm_enabled(void);
void config_set_bgm_enabled(int v);

int config_get_bgm_volume(void);
void config_set_bgm_volume(int v);

int config_get_sfx_enabled(void);
void config_set_sfx_enabled(int v);

int config_get_sfx_volume(void);
void config_set_sfx_volume(int v);

#endif
