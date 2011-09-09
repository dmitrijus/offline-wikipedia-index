#ifndef _WKINDEX_H
#define _WKINDEX_H

#ifdef __cplusplus
extern "C" {
#endif 

#include "wkparse.h"

struct wkindex_t {
	void *obj;
};

struct wkreader_t {
	void *obj;
};

struct wk_page_entry_t {
	// search info
	char *id; // unique id
	char *title;
	
	// source info
	char *fn;
	uint64_t bit_offset;
	uint64_t byte_offset;
	uint64_t byte_count;;
};

struct wk_title_match_t {
	struct wk_title_match_t *next;

	struct wk_page_entry_t page;
};

int wkindex_init(struct wkindex_t *_x, char *db);
int wkindex_destroy(struct wkindex_t *_x);
void wkindex_add_page(struct wkindex_t *_x, struct wk_page_entry_t *page);

int wkreader_init(struct wkreader_t *_x, char *db);
int wkreader_destroy(struct wkreader_t *_x);
struct wk_title_match_t *wkreader_match_query(struct wkreader_t *_x, char *query, int limit);
struct wk_title_match_t *wkreader_match_title(struct wkreader_t *_x, char *title);

void wk_title_match_free(struct wk_title_match_t *f);

#ifdef __cplusplus
}
#endif

#endif
