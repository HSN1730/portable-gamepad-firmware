// Copyright (c) 2026 Jacek Fedorynski
// SPDX-License-Identifier: MIT

#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "analog_axes.h"
#include "chk.h"

LOG_MODULE_DECLARE(pgf);

uint16_t* analog_axes[NUM_ANALOG_AXES];

static const struct adc_dt_spec adc_channels[] = {
    ADC_DT_SPEC_GET_BY_NAME_OR(DT_PATH(gamepad_axes), left_stick_x, { 0 }),
    ADC_DT_SPEC_GET_BY_NAME_OR(DT_PATH(gamepad_axes), left_stick_y, { 0 }),
    ADC_DT_SPEC_GET_BY_NAME_OR(DT_PATH(gamepad_axes), right_stick_x, { 0 }),
    ADC_DT_SPEC_GET_BY_NAME_OR(DT_PATH(gamepad_axes), right_stick_y, { 0 }),
    ADC_DT_SPEC_GET_BY_NAME_OR(DT_PATH(gamepad_axes), left_trigger, { 0 }),
    ADC_DT_SPEC_GET_BY_NAME_OR(DT_PATH(gamepad_axes), right_trigger, { 0 }),
};

// potentiometer +V
#define ANALOG_AXIS_VOLTAGE_MV DT_PROP_OR(DT_PATH(gamepad_axes), analog_axis_voltage_mv, 3300)

// The ADC's effective full-scale voltage (reference, adjusted for gain), in mV.
static int32_t analog_axis_scale_mv[NUM_ANALOG_AXES];

static void set_analog_axis_scale_mv(size_t axis, const struct adc_dt_spec* spec) {
    int64_t scale = (spec->channel_cfg.reference == ADC_REF_INTERNAL) ? adc_ref_internal(spec->dev)
                                                                      : spec->vref_mv;
    CHK(adc_gain_invert_64(spec->channel_cfg.gain, &scale));
    analog_axis_scale_mv[axis] = (int32_t) scale;
}

#if IS_ENABLED(CONFIG_PGF_GROUP_ADC_CHANNELS)

// Axes can be spread across more than one ADC controller - more than
// one peripheral on the MCU, or an external ADC chip - so group them:
// one adc_sequence (with its own buffer) per distinct controller,
// filled in by initialize_adc() and read with one adc_read() call per
// controller in read_adc_channels().
static const struct device* adc_controllers[NUM_ANALOG_AXES];
static struct adc_sequence adc_sequences[NUM_ANALOG_AXES];
static uint16_t adc_buffers[NUM_ANALOG_AXES][NUM_ANALOG_AXES];
static int active_adc_controllers = 0;

void initialize_adc() {
    uint8_t axis_adc_controller_idx[NUM_ANALOG_AXES];

    for (size_t i = 0; i < NUM_ANALOG_AXES; i++) {
        if (adc_channels[i].dev == NULL) {
            continue;
        }

        if (!adc_is_ready_dt(&adc_channels[i])) {
            LOG_ERR("ADC controller for channel %d not ready", i);
            continue;
        }
        if (!CHK(adc_channel_setup_dt(&adc_channels[i]))) {
            continue;
        }
        set_analog_axis_scale_mv(i, &adc_channels[i]);

        int idx = -1;
        for (int j = 0; j < active_adc_controllers; j++) {
            if (adc_controllers[j] == adc_channels[i].dev) {
                idx = j;
                break;
            }
        }
        if (idx == -1) {
            idx = active_adc_controllers++;
            adc_controllers[idx] = adc_channels[i].dev;
            adc_sequences[idx].buffer = adc_buffers[idx];
            adc_sequences[idx].buffer_size = sizeof(adc_buffers[idx]);
            adc_sequences[idx].resolution = adc_channels[i].resolution;
        }

        adc_sequences[idx].channels |= BIT(adc_channels[i].channel_id);
        axis_adc_controller_idx[i] = idx;
    }

    LOG_INF("active_adc_controllers=%d", active_adc_controllers);

    // adc_read() packs each controller's samples into its buffer in
    // ascending channel-ID order, so an axis's slot within its own
    // controller's buffer is however many of that controller's other
    // channels have a lower ID.
    for (size_t i = 0; i < NUM_ANALOG_AXES; i++) {
        if (adc_channels[i].dev == NULL) {
            continue;
        }

        uint8_t controller_idx = axis_adc_controller_idx[i];
        uint32_t lower_channels = adc_sequences[controller_idx].channels & (BIT(adc_channels[i].channel_id) - 1);
        analog_axes[i] = &adc_buffers[controller_idx][POPCOUNT(lower_channels)];
    }
}

