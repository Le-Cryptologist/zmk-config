# The browser harness. Needs emscripten; on this Mac it comes from nix:
#   nix shell nixpkgs#emscripten -c make
# Produces game.js with the wasm embedded, so index.html works from file://.

SRC = glue.c ../src/fb.c ../src/font5x7.c ../src/engine.c ../src/game_drift.c \
      ../src/game_catch.c ../src/game_balance.c

game.js: $(SRC) ../include/totem_game.h
	emcc -O2 -std=c11 -Wall -Wextra -Wno-unused-parameter -I../include $(SRC) -o $@ \
	  -s SINGLE_FILE=1 -s MODULARIZE=1 -s EXPORT_NAME=createGame -s ENVIRONMENT=web \
	  -s EXPORTED_RUNTIME_METHODS=UTF8ToString,HEAPU8 -s ALLOW_MEMORY_GROWTH=0

clean:
	rm -f game.js

.PHONY: clean
