#ifndef _SLEEP_H_
#define _SLEEP_H_

#include <zephyr/drivers/gpio.h>

// Configures wake_button for wake-on-interrupt and puts the system to sleep.
void enter_sleep(const struct gpio_dt_spec* wake_button);

#endif
