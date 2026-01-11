#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "player.h"
#include "enemy.h"
#include "render.h"
#include "map.h"
#include "items.h"
#include "font.h"
#include "audio.h"
#include "savegame.h"
#include "config.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 * Episode-based FPS loop.
 * - map1..3: Episode 1 "ESCAPING FROM THEM"
 * - map4..6: Episode 2 "ALIEN INVASION!"
 * - map7..9: Episode 3 "FINAL CONFRONTATION"
 *
 * After each map, show a fullscreen cutscene image:
 *  1.bmp shown between map1->map2, 2.bmp between map2->map3, ... 8.bmp between map8->map9.
 * Final completion shows ending.bmp.
 */

/* Fullscreen toggle state */
static int isFullscreen = 0;

/* Game states */
typedef enum {
    STATE_MENU,
    STATE_EPISODE_SELECT,
    STATE_OPTIONS,
    STATE_PAUSED,
    STATE_LOADMENU,
    STATE_SAVEMENU,
    STATE_PLAYING,
    STATE_CUTSCENE,
    STATE_END
} GameState;

static GameState state = STATE_MENU;
static int currentLevel = 1;
static int active_slot = 1; /* 1..3 autosave target */

/* Cutscene index (1..8) used in STATE_CUTSCENE */
static int cutscene_index = 0;

static char message_text[64] = "";
static Uint32 message_end_time = 0;

/* UI notices for menus (shown at bottom). */
static char menu_notice[96] = "";
static Uint32 menu_notice_end_time = 0;

/* Fonts */
static BitmapFont fontPixel;
static BitmapFont fontNumbers;
static BitmapFont fontMenu;

/* Menu selection indices */
static int menu_selection = 0;      /* 0: start, 1: load, 2: options, 3: quit */
static int episode_selection = 0;   /* 0..2 episodes, 3 back */

typedef enum {
    OPTPAGE_MAIN = 0,
    OPTPAGE_AUDIO,
    OPTPAGE_KEYS,
    OPTPAGE_BIND_CAPTURE
} OptionsPage;

static OptionsPage options_page = OPTPAGE_MAIN;
static int opt_main_sel = 0;
static int opt_audio_sel = 0;
static int opt_keys_sel = 0;
static Action opt_capture_action = ACTION_COUNT;

static int pause_selection = 0;     /* 0: continue, 1: load, 2: save, 3: quit */
static int slot_selection = 0;      /* 0..2 slots, 3 back */

static SaveMeta slot_meta[3];
static GameState slot_return_state = STATE_MENU;

/* Cheat input buffer */
static char cheat_buf[16] = "";
static int cheat_len = 0;

/* Forward declarations */
static void refresh_slot_meta(void);
static int  load_slot_and_enter(int slot, SDL_Window *win, SDL_Renderer *renderer);
static int  save_current_to_slot(int slot);
static void begin_new_game(int startLevel, SDL_Window *win, SDL_Renderer *renderer);
static int  save_progress_to_slot(int slot);
void show_message(const char *text);

static void apply_fullscreen(SDL_Window *win, SDL_Renderer *renderer, int enable)
{
    if (!win) return;

    /* Use the real window flags as the source of truth (avoids desync). */
    Uint32 flags = SDL_GetWindowFlags(win);
    int currently_fs = ((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) || (flags & SDL_WINDOW_FULLSCREEN)) ? 1 : 0;
    if (enable == currently_fs) {
        isFullscreen = currently_fs;
        return;
    }

    if (enable) {
        if (SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN_DESKTOP) == 0) {
            isFullscreen = 1;
        } else {
            isFullscreen = 0;
        }
    } else {
        if (SDL_SetWindowFullscreen(win, 0) == 0) {
            isFullscreen = 0;
            SDL_SetWindowSize(win, W, H);
            SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        } else {
            isFullscreen = 1;
        }
    }

    if (renderer) {
        SDL_RenderSetLogicalSize(renderer, W, H);
        SDL_RenderSetIntegerScale(renderer, SDL_TRUE);
    }
}

static void toggle_fullscreen(SDL_Window *win, SDL_Renderer *renderer)
{
    Uint32 flags = SDL_GetWindowFlags(win);
    int currently_fs = ((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) || (flags & SDL_WINDOW_FULLSCREEN)) ? 1 : 0;
    apply_fullscreen(win, renderer, !currently_fs);

    /* Persist */
    config_set_fullscreen(isFullscreen);
    (void)config_save();
}

static void adjust_sensitivity(int dir)
{
    const float step = 0.0005f;
    const float minS = 0.0005f;
    const float maxS = 0.0200f;
    mouse_sensitivity += (float)dir * step;
    if (mouse_sensitivity < minS) mouse_sensitivity = minS;
    if (mouse_sensitivity > maxS) mouse_sensitivity = maxS;

    config_set_mouse_sensitivity(mouse_sensitivity);
    (void)config_save();
}

static void apply_config_to_runtime(SDL_Window *win, SDL_Renderer *renderer)
{
    /* Video */
    apply_fullscreen(win, renderer, config_get_fullscreen());

    /* Input */
    mouse_sensitivity = config_get_mouse_sensitivity();

    /* Audio */
    audio_set_master_volume(config_get_master_volume());
    audio_set_bgm_volume(config_get_bgm_volume());
    audio_set_sfx_volume(config_get_sfx_volume());
    audio_bgm_set_enabled(config_get_bgm_enabled());
    audio_sfx_set_enabled(config_get_sfx_enabled());
}

