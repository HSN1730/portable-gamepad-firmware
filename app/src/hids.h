#ifndef _HIDS_H_
#define _HIDS_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/bluetooth/conn.h>

// Largest single HID input report we'll ever be asked to send over Bluetooth.
// Keep in sync with MAX_REPORT_SIZE in main.c (checked there with a BUILD_ASSERT).
#define PGF_MAX_BT_REPORT_SIZE 32

void hids_init(const uint8_t* report_map, size_t report_map_len, uint8_t report_id, size_t report_size);
int hids_connected(struct bt_conn* conn);
int hids_disconnected(struct bt_conn* conn);
int hids_send_report(struct bt_conn* conn, const uint8_t* data, size_t len);

#endif
