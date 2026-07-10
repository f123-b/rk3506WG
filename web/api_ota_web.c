/**
 * @file    api_ota_web.c
 * @brief   OTA Web API — 线程安全 + 并发守护
 *
 * 端点:
 *   GET  /api/ota/check  — 检查更新
 *   GET  /api/ota/status — 查询状态
 *   POST /api/ota/start  — 开始升级 (异步, 有并发锁守卫)
 */

#include "api_ota_web.h"
#include "../web_server.h"
#include "../ota_manager.h"
#include "../infra/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* web_server.c 提供的 HTTP 响应发送函数 */
extern void web_send_response(int client_fd, int status,
                              const char *content_type,
                              const char *body, size_t body_len);

/* ==================== OTA 后台下载线程 ==================== */

static void *ota_download_thread(void *arg)
{
    (void)arg;
    ota_version_info_t info;

    LOG_INFO("Web OTA: checking for updates...");
    if (ota_check_update(&info)) {
        LOG_INFO("Web OTA: downloading version %s...", info.version);
        ota_download_and_apply();
        /* ota_download_and_apply → ota_apply_app_update → _exit(0)
         * 正常情况下不会执行到这里 */
    }

    /* 解锁 OTA 操作 (如果失败或没有更新) */
    ota_unlock();
    LOG_INFO("Web OTA: thread finished");
    return NULL;
}

/* ==================== API 处理器 ==================== */

/** GET /api/ota/check — 检查 OTA 更新 (线程安全) */
void api_ota_handle_check(int client_fd)
{
    /* 先检查是否正在 OTA */
    ota_status_t st = ota_get_status();
    if (st == OTA_DOWNLOADING || st == OTA_VERIFYING ||
        st == OTA_APPLYING || st == OTA_PATCHING) {
        char json[128];
        snprintf(json, sizeof(json),
            "{\"update_available\":false,"
            "\"error\":\"OTA in progress, please wait\"}");
        web_send_response(client_fd, 200, "application/json",
                          json, strlen(json));
        return;
    }

    ota_version_info_t info;
    bool has_update = ota_check_update(&info);

    char json[1024];
    if (has_update) {
        /* 转义 changelog 中的特殊字符 */
        char escaped[512];
        char *d = escaped, *s = info.changelog;
        while (*s && (d - escaped) < (int)sizeof(escaped) - 2) {
            if (*s == '"') { *d++ = '\\'; *d++ = '"'; }
            else if (*s == '\\') { *d++ = '\\'; *d++ = '\\'; }
            else if (*s == '\n') { *d++ = '\\'; *d++ = 'n'; }
            else *d++ = *s;
            s++;
        }
        *d = '\0';

        snprintf(json, sizeof(json),
            "{"
            "\"update_available\":true,"
            "\"update_type\":\"%s\","
            "\"version\":\"%s\","
            "\"build_date\":\"%s\","
            "\"size\":%lld,"
            "\"changelog\":\"%s\","
            "\"force_update\":%s,"
            "\"has_delta\":%s"
            "}",
            info.update_type[0] ? info.update_type : "firmware",
            info.version, info.build_date,
            (long long)info.size,
            escaped,
            info.force_update ? "true" : "false",
            info.has_delta ? "true" : "false");
    } else {
        snprintf(json, sizeof(json),
            "{"
            "\"update_available\":false,"
            "\"error\":\"%s\""
            "}",
            ota_get_last_error_msg());
    }

    web_send_response(client_fd, 200, "application/json", json, strlen(json));
}

/** GET /api/ota/status — 获取 OTA 当前状态 */
void api_ota_handle_status(int client_fd)
{
    ota_status_t st = ota_get_status();
    const char *status_str = "idle";
    switch (st) {
        case OTA_CHECKING:    status_str = "checking"; break;
        case OTA_DOWNLOADING: status_str = "downloading"; break;
        case OTA_VERIFYING:   status_str = "verifying"; break;
        case OTA_APPLYING:    status_str = "applying"; break;
        case OTA_PATCHING:    status_str = "patching"; break;
        case OTA_SUCCESS:     status_str = "success"; break;
        case OTA_FAILED:      status_str = "failed"; break;
        default: break;
    }

    char json[256];
    snprintf(json, sizeof(json),
        "{"
        "\"status\":\"%s\","
        "\"progress\":%d,"
        "\"error\":\"%s\""
        "}",
        status_str,
        ota_get_progress(),
        ota_get_last_error_msg());

    web_send_response(client_fd, 200, "application/json", json, strlen(json));
}

/** POST /api/ota/start — 开始 OTA 下载升级 (有并发锁守卫) */
void api_ota_handle_start(int client_fd, const char *body)
{
    (void)body;

    /* ★ 尝试获取 OTA 锁 (防止重复启动) */
    if (!ota_try_lock()) {
        char json[128];
        snprintf(json, sizeof(json),
            "{\"success\":false,\"status\":\"busy\","
            "\"error\":\"OTA already in progress\"}");
        web_send_response(client_fd, 200, "application/json",
                          json, strlen(json));
        return;
    }

    /* 检查当前状态 */
    ota_status_t st = ota_get_status();
    if (st == OTA_DOWNLOADING || st == OTA_VERIFYING || st == OTA_APPLYING) {
        ota_unlock();
        char json[128];
        snprintf(json, sizeof(json),
            "{\"success\":false,\"status\":\"busy\","
            "\"error\":\"OTA already in progress\"}");
        web_send_response(client_fd, 200, "application/json",
                          json, strlen(json));
        return;
    }

    /* 后台线程执行 OTA (线程结束时自动 unlock) */
    ota_cancel();
    pthread_t tid;
    if (pthread_create(&tid, NULL, ota_download_thread, NULL) != 0) {
        ota_unlock();
        char json[128];
        snprintf(json, sizeof(json),
            "{\"success\":false,\"error\":\"Failed to start OTA thread\"}");
        web_send_response(client_fd, 200, "application/json",
                          json, strlen(json));
        return;
    }
    pthread_detach(tid);

    char json[128];
    snprintf(json, sizeof(json),
        "{\"success\":true,\"status\":\"started\","
        "\"message\":\"OTA started in background\"}");
    web_send_response(client_fd, 200, "application/json", json, strlen(json));
}
