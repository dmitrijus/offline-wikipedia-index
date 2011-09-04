#include "bzpartial.h"
#include "bzextract.h"

#include <string.h>
#include <stdio.h>
#include <expat.h>
#include <getopt.h>
#include <assert.h>
#include <unistd.h>

void bze_help(char *name) {
	fprintf(stderr, "usage: %s [-i offset] [-s skip] [-c count] file.bz2\n", name);
	fprintf(stderr, "version: 0.2\n");
	fprintf(stderr, "options:\n");
	fprintf(stderr, "\t -i bits \t\tnumbers of bits to skip while opening a given .bz2 .\n");
	fprintf(stderr, "\t -s count \t\tdont include the count of extracted bytes in the output.\n");
	fprintf(stderr, "\t -c count \t\tstop after writing given count of bytes to the output.\n");
	fprintf(stderr, "\t -m \t\tmachine parsable, binary output\t\t\n");
}

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

		ssize_t written = write(1, p, sizeof(struct bze_part_t));
		if (written != sizeof(struct bze_part_t)) {
			perror("Error writing:");
			exit(EXIT_FAILURE);
		}
	}

	if (opts->output_type & OUT_SIMPLE) {
		ssize_t written = write(1, out_buf, out_len);
		if (written != out_len) {
			perror("Error writing:");
			exit(EXIT_FAILURE);
		}
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
	strncpy(part.src_fn, opts->fn, 1024);
	part.src_fn[1024] = 0;
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

int main(int argc, char **argv) {
	int c;

	struct bze_options_t opts;
	opts.start_bit = 0;
	opts.seek_bytes = 0;
	opts.stop_bytes = -1;
	opts.output_type = OUT_SIMPLE;

	while ((c = getopt(argc, argv, ":vhmi:s:c:")) != -1) {
		switch (c) {
			case 'v':
				bze_help(argv[0]);
				return 0;
			case 'h':
				bze_help(argv[0]);
				return 0;
			case 'i':
				sscanf(optarg, "%lu", &opts.start_bit);
				break;
			case 's':
				sscanf(optarg, "%lu", &opts.seek_bytes);
				break;
			case 'c':
				sscanf(optarg, "%lu", &opts.stop_bytes);
				break;
			case 'm':
				opts.output_type = OUT_TAGGED;
				break;
			default:
				bze_help(argv[0]);
				return 1;
        }
	}

	if (optind != (argc - 1)) {
		bze_help(argv[0]);
		return 1;
	};

	opts.fn =  argv[optind];
	fprintf(stderr, "Starting %s with bit=%lu, seek=%lu, count=%lu\n", opts.fn,
		opts.start_bit, opts.seek_bytes, opts.stop_bytes);

	return bze_extract_data(&opts);
}
