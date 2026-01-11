#include <SDL2/SDL.h>
#include <math.h>

#include "player.h"
#include "game.h"
#include "map.h"
#include "enemy.h"
#include "audio.h"
#include "config.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------
 * PLAYER STATE
 * ------------------------------------------------------------ */
float px = 3.0f;
float py = 3.0f;
float angle = 0.0f;

int gun_recoil_timer = 0;
int shot_fired = 0;

int hasKey = 0;
int escaped = 0;

int godmode_enabled = 0;

int hasShotgun = 0;
int hasSMG = 0;
int hasPlasma = 0;
int hasRRG = 0;
WeaponType current_weapon = WEAPON_PISTOL;

float mouse_sensitivity = 0.0035f;

int hp = 100;
int ammo_bullets = 10;
int ammo_shells = 0;
int ammo_energy = 0;

float player_damage_timer = 0.0f;
int player_dead = 0;

static float moveSpeed = 2.0f;

/* Cooldowns (seconds, frame-rate independent) */
#define PISTOL_COOLDOWN_SEC  0.55f
#define SHOTGUN_COOLDOWN_SEC 0.95f
#define SMG_COOLDOWN_SEC     0.12f
#define PLASMA_COOLDOWN_SEC  0.16f
#define RRG_COOLDOWN_SEC     0.90f

static float cd_pistol = 0.0f;
static float cd_shotgun = 0.0f;
static float cd_smg = 0.0f;
static float cd_plasma = 0.0f;
static float cd_rrg = 0.0f;

static void cooldown_tick(float *t, float dt)
{
    if (*t > 0.0f) {
        *t -= dt;
        if (*t < 0.0f) *t = 0.0f;
    }
}

static int weapon_has_ammo(WeaponType w)
{
    if (godmode_enabled) return 1;
    switch (w) {
        case WEAPON_PISTOL: return (ammo_bullets > 0);
        case WEAPON_SMG: return (ammo_bullets > 0);
        case WEAPON_SHOTGUN: return (ammo_shells > 0);
        case WEAPON_PLASMA: return (ammo_energy > 0);
        case WEAPON_RRG: return (ammo_energy >= 5);
        default: return 0;
    }
}

static void weapon_consume_ammo(WeaponType w)
{
    if (godmode_enabled) return;
    switch (w) {
        case WEAPON_PISTOL:
        case WEAPON_SMG:
            ammo_bullets--;
            if (ammo_bullets < 0) ammo_bullets = 0;
            break;
        case WEAPON_SHOTGUN:
            ammo_shells--;
            if (ammo_shells < 0) ammo_shells = 0;
            break;
        case WEAPON_PLASMA:
            ammo_energy--;
            if (ammo_energy < 0) ammo_energy = 0;
            break;
        case WEAPON_RRG:
            ammo_energy -= 5;
            if (ammo_energy < 0) ammo_energy = 0;
            break;
    }
}

void init_player(void)
{
    if (worldmap && worldWidth > 0 && worldHeight > 0) {
        px = player_spawn_x;
        py = player_spawn_y;
    } else {
        px = 3.0f;
        py = 3.0f;
    }
    angle = 0.0f;

    hp = 100;
    if (hp < 0) hp = 0;

    if (ammo_bullets < 0) ammo_bullets = 0;
    if (ammo_shells < 0) ammo_shells = 0;
    if (ammo_energy < 0) ammo_energy = 0;

    hasKey = 0;
    escaped = 0;

    gun_recoil_timer = 0;
    shot_fired = 0;

    cd_pistol = cd_shotgun = cd_smg = cd_plasma = cd_rrg = 0.0f;

    /* Make sure current weapon is valid. */
    if (current_weapon == WEAPON_SHOTGUN && !hasShotgun) current_weapon = WEAPON_PISTOL;
    if (current_weapon == WEAPON_SMG && !hasSMG) current_weapon = WEAPON_PISTOL;
    if (current_weapon == WEAPON_PLASMA && !hasPlasma) current_weapon = WEAPON_PISTOL;
    if (current_weapon == WEAPON_RRG && !hasRRG) current_weapon = WEAPON_PISTOL;

    player_damage_timer = 0.0f;
    player_dead = 0;
}

