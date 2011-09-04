#ifndef _WKINDEX_H
#define _WKINDEX_H

#ifdef __cplusplus
extern "C" {
#endif 

#include "wkparse.h"

struct wkindex_t {
	void *obj;
};

void wkindex_init(struct wkindex_t *_x, char *db);
void wkindex_destroy(struct wkindex_t *_x);

void wkindex_add_page(struct wkindex_t *_x, char *doc, struct page_info_t *page);

#ifdef __cplusplus
}
#endif

#endif
