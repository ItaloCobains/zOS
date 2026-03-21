/*
 * mmu.c -- MMU setup with translation tables for aarch64.
 *
 * AArch64 uses a 4-level page table (L0 -> L3) for 4KB granule.
 * Virtual address bits [47:0] are split:
 *   [47:39] = L0 index (9 bits, 512 entries)
 *   [38:30] = L1 index (9 bits, 512 entries, each maps 1GB)
 *   [29:21] = L2 index (9 bits, 512 entries, each maps 2MB)
 *   [20:12] = L3 index (9 bits, 512 entries, each maps 4KB)
 *   [11:0]  = page offset
 *
 * For the kernel we use 2MB block mappings (L2 level) which is
 * simpler -- no L3 tables needed. This maps our 128MB RAM region
 * and the UART device region.
 *
 * For userspace we create full 4KB page mappings (L3 level) so we
 * can map individual pages with proper permissions.
 *
 * TTBR1_EL1 = kernel page tables (upper VA space, addresses >= 0xFFFF...)
 *   -- We use identity mapping for simplicity: TTBR0 maps low addresses
 *      for both kernel and user, and we switch TTBR0 per-task.
 *   -- Actually, for simplicity in this educational OS, we keep the
 *      kernel identity-mapped in TTBR0 and swap TTBR0 on task switch.
 *      Kernel runs with its own TTBR0 that identity-maps all RAM + devices.
 */

#include "types.h"
#include "mm.h"
#include "mmu.h"
#include "uart.h"

/* Page table entry flags */
#define PT_VALID        (1UL << 0)   /* Entry is valid */
#define PT_TABLE        (1UL << 1)   /* Points to next-level table (not block) */
#define PT_BLOCK        (0UL << 1)   /* Block entry (L1/L2 only) */
#define PT_PAGE         (1UL << 1)   /* Page entry (L3 only) */
#define PT_AF           (1UL << 10)  /* Access Flag -- must be set */
#define PT_SH_INNER     (3UL << 8)   /* Inner shareable */
#define PT_ATTR_NORMAL  (0UL << 2)   /* MAIR index 0: normal memory */
#define PT_ATTR_DEVICE  (1UL << 2)   /* MAIR index 1: device memory */

/* Access permissions */
#define PT_AP_RW_EL1    (0UL << 6)   /* EL1 read/write, EL0 no access */
#define PT_AP_RW_ALL    (1UL << 6)   /* EL1 and EL0 read/write */
#define PT_AP_RO_EL1    (2UL << 6)   /* EL1 read-only, EL0 no access */
#define PT_AP_RO_ALL    (3UL << 6)   /* EL1 and EL0 read-only */

#define PT_UXN          (1UL << 54)  /* Unprivileged eXecute Never */
#define PT_PXN          (1UL << 53)  /* Privileged eXecute Never */

/* MAIR register configuration */
#define MAIR_ATTR_NORMAL  0xFF  /* Write-back, write-allocate, inner/outer */
#define MAIR_ATTR_DEVICE  0x00  /* Device-nGnRnE (strongly ordered) */

/* Addresses we need to map */
#define DEVICE_BASE  0x00000000  /* Device MMIO region (0x00000000 - 0x40000000) */
#define RAM_BASE     0x40000000
#define RAM_SIZE     (128 * 1024 * 1024)

/* Kernel page tables -- statically allocated, page-aligned */
static uint64_t kernel_l1[512] __attribute__((aligned(4096)));
static uint64_t kernel_l2_device[512] __attribute__((aligned(4096)));
static uint64_t kernel_l2_ram[512] __attribute__((aligned(4096)));

/*
 * Set up kernel page tables and enable the MMU.
 *
 * We create an identity mapping (VA = PA) for:
 *   - Device region (0x00000000 - 0x3FFFFFFF): 1GB as device memory
 *   - RAM region (0x40000000 - 0x47FFFFFF): 128MB as normal memory
 */
