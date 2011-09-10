#include "bzpartial.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <bzlib.h>

//                          76543210 // 16mb (-1 must result in a mask, max = 2gb/8)
#define MMAP_SIZE 0x0000000000001000ULL

#define BZ_START_MAGIC 0x0000314159265359ULL
#define BZ_START_MASK  0x0000FFFFFFFFFFFFULL
#define BZ_EOS_MAGIC  0x0000177245385090ULL
#define BZ_EOS_MASK  0x0000FFFFFFFFFFFFULL

/*
 * Functions dealing with reading and seeking of bit streams.
 * Implemented using mmap, because we can (and want to learn)
 * It seems that using getc/read actually yield better performance
 * (but i really wanted to try mmap/madvise).
 */
void buffer_open(struct buffer_t *buf, char *fn, char *mode) {
    buf->offset = 0;
    buf->bit = 0;
    buf->map = NULL;
	buf->map_size = MMAP_SIZE;
	buf->map_offset = 0;
	buf->offset = 0;
	buf->fn = fn;

    buf->fd = open(fn, O_RDONLY);
    if (buf->fd == -1) {
        perror("error opening file for reading");
        exit(EXIT_FAILURE);
    }

    buf->size = lseek64(buf->fd, 0, SEEK_END);
    lseek64(buf->fd, 0, SEEK_SET);

    if (read(buf->fd, buf->head, 16) < 16) {
        perror("error reading this 16 bytes");
        close(buf->fd);
        exit(EXIT_FAILURE);
    }

    // fprintf(stderr, "set size: %ld", buf->size);
    buffer_reopen(buf);
}

void buffer_reopen(struct buffer_t *buf) {
    if ((buf->map_offset == buf->offset) && (buf->map))
        return;

	// check for eof
	if ((buf->offset + (buf->bit >> 3)) >= buf->size) {
		buf->eof = 1;
	} else {
		buf->eof = 0;
		buf->act_size = buf->map_size;

		if (buf->act_size > (buf->size - buf->offset))
			buf->act_size = buf->size - buf->offset;
	}

    if (buf->map) {
		munmap(buf->map, buf->map_size);
    };

    lseek(buf->fd, 0, SEEK_SET);
    char *ret = mmap(0, buf->map_size, PROT_READ, MAP_PRIVATE, buf->fd, buf->offset);
    if (ret == NULL) {
        perror("Error mmap'ing file:");
        exit(EXIT_FAILURE);
    }

    madvise(ret, buf->map_size, MADV_SEQUENTIAL);

    buf->map = ret;
	buf->map_offset = buf->offset;
}

void buffer_close(struct buffer_t *buf) {
    munmap(buf->map, buf->map_size);
	close(buf->fd);
}

int inline buffer_read_bit(struct buffer_t *buf) {
	if (buf->eof)
		return -1;

    int bit = buf->bit & 0x7;
    bit = 7 - bit; // big endianness

    int ret = (buf->map[buf->bit >> 3] >> bit) & 0x1;

	if (buf->bit >= ((buf->act_size << 3) - 1)) {
		buf->offset += buf->map_size;
		buf->bit = 0;
		buffer_reopen(buf);
	} else {
		++buf->bit;
	}

	return ret;
}

void buffer_seek_bits(struct buffer_t *buf, uint64_t bits) {
	uint64_t bytes_from_offset = (bits >> 3) & (buf->map_size - 1);

	buf->offset = (bits >> 3) - bytes_from_offset;
    buf->bit = (bytes_from_offset << 3) + (bits & 0x7); 

	//fprintf(stderr, "seek to: offset=%ld bit=%d given=%ld\n", buf->offset, buf->bit, bits);
    buffer_reopen(buf);
}

/* actions for anon buffer */
void abuffer_init(struct abuffer_t *buf) {
	buf->size = PART_BUF_SIZE;
	buf->bit = 0;

    char *ret = mmap(0, buf->size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ret == NULL) {
        perror("Error mmap'ing file:");
        exit(EXIT_FAILURE);
    }

	buf->map = ret;
}

void abuffer_close(struct abuffer_t *buf) {
    munmap(buf->map, buf->size);
}

void inline abuffer_put_bit(struct abuffer_t *buf, char bit) {
	buf->map[buf->bit >> 3] &= ~(1  << (7 - (buf->bit & 0x7)));
	buf->map[buf->bit >> 3] |= (bit << (7 - (buf->bit & 0x7)));

	buf->bit++;
}

void abuffer_put_bytes(struct abuffer_t *buf, const char *bytes, uint32_t len) {
	while (len > 0) {
		for (int i = 7; i >= 0; --i)
			abuffer_put_bit(buf, (bytes[0] >> i) & 0x1);

		--len;
		bytes++;
	}
}

void buffer_transfer_bytes_aligned(struct buffer_t *buf, char *dest, uint32_t len) {
	assert(!(buf->bit & 0x7));

	uint32_t limit = buf->act_size - (buf->bit >> 3);

	if (limit > len) {
		memcpy(dest, buf->map + (buf->bit >> 3), len);

		buf->bit += len << 3;
		return;
	} else {
		memcpy(dest, buf->map + (buf->bit >> 3), limit);

		dest += limit;
		len -= limit;

		buffer_seek_bits(buf, (buf->offset << 3) + buf->bit + (limit << 3));
		if (len)
			return buffer_transfer_bytes_aligned(buf, dest, len);
	}
}

