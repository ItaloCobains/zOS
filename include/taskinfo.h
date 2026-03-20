#ifndef ZOS_TASKINFO_H
#define ZOS_TASKINFO_H

#include "types.h"

struct task_info {
  int id;
  int state; /* 0=unsed, 1=ready, 2=running, 3=sleeping, 4=dead */
  uint64_t sleep_ticks;
};

#endif
