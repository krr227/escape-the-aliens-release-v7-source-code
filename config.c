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

#include "config.h"

static GameConfig g_cfg;

/* ------------------------------------------------------------------------- */
/* Paths / dirs                                                              */
/* ------------------------------------------------------------------------- */

static void build_config_path(char *out, size_t out_sz)
{
    if (!out || out_sz == 0) return;

    char *base = SDL_GetBasePath();
    if (base) {
        snprintf(out, out_sz, "%sDATA/config/config.json", base);
        SDL_free(base);
    } else {
        snprintf(out, out_sz, "DATA/config/config.json");
    }
}

static void ensure_config_dirs(void)
{
    char dir1[512];
    char dir2[512];
    char *base = SDL_GetBasePath();
    if (base) {
        snprintf(dir1, sizeof dir1, "%sDATA", base);
        snprintf(dir2, sizeof dir2, "%sDATA/config", base);
        SDL_free(base);
    } else {
        snprintf(dir1, sizeof dir1, "DATA");
        snprintf(dir2, sizeof dir2, "DATA/config");
    }

    (void)MKDIR(dir1);
    (void)MKDIR(dir2);
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

static int clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int scancode_valid(int v)
{
    return (v >= 0 && v < (int)SDL_NUM_SCANCODES);
}

/* ------------------------------------------------------------------------- */
/* Defaults                                                                  */
/* ------------------------------------------------------------------------- */

void config_set_defaults(GameConfig *cfg)
{
    if (!cfg) return;
    memset(cfg, 0, sizeof *cfg);

    cfg->version = CONFIG_VERSION;
    cfg->fullscreen = 0;
    cfg->mouse_sensitivity = 0.0035f;

    cfg->master_volume = 128;
    cfg->bgm_enabled = 1;
    cfg->bgm_volume = 96;
    cfg->sfx_enabled = 1;
    cfg->sfx_volume = 128;

    cfg->binds[ACTION_MOVE_FORWARD] = SDL_SCANCODE_W;
    cfg->binds[ACTION_MOVE_BACK]    = SDL_SCANCODE_S;
    cfg->binds[ACTION_STRAFE_LEFT]  = SDL_SCANCODE_A;
    cfg->binds[ACTION_STRAFE_RIGHT] = SDL_SCANCODE_D;
    cfg->binds[ACTION_INTERACT]     = SDL_SCANCODE_E;
    cfg->binds[ACTION_PAUSE]        = SDL_SCANCODE_ESCAPE;

    cfg->binds[ACTION_WEAPON_1]     = SDL_SCANCODE_1;
    cfg->binds[ACTION_WEAPON_2]     = SDL_SCANCODE_2;
    cfg->binds[ACTION_WEAPON_3]     = SDL_SCANCODE_3;
    cfg->binds[ACTION_WEAPON_4]     = SDL_SCANCODE_4;
    cfg->binds[ACTION_WEAPON_5]     = SDL_SCANCODE_5;
}

const char *config_action_label(Action a)
{
    switch (a) {
        case ACTION_MOVE_FORWARD: return "MOVE FORWARD";
        case ACTION_MOVE_BACK:    return "MOVE BACK";
        case ACTION_STRAFE_LEFT:  return "STRAFE LEFT";
        case ACTION_STRAFE_RIGHT: return "STRAFE RIGHT";
        case ACTION_INTERACT:     return "INTERACT";
        case ACTION_PAUSE:        return "PAUSE";
        case ACTION_WEAPON_1:     return "WEAPON 1";
        case ACTION_WEAPON_2:     return "WEAPON 2";
        case ACTION_WEAPON_3:     return "WEAPON 3";
        case ACTION_WEAPON_4:     return "WEAPON 4";
        case ACTION_WEAPON_5:     return "WEAPON 5";
        default:                  return "";
    }
}

/* ------------------------------------------------------------------------- */
/* Load / Save                                                               */
/* ------------------------------------------------------------------------- */

static void parse_bind(const char *buf, const char *key, Action a)
{
    int v = 0;
    if (json_get_int(buf, key, &v) && scancode_valid(v)) {
        g_cfg.binds[a] = (SDL_Scancode)v;
    }
}

int config_load_or_create(void)
{
    config_set_defaults(&g_cfg);

    char path[512];
    build_config_path(path, sizeof path);

    size_t len = 0;
    char *buf = read_file(path, &len);
    if (!buf || len == 0) {
        free(buf);
        /* Create default file. */
        (void)config_save();
        return 0;
    }

    int iv = 0;
    float fv = 0.0f;

    if (json_get_int(buf, "fullscreen", &iv)) g_cfg.fullscreen = (iv != 0);
    if (json_get_float(buf, "mouse_sensitivity", &fv)) g_cfg.mouse_sensitivity = fv;

    if (json_get_int(buf, "master_volume", &iv)) g_cfg.master_volume = iv;
    if (json_get_int(buf, "bgm_enabled", &iv)) g_cfg.bgm_enabled = (iv != 0);
    if (json_get_int(buf, "bgm_volume", &iv)) g_cfg.bgm_volume = iv;
    if (json_get_int(buf, "sfx_enabled", &iv)) g_cfg.sfx_enabled = (iv != 0);
    if (json_get_int(buf, "sfx_volume", &iv)) g_cfg.sfx_volume = iv;

    parse_bind(buf, "move_forward", ACTION_MOVE_FORWARD);
    parse_bind(buf, "move_back", ACTION_MOVE_BACK);
    parse_bind(buf, "strafe_left", ACTION_STRAFE_LEFT);
    parse_bind(buf, "strafe_right", ACTION_STRAFE_RIGHT);
    parse_bind(buf, "interact", ACTION_INTERACT);
    parse_bind(buf, "pause", ACTION_PAUSE);

    parse_bind(buf, "weapon1", ACTION_WEAPON_1);
    parse_bind(buf, "weapon2", ACTION_WEAPON_2);
    parse_bind(buf, "weapon3", ACTION_WEAPON_3);
    parse_bind(buf, "weapon4", ACTION_WEAPON_4);
    parse_bind(buf, "weapon5", ACTION_WEAPON_5);

    free(buf);

    /* Validate and clamp. */
    g_cfg.mouse_sensitivity = clampf(g_cfg.mouse_sensitivity, 0.0005f, 0.0200f);
    g_cfg.master_volume = clampi(g_cfg.master_volume, 0, 128);
    g_cfg.bgm_volume = clampi(g_cfg.bgm_volume, 0, 128);
    g_cfg.sfx_volume = clampi(g_cfg.sfx_volume, 0, 128);

    return 0;
}

int config_save(void)
{
    ensure_config_dirs();

    char path[512];
    build_config_path(path, sizeof path);

    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;

    /* Clamp before writing. */
    g_cfg.mouse_sensitivity = clampf(g_cfg.mouse_sensitivity, 0.0005f, 0.0200f);
    g_cfg.master_volume = clampi(g_cfg.master_volume, 0, 128);
    g_cfg.bgm_volume = clampi(g_cfg.bgm_volume, 0, 128);
    g_cfg.sfx_volume = clampi(g_cfg.sfx_volume, 0, 128);

    fprintf(fp, "{\n");
    fprintf(fp, "  \"version\": %d,\n", CONFIG_VERSION);
    fprintf(fp, "  \"fullscreen\": %d,\n", g_cfg.fullscreen ? 1 : 0);
    fprintf(fp, "  \"mouse_sensitivity\": %.6f,\n", g_cfg.mouse_sensitivity);

    fprintf(fp, "  \"master_volume\": %d,\n", g_cfg.master_volume);
    fprintf(fp, "  \"bgm_enabled\": %d,\n", g_cfg.bgm_enabled ? 1 : 0);
    fprintf(fp, "  \"bgm_volume\": %d,\n", g_cfg.bgm_volume);
    fprintf(fp, "  \"sfx_enabled\": %d,\n", g_cfg.sfx_enabled ? 1 : 0);
    fprintf(fp, "  \"sfx_volume\": %d,\n", g_cfg.sfx_volume);

    fprintf(fp, "  \"bindings\": {\n");
    fprintf(fp, "    \"move_forward\": %d,\n", (int)g_cfg.binds[ACTION_MOVE_FORWARD]);
    fprintf(fp, "    \"move_back\": %d,\n", (int)g_cfg.binds[ACTION_MOVE_BACK]);
    fprintf(fp, "    \"strafe_left\": %d,\n", (int)g_cfg.binds[ACTION_STRAFE_LEFT]);
    fprintf(fp, "    \"strafe_right\": %d,\n", (int)g_cfg.binds[ACTION_STRAFE_RIGHT]);
    fprintf(fp, "    \"interact\": %d,\n", (int)g_cfg.binds[ACTION_INTERACT]);
    fprintf(fp, "    \"pause\": %d,\n", (int)g_cfg.binds[ACTION_PAUSE]);
    fprintf(fp, "    \"weapon1\": %d,\n", (int)g_cfg.binds[ACTION_WEAPON_1]);
    fprintf(fp, "    \"weapon2\": %d,\n", (int)g_cfg.binds[ACTION_WEAPON_2]);
    fprintf(fp, "    \"weapon3\": %d,\n", (int)g_cfg.binds[ACTION_WEAPON_3]);
    fprintf(fp, "    \"weapon4\": %d,\n", (int)g_cfg.binds[ACTION_WEAPON_4]);
    fprintf(fp, "    \"weapon5\": %d\n",  (int)g_cfg.binds[ACTION_WEAPON_5]);
    fprintf(fp, "  }\n");
    fprintf(fp, "}\n");

    fclose(fp);
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Accessors                                                                 */
/* ------------------------------------------------------------------------- */

const GameConfig *config_get(void) { return &g_cfg; }
GameConfig *config_mut(void) { return &g_cfg; }

SDL_Scancode config_get_bind(Action a)
{
    if (a < 0 || a >= ACTION_COUNT) return SDL_SCANCODE_UNKNOWN;
    return g_cfg.binds[a];
}

void config_set_bind(Action a, SDL_Scancode sc)
{
    if (a < 0 || a >= ACTION_COUNT) return;
    if (!scancode_valid((int)sc)) return;
    g_cfg.binds[a] = sc;
}

int config_get_fullscreen(void) { return g_cfg.fullscreen ? 1 : 0; }
void config_set_fullscreen(int v) { g_cfg.fullscreen = (v != 0); }

float config_get_mouse_sensitivity(void) { return g_cfg.mouse_sensitivity; }
void config_set_mouse_sensitivity(float v)
{
    g_cfg.mouse_sensitivity = clampf(v, 0.0005f, 0.0200f);
}

int config_get_master_volume(void) { return g_cfg.master_volume; }
void config_set_master_volume(int v) { g_cfg.master_volume = clampi(v, 0, 128); }

int config_get_bgm_enabled(void) { return g_cfg.bgm_enabled ? 1 : 0; }
void config_set_bgm_enabled(int v) { g_cfg.bgm_enabled = (v != 0); }

int config_get_bgm_volume(void) { return g_cfg.bgm_volume; }
void config_set_bgm_volume(int v) { g_cfg.bgm_volume = clampi(v, 0, 128); }

int config_get_sfx_enabled(void) { return g_cfg.sfx_enabled ? 1 : 0; }
void config_set_sfx_enabled(int v) { g_cfg.sfx_enabled = (v != 0); }

int config_get_sfx_volume(void) { return g_cfg.sfx_volume; }
void config_set_sfx_volume(int v) { g_cfg.sfx_volume = clampi(v, 0, 128); }

