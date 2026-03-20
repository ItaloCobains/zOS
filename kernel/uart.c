/*
 * uart.c -- PL011 UART driver for QEMU virt.
 *
 * The PL011 is a simple serial port. On QEMU virt it's mapped at 0x09000000.
 * We use it in the simplest possible way: polling mode, no interrupts,
 * no FIFO configuration. Write a byte to the data register and it appears
 * on the terminal.
 */

#include "types.h"
#include "uart.h"

/* PL011 register offsets */
#define UART_BASE   0x09000000
#define UART_DR     (*(volatile uint32_t *)(UART_BASE + 0x00))  /* Data register */
#define UART_FR     (*(volatile uint32_t *)(UART_BASE + 0x18))  /* Flag register */
#define UART_FR_TXFF (1 << 5)  /* Transmit FIFO full */

void uart_init(void)
{
    /* PL011 on QEMU comes pre-configured. Nothing to do for basic output. */
}

void uart_putc(char c)
{
    /* Wait until the transmit FIFO has space */
    while (UART_FR & UART_FR_TXFF)
        ;
    UART_DR = c;
}

void uart_puts(const char *s)
{
    while (*s) {
        if (*s == '\n')
            uart_putc('\r');  /* Terminal expects \r\n for newlines */
        uart_putc(*s);
        s++;
    }
}

void uart_puthex(uint64_t value)
{
    const char *hex = "0123456789abcdef";
    uart_puts("0x");

    /* Print 64-bit value as 16 hex digits */
    for (int i = 60; i >= 0; i -= 4) {
        uart_putc(hex[(value >> i) & 0xF]);
    }
}
