#ifndef ITEMS_H
#define ITEMS_H

#include <SDL2/SDL.h>

/*
 * Collectible items.
 * The numeric values correspond to tile encodings in map files.
 */
typedef enum {
    ITEM_BULLETS     = 5,
    ITEM_MEDKIT      = 6,
    ITEM_SHOTGUN     = 7,

    ITEM_SMG         = 11,
    ITEM_SHELLS      = 14,
    ITEM_ENERGY      = 15,
    ITEM_PLASMA      = 16,
    ITEM_RRG         = 17
} ItemType;

typedef struct {
    float x;
    float y;
    ItemType type;
    int collected;
} Item;

#define MAX_ITEMS 96

extern Item items[MAX_ITEMS];
extern int item_count;

void init_items(void);
void update_items(void);
void draw_items(SDL_Renderer *renderer);

#endif /* ITEMS_H */
