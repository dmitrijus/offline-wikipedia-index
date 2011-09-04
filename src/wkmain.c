#include <string.h>
#include <stdio.h>
#include <expat.h>
#include <assert.h>
#include <getopt.h>

#include "bzextract.h"
#include "wkparse.h"
#include "wkindex.h"

struct parser_extra_t  {
    struct bze_part_t *src;
    struct wkindex_t index;

    uint64_t articles;
    uint64_t total;
    uint64_t byte_offset; // output started at this byte
};

void create_name(struct parser_extra_t *ei, struct page_info_t *p, char *buf, int buf_len) {
    struct bze_part_t *part = ei->src;

    snprintf(buf, buf_len, "%s:%ld:%ld:%ld:%s",
        part->src_fn,
        part->src_start,
        p->position - ei->byte_offset,
        p->size,
        p->title
    );
}

void print_page_handler(struct page_info_t *p, void *extra) {
    struct parser_extra_t *ei =  (struct parser_extra_t *)extra;
    ei->articles++;

    char pn[1024]; pn[0] = 0;
    create_name(ei, p, pn, 1023);

    fprintf(stderr, "found article at: byte=%ld offset=%ld title=%s\n", 
        p->position,
        p->position - ei->byte_offset,
        p->title
    );

    wkindex_add_page(&ei->index, pn, p);
}

#define XML_BUF_SIZE 1024

void index_file(FILE *in) {
    struct bze_part_t current;

    struct parser_extra_t extra;
    extra.articles = 0;
    extra.total = 0;
    wkindex_init(&extra.index, "db/");

    struct wkxml_parser_t parser;
    wkxml_init(&parser);
    parser.page_handler = print_page_handler;
    parser.extra = &extra;

    for (;;) {
        int r = fread(&current, sizeof(struct bze_part_t), 1, in);

        if (r != 1) {
            if (feof(in)) {
                break;
            } else {
                perror("error reading file:");
                exit(EXIT_FAILURE);
            }
        }

        if (current.magic != BZE_PART_MAGIC) {
            perror("received invalid magic:");
            exit(EXIT_FAILURE);
        }

        // fprintf(stderr, "vread: magic=%lx start=%lu size=%u\n",
        //    current.magic, current.src_start, current.dst_size);

        uint32_t left = current.dst_size;
        char xml_buf[XML_BUF_SIZE];

        extra.src = &current;
        extra.byte_offset = extra.total;

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
        extra.total += current.dst_size;

        fprintf(stderr, "so far parsed %ld articles [%ld bytes].\n", extra.articles, extra.total);
    }

    wkxml_destroy(&parser);
    wkindex_destroy(&extra.index);
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

    struct bze_options_t opts;
    opts.start_bit = offset;
    opts.seek_bytes = pg_offset;
    opts.stop_bytes = pg_size;
    opts.fn = fn;

    //bze_extract_data(&opts);

    /*
    struct buffer_t buf;
    buffer_open(&buf, fn, "r");
    
    char *xml_buf = malloc((pg_offset + pg_size + 1) * sizeof(char));
    extract_page(&buf, offset - 48, xml_buf, pg_offset + pg_size);

    xml_buf[pg_offset + pg_size] = 0;
    printf("%s\n", &xml_buf[pg_offset]);
    */
};

void help(char *name) {
    fprintf(stderr, "%s version v0.1\n", name);
    fprintf(stderr, "usage: %s -i file | -h | -v\noptions:\n", name);
    fprintf(stderr, "\t -i          \tindex tagged xml from stdin (output from bzextract -m).\n");
    fprintf(stderr, "\t -e article_id\textract xml dump for given article id.\n");
}

int main(int argc, char **argv) {
    int c;

    while ((c = getopt (argc, argv, "vhie:")) != -1) {
        switch (c) {
            case 'v':
                help(argv[0]);
                return 0;
            case 'h':
                help(argv[0]);
                return 0;
            case 'i':
                index_file(stdin);
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
