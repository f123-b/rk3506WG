/**
 * @file    api_system.c
 * @brief   系统 API 实现 (从 web_server.c 提取)
 */

#include "api_system.h"
#include "../web_server.h"
#include "../ntp_sync.h"
#include "../ota_manager.h"
#include "../app_config.h"
#include "../infra/logger.h"
#include "../services/mqtt_client.h"
#include "../database.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>

/* 外部: web_server.c 提供的 HTTP 响应发送函数 */
extern void web_send_response(int client_fd, int status,
                              const char *content_type,
                              const char *body, size_t body_len);

/** GET /api/system/info — 返回系统信息 */
void api_system_handle_info(int client_fd)
{
    bool ntp_ok;
    time_t ntp_last;
    ntp_sync_get_status(&ntp_ok, &ntp_last);

    char ntp_time_str[32] = "never";
    if (ntp_last > 0) {
        struct tm tm_buf;
        localtime_r(&ntp_last, &tm_buf);
        strftime(ntp_time_str, sizeof(ntp_time_str),
                 "%Y-%m-%d %H:%M:%S", &tm_buf);
    }

    char local_ver[32];
    ota_get_local_version(local_ver, sizeof(local_ver));

    ota_status_t ota_st = ota_get_status();
    const char *ota_status_str = "idle";
    switch (ota_st) {
        case OTA_CHECKING:    ota_status_str = "checking"; break;
        case OTA_DOWNLOADING: ota_status_str = "downloading"; break;
        case OTA_VERIFYING:   ota_status_str = "verifying"; break;
        case OTA_APPLYING:    ota_status_str = "applying"; break;
        case OTA_SUCCESS:     ota_status_str = "success"; break;
        case OTA_FAILED:      ota_status_str = "failed"; break;
        default: break;
    }

    struct sysinfo si;
    long uptime = (sysinfo(&si) == 0) ? si.uptime : 0;

    char json[1024];
    int len = snprintf(json, sizeof(json),
        "{"
        "\"device\":\"RK3506\","
        "\"role\":\"Edge Gateway + Display\","
        "\"os\":\"Linux\","
        "\"ui\":\"LVGL\","
        "\"protocols\":[\"MQTT\",\"Modbus RTU\",\"CAN\"],"
        "\"database\":\"SQLite3\","
        "\"version\":\"%s\","
        "\"ntp_synced\":%s,"
        "\"ntp_last_sync\":\"%s\","
        "\"ota_status\":\"%s\","
        "\"ota_progress\":%d,"
        "\"uptime\":%ld"
        "}",
        local_ver,
        ntp_ok ? "true" : "false",
        ntp_time_str,
        ota_status_str,
        ota_get_progress(),
        uptime);

    web_send_response(client_fd, 200, "application/json", json, len);
}

/** GET /api/health — 健康检查 (新增) */
void api_system_handle_health(int client_fd)
{
    bool ntp_ok;
    time_t ntp_last;
    ntp_sync_get_status(&ntp_ok, &ntp_last);

    bool mqtt_ok = mqtt_client_is_connected();
    bool db_ok = database_is_ready();

    struct statvfs vfs;
    const char *disk_status = "unknown";
    double disk_free_pct = 0.0;
    if (statvfs("/", &vfs) == 0 && vfs.f_blocks > 0) {
        disk_free_pct = (double)vfs.f_bavail / (double)vfs.f_blocks * 100.0;
        if (disk_free_pct > 20.0) disk_status = "ok";
        else if (disk_free_pct > 10.0) disk_status = "warning";
        else disk_status = "critical";
    }

    struct sysinfo si;
    long uptime = 0;
    double memory_free_pct = 0.0;
    const char *memory_status = "unknown";
    if (sysinfo(&si) == 0) {
        uptime = si.uptime;
        unsigned long long total =
            (unsigned long long)si.totalram * (unsigned long long)si.mem_unit;
        unsigned long long free_mem =
            ((unsigned long long)si.freeram + (unsigned long long)si.bufferram) *
            (unsigned long long)si.mem_unit;
        if (total > 0) {
            memory_free_pct = (double)free_mem / (double)total * 100.0;
            if (memory_free_pct > 20.0) memory_status = "ok";
            else if (memory_free_pct > 10.0) memory_status = "warning";
            else memory_status = "critical";
        }
    }

    bool critical = !db_ok || disk_free_pct <= 10.0 || memory_free_pct <= 10.0;
    bool degraded = critical || !mqtt_ok || !ntp_ok;

    char json[768];
    int len = snprintf(json, sizeof(json),
        "{"
        "\"status\":\"%s\","
        "\"mqtt\":\"%s\","
        "\"ntp\":\"%s\","
        "\"ntp_last_sync\":%ld,"
        "\"database\":\"%s\","
        "\"disk\":\"%s\","
        "\"disk_free_pct\":%.1f,"
        "\"memory\":\"%s\","
        "\"memory_free_pct\":%.1f,"
        "\"uptime\":%ld,"
        "\"version\":\"%s\""
        "}",
        critical ? "critical" : (degraded ? "degraded" : "healthy"),
        mqtt_ok ? "connected" : "disconnected",
        ntp_ok ? "synced" : "unsynced",
        (long)ntp_last,
        db_ok ? "ok" : "error",
        disk_status,
        disk_free_pct,
        memory_status,
        memory_free_pct,
        uptime,
        APP_VERSION);

    web_send_response(client_fd, 200, "application/json", json, len);
}