static int clampi_local(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void adjust_volume_key(int *val_io, int dir, int step)
{
    if (!val_io) return;
    int v = *val_io;
    v += dir * step;
    v = clampi_local(v, 0, 128);
    *val_io = v;
}

static Action keys_page_action_at(int idx)
{
    /* Keep order stable for UI. */
    static const Action map[] = {
        ACTION_MOVE_FORWARD,
        ACTION_MOVE_BACK,
        ACTION_STRAFE_LEFT,
        ACTION_STRAFE_RIGHT,
        ACTION_INTERACT,
        ACTION_PAUSE,
        ACTION_WEAPON_1,
        ACTION_WEAPON_2,
        ACTION_WEAPON_3,
        ACTION_WEAPON_4,
        ACTION_WEAPON_5
    };

    if (idx < 0 || idx >= (int)(sizeof(map) / sizeof(map[0]))) return ACTION_COUNT;
    return map[idx];
}

static int keys_page_action_count(void)
{
    return 11;
}

static void ui_notice(const char *text, Uint32 ms)
{
    if (!text) return;
    strncpy(menu_notice, text, sizeof menu_notice - 1);
    menu_notice[sizeof menu_notice - 1] = '\0';
    menu_notice_end_time = (ms == 0) ? 0 : (SDL_GetTicks() + ms);
}

static void refresh_slot_meta(void)
{
    for (int i = 0; i < 3; i++) {
        SaveMeta m;
        if (savegame_peek(i + 1, &m) == 0 && m.exists) {
            slot_meta[i] = m;
        } else {
            memset(&slot_meta[i], 0, sizeof slot_meta[i]);
            slot_meta[i].exists = 0;
        }
    }
}

static int weapon_damage(WeaponType w)
{
    switch (w) {
        case WEAPON_SHOTGUN: return 2;
        case WEAPON_PLASMA:  return 2;
        case WEAPON_RRG:     return 12;
        case WEAPON_SMG:     return 1;
        case WEAPON_PISTOL:
        default:             return 1;
    }
}

static int weapon_hitbox_size(WeaponType w)
{
    if (w == WEAPON_SHOTGUN) return 280;
    if (w == WEAPON_RRG) return 230;
    return 200;
}

static void build_ammo_string(char *out, size_t out_sz)
{
    if (!out || out_sz == 0) return;

    const char *label = "AMMO";
    int val = 0;

    switch (current_weapon) {
        case WEAPON_SHOTGUN:
            label = "SHELLS";
            val = ammo_shells;
            break;
        case WEAPON_PLASMA:
        case WEAPON_RRG:
            label = "ENERGY";
            val = ammo_energy;
            break;
        case WEAPON_PISTOL:
        case WEAPON_SMG:
        default:
            label = "BULLETS";
            val = ammo_bullets;
            break;
    }

    if (godmode_enabled) {
        snprintf(out, out_sz, "%s INF", label);
    } else {
        snprintf(out, out_sz, "%s %d", label, val);
    }
}

static void snapshot_current(SaveGame *sg)
{
    if (!sg) return;
    memset(sg, 0, sizeof *sg);

    sg->version = SAVEGAME_VERSION;
    sg->level = currentLevel;

    sg->px = px;
    sg->py = py;
    sg->angle = angle;

    sg->hp = hp;
    sg->ammo_bullets = ammo_bullets;
    sg->ammo_shells = ammo_shells;
    sg->ammo_energy = ammo_energy;

    sg->hasKey = hasKey;
    sg->hasShotgun = hasShotgun;
    sg->hasSMG = hasSMG;
    sg->hasPlasma = hasPlasma;
    sg->hasRRG = hasRRG;
    sg->weapon = (int)current_weapon;
    sg->godmode = godmode_enabled ? 1 : 0;

    sg->sensitivity = mouse_sensitivity;

    sg->enemy_count = enemy_count;
    if (sg->enemy_count < 0) sg->enemy_count = 0;
    if (sg->enemy_count > MAX_ENEMIES) sg->enemy_count = MAX_ENEMIES;
    for (int i = 0; i < sg->enemy_count; i++) {
        sg->enemy_x[i] = enemies[i].x;
        sg->enemy_y[i] = enemies[i].y;
        sg->enemy_kind[i] = (int)enemies[i].kind;
        sg->enemy_state[i] = (int)enemies[i].state;
        sg->enemy_hp[i] = enemies[i].hp;
        sg->enemy_dying_timer[i] = enemies[i].dying_timer;
    }

    sg->item_count = item_count;
    if (sg->item_count < 0) sg->item_count = 0;
    if (sg->item_count > MAX_ITEMS) sg->item_count = MAX_ITEMS;
    for (int i = 0; i < sg->item_count; i++) {
        sg->item_x[i] = items[i].x;
        sg->item_y[i] = items[i].y;
        sg->item_type[i] = (int)items[i].type;
        sg->item_collected[i] = items[i].collected ? 1 : 0;
    }
}

static int save_current_to_slot(int slot)
{
    SaveGame sg;
    snapshot_current(&sg);
    if (savegame_write(slot, &sg) != 0) return -1;
    active_slot = slot;
    refresh_slot_meta();
    return 0;
}

/* Save only "progress" (used for level transitions) so we don't accidentally
 * restore enemies/items from the previous level into the next. */
static int save_progress_to_slot(int slot)
{
    SaveGame sg;
    memset(&sg, 0, sizeof sg);

    sg.version = SAVEGAME_VERSION;
    sg.level = currentLevel;

    /* Force spawn fallback on load. */
    sg.px = 0.0f;
    sg.py = 0.0f;
    sg.angle = 0.0f;

    sg.hp = 100;

    /* Carry progression */
    sg.ammo_bullets = ammo_bullets;
    sg.ammo_shells = ammo_shells;
    sg.ammo_energy = ammo_energy;

    sg.hasKey = 0;
    sg.hasShotgun = hasShotgun ? 1 : 0;
    sg.hasSMG = hasSMG ? 1 : 0;
    sg.hasPlasma = hasPlasma ? 1 : 0;
    sg.hasRRG = hasRRG ? 1 : 0;
    sg.weapon = (int)current_weapon;
    sg.godmode = godmode_enabled ? 1 : 0;

    sg.sensitivity = mouse_sensitivity;
    sg.enemy_count = 0;
    sg.item_count = 0;

    if (savegame_write(slot, &sg) != 0) return -1;
    active_slot = slot;
    refresh_slot_meta();
    return 0;
}

/* Clamp/validate a loaded position so we don't spawn into walls / outside map. */
static void apply_player_pos_safely(float in_x, float in_y, float in_angle)
{
    float nx = in_x;
    float ny = in_y;

    int ok = 1;
    if (!worldmap || worldWidth <= 0 || worldHeight <= 0) ok = 0;
    if (nx < 0.1f || ny < 0.1f) ok = 0;
    if (nx >= (float)worldWidth - 0.1f || ny >= (float)worldHeight - 0.1f) ok = 0;
    if (ok) {
        int tx = (int)nx;
        int ty = (int)ny;
        if (tx < 0 || ty < 0 || tx >= worldWidth || ty >= worldHeight) ok = 0;
        else if (worldmap[ty][tx] >= 2) ok = 0; /* wall/door */
    }

    if (!ok) {
        px = player_spawn_x;
        py = player_spawn_y;
        angle = 0.0f;
    } else {
        px = nx;
        py = ny;
        angle = in_angle;
    }

    const float two_pi = (float)(M_PI * 2.0);
    if (angle >= two_pi || angle <= -two_pi) angle = fmodf(angle, two_pi);
    if (angle < 0.0f) angle += two_pi;
}

static WeaponType sanitize_weapon(int w)
{
    if (w < 0) w = 0;
    if (w > 4) w = 0;
    WeaponType ww = (WeaponType)w;

    /* Must have the weapon to equip it. */
    if (ww == WEAPON_SHOTGUN && !hasShotgun) return WEAPON_PISTOL;
    if (ww == WEAPON_SMG && !hasSMG) return WEAPON_PISTOL;
    if (ww == WEAPON_PLASMA && !hasPlasma) return WEAPON_PISTOL;
    if (ww == WEAPON_RRG && !hasRRG) return WEAPON_PISTOL;
    return ww;
}

static int load_slot_and_enter(int slot, SDL_Window *win, SDL_Renderer *renderer)
{
    SaveGame sg;
    if (savegame_read(slot, &sg) != 0) {
        ui_notice("SAVE SLOT IS EMPTY", 1600);
        return -1;
    }

    if (sg.level < 1 || sg.level > 9) sg.level = 1;
    currentLevel = sg.level;
    active_slot = slot;

    if (sg.sensitivity > 0.0001f && sg.sensitivity < 0.05f)
        mouse_sensitivity = sg.sensitivity;

    /* Restore progression */
    hasShotgun = sg.hasShotgun ? 1 : 0;
    hasSMG = sg.hasSMG ? 1 : 0;
    hasPlasma = sg.hasPlasma ? 1 : 0;
    hasRRG = sg.hasRRG ? 1 : 0;

    godmode_enabled = sg.godmode ? 1 : 0;

    ammo_bullets = sg.ammo_bullets;
    ammo_shells = sg.ammo_shells;
    ammo_energy = sg.ammo_energy;

    if (ammo_bullets < 0) ammo_bullets = 0;
    if (ammo_shells < 0) ammo_shells = 0;
    if (ammo_energy < 0) ammo_energy = 0;

    current_weapon = sanitize_weapon(sg.weapon);

    free_map();
    if (load_map(currentLevel) != 0) {
        currentLevel = 1;
        (void)load_map(currentLevel);
    }

    init_player();
    init_enemies();
    init_items();

    /* Restore player core state after init_player() resets it. */
    hp = sg.hp;
    if (hp < 0) hp = 0;
    if (hp > 100) hp = 100;

    hasKey = sg.hasKey ? 1 : 0;
    escaped = 0;

    player_dead = (hp <= 0) ? 1 : 0;
    player_damage_timer = 0.0f;
    gun_recoil_timer = 0;
    shot_fired = 0;

    apply_player_pos_safely(sg.px, sg.py, sg.angle);

    /* Restore enemies (best-effort). */
    int nE = sg.enemy_count;
    if (nE > enemy_count) nE = enemy_count;
    if (nE > MAX_ENEMIES) nE = MAX_ENEMIES;
    for (int i = 0; i < nE; i++) {
        enemies[i].x = sg.enemy_x[i];
        enemies[i].y = sg.enemy_y[i];

        int k = sg.enemy_kind[i];
        if (k < 0) k = 0;
        if (k > 3) k = 0;
        enemies[i].kind = (EnemyKind)k;

        int st = sg.enemy_state[i];
        if (st < (int)ENEMY_ALIVE) st = (int)ENEMY_ALIVE;
        if (st > (int)ENEMY_DEAD) st = (int)ENEMY_DEAD;
        enemies[i].state = (EnemyState)st;

        enemies[i].hp = sg.enemy_hp[i];
        if (enemies[i].hp < 0) enemies[i].hp = 0;

        enemies[i].touch_cooldown = 0.0f;
        enemies[i].attack_timer = 0.0f;
        enemies[i].dying_timer = sg.enemy_dying_timer[i];
        if (enemies[i].state != ENEMY_DYING) enemies[i].dying_timer = 0.0f;
    }

    /* Restore items (match by type+position). */
    int nI = sg.item_count;
    if (nI > MAX_ITEMS) nI = MAX_ITEMS;
    for (int si = 0; si < nI; si++) {
        int stype = sg.item_type[si];
        float sx = sg.item_x[si];
        float sy = sg.item_y[si];
        int scol = sg.item_collected[si] ? 1 : 0;

        for (int li = 0; li < item_count; li++) {
            if ((int)items[li].type != stype) continue;
            float dx = items[li].x - sx;
            float dy = items[li].y - sy;
            if (dx*dx + dy*dy < 0.01f) {
                items[li].collected = scol;
                break;
            }
        }
    }

    state = STATE_PLAYING;

    show_message((slot == 1) ? "LOADED SAVE 1" : (slot == 2) ? "LOADED SAVE 2" : "LOADED SAVE 3");
    refresh_slot_meta();

    (void)renderer;
    (void)win;
    return 0;
}

static void begin_new_game(int startLevel, SDL_Window *win, SDL_Renderer *renderer)
{
    (void)renderer;
    if (startLevel < 1) startLevel = 1;
    if (startLevel > 9) startLevel = 9;
    currentLevel = startLevel;
    active_slot = 1;
    cutscene_index = 0;

    /* Reset progression */
    ammo_bullets = 10;
    ammo_shells = 0;
    ammo_energy = 0;

    hasShotgun = 0;
    hasSMG = 0;
    hasPlasma = 0;
    hasRRG = 0;

    current_weapon = WEAPON_PISTOL;

    hasKey = 0;
    escaped = 0;

    player_dead = 0;
    player_damage_timer = 0.0f;
    gun_recoil_timer = 0;
    shot_fired = 0;

    hp = 100;

    godmode_enabled = 0;
    cheat_len = 0;
    cheat_buf[0] = '\0';

    audio_bgm_set_enabled(1);

    free_map();
    if (load_map(currentLevel) != 0) {
        (void)load_map(1);
    }

    init_player();
    init_enemies();
    init_items();

    state = STATE_PLAYING;
    (void)win;
}

void show_message(const char *text)
{
    if (!text) return;
    strncpy(message_text, text, sizeof message_text - 1);
    message_text[sizeof message_text - 1] = '\0';
    message_end_time = SDL_GetTicks() + 2000;
}

static void cheat_feed_char(char c)
{
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if (c < 'a' || c > 'z') return;

    if (cheat_len < (int)sizeof(cheat_buf) - 1) {
        cheat_buf[cheat_len++] = c;
        cheat_buf[cheat_len] = '\0';
    } else {
        /* shift left */
        memmove(cheat_buf, cheat_buf + 1, sizeof(cheat_buf) - 2);
        cheat_buf[sizeof(cheat_buf) - 2] = c;
        cheat_buf[sizeof(cheat_buf) - 1] = '\0';
        cheat_len = (int)strlen(cheat_buf);
    }

    if (cheat_len >= 6) {
        if (strstr(cheat_buf, "godmod") != NULL) {
            if (!godmode_enabled) {
                godmode_enabled = 1;
                show_message("GODMODE ENABLED");
            }
            /* Clear buffer to prevent repeated triggers from lingering text */
            cheat_len = 0;
            cheat_buf[0] = '\0';
        }
    }
}

void game_loop(SDL_Window *win, SDL_Renderer *renderer)
{
    SDL_Event e;
    int running = 1;
    Uint32 lastTick = SDL_GetTicks();

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    if (renderer) {
        SDL_RenderSetLogicalSize(renderer, W, H);
        SDL_RenderSetIntegerScale(renderer, SDL_TRUE);
    }

    /* Load/create persistent config (DATA/config/config.json) and apply it. */
    (void)config_load_or_create();
    apply_config_to_runtime(win, renderer);

    (void)audio_init();

    load_textures(renderer);

    if (load_font(renderer, "pixel.bmp", "pixel.fnt", &fontPixel) != 0) {
        memset(&fontPixel, 0, sizeof fontPixel);
    }
    fontMenu = fontPixel;
    fontNumbers = fontPixel;

    state = STATE_MENU;
    free_map();
    item_count = 0;
    enemy_count = 0;

    SDL_StartTextInput();

    while (running) {
        /* Toggle mouse capture depending on state */
        static int mouse_lock = -1;
        int want_lock = (state == STATE_PLAYING);
        if (want_lock != mouse_lock) {
            SDL_SetRelativeMouseMode(want_lock ? SDL_TRUE : SDL_FALSE);
            SDL_ShowCursor(want_lock ? SDL_DISABLE : SDL_ENABLE);
            mouse_lock = want_lock;
        }

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = 0;
                continue;
            }

            if (e.type == SDL_TEXTINPUT) {
                if (state == STATE_PLAYING && e.text.text[0]) {
                    for (int i = 0; e.text.text[i]; i++) {
                        cheat_feed_char(e.text.text[i]);
                    }
                }
            }

            if (e.type == SDL_KEYDOWN && e.key.repeat == 0) {
                /* Alt+Enter toggles fullscreen (all states). */
                if ((e.key.keysym.scancode == SDL_SCANCODE_RETURN || e.key.keysym.scancode == SDL_SCANCODE_KP_ENTER) &&
                    (e.key.keysym.mod & KMOD_ALT)) {
                    toggle_fullscreen(win, renderer);
                    continue;
                }

                if (e.key.keysym.scancode == SDL_SCANCODE_F5) {
                    toggle_fullscreen(win, renderer);
                }

                if (state == STATE_MENU) {
                    SDL_Scancode sc = e.key.keysym.scancode;
                    if (sc == SDL_SCANCODE_UP) {
                        if (menu_selection > 0) menu_selection--;
                    } else if (sc == SDL_SCANCODE_DOWN) {
                        if (menu_selection < 3) menu_selection++;
                    } else if (sc == SDL_SCANCODE_RETURN) {
                        if (menu_selection == 0) {
                            episode_selection = 0;
                            state = STATE_EPISODE_SELECT;
                        } else if (menu_selection == 1) {
                            refresh_slot_meta();
                            slot_selection = 0;
                            slot_return_state = STATE_MENU;
                            state = STATE_LOADMENU;
                        } else if (menu_selection == 2) {
                            options_page = OPTPAGE_MAIN;
                            opt_main_sel = 0;
                            opt_audio_sel = 0;
                            opt_keys_sel = 0;
                            opt_capture_action = ACTION_COUNT;
                            ui_notice("ARROWS: NAVIGATE", 0);
                            state = STATE_OPTIONS;
                        } else if (menu_selection == 3) {
                            running = 0;
                        }
                    }

                } else if (state == STATE_EPISODE_SELECT) {
                    SDL_Scancode sc = e.key.keysym.scancode;
                    if (sc == SDL_SCANCODE_UP) {
                        if (episode_selection > 0) episode_selection--;
                    } else if (sc == SDL_SCANCODE_DOWN) {
                        if (episode_selection < 3) episode_selection++;
                    } else if (sc == SDL_SCANCODE_ESCAPE) {
                        state = STATE_MENU;
                    } else if (sc == SDL_SCANCODE_RETURN) {
                        if (episode_selection == 3) {
                            state = STATE_MENU;
                        } else {
                            int startLevel = 1;
                            if (episode_selection == 1) startLevel = 4;
                            if (episode_selection == 2) startLevel = 7;
                            begin_new_game(startLevel, win, renderer);
                        }
                    }

                } else if (state == STATE_OPTIONS) {
                    SDL_Scancode sc = e.key.keysym.scancode;

                    /* Key bind capture has priority. */
                    if (options_page == OPTPAGE_BIND_CAPTURE) {
                        if (sc == SDL_SCANCODE_ESCAPE) {
                            options_page = OPTPAGE_KEYS;
                            opt_capture_action = ACTION_COUNT;
                            ui_notice("CANCELLED", 1200);
                        } else {
                            if (opt_capture_action < ACTION_COUNT) {
                                SDL_Scancode old = config_get_bind(opt_capture_action);

                                if (sc == SDL_SCANCODE_BACKSPACE || sc == SDL_SCANCODE_DELETE) {
                                    config_set_bind(opt_capture_action, SDL_SCANCODE_UNKNOWN);
                                    (void)config_save();
                                    ui_notice("UNBOUND", 1200);
                                } else {
                                    /* Avoid duplicates by swapping if the key is already used. */
                                    for (int a = 0; a < ACTION_COUNT; a++) {
                                        if ((Action)a == opt_capture_action) continue;
                                        if (config_get_bind((Action)a) == sc) {
                                            config_set_bind((Action)a, old);
                                            break;
                                        }
                                    }
                                    config_set_bind(opt_capture_action, sc);
                                    (void)config_save();
                                    ui_notice("BOUND", 1200);
                                }
                            }

                            options_page = OPTPAGE_KEYS;
                            opt_capture_action = ACTION_COUNT;
                        }
                        continue;
                    }

                    if (sc == SDL_SCANCODE_ESCAPE) {
                        if (options_page == OPTPAGE_MAIN) {
                            ui_notice("", 0);
                            state = STATE_MENU;
                        } else {
                            options_page = OPTPAGE_MAIN;
                            ui_notice("ARROWS: NAVIGATE", 0);
                        }
                        continue;
                    }

                    if (options_page == OPTPAGE_MAIN) {
                        if (sc == SDL_SCANCODE_UP) {
                            if (opt_main_sel > 0) opt_main_sel--;
                        } else if (sc == SDL_SCANCODE_DOWN) {
                            if (opt_main_sel < 5) opt_main_sel++;
                        } else if (sc == SDL_SCANCODE_LEFT || sc == SDL_SCANCODE_MINUS || sc == SDL_SCANCODE_KP_MINUS) {
                            if (opt_main_sel == 1) adjust_sensitivity(-1);
                        } else if (sc == SDL_SCANCODE_RIGHT || sc == SDL_SCANCODE_EQUALS || sc == SDL_SCANCODE_KP_PLUS) {
                            if (opt_main_sel == 1) adjust_sensitivity(+1);
                        } else if (sc == SDL_SCANCODE_RETURN) {
                            if (opt_main_sel == 0) {
                                toggle_fullscreen(win, renderer);
                            } else if (opt_main_sel == 2) {
                                options_page = OPTPAGE_AUDIO;
                                opt_audio_sel = 0;
                                ui_notice("LEFT/RIGHT: ADJUST", 0);
                            } else if (opt_main_sel == 3) {
                                options_page = OPTPAGE_KEYS;
                                opt_keys_sel = 0;
                                ui_notice("ENTER: REBIND", 0);
                            } else if (opt_main_sel == 4) {
                                GameConfig *cfg = config_mut();
                                config_set_defaults(cfg);
                                (void)config_save();
                                apply_config_to_runtime(win, renderer);
                                ui_notice("RESET TO DEFAULTS", 1600);
                            } else if (opt_main_sel == 5) {
                                ui_notice("", 0);
                                state = STATE_MENU;
                            }
                        }

                    } else if (options_page == OPTPAGE_AUDIO) {
                        if (sc == SDL_SCANCODE_UP) {
                            if (opt_audio_sel > 0) opt_audio_sel--;
                        } else if (sc == SDL_SCANCODE_DOWN) {
                            if (opt_audio_sel < 5) opt_audio_sel++;
                        } else if (sc == SDL_SCANCODE_LEFT || sc == SDL_SCANCODE_MINUS || sc == SDL_SCANCODE_KP_MINUS) {
                            if (opt_audio_sel == 0) {
                                int v = audio_get_master_volume() - 8;
                                audio_set_master_volume(v);
                                config_set_master_volume(audio_get_master_volume());
                                (void)config_save();
                            } else if (opt_audio_sel == 2) {
                                int v = audio_get_bgm_volume() - 8;
                                audio_set_bgm_volume(v);
                                config_set_bgm_volume(audio_get_bgm_volume());
                                (void)config_save();
                            } else if (opt_audio_sel == 4) {
                                int v = audio_get_sfx_volume() - 8;
                                audio_set_sfx_volume(v);
                                config_set_sfx_volume(audio_get_sfx_volume());
                                (void)config_save();
                            }
                        } else if (sc == SDL_SCANCODE_RIGHT || sc == SDL_SCANCODE_EQUALS || sc == SDL_SCANCODE_KP_PLUS) {
                            if (opt_audio_sel == 0) {
                                int v = audio_get_master_volume() + 8;
                                audio_set_master_volume(v);
                                config_set_master_volume(audio_get_master_volume());
                                (void)config_save();
                            } else if (opt_audio_sel == 2) {
                                int v = audio_get_bgm_volume() + 8;
                                audio_set_bgm_volume(v);
                                config_set_bgm_volume(audio_get_bgm_volume());
                                (void)config_save();
                            } else if (opt_audio_sel == 4) {
                                int v = audio_get_sfx_volume() + 8;
                                audio_set_sfx_volume(v);
                                config_set_sfx_volume(audio_get_sfx_volume());
                                (void)config_save();
                            }
                        } else if (sc == SDL_SCANCODE_RETURN) {
                            if (opt_audio_sel == 1) {
                                int en = !audio_bgm_get_enabled();
                                audio_bgm_set_enabled(en);
                                config_set_bgm_enabled(en);
                                (void)config_save();
                            } else if (opt_audio_sel == 3) {
                                int en = !audio_sfx_get_enabled();
                                audio_sfx_set_enabled(en);
                                config_set_sfx_enabled(en);
                                (void)config_save();
                            } else if (opt_audio_sel == 5) {
                                options_page = OPTPAGE_MAIN;
                                ui_notice("ARROWS: NAVIGATE", 0);
                            }
                        }

                    } else if (options_page == OPTPAGE_KEYS) {
                        const int maxSel = ACTION_COUNT + 1;
                        if (sc == SDL_SCANCODE_UP) {
                            if (opt_keys_sel > 0) opt_keys_sel--;
                        } else if (sc == SDL_SCANCODE_DOWN) {
                            if (opt_keys_sel < maxSel) opt_keys_sel++;
                        } else if (sc == SDL_SCANCODE_RETURN) {
                            if (opt_keys_sel < ACTION_COUNT) {
                                opt_capture_action = (Action)opt_keys_sel;
                                options_page = OPTPAGE_BIND_CAPTURE;
                                ui_notice("PRESS KEY (DEL/BKSP UNBIND)", 0);
                            } else if (opt_keys_sel == ACTION_COUNT) {
                                /* Reset only key bindings to defaults. */
                                GameConfig d;
                                config_set_defaults(&d);
                                for (int a = 0; a < ACTION_COUNT; a++) {
                                    config_set_bind((Action)a, d.binds[a]);
                                }
                                (void)config_save();
                                ui_notice("KEYS RESET", 1600);
                            } else {
                                options_page = OPTPAGE_MAIN;
                                ui_notice("ARROWS: NAVIGATE", 0);
                            }
                        }
                    }

                } else if (state == STATE_END) {
                    /* Any key exits after ending. */
                    running = 0;

                } else if (state == STATE_CUTSCENE) {
                    /* Any key continues */
                    SDL_Scancode sc = e.key.keysym.scancode;
                    if (sc == SDL_SCANCODE_ESCAPE) {
                        /* Allow skipping to menu */
                        state = STATE_MENU;
                    } else {
                        /* Load next level */
                        free_map();
                        if (load_map(currentLevel) != 0) {
                            currentLevel = 1;
                            (void)load_map(currentLevel);
                        }
                        init_player();
                        init_enemies();
                        init_items();
                        hasKey = 0;
                        escaped = 0;
                        player_dead = 0;
                        player_damage_timer = 0.0f;
                        gun_recoil_timer = 0;
                        shot_fired = 0;
                        hp = 100;
                        state = STATE_PLAYING;
                    }

                } else if (state == STATE_PLAYING) {
                    SDL_Scancode sc = e.key.keysym.scancode;
                    if (sc == config_get_bind(ACTION_PAUSE)) {
                        pause_selection = 0;
                        state = STATE_PAUSED;
                        ui_notice("", 0);
                    }

                } else if (state == STATE_PAUSED) {
                    SDL_Scancode sc = e.key.keysym.scancode;
                    if (sc == SDL_SCANCODE_UP) {
                        if (pause_selection > 0) pause_selection--;
                    } else if (sc == SDL_SCANCODE_DOWN) {
                        if (pause_selection < 3) pause_selection++;
                    } else if (sc == SDL_SCANCODE_ESCAPE) {
                        state = STATE_PLAYING;
                    } else if (sc == SDL_SCANCODE_RETURN) {
                        if (pause_selection == 0) {
                            state = STATE_PLAYING;
                        } else if (pause_selection == 1) {
                            refresh_slot_meta();
                            slot_selection = 0;
                            slot_return_state = STATE_PAUSED;
                            state = STATE_LOADMENU;
                        } else if (pause_selection == 2) {
                            refresh_slot_meta();
                            slot_selection = 0;
                            slot_return_state = STATE_PAUSED;
                            state = STATE_SAVEMENU;
                        } else if (pause_selection == 3) {
                            state = STATE_MENU;
                        }
                    }

                } else if (state == STATE_LOADMENU) {
                    SDL_Scancode sc = e.key.keysym.scancode;
                    if (sc == SDL_SCANCODE_UP) {
                        if (slot_selection > 0) slot_selection--;
                    } else if (sc == SDL_SCANCODE_DOWN) {
                        if (slot_selection < 3) slot_selection++;
                    } else if (sc == SDL_SCANCODE_ESCAPE) {
                        state = slot_return_state;
                    } else if (sc == SDL_SCANCODE_RETURN) {
                        if (slot_selection == 3) {
                            state = slot_return_state;
                        } else {
                            (void)load_slot_and_enter(slot_selection + 1, win, renderer);
                        }
                    }

                } else if (state == STATE_SAVEMENU) {
                    SDL_Scancode sc = e.key.keysym.scancode;
                    if (sc == SDL_SCANCODE_UP) {
                        if (slot_selection > 0) slot_selection--;
                    } else if (sc == SDL_SCANCODE_DOWN) {
                        if (slot_selection < 3) slot_selection++;
                    } else if (sc == SDL_SCANCODE_ESCAPE) {
                        state = slot_return_state;
                    } else if (sc == SDL_SCANCODE_RETURN) {
                        if (slot_selection == 3) {
                            state = slot_return_state;
                        } else {
                            int slot = slot_selection + 1;
                            if (save_current_to_slot(slot) == 0) {
                                ui_notice((slot == 1) ? "SAVED TO SLOT 1" : (slot == 2) ? "SAVED TO SLOT 2" : "SAVED TO SLOT 3", 1200);
                                state = slot_return_state;
                            } else {
                                ui_notice("SAVE FAILED", 1400);
                            }
                        }
                    }
                }
            }
        }

        Uint32 now = SDL_GetTicks();
        float dt = (now - lastTick) / 1000.0f;
        lastTick = now;

        if (state == STATE_PLAYING) {
            static int prev_player_dead = 0;

            update_player(dt);
            update_enemies(dt);
            update_items();

            if (shot_fired) {
                /* Play different sound depending on weapon type. */
                SfxId sfx = SFX_GUN;
                switch (current_weapon) {
                    case WEAPON_SHOTGUN:
                        sfx = SFX_SHOTGUN;
                        break;
                    case WEAPON_PLASMA:
                        sfx = SFX_PLASMA;
                        break;
                    case WEAPON_RRG:
                        sfx = SFX_RRG;
                        break;
                    default:
                        sfx = SFX_GUN;
                        break;
                }
                audio_play_sfx(sfx);
            }

            if (!prev_player_dead && player_dead) {
                audio_play_sfx(SFX_PLAYER_DIE);
            }
            prev_player_dead = player_dead;

            if (shot_fired) {
                int hitboxSize = weapon_hitbox_size(current_weapon);
                int hx = W / 2 - hitboxSize / 2;
                int hy = H / 2 - hitboxSize / 2;
                int best = -1;
                float bestDist = 1e9f;

                const float fov = FOV;

                for (int i = 0; i < enemy_count; i++) {
                    if (enemies[i].state != ENEMY_ALIVE) continue;

                    float dx = enemies[i].x - px;
                    float dy = enemies[i].y - py;
                    float dist = sqrtf(dx*dx + dy*dy);
                    float dir = atan2f(dy, dx) - angle;

                    while (dir >  M_PI) dir -= 2 * M_PI;
                    while (dir < -M_PI) dir += 2 * M_PI;

                    if (fabsf(dir) < fov * 0.5f) {
                        float sx = (dir + fov * 0.5f) / fov * W;
                        float sy = H / 2;
                        if (sx >= hx && sx <= hx + hitboxSize && sy >= hy && sy <= hy + hitboxSize) {
                            if (dist < bestDist) {
                                bestDist = dist;
                                best = i;
                            }
                        }
                    }
                }

                if (best >= 0) {
                    damage_enemy(best, weapon_damage(current_weapon));
                }
            }

            if (escaped) {
                escaped = 0;

                if (currentLevel < 9) {
                    /* Show cutscene currentLevel.bmp, then load next map. */
                    cutscene_index = currentLevel;
                    currentLevel++;

                    (void)save_progress_to_slot(active_slot);
                    audio_play_sfx(SFX_VICTORY);

                    state = STATE_CUTSCENE;
                } else {
                    state = STATE_END;
                    audio_bgm_set_enabled(0);
                    audio_play_sfx(SFX_ENDING);
                }
            }
        }

        /* Render */
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        if (state == STATE_MENU) {
            if (texMenu) {
                SDL_RenderCopy(renderer, texMenu, NULL, &(SDL_Rect){0, 0, W, H});
            }

            const char *itemsM[4] = { "START GAME", "LOAD GAME", "OPTIONS", "QUIT" };
            for (int i = 0; i < 4; i++) {
                float scale = (i == menu_selection) ? 3.0f : 2.0f;
                int textWidth = measure_text(&fontPixel, itemsM[i], scale);
                int x = (W - textWidth) / 2;
                int y = H / 2 - 100 + i * 70;
                draw_text(renderer, &fontPixel, x, y, itemsM[i], scale);
            }

        } else if (state == STATE_EPISODE_SELECT) {
            if (texMenu) {
                SDL_RenderCopy(renderer, texMenu, NULL, &(SDL_Rect){0, 0, W, H});
            }

            const char *title = "SELECT EPISODE";
            int tw = measure_text(&fontPixel, title, 3.0f);
            draw_text(renderer, &fontPixel, (W - tw) / 2, 70, title, 3.0f);

            const char *eps[4] = {
                "ESCAPE THE ALIENS (MAP 1-3)",
                "ALIEN INVASION ON EARTH (MAP 4-6)",
                "FINAL CONFRONTATION (MAP 7-9)",
                "BACK"
            };

            for (int i = 0; i < 4; i++) {
                float scale = (i == episode_selection) ? 2.4f : 1.9f;
                int w = measure_text(&fontPixel, eps[i], scale);
                int x = (W - w) / 2;
                int y = 190 + i * 70;
                draw_text(renderer, &fontPixel, x, y, eps[i], scale);
            }

            const char *hint = "ENTER TO START  ESC TO BACK";
            int hw = measure_text(&fontPixel, hint, 1.0f);
            draw_text(renderer, &fontPixel, (W - hw) / 2, H - 70, hint, 1.0f);

        } else if (state == STATE_OPTIONS) {
            if (texMenu) {
                SDL_RenderCopy(renderer, texMenu, NULL, &(SDL_Rect){0, 0, W, H});
            }

            const char *title =
                (options_page == OPTPAGE_MAIN) ? "OPTIONS" :
                (options_page == OPTPAGE_AUDIO) ? "AUDIO" :
                (options_page == OPTPAGE_KEYS) ? "KEY BINDINGS" :
                "BIND KEY";

            int tw = measure_text(&fontPixel, title, 3.0f);
            draw_text(renderer, &fontPixel, (W - tw) / 2, 60, title, 3.0f);

            if (options_page == OPTPAGE_MAIN) {
                char line0[64];
                snprintf(line0, sizeof line0, "FULLSCREEN: %s", isFullscreen ? "ON" : "OFF");
                char line1[64];
                int sensVal = (int)(mouse_sensitivity * 10000.0f + 0.5f);
                snprintf(line1, sizeof line1, "MOUSE SENSITIVITY: %d", sensVal);

                const char *lines[6] = {
                    line0,
                    line1,
                    "AUDIO...",
                    "KEY BINDINGS...",
                    "RESET TO DEFAULTS",
                    "BACK"
                };

                for (int i = 0; i < 6; i++) {
                    float scale = (i == opt_main_sel) ? 2.5f : 2.0f;
                    int w = measure_text(&fontPixel, lines[i], scale);
                    int x = (W - w) / 2;
                    int y = 170 + i * 70;
                    draw_text(renderer, &fontPixel, x, y, lines[i], scale);
                }

            } else if (options_page == OPTPAGE_AUDIO) {
                int master = audio_get_master_volume();
                int bgmV = audio_get_bgm_volume();
                int sfxV = audio_get_sfx_volume();

                int masterP = (master * 100) / 128;
                int bgmP = (bgmV * 100) / 128;
                int sfxP = (sfxV * 100) / 128;

                char a0[64];
                snprintf(a0, sizeof a0, "MASTER VOLUME: %d%%", masterP);
                char a1[64];
                snprintf(a1, sizeof a1, "BGM: %s", audio_bgm_get_enabled() ? "ON" : "OFF");
                char a2[64];
                snprintf(a2, sizeof a2, "BGM VOLUME: %d%%", bgmP);
                char a3[64];
                snprintf(a3, sizeof a3, "SFX: %s", audio_sfx_get_enabled() ? "ON" : "OFF");
                char a4[64];
                snprintf(a4, sizeof a4, "SFX VOLUME: %d%%", sfxP);

                const char *lines[6] = { a0, a1, a2, a3, a4, "BACK" };

                for (int i = 0; i < 6; i++) {
                    float scale = (i == opt_audio_sel) ? 2.4f : 1.95f;
                    int w = measure_text(&fontPixel, lines[i], scale);
                    int x = (W - w) / 2;
                    int y = 170 + i * 65;
                    draw_text(renderer, &fontPixel, x, y, lines[i], scale);
                }

            } else if (options_page == OPTPAGE_KEYS) {
                /* 0..ACTION_COUNT-1: actions, ACTION_COUNT: reset keys, ACTION_COUNT+1: back */
                for (int i = 0; i < ACTION_COUNT + 2; i++) {
                    char line[192];
                    if (i == ACTION_COUNT) {
                        snprintf(line, sizeof line, "RESET KEYS TO DEFAULTS");
                    } else if (i == ACTION_COUNT + 1) {
                        snprintf(line, sizeof line, "BACK");
                    } else {
                        Action a = (Action)i;
                        SDL_Scancode sc = config_get_bind(a);
                        const char *key = SDL_GetScancodeName(sc);
                        if (!key || !key[0]) key = "UNBOUND";
                        snprintf(line, sizeof line, "%s: %s", config_action_label(a), key);
                    }

                    float scale = (i == opt_keys_sel) ? 1.9f : 1.5f;
                    int w = measure_text(&fontPixel, line, scale);
                    int x = (W - w) / 2;
                    int y = 140 + i * 40;
                    draw_text(renderer, &fontPixel, x, y, line, scale);
                }

            } else { /* OPTPAGE_BIND_CAPTURE */
                const char *p1 = "PRESS A KEY";
                const char *p2 = (opt_capture_action < ACTION_COUNT) ? config_action_label(opt_capture_action) : "";
                const char *p3 = "DEL/BKSP: UNBIND   ESC: CANCEL";

                float sc1 = 3.0f;
                float sc2 = 2.4f;
                float sc3 = 1.3f;

                int w1 = measure_text(&fontPixel, p1, sc1);
                int w2 = measure_text(&fontPixel, p2, sc2);
                int w3 = measure_text(&fontPixel, p3, sc3);

                int y = H / 2 - 70;
                draw_text(renderer, &fontPixel, (W - w1) / 2, y, p1, sc1);
                draw_text(renderer, &fontPixel, (W - w2) / 2, y + 80, p2, sc2);
                draw_text(renderer, &fontPixel, (W - w3) / 2, y + 150, p3, sc3);
            }

            /* Bottom hint/notice (persistent if end_time == 0). */
            if (menu_notice[0]) {
                Uint32 now = SDL_GetTicks();
                if (menu_notice_end_time == 0 || now < menu_notice_end_time) {
                    int nw = measure_text(&fontPixel, menu_notice, 1.0f);
                    draw_text(renderer, &fontPixel, (W - nw) / 2, H - 60, menu_notice, 1.0f);
                }
            }

        } else if (state == STATE_PAUSED || state == STATE_LOADMENU || state == STATE_SAVEMENU) {
            draw_world(renderer);
            draw_keys(renderer);
            draw_items(renderer);
            draw_enemies(renderer);
            draw_hud(renderer);

            char hpStr[32];
            snprintf(hpStr, sizeof hpStr, "HP %d", hp);
            draw_text(renderer, &fontPixel, 20, H - 112, hpStr, 2.0f);

            char ammoStr[32];
            build_ammo_string(ammoStr, sizeof ammoStr);
            int aw = measure_text(&fontPixel, ammoStr, 2.0f);
            draw_text(renderer, &fontPixel, W - 20 - aw, H - 112, ammoStr, 2.0f);
            draw_gun(renderer);

            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
            SDL_RenderFillRect(renderer, &(SDL_Rect){0, 0, W, H});

            const char *title = (state == STATE_PAUSED) ? "PAUSED" : (state == STATE_LOADMENU) ? "LOAD GAME" : "SAVE GAME";
            int tww = measure_text(&fontPixel, title, 3.0f);
            draw_text(renderer, &fontPixel, (W - tww) / 2, 70, title, 3.0f);

            if (state == STATE_PAUSED) {
                const char *opts[4] = { "CONTINUE GAME", "LOAD GAME", "SAVE GAME", "QUIT GAME" };
                for (int i = 0; i < 4; i++) {
                    float s = (i == pause_selection) ? 2.8f : 2.2f;
                    int w = measure_text(&fontPixel, opts[i], s);
                    int x = (W - w) / 2;
                    int y = 190 + i * 70;
                    draw_text(renderer, &fontPixel, x, y, opts[i], s);
                }
            } else {
                for (int i = 0; i < 4; i++) {
                    char line[160];
                    if (i < 3) {
                        const SaveMeta *m = &slot_meta[i];
                        if (m->exists) {
                            snprintf(line, sizeof line,
                                     "SAVE %d (L%d HP%d B%d S%d E%d)%s",
                                     i + 1, m->level, m->hp,
                                     m->ammo_bullets, m->ammo_shells, m->ammo_energy,
                                     (active_slot == i + 1) ? " *" : "");
                        } else {
                            snprintf(line, sizeof line, "SAVE %d (EMPTY)%s", i + 1, (active_slot == i + 1) ? " *" : "");
                        }
                    } else {
                        snprintf(line, sizeof line, "BACK");
                    }

                    float s = (i == slot_selection) ? 2.6f : 2.1f;
                    int w = measure_text(&fontPixel, line, s);
                    int x = (W - w) / 2;
                    int y = 190 + i * 70;
                    draw_text(renderer, &fontPixel, x, y, line, s);
                }

                const char *hint = (state == STATE_LOADMENU) ? "ENTER TO LOAD  ESC TO BACK" : "ENTER TO SAVE  ESC TO BACK";
                int hw = measure_text(&fontPixel, hint, 1.0f);
                draw_text(renderer, &fontPixel, (W - hw) / 2, H - 70, hint, 1.0f);
            }

            if (menu_notice[0]) {
                Uint32 now = SDL_GetTicks();
                if (menu_notice_end_time == 0 || now < menu_notice_end_time) {
                    int nw = measure_text(&fontPixel, menu_notice, 1.8f);
                    draw_text(renderer, &fontPixel, (W - nw) / 2, H - 120, menu_notice, 1.8f);
                }
            }

            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

        } else if (state == STATE_PLAYING) {
            draw_world(renderer);
            draw_keys(renderer);
            draw_items(renderer);
            draw_enemies(renderer);
            draw_hud(renderer);

            char hpStr[32];
            snprintf(hpStr, sizeof hpStr, "HP %d", hp);
            draw_text(renderer, &fontPixel, 20, H - 112, hpStr, 2.0f);

            char ammoStr[32];
            build_ammo_string(ammoStr, sizeof ammoStr);
            int aw = measure_text(&fontPixel, ammoStr, 2.0f);
            draw_text(renderer, &fontPixel, W - 20 - aw, H - 112, ammoStr, 2.0f);

            draw_gun(renderer);

            if (SDL_GetTicks() < message_end_time && message_text[0]) {
                draw_text(renderer, &fontPixel, 20, H - 160, message_text, 2.0f);
            }

        } else if (state == STATE_CUTSCENE) {
            SDL_Texture *t = NULL;
            if (cutscene_index >= 1 && cutscene_index <= 8) t = texCutscene[cutscene_index];

            if (t) {
                SDL_RenderCopy(renderer, t, NULL, &(SDL_Rect){0, 0, W, H});
            }

            const char *hint = "PRESS ANY KEY";
            int hw = measure_text(&fontPixel, hint, 2.0f);
            draw_text(renderer, &fontPixel, (W - hw) / 2, H - 80, hint, 2.0f);

        } else if (state == STATE_END) {
            if (texEnding) {
                SDL_RenderCopy(renderer, texEnding, NULL, &(SDL_Rect){0, 0, W, H});
            }
            const char *line1 = "YOU SAVED THE EARTH!";
            const char *line2 = "SEE YOU IN PART 2!";

            const float scale = 2.5f;
            const int gap = 10;
            const int lh = (int)(fontPixel.lineHeight * scale);

            int w1 = measure_text(&fontPixel, line1, scale);
            int w2 = measure_text(&fontPixel, line2, scale);

            int x1 = (W - w1) / 2;
            int x2 = (W - w2) / 2;

            int y1 = (H - (lh * 2 + gap)) / 2;
            int y2 = y1 + lh + gap;

            draw_text(renderer, &fontPixel, x1, y1, line1, scale);
            draw_text(renderer, &fontPixel, x2, y2, line2, scale);
        }

        SDL_RenderPresent(renderer);
    }

    SDL_StopTextInput();

    audio_shutdown();
    free_map();
}
