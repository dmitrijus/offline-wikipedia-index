#ifndef _BZPARSE_H
#define _BZPARSE_H

#include "bzpartial.h"

#include <stdint.h>
#include <expat.h>

struct page_info_t {
	uint64_t offset;
	uint64_t size;

	enum {
		WK_STATE_EMPTY = 0,
		WK_STATE_TITLE

	} state;

	struct bz_part_t *source;
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

void traverse_pages(struct buffer_t *, void (*page_handler)(struct page_info_t *, void *), void *extra);
void retrieve_block(struct buffer_t *, struct bz_part_t *, uint64_t offset, uint64_t size);

#endif
