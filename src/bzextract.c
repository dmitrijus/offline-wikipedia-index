#include "bzpartial.h"
#include "bzextract.h"

#include <string.h>
#include <stdio.h>
#include <expat.h>
#include <getopt.h>
#include <assert.h>
#include <unistd.h>

void print_part_info(struct bze_part_t *p, uint64_t buf_size, uint64_t total) {
	int pm = (uint64_t)p->src_end * 1000 / ((uint64_t)buf_size * 8);

	fprintf(stderr,
		"[%3d.%1d%%] part [%ld - %ld], uncompressed = %ld, total = %ld\n",
		pm / 10, pm % 10,
		p->src_start, p->src_end,
		p->dst_size, total);
}


int print_part(struct bze_part_t *p, struct bze_options_t *opts, char *out_buf, uint64_t out_len) {
	// check seek_bytes
	if (out_len <= opts->seek_bytes) {
		opts->seek_bytes -= out_len;
		return 0;
	} else if (opts->seek_bytes > 0) {
		out_len -= opts->seek_bytes;
		out_buf += opts->seek_bytes;
		opts->seek_bytes = 0;
	}

	// check output bytes
	if (opts->stop_bytes == BZE_STOP_INF) {
		// do nothing
	} else if (out_len <= opts->stop_bytes) {
		opts->stop_bytes -= out_len;
	} else if (out_len > opts->stop_bytes) {
		out_len = opts->stop_bytes;
		opts->stop_bytes = 0;
	}

	// actual printing
	if (opts->output_type & OUT_PARTI) {
		p->dst_size = out_len;

		opts->write_fun(opts->write_opts, (char *)p, sizeof(struct bze_part_t));
	}

	if (opts->output_type & OUT_SIMPLE) {
		opts->write_fun(opts->write_opts, out_buf, out_len);
	}

	// output
	if (opts->stop_bytes == 0) return 1;
	else return 0;
}

int bze_extract_data(struct bze_options_t *opts) {
	struct buffer_t buf;
	buffer_open(&buf, opts->fn, "r");

	uint64_t p_start = opts->start_bit;
	uint64_t p_end = opts->start_bit;

	struct bze_part_t part;
	strncpy(part.src_fn, opts->fn, 1023);
	part.src_fn[1023] = 0;
	part.magic = BZE_PART_MAGIC;

	char *data_buf = (char *)malloc(DATA_BUF_SIZE);
	uint64_t total = 0;

	for (;;) {
		p_start = p_end;

		uint64_t size = bz_dstream(
			&buf, &p_end, data_buf, DATA_BUF_SIZE);

		if (!size)
			break;

		part.src_start = p_start;
		part.src_end = p_end;
		part.dst_size = size;
		total += size;

		print_part_info(&part, buf.size, total);

		if (print_part(&part, opts, data_buf, size))
			break;
	}

	free(data_buf);
	buffer_close(&buf);

	return 0;
}

void bze_write_stdout_fun(void *opts, char *buf, uint64_t buf_len) {
	ssize_t written = write(1, buf, buf_len);

	if (written != buf_len) {
		perror("Error writing:");
		exit(EXIT_FAILURE);
	}
}

struct bze_string_state_t {
	uint32_t alloc_size;
	char *string;
};

void bze_write_string_fun(void *opts, char *buf, uint64_t buf_len) {
	struct bze_string_state_t *s = (struct bze_string_state_t *)opts;

	uint32_t current_size = 0;
	if (s->string)
		current_size = strlen(s->string);

	uint32_t nsize = s->alloc_size;
	while ((buf_len + current_size + 1) >= nsize)
		nsize += 64*1024;

	if (nsize != s->alloc_size) {
		s->string = realloc(s->string, nsize);
		s->alloc_size = nsize;
		s->string[current_size] = 0;
	}

	strncat(s->string, buf, buf_len); /* txt isn't zero terminated */

	s->string = s->string;
	return;
}


char *bze_extract_string(char *fn, uint64_t bit, uint64_t seek, uint64_t count) {
	struct bze_string_state_t state;
	state.string = NULL;
	state.alloc_size = 0;

	struct bze_options_t opts;

	opts.fn = fn;
	opts.start_bit = bit;
	opts.seek_bytes = seek;
	opts.stop_bytes = count;
	opts.output_type = OUT_SIMPLE;
	opts.write_opts = &state;
	opts.write_fun= bze_write_string_fun;
	bze_extract_data(&opts);

	return state.string;
}
