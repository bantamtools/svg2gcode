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

static void vector_resize(Vector *v, size_t new_capacity) {
    void *items = realloc(v->items, v->item_size * new_capacity);
    if (items) {
        v->items = items;
        v->capacity = new_capacity;
    }
}

void vector_add(Vector *v, void *item, FILE* debug) {
    if (v->capacity == v->size) {
        // Double vector size on resize.
        vector_resize(v, v->capacity * 2);
    }

    // Calculate the address of the new element
    void *destination = (char *)v->items + (v->size * v->item_size);
    
    // Copy the item into the new location
    memcpy(destination, item, v->item_size);

    // Increase the size of the vector
    v->size += 1;
    
    // Optional: Debug output
    if (debug) {
        fprintf(debug, "Added item to vector, new size: %zu\n", v->size);
    }
}




void *vector_get(Vector *v, size_t index) {
    if (index < v->size) {
        return *((void **)((char *)v->items + index * v->item_size));
    }
    return NULL;
}



void vector_delete(Vector *v, size_t index) {
    if (index < v->size) {
        // Calculate the address of the element to delete
        char *base = (char *)v->items;
        
        // Move the memory down one element's size
        memmove(base + index * v->item_size,
                base + (index + 1) * v->item_size,
                (v->size - index - 1) * v->item_size);

        // Decrease the size of the vector
        v->size--;

        // Optionally resize the vector if it's much smaller than the capacity
        if (v->size > 0 && v->size == v->capacity / 4) {
            vector_resize(v, v->capacity / 2);
        }
    }
}



void vector_free(Vector *v) {
    free(v->items);
    v->items = NULL;
    v->size = 0;
    v->capacity = 0;
}
