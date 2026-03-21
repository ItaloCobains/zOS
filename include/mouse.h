#ifndef ZOS_MOUSE_H
#define ZOS_MOUSE_H

#include "types.h"

struct mouse_state {
    int x, y;
    int buttons;  /* bit 0 = left, bit 1 = right */
    int clicked;  /* set on press, cleared after read */
};

void mouse_init(void);
int  mouse_get_mmio_slot(void);  /* returns which MMIO slot the mouse uses */
void mouse_poll(void);
struct mouse_state mouse_get(void);

#endif
