#include <string.h>
#include <stdio.h>
#include <expat.h>
#include <assert.h>
#include <getopt.h>

#include "bzpartial.h"
#include "bzparse.h"
#include "wkindex.h"

struct parser_extra_t  {
	uint64_t total_bits;
	uint32_t articles;

	struct wkindex_t index;
};

void create_name(char *buf, int buf_len, struct page_info_t *p) {
	char *fn = p->source->buffer->fn;

	snprintf(buf, buf_len, "%s:%ld:%ld:%ld:%s",
		fn,
		p->source->start,
		p->offset,
		p->size,
		p->title
	);
}

void print_part_handler(struct bz_part_t *p, void *extra) {
	struct parser_extra_t *ei =  (struct parser_extra_t *)extra;

	int pm = (uint64_t)p->end * 1000 / (uint64_t)ei->total_bits;

	fprintf(stderr,
		"[%3d.%1d%% / %d] Part [%ld - %ld] byte_offset at %ld, page_offset %ld\n",
		pm / 10,
		pm % 10,
		ei->articles,
		p->start,
		p->end,
		p->byte_offset,
		p->page_offset);
}

void print_page_handler(struct page_info_t *p, void *extra) {
	struct parser_extra_t *ei =  (struct parser_extra_t *)extra;
	ei->articles++;

	char pn[1024]; pn[0] = 0;
	create_name(pn, 1023, p);

	wkindex_add_page(&ei->index, pn, p);
	//fprintf(stderr, "Found page: %s @ %s\n", p->title, pn);
}

void index_file(char *fn) {
	struct buffer_t buf;
	buffer_open(&buf, fn, "r");

	struct parser_extra_t extra;
	extra.total_bits = buf.size * 8;
	extra.articles = 0;
	wkindex_init(&extra.index, "db/");

	struct bz_part_t *root = traverse_pages(&buf, print_page_handler, print_part_handler, &extra);
	//struct bz_part_t *root = find_parts(&buf);

	wkindex_destroy(&extra.index);
	bz_free_parts(root);
	buffer_close(&buf);
}

void invalid_if_null(char *token, char *path_id) {
	if (token == NULL) {
		fprintf(stderr, "invalid id: %s\n", path_id);
		exit(EXIT_FAILURE);
	}
}

char *extract_char(char *path_id, char *fn, int fn_len) {
	char *token = strchr(path_id, ':');
	invalid_if_null(token, path_id);

	if ((token - path_id) >= fn_len) {
		fprintf(stderr, "Filename too long.");
		exit(EXIT_FAILURE);
	}

	fn[0] = 0;
	strncat(fn, path_id, token - path_id);
	return token + 1;
}

char *extract_uint64(char *path_id, uint64_t *out) {
	char *token = strchr(path_id, ':');
	invalid_if_null(token, path_id);

	if ((token - path_id) >= 20) {
		fprintf(stderr, "Integer too long.");
		exit(EXIT_FAILURE);
	}

	char buf[32]; buf[0] = 0;

	char *r = NULL;

	strncat(buf, path_id, token - path_id);
	*out = strtol(buf, &r, 10);

	if (r && (*r != 0)) {
		fprintf(stderr, "Invalid integer: %s.", r);
		exit(EXIT_FAILURE);
	}

	return token + 1;
}

void extract_xml(char *path_id) {
	char *token = path_id;
	char fn[1024];

	uint64_t offset;
	uint64_t pg_offset;
	uint64_t pg_size;

	token = extract_char(token, fn, 1000);
	token = extract_uint64(token, &offset);
	token = extract_uint64(token, &pg_offset);
	token = extract_uint64(token, &pg_size);

	fprintf(stderr, "give: %s:%ld:%ld:%ld:dontcare\n",
		fn,
		offset,
		pg_offset,
		pg_size
	);

	struct buffer_t buf;
	buffer_open(&buf, fn, "r");
	
	char *xml_buf = malloc((pg_offset + pg_size + 1) * sizeof(char));
	extract_page(&buf, offset - 48, xml_buf, pg_offset + pg_size);

	xml_buf[pg_offset + pg_size] = 0;
	printf("%s\n", &xml_buf[pg_offset]);
};

void help(char *name) {
	fprintf(stderr, "%s version v0.1\n", name);
	fprintf(stderr, "usage: %s -i file | -h | -v\noptions:\n", name);
	fprintf(stderr, "\t -i filename \t\tindex the given .bz2 dump file.\n");
	fprintf(stderr, "\t -e article_id \t\textract xml dump for given article id.\n");
}

int main(int argc, char **argv) {
	int c;

	while ((c = getopt (argc, argv, "vhi:e:")) != -1) {
		switch (c) {
			case 'v':
				help(argv[0]);
				return 0;
			case 'h':
				help(argv[0]);
				return 0;
			case 'i':
				index_file(optarg);
				return 0;
			case 'e':
				extract_xml(optarg);
				return 0;
			default:
				help(argv[0]);
				return 1;
        }
	}

	help(argv[0]);
	return 1;
}
