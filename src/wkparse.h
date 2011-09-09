#ifndef _WKPARSE_H
#define _WKPARSE_H

#include <stdint.h>
#include <expat.h>

struct page_info_t {
	uint64_t position; // xml position in stream (from xml start)
	uint64_t size; // bytes in xml stream

	enum {
		WK_STATE_EMPTY = 0,
		WK_STATE_BODY = 1,
		WK_STATE_TITLE = 2
	} state;

	char title[4096];

	char *body;
	int body_len;
};

struct wkxml_parser_t {
	int depth;
	struct page_info_t page;

	void (*page_handler)(struct page_info_t *, void *);
	void *extra;

	XML_Parser xmlp;
};

void wkxml_init(struct wkxml_parser_t *i);
void wkxml_destroy(struct wkxml_parser_t *i);
void wkxml_parse(struct wkxml_parser_t *i, char *data, uint64_t size, int done);

#endif
