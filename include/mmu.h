#ifndef ZOS_MMU_H
#define ZOS_MMU_H

#include "types.h"

void mmu_init(void);
uint64_t *mmu_create_user_tables(uint64_t phys_addr, size_t size);
uint64_t *mmu_fork_tables(uint64_t *parent_l1);
int mmu_handle_cow(uint64_t *l1, uint64_t fault_addr);
void mmu_free_user_tables(uint64_t *l1);

#endif
