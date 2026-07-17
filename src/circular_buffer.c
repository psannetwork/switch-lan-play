#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint8_t *buffer;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t size;
} circular_buffer_t;

circular_buffer_t *circular_buffer_create(size_t capacity) {
    circular_buffer_t *cb = malloc(sizeof(circular_buffer_t));
    cb->buffer = malloc(capacity);
    cb->capacity = capacity;
    cb->head = 0;
    cb->tail = 0;
    cb->size = 0;
    return cb;
}

void circular_buffer_destroy(circular_buffer_t *cb) {
    free(cb->buffer);
    free(cb);
}

void circular_buffer_push(circular_buffer_t *cb, const uint8_t *data, size_t len) {
    if (cb->size + len > cb->capacity) {
        size_t new_capacity = cb->size + len + 65536;
        uint8_t *new_buffer = malloc(new_capacity);

        // Copy existing data to new buffer
        for (size_t i = 0; i < cb->size; i++) {
            new_buffer[i] = cb->buffer[(cb->head + i) % cb->capacity];
        }

        free(cb->buffer);
        cb->buffer = new_buffer;
        cb->capacity = new_capacity;
        cb->head = 0;
        cb->tail = cb->size;
    }

    for (size_t i = 0; i < len; i++) {
        cb->buffer[cb->tail] = data[i];
        cb->tail = (cb->tail + 1) % cb->capacity;
        cb->size++;
    }
}

size_t circular_buffer_size(circular_buffer_t *cb) {
    return cb->size;
}

void circular_buffer_read(circular_buffer_t *cb, uint8_t *dest, size_t len) {
    for (size_t i = 0; i < len; i++) {
        dest[i] = cb->buffer[(cb->head + i) % cb->capacity];
    }
}

void circular_buffer_discard(circular_buffer_t *cb, size_t len) {
    cb->head = (cb->head + len) % cb->capacity;
    cb->size -= len;
}

