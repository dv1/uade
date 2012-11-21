#ifndef _BUFFER_FIFO_H_
#define _BUFFER_FIFO_H_

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

struct fifo {
	size_t lower;
	size_t upper;
	size_t capacity;
	uint8_t *buf; /* There is valid data in range [lower, upper) */
};

/* Create a fifo */
struct fifo *fifo_create(void);

/* Frees the fifo. You may not used the fifo after calling this. */
void fifo_free(struct fifo *fifo);

static inline size_t fifo_len(const struct fifo *fifo)
{
	assert(fifo->lower <= fifo->upper);
	return fifo->upper - fifo->lower;
}

/* Returns the number of bytes read */
size_t fifo_read(void *data, size_t maxbytes, struct fifo *fifo);

/* Return 0 on success, -1 on error (no memory or too large buffer). */
int fifo_write(struct fifo *fifo, const void *data, size_t bytes);

#endif
