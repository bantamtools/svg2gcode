#ifndef VECTOR_H
#define VECTOR_H

#include <stdlib.h>

typedef struct {
    void **items;
    size_t capacity;
    size_t size;
    size_t item_size;
} Vector;

void vector_init(Vector *m, size_t item_size);
size_t vector_size(Vector *v);
static void vector_resize(Vector *v, size_t capacity);
void vector_add(Vector *v, void *item);
void *vector_get(Vector *v, size_t index);
void vector_set(Vector *v, size_t index, void *item);
void vector_delete(Vector *v, size_t index);
void vector_free(Vector *v);

#endif //VECTOR_H