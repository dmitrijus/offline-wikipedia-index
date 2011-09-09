#ifndef _BZEXTRACT_H
#define _BZEXTRACT_H

#include <sys/types.h>
#include <stdint.h>

#define BZE_PART_MAGIC 0x15121715
#define BZE_STOP_INF -1

struct bze_part_t {
	uint64_t magic;

	char src_fn[1024];
	uint64_t src_start;
	uint64_t src_end;

	uint64_t dst_size;
};

struct bze_options_t {
	uint64_t start_bit;
	uint64_t seek_bytes;
	uint64_t stop_bytes;
	char *fn;

	enum output_type {
		OUT_NONE = 0,
		OUT_SIMPLE = 1,
		OUT_PARTI = 2,
		OUT_TAGGED = 3,
	}	output_type;

	int printing_opts;
	void (*write_fun)(void *, char *, uint64_t);
	void *write_opts;
};

int bze_extract_data(struct bze_options_t *opts);
char *bze_extract_string(char *fn, uint64_t bit, uint64_t seek, uint64_t count);
void bze_write_stdout_fun(void *opts, char *buf, uint64_t buf_len);
#endif
