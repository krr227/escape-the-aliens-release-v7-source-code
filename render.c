#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "render.h"
#include "player.h"
#include "enemy.h"
#include "map.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAX_DIST 30.0f
/* Removed STEP constant – we now use a DDA algorithm for raycasts. */
/* The DDA algorithm calculates intersections with the grid and reduces per-ray
 * iterations compared to naive stepping. */

/* -------------------------------------------------------------------------
 * Visibility helper
 *
 * Determine whether a point at (tx, ty) is visible from the player's
 * position by checking for walls along the line between them. The function
 * marches along the line using small increments and stops when it either
 * encounters a solid wall or reaches the tile containing the target point.
 * Returning 1 indicates the target is visible, 0 means it is occluded.
 */
static int is_visible_to_player(float tx, float ty)
{
    /* Compute direction vector from player to target. */
    float dx = tx - px;
    float dy = ty - py;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist <= 0.0f) {
        return 1;
    }
    float invDist = 1.0f / dist;
    float vx = dx * invDist;
    float vy = dy * invDist;

    float t = 0.0f;
    /* Step along the line in small increments. A smaller step results in
     * finer visibility testing at the cost of more iterations. */
    const float step = 0.05f;
    while (t < dist) {
        float cx = px + vx * t;
        float cy = py + vy * t;
        int mx = (int)cx;
        int my = (int)cy;
        if (mx < 0 || my < 0 || mx >= worldWidth || my >= worldHeight) {
            return 0;
        }
        /* If we've reached the tile containing the target, stop. */
        if (mx == (int)tx && my == (int)ty) {
            break;
        }
        /* If this tile is a wall/door, the target is occluded. */
        if (worldmap[my][mx] >= 2) {
            return 0;
        }
        t += step;
    }
    return 1;
}

/* ------------------------------------------------------------
 * TEXTURES
 * ------------------------------------------------------------ */
SDL_Texture *texWall1_ep[3] = {NULL, NULL, NULL};
SDL_Texture *texWall2_ep[3] = {NULL, NULL, NULL};
SDL_Texture *texFloor_ep[3] = {NULL, NULL, NULL};
SDL_Texture *texCeil_ep[3]  = {NULL, NULL, NULL};
SDL_Texture *texDoor = NULL;

SDL_Texture *texKey = NULL;

SDL_Texture *texMenu = NULL;
SDL_Texture *texCutscene[9] = {NULL};
SDL_Texture *texEnding = NULL;

SDL_Texture *texAmmo = NULL;
SDL_Texture *texMedkit = NULL;
SDL_Texture *texShotgunItem = NULL;
SDL_Texture *texSMGItem = NULL;
SDL_Texture *texShells = NULL;
SDL_Texture *texEnergy = NULL;
SDL_Texture *texPlasmaItem = NULL;
SDL_Texture *texRRGItem = NULL;

SDL_Texture *texEnemy1 = NULL;
SDL_Texture *texEnemy1Die = NULL;
SDL_Texture *texEnemy1Attack = NULL;

SDL_Texture *texEnemy2 = NULL;
SDL_Texture *texEnemy2Die = NULL;
SDL_Texture *texEnemy2Attack = NULL;

SDL_Texture *texMiniboss1 = NULL;
SDL_Texture *texMiniboss1Die = NULL;
SDL_Texture *texMiniboss1Attack = NULL;

SDL_Texture *texFinalboss = NULL;
SDL_Texture *texFinalbossDie = NULL;
SDL_Texture *texFinalbossAttack = NULL;

SDL_Texture *texGun = NULL;
SDL_Texture *texGunRecoil = NULL;
SDL_Texture *texShotgun = NULL;
SDL_Texture *texShotgunRecoil = NULL;
SDL_Texture *texSMG = NULL;
SDL_Texture *texSMGRecoil = NULL;
SDL_Texture *texPlasma = NULL;
SDL_Texture *texPlasmaRecoil = NULL;
SDL_Texture *texRRG = NULL;
SDL_Texture *texRRGRecoil = NULL;

SDL_Texture *texPlayer = NULL;
SDL_Texture *texPlayerDamage = NULL;
SDL_Texture *texPlayerDead = NULL;
SDL_Texture *texGodmod = NULL;

static char ASSET_PATH[512];

static void init_asset_path(void)
{
    char *base = SDL_GetBasePath();
    if (base) {
        snprintf(ASSET_PATH, sizeof ASSET_PATH, "%sDATA/ASSETS/", base);
        SDL_free(base);
    } else {
        strcpy(ASSET_PATH, "DATA/ASSETS/");
    }
}

