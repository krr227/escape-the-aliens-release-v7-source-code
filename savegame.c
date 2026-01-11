#include <SDL2/SDL.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #include <direct.h>
    #define MKDIR(p) _mkdir(p)
#else
    #include <sys/stat.h>
    #include <sys/types.h>
    #define MKDIR(p) mkdir((p), 0755)
#endif

#include "savegame.h"

/* ------------------------------------------------------------------------- */
/* Paths / directories                                                       */
/* ------------------------------------------------------------------------- */

static void build_save_path(int slot, char *out, size_t out_sz)
{
    if (!out || out_sz == 0) return;
    if (slot < 1) slot = 1;
    if (slot > 3) slot = 3;

    char *base = SDL_GetBasePath();
    if (base) {
        snprintf(out, out_sz, "%sDATA/saves/save%d.json", base, slot);
        SDL_free(base);
    } else {
        snprintf(out, out_sz, "DATA/saves/save%d.json", slot);
    }
}

static void ensure_save_dirs(void)
{
    char dir1[512];
    char dir2[512];

    char *base = SDL_GetBasePath();
    if (base) {
        snprintf(dir1, sizeof dir1, "%sDATA", base);
        snprintf(dir2, sizeof dir2, "%sDATA/saves", base);
        SDL_free(base);
    } else {
        snprintf(dir1, sizeof dir1, "DATA");
        snprintf(dir2, sizeof dir2, "DATA/saves");
    }

    (void)MKDIR(dir1);
    (void)MKDIR(dir2);
}

int savegame_path(int slot, char *out, size_t outsz)
{
    if (!out || outsz == 0) return -1;
    build_save_path(slot, out, outsz);
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Tiny JSON helpers (string search + number parsing).                        */
/* ------------------------------------------------------------------------- */

static int json_find_number(const char *buf, const char *key, const char **out_num)
{
    if (!buf || !key || !out_num) return 0;
    char pat[128];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(buf, pat);
    if (!p) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    *out_num = p;
    return 1;
}

static int json_get_int(const char *buf, const char *key, int *out)
{
    if (!out) return 0;
    const char *p = NULL;
    if (!json_find_number(buf, key, &p)) return 0;
    errno = 0;
    char *end = NULL;
    long v = strtol(p, &end, 10);
    if (end == p || errno != 0) return 0;
    *out = (int)v;
    return 1;
}

static int json_get_float(const char *buf, const char *key, float *out)
{
    if (!out) return 0;
    const char *p = NULL;
    if (!json_find_number(buf, key, &p)) return 0;
    errno = 0;
    char *end = NULL;
    float v = strtof(p, &end);
    if (end == p || errno != 0) return 0;
    *out = v;
    return 1;
}

static int json_parse_int_array(const char *buf, const char *key, int *out, int max)
{
    if (!buf || !key || !out || max <= 0) return 0;
    char pat[128];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(buf, pat);
    if (!p) return 0;
    p = strchr(p, '[');
    if (!p) return 0;
    p++;

    int n = 0;
    while (*p && n < max) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',') p++;
        if (*p == ']') break;
        errno = 0;
        char *end = NULL;
        long v = strtol(p, &end, 10);
        if (end == p || errno != 0) break;
        out[n++] = (int)v;
        p = end;
    }
    return n;
}

static int json_parse_float_array(const char *buf, const char *key, float *out, int max)
{
    if (!buf || !key || !out || max <= 0) return 0;
    char pat[128];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(buf, pat);
    if (!p) return 0;
    p = strchr(p, '[');
    if (!p) return 0;
    p++;

    int n = 0;
    while (*p && n < max) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',') p++;
        if (*p == ']') break;
        errno = 0;
        char *end = NULL;
        float v = strtof(p, &end);
        if (end == p || errno != 0) break;
        out[n++] = v;
        p = end;
    }
    return n;
}

static char *read_file(const char *path, size_t *out_len)
{
    if (out_len) *out_len = 0;
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;

    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    long sz = ftell(fp);
    if (sz < 0) { fclose(fp); return NULL; }
    if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return NULL; }

    char *buf = (char *)malloc((size_t)sz + 1u);
    if (!buf) { fclose(fp); return NULL; }

    size_t n = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    buf[n] = '\0';
    if (out_len) *out_len = n;
    return buf;
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