static void read_adc_channels() {
    for (int c = 0; c < active_adc_controllers; c++) {
        CHK(adc_read(adc_controllers[c], &adc_sequences[c]));
    }
}

#else  // !CONFIG_PGF_GROUP_ADC_CHANNELS

// Some ADC drivers reject an adc_sequence with more than one channel
// bit set, so instead of grouping every axis on a shared controller
// into one sequence, each axis gets its own single-channel sequence
// and buffer slot, read with its own adc_read() call.
static const struct device* adc_devices[NUM_ANALOG_AXES];
static struct adc_sequence adc_sequences[NUM_ANALOG_AXES];
static uint16_t adc_buffers[NUM_ANALOG_AXES];
static int active_adc_channels = 0;

#if IS_ENABLED(CONFIG_ADC_SAM0)
// SAMD21 workaround
static const struct adc_dt_spec* adc_slot_spec[NUM_ANALOG_AXES];
#endif

void initialize_adc() {
    for (size_t i = 0; i < NUM_ANALOG_AXES; i++) {
        if (adc_channels[i].dev == NULL) {
            continue;
        }

        if (!adc_is_ready_dt(&adc_channels[i])) {
            LOG_ERR("ADC controller for channel %d not ready", i);
            continue;
        }
        if (!CHK(adc_channel_setup_dt(&adc_channels[i]))) {
            continue;
        }
        set_analog_axis_scale_mv(i, &adc_channels[i]);

        int idx = active_adc_channels++;
#if IS_ENABLED(CONFIG_ADC_SAM0)
        adc_slot_spec[idx] = &adc_channels[i];
#endif
        adc_devices[idx] = adc_channels[i].dev;
        adc_sequences[idx].channels = BIT(adc_channels[i].channel_id);
        adc_sequences[idx].buffer = &adc_buffers[idx];
        adc_sequences[idx].buffer_size = sizeof(adc_buffers[idx]);
        adc_sequences[idx].resolution = adc_channels[i].resolution;
        analog_axes[i] = &adc_buffers[idx];
    }
}

static void read_adc_channels() {
    for (int i = 0; i < active_adc_channels; i++) {
#if IS_ENABLED(CONFIG_ADC_SAM0)
        CHK(adc_channel_setup_dt(adc_slot_spec[i]));
#endif
        CHK(adc_read(adc_devices[i], &adc_sequences[i]));
    }
}

#endif  // CONFIG_PGF_GROUP_ADC_CHANNELS

// Normalize a raw reading to a full 16-bit range representing how much of
// ANALOG_AXIS_VOLTAGE_MV was actually measured, regardless of the axis's
// ADC's resolution or effective full-scale voltage (reference/gain), so
// fill_out_report() (and anything else downstream) can just treat every
// axis as "16-bit proportion of the real supply voltage" without knowing
// anything about the ADC behind it.
static void normalize_analog_axis(enum analog_axes axis) {
    int64_t v = (int64_t) *analog_axes[axis] << (16 - adc_channels[axis].resolution);
    v = v * analog_axis_scale_mv[axis] / ANALOG_AXIS_VOLTAGE_MV;
    *analog_axes[axis] = (uint16_t) CLAMP(v, 0, UINT16_MAX);
}

// Classic digit-by-digit integer square root. dx and dy below can each be
// up to 65535 in magnitude, so dx*dx+dy*dy needs up to 64 bits; the
// result fits comfortably in 32 (max distance across the whole 0-65535
// square is ~92682).
static uint32_t isqrt64(uint64_t n) {
    uint64_t res = 0;
    uint64_t bit = (uint64_t) 1 << 62;
    while (bit > n) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (n >= res + bit) {
            n -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t) res;
}

#define OUTPUT_CENTER 32768
#define OUTPUT_MAX_MAGNITUDE 32767

struct stick_calibration {
    int32_t center_x;
    int32_t center_y;
    int32_t inner_radius;
    int32_t radius_range;  // outer_radius - inner_radius
};