static SDL_Texture *load_tex(SDL_Renderer *r, const char *file, int ck)
{
    char full[512];
    snprintf(full, sizeof full, "%s%s", ASSET_PATH, file);

    SDL_Surface *s = SDL_LoadBMP(full);
    if (!s) {
        return NULL;
    }

    if (ck)
        SDL_SetColorKey(s, SDL_TRUE, SDL_MapRGB(s->format, 0, 0, 0));

    SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
    SDL_FreeSurface(s);
    return t;
}

static SDL_Texture *load_tex_try(SDL_Renderer *r, int ck, const char *a, const char *b)
{
    SDL_Texture *t = NULL;
    if (a && a[0]) t = load_tex(r, a, ck);
    if (!t && b && b[0]) t = load_tex(r, b, ck);
    return t;
}

static void load_episode_textures(SDL_Renderer *r)
{
    /* EP1 */
    texWall1_ep[0] = load_tex_try(r, 0, "wall1.bmp", "wall.bmp");
    texWall2_ep[0] = load_tex(r, "wall2.bmp", 0);
    texFloor_ep[0] = load_tex(r, "floor.bmp", 0);
    texCeil_ep[0]  = load_tex(r, "ceiling.bmp", 0);

    /* EP2 */
    texWall1_ep[1] = load_tex(r, "wall_ep2.bmp", 0);
    texWall2_ep[1] = load_tex(r, "wall2_ep2.bmp", 0);
    texFloor_ep[1] = load_tex(r, "floor_ep2.bmp", 0);
    texCeil_ep[1]  = load_tex(r, "ceiling_ep2.bmp", 0);

    /* EP3 */
    texWall1_ep[2] = load_tex(r, "wall_ep3.bmp", 0);
    texWall2_ep[2] = load_tex(r, "wall2_ep3.bmp", 0);
    texFloor_ep[2] = load_tex(r, "floor_ep3.bmp", 0);
    texCeil_ep[2]  = load_tex(r, "ceiling_ep3.bmp", 0);

    /* Fallbacks if episode assets are missing (keep game running). */
    for (int i = 0; i < 3; i++) {
        if (!texWall1_ep[i]) texWall1_ep[i] = texWall1_ep[0];
        if (!texWall2_ep[i]) texWall2_ep[i] = texWall2_ep[0];
        if (!texFloor_ep[i]) texFloor_ep[i] = texFloor_ep[0];
        if (!texCeil_ep[i])  texCeil_ep[i]  = texCeil_ep[0];
    }
}

void load_textures(SDL_Renderer *r)
{
    init_asset_path();

    load_episode_textures(r);

    texDoor = load_tex(r, "door.bmp", 1);
    texKey  = load_tex(r, "key.bmp", 1);

    texMenu = load_tex(r, "menu.bmp", 0);

    for (int i = 1; i <= 8; i++) {
        char name[32];
        snprintf(name, sizeof name, "%d.bmp", i);
        texCutscene[i] = load_tex(r, name, 0);
    }
    texEnding = load_tex_try(r, 0, "ending.bmp", "escape.bmp");

    texAmmo        = load_tex(r, "ammo.bmp", 1);
    texMedkit      = load_tex(r, "medkit.bmp", 1);
    texShotgunItem = load_tex_try(r, 1, "shotgun_item.bmp", "shogun_item.bmp");
    texSMGItem     = load_tex(r, "smg_item.bmp", 1);
    texShells      = load_tex(r, "shells.bmp", 1);
    texEnergy      = load_tex(r, "energy.bmp", 1);
    texPlasmaItem  = load_tex(r, "plasma_item.bmp", 1);
    texRRGItem     = load_tex_try(r, 1, "RRG_item.bmp", "rrg_item.bmp");

    texEnemy1       = load_tex(r, "enemy.bmp", 1);
    texEnemy1Die    = load_tex(r, "enemy_die.bmp", 1);
    texEnemy1Attack = load_tex(r, "enemy_attack.bmp", 1);

    texEnemy2       = load_tex(r, "enemy2.bmp", 1);
    texEnemy2Die    = load_tex(r, "enemy2_die.bmp", 1);
    texEnemy2Attack = load_tex(r, "enemy2_attack.bmp", 1);

    texMiniboss1       = load_tex(r, "miniboss1.bmp", 1);
    texMiniboss1Die    = load_tex(r, "miniboss1_die.bmp", 1);
    texMiniboss1Attack = load_tex(r, "miniboss1_attack.bmp", 1);

    texFinalboss       = load_tex(r, "finalboss.bmp", 1);
    texFinalbossDie    = load_tex(r, "finalboss_die.bmp", 1);
    texFinalbossAttack = load_tex(r, "finalboss_attack.bmp", 1);

    texGun         = load_tex(r, "gun.bmp", 1);
    texGunRecoil   = load_tex(r, "gun_recoil.bmp", 1);
    texShotgun     = load_tex(r, "shotgun.bmp", 1);
    texShotgunRecoil = load_tex(r, "shotgun_recoil.bmp", 1);
    texSMG         = load_tex(r, "smg.bmp", 1);
    texSMGRecoil   = load_tex(r, "smg_recoil.bmp", 1);
    texPlasma      = load_tex(r, "plasma.bmp", 1);
    texPlasmaRecoil = load_tex(r, "plasma_recoil.bmp", 1);
    texRRG         = load_tex_try(r, 1, "RRG.bmp", "rrg.bmp");
    texRRGRecoil   = load_tex_try(r, 1, "RRG_recoil.bmp", "rrg_recoil.bmp");

    texPlayer       = load_tex(r, "player.bmp", 1);
    texPlayerDamage = load_tex(r, "player_damage.bmp", 1);
    texPlayerDead   = load_tex(r, "player_dead.bmp", 1);
    texGodmod       = load_tex(r, "godmod.bmp", 1);

    /* If enemy attack textures are missing, fall back to normal sprites. */
    if (!texEnemy1Attack) texEnemy1Attack = texEnemy1;
    if (!texEnemy2Attack) texEnemy2Attack = texEnemy2;
    if (!texMiniboss1Attack) texMiniboss1Attack = texMiniboss1;
    if (!texFinalbossAttack) texFinalbossAttack = texFinalboss;

    if (!texEnemy2Die) texEnemy2Die = texEnemy1Die;
    if (!texMiniboss1Die) texMiniboss1Die = texEnemy1Die;
    if (!texFinalbossDie) texFinalbossDie = texEnemy1Die;
}

