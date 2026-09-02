// Copyright (c) 2026 Jacek Fedorynski
// SPDX-License-Identifier: MIT

// Stub used when no battery level hardware backend is enabled.

#include "battery_hw.h"

bool battery_hw_init(void) {
    return false;
}

bool battery_hw_read_mv(int* mv) {
    return false;
}
