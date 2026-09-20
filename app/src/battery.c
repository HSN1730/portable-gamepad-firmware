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

// Level at or below which the battery counts as "low": drives the status LED
// warning and the BAS 1.1 Battery Charge Level. Adjust to taste.
#define BATTERY_LOW_PERCENT 20
// Level at or below which the battery counts as "critical".
#define BATTERY_CRITICAL_PERCENT 5

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

// VBUS is present, i.e. the on-board charger is charging the battery. The board
// has no charger status GPIO, but VBUS presence is an accurate stand-in: the
// charger starts charging the moment 5V appears, whatever the source (wall
// charger, power bank or PC). Set from the USB VBUS messages in main.c.
static bool usb_present = false;

// Set when VBUS has been seen since the last successful sample. Sampling can be
// skipped while charging (HIDS is suspended on USB), so this makes sure the
// level is allowed to rise again on the first sample after unplugging.
static bool usb_seen_since_sample = false;

// Lowest level reported since the charger was last connected. Hosts expect the
// reported level to fall monotonically unless the battery is really charging -
// a level that climbs back up makes Windows fire repeated low battery alerts.
static int lowest_level = -1;

bool battery_is_low(void) {
    return lowest_level >= 0 && lowest_level <= BATTERY_LOW_PERCENT;
}

void battery_set_usb_present(bool present) {
    usb_present = present;
    if (present) {
        usb_seen_since_sample = true;
    }
}

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

        // The reported level may only rise again while the battery is actually
        // being charged (or on the very first sample).
        if (usb_present || usb_seen_since_sample || lowest_level < 0) {
            lowest_level = level;
            usb_seen_since_sample = false;
        } else if (level < lowest_level) {
            lowest_level = level;
        }

        LOG_INF("battery: %d mV, %d%% (reporting %d%%)", mv, level, lowest_level);

#ifdef CONFIG_BT_BAS
        if (last_reported_level < 0 ||
            abs(lowest_level - last_reported_level) >= BATTERY_LEVEL_REPORT_THRESHOLD) {
            CHK(bt_bas_set_battery_level(lowest_level));
            last_reported_level = lowest_level;
        }

#ifdef CONFIG_BT_BAS_BLS
        // BAS 1.1: the accessory decides when the host should warn, and reports
        // whether it is charging so the host can suppress stale alerts. Hosts
        // that do not support this fall back to the BAS 1.0 behaviour (a low
        // battery alert at 5%).
        if (lowest_level <= BATTERY_CRITICAL_PERCENT) {
            bt_bas_bls_set_battery_charge_level(BT_BAS_BLS_CHARGE_LEVEL_CRITICAL);
        } else if (lowest_level <= BATTERY_LOW_PERCENT) {
            bt_bas_bls_set_battery_charge_level(BT_BAS_BLS_CHARGE_LEVEL_LOW);
        } else {
            bt_bas_bls_set_battery_charge_level(BT_BAS_BLS_CHARGE_LEVEL_GOOD);
        }

        bt_bas_bls_set_battery_charge_state(usb_present ? BT_BAS_BLS_CHARGE_STATE_CHARGING
                                                        : BT_BAS_BLS_CHARGE_STATE_DISCHARGING_ACTIVE);
        bt_bas_bls_set_wired_external_power_source(
            usb_present ? BT_BAS_BLS_WIRED_POWER_CONNECTED : BT_BAS_BLS_WIRED_POWER_NOT_CONNECTED);
#endif // CONFIG_BT_BAS_BLS
#endif // CONFIG_BT_BAS
    }

    k_work_reschedule(&battery_work, BATTERY_REPORT_INTERVAL);
}

void battery_init(void) {
    if (!battery_hw_init()) {
        return;
    }

    k_work_reschedule(&battery_work, K_NO_WAIT);
}
