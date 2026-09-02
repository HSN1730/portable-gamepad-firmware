// Copyright (c) 2026 Jacek Fedorynski
// SPDX-License-Identifier: MIT

#include <zephyr/devicetree.h>

#include "battery_hw.h"

#define PMIC_CHARGER_NODE DT_NODELABEL(pmic_charger)

#if !DT_NODE_HAS_STATUS_OKAY(PMIC_CHARGER_NODE)
#error \
    "CONFIG_PGF_BATTERY_LEVEL_REPORTING_PMIC is enabled but this board has no \"pmic_charger\" devicetree node (compatible \"nordic,npm1300-charger\") with status \"okay\""
#endif

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

#include "chk.h"

LOG_MODULE_DECLARE(pgf);

static const struct device* pmic_charger = DEVICE_DT_GET(PMIC_CHARGER_NODE);

bool battery_hw_init(void) {
    if (!device_is_ready(pmic_charger)) {
        LOG_ERR("battery: PMIC charger device not ready");
        return false;
    }

    return true;
}

bool battery_hw_read_mv(int* mv) {
    struct sensor_value val;

    if (CHK(sensor_sample_fetch(pmic_charger)) &&
        CHK(sensor_channel_get(pmic_charger, SENSOR_CHAN_GAUGE_VOLTAGE, &val))) {
        *mv = val.val1 * 1000 + val.val2 / 1000;
        return true;
    }

    return false;
}
