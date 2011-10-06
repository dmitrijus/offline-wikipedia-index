#include <string.h>
#include <stdio.h>
#include <expat.h>
#include <assert.h>
#include <getopt.h>
#include <libgen.h>
#include <inttypes.h>

#include "bzextract.h"
#include "wkparse.h"
#include "wkindex.h"

// we need a temporal queue to hold our source position
struct source_list_t {
	struct source_list_t *next;

    struct bze_part_t src;
    uint64_t src_offset; // output started at this byte
};

struct parser_extra_t  {
    struct wkindex_t index;

    uint64_t articles;
    uint64_t total;

	struct source_list_t *root;
};

// structure to hold command line arguments;
struct main_opts_t {
	int max_results;
	char print_article;

	char *base_dir;
	char *db_path;
};

static struct main_opts_t defaults;

// if possible, remove the top element from source_list, so the next would represent the _current_ source chunk
void deqeue_root(struct parser_extra_t *ei, uint64_t pos) {
	assert(ei->root);

	if ((ei->root->next) && (ei->root->next->src_offset <= pos)) {
		struct source_list_t *tmp = ei->root;
		ei->root = ei->root->next;
		free(tmp);

		deqeue_root(ei, pos);
	}
}

void print_page_handler(struct page_info_t *pg, void *extra) {
    struct parser_extra_t *ei =  (struct parser_extra_t *)extra;
    ei->articles++;

	deqeue_root(ei, pg->position);
	
	struct wk_page_entry_t p;
	p.fn = basename(ei->root->src.src_fn);
	p.bit_offset = ei->root->src.src_start;
	p.byte_offset = pg->position - ei->root->src_offset;
	p.byte_count = pg->size;

    char pn[1024]; pn[0] = 0;
    snprintf(pn, 1024, "%s:%"PRIu64":%"PRIu64":%"PRIu64":",
        p.fn, p.bit_offset, p.byte_offset, p.byte_count);

	p.title = pg->title;
	p.id = pn;

/*
    fprintf(stderr, "found article at: byte=%ld offset=%ld title=%s\n", 
        p->position,
        p->position - ei->root->src_offset,
        p->title
    );
*/

    wkindex_add_page(&ei->index, &p);
}

#define XML_BUF_SIZE 1024

int index_file(FILE *in) {
    struct parser_extra_t extra;
    extra.articles = 0;
    extra.total = 0;
	extra.root = NULL;
    wkindex_init(&extra.index, defaults.db_path);

    struct wkxml_parser_t parser;
    wkxml_init(&parser);
    parser.page_handler = print_page_handler;
    parser.extra = &extra;

	struct source_list_t *previous_el = NULL;
	struct source_list_t *current_el = NULL;

    for (;;) {
		previous_el = current_el;
		current_el = malloc(sizeof(struct source_list_t));
		memset(current_el, 0, sizeof(struct source_list_t));

		if (previous_el) {
			previous_el->next = current_el;
		}

        int r = fread(&current_el->src, sizeof(struct bze_part_t), 1, in);

        if (r != 1) {
            if (feof(in)) {
                break;
            } else {
                perror("error reading file:");
                exit(EXIT_FAILURE);
            }
        }

        if (current_el->src.magic != BZE_PART_MAGIC) {
            perror("received invalid magic:");
            exit(EXIT_FAILURE);
        }

        // fprintf(stderr, "vread: magic=%lx start=%lu size=%u\n",
        //    current.magic, current.src_start, current.dst_size);

        uint32_t left = current_el->src.dst_size;
        char xml_buf[XML_BUF_SIZE];

		if (!extra.root) {
			extra.root = current_el;
		}
        current_el->src_offset = extra.total;

        while (left > 0) {
            uint32_t vread = left;
            if (left > XML_BUF_SIZE)
                vread = XML_BUF_SIZE;

            uint32_t have_read = fread(xml_buf, sizeof(char), vread, in);
            if (have_read <= 0) {
                perror("premature stream end:");
                exit(EXIT_FAILURE);
            }

            left -= have_read;
            wkxml_parse(&parser, xml_buf, have_read, 0);
        }
        extra.total += current_el->src.dst_size;

        fprintf(stderr, "so far parsed %"PRIu64" articles [%"PRIu64" bytes].\n", extra.articles, extra.total);
    }

	while (extra.root) {
		struct source_list_t *tmp = extra.root;
		extra.root = tmp->next;
		free(tmp);
	}

    wkxml_destroy(&parser);
    wkindex_destroy(&extra.index);

	return 0;
}


