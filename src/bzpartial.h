#ifndef _BZPARTIAL_H
#define _BZPARTIAL_H

#include <sys/types.h>
#include <stdint.h>

#define PART_BUF_SIZE 1024*1024
#define DATA_BUF_SIZE 1024*1024

struct buffer_t {
	char *fn;
	int fd;
	uint64_t size;

	uint64_t bit;
	uint64_t offset;

	char head[16];
	char *map;
};

void buffer_open(struct buffer_t *buf, char *fn, char *mode);
void buffer_close(struct buffer_t *buf);
void buffer_reopen(struct buffer_t *);

int buffer_read_bit(struct buffer_t *buf);
uint64_t buffer_transfer_bits(struct buffer_t *buf, uint64_t size, char *dst);
void buffer_seek_bits(struct buffer_t *buf, uint64_t bits);

int bz_find_part(struct buffer_t *buf, uint64_t start_offset, uint64_t *p_start, uint64_t *p_end);
uint64_t bz_recreate(struct buffer_t *buf, uint64_t p_start, uint64_t p_stop, char *dst, uint64_t dst_len);
uint64_t bz_dstream(struct buffer_t *buf, uint64_t *offset, char *dst, uint64_t dst_len);

#endif
