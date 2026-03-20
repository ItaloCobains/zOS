#ifndef ZOS_TIMER_H
#define ZOS_TIMER_H

#include "types.h"

void     timer_init(void);
void     timer_handler(void);
uint64_t timer_get_ticks(void);

#endif
