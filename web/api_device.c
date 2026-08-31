/**
 * @file    api_device.c
 * @brief   设备数据 API 实现
 */

#include "api_device.h"
#include "../web_server.h"
#include "../services/data_bus.h"
#include <stdio.h>
#include <string.h>

extern void web_send_response(int client_fd, int status,
                              const char *content_type,
                              const char *body, size_t body_len);

/** GET /api/device/list */
void api_device_handle_list(int client_fd)
{
    data_point_t points[DATA_BUS_MAX_POINTS];
    int count = data_bus_get_all(points, DATA_BUS_MAX_POINTS);

    char json[16384];
    char *p = json;
    int rem = (int)sizeof(json) - 1;
    int w;

    w = snprintf(p, rem, "{\"devices\":[");
    p += w; rem -= w;

    for (int i = 0; i < count; i++) {
        data_point_t *pt = &points[i];
        const char *src;
        switch (pt->source) {
            case DATA_SOURCE_MQTT:   src = "mqtt";   break;
            case DATA_SOURCE_MODBUS: src = "modbus"; break;
            case DATA_SOURCE_CAN:    src = "can";    break;
            default:                 src = "unknown"; break;
        }

        w = snprintf(p, rem,
            "%s{\"source\":\"%s\",\"device\":\"%s\",\"point\":\"%s\","
            "\"value\":%.2f,\"unit\":\"%s\",\"valid\":%s}",
            i > 0 ? "," : "",
            src, pt->device_name, pt->point_name,
            pt->value, pt->unit,
            pt->valid ? "true" : "false");
        p += w; rem -= w;
    }

    w = snprintf(p, rem, "],\"total\":%d}", count);
    p += w;

    web_send_response(client_fd, 200, "application/json", json, p - json);
}

/** 辅助: 输出指定来源的设备数据 */
static void output_source_devices(int client_fd, data_source_t source)
{
    data_point_t points[DATA_BUS_MAX_POINTS];
    int count = data_bus_get_by_source(source, points, DATA_BUS_MAX_POINTS);

    char json[16384];
    char *p = json;
    int rem = (int)sizeof(json) - 1;
    int w;

    w = snprintf(p, rem, "{\"source\":\"%s\",\"count\":%d,\"devices\":[",
                 source == DATA_SOURCE_MODBUS ? "modbus" : "can", count);
    p += w; rem -= w;

    for (int i = 0; i < count; i++) {
        data_point_t *pt = &points[i];
        w = snprintf(p, rem,
            "%s{\"device\":\"%s\",\"point\":\"%s\",\"value\":%.2f,"
            "\"unit\":\"%s\",\"valid\":%s,\"timestamp\":%ld}",
            i > 0 ? "," : "",
            pt->device_name, pt->point_name,
            pt->value, pt->unit,
            pt->valid ? "true" : "false",
            (long)pt->timestamp);
        p += w; rem -= w;
    }

    w = snprintf(p, rem, "]}");
    p += w;

    web_send_response(client_fd, 200, "application/json", json, p - json);
}

void api_device_handle_modbus(int client_fd)
{
    output_source_devices(client_fd, DATA_SOURCE_MODBUS);
}

void api_device_handle_can(int client_fd)
{
    output_source_devices(client_fd, DATA_SOURCE_CAN);
}
