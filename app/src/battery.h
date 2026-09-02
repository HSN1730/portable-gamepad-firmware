#ifndef _BATTERY_H_
#define _BATTERY_H_

// Starts periodic battery voltage sampling and reporting (over the
// Bluetooth Battery Service, if enabled). No-op if no battery reporting
// backend is enabled (see app/Kconfig), or if hardware setup fails.
void battery_init(void);

#endif