void update_player(float dt)
{
    if (godmode_enabled) {
        player_dead = 0;
        hp = 100;
    }

    if (player_dead)
        return;

    const Uint8 *k = SDL_GetKeyboardState(NULL);

    /* ---------------- Mouse look ---------------- */
    int mdx = 0, mdy = 0;
    Uint32 mstate = SDL_GetRelativeMouseState(&mdx, &mdy);
    (void)mdy;

    angle += (float)mdx * mouse_sensitivity;

    const float two_pi = (float)(M_PI * 2.0);
    if (angle >= two_pi || angle <= -two_pi) angle = fmodf(angle, two_pi);
    if (angle < 0.0f) angle += two_pi;

    /* ---------------- Movement ---------------- */
    float nx = px;
    float ny = py;

    /* Configurable binds (scancodes). */
    SDL_Scancode b_fwd  = config_get_bind(ACTION_MOVE_FORWARD);
    SDL_Scancode b_back = config_get_bind(ACTION_MOVE_BACK);
    SDL_Scancode b_left = config_get_bind(ACTION_STRAFE_LEFT);
    SDL_Scancode b_right= config_get_bind(ACTION_STRAFE_RIGHT);

    #define KEYDOWN(sc) ((sc) != SDL_SCANCODE_UNKNOWN && k[(sc)])

    if (KEYDOWN(b_fwd))  { nx += cosf(angle) * moveSpeed * dt; ny += sinf(angle) * moveSpeed * dt; }
    if (KEYDOWN(b_back)) { nx -= cosf(angle) * moveSpeed * dt; ny -= sinf(angle) * moveSpeed * dt; }
    if (KEYDOWN(b_left)) { nx += cosf(angle - (float)M_PI/2.0f) * moveSpeed * dt; ny += sinf(angle - (float)M_PI/2.0f) * moveSpeed * dt; }
    if (KEYDOWN(b_right)){ nx += cosf(angle + (float)M_PI/2.0f) * moveSpeed * dt; ny += sinf(angle + (float)M_PI/2.0f) * moveSpeed * dt; }

    int tx = (int)nx;
    int ty = (int)ny;

    if (worldmap && tx >= 0 && ty >= 0 && tx < worldWidth && ty < worldHeight) {
        if (worldmap[ty][tx] < 2) {
            px = nx;
            py = ny;
        }
    }

    /* ---------------- Shooting ---------------- */
    cooldown_tick(&cd_pistol, dt);
    cooldown_tick(&cd_shotgun, dt);
    cooldown_tick(&cd_smg, dt);
    cooldown_tick(&cd_plasma, dt);
    cooldown_tick(&cd_rrg, dt);

    shot_fired = 0;
    static int lastLMB = 0;
    int currLMB = ((mstate & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0);
    int edge = (currLMB && !lastLMB);

    int want_fire = 0;
    float *cd = NULL;
    float cd_reset = 0.0f;
    int recoil_frames = 6;
    int require_edge = 1;

    switch (current_weapon) {
        case WEAPON_PISTOL:
            cd = &cd_pistol; cd_reset = PISTOL_COOLDOWN_SEC; recoil_frames = 6; require_edge = 1;
            break;
        case WEAPON_SHOTGUN:
            cd = &cd_shotgun; cd_reset = SHOTGUN_COOLDOWN_SEC; recoil_frames = 10; require_edge = 1;
            break;
        case WEAPON_SMG:
            cd = &cd_smg; cd_reset = SMG_COOLDOWN_SEC; recoil_frames = 3; require_edge = 0;
            break;
        case WEAPON_PLASMA:
            cd = &cd_plasma; cd_reset = PLASMA_COOLDOWN_SEC; recoil_frames = 4; require_edge = 0;
            break;
        case WEAPON_RRG:
            cd = &cd_rrg; cd_reset = RRG_COOLDOWN_SEC; recoil_frames = 14; require_edge = 1;
            break;
    }

    if (require_edge) want_fire = edge;
    else want_fire = currLMB;

    if (want_fire && cd && (*cd <= 0.0f) && weapon_has_ammo(current_weapon)) {
        weapon_consume_ammo(current_weapon);
        gun_recoil_timer = recoil_frames;
        shot_fired = 1;
        *cd = cd_reset;
    }

    lastLMB = currLMB;

    /* ---------------- Weapon switch (binds) ---------------- */
    static int lastWep[5] = {0,0,0,0,0};
    SDL_Scancode b1 = config_get_bind(ACTION_WEAPON_1);
    SDL_Scancode b2 = config_get_bind(ACTION_WEAPON_2);
    SDL_Scancode b3 = config_get_bind(ACTION_WEAPON_3);
    SDL_Scancode b4 = config_get_bind(ACTION_WEAPON_4);
    SDL_Scancode b5 = config_get_bind(ACTION_WEAPON_5);

    int curr[5];
    curr[0] = KEYDOWN(b1);
    curr[1] = KEYDOWN(b2);
    curr[2] = KEYDOWN(b3);
    curr[3] = KEYDOWN(b4);
    curr[4] = KEYDOWN(b5);

    if (curr[0] && !lastWep[0]) current_weapon = WEAPON_PISTOL;
    if (curr[1] && !lastWep[1] && hasShotgun) current_weapon = WEAPON_SHOTGUN;
    if (curr[2] && !lastWep[2] && hasSMG) current_weapon = WEAPON_SMG;
    if (curr[3] && !lastWep[3] && hasPlasma) current_weapon = WEAPON_PLASMA;
    if (curr[4] && !lastWep[4] && hasRRG) current_weapon = WEAPON_RRG;

    for (int i = 0; i < 5; i++) lastWep[i] = curr[i];

    /* ---------------- Interaction (bind) ---------------- */
    static int lastE = 0;
    SDL_Scancode b_interact = config_get_bind(ACTION_INTERACT);
    int currE = KEYDOWN(b_interact);

    if (currE && !lastE) {
        if (worldmap) {
            for (int y = 0; y < worldHeight; y++)
            for (int x = 0; x < worldWidth; x++) {
                float cx = x + 0.5f;
                float cy = y + 0.5f;

                float dx = cx - px;
                float dy = cy - py;
                float dist = sqrtf(dx * dx + dy * dy);

                int tile = worldmap[y][x];

                if (tile == 1 && dist < 0.7f) {
                    worldmap[y][x] = 0;
                    hasKey = 1;
                    show_message("you got the key!");
                    audio_play_sfx(SFX_ITEM);
                }
                else if (tile == 3 && dist < 1.0f) {
                    if (enemy_boss_alive()) {
                        show_message("the exit is sealed. defeat the boss!");
                    } else if (hasKey) {
                        worldmap[y][x] = 0;
                        escaped = 1;
                    } else {
                        show_message("you need key to open the exit door.");
                    }
                }
            }
        }
    }
    lastE = currE;

    #undef KEYDOWN

    /* ---------------- Damage FX timer ---------------- */
    if (player_damage_timer > 0.0f) {
        player_damage_timer -= dt;
        if (player_damage_timer < 0.0f)
            player_damage_timer = 0.0f;
    }
}
