#ifndef RENDER_H
#define RENDER_H

#include <SDL2/SDL.h>

/* Screen size. Keep in sync with the window creation in main.c. */
#define W 800
#define H 600

/* Horizontal field of view in radians. */
#define FOV 0.6f

/* Episode wall/floor/ceiling textures (0=EP1,1=EP2,2=EP3). */
extern SDL_Texture *texWall1_ep[3];
extern SDL_Texture *texWall2_ep[3];
extern SDL_Texture *texFloor_ep[3];
extern SDL_Texture *texCeil_ep[3];
extern SDL_Texture *texDoor;

extern SDL_Texture *texKey;

/* UI */
extern SDL_Texture *texMenu;
extern SDL_Texture *texCutscene[9]; /* 1..8 used */
extern SDL_Texture *texEnding;

/* Items */
extern SDL_Texture *texAmmo;
extern SDL_Texture *texMedkit;
extern SDL_Texture *texShotgunItem;
extern SDL_Texture *texSMGItem;
extern SDL_Texture *texShells;
extern SDL_Texture *texEnergy;
extern SDL_Texture *texPlasmaItem;
extern SDL_Texture *texRRGItem;

/* Enemies */
extern SDL_Texture *texEnemy1;
extern SDL_Texture *texEnemy1Die;
extern SDL_Texture *texEnemy1Attack;

extern SDL_Texture *texEnemy2;
extern SDL_Texture *texEnemy2Die;
extern SDL_Texture *texEnemy2Attack;

extern SDL_Texture *texMiniboss1;
extern SDL_Texture *texMiniboss1Die;
extern SDL_Texture *texMiniboss1Attack;

extern SDL_Texture *texFinalboss;
extern SDL_Texture *texFinalbossDie;
extern SDL_Texture *texFinalbossAttack;

/* Weapons */
extern SDL_Texture *texGun;
extern SDL_Texture *texGunRecoil;
extern SDL_Texture *texShotgun;
extern SDL_Texture *texShotgunRecoil;
extern SDL_Texture *texSMG;
extern SDL_Texture *texSMGRecoil;
extern SDL_Texture *texPlasma;
extern SDL_Texture *texPlasmaRecoil;
extern SDL_Texture *texRRG;
extern SDL_Texture *texRRGRecoil;

/* Player faces */
extern SDL_Texture *texPlayer;
extern SDL_Texture *texPlayerDamage;
extern SDL_Texture *texPlayerDead;
extern SDL_Texture *texGodmod;

void load_textures(SDL_Renderer *r);

void draw_world(SDL_Renderer *r);
void draw_keys(SDL_Renderer *r);
void draw_enemies(SDL_Renderer *r);
void draw_items(SDL_Renderer *renderer); /* from items.c, but renderer calls it */
void draw_hud(SDL_Renderer *r);
void draw_gun(SDL_Renderer *r);
void draw_hitbox(SDL_Renderer *r);

#endif /* RENDER_H */
