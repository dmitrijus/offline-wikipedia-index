#include "bzpartial.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <bzlib.h>

#define MMAP_SIZE 0x0000000010000000ULL
#define MMAP_MASK 0x000000000FFFFFFFULL
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
    buf->map = 0;
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
    uint64_t offset = (buf->bit >> 3) & (~MMAP_MASK);

    if ((offset == buf->offset) && (buf->map))
        return;

    if (buf->map) {
        munmap(buf->map, MMAP_SIZE);
    };

    lseek(buf->fd, 0, SEEK_SET);
    char *ret = mmap(0, MMAP_SIZE, PROT_READ, MAP_PRIVATE, buf->fd, offset);
    if (ret == NULL) {
        perror("Error mmap'ing file:");
        exit(EXIT_FAILURE);
    }

    madvise(ret, MMAP_SIZE, MADV_SEQUENTIAL);

    buf->map = ret;
    buf->offset = offset;
}

void buffer_close(struct buffer_t *buf) {
    close(buf->fd);
}

int buffer_read_bit(struct buffer_t *buf) {
    uint64_t byte = (buf->bit >> 3);
    int bit = (buf->bit & 0x7);
    bit = 7 - bit; // bit endianness

    if (byte >= buf->size)
        return EOF;

    if (!(byte & MMAP_MASK))
        buffer_reopen(buf);

    /* fprintf(stderr, "pos: %ld[%ld] byte: %d bit: %d",
        buf->bit, byte & MMAP_MASK,
        buf->map[byte & MMAP_MASK],
        buf->map[byte & MMAP_MASK] >> bit);
    */

//  fprintf(stderr, "reading bit=%ld byte=%ld mbyte=%ld\n", buf->bit, byte, byte & MMAP_MASK);
    buf->bit++;
    return (buf->map[byte & MMAP_MASK] >> bit) & 0x1;
}

uint64_t buffer_transfer_bits(struct buffer_t *buf, uint64_t size, char *dst) {
    uint64_t pos = 0;
    uint64_t total = size >> 8;

    if (size & 0x7)
        total++;

    memset(dst, 0, total);

    while (pos < size) {
        int bit = buffer_read_bit(buf);
        dst[pos >> 3] |= (bit << (7 - (pos & 0x7)));

        pos++;
    }

    return size;
}

void buffer_seek_bits(struct buffer_t *buf, uint64_t bits) {
    buf->bit = bits;
    buffer_reopen(buf);
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
            //fprintf(stderr, "Found start at: %ld\n", bits_read);
            *p_start = start_offset + bits_read;
            break;
        }
    }

    // find end
    head = 0;
    for (;;) {
        int bit = buffer_read_bit(buf);
        if (bit == EOF) {
            fprintf(stderr, "Premature eof at bit: %ld\n", buf->bit);
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

    //fprintf(stderr, "Found part at: %ld - %ld\n", part->start, part->end);
    return 0;
}

void put_byte(char *dst, uint64_t *size, char byte) {
    char offset = (*size) & 0x7;
    dst[(*size) / 8] |= ((unsigned char)byte >> offset);
    dst[((*size) / 8)  + 1] |= ((unsigned char)byte << (8 - offset)) & 0xFF;
    *size = *size + 8;
}

/*
 * Recreate partial archive for decompression, dst must be larger than 900k
 */
uint64_t bz_recreate(struct buffer_t *buf, uint64_t p_start, uint64_t p_end, char *dst, uint64_t dst_len) {
	uint64_t buf_size = (p_end - p_start + 8) / 8;
	buf_size = buf_size + 4 + 6 + 6 + 4;

	if (dst_len <= buf_size) {
		fprintf(stderr, "Buffer size is too small to hold bz2 part");
		exit(EXIT_FAILURE);
	}

    /* recreate the header */
    uint64_t size = 0;
    memset(dst, 0, dst_len);

    memcpy(dst, buf->head, 4); /* includes the start magic */
    size = 32;

    put_byte(dst, &size, 0x31); put_byte(dst, &size, 0x41); put_byte(dst, &size, 0x59);
    put_byte(dst, &size, 0x26); put_byte(dst, &size, 0x53); put_byte(dst, &size, 0x59);

    buffer_seek_bits(buf, p_start);
    uint64_t data_size = buffer_transfer_bits(buf, p_end - p_start, dst+10);
    size += data_size;

    // copy eos magic
    put_byte(dst, &size, 0x17); put_byte(dst, &size, 0x72); put_byte(dst, &size, 0x45);
    put_byte(dst, &size, 0x38); put_byte(dst, &size, 0x50); put_byte(dst, &size, 0x90);

    // copy checksum
    put_byte(dst, &size, dst[10]);
    put_byte(dst, &size, dst[11]);
    put_byte(dst, &size, dst[12]);
    put_byte(dst, &size, dst[13]);

    if (size & 0x7)
        size += 8 - (size & 0x7);

    return size / 8;
}

uint64_t bz_dstream(struct buffer_t *buf, uint64_t *offset, char *dst, uint64_t dst_len) {
	char *part_buf = (char *)malloc(PART_BUF_SIZE);

	uint64_t p_start, p_end;

	if (bz_find_part(buf, *offset, &p_start, &p_end) != 0) {
        free(part_buf);    
        return 0;
    }

	uint64_t part_len = bz_recreate(buf, p_start, p_end, part_buf, PART_BUF_SIZE);
	unsigned int dst_len_ptr = dst_len;

	int ret = BZ2_bzBuffToBuffDecompress(dst, &dst_len_ptr, part_buf, part_len, 0, 0);

    if (ret == BZ_OK) {
        // fprintf(stderr, "decoded bit: %u\n", dst_len_ptr);
    } else { 
		fprintf(stderr, "BZip2 read error: %d\n", ret);
        exit(EXIT_FAILURE);
    }

	*offset = p_end;
	free(part_buf);
    return dst_len_ptr;
}
