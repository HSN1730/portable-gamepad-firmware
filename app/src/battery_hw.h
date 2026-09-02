#ifndef _BATTERY_HW_H_
#define _BATTERY_HW_H_

#include <stdbool.h>

// One-time hardware setup. Returns false if there's no backend to set up,
// or if setup fails.
bool battery_hw_init(void);

// Reads the current battery voltage into *mv. Returns false on failure.
bool battery_hw_read_mv(int* mv);

#endif