void mmu_init(void)
{
    /* Zero all tables */
    for (int i = 0; i < 512; i++) {
        kernel_l1[i] = 0;
        kernel_l2_device[i] = 0;
        kernel_l2_ram[i] = 0;
    }

    /*
     * L1 table: each entry covers 1GB.
     * Entry 0 -> L2 table for devices (0x00000000 - 0x3FFFFFFF)
     * Entry 1 -> L2 table for RAM     (0x40000000 - 0x7FFFFFFF)
     */
    kernel_l1[0] = (uint64_t)kernel_l2_device | PT_VALID | PT_TABLE;
    kernel_l1[1] = (uint64_t)kernel_l2_ram    | PT_VALID | PT_TABLE;

    /*
     * L2 device table: 2MB blocks of device memory.
     * We map the entire first GB as device memory. This covers
     * the GIC (0x08000000) and UART (0x09000000).
     */
    for (int i = 0; i < 512; i++) {
        kernel_l2_device[i] = (uint64_t)(i * 0x200000)
            | PT_VALID | PT_BLOCK | PT_AF
            | PT_ATTR_DEVICE
            | PT_AP_RW_EL1
            | PT_UXN | PT_PXN;
    }

    /*
     * L2 RAM table: 2MB blocks of normal memory.
     * 128MB = 64 blocks of 2MB each.
     */
    for (int i = 0; i < 64; i++) {
        kernel_l2_ram[i] = (uint64_t)(RAM_BASE + i * 0x200000)
            | PT_VALID | PT_BLOCK | PT_AF
            | PT_ATTR_NORMAL
            | PT_SH_INNER
            | PT_AP_RW_EL1
            | PT_UXN;
    }

    /*
     * Configure system registers and enable the MMU.
     *
     * MAIR_EL1: Memory Attribute Indirection Register.
     *   Attr0 = 0xFF (normal, write-back)
     *   Attr1 = 0x00 (device, nGnRnE)
     */
    uint64_t mair = (MAIR_ATTR_NORMAL << 0) | (MAIR_ATTR_DEVICE << 8);
    __asm__ volatile("msr mair_el1, %0" : : "r"(mair));

    /*
     * TCR_EL1: Translation Control Register.
     *   T0SZ = 25   -> 39-bit VA space (512GB), but we only use low bits
     *   IRGN0 = 01  -> write-back, write-allocate (inner)
     *   ORGN0 = 01  -> write-back, write-allocate (outer)
     *   SH0 = 11    -> inner shareable
     *   TG0 = 00    -> 4KB granule
     *   T1SZ = 25   -> (not used, but set symmetrically)
     */
    uint64_t tcr = (25UL << 0)    /* T0SZ */
                 | (1UL << 8)     /* IRGN0: write-back */
                 | (1UL << 10)    /* ORGN0: write-back */
                 | (3UL << 12)    /* SH0: inner shareable */
                 | (0UL << 14)    /* TG0: 4KB */
                 | (25UL << 16)   /* T1SZ */
                 | (1UL << 24)    /* IRGN1 */
                 | (1UL << 26)    /* ORGN1 */
                 | (3UL << 28)    /* SH1 */
                 | (2UL << 30);   /* TG1: 4KB (encoded as 2) */
    __asm__ volatile("msr tcr_el1, %0" : : "r"(tcr));

    /* Set TTBR0_EL1 to our kernel L1 table */
    __asm__ volatile("msr ttbr0_el1, %0" : : "r"(kernel_l1));

    /* Invalidate all TLB entries */
    __asm__ volatile("tlbi vmalle1is");

    /* Ensure all writes are visible before enabling MMU */
    __asm__ volatile("dsb ish");
    __asm__ volatile("isb");

    /* Enable the MMU via SCTLR_EL1 */
    uint64_t sctlr;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1UL << 0);   /* M: enable MMU */
    sctlr |= (1UL << 2);   /* C: data cache enable */
    sctlr |= (1UL << 12);  /* I: instruction cache enable */
    __asm__ volatile("msr sctlr_el1, %0" : : "r"(sctlr));

    __asm__ volatile("isb");

    uart_puts("[mmu] MMU enabled with identity mapping\n");
}

/*
 * Create user-space page tables for a task.
 *
 * Maps `size` bytes of physical memory starting at `phys_addr`
 * to virtual address 0x00400000 (user text/data) with EL0 permissions.
 * Also allocates and maps a user stack at 0x00800000.
 *
 * Returns pointer to the L1 table (to be loaded into TTBR0_EL1).
 */