static int episode_index_for_level(int level)
{
    if (level <= 3) return 0;
    if (level <= 6) return 1;
    return 2;
}

void draw_world(SDL_Renderer *r)
{
    if (!worldmap || worldWidth <= 0 || worldHeight <= 0)
        return;

    int ep = episode_index_for_level(map_current_level);
    SDL_Texture *tWall1 = texWall1_ep[ep];
    SDL_Texture *tWall2 = texWall2_ep[ep];
    SDL_Texture *tFloor = texFloor_ep[ep];
    SDL_Texture *tCeil  = texCeil_ep[ep];

    /* Ray cast each column using a DDA algorithm. */
    for (int sx = 0; sx < W; sx++) {
        /* Compute current ray angle within the player's field of view. */
        float rayAngle = angle - FOV * 0.5f + ((float)sx / (float)W) * FOV;
        float rayDirX = cosf(rayAngle);
        float rayDirY = sinf(rayAngle);

        /* Grid position of the player. */
        int mapX = (int)px;
        int mapY = (int)py;

        /* Calculate the distance the ray has to travel from one x or y-side to the next. */
        float deltaDistX = (rayDirX == 0.0f) ? 1e30f : fabsf(1.0f / rayDirX);
        float deltaDistY = (rayDirY == 0.0f) ? 1e30f : fabsf(1.0f / rayDirY);

        /* Calculate step direction and initial side distance. */
        int stepX;
        int stepY;
        float sideDistX;
        float sideDistY;

        if (rayDirX < 0) {
            stepX = -1;
            sideDistX = (px - (float)mapX) * deltaDistX;
        } else {
            stepX = 1;
            sideDistX = ((float)mapX + 1.0f - px) * deltaDistX;
        }
        if (rayDirY < 0) {
            stepY = -1;
            sideDistY = (py - (float)mapY) * deltaDistY;
        } else {
            stepY = 1;
            sideDistY = ((float)mapY + 1.0f - py) * deltaDistY;
        }

        int hit = 0;
        int side = 0;
        int tile = 0;

        float perpWallDist = MAX_DIST;

        /* Perform DDA: step through the grid until hitting a wall or exceeding max distance. */
        while (!hit) {
            if (sideDistX < sideDistY) {
                sideDistX += deltaDistX;
                mapX += stepX;
                side = 0;
            } else {
                sideDistY += deltaDistY;
                mapY += stepY;
                side = 1;
            }
            /* Check world bounds. */
            if (mapX < 0 || mapY < 0 || mapX >= worldWidth || mapY >= worldHeight) {
                tile = 2;
                hit = 1;
                /* Use MAX_DIST as distance for out-of-bound rays. */
                perpWallDist = MAX_DIST;
                break;
            }
            tile = worldmap[mapY][mapX];
            if (tile >= 2) {
                hit = 1;
                /* Calculate distance projected on camera direction (perpendicular distance) to avoid fish-eye effect. */
                if (side == 0) {
                    perpWallDist = ((float)mapX - px + (1.0f - (float)stepX) * 0.5f) / (rayDirX == 0.0f ? 1e-6f : rayDirX);
                } else {
                    perpWallDist = ((float)mapY - py + (1.0f - (float)stepY) * 0.5f) / (rayDirY == 0.0f ? 1e-6f : rayDirY);
                }
                /* Clamp to avoid extremely small distances. */
                if (perpWallDist <= 0.0f) perpWallDist = 0.001f;
            }
            /* Stop if the distance is beyond maximum draw distance. */
            float approxDist = (side == 0) ? sideDistX - deltaDistX : sideDistY - deltaDistY;
            if (approxDist > MAX_DIST) {
                hit = 1;
                tile = 0;
                break;
            }
        }

        /* Skip rendering if nothing hit or beyond max range. */
        if (tile < 2 || perpWallDist > MAX_DIST) {
            /* Fill entire column with ceiling on top and floor on bottom. */
            int half = H / 2;
            if (tCeil) {
                SDL_RenderCopy(r, tCeil, NULL, &(SDL_Rect){sx, 0, 1, half});
            }
            if (tFloor) {
                SDL_RenderCopy(r, tFloor, NULL, &(SDL_Rect){sx, half, 1, H - half});
            }
            continue;
        }

        /* Calculate height of line to draw on screen. */
        float h = 240.0f / perpWallDist;
        int y1 = (int)(H / 2 - h / 2);
        int y2 = (int)(H / 2 + h / 2);
        if (y1 < 0) y1 = 0;
        if (y2 > H) y2 = H;

        /* Draw ceiling above the wall. */
        if (tCeil && y1 > 0) {
            SDL_RenderCopy(r, tCeil, NULL, &(SDL_Rect){sx, 0, 1, y1});
        }
        /* Draw floor below the wall. */
        if (tFloor && y2 < H) {
            SDL_RenderCopy(r, tFloor, NULL, &(SDL_Rect){sx, y2, 1, H - y2});
        }

        /* Choose texture based on tile type. */
        SDL_Texture *T = (tile == 4) ? tWall2 : (tile == 3) ? texDoor : tWall1;
        if (!T) continue;

        int texW = 0, texH = 0;
        SDL_QueryTexture(T, NULL, NULL, &texW, &texH);

        /* Calculate where exactly the wall was hit. */
        float wallX;
        if (side == 0) {
            wallX = py + perpWallDist * rayDirY;
        } else {
            wallX = px + perpWallDist * rayDirX;
        }
        wallX -= floorf(wallX);
        int texX = (int)(wallX * (float)texW);
        /* Flip the texture coordinate for certain faces to prevent mirroring. */
        if ((side == 0 && rayDirX > 0) || (side == 1 && rayDirY < 0)) {
            texX = texW - texX - 1;
        }
        if (texX < 0) texX = 0;
        if (texX >= texW) texX = texW - 1;

        /* Render a single vertical stripe from the texture. */
        SDL_RenderCopy(r, T,
                       &(SDL_Rect){texX, 0, 1, texH},
                       &(SDL_Rect){sx, y1, 1, y2 - y1});
    }
}

