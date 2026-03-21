#ifndef ZOS_MM_H
#define ZOS_MM_H

#include "types.h"

#define PAGE_SIZE 4096

void  mm_init(void);
void *page_alloc(void);
void  page_free(void *addr);

/* Reference counting for COW */
void  page_ref(void *addr);
void  page_unref(void *addr);
int   page_get_ref(void *addr);

#endif
