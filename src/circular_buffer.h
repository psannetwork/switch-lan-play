#ifndef _CIRCULAR_BUFFER_H_
#define _CIRCULAR_BUFFER_H_

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t *buffer;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t size;
} circular_buffer_t;

circular_buffer_t *circular_buffer_create(size_t capacity);
void circular_buffer_destroy(circular_buffer_t *cb);
void circular_buffer_push(circular_buffer_t *cb, const uint8_t *data, size_t len);
size_t circular_buffer_size(circular_buffer_t *cb);
void circular_buffer_read(circular_buffer_t *cb, uint8_t *dest, size_t len);
void circular_buffer_discard(circular_buffer_t *cb, size_t len);

#endif
