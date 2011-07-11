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

void parse_el_start(void *data, const char *el, const char **attr) {
	struct bzxml_parser_t *i = (struct bzxml_parser_t *)data;
	i->depth++;

	if (i->page) {
		/* parsing of teh text nodes */
		struct page_info_t *page = i->page;
		if ((i->depth == 3) && (strcmp(el, "title") == 0))
			i->page->state = WK_STATE_TITLE;
	} else if ((i->depth == 2) && (strcmp(el, "page") == 0)) {
		uint64_t pos = get_pos(i);
		struct page_info_t *page = malloc(sizeof(struct page_info_t));
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

		free(page);
		i->page = NULL;

		return;
	}

	if ((i->depth == 2) && (strcmp(el, "title")) == 0)
		i->page->state = WK_STATE_EMPTY;

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
	XML_SetUserData(i->xmlp, i);
}

void bzxml_parse(struct bzxml_parser_t *i, char *data, uint64_t size, int done) {
	if (! XML_Parse(i->xmlp, data, size, done)) {
		if (XML_GetErrorCode(i->xmlp) == XML_ERROR_NO_ELEMENTS) {
			fprintf(stderr, "Premature ending of xml stream: %s\n",
				//XML_GetCurrentLineNumber(i->xmlp),
				XML_ErrorString(XML_GetErrorCode(i->xmlp)));
		} else {
			fprintf(stderr, "XMLParse error: %s\n",
				//XML_GetCurrentLineNumber(i->xmlp),
				XML_ErrorString(XML_GetErrorCode(i->xmlp)));

			exit(EXIT_FAILURE);
		}
	}
}

void bzxml_destroy(struct bzxml_parser_t *i) {
	XML_ParserFree(i->xmlp);
	if (i->page)
		free(i->page);
}

struct extra_t  {
	uint64_t total_bits;
	uint32_t articles;
};

void print_part_handler(struct bz_part_t *p, void *extra) {
	struct extra_t *ei =  (struct extra_t *)extra;

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
	struct extra_t *ei =  (struct extra_t *)extra;
	ei->articles++;

	fprintf(stderr, "Found and parsed page at global=%ld relative=%ld size=%ld\n",
		p->offset + p->source->byte_offset,
		p->offset,
		p->size);
}

struct bz_part_t *parse_parts(struct buffer_t *buf) {
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

	struct extra_t extra;
	extra.total_bits = buf->size * 8;
	extra.articles = 0;

	struct bzxml_parser_t parser;
	memset(&parser, 0, sizeof(struct bzxml_parser_t));
	bzxml_init(&parser);
	parser.extra = &extra;
	parser.page_handler = print_page_handler;
	parser.part_handler = print_part_handler;
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
				//if (++part_cnt == 2) { free(next); next = NULL; }
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

struct bz_part_t *find_parts(struct buffer_t *buf) {
	struct bz_part_t *root = bz_find_part(buf, 0);
	struct bz_part_t *part = root;

	if (!root) {
		fprintf(stderr, "Empty file?!");
		return NULL;
	};

	struct extra_t extra;
	extra.total_bits = buf->size * 8;
	extra.articles = 0;

	while (part) {
		struct bz_part_t *next = bz_find_part(buf, part->end);
		print_part_handler(part, &extra);

		part->next = next;
		part = next;
	}

	return root;
}

int main(int argc, char **argv) {
	struct buffer_t buf;
	char *fn = "enwiki-20110526-pages-articles1.xml.bz2";
	//char *fn = "/volumes/archive/torrents/enwiki-20110526-pages-articles.xml.bz2";
	buffer_open(&buf, fn, "r");

	struct bz_part_t *root = parse_parts(&buf);
	//struct bz_part_t *root = find_parts(&buf);


	bz_free_parts(root);
	buffer_close(&buf);

}