uint64_t *mmu_create_user_tables(uint64_t phys_addr, size_t size)
{
    /* Allocate L1, L2, L3 tables */
    uint64_t *l1 = (uint64_t *)page_alloc();
    uint64_t *l2 = (uint64_t *)page_alloc();
    uint64_t *l3_text = (uint64_t *)page_alloc();
    uint64_t *l3_stack = (uint64_t *)page_alloc();

    if (!l1 || !l2 || !l3_text || !l3_stack) {
        uart_puts("[mmu] ERROR: failed to allocate user page tables\n");
        return NULL;
    }

    /*
     * L1[0] -> L2 (covers 0x00000000 - 0x3FFFFFFF)
     * L2[2] -> L3_text (covers 0x00400000 - 0x005FFFFF)
     * L2[4] -> L3_stack (covers 0x00800000 - 0x009FFFFF)
     *
     * L3 entries map individual 4KB pages.
     */
    l1[0] = (uint64_t)l2 | PT_VALID | PT_TABLE;

    /* L2 entry for user text: VA 0x00400000 = L2 index 2 */
    l2[2] = (uint64_t)l3_text | PT_VALID | PT_TABLE;

    /* L2 entry for user stack: VA 0x00800000 = L2 index 4 */
    l2[4] = (uint64_t)l3_stack | PT_VALID | PT_TABLE;

    /* Map user text/data pages */
    size_t num_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (size_t i = 0; i < num_pages && i < 512; i++) {
        l3_text[i] = (phys_addr + i * PAGE_SIZE)
            | PT_VALID | PT_PAGE | PT_AF
            | PT_ATTR_NORMAL
            | PT_SH_INNER
            | PT_AP_RW_ALL;   /* EL0 can read/write/execute */
    }

    /* Allocate and map one page for user stack at top of stack region */
    void *stack_page = page_alloc();
    if (!stack_page) {
        uart_puts("[mmu] ERROR: failed to allocate user stack\n");
        return NULL;
    }

    /* Map at L3 index 0 -> VA 0x00800000 */
    l3_stack[0] = (uint64_t)stack_page
        | PT_VALID | PT_PAGE | PT_AF
        | PT_ATTR_NORMAL
        | PT_SH_INNER
        | PT_AP_RW_ALL
        | PT_UXN;  /* Stack is not executable */

    /*
     * We also need kernel mappings in this table so that when we're
     * handling exceptions in EL1, we can still access kernel memory.
     * L1[1] maps 0x40000000-0x7FFFFFFF (RAM) using kernel's L2 table.
     */
    l1[1] = (uint64_t)kernel_l2_ram | PT_VALID | PT_TABLE;

    /* Also map devices so kernel can access UART/GIC during exceptions */
    uint64_t *l2_dev = (uint64_t *)page_alloc();
    if (!l2_dev) {
        uart_puts("[mmu] ERROR: failed to allocate device L2\n");
        return NULL;
    }
    for (int i = 0; i < 512; i++) {
        l2_dev[i] = kernel_l2_device[i];
    }
    /* But mark device pages as EL1-only */
    l1[0] = (uint64_t)l2_dev | PT_VALID | PT_TABLE;
    /* Re-add user mappings into this new L2 */
    l2_dev[2] = (uint64_t)l3_text | PT_VALID | PT_TABLE;
    l2_dev[4] = (uint64_t)l3_stack | PT_VALID | PT_TABLE;

    return l1;
}

/* Mask to extract physical address from a page table entry */
#define PT_ADDR_MASK  0x0000FFFFFFFFF000UL
#define PT_AP_MASK    (3UL << 6)

/*
 * Fork page tables with COW.
 *
 * Creates a new set of page tables for the child process.
 * User pages (L3 entries) are shared with the parent but marked
 * read-only in BOTH parent and child. Physical page refcounts
 * are incremented. When either process writes, a page fault
 * triggers a copy (handled by mmu_handle_cow).
 */
uint64_t *mmu_fork_tables(uint64_t *parent_l1)
{
    uint64_t *child_l1 = (uint64_t *)page_alloc();
    if (!child_l1) return NULL;

    /* Copy kernel mappings directly */
    child_l1[1] = parent_l1[1];  /* kernel RAM */

    /* Get parent's L2 (device + user mappings) */
    uint64_t *parent_l2 = (uint64_t *)(parent_l1[0] & PT_ADDR_MASK);

    /* Allocate child's L2 -- copy device entries from parent */
    uint64_t *child_l2 = (uint64_t *)page_alloc();
    if (!child_l2) return NULL;

    for (int i = 0; i < 512; i++)
        child_l2[i] = parent_l2[i];

    child_l1[0] = (uint64_t)child_l2 | PT_VALID | PT_TABLE;

    /*
     * For each L3 table in the parent (user text and stack),
     * create a new L3 table in the child. Share the physical pages
     * but mark them read-only in BOTH parent and child.
     */
    int l2_indices[] = {2, 4};  /* L2 idx 2 = text (0x400000), idx 4 = stack (0x800000) */
    int num_l2 = 2;

    for (int li = 0; li < num_l2; li++) {
        int idx = l2_indices[li];

        if (!(parent_l2[idx] & PT_VALID))
            continue;

        uint64_t *parent_l3 = (uint64_t *)(parent_l2[idx] & PT_ADDR_MASK);
        uint64_t *child_l3 = (uint64_t *)page_alloc();
        if (!child_l3) return NULL;

        for (int i = 0; i < 512; i++) {
            if (!(parent_l3[i] & PT_VALID))
                continue;

            uint64_t phys = parent_l3[i] & PT_ADDR_MASK;

            /* Make entry read-only for COW */
            uint64_t entry = parent_l3[i];
            entry &= ~PT_AP_MASK;
            entry |= PT_AP_RO_ALL;  /* read-only for EL0 and EL1 */

            /* Mark parent as read-only too */
            parent_l3[i] = entry;

            /* Child gets same entry (read-only, same physical page) */
            child_l3[i] = entry;

            /* Increment physical page refcount */
            page_ref((void *)phys);
        }

        child_l2[idx] = (uint64_t)child_l3 | PT_VALID | PT_TABLE;
    }

    /* Flush TLB so parent sees the read-only change */
    __asm__ volatile("tlbi vmalle1is; dsb ish; isb");

    return child_l1;
}