/*
 * handling articles paths
 * extacting xml
 * */

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

int extract_xml(char *path_id) {
    char *token = path_id;
    char fn[1024];

    uint64_t offset;
    uint64_t pg_offset;
    uint64_t pg_size;

    token = extract_char(token, fn, 1000);
    token = extract_uint64(token, &offset);
    token = extract_uint64(token, &pg_offset);
    token = extract_uint64(token, &pg_size);

//    fprintf(stderr, "give: %s:%ld:%ld:%ld:dontcare\n",
//        fn,offset,pg_offset, pg_size);

	char real_fn[1024];
	snprintf(real_fn, 1000, "%s/%s", defaults.base_dir, fn);

	char *out = bze_extract_string(real_fn, offset, pg_offset, pg_size);

	fputs(out, stdout);

	free(out);
	return 0;
}

int query_string(char *str) {
	struct wkreader_t r;
	struct wk_title_match_t *m;

	wkreader_init(&r, defaults.db_path);
	m = wkreader_match_query(&r, str, defaults.max_results);

	wk_title_match_free(m);
	wkreader_destroy(&r);

	return 0;
}

int query_title(char *str) {
	struct wkreader_t r;
	struct wk_title_match_t *m;

	wkreader_init(&r, defaults.db_path);
	m = wkreader_match_title(&r, str);

	wk_title_match_free(m);
	wkreader_destroy(&r);

	return 0;
}

void help(char *name) {
    fprintf(stderr, "%s version v0.1\n", name);
    fprintf(stderr, "usage: %s -i file | -h | -v\noptions:\n", name);
    fprintf(stderr, "\t -i          \tindex tagged xml from stdin (output from bzextract -m).\n");
    fprintf(stderr, "\t -e \"art_id\"\textract xml dump for given article id.\n");
    fprintf(stderr, "\t -q \"query\" \tquery teh database.\n");
    fprintf(stderr, "\t -t \"query\" \tquery teh database for given title, output first document found.\n");
    fprintf(stderr, "\t -c count     \tnumber of results to display (for -q).\n");
    fprintf(stderr, "\t -d           \tdisplay the first article (for -q and -t).\n");
    fprintf(stderr, "\t -p path      \tdb path, default: \"./db/\".\n");
    fprintf(stderr, "\t -b path      \tbase dir for opening files (including trailing slash), default: \"./\".\n");

}

int main(int argc, char **argv) {
    int c;

	defaults.max_results = 32;
	defaults.print_article = 0;
	defaults.base_dir = "./";
	defaults.db_path = "./db";

    while ((c = getopt (argc, argv, "vhide:q:t:c:p:b:")) != -1) {
        switch (c) {
            case 'v':
                help(argv[0]);
                return 0;
            case 'h':
                help(argv[0]);
                return 0;
			case 'c':
				sscanf(optarg, "%d", &defaults.max_results);
				break;
			case 'd':
				defaults.print_article = 1;
				break;
			case 'p':
				defaults.db_path = optarg;
				break;
			case 'b':
				defaults.base_dir = optarg;
				break;
            case 'i':
                return index_file(stdin);
            case 'e':
                return extract_xml(optarg);
			case 'q':
				return query_string(optarg);
			case 't':
				return query_title(optarg);
            default:
                help(argv[0]);
                return 1;
        }
    }

    help(argv[0]);
    return 1;
}
