#include "bzpartial.h"
#include "bzparse.h"

#include <string.h>
#include <stdio.h>
#include <expat.h>
#include <assert.h>

#define MEDIAWIKI_HEAD "<mediawiki lang=\"en\">"

uint64_t get_pos(struct bzxml_parser_t *i) {
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
	struct bzxml_parser_t *i = (struct bzxml_parser_t *)data;
	i->depth++;

	if (i->page) {
		/* parsing of teh text nodes */
		struct page_info_t *page = i->page;

		if ((i->depth == 3) && (strcmp(el, "title") == 0))
			page->state = WK_STATE_TITLE;
	} else if ((i->depth == 2) && (strcmp(el, "page") == 0)) {
		uint64_t pos = get_pos(i);

		struct page_info_t *page = malloc(sizeof(struct page_info_t));
		page_init(page);
		i->page = page;

		if ((i->part->next) && (pos >= i->part->next->byte_offset)) {
			i->part = i->part->next;
			i->page_init = 0;
		}

		if (!i->page_init) {
			i->part->page_offset = pos - i->part->byte_offset;
			i->page_init = 1;

			if (i->page_handler) {
				i->part_handler(i->part, i->extra);
			}
		}

		page->offset = pos - i->part->byte_offset;
		page->source = i->part;
	}
}

void parse_el_end(void *data, const char *el) {
	struct bzxml_parser_t *i = (struct bzxml_parser_t *)data;
	i->depth--;

	if (i->page == NULL) return;

	if ((i->depth == 1) && (strcmp(el, "page")) == 0) {
		struct page_info_t *page = i->page;

		uint64_t pos = get_pos(i);
		page->size = pos + XML_GetCurrentByteCount(i->xmlp) - page->offset - page->source->byte_offset;

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
	struct bzxml_parser_t *i = (struct bzxml_parser_t *)data;
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

void bzxml_init(struct bzxml_parser_t *i) {
	i->xmlp = XML_ParserCreate(NULL);
	i->page = NULL;
	i->page_handler = NULL;
	i->page_init = 0;

	if (! i->xmlp) {
		fprintf(stderr, "Couldn't allocate memory for expat.\n");
		exit(EXIT_FAILURE);
	}

	XML_SetElementHandler(i->xmlp, parse_el_start, parse_el_end);
	XML_SetCharacterDataHandler(i->xmlp, parse_el_text);
	XML_SetUserData(i->xmlp, i);
}

void bzxml_parse(struct bzxml_parser_t *i, char *data, uint64_t size, int done) {
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

void bzxml_destroy(struct bzxml_parser_t *i) {
	XML_ParserFree(i->xmlp);
	if (i->page) {
		page_destroy(i->page);
		free(i->page);
	}
}

struct bz_part_t *traverse_pages(
	struct buffer_t *buf,
	void (*page_handler)(struct page_info_t *, void *),
	void (*part_handler)(struct bz_part_t*, void *),
	void *extra) 
{
	struct bz_part_t *root = bz_find_part(buf, 0);
	struct bz_part_t *part = root;

	if (!root) {
		fprintf(stderr, "Empty file?!");
		return NULL;
	}

	/* calculate absolute decompressed byte offsets,
	 * bzip2 blocks are guaranteed to align to bytes */
	uint64_t sum = 0;
	uint64_t part_cnt = 0;

	part->byte_offset = 0;
	part->page_offset = -1; /* nuo nulio */

	struct bzxml_parser_t parser;
	memset(&parser, 0, sizeof(struct bzxml_parser_t));
	bzxml_init(&parser);

	parser.extra = extra;
	parser.page_handler = page_handler;
	parser.part_handler = part_handler;
	parser.part = part;

	struct bz_decoder_t *d = malloc(sizeof(struct bz_decoder_t));
	bz_decoder_init(d, buf, part);
	for (;;) {
		char dbuf[1024*1024];
		uint64_t ret = bz_decode(d, dbuf, 1024*1024);
		sum += ret;

		if (ret == 0) {
			assert(!part->next);

			struct bz_part_t *next = bz_find_part(buf, part->end);

			if (next) {
				next->byte_offset = sum;
				part_cnt++;
				
				//if (part_cnt == 2) { free(next); next = NULL; }
			}

			part->next = next;
			part = next;

			if (part) {
				bz_decoder_destroy(d);
				bz_decoder_init(d, buf, part);
			} else {
				bzxml_parse(&parser, dbuf, 0, 1);
				break;
			}
		} else {
			bzxml_parse(&parser, dbuf, ret, 0);
		}
	}

	bz_decoder_destroy(d);
	free(d);
	bzxml_destroy(&parser);

	return root;
}

struct bz_part_t *traverse_parts_only(
	struct buffer_t *buf,
	void (*part_handler)(struct bz_part_t*, void *),
	void *extra) 
{
	struct bz_part_t *root = bz_find_part(buf, 0);
	struct bz_part_t *part = root;

	if (!root) {
		fprintf(stderr, "Empty file?!");
		return NULL;
	};

	while (part) {
		struct bz_part_t *next = bz_find_part(buf, part->end);
		part_handler(part, extra);

		part->next = next;
		part = next;
	}

	return root;
}

uint64_t extract_page(
	struct buffer_t *buf, uint64_t bit_offset,
	char *dst, uint64_t dst_size)
{
	struct bz_part_t *root = bz_find_part(buf, bit_offset);
	struct bz_part_t *part = root;
	
	struct bz_decoder_t *d = malloc(sizeof(struct bz_decoder_t));
	bz_decoder_init(d, buf, part);
	uint64_t total = 0;

	for (;;) {  /* read till offset */
		if (dst_size == 0)
			break;

		uint64_t ret = bz_decode(d, dst, dst_size);

		if (ret == 0) {
			struct bz_part_t *next = bz_find_part(buf, part->end);

			part->next = next;
			part = next;

			if (part) {
				bz_decoder_destroy(d);
				bz_decoder_init(d, buf, part);
			} else {
				/* error */
				break;
			}
		} else {
			dst_size -= ret;
			dst += ret;
			total += ret;
		}
	}

	bz_decoder_destroy(d);
	bz_free_parts(root);
	free(d);

	return total;
}

