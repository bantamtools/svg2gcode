#include <stdlib.h>
#include <string.h>
#include "vector.h"

void vector_init(Vector *v, size_t item_size) {
    v->capacity = 4;  // initial capacity
    v->size = 0;
    v->item_size = item_size;
    v->items = malloc(v->item_size * v->capacity);
}

size_t vector_size(Vector *v) {
    return v->size;
}

static void vector_resize(Vector *v, size_t capacity) {
    void **items = realloc(v->items, sizeof(void *) * capacity);
    if (items) {
        v->items = items;
        v->capacity = capacity;
    }
}

void vector_add(Vector *v, void *item) {
    if (v->capacity == v->size) {
        vector_resize(v, v->capacity * 2);
    }
    v->items[v->size] = malloc(v->item_size);
    if (v->items[v->size]) {
        memcpy(v->items[v->size], item, v->item_size);
        v->size++;
    }
    // Consider handling allocation failure
}

void *vector_get(Vector *v, size_t index) {
    if (index < v->size) {
        return v->items[index];
    }
    return NULL;
}

void vector_set(Vector *v, size_t index, void *item) {
    if (index < v->size) {
        v->items[index] = item;
    }
}

void vector_delete(Vector *v, size_t index) {
    if (index < v->size) {
        v->items[index] = NULL;
        for (size_t i = index; i < v->size - 1; i++) {
            v->items[i] = v->items[i + 1];
            v->items[i + 1] = NULL;
        }
        v->size--;
        if (v->size > 0 && v->size == v->capacity / 4) {
            vector_resize(v, v->capacity / 2);
        }
    }
}

void vector_free(Vector *v) {
    free(v->items);
}