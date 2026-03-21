/*
 * mm.c -- Physical page allocator.
 *
 * Uses a simple bitmap to track which 4KB pages are free or allocated.
 * Each bit in the bitmap represents one page:
 *   0 = free
 *   1 = allocated
 *
 * Free memory starts at _kernel_end (from linker script) and extends
 * to the end of RAM. QEMU virt gives us 128MB starting at 0x40000000.
 */

#include "types.h"
#include "mm.h"
#include "uart.h"

#define RAM_BASE    0x40000000
#define RAM_SIZE    (128 * 1024 * 1024)  /* 128 MB */
#define RAM_END     (RAM_BASE + RAM_SIZE)
#define MAX_PAGES   (RAM_SIZE / PAGE_SIZE)

/* Bitmap: one bit per page. Array of 64-bit words. */
static uint64_t bitmap[MAX_PAGES / 64];

/* Reference count per page for COW. 0 = free, 1 = one owner, 2+ = shared */
static uint8_t refcount[MAX_PAGES];

/* Where allocatable memory starts (page-aligned, after kernel image) */
static uint64_t mem_start;
static uint64_t total_pages;

/* Linker script symbol: end of kernel image */
extern char _kernel_end[];

void mm_init(void)
{
    /* Start after kernel image + 16KB stack (matches start.S) */
    mem_start = ((uint64_t)_kernel_end + 0x4000 + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    total_pages = (RAM_END - mem_start) / PAGE_SIZE;

    /* Mark all pages as free (zero the bitmap) */
    for (size_t i = 0; i < MAX_PAGES / 64; i++)
        bitmap[i] = 0;

    uart_puts("[mm] page allocator initialized\n");
    uart_puts("[mm] free memory: ");
    uart_puthex(mem_start);
    uart_puts(" - ");
    uart_puthex(RAM_END);
    uart_puts(" (");
    uart_puthex(total_pages);
    uart_puts(" pages)\n");
}

/*
 * Allocate a single 4KB page. Returns the physical address, or NULL
 * if no pages are available.
 */
void *page_alloc(void)
{
    /* Start from the first allocatable page */
    uint64_t first_page = (mem_start - RAM_BASE) / PAGE_SIZE;

    for (uint64_t i = first_page; i < first_page + total_pages; i++) {
        uint64_t word = i / 64;
        uint64_t bit  = i % 64;

        if (!(bitmap[word] & (1UL << bit))) {
            /* Found a free page -- mark it as allocated */
            bitmap[word] |= (1UL << bit);
            uint64_t addr = RAM_BASE + i * PAGE_SIZE;

            /* Zero the page and set refcount to 1 */
            uint64_t *p = (uint64_t *)addr;
            for (int j = 0; j < (int)(PAGE_SIZE / sizeof(uint64_t)); j++)
                p[j] = 0;

            refcount[i] = 1;
            return (void *)addr;
        }
    }

    uart_puts("[mm] ERROR: out of memory!\n");
    return NULL;
}

/*
 * Free a previously allocated page.
 */
void page_free(void *addr)
{
    uint64_t a = (uint64_t)addr;

    if (a < RAM_BASE || a >= RAM_END || (a & (PAGE_SIZE - 1)) != 0) {
        uart_puts("[mm] ERROR: invalid page_free address ");
        uart_puthex(a);
        uart_puts("\n");
        return;
    }

    uint64_t page = (a - RAM_BASE) / PAGE_SIZE;
    uint64_t word = page / 64;
    uint64_t bit  = page % 64;

    refcount[page] = 0;
    bitmap[word] &= ~(1UL << bit);
}

static uint64_t addr_to_page(uint64_t a)
{
    return (a - RAM_BASE) / PAGE_SIZE;
}

void page_ref(void *addr)
{
    uint64_t a = (uint64_t)addr;
    if (a < RAM_BASE || a >= RAM_END)
        return;
    refcount[addr_to_page(a)]++;
}

void page_unref(void *addr)
{
    uint64_t a = (uint64_t)addr;
    if (a < RAM_BASE || a >= RAM_END)
        return;

    uint64_t page = addr_to_page(a);
    if (refcount[page] > 1) {
        refcount[page]--;
    } else {
        page_free(addr);
    }
}

int page_get_ref(void *addr)
{
    uint64_t a = (uint64_t)addr;
    if (a < RAM_BASE || a >= RAM_END)
        return 0;
    return refcount[addr_to_page(a)];
}
