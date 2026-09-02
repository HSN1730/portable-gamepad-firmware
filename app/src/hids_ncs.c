// Copyright (c) 2026 Jacek Fedorynski
// SPDX-License-Identifier: MIT

// Bluetooth HID service implementation backed by Nordic's bt_hids library
// (nRF Connect SDK). See hids_zephyr.c for a vanilla-Zephyr alternative.

#include <zephyr/bluetooth/conn.h>
#include <zephyr/logging/log.h>

#include <bluetooth/services/hids.h>

#include "chk.h"
#include "hids.h"

LOG_MODULE_DECLARE(pgf);

#define REPORT_ID_IDX 0

BT_HIDS_DEF(hids_obj, PGF_MAX_BT_REPORT_SIZE);

static bool suspended = false;

static void conn_cp_evt_handler(enum bt_hids_cp_evt evt, struct bt_conn* conn) {
    switch (evt) {
        case BT_HIDS_CP_EVT_HOST_SUSP:
            suspended = true;
            break;
        case BT_HIDS_CP_EVT_HOST_EXIT_SUSP:
            suspended = false;
            break;
        default:
            break;
    }
}

bool hids_is_suspended(void) {
    return suspended;
}

void hids_init(const uint8_t* report_map, size_t report_map_len, uint8_t report_id, size_t report_size) {
    struct bt_hids_init_param hids_init_param = { 0 };
    struct bt_hids_inp_rep* hids_inp_rep;

    hids_init_param.rep_map.data = report_map;
    hids_init_param.rep_map.size = report_map_len;

    hids_init_param.info.bcd_hid = 0x0101;
    hids_init_param.info.b_country_code = 0x00;
    hids_init_param.info.flags = (BT_HIDS_REMOTE_WAKE |
                                  BT_HIDS_NORMALLY_CONNECTABLE);

    hids_inp_rep = &hids_init_param.inp_rep_group_init.reports[0];
    hids_inp_rep->size = report_size;
    hids_inp_rep->id = report_id;
    hids_init_param.inp_rep_group_init.cnt++;

    hids_init_param.conn_cp_evt_handler = conn_cp_evt_handler;

    CHK(bt_hids_init(&hids_obj, &hids_init_param));
}

int hids_connected(struct bt_conn* conn) {
    return bt_hids_connected(&hids_obj, conn);
}

int hids_disconnected(struct bt_conn* conn) {
    suspended = false;
    return bt_hids_disconnected(&hids_obj, conn);
}

static void report_sent_cb(struct bt_conn* conn, void* user_data) {
    LOG_DBG("");
}

int hids_send_report(struct bt_conn* conn, const uint8_t* data, size_t len) {
    return bt_hids_inp_rep_send(&hids_obj, conn, REPORT_ID_IDX, data, len, report_sent_cb);
}