/*
 * Handle a COW page fault.
 *
 * Called when a write to a read-only page causes a Data Abort.
 * If the page has refcount > 1, we allocate a new page, copy the
 * content, and remap as read-write. If refcount == 1, just remap
 * as read-write (we're the only owner).
 *
 * Returns 1 if COW was handled, 0 if not a COW fault.
 */
int mmu_handle_cow(uint64_t *l1, uint64_t fault_addr)
{
    /* Walk page tables to find the L3 entry */
    if (!(l1[0] & PT_VALID))
        return 0;

    uint64_t *l2 = (uint64_t *)(l1[0] & PT_ADDR_MASK);
    int l2_idx = (fault_addr >> 21) & 0x1FF;

    if (!(l2[l2_idx] & PT_VALID) || !(l2[l2_idx] & PT_TABLE))
        return 0;

    uint64_t *l3 = (uint64_t *)(l2[l2_idx] & PT_ADDR_MASK);
    int l3_idx = (fault_addr >> 12) & 0x1FF;

    if (!(l3[l3_idx] & PT_VALID))
        return 0;

    /* Check if this is a read-only page (COW candidate) */
    uint64_t entry = l3[l3_idx];
    uint64_t ap = entry & PT_AP_MASK;
    if (ap != PT_AP_RO_ALL)
        return 0;  /* Not read-only, not a COW fault */

    uint64_t old_phys = entry & PT_ADDR_MASK;
    int ref = page_get_ref((void *)old_phys);

    if (ref > 1) {
        /* Multiple owners: copy the page */
        void *new_page = page_alloc();
        if (!new_page) return 0;

        uint8_t *src = (uint8_t *)old_phys;
        uint8_t *dst = (uint8_t *)new_page;
        for (int i = 0; i < PAGE_SIZE; i++)
            dst[i] = src[i];

        /* Update L3 entry to point to new page, make writable */
        l3[l3_idx] = (uint64_t)new_page
            | (entry & ~(PT_ADDR_MASK | PT_AP_MASK))
            | PT_AP_RW_ALL;

        /* Decrement old page refcount */
        page_unref((void *)old_phys);
    } else {
        /* Only owner: just make it writable */
        l3[l3_idx] = (entry & ~PT_AP_MASK) | PT_AP_RW_ALL;
    }

    /* Flush TLB for the faulting address */
    __asm__ volatile("tlbi vmalle1is; dsb ish; isb");

    return 1;
}

/*
 * Free user page tables and decrement refcounts for mapped pages.
 * Used by exec() to release old address space.
 */
void mmu_free_user_tables(uint64_t *l1)
{
    if (!l1) return;

    uint64_t *l2 = (uint64_t *)(l1[0] & PT_ADDR_MASK);
    int l2_indices[] = {2, 4};

    for (int li = 0; li < 2; li++) {
        int idx = l2_indices[li];
        if (!(l2[idx] & PT_VALID) || !(l2[idx] & PT_TABLE))
            continue;

        uint64_t *l3 = (uint64_t *)(l2[idx] & PT_ADDR_MASK);
        for (int i = 0; i < 512; i++) {
            if (l3[i] & PT_VALID) {
                uint64_t phys = l3[i] & PT_ADDR_MASK;
                page_unref((void *)phys);
            }
        }
        page_free(l3);
    }

    page_free(l2);
    page_free(l1);
}
