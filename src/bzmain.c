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

int main(int argc, char **argv) {
	int c;

	struct bze_options_t opts;
	opts.start_bit = 0;
	opts.seek_bytes = 0;
	opts.stop_bytes = -1;
	opts.output_type = OUT_SIMPLE;
	opts.write_fun = bze_write_stdout_fun;;
	opts.write_opts = NULL;

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
