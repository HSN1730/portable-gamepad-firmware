// Copyright (c) 2026 Jacek Fedorynski
// SPDX-License-Identifier: MIT

// Battery voltage measurement via a resistor-divider ADC channel.

#include <zephyr/devicetree.h>

#include "battery_hw.h"

#define VBATT_NODE DT_PATH(vbatt)

#if !DT_NODE_HAS_STATUS_OKAY(VBATT_NODE)
#error \
    "CONFIG_PGF_BATTERY_LEVEL_REPORTING_VBATT is enabled but this board has no /vbatt devicetree node with status \"okay\""
#endif

#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/adc/voltage_divider.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "chk.h"

LOG_MODULE_DECLARE(pgf);

static const struct voltage_divider_dt_spec vbatt = VOLTAGE_DIVIDER_DT_SPEC_GET(VBATT_NODE);
static const struct gpio_dt_spec vbatt_power = GPIO_DT_SPEC_GET_OR(VBATT_NODE, power_gpios, { 0 });
#define VBATT_POWER_ON_SAMPLE_DELAY_US DT_PROP_OR(VBATT_NODE, power_on_sample_delay_us, 100)

bool battery_hw_init(void) {
    if (!adc_is_ready_dt(&vbatt.port)) {
        LOG_ERR("battery: ADC not ready");
        return false;
    }

    if (!CHK(adc_channel_setup_dt(&vbatt.port))) {
        return false;
    }

    if (vbatt_power.port != NULL) {
        if (!CHK(gpio_pin_configure_dt(&vbatt_power, GPIO_OUTPUT_INACTIVE))) {
            return false;
        }
    }

    return true;
}

bool battery_hw_read_mv(int* mv) {
    if (vbatt_power.port != NULL) {
        if (CHK(gpio_pin_set_dt(&vbatt_power, 1))) {
            k_sleep(K_USEC(VBATT_POWER_ON_SAMPLE_DELAY_US));
        }
    }

    uint16_t raw;
    struct adc_sequence seq = {
        .buffer = &raw,
        .buffer_size = sizeof(raw),
        .calibrate = true,
    };

    bool ok = false;

    if (CHK(adc_sequence_init_dt(&vbatt.port, &seq)) && CHK(adc_read_dt(&vbatt.port, &seq))) {
        int32_t raw_mv = raw;

        if (CHK(adc_raw_to_millivolts_dt(&vbatt.port, &raw_mv))) {
            int64_t scaled_mv = raw_mv;
            voltage_divider_scale64_dt(&vbatt, &scaled_mv);
            *mv = (int) scaled_mv;
            ok = true;
        }
    }

    if (vbatt_power.port != NULL) {
        CHK(gpio_pin_set_dt(&vbatt_power, 0));
    }

    return ok;
}
