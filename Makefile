main: main.c
	gcc $< -o $@ -lm `pkg-config --cflags --libs gio-unix-2.0`
