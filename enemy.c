#include <math.h>

#include "enemy.h"
#include "player.h"
#include "map.h"
#include "audio.h"

Enemy enemies[MAX_ENEMIES];
int enemy_count = 0;

static int hp_for_kind(EnemyKind k)
{
    switch (k) {
        case ENEMY_KIND1: return 2;      /* pistol needs 2 hits */
        case ENEMY_KIND2: return 2;      /* same HP, faster attacks */
        case ENEMY_MINIBOSS1: return 30;
        case ENEMY_FINALBOSS: return 60;
        default: return 2;
    }
}

static float move_speed_for_kind(EnemyKind k)
{
    switch (k) {
        case ENEMY_KIND1: return 0.60f;
        case ENEMY_KIND2: return 0.70f;
        case ENEMY_MINIBOSS1: return 0.50f;
        case ENEMY_FINALBOSS: return 0.45f;
        default: return 0.60f;
    }
}

static float attack_cooldown_for_kind(EnemyKind k)
{
    switch (k) {
        case ENEMY_KIND1: return 1.00f;
        case ENEMY_KIND2: return 0.60f;
        case ENEMY_MINIBOSS1: return 0.85f;
        case ENEMY_FINALBOSS: return 0.70f;
        default: return 1.00f;
    }
}

static int attack_damage_for_kind(EnemyKind k)
{
    switch (k) {
        case ENEMY_KIND1: return 10;
        case ENEMY_KIND2: return 10;
        case ENEMY_MINIBOSS1: return 15;
        case ENEMY_FINALBOSS: return 20;
        default: return 10;
    }
}

static float attack_range_for_kind(EnemyKind k)
{
    switch (k) {
        case ENEMY_MINIBOSS1: return 0.65f;
        case ENEMY_FINALBOSS: return 0.70f;
        default: return 0.50f;
    }
}

void init_enemies(void)
{
    enemy_count = 0;
    if (!worldmap || worldWidth <= 0 || worldHeight <= 0)
        return;

    for (int y = 0; y < worldHeight; y++) {
        for (int x = 0; x < worldWidth; x++) {
            int t = worldmap[y][x];
            EnemyKind kind;
            int spawn = 1;

            if (t == 9) kind = ENEMY_KIND1;
            else if (t == 10) kind = ENEMY_KIND2;
            else if (t == 12) kind = ENEMY_MINIBOSS1;
            else if (t == 13) kind = ENEMY_FINALBOSS;
            else spawn = 0;

            if (spawn && enemy_count < MAX_ENEMIES) {
                enemies[enemy_count++] = (Enemy){
                    .x = x + 0.5f,
                    .y = y + 0.5f,
                    .state = ENEMY_ALIVE,
                    .kind = kind,
                    .hp = hp_for_kind(kind),
                    .touch_cooldown = 0.0f,
                    .dying_timer = 0.0f,
                    .attack_timer = 0.0f
                };
                worldmap[y][x] = 0;
            }
        }
    }
}

void damage_enemy(int i, int dmg)
{
    if (i < 0 || i >= enemy_count) return;
    if (dmg <= 0) return;

    Enemy *e = &enemies[i];
    if (e->state != ENEMY_ALIVE) return;

    e->hp -= dmg;
    if (e->hp <= 0) {
        e->state = ENEMY_DYING;
        e->dying_timer = (e->kind == ENEMY_MINIBOSS1 || e->kind == ENEMY_FINALBOSS) ? 0.85f : 0.45f;
        e->attack_timer = 0.0f;
        audio_play_sfx(SFX_ENEMY_DIE);
    }
}

int enemy_boss_alive(void)
{
    for (int i = 0; i < enemy_count; i++) {
        if (enemies[i].state != ENEMY_ALIVE) continue;
        if (enemies[i].kind == ENEMY_MINIBOSS1 || enemies[i].kind == ENEMY_FINALBOSS)
            return 1;
    }
    return 0;
}

void update_enemies(float dt)
{
    for (int i = 0; i < enemy_count; i++) {
        Enemy *e = &enemies[i];
        if (e->state == ENEMY_DEAD) continue;

        if (e->attack_timer > 0.0f) {
            e->attack_timer -= dt;
            if (e->attack_timer < 0.0f) e->attack_timer = 0.0f;
        }

        if (e->state == ENEMY_DYING) {
            if (e->dying_timer > 0.0f)
                e->dying_timer -= dt;
            if (e->dying_timer <= 0.0f)
                e->state = ENEMY_DEAD;
            continue;
        }

        /* Simple chase movement (no wall collision). */
        float dx = px - e->x;
        float dy = py - e->y;
        float dist = sqrtf(dx * dx + dy * dy);

        float speed = move_speed_for_kind(e->kind);
        if (dist > 0.01f) {
            float stop = attack_range_for_kind(e->kind) * 0.9f;
            if (dist > stop) {
                /* Proposed new position towards player. */
                float inv = 1.0f / dist;
                float mv = dt * speed;
                float nx = e->x + dx * inv * mv;
                float ny = e->y + dy * inv * mv;

                /* Prevent enemies from walking through walls by checking collisions.
                 * We update X and Y separately to allow sliding along walls. */
                if (worldmap && worldWidth > 0 && worldHeight > 0) {
                    int curY = (int)e->y;
                    int curX = (int)e->x;
                    /* Attempt to move along X axis if next tile is free. */
                    int mx = (int)nx;
                    if (mx >= 0 && mx < worldWidth && curY >= 0 && curY < worldHeight && worldmap[curY][mx] < 2) {
                        e->x = nx;
                    }
                    /* Attempt to move along Y axis if next tile is free. */
                    int my = (int)ny;
                    mx = (int)e->x; /* use potentially updated x coordinate */
                    if (mx >= 0 && mx < worldWidth && my >= 0 && my < worldHeight && worldmap[my][mx] < 2) {
                        e->y = ny;
                    }
                } else {
                    /* Fallback if map is invalid. */
                    e->x = nx;
                    e->y = ny;
                }
            }
        }

        if (e->touch_cooldown > 0.0f) {
            e->touch_cooldown -= dt;
            if (e->touch_cooldown < 0.0f) e->touch_cooldown = 0.0f;
        }

        float range = attack_range_for_kind(e->kind);
        if (!player_dead && dist < range && e->touch_cooldown <= 0.0f) {
            e->touch_cooldown = attack_cooldown_for_kind(e->kind);
            e->attack_timer = 0.22f;

            if (!godmode_enabled) {
                hp -= attack_damage_for_kind(e->kind);
                player_damage_timer = 0.30f;
                if (hp <= 0) {
                    hp = 0;
                    player_dead = 1;
                }
            }
        }
    }
}
