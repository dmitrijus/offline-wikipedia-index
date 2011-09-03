#ifndef _BZPARSE_H
#define _BZPARSE_H

#include "bzpartial.h"

#include <stdint.h>
#include <expat.h>

#define MAX_TITLE 1024

struct page_info_t {
	uint64_t offset;
	uint64_t size;

	enum {
		WK_STATE_EMPTY = 0,
		WK_STATE_TITLE

	} state;

	struct bz_part_t *source;

	XML_Char *title;
	XML_Char *body;

	int title_len;
	int body_len;
};

struct bzxml_parser_t {
	int depth;
	int page_init; /* checks if page_offset needs to bet on page */

	struct page_info_t  *page;
	struct bz_part_t    *part;

	void (*page_handler)(struct page_info_t *, void *);
	void (*part_handler)(struct bz_part_t *, void *);
	void *extra;

	XML_Parser xmlp;
};

void bzxml_init(struct bzxml_parser_t *i);
void bzxml_destroy(struct bzxml_parser_t *i);
void bzxml_parse(struct bzxml_parser_t *i, char *data, uint64_t size, int done);

struct bz_part_t *traverse_pages(
	struct buffer_t *buf,
	void (*page_handler)(struct page_info_t *, void *),
	void (*part_handler)(struct bz_part_t*, void *),
	void *extra);

struct bz_part_t *traverse_parts_only(
	struct buffer_t *buf,
	void (*part_handler)(struct bz_part_t*, void *),
	void *extra);

uint64_t extract_page(
	struct buffer_t *buf, uint64_t bit_offset,
	char *dst, uint64_t dst_size);

#endif
