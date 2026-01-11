CC = gcc

CFLAGS = -std=c99 -O2 -Wall -Wextra \
         -IC:/msys64/mingw64/include/SDL2 \
         -Dmain=SDL_main

LDFLAGS = -static -static-libgcc

# Linker flags.  The order of libraries matters on Windows/MinGW; in
# particular, `-lmingw32` must appear before `-lSDL2main` when
# statically linking against SDL2 on Windows.  Without this the linker
# will complain about a missing `WinMain` entry point.
LIBS = \
    -LC:/msys64/mingw64/lib \
    -lmingw32 \
    -lSDL2main -lSDL2 \
    -lwinmm -lgdi32 -luser32 -lkernel32 \
    -lole32 -loleaut32 -limm32 -lsetupapi -lversion -lm

TARGET = game.exe
TARGET_GUI = game_gui.exe

BUILD_DIR = build

SRC = \
    main.c \
    game.c \
    render.c \
    player.c \
    enemy.c \
    map.c \
    items.c \
    audio.c \
    font.c \
    savegame.c \
    config.c

OBJ = $(addprefix $(BUILD_DIR)/, $(SRC:.c=.o))

all: $(TARGET) $(TARGET_GUI)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS) $(LIBS)

$(TARGET_GUI): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS) -mwindows $(LIBS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(TARGET) $(TARGET_GUI) \
	    *.exe *.dll *.a *.lib \
	    *.pdb *.ilk *.map *.d core core.*
