#include <math.h>
#include <stdio.h>

#include "items.h"
#include "map.h"
#include "render.h"
#include "player.h"
#include "audio.h"
#include "game.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

Item items[MAX_ITEMS];
int item_count = 0;

/* Texture pointers live in render.c (declared in render.h). */

static int is_item_tile(int v)
{
    return (v == ITEM_BULLETS || v == ITEM_MEDKIT || v == ITEM_SHOTGUN ||
            v == ITEM_SMG || v == ITEM_SHELLS || v == ITEM_ENERGY ||
            v == ITEM_PLASMA || v == ITEM_RRG);
}

void init_items(void)
{
    item_count = 0;
    if (!worldmap) return;

    for (int y = 0; y < worldHeight; y++) {
        for (int x = 0; x < worldWidth; x++) {
            int v = worldmap[y][x];
            if (is_item_tile(v) && item_count < MAX_ITEMS) {
                items[item_count].x = x + 0.5f;
                items[item_count].y = y + 0.5f;
                items[item_count].type = (ItemType)v;
                items[item_count].collected = 0;
                worldmap[y][x] = 0;
                item_count++;
            }
        }
    }
}

void update_items(void)
{
    for (int i = 0; i < item_count; i++) {
        Item *it = &items[i];
        if (it->collected) continue;

        float dx = it->x - px;
        float dy = it->y - py;
        float dist = sqrtf(dx * dx + dy * dy);
        if (dist >= 0.7f) continue;

        audio_play_sfx(SFX_ITEM);

        switch (it->type) {
            case ITEM_BULLETS:
                ammo_bullets += 3;
                if (ammo_bullets > 999) ammo_bullets = 999;
                show_message("BULLETS +3");
                break;

            case ITEM_MEDKIT:
                hp += 10;
                if (hp > 100) hp = 100;
                show_message("MEDKIT +10");
                break;

            case ITEM_SHOTGUN:
                hasShotgun = 1;
                current_weapon = WEAPON_SHOTGUN;
                show_message("SHOTGUN ACQUIRED");
                break;

            case ITEM_SMG:
                hasSMG = 1;
                current_weapon = WEAPON_SMG;
                show_message("SMG ACQUIRED");
                break;

            case ITEM_SHELLS:
                ammo_shells += 4;
                if (ammo_shells > 999) ammo_shells = 999;
                show_message("SHELLS +4");
                break;

            case ITEM_ENERGY:
                ammo_energy += 10;
                if (ammo_energy > 999) ammo_energy = 999;
                show_message("ENERGY +10");
                break;

            case ITEM_PLASMA:
                hasPlasma = 1;
                current_weapon = WEAPON_PLASMA;
                show_message("PLASMA ACQUIRED");
                break;

            case ITEM_RRG:
                hasRRG = 1;
                current_weapon = WEAPON_RRG;
                show_message("RRG ACQUIRED");
                break;
        }

        it->collected = 1;
    }
}

void draw_items(SDL_Renderer *renderer)
{
    if (!renderer) return;

    for (int i = 0; i < item_count; i++) {
        Item *it = &items[i];
        if (it->collected) continue;

        float dx = it->x - px;
        float dy = it->y - py;
        float dir = atan2f(dy, dx) - angle;
        while (dir >  M_PI) dir -= 2 * (float)M_PI;
        while (dir < -M_PI) dir += 2 * (float)M_PI;

        if (fabsf(dir) >= FOV / 2) continue;

        float sx = (dir + FOV / 2) / FOV * W;
        float size = 80.0f / sqrtf(dx * dx + dy * dy);

        /* Cull items that are behind walls by performing a simple visibility test.
         * March a ray from the player to the item and see if a wall is encountered
         * before reaching the item's tile. This is similar to the function in
         * render.c but duplicated here to avoid cross-file dependencies. */
        {
            float tx = it->x;
            float ty = it->y;
            float odx = tx - px;
            float ody = ty - py;
            float odist = sqrtf(odx * odx + ody * ody);
            int occluded = 0;
            if (odist > 0.0f) {
                float invD = 1.0f / odist;
                float vx = odx * invD;
                float vy = ody * invD;
                float t = 0.0f;
                const float step = 0.05f;
                while (t < odist) {
                    float cx = px + vx * t;
                    float cy = py + vy * t;
                    int mx = (int)cx;
                    int my = (int)cy;
                    if (mx < 0 || my < 0 || mx >= worldWidth || my >= worldHeight) {
                        occluded = 1;
                        break;
                    }
                    if (mx == (int)tx && my == (int)ty) {
                        break;
                    }
                    if (worldmap[my][mx] >= 2) {
                        occluded = 1;
                        break;
                    }
                    t += step;
                }
            }
            if (occluded) {
                continue;
            }
        }

        SDL_Texture *tex = NULL;
        switch (it->type) {
            case ITEM_BULLETS: tex = texAmmo; break;
            case ITEM_MEDKIT:  tex = texMedkit; break;
            case ITEM_SHOTGUN: tex = texShotgunItem; break;
            case ITEM_SMG:     tex = texSMGItem; break;
            case ITEM_SHELLS:  tex = texShells; break;
            case ITEM_ENERGY:  tex = texEnergy; break;
            case ITEM_PLASMA:  tex = texPlasmaItem; break;
            case ITEM_RRG:     tex = texRRGItem; break;
        }
        if (!tex) continue;

        SDL_RenderCopy(renderer, tex, NULL,
                       &(SDL_Rect){(int)(sx - size / 2), (int)(H / 2 - size / 2), (int)size, (int)size});
    }
}