void draw_keys(SDL_Renderer *r)
{
    if (!worldmap || worldWidth <= 0 || worldHeight <= 0 || !texKey)
        return;

    for (int y = 0; y < worldHeight; y++)
    for (int x = 0; x < worldWidth; x++) {
        if (worldmap[y][x] != 1) continue;

        float dx = x + 0.5f - px;
        float dy = y + 0.5f - py;
        float dir = atan2f(dy, dx) - angle;

        while (dir >  (float)M_PI) dir -= 2.0f * (float)M_PI;
        while (dir < -(float)M_PI) dir += 2.0f * (float)M_PI;

        if (fabsf(dir) < FOV/2) {
            float sx = (dir + FOV/2) / FOV * W;
            float size = 80.0f / sqrtf(dx*dx + dy*dy);
            SDL_RenderCopy(r, texKey, NULL,
                           &(SDL_Rect){(int)(sx - size/2), (int)(H/2 - size/2), (int)size, (int)size});
        }
    }
}

static void enemy_tex_for(const Enemy *e, SDL_Texture **out)
{
    SDL_Texture *base = NULL, *die = NULL, *atk = NULL;

    switch (e->kind) {
        case ENEMY_KIND1:
            base = texEnemy1; die = texEnemy1Die; atk = texEnemy1Attack; break;
        case ENEMY_KIND2:
            base = texEnemy2; die = texEnemy2Die; atk = texEnemy2Attack; break;
        case ENEMY_MINIBOSS1:
            base = texMiniboss1; die = texMiniboss1Die; atk = texMiniboss1Attack; break;
        case ENEMY_FINALBOSS:
            base = texFinalboss; die = texFinalbossDie; atk = texFinalbossAttack; break;
        default:
            base = texEnemy1; die = texEnemy1Die; atk = texEnemy1Attack; break;
    }

    if (e->state == ENEMY_DYING) *out = die ? die : base;
    else if (e->state == ENEMY_ALIVE && e->attack_timer > 0.0f) *out = atk ? atk : base;
    else *out = base;
}