void abuffer_transfer_bits(struct buffer_t *buf, struct abuffer_t *dest, uint32_t len) {
	while ((len > 0) && (buf->bit & 0x7)) {
		char bit = buffer_read_bit(buf);
		abuffer_put_bit(dest, bit);
		--len;
	}

	// input is now byte alligned
	// calculate the output offset
	char out_pad = (8 - (dest->bit & 0x7)) & 0x7;
	
	char *out_start = dest->map + ((dest->bit + out_pad) >> 3);
	uint32_t out_len = len >> 3;

	buffer_transfer_bytes_aligned(buf, out_start, out_len);


	// transfer them to this buffah
	abuffer_put_bytes(dest, out_start, out_len);

	// get the reminder
	len = len & 0x7;
	while (len > 0) {
		char bit = buffer_read_bit(buf);
		abuffer_put_bit(dest, bit);
		--len;
	}
}

void abuffer_pad8(struct abuffer_t *buf) {
	while (buf->bit & 0x7) {
		abuffer_put_bit(buf, 0);
	}
}

/*
 * Find part in a stream, return it's relative position and size
 */
int bz_find_part(struct buffer_t *buf, uint64_t start_offset, uint64_t *p_start, uint64_t *p_end) {
    uint64_t bits_read = 0;
    uint64_t head;

    buffer_seek_bits(buf, start_offset);

    // find start
    head = 0;
    for (;;) {
        int bit = buffer_read_bit(buf);
        if (bit == EOF) {
            return -1;
        }

        bits_read++;

        head = (head << 1) ^ bit;
        /* fprintf(stderr, "sup at: %ld\n", head); */
        if ((head & BZ_START_MASK) == BZ_START_MAGIC) {
            // fprintf(stderr, "Found start at: %ld\n", bits_read);
            *p_start = start_offset + bits_read;
            break;
        }
    }

    // find end
    head = 0;
    for (;;) {
        int bit = buffer_read_bit(buf);

        if (bit == EOF) {
            fprintf(stderr, "Premature eof at bit: %ld\n", bits_read);
            *p_end = start_offset + bits_read;
            break;
        }

        bits_read++;

        head = (head << 1) ^ bit;
        /* fprintf(stderr, "sup at: %ld\n", head); */
        if (((head & BZ_START_MASK) == BZ_START_MAGIC) ||
            ((head & BZ_EOS_MASK) == BZ_EOS_MAGIC)) {

            //fprintf(stderr, "magic: %lx at %ld\n", head & BZ_EOS_MASK, buf->bit);
            *p_end = start_offset + bits_read - 48;
            break;
        }
    }

    //fprintf(stderr, "Found part at: %lu - %lu\n", *p_start, *p_end);
    return 0;
}

/*
 * Recreate partial archive for decompression, dst must be larger than 900k
 */
uint64_t bz_recreate(struct buffer_t *buf, struct abuffer_t *dest, uint64_t p_start, uint64_t p_end) {
    buffer_seek_bits(buf, p_start);

    /* recreate the header */

	const char cm_head[6] = {0x31, 0x41, 0x59, 0x26, 0x53, 0x59};
	const char cm_tail[6] = {0x17, 0x72, 0x45, 0x38, 0x50, 0x90};

	abuffer_put_bytes(dest, buf->head, 4); // includes the start magic
	abuffer_put_bytes(dest, cm_head, 6);

	abuffer_transfer_bits(buf, dest, p_end - p_start);

    // copy eos magic
	abuffer_put_bytes(dest, cm_tail, 6);
    // copy checksum, the same as in stream
	abuffer_put_bytes(dest, dest->map + 10, 4);

    abuffer_pad8(dest);
	return dest->bit >> 3;
}

uint64_t bz_dstream(struct buffer_t *buf, uint64_t *offset, char *dst, uint64_t dst_len) {
	char *part_buf = (char *)malloc(PART_BUF_SIZE);

	uint64_t p_start, p_end;

	if (bz_find_part(buf, *offset, &p_start, &p_end) != 0) {
        free(part_buf);    
        return 0;
    }

	struct abuffer_t dest;

	abuffer_init(&dest);

	uint64_t part_len = bz_recreate(buf, &dest, p_start, p_end);
	unsigned int dst_len_ptr = dst_len;

	int ret = BZ2_bzBuffToBuffDecompress(dst, &dst_len_ptr, dest.map, part_len, 0, 0);

    if (ret == BZ_OK) {
        // fprintf(stderr, "decoded bit: %u\n", dst_len_ptr);
    } else { 
		fprintf(stderr, "BZip2 read error: %d\n", ret);
        exit(EXIT_FAILURE);
    }

	abuffer_close(&dest);

	*offset = p_end;
	free(part_buf);
    return dst_len_ptr;
}