int savegame_peek(int slot, SaveMeta *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof *out);

    char path[512];
    build_save_path(slot, path, sizeof path);

    size_t len = 0;
    char *buf = read_file(path, &len);
    if (!buf || len == 0) {
        free(buf);
        out->exists = 0;
        return -1;
    }
    out->exists = 1;

    (void)json_get_int(buf, "level", &out->level);
    (void)json_get_int(buf, "hp", &out->hp);

    /* Prefer new ammo pools, but allow old saves. */
    if (!json_get_int(buf, "ammo_bullets", &out->ammo_bullets)) {
        (void)json_get_int(buf, "ammo", &out->ammo_bullets);
    }
    (void)json_get_int(buf, "ammo_shells", &out->ammo_shells);
    (void)json_get_int(buf, "ammo_energy", &out->ammo_energy);

    (void)json_get_int(buf, "hasShotgun", &out->hasShotgun);
    (void)json_get_int(buf, "hasSMG", &out->hasSMG);
    (void)json_get_int(buf, "hasPlasma", &out->hasPlasma);
    (void)json_get_int(buf, "hasRRG", &out->hasRRG);
    (void)json_get_int(buf, "godmode", &out->godmode);

    free(buf);
    return 0;
}

int savegame_write(int slot, const SaveGame *in)
{
    if (!in) return -1;
    if (slot < 1 || slot > 3) return -1;

    ensure_save_dirs();

    char path[512];
    build_save_path(slot, path, sizeof path);

    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;

    fprintf(fp, "{\n");
    fprintf(fp, "  \"version\": %d,\n", (in->version <= 0) ? SAVEGAME_VERSION : in->version);
    fprintf(fp, "  \"level\": %d,\n", in->level);
    fprintf(fp, "  \"px\": %.6f,\n", in->px);
    fprintf(fp, "  \"py\": %.6f,\n", in->py);
    fprintf(fp, "  \"angle\": %.6f,\n", in->angle);
    fprintf(fp, "  \"hp\": %d,\n", in->hp);

    /* Keep old key for backwards compatibility. */
    fprintf(fp, "  \"ammo\": %d,\n", in->ammo_bullets);
    fprintf(fp, "  \"ammo_bullets\": %d,\n", in->ammo_bullets);
    fprintf(fp, "  \"ammo_shells\": %d,\n", in->ammo_shells);
    fprintf(fp, "  \"ammo_energy\": %d,\n", in->ammo_energy);

    fprintf(fp, "  \"hasKey\": %d,\n", in->hasKey);
    fprintf(fp, "  \"hasShotgun\": %d,\n", in->hasShotgun);
    fprintf(fp, "  \"hasSMG\": %d,\n", in->hasSMG);
    fprintf(fp, "  \"hasPlasma\": %d,\n", in->hasPlasma);
    fprintf(fp, "  \"hasRRG\": %d,\n", in->hasRRG);
    fprintf(fp, "  \"weapon\": %d,\n", in->weapon);
    fprintf(fp, "  \"godmode\": %d,\n", in->godmode);

    fprintf(fp, "  \"sens\": %.6f,\n", in->sensitivity);

    fprintf(fp, "  \"enemy_count\": %d,\n", in->enemy_count);

    /* Enemies */
    fprintf(fp, "  \"enemy_x\": [");
    for (int i = 0; i < in->enemy_count && i < MAX_ENEMIES; i++) {
        if (i) fprintf(fp, ", ");
        fprintf(fp, "%.6f", in->enemy_x[i]);
    }
    fprintf(fp, "],\n");

    fprintf(fp, "  \"enemy_y\": [");
    for (int i = 0; i < in->enemy_count && i < MAX_ENEMIES; i++) {
        if (i) fprintf(fp, ", ");
        fprintf(fp, "%.6f", in->enemy_y[i]);
    }
    fprintf(fp, "],\n");

    fprintf(fp, "  \"enemy_kind\": [");
    for (int i = 0; i < in->enemy_count && i < MAX_ENEMIES; i++) {
        if (i) fprintf(fp, ", ");
        fprintf(fp, "%d", in->enemy_kind[i]);
    }
    fprintf(fp, "],\n");

    fprintf(fp, "  \"enemy_state\": [");
    for (int i = 0; i < in->enemy_count && i < MAX_ENEMIES; i++) {
        if (i) fprintf(fp, ", ");
        fprintf(fp, "%d", in->enemy_state[i]);
    }
    fprintf(fp, "],\n");

    fprintf(fp, "  \"enemy_hp\": [");
    for (int i = 0; i < in->enemy_count && i < MAX_ENEMIES; i++) {
        if (i) fprintf(fp, ", ");
        fprintf(fp, "%d", in->enemy_hp[i]);
    }
    fprintf(fp, "],\n");

    fprintf(fp, "  \"enemy_dying_timer\": [");
    for (int i = 0; i < in->enemy_count && i < MAX_ENEMIES; i++) {
        if (i) fprintf(fp, ", ");
        fprintf(fp, "%.6f", in->enemy_dying_timer[i]);
    }
    fprintf(fp, "],\n");

    /* Items */
    fprintf(fp, "  \"item_count\": %d,\n", in->item_count);

    fprintf(fp, "  \"item_x\": [");
    for (int i = 0; i < in->item_count && i < MAX_ITEMS; i++) {
        if (i) fprintf(fp, ", ");
        fprintf(fp, "%.6f", in->item_x[i]);
    }
    fprintf(fp, "],\n");

    fprintf(fp, "  \"item_y\": [");
    for (int i = 0; i < in->item_count && i < MAX_ITEMS; i++) {
        if (i) fprintf(fp, ", ");
        fprintf(fp, "%.6f", in->item_y[i]);
    }
    fprintf(fp, "],\n");

    fprintf(fp, "  \"item_type\": [");
    for (int i = 0; i < in->item_count && i < MAX_ITEMS; i++) {
        if (i) fprintf(fp, ", ");
        fprintf(fp, "%d", in->item_type[i]);
    }
    fprintf(fp, "],\n");

    fprintf(fp, "  \"item_collected\": [");
    for (int i = 0; i < in->item_count && i < MAX_ITEMS; i++) {
        if (i) fprintf(fp, ", ");
        fprintf(fp, "%d", in->item_collected[i]);
    }
    fprintf(fp, "]\n");

    fprintf(fp, "}\n");
    fclose(fp);
    return 0;
}

