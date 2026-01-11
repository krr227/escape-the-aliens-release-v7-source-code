#ifndef SAVEGAME_H
#define SAVEGAME_H

#include <stddef.h>
#include "enemy.h"
#include "items.h"

/* Save file version. Increment when new fields are added. */
#define SAVEGAME_VERSION 3

typedef struct {
    int exists;
    int level;
    int hp;
    int ammo_bullets;
    int ammo_shells;
    int ammo_energy;
    int hasShotgun;
    int hasSMG;
    int hasPlasma;
    int hasRRG;
    int godmode;
} SaveMeta;

typedef struct {
    int version;
    int level;

    float px, py, angle;
    int hp;

    /* Ammo pools */
    int ammo_bullets;
    int ammo_shells;
    int ammo_energy;

    /* Key + weapons */
    int hasKey;
    int hasShotgun;
    int hasSMG;
    int hasPlasma;
    int hasRRG;
    int weapon; /* WeaponType */

    int godmode;

    float sensitivity;

    /* Enemies */
    int enemy_count;
    float enemy_x[MAX_ENEMIES];
    float enemy_y[MAX_ENEMIES];
    int enemy_kind[MAX_ENEMIES];
    int enemy_state[MAX_ENEMIES];
    int enemy_hp[MAX_ENEMIES];
    float enemy_dying_timer[MAX_ENEMIES];

    /* Items */
    int item_count;
    float item_x[MAX_ITEMS];
    float item_y[MAX_ITEMS];
    int item_type[MAX_ITEMS];
    int item_collected[MAX_ITEMS];
} SaveGame;

int savegame_path(int slot, char *out, size_t outsz);
int savegame_write(int slot, const SaveGame *g);
int savegame_read(int slot, SaveGame *out);
int savegame_peek(int slot, SaveMeta *out);

#endif /* SAVEGAME_H */
