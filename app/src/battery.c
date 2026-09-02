// Copyright (c) 2026 Jacek Fedorynski
// SPDX-License-Identifier: MIT

// Battery voltage sampling schedule, discharge curve, and reporting.

#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#ifdef CONFIG_BT_BAS
#include <zephyr/bluetooth/services/bas.h>
#endif

#ifdef CONFIG_BT
#include "hids.h"
#endif

#include "battery.h"
#include "battery_hw.h"
#include "chk.h"

LOG_MODULE_DECLARE(pgf);

#define BATTERY_REPORT_INTERVAL K_SECONDS(60)

struct discharge_point {
    uint16_t pptt;
    uint16_t mv;
};

static const struct discharge_point discharge_curve[] = {
    { 10000, 4200 },
    { 7500, 4000 },
    { 5000, 3800 },
    { 2500, 3700 },
    { 1000, 3500 },
    { 0, 3300 },
};

static unsigned int level_pptt(unsigned int mv) {
    const struct discharge_point* p = discharge_curve;

    if (mv >= p->mv) {
        return p->pptt;
    }
    while (p->pptt > 0 && mv < p->mv) {
        p++;
    }
    if (mv < p->mv) {
        return p->pptt;
    }

    const struct discharge_point* prev = p - 1;
    return p->pptt + (prev->pptt - p->pptt) * (mv - p->mv) / (prev->mv - p->mv);
}

static void battery_work_fn(struct k_work* work);
static K_WORK_DELAYABLE_DEFINE(battery_work, battery_work_fn);

#ifdef CONFIG_BT_BAS
#define BATTERY_LEVEL_REPORT_THRESHOLD 5
static int last_reported_level = -1;
#endif

static void battery_work_fn(struct k_work* work) {
    int mv;

#ifdef CONFIG_BT
    bool suspended = hids_is_suspended();
#else
    bool suspended = false;
#endif

    if (!suspended && battery_hw_read_mv(&mv)) {
        unsigned int pptt = level_pptt(mv);
        int level = (int) (pptt / 100);
        LOG_INF("battery: %d mV, %d%%", mv, level);

#ifdef CONFIG_BT_BAS
        if (last_reported_level < 0 || abs(level - last_reported_level) >= BATTERY_LEVEL_REPORT_THRESHOLD) {
            CHK(bt_bas_set_battery_level(level));
            last_reported_level = level;
        }
#endif
    }

    k_work_reschedule(&battery_work, BATTERY_REPORT_INTERVAL);
}

void battery_init(void) {
    if (!battery_hw_init()) {
        return;
    }

    k_work_reschedule(&battery_work, K_NO_WAIT);
}
