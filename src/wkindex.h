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

struct wk_title_match_t {
	struct wk_title_match_t *next;

	char *title;
};

int wkindex_init(struct wkindex_t *_x, char *db);
int wkindex_destroy(struct wkindex_t *_x);
void wkindex_add_page(struct wkindex_t *_x, char *doc, struct page_info_t *page);

int wkreader_init(struct wkreader_t *_x, char *db);
int wkreader_destroy(struct wkreader_t *_x);
struct wk_title_match_t *wkreader_match(struct wkreader_t *_x, char *query);


#ifdef __cplusplus
}
#endif

#endif
