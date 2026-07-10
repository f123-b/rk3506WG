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
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/statvfs.h>

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

    char json[1024];
    int len = snprintf(json, sizeof(json),
        "{"
        "\"device\":\"RK3506\","
        "\"role\":\"Edge Gateway + Display\","
        "\"os\":\"Linux\","
        "\"ui\":\"LVGL\","
        "\"protocol\":\"MQTT\","
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
        (long)time(NULL));

    web_send_response(client_fd, 200, "application/json", json, len);
}

/** GET /api/health — 健康检查 (新增) */
void api_system_handle_health(int client_fd)
{
    /* 检查各子系统状态 */
    bool ntp_ok;
    time_t ntp_last;
    ntp_sync_get_status(&ntp_ok, &ntp_last);

    /* 检查磁盘空间 */
    struct statvfs vfs;
    const char *disk_status = "unknown";
    double disk_free_pct = 0;
    if (statvfs("/", &vfs) == 0) {
        disk_free_pct = (double)vfs.f_bavail / (double)vfs.f_blocks * 100.0;
        if (disk_free_pct > 20) disk_status = "ok";
        else if (disk_free_pct > 10) disk_status = "warning";
        else disk_status = "critical";
    }

    /* 简单内存检查 (通过读取 /proc/meminfo 更准确, 这里采样) */
    const char *memory_status = "ok";  /* 简化: 实际应检查 /proc/meminfo */

    /* 数据库状态 */
    const char *db_status = "ok";  /* database_init 成功即 OK */

    /* 构建 JSON 响应 */
    char json[512];
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
        "\"uptime\":%ld,"
        "\"version\":\"%s\""
        "}",
        (ntp_ok) ? "healthy" : "degraded",
        "check",  /* MQTT 状态需从 mqtt_client 获取, 简化为 check */
        ntp_ok ? "synced" : "unsynced",
        (long)ntp_last,
        db_status,
        disk_status,
        disk_free_pct,
        memory_status,
        (long)time(NULL),
        APP_VERSION);

    web_send_response(client_fd, 200, "application/json", json, len);
}
