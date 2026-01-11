#ifndef MAP_H
#define MAP_H

#include <SDL2/SDL.h>

/*
 * Dynamic world map system.
 *
 * Map files live under DATA/maps/ as:
 *   map1.txt .. map9.txt
 *
 * Each file is whitespace-separated integers.
 *
 * Tile encodings:
 * 0  – empty floor (walkable)
 * 1  – key (collect with E)
 * 2  – wall (solid, wall1)
 * 3  – exit door (requires key + boss defeated; triggers level transition)
 * 4  – alternate wall (solid, wall2)
 * 5  – bullets pickup (adds 3 bullets)
 * 6  – medkit pickup (adds 10 HP)
 * 7  – shotgun pickup (weapon)
 * 8  – player spawn (starting position; treated as floor)
 * 9  – enemy spawn (enemy1)
 * 10 – enemy2 spawn (faster attack)
 * 11 – SMG pickup (weapon)
 * 12 – miniboss spawn (30 HP)
 * 13 – final boss spawn (60 HP)
 * 14 – shells pickup (adds 4 shells)
 * 15 – energy pickup (adds 10 energy)
 * 16 – plasma pickup (weapon)
 * 17 – RRG pickup (weapon)
 */

extern int **worldmap;
extern int worldWidth;
extern int worldHeight;
extern float player_spawn_x;
extern float player_spawn_y;

/* Last successfully loaded level number (1..9). */
extern int map_current_level;

void free_map(void);

/* Load map for the given level (1–9). Returns 0 on success, -1 on failure. */
int load_map(int level);

#endif /* MAP_H */
