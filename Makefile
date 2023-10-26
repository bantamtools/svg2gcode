CC = clang
GCC = clang
CFLAGS = -Ofast -v -mlongcalls

# Object files
OBJS = svg2gcode.o boundingViewHierarchy.o dynamicShapeArray.o

# Default target
all: release

# Executable
tool: svg2gcode

# Static library
lib: libsvg2gcode.a

# Main executable
svg2gcode: $(OBJS)
	$(CC) -o svg2gcode -g $(OBJS) -lm

# Static library
libsvg2gcode.a: $(OBJS)
	xtensa-esp32s3-elf-ar rcs libsvg2gcode.a $(OBJS)

# Individual object files
svg2gcode.o: svg2gcode.c nanosvg.h boundingViewHierarchy.h dynamicShapeArray.h
	$(GCC) $(CFLAGS) -c svg2gcode.c

boundingViewHierarchy.o: boundingViewHierarchy.c boundingViewHierarchy.h
	$(GCC) $(CFLAGS) -c boundingViewHierarchy.c

dynamicShapeArray.o: dynamicShapeArray.c dynamicShapeArray.h
	$(GCC) $(CFLAGS) -c dynamicShapeArray.c

# Clean up
clean:
	rm -fr svg2gcode *.o *.a

# Release build
release: svg2gcode
