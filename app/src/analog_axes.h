#ifndef _ANALOG_AXES_H_
#define _ANALOG_AXES_H_

#include <stdint.h>
#include <zephyr/devicetree.h>

// True if the board's /gamepad-axes node has an io-channels entry named
// `name` (one of left_stick_x, left_stick_y, right_stick_x, right_stick_y,
// left_trigger, right_trigger).
#define HAVE_ANALOG_AXIS(name) DT_PROP_HAS_NAME(DT_PATH(gamepad_axes), io_channels, name)

enum analog_axes {
    ANALOG_AXIS_LX = 0,
    ANALOG_AXIS_LY,
    ANALOG_AXIS_RX,
    ANALOG_AXIS_RY,
    ANALOG_AXIS_LT,
    ANALOG_AXIS_RT,
    NUM_ANALOG_AXES,
};

// Points into the latest 16-bit reading (a proportion of that axis's actual
// supply voltage) for each axis the board has wired up, updated in place by
// read_adc(). NULL for any axis the board doesn't have.
// Set up by initialize_adc().
extern uint16_t* analog_axes[NUM_ANALOG_AXES];

// Sets up the ADC channel(s) backing each analog axis this board has wired
// up. No-op if it has none. Call once at startup, before read_adc().
void initialize_adc(void);

// Samples every analog axis this board has wired up and normalizes each
// reading in place (through analog_axes[]). No-op if the board has none.
void read_adc(void);

#endif
