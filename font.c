#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font.h"

/*
 * Parse a BMFont text file to extract character metrics.  The file
 * format consists of space‑separated key=value pairs on each line.
 * Only the "char" entries are parsed here.  Unknown entries are
 * ignored.  This function expects ASCII characters in the range
 * 0–255 and silently skips any outside this range.  The line height
 * field is updated when the "common" line is encountered.
 */
static int parse_fnt(const char *path, BitmapFont *font)
{
    FILE *fp = fopen(path, "r");
    if (!fp)
        return -1;

    char line[512];
    while (fgets(line, sizeof line, fp)) {
        /* Look for the common line to read the lineHeight */
        if (strncmp(line, "common", 6) == 0) {
            char *p = strstr(line, "lineHeight=");
            if (p) {
                p += strlen("lineHeight=");
                font->lineHeight = atoi(p);
            }
            continue;
        }
        /* Parse char lines: char id=xx x=.. y=.. width=.. height=.. xoffset=.. yoffset=.. xadvance=.. */
        if (strncmp(line, "char ", 5) == 0) {
            int id = -1, x = 0, y = 0, w = 0, h = 0, xo = 0, yo = 0, xa = 0;
            /* The order of the attributes is not guaranteed; scan each key */
            char *token = strtok(line, " ");
            while (token) {
                if (strncmp(token, "id=", 3) == 0)        id = atoi(token + 3);
                else if (strncmp(token, "x=", 2) == 0)     x = atoi(token + 2);
                else if (strncmp(token, "y=", 2) == 0)     y = atoi(token + 2);
                else if (strncmp(token, "width=", 6) == 0)  w = atoi(token + 6);
                else if (strncmp(token, "height=", 7) == 0) h = atoi(token + 7);
                else if (strncmp(token, "xoffset=", 8) == 0) xo = atoi(token + 8);
                else if (strncmp(token, "yoffset=", 8) == 0) yo = atoi(token + 8);
                else if (strncmp(token, "xadvance=", 9) == 0) xa = atoi(token + 9);
                token = strtok(NULL, " ");
            }
            if (id >= 0 && id < 256) {
                font->chars[id].x        = x;
                font->chars[id].y        = y;
                font->chars[id].w        = w;
                font->chars[id].h        = h;
                font->chars[id].xoffset  = xo;
                font->chars[id].yoffset  = yo;
                font->chars[id].xadvance = xa;
            }
        }
    }
    fclose(fp);
    return 0;
}

int load_font(SDL_Renderer *renderer, const char *bmpFile, const char *fntFile, BitmapFont *font)
{
    if (!renderer || !bmpFile || !fntFile || !font)
        return -1;

    memset(font, 0, sizeof *font);

    /* Construct full paths using SDL_GetBasePath to locate the assets.
     * Assets are stored under DATA/ASSETS/ (not a top-level ASSETS/).
     */
    char bmpPath[512];
    char fntPath[512];
    char *base = SDL_GetBasePath();
    if (base) {
        snprintf(bmpPath, sizeof bmpPath, "%sDATA/ASSETS/%s", base, bmpFile);
        snprintf(fntPath, sizeof fntPath, "%sDATA/ASSETS/%s", base, fntFile);
        SDL_free(base);
    } else {
        snprintf(bmpPath, sizeof bmpPath, "DATA/ASSETS/%s", bmpFile);
        snprintf(fntPath, sizeof fntPath, "DATA/ASSETS/%s", fntFile);
    }

    /* Load the bitmap */
    SDL_Surface *surf = SDL_LoadBMP(bmpPath);
    if (!surf)
        return -1;
    /* Use colour key to treat black background as transparent */
    SDL_SetColorKey(surf, SDL_TRUE, SDL_MapRGB(surf->format, 0, 0, 0));
    font->texture = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_FreeSurface(surf);
    if (!font->texture)
        return -1;

    /* Parse the .fnt metrics */
    if (parse_fnt(fntPath, font) != 0) {
        SDL_DestroyTexture(font->texture);
        font->texture = NULL;
        return -1;
    }
    return 0;
}

void draw_text(SDL_Renderer *renderer, BitmapFont *font, int x, int y, const char *text, float scale)
{
    if (!renderer || !font || !font->texture || !text)
        return;

    int penX = x;
    int penY = y;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        unsigned char c = *p;
        if (c == '\n') {
            penX = x;
            penY += (int)(font->lineHeight * scale);
            continue;
        }
        SDL_Rect src = {
            font->chars[c].x,
            font->chars[c].y,
            font->chars[c].w,
            font->chars[c].h
        };
        SDL_Rect dst = {
            penX + (int)(font->chars[c].xoffset * scale),
            penY + (int)(font->chars[c].yoffset * scale),
            (int)(font->chars[c].w * scale),
            (int)(font->chars[c].h * scale)
        };

        /* Some BMFont exports may omit metrics for certain characters
         * (notably space).  Render if we have a non-zero glyph size.
         */
        if (src.w > 0 && src.h > 0)
            SDL_RenderCopy(renderer, font->texture, &src, &dst);

        int adv = font->chars[c].xadvance;
        if (adv <= 0) {
            /* Fallback advance: half a line height works well for pixel fonts. */
            adv = (font->lineHeight > 0) ? (font->lineHeight / 2) : 8;
        }
        penX += (int)(adv * scale);
    }
}

int measure_text(BitmapFont *font, const char *text, float scale)
{
    if (!font || !text)
        return 0;

    int maxW = 0;
    int curW = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        unsigned char c = *p;
        if (c == '\n') {
            if (curW > maxW) maxW = curW;
            curW = 0;
            continue;
        }
        int adv = font->chars[c].xadvance;
        if (adv <= 0)
            adv = (font->lineHeight > 0) ? (font->lineHeight / 2) : 8;
        curW += (int)(adv * scale);
    }
    if (curW > maxW) maxW = curW;
    return maxW;
}