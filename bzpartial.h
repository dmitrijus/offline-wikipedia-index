#ifndef _BZPARTIAL_H
#define _BZPARTIAL_H

#include <sys/types.h>
#include <stdint.h>
#include <bzlib.h>

#define BZ_BUFSIZE 1024*1024*2

struct buffer_t {
	int fd;
	uint64_t size;

	uint64_t bit;
	uint64_t offset;

	char head[16];
	char *map;
};

struct bz_part_t {
	struct bz_part_t *next;
	char *fn;

	uint64_t start; /* start bit in bz2 */
	uint64_t end; /* last+1 bit in bz2 */

	uint64_t byte_offset; /* offset in a global stream, byte */
	uint64_t page_offset; /* hack, start of meaningful information, byte */
};

struct bz_decoder_t {
	bz_stream	stream;
	char		bzbuf[BZ_BUFSIZE];
	uint64_t	bzbuf_len;

	struct buffer_t		*buf;
	struct bz_part_t	*part;
	char				eof;
};

void buffer_open(struct buffer_t *buf, char *fn, char *mode);
void buffer_close(struct buffer_t *buf);
void buffer_reopen(struct buffer_t *);

int buffer_read_bit(struct buffer_t *buf);
uint64_t buffer_transfer_bits(struct buffer_t *buf, uint64_t size, char *dst);
void buffer_seek_bits(struct buffer_t *buf, uint64_t bits);

struct bz_part_t *bz_find_part(struct buffer_t *buf, uint64_t start_offset);
void bz_free_parts(struct bz_part_t *part);

uint64_t bz_recreate(struct buffer_t *buf, struct bz_part_t *part, char *dst);
void bz_decoder_init(struct bz_decoder_t *d, struct buffer_t *buf, struct bz_part_t *p);
uint64_t bz_decode(struct bz_decoder_t *d, char *buf, uint64_t len);
void bz_decoder_destroy(struct bz_decoder_t *d);

#endif
