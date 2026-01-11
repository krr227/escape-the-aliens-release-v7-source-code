#ifndef PLAYER_H
#define PLAYER_H

/* Position / view */
extern float px;
extern float py;
extern float angle;

/* Combat */
extern int gun_recoil_timer;
extern int shot_fired;

/* Weapons */
typedef enum {
    WEAPON_PISTOL = 0,
    WEAPON_SHOTGUN = 1,
    WEAPON_SMG = 2,
    WEAPON_PLASMA = 3,
    WEAPON_RRG = 4
} WeaponType;

extern int hasShotgun;
extern int hasSMG;
extern int hasPlasma;
extern int hasRRG;
extern WeaponType current_weapon;

/* Options */
extern float mouse_sensitivity; /* radians per pixel */

/* Game flags */
extern int hasKey;
extern int escaped;

/* God mode */
extern int godmode_enabled;

/* Stats */
extern int hp;
extern int ammo_bullets;
extern int ammo_shells;
extern int ammo_energy;

/* Damage / death effects */
extern float player_damage_timer;
extern int player_dead;

void init_player(void);
void update_player(float dt);

#endif /* PLAYER_H */