static float enemy_sprite_base_size(const Enemy *e)
{
    switch (e->kind) {
        case ENEMY_MINIBOSS1: return 260.0f;
        case ENEMY_FINALBOSS: return 320.0f;
        default: return 160.0f;
    }
}

void draw_enemies(SDL_Renderer *r)
{
    for (int i = 0; i < enemy_count; i++) {
        Enemy *e = &enemies[i];
        if (e->state == ENEMY_DEAD) continue;

        float dx = e->x - px;
        float dy = e->y - py;
        float dir = atan2f(dy, dx) - angle;
        while (dir >  (float)M_PI) dir -= 2.0f * (float)M_PI;
        while (dir < -(float)M_PI) dir += 2.0f * (float)M_PI;

        if (fabsf(dir) >= FOV/2) continue;

        /* Determine projected screen x and distance. */
        float sx = (dir + FOV / 2.0f) / FOV * W;
        float dist = sqrtf(dx * dx + dy * dy);
        if (dist < 0.01f) dist = 0.01f;
        /* Cull enemies that are behind walls. */
        if (!is_visible_to_player(e->x, e->y)) {
            continue;
        }
        float size = enemy_sprite_base_size(e) / dist;

        SDL_Texture *T = NULL;
        enemy_tex_for(e, &T);
        if (!T) continue;

        SDL_RenderCopy(r, T, NULL,
                       &(SDL_Rect){(int)(sx - size/2), (int)(H/2 - size/2), (int)size, (int)size});
    }
}

void draw_hud(SDL_Renderer *r)
{
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderFillRect(r, &(SDL_Rect){0, H - 120, W, 120});

    SDL_Texture *face = texPlayer;
    if (godmode_enabled && texGodmod) face = texGodmod;
    else if (player_dead) face = texPlayerDead;
    else if (player_damage_timer > 0.0f) face = texPlayerDamage;

    if (face) {
        SDL_RenderCopy(r, face, NULL,
                       &(SDL_Rect){W/2 - 56, H - 120/2 - 56, 112, 112});
    }

    SDL_SetRenderDrawColor(r, 200, 0, 0, 255);
    int barw = hp * 2;
    if (barw < 0) barw = 0;
    if (barw > W - 40) barw = W - 40;
    SDL_RenderFillRect(r, &(SDL_Rect){20, H - 100, barw, 24});
}

void draw_gun(SDL_Renderer *r)
{
    SDL_Texture *base = texGun;
    SDL_Texture *recoil = texGunRecoil;

    switch (current_weapon) {
        case WEAPON_SHOTGUN:
            if (texShotgun && texShotgunRecoil) { base = texShotgun; recoil = texShotgunRecoil; }
            break;
        case WEAPON_SMG:
            if (texSMG && texSMGRecoil) { base = texSMG; recoil = texSMGRecoil; }
            break;
        case WEAPON_PLASMA:
            if (texPlasma && texPlasmaRecoil) { base = texPlasma; recoil = texPlasmaRecoil; }
            break;
        case WEAPON_RRG:
            if (texRRG && texRRGRecoil) { base = texRRG; recoil = texRRGRecoil; }
            break;
        default:
            break;
    }

    if (!base) return;

    SDL_RenderCopy(r, (gun_recoil_timer ? recoil : base),
                   NULL, &(SDL_Rect){W/2 - 110, H - 270, 220, 150});
    if (gun_recoil_timer > 0) gun_recoil_timer--;
}

void draw_hitbox(SDL_Renderer *r)
{
    SDL_RenderDrawRect(r, &(SDL_Rect){W/2 - 125, H/2 - 125, 250, 250});
}
