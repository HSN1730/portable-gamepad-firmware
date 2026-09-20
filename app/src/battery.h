#ifndef _BATTERY_H_
#define _BATTERY_H_

#include <stdbool.h>

// Starts periodic battery voltage sampling and reporting (over the
// Bluetooth Battery Service, if enabled). No-op if no battery reporting
// backend is enabled (see app/Kconfig), or if hardware setup fails.
void battery_init(void);

// True while the reported battery level is at or below the low battery
// threshold. Used by the status LED to warn the user before the battery runs
// out. Always false when no battery reporting backend is enabled.
bool battery_is_low(void);

// Tells the battery code whether VBUS is present. Used as the "is charging"
// signal (the board has no charger status GPIO) so that the reported level is
// allowed to rise again while charging, and to fill in the BAS 1.1 charge
// state.
void battery_set_usb_present(bool present);

#endif
