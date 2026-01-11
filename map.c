#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "map.h"

/*
 * The world map is a dynamically allocated 2D array. It is implemented as an
 * array of pointers to rows for convenient indexing.
 */
int **worldmap = NULL;
int worldWidth = 0;
int worldHeight = 0;
float player_spawn_x = 1.5f;
float player_spawn_y = 1.5f;
int map_current_level = 0;

void free_map(void)
{
    if (worldmap) {
        for (int y = 0; y < worldHeight; y++)
            free(worldmap[y]);
        free(worldmap);
        worldmap = NULL;
    }
    worldWidth = 0;
    worldHeight = 0;
    map_current_level = 0;
}

static void map_path(int level, char *out, size_t outsz)
{
    if (!out || outsz == 0) return;
    if (level < 1) level = 1;
    if (level > 9) level = 9;

    char name[32];
    snprintf(name, sizeof name, "map%d.txt", level);

    char *base = SDL_GetBasePath();
    if (base) {
        snprintf(out, outsz, "%sDATA/maps/%s", base, name);
        SDL_free(base);
    } else {
        snprintf(out, outsz, "DATA/maps/%s", name);
    }
}

/* Parse the next integer token from *p and advance *p.
 * Returns 1 on success, 0 if no more tokens on the line.
 * On parse failure, returns 1 and sets *out to a safe default.
 */
static int parse_next_int(char **p, int *out)
{
    if (!p || !*p || !out)
        return 0;

    while (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n')
        (*p)++;

    if (**p == '\0')
        return 0;

    errno = 0;
    char *end = NULL;
    long v = strtol(*p, &end, 10);
    if (end == *p) {
        /* Not a number: consume until next whitespace and substitute a wall. */
        while (**p && **p != ' ' && **p != '\t' && **p != '\r' && **p != '\n')
            (*p)++;
        *out = 2;
        return 1;
    }

    /* Allow a wider tile id range so we can extend the game without rewriting maps. */
    if (errno != 0 || v < 0 || v > 99) {
        *out = 2;
    } else {
        *out = (int)v;
    }

    *p = end;
    return 1;
}

int load_map(int level)
{
    free_map();

    /* Reset spawn defaults so a malformed map can't inherit old values. */
    player_spawn_x = 1.5f;
    player_spawn_y = 1.5f;

    char path[512];
    map_path(level, path, sizeof path);

    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "Failed to load map: %s\n", path);
        return -1;
    }

    /* First pass: determine width/height. */
    char line[2048];
    worldWidth = 0;
    worldHeight = 0;

    while (fgets(line, sizeof line, fp)) {
        char *p = line;
        while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
        if (!*p) continue;

        int cols = 0;
        char *scan = p;
        int tmp;
        while (parse_next_int(&scan, &tmp))
            cols++;
        worldWidth = cols;
        break;
    }

    fseek(fp, 0, SEEK_SET);
    while (fgets(line, sizeof line, fp)) {
        char *p = line;
        while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
        if (*p)
            worldHeight++;
    }

    if (worldWidth <= 0 || worldHeight <= 0) {
        fclose(fp);
        return -1;
    }

    worldmap = (int **)calloc((size_t)worldHeight, sizeof(int *));
    if (!worldmap) {
        fclose(fp);
        return -1;
    }
    for (int y = 0; y < worldHeight; y++) {
        worldmap[y] = (int *)calloc((size_t)worldWidth, sizeof(int));
        if (!worldmap[y]) {
            fclose(fp);
            free_map();
            return -1;
        }
    }

    fseek(fp, 0, SEEK_SET);
    int y = 0;
    while (fgets(line, sizeof line, fp) && y < worldHeight) {
        char *p = line;
        while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
        if (!*p) continue;

        for (int x = 0; x < worldWidth; x++) {
            int v = 2; /* default wall */
            (void)parse_next_int(&p, &v);
            worldmap[y][x] = v;

            if (v == 8) {
                player_spawn_x = x + 0.5f;
                player_spawn_y = y + 0.5f;
                worldmap[y][x] = 0;
            }
        }
        y++;
    }

    for (; y < worldHeight; y++) {
        for (int x = 0; x < worldWidth; x++)
            worldmap[y][x] = 2;
    }

    fclose(fp);
    map_current_level = (level < 1) ? 1 : (level > 9) ? 9 : level;
    return 0;
}
