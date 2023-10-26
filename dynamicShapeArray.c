#include "dynamicShapeArray.h"

DynamicShapeArray* createDynamicShapeArray() {
    DynamicShapeArray *arr = (DynamicShapeArray*) malloc(sizeof(DynamicShapeArray));
    if (!arr) return NULL;

    arr->capacity = 4;  // Initial capacity
    arr->size = 0;
    arr->items = (NSVGshape**) malloc(sizeof(NSVGshape*) * arr->capacity);
    if (!arr->items) {
        free(arr);
        return NULL;
    }

    return arr;
}

void freeDynamicShapeArray(DynamicShapeArray *arr) {
    if (!arr) return;
    free(arr->items);
    free(arr);
}

int addToDynamicShapeArray(DynamicShapeArray *arr, NSVGshape *shape) {
    if (arr->size == arr->capacity) {
        size_t newCapacity = arr->capacity * 2;
        NSVGshape **newItems = (NSVGshape**) realloc(arr->items, newCapacity * sizeof(NSVGshape*));
        if (!newItems) return -1;  // Realloc failed

        arr->items = newItems;
        arr->capacity = newCapacity;
    }
    
    arr->items[arr->size++] = shape;
    return 0;  // Success
}
