CC = clang
GCC = clang
CFLAGS = -Ofast -v -mlongcalls

all: release

tool: svg2gcode

lib: libsvg2gcode.a

svg2gcode: svg2gcode.c nanosvg.h
	$(CC) -o svg2gcode -g svg2gcode.c -lm

libsvg2gcode.a: svg2gcode.o
	xtensa-esp32s3-elf-ar rcs libsvg2gcode.a svg2gcode.o

svg2gcode.o: svg2gcode.c nanosvg.h
	$(GCC) $(CFLAGS) -c svg2gcode.c

clean:
	rm -fr svg2gcode *.o

release: svg2gcode.o
