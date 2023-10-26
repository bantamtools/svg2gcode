#ifndef DYNAMIC_SHAPE_ARRAY_H
#define DYNAMIC_SHAPE_ARRAY_H

#ifndef NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#endif // Include your NSVGshape definition here

#include "stdlib.h"

typedef struct {
    NSVGshape **items;
    size_t size;
    size_t capacity;
} DynamicShapeArray;

DynamicShapeArray* createDynamicShapeArray();
void freeDynamicShapeArray(DynamicShapeArray *arr);
int addToDynamicShapeArray(DynamicShapeArray *arr, NSVGshape *shape);

#endif // DYNAMIC_SHAPE_ARRAY_H
