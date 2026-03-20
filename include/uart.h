#ifndef ZOS_UART_H
#define ZOS_UART_H

#include "types.h"

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);
void uart_puthex(uint64_t value);
int  uart_getc(void);

#endif
