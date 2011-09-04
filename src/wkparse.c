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

	page->title = malloc(1024*sizeof(XML_Char));
	page->title[0] = 0;
	page->title_len = 1024;

	page->body = malloc(4096*sizeof(XML_Char));
	page->body[0] = 0;
	page->body_len = 4096;
}

void page_destroy(struct page_info_t *page) {
	free(page->title);
	free(page->body);
}

void parse_el_start(void *data, const char *el, const char **attr) {
	struct wkxml_parser_t *i = (struct wkxml_parser_t *)data;
	i->depth++;

	if (i->page) {
		/* parsing of teh text nodes */
		struct page_info_t *page = i->page;

		if ((i->depth == 3) && (strcmp(el, "title") == 0))
			page->state = WK_STATE_TITLE;
	} else if ((i->depth == 2) && (strcmp(el, "page") == 0)) {
		struct page_info_t *page = malloc(sizeof(struct page_info_t));
		page_init(page);
		page->position = get_pos(i);

		i->page = page;
	}
}

void parse_el_end(void *data, const char *el) {
	struct wkxml_parser_t *i = (struct wkxml_parser_t *)data;
	i->depth--;

	if (i->page == NULL) return;

	if ((i->depth == 1) && (strcmp(el, "page")) == 0) {
		struct page_info_t *page = i->page;

		page->size = XML_GetCurrentByteCount(i->xmlp) + get_pos(i) - page->position;

		if (i->page_handler) {
			i->page_handler(page, i->extra);
		}

		page_destroy(page);
		free(page);
		i->page = NULL;

		return;
	}

	if ((i->depth == 2) && (strcmp(el, "title")) == 0)
		i->page->state = WK_STATE_EMPTY;
}

void parse_el_text(void *data, const XML_Char *txt, int len) {
	struct wkxml_parser_t *i = (struct wkxml_parser_t *)data;
	struct page_info_t *page = i->page;
	if (page == NULL) return;

	if (page->state == WK_STATE_TITLE) {
		int csize = strlen(page->title);

		if ((len + csize + 1) >= page->title_len) {
			uint32_t nsize = page->title_len;
			while (nsize < (len + csize + 1)) nsize += 1024;

			page->title = realloc(page->title, nsize);
			page->title_len = nsize;
		}

		strncat(page->title, txt, len); /* txt isn't zero terminated */
	}
}

void wkxml_init(struct wkxml_parser_t *i) {
	i->xmlp = XML_ParserCreate(NULL);
	i->page = NULL;
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
	if (i->page) {
		page_destroy(i->page);
		free(i->page);
	}
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

