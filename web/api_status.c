/**
 * @file    api_status.c
 * @brief   综合状态 API 实现 — 聚合 Modbus / CAN / OTA / MQTT 子系统状态
 *
 * GET /api/status 返回示例:
 * {
 *   "mqtt":   {"connected":true,  "retries":0,  "broker":"192.168.5.10:1883"},
 *   "modbus": {"active":true,  "slaves":1,  "tx_count":5,  "rx_count":3},
 *   "can":    {"active":true,  "tx_count":12, "rx_count":45},
 *   "ota":    {"status":"idle",  "progress":0,  "version":"3.1.4"},
 *   "system": {"version":"3.1.4", "uptime":3600, "ntp_ok":false}
 * }
 */
#include "api_status.h"
#include "../web_server.h"
#include "../app_config.h"
#include "../services/mqtt_client.h"
#include "../services/data_bus.h"
#include "../ota_manager.h"
#include "../ntp_sync.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

extern void web_send_response(int client_fd, int status,
                              const char *content_type,
                              const char *body, size_t body_len);

/* main.c 提供线程安全的计数器快照接口。 */
extern void app_get_can_counters(int *tx_count, int *rx_count);
extern int app_get_rs485_tx_count(void);
extern long app_get_uptime_seconds(void);

void api_status_handle(int client_fd)
{
    /* ---- MQTT ---- */
    bool mqtt_ok = mqtt_client_is_connected();
    int  mqtt_retries = mqtt_client_get_retry_count();
    int can_tx_count = 0;
    int can_rx_count = 0;
    app_get_can_counters(&can_tx_count, &can_rx_count);
    int rs485_tx_count = app_get_rs485_tx_count();

    /* ---- Modbus: 从 data_bus 获取 modbus 数据点 ---- */
    data_point_t modbus_pts[DATA_BUS_MAX_POINTS];
    int modbus_count = data_bus_get_by_source(DATA_SOURCE_MODBUS,
                                              modbus_pts, DATA_BUS_MAX_POINTS);

    /* ---- CAN: 从 data_bus 获取 CAN 数据点 ---- */
    data_point_t can_pts[DATA_BUS_MAX_POINTS];
    int can_count = data_bus_get_by_source(DATA_SOURCE_CAN,
                                           can_pts, DATA_BUS_MAX_POINTS);

    /* ---- OTA ---- */
    ota_status_t ota_st = ota_get_status();
    const char *ota_str = "idle";
    switch (ota_st) {
        case OTA_CHECKING:    ota_str = "checking";    break;
        case OTA_DOWNLOADING: ota_str = "downloading"; break;
        case OTA_VERIFYING:   ota_str = "verifying";   break;
        case OTA_APPLYING:    ota_str = "applying";    break;
        case OTA_PATCHING:    ota_str = "patching";    break;
        case OTA_SUCCESS:     ota_str = "success";     break;
        case OTA_FAILED:      ota_str = "failed";      break;
        default: break;
    }

    /* ---- NTP ---- */
    bool ntp_ok;
    time_t ntp_last;
    ntp_sync_get_status(&ntp_ok, &ntp_last);

    /* 本地版本 */
    char ver[32];
    ota_get_local_version(ver, sizeof(ver));

    /* ---- 构建 JSON ---- */
    char json[4096];
    int len = snprintf(json, sizeof(json),
        "{"
        "\"mqtt\":{"
            "\"connected\":%s,"
            "\"retries\":%d,"
            "\"broker\":\"%s:%d\","
            "\"topic\":\"%s\""
        "},"
        "\"modbus\":{"
            "\"active\":%s,"
            "\"data_points\":%d,"
            "\"tx_count\":%d"
        "},"
        "\"can\":{"
            "\"active\":%s,"
            "\"data_points\":%d,"
            "\"tx_count\":%d,"
            "\"rx_count\":%d"
        "},"
        "\"ota\":{"
            "\"status\":\"%s\","
            "\"progress\":%d,"
            "\"version\":\"%s\","
            "\"server\":\"%s\""
        "},"
        "\"system\":{"
            "\"version\":\"%s\","
            "\"uptime\":%ld,"
            "\"ntp_ok\":%s"
        "}"
        "}",
        /* mqtt */
        mqtt_ok ? "true" : "false",
        mqtt_retries,
        MQTT_BROKER, MQTT_PORT,
        MQTT_TOPIC,
        /* modbus */
        modbus_count > 0 ? "true" : "false",
        modbus_count,
         rs485_tx_count,
        /* can */
        can_count > 0 ? "true" : "false",
        can_count,
         can_tx_count, can_rx_count,
        /* ota */
        ota_str, ota_get_progress(),
        ver, OTA_DEFAULT_SERVER,
        /* system */
         ver, app_get_uptime_seconds(),
        ntp_ok ? "true" : "false");

    web_send_response(client_fd, 200, "application/json", json, len);
}
