#include "wkparse.h"
#include "bzextract.h"

#include <string.h>
#include <stdio.h>
#include <expat.h>
#include <assert.h>

#define MEDIAWIKI_HEAD "<mediawiki lang=\"en\">"

uint64_t get_pos(struct wkxml_parser_t *i) {
	XML_Index index_unsafe = XML_GetCurrentByteIndex(i->xmlp);
	return index_unsafe;
}

void page_init(struct page_info_t *page) {
	memset(page, 0, sizeof(struct page_info_t));

	// just to be sure
	page->title[0] = 0;
	page->body_len = 0;
	page->body = NULL;
	page->state = WK_STATE_BODY;
}

void page_destroy(struct page_info_t *page) {
	page->state = WK_STATE_EMPTY;
}

void parse_el_start(void *data, const char *el, const char **attr) {
	struct wkxml_parser_t *i = (struct wkxml_parser_t *)data;
	i->depth++;

	if (i->page.state != WK_STATE_EMPTY) {
		/* parsing of teh text nodes */
		if ((i->depth == 3) && (strcmp(el, "title") == 0))
			i->page.state = WK_STATE_TITLE;
	} else if ((i->depth == 2) && (strcmp(el, "page") == 0)) {
		page_init(&i->page);
		i->page.position = get_pos(i);
	}
}

void parse_el_end(void *data, const char *el) {
	struct wkxml_parser_t *i = (struct wkxml_parser_t *)data;
	i->depth--;

	if (i->page.state == WK_STATE_EMPTY) return;

	if ((i->depth == 1) && (strcmp(el, "page")) == 0) {
		struct page_info_t *page = &i->page;

		page->size = XML_GetCurrentByteCount(i->xmlp) + get_pos(i) - page->position;

		if (i->page_handler) {
			i->page_handler(page, i->extra);
		}

		page_destroy(page);
		return;
	}

	if ((i->depth == 2) && (strcmp(el, "title")) == 0)
		i->page.state = WK_STATE_BODY;
}

#define MIN(a, b) ((a)<(b)?(a):(b))

void parse_el_text(void *data, const XML_Char *txt, int len) {
	struct wkxml_parser_t *i = (struct wkxml_parser_t *)data;
	struct page_info_t *page = &i->page;
	if (page == NULL) return;

	if (page->state == WK_STATE_TITLE) {
		strncat(page->title, txt, MIN(len, sizeof(page->title) - 1)); /* txt isn't zero terminated */
	}
}

void wkxml_init(struct wkxml_parser_t *i) {
	i->xmlp = XML_ParserCreate(NULL);
	i->page.state = WK_STATE_EMPTY;
	i->page_handler = NULL;
	i->depth = 0;

	if (! i->xmlp) {
		fprintf(stderr, "Couldn't allocate memory for expat.\n");
		exit(EXIT_FAILURE);
	}

	XML_SetElementHandler(i->xmlp, parse_el_start, parse_el_end);
	XML_SetCharacterDataHandler(i->xmlp, parse_el_text);
	XML_SetUserData(i->xmlp, i);
}

void wkxml_destroy(struct wkxml_parser_t *i) {
	XML_ParserFree(i->xmlp);
	page_destroy(&i->page);
}

void wkxml_parse(struct wkxml_parser_t *i, char *data, uint64_t size, int done) {
	if (! XML_Parse(i->xmlp, data, size, done)) {
		if (XML_GetErrorCode(i->xmlp) == XML_ERROR_NO_ELEMENTS) {
			fprintf(stderr, "Premature ending of xml stream: %s\n",
				XML_ErrorString(XML_GetErrorCode(i->xmlp)));
		} else {
			fprintf(stderr, "XMLParse error: %s\n",
				XML_ErrorString(XML_GetErrorCode(i->xmlp)));

			exit(EXIT_FAILURE);
		}
	}
}

