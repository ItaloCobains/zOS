#ifndef ZOS_MMU_H
#define ZOS_MMU_H

#include "types.h"

void mmu_init(void);
uint64_t *mmu_create_user_tables(uint64_t phys_addr, size_t size);

#endif