int savegame_read(int slot, SaveGame *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof *out);

    char path[512];
    build_save_path(slot, path, sizeof path);

    size_t len = 0;
    char *buf = read_file(path, &len);
    if (!buf || len == 0) {
        free(buf);
        return -1;
    }

    /* Defaults (important for old saves). */
    out->version = 1;
    out->level = 1;
    out->px = 3.0f; out->py = 3.0f; out->angle = 0.0f;
    out->hp = 100;
    out->ammo_bullets = 10;
    out->ammo_shells = 0;
    out->ammo_energy = 0;
    out->hasKey = 0;
    out->hasShotgun = 0;
    out->hasSMG = 0;
    out->hasPlasma = 0;
    out->hasRRG = 0;
    out->weapon = 0;
    out->godmode = 0;
    out->sensitivity = 0.0035f;

    (void)json_get_int(buf, "version", &out->version);
    (void)json_get_int(buf, "level", &out->level);
    (void)json_get_float(buf, "px", &out->px);
    (void)json_get_float(buf, "py", &out->py);
    (void)json_get_float(buf, "angle", &out->angle);
    (void)json_get_int(buf, "hp", &out->hp);

    /* Ammo pools */
    if (!json_get_int(buf, "ammo_bullets", &out->ammo_bullets)) {
        (void)json_get_int(buf, "ammo", &out->ammo_bullets);
    }
    (void)json_get_int(buf, "ammo_shells", &out->ammo_shells);
    (void)json_get_int(buf, "ammo_energy", &out->ammo_energy);

    (void)json_get_int(buf, "hasKey", &out->hasKey);
    (void)json_get_int(buf, "hasShotgun", &out->hasShotgun);
    (void)json_get_int(buf, "hasSMG", &out->hasSMG);
    (void)json_get_int(buf, "hasPlasma", &out->hasPlasma);
    (void)json_get_int(buf, "hasRRG", &out->hasRRG);
    (void)json_get_int(buf, "weapon", &out->weapon);
    (void)json_get_int(buf, "godmode", &out->godmode);

    (void)json_get_float(buf, "sens", &out->sensitivity);

    (void)json_get_int(buf, "enemy_count", &out->enemy_count);
    if (out->enemy_count < 0) out->enemy_count = 0;
    if (out->enemy_count > MAX_ENEMIES) out->enemy_count = MAX_ENEMIES;

    json_parse_float_array(buf, "enemy_x", out->enemy_x, out->enemy_count);
    json_parse_float_array(buf, "enemy_y", out->enemy_y, out->enemy_count);

    int got_kind = json_parse_int_array(buf, "enemy_kind", out->enemy_kind, out->enemy_count);
    if (got_kind <= 0) {
        for (int i = 0; i < out->enemy_count; i++) out->enemy_kind[i] = 0;
    }

    json_parse_int_array(buf, "enemy_state", out->enemy_state, out->enemy_count);
    json_parse_int_array(buf, "enemy_hp", out->enemy_hp, out->enemy_count);
    json_parse_float_array(buf, "enemy_dying_timer", out->enemy_dying_timer, out->enemy_count);

    (void)json_get_int(buf, "item_count", &out->item_count);
    if (out->item_count < 0) out->item_count = 0;
    if (out->item_count > MAX_ITEMS) out->item_count = MAX_ITEMS;

    json_parse_float_array(buf, "item_x", out->item_x, out->item_count);
    json_parse_float_array(buf, "item_y", out->item_y, out->item_count);
    json_parse_int_array(buf, "item_type", out->item_type, out->item_count);
    json_parse_int_array(buf, "item_collected", out->item_collected, out->item_count);

    free(buf);
    return 0;
}
