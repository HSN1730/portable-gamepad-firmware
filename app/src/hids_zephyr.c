// Copyright (c) 2026 Jacek Fedorynski
// SPDX-License-Identifier: MIT

// Bluetooth HID service implementation using plain Zephyr GATT APIs, with
// no dependency on Nordic's bt_hids library (nRF Connect SDK). Modeled on
// samples/bluetooth/peripheral_hids in upstream Zephyr. See hids_ncs.c for
// the Nordic-specific alternative.

#include <errno.h>
#include <string.h>
#include <zephyr/bluetooth/att.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/logging/log.h>

#include "hids.h"

LOG_MODULE_DECLARE(pgf);

enum {
    HIDS_REMOTE_WAKE = BIT(0),
    HIDS_NORMALLY_CONNECTABLE = BIT(1),
};

enum {
    HIDS_REPORT_TYPE_INPUT = 0x01,
};

struct __attribute__((packed)) hids_info_t {
    uint16_t bcd_hid;
    uint8_t country_code;
    uint8_t flags;
};

struct __attribute__((packed)) hids_report_ref_t {
    uint8_t id;
    uint8_t type;
};

static const struct hids_info_t hids_info = {
    .bcd_hid = 0x0101,
    .country_code = 0x00,
    .flags = HIDS_REMOTE_WAKE | HIDS_NORMALLY_CONNECTABLE,
};

static struct hids_report_ref_t input_report_ref = {
    .type = HIDS_REPORT_TYPE_INPUT,
};

static const uint8_t* report_map_data;
static uint16_t report_map_len;
static uint16_t input_report_size;

static uint8_t protocol_mode = 0x01;  // report protocol, we don't support boot protocol

enum {
    HIDS_CTRL_POINT_SUSPEND = 0x00,
    HIDS_CTRL_POINT_EXIT_SUSPEND = 0x01,
};

static uint8_t ctrl_point;
static bool suspended = false;

static ssize_t read_info(struct bt_conn* conn, const struct bt_gatt_attr* attr, void* buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &hids_info, sizeof(hids_info));
}

static ssize_t read_report_map(struct bt_conn* conn, const struct bt_gatt_attr* attr, void* buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, report_map_data, report_map_len);
}

static ssize_t read_input_report(struct bt_conn* conn, const struct bt_gatt_attr* attr, void* buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, NULL, 0);
}

static void input_ccc_changed(const struct bt_gatt_attr* attr, uint16_t value) {
    LOG_DBG("value=%u", value);
}

static ssize_t read_report_ref(struct bt_conn* conn, const struct bt_gatt_attr* attr, void* buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data, sizeof(struct hids_report_ref_t));
}

static ssize_t read_protocol_mode(struct bt_conn* conn, const struct bt_gatt_attr* attr, void* buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &protocol_mode, sizeof(protocol_mode));
}

static ssize_t write_protocol_mode(struct bt_conn* conn, const struct bt_gatt_attr* attr, const void* buf, uint16_t len, uint16_t offset, uint8_t flags) {
    if (offset != 0 || len != sizeof(protocol_mode)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    memcpy(&protocol_mode, buf, len);

    return len;
}

static ssize_t write_ctrl_point(struct bt_conn* conn, const struct bt_gatt_attr* attr, const void* buf, uint16_t len, uint16_t offset, uint8_t flags) {
    if (offset + len > sizeof(ctrl_point)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    memcpy((uint8_t*) &ctrl_point + offset, buf, len);

    if (ctrl_point == HIDS_CTRL_POINT_SUSPEND) {
        suspended = true;
    } else if (ctrl_point == HIDS_CTRL_POINT_EXIT_SUSPEND) {
        suspended = false;
    }

    return len;
}

enum hids_svc_attr_idx {
    HIDS_ATTR_IDX_SVC = 0,
    HIDS_ATTR_IDX_PROTOCOL_MODE_CHRC,
    HIDS_ATTR_IDX_PROTOCOL_MODE_VALUE,
    HIDS_ATTR_IDX_INFO_CHRC,
    HIDS_ATTR_IDX_INFO_VALUE,
    HIDS_ATTR_IDX_REPORT_MAP_CHRC,
    HIDS_ATTR_IDX_REPORT_MAP_VALUE,
    HIDS_ATTR_IDX_REPORT_CHRC,
    HIDS_ATTR_IDX_REPORT_VALUE,
    HIDS_ATTR_IDX_REPORT_CCC,
    HIDS_ATTR_IDX_REPORT_REF,
    HIDS_ATTR_IDX_CTRL_POINT_CHRC,
    HIDS_ATTR_IDX_CTRL_POINT_VALUE,
};

// Every characteristic requires encryption, mirroring CONFIG_BT_HIDS_DEFAULT_PERM_RW_ENCRYPT=y
// (see boards/bt.conf), which is what the NCS bt_hids backend applies by default.
BT_GATT_SERVICE_DEFINE(
    hids_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_HIDS),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_PROTOCOL_MODE, BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE_WITHOUT_RESP, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT, read_protocol_mode, write_protocol_mode, NULL),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_INFO, BT_GATT_CHRC_READ, BT_GATT_PERM_READ_ENCRYPT, read_info, NULL, NULL),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT_MAP, BT_GATT_CHRC_READ, BT_GATT_PERM_READ_ENCRYPT, read_report_map, NULL, NULL),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_READ_ENCRYPT, read_input_report, NULL, NULL),
    BT_GATT_CCC(input_ccc_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ_ENCRYPT, read_report_ref, NULL, &input_report_ref),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_CTRL_POINT, BT_GATT_CHRC_WRITE_WITHOUT_RESP, BT_GATT_PERM_WRITE_ENCRYPT, NULL, write_ctrl_point, &ctrl_point), );

void hids_init(const uint8_t* report_map, size_t report_map_len_, uint8_t report_id, size_t report_size) {
    report_map_data = report_map;
    report_map_len = report_map_len_;
    input_report_ref.id = report_id;
    input_report_size = report_size;
}

int hids_connected(struct bt_conn* conn) {
    return 0;
}

int hids_disconnected(struct bt_conn* conn) {
    suspended = false;
    return 0;
}

bool hids_is_suspended(void) {
    return suspended;
}

static void report_sent_cb(struct bt_conn* conn, void* user_data) {
    LOG_DBG("");
}

int hids_send_report(struct bt_conn* conn, const uint8_t* data, size_t len) {
    if (len != input_report_size) {
        return -EINVAL;
    }

    if (!bt_gatt_is_subscribed(conn, &hids_svc.attrs[HIDS_ATTR_IDX_REPORT_VALUE], BT_GATT_CCC_NOTIFY)) {
        return -EACCES;
    }

    struct bt_gatt_notify_params params = {
        .attr = &hids_svc.attrs[HIDS_ATTR_IDX_REPORT_VALUE],
        .data = data,
        .len = len,
        .func = report_sent_cb,
    };

    return bt_gatt_notify_cb(conn, &params);
}
