# Build the game core natively, for tuning on the Mac.
#   make && ./sim            play at rung 2 (single letters)
#   make && ./sim -r 3       chords
#   make && ./sim -a         headless smoke run, prints one frame and the stats

CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -I../include
SRC = sim.c ../src/fb.c ../src/font5x7.c ../src/engine.c ../src/game_drift.c ../src/game_catch.c ../src/game_balance.c

sim: $(SRC) ../include/totem_game.h
	$(CC) $(CFLAGS) -o $@ $(SRC)

clean:
	rm -f sim

.PHONY: clean
