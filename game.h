#ifndef GAME_H
#define GAME_H

#include <SDL2/SDL.h>

void game_loop(SDL_Window *win, SDL_Renderer *renderer);

/* HUD message helper (defined in game.c). */
void show_message(const char *text);

#endif
