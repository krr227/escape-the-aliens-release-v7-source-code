#ifndef FONT_H
#define FONT_H

/*
 * Bitmap font loading and rendering.
 *
 * The game uses two BMFont bitmap fonts: one for general text (pixel) and
 * another for numbers.  The `.fnt` file describes where each character
 * resides within the `.bmp` image.  At load time the parser stores
 * character metrics for quick lookup.  When drawing text the renderer
 * copies the appropriate slice of the texture onto the screen at the
 * requested position, applying an optional scale factor.  All positions
 * are integers so there is no sub‑pixel rendering.
 */

#include <SDL2/SDL.h>

typedef struct {
    SDL_Texture *texture;           /* spritesheet containing all glyphs */
    int lineHeight;                 /* distance between baselines */
    struct {
        int x;
        int y;
        int w;
        int h;
        int xoffset;
        int yoffset;
        int xadvance;
    } chars[256];                   /* metrics indexed by ASCII code */
} BitmapFont;

/*
 * Load a BMFont.  The bmpFile and fntFile parameters are relative
 * paths under the ASSETS directory (e.g. "pixel.bmp" and
 * "pixel.fnt").  The renderer argument is used to create the
 * underlying texture.  Returns 0 on success and -1 on failure.
 */
int load_font(SDL_Renderer *renderer, const char *bmpFile, const char *fntFile, BitmapFont *font);

/*
 * Draw a UTF‑8 string using the provided font.  The x and y arguments
 * specify the top left position where the first character will be
 * drawn.  The scale factor allows the caller to enlarge the glyphs; a
 * value of 1 draws the glyphs at native size.  The function does not
 * perform clipping – the caller must ensure the text fits on screen.
 */
void draw_text(SDL_Renderer *renderer, BitmapFont *font, int x, int y, const char *text, float scale);

/*
 * Measure the width in pixels that a string would occupy when drawn.
 * Newlines are treated as line breaks; the returned width is the
 * maximum width of any line.
 */
int measure_text(BitmapFont *font, const char *text, float scale);

#endif /* FONT_H */