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

    if (byte > buf->size)
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
struct bz_part_t *bz_find_part(struct buffer_t *buf, uint64_t start_offset) {
    uint64_t bits_read = 0;
    uint64_t head;

    struct bz_part_t *part = (struct bz_part_t *)malloc(sizeof(struct bz_part_t));
    memset(part, 0, sizeof(struct bz_part_t));
    buffer_seek_bits(buf, start_offset);

    // find start
    head = 0;
    for (;;) {
        int bit = buffer_read_bit(buf);
        if (bit == EOF) {
            free(part);
            return NULL;
        }

        bits_read++;

        head = (head << 1) ^ bit;
        /* fprintf(stderr, "sup at: %ld\n", head); */
        if ((head & BZ_START_MASK) == BZ_START_MAGIC) {
            //fprintf(stderr, "Found start at: %ld\n", bits_read);
            part->start = start_offset + bits_read;
            break;
        }
    }

    // find end
    head = 0;
    for (;;) {
        int bit = buffer_read_bit(buf);
        if (bit == EOF) {
            fprintf(stderr, "Premature eof at bit: %ld\n", buf->bit);
            part->end = start_offset + bits_read;
            break;
        }

        bits_read++;

        head = (head << 1) ^ bit;
        /* fprintf(stderr, "sup at: %ld\n", head); */
        if (((head & BZ_START_MASK) == BZ_START_MAGIC) ||
            ((head & BZ_EOS_MASK) == BZ_EOS_MAGIC)) {

            //fprintf(stderr, "magic: %lx at %ld\n", head & BZ_EOS_MASK, buf->bit);
            part->end = start_offset + bits_read - 48;
            break;
        }
    }

    //fprintf(stderr, "Found part at: %ld - %ld\n", part->start, part->end);
    return part;
}

void bz_free_parts(struct bz_part_t *part) {
    while (part) {
        struct bz_part_t *n = part->next;
        free(part);
        part = n;
    }
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
uint64_t bz_recreate(struct buffer_t *buf, struct bz_part_t *part, char *dst) {
    /* recreate the header */
    uint64_t size = 0;
    memset(dst, 0, BZ_BUFSIZE);

    memcpy(dst, buf->head, 4); /* includes the start magic */
    size = 32;

    put_byte(dst, &size, 0x31); put_byte(dst, &size, 0x41); put_byte(dst, &size, 0x59);
    put_byte(dst, &size, 0x26); put_byte(dst, &size, 0x53); put_byte(dst, &size, 0x59);

    buffer_seek_bits(buf, part->start);
    uint64_t data_size = buffer_transfer_bits(buf, part->end - part->start, dst+10);
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

void bz_decoder_init(struct bz_decoder_t *d, struct buffer_t *buf, struct bz_part_t *p) {
    d->part= p;
    d->buf = buf;
    d->eof = 0;

    memset(&d->stream, 0, sizeof(d->stream));
    memset(d->bzbuf, 0, BZ_BUFSIZE);

    if (BZ2_bzDecompressInit(&d->stream, 1, 0) != BZ_OK) {
        fprintf(stderr, "BZip2 init error.\n");
        exit(EXIT_FAILURE);
    }

    d->bzbuf_len = bz_recreate(d->buf, d->part, d->bzbuf);

    d->stream.next_in = d->bzbuf;
    d->stream.avail_in = d->bzbuf_len;

    // FILE *t = fopen("kaka.bz2", "w"); fwrite(d->bzbuf, 1, d->bzbuf_len, t); fclose(t);
    //fprintf(stderr, "init for part: %ld-%ld size=%ld\n", p->start, p->end, d->bzbuf_len);
}

void bz_decoder_destroy(struct bz_decoder_t *d) {
    d->eof = 1;
    BZ2_bzDecompressEnd(&d->stream);
}

uint64_t bz_decode(struct bz_decoder_t *d, char *buf, uint64_t len) {
    if (d->eof)
        return 0;

    d->stream.next_out = buf;
    d->stream.avail_out = len;

    int ret = BZ2_bzDecompress(&d->stream);
    uint64_t ret_size = len - d->stream.avail_out;

    if (ret == BZ_OK) {
        fprintf(stderr, "decoded bit: %ld\n", ret_size);
        return ret_size;
    } else if (ret == BZ_STREAM_END) {
        d->eof = 1;
        return ret_size;
    } else {
        fprintf(stderr, "BZip2 read error: %d with avail_out=%d.\n", ret, d->stream.avail_out);
        exit(EXIT_FAILURE);
        return 0;
    }
}
