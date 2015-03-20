gasim: main.c
	gcc -o $@ -Wall -std=gnu99 -g `pkg-config --cflags --libs sdl` $^