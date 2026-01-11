#ifndef ENEMY_H
#define ENEMY_H

#define MAX_ENEMIES 64

typedef enum {
    ENEMY_ALIVE = 0,
    ENEMY_DYING = 1,
    ENEMY_DEAD  = 2
} EnemyState;

typedef enum {
    ENEMY_KIND1 = 0,
    ENEMY_KIND2 = 1,
    ENEMY_MINIBOSS1 = 2,
    ENEMY_FINALBOSS = 3
} EnemyKind;

typedef struct {
    float x, y;
    EnemyState state;
    EnemyKind kind;
    int hp;

    float touch_cooldown;   /* seconds until next melee hit */
    float dying_timer;      /* seconds remaining in dying animation */
    float attack_timer;     /* seconds remaining to display attack sprite */
} Enemy;

extern Enemy enemies[MAX_ENEMIES];
extern int enemy_count;

void init_enemies(void);
void update_enemies(float dt);
void damage_enemy(int i, int dmg);

/* Returns 1 if any miniboss/final boss is still alive on this map. */
int enemy_boss_alive(void);

#endif /* ENEMY_H */