// Re-centers and applies radial deadzone.
// Scales magnitude from [inner_radius, outer_radius] to [0, 65535].
static void apply_stick_calibration(uint16_t* x, uint16_t* y, const struct stick_calibration* cal) {
    int32_t dx = (int32_t) *x - cal->center_x;
    int32_t dy = (int32_t) *y - cal->center_y;
    uint64_t dist_sq = (uint64_t) ((int64_t) dx * dx) + (uint64_t) ((int64_t) dy * dy);
    uint32_t dist = isqrt64(dist_sq);

    int32_t new_dx = 0;
    int32_t new_dy = 0;

    if (dist > (uint32_t) cal->inner_radius) {
        uint32_t outer_radius = (uint32_t) (cal->inner_radius + cal->radius_range);
        uint32_t clamped_dist = MIN(dist, outer_radius);
        int64_t scaled_dist = (int64_t) (clamped_dist - cal->inner_radius) * OUTPUT_MAX_MAGNITUDE / cal->radius_range;

        // Rescale (dx, dy) to the new magnitude while keeping the same direction.
        new_dx = (int32_t) ((int64_t) dx * scaled_dist / dist);
        new_dy = (int32_t) ((int64_t) dy * scaled_dist / dist);
    }

    *x = (uint16_t) CLAMP(OUTPUT_CENTER + new_dx, 0, UINT16_MAX);
    *y = (uint16_t) CLAMP(OUTPUT_CENTER + new_dy, 0, UINT16_MAX);
}

#if HAVE_ANALOG_AXIS(left_stick_x) && HAVE_ANALOG_AXIS(left_stick_y) &&     \
    (DT_NODE_HAS_PROP(DT_PATH(gamepad_axes), left_stick_x_center) ||        \
        DT_NODE_HAS_PROP(DT_PATH(gamepad_axes), left_stick_y_center) ||     \
        DT_NODE_HAS_PROP(DT_PATH(gamepad_axes), left_stick_inner_radius) || \
        DT_NODE_HAS_PROP(DT_PATH(gamepad_axes), left_stick_outer_radius))
#define HAVE_LEFT_STICK_CALIBRATION 1
#if DT_PROP_OR(DT_PATH(gamepad_axes), left_stick_outer_radius, OUTPUT_MAX_MAGNITUDE) <= \
    DT_PROP_OR(DT_PATH(gamepad_axes), left_stick_inner_radius, 0)
#error "left-stick-outer-radius must be greater than left-stick-inner-radius"
#endif
static const struct stick_calibration left_stick_calibration = {
    .center_x = DT_PROP_OR(DT_PATH(gamepad_axes), left_stick_x_center, OUTPUT_CENTER),
    .center_y = DT_PROP_OR(DT_PATH(gamepad_axes), left_stick_y_center, OUTPUT_CENTER),
    .inner_radius = DT_PROP_OR(DT_PATH(gamepad_axes), left_stick_inner_radius, 0),
    .radius_range = DT_PROP_OR(DT_PATH(gamepad_axes), left_stick_outer_radius, OUTPUT_MAX_MAGNITUDE) -
                    DT_PROP_OR(DT_PATH(gamepad_axes), left_stick_inner_radius, 0),
};
#else
#define HAVE_LEFT_STICK_CALIBRATION 0
#endif

#if HAVE_ANALOG_AXIS(right_stick_x) && HAVE_ANALOG_AXIS(right_stick_y) &&    \
    (DT_NODE_HAS_PROP(DT_PATH(gamepad_axes), right_stick_x_center) ||        \
        DT_NODE_HAS_PROP(DT_PATH(gamepad_axes), right_stick_y_center) ||     \
        DT_NODE_HAS_PROP(DT_PATH(gamepad_axes), right_stick_inner_radius) || \
        DT_NODE_HAS_PROP(DT_PATH(gamepad_axes), right_stick_outer_radius))
#define HAVE_RIGHT_STICK_CALIBRATION 1
#if DT_PROP_OR(DT_PATH(gamepad_axes), right_stick_outer_radius, OUTPUT_MAX_MAGNITUDE) <= \
    DT_PROP_OR(DT_PATH(gamepad_axes), right_stick_inner_radius, 0)
#error "right-stick-outer-radius must be greater than right-stick-inner-radius"
#endif
static const struct stick_calibration right_stick_calibration = {
    .center_x = DT_PROP_OR(DT_PATH(gamepad_axes), right_stick_x_center, OUTPUT_CENTER),
    .center_y = DT_PROP_OR(DT_PATH(gamepad_axes), right_stick_y_center, OUTPUT_CENTER),
    .inner_radius = DT_PROP_OR(DT_PATH(gamepad_axes), right_stick_inner_radius, 0),
    .radius_range = DT_PROP_OR(DT_PATH(gamepad_axes), right_stick_outer_radius, OUTPUT_MAX_MAGNITUDE) -
                    DT_PROP_OR(DT_PATH(gamepad_axes), right_stick_inner_radius, 0),
};
#else
#define HAVE_RIGHT_STICK_CALIBRATION 0
#endif

struct trigger_calibration {
    int32_t lower_end;
    int32_t range;  // upper_end - lower_end
};

