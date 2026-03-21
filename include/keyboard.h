#ifndef ZOS_KEYBOARD_H
#define ZOS_KEYBOARD_H

#include "types.h"

void keyboard_init(void);
void keyboard_poll(void);
int  keyboard_getc(void);  /* returns ASCII char or -1 */

#endif