static void apply_trigger_calibration(uint16_t* v, const struct trigger_calibration* cal) {
    int64_t scaled = ((int64_t) *v - cal->lower_end) * UINT16_MAX / cal->range;
    *v = (uint16_t) CLAMP(scaled, 0, UINT16_MAX);
}

#if HAVE_ANALOG_AXIS(left_trigger) &&                                   \
    (DT_NODE_HAS_PROP(DT_PATH(gamepad_axes), left_trigger_lower_end) || \
        DT_NODE_HAS_PROP(DT_PATH(gamepad_axes), left_trigger_upper_end))
#define HAVE_LEFT_TRIGGER_CALIBRATION 1
#if DT_PROP_OR(DT_PATH(gamepad_axes), left_trigger_upper_end, UINT16_MAX) <= \
    DT_PROP_OR(DT_PATH(gamepad_axes), left_trigger_lower_end, 0)
#error "left-trigger-upper-end must be greater than left-trigger-lower-end"
#endif
static const struct trigger_calibration left_trigger_calibration = {
    .lower_end = DT_PROP_OR(DT_PATH(gamepad_axes), left_trigger_lower_end, 0),
    .range = DT_PROP_OR(DT_PATH(gamepad_axes), left_trigger_upper_end, UINT16_MAX) -
             DT_PROP_OR(DT_PATH(gamepad_axes), left_trigger_lower_end, 0),
};
#else
#define HAVE_LEFT_TRIGGER_CALIBRATION 0
#endif

#if HAVE_ANALOG_AXIS(right_trigger) &&                                   \
    (DT_NODE_HAS_PROP(DT_PATH(gamepad_axes), right_trigger_lower_end) || \
        DT_NODE_HAS_PROP(DT_PATH(gamepad_axes), right_trigger_upper_end))
#define HAVE_RIGHT_TRIGGER_CALIBRATION 1
#if DT_PROP_OR(DT_PATH(gamepad_axes), right_trigger_upper_end, UINT16_MAX) <= \
    DT_PROP_OR(DT_PATH(gamepad_axes), right_trigger_lower_end, 0)
#error "right-trigger-upper-end must be greater than right-trigger-lower-end"
#endif
static const struct trigger_calibration right_trigger_calibration = {
    .lower_end = DT_PROP_OR(DT_PATH(gamepad_axes), right_trigger_lower_end, 0),
    .range = DT_PROP_OR(DT_PATH(gamepad_axes), right_trigger_upper_end, UINT16_MAX) -
             DT_PROP_OR(DT_PATH(gamepad_axes), right_trigger_lower_end, 0),
};
#else
#define HAVE_RIGHT_TRIGGER_CALIBRATION 0
#endif

void read_adc() {
    read_adc_channels();

#if HAVE_ANALOG_AXIS(left_stick_x)
    normalize_analog_axis(ANALOG_AXIS_LX);
#endif
#if HAVE_ANALOG_AXIS(left_stick_y)
    normalize_analog_axis(ANALOG_AXIS_LY);
    *analog_axes[ANALOG_AXIS_LY] = 65535 - *analog_axes[ANALOG_AXIS_LY];
#endif
#if HAVE_ANALOG_AXIS(right_stick_x)
    normalize_analog_axis(ANALOG_AXIS_RX);
#endif
#if HAVE_ANALOG_AXIS(right_stick_y)
    normalize_analog_axis(ANALOG_AXIS_RY);
    *analog_axes[ANALOG_AXIS_RY] = 65535 - *analog_axes[ANALOG_AXIS_RY];
#endif
#if HAVE_ANALOG_AXIS(left_trigger)
    normalize_analog_axis(ANALOG_AXIS_LT);
#endif
#if HAVE_ANALOG_AXIS(right_trigger)
    normalize_analog_axis(ANALOG_AXIS_RT);
#endif

#if HAVE_LEFT_STICK_CALIBRATION
    apply_stick_calibration(analog_axes[ANALOG_AXIS_LX], analog_axes[ANALOG_AXIS_LY], &left_stick_calibration);
#endif
#if HAVE_RIGHT_STICK_CALIBRATION
    apply_stick_calibration(analog_axes[ANALOG_AXIS_RX], analog_axes[ANALOG_AXIS_RY], &right_stick_calibration);
#endif
#if HAVE_LEFT_TRIGGER_CALIBRATION
    apply_trigger_calibration(analog_axes[ANALOG_AXIS_LT], &left_trigger_calibration);
#endif
#if HAVE_RIGHT_TRIGGER_CALIBRATION
    apply_trigger_calibration(analog_axes[ANALOG_AXIS_RT], &right_trigger_calibration);
#endif
}
