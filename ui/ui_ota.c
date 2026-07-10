/**
 * @file    ui_ota.c
 * @brief   OTA 升级 UI 实现 — 两步流程: Check Only | Install
 */

#include "ui_ota.h"
#include "ui_dashboard.h"
#include "ui_page_ota.h"
#include "../ota_manager.h"
#include "../infra/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>

/* ==================== 模块状态 ==================== */
static pthread_t ota_thread;
static bool ota_running = false;
static bool ota_install_mode = false;  /* true=下载安装, false=仅检查 */

/* 最近一次检查结果缓存 */
static char  last_version[32] = "";
static char  last_changelog[512] = "";
static bool  last_update_available = false;

/* ==================== 仅检查更新 ==================== */
static void *ota_check_thread_func(void *arg)
{
    (void)arg;
    ota_version_info_t info;

    LOG_INFO("OTA check-only thread started");
    ota_cancel();  /* 重置状态 */

    if (ota_check_update(&info)) {
        /* 有新版本 */
        strncpy(last_version, info.version, sizeof(last_version) - 1);
        strncpy(last_changelog, info.changelog, sizeof(last_changelog) - 1);
        last_update_available = true;
        LOG_INFO("OTA check: new version %s found", info.version);
    } else {
        last_update_available = false;
        LOG_INFO("OTA check: no update available (%s)", ota_get_last_error_msg());
    }

    ota_running = false;
    LOG_INFO("OTA check-only thread finished");
    return NULL;
}

/* ==================== 下载 + 安装 ==================== */
static void *ota_install_thread_func(void *arg)
{
    (void)arg;
    ota_version_info_t info;

    LOG_INFO("OTA install thread started");
    ota_cancel();

    if (ota_check_update(&info)) {
        LOG_INFO("OTA install: new version %s found, downloading...", info.version);
        ota_download_and_apply();
        /* ota_download_and_apply → ota_apply_app_update → 后台脚本替换 + 退出进程
         * 正常情况下不会执行到这里 */
    }

    ota_running = false;
    LOG_INFO("OTA install thread finished (no restart needed or failed)");
    return NULL;
}

/* ==================== 公开 API ==================== */

bool ui_ota_check(void)
{
    if (ota_running) {
        LOG_WARN("OTA already running");
        return false;
    }

    ota_running = true;
    ota_install_mode = false;

    if (pthread_create(&ota_thread, NULL, ota_check_thread_func, NULL) != 0) {
        LOG_ERROR("Failed to create OTA check thread");
        ota_running = false;
        return false;
    }

    LOG_INFO("OTA check-only started");
    return true;
}

bool ui_ota_start(void)
{
    if (ota_running) {
        LOG_WARN("OTA already running");
        return false;
    }

    ota_running = true;
    ota_install_mode = true;

    if (pthread_create(&ota_thread, NULL, ota_install_thread_func, NULL) != 0) {
        LOG_ERROR("Failed to create OTA install thread");
        ota_running = false;
        return false;
    }

    LOG_INFO("OTA install started");
    return true;
}

bool ui_ota_get_last_check(char *version, char *changelog)
{
    if (!last_update_available) return false;
    if (version) strncpy(version, last_version, 31);
    if (changelog) strncpy(changelog, last_changelog, 511);
    return true;
}

void ui_ota_poll(lv_timer_t *timer)
{
    (void)timer;

    ota_status_t st = ota_get_status();
    int pct = ota_get_progress();
    const char *err = ota_get_last_error_msg();

    /* --- 更新 OTA 页面状态标签 --- */
    char status_text[128] = "";
    int status_color = 0xcbd5e1;  /* 默认灰色 */

    switch (st) {
        case OTA_IDLE:
            if (!ota_running) {
                if (last_update_available) {
                    snprintf(status_text, sizeof(status_text),
                             "New version: %s", last_version);
                    status_color = 0x10b981;
                } else if (last_version[0]) {
                    snprintf(status_text, sizeof(status_text),
                             "Already latest (%s)", last_version);
                    status_color = 0x64748b;
                } else {
                    snprintf(status_text, sizeof(status_text), "Idle");
                }
            }
            break;

        case OTA_CHECKING:
            snprintf(status_text, sizeof(status_text), "# Checking for updates...");
            status_color = 0xf59e0b;
            break;

        case OTA_DOWNLOADING:
            snprintf(status_text, sizeof(status_text), "Downloading %d%%", pct);
            status_color = 0x3b82f6;
            break;

        case OTA_VERIFYING:
            snprintf(status_text, sizeof(status_text), "# Verifying SHA256...");
            status_color = 0xf59e0b;
            break;

        case OTA_APPLYING:
            if (ota_install_mode) {
                snprintf(status_text, sizeof(status_text), "# Installing, restarting...");
                status_color = 0x10b981;
            } else {
                snprintf(status_text, sizeof(status_text), "# Applying...");
                status_color = 0x10b981;
            }
            break;

        case OTA_PATCHING:
            snprintf(status_text, sizeof(status_text), "# Patching...");
            status_color = 0xf59e0b;
            break;

        case OTA_SUCCESS:
            snprintf(status_text, sizeof(status_text), "# Update complete!");
            status_color = 0x10b981;
            break;

        case OTA_FAILED:
            snprintf(status_text, sizeof(status_text), "Failed: %s",
                     err && err[0] ? err : "unknown");
            status_color = 0xef4444;
            break;
    }

    /* 更新 OTA 页面的状态和进度条 */
    if (status_text[0]) {
        ui_page_ota_update(status_text, pct);
    }
}

bool ui_ota_is_running(void)
{
    return ota_running;
}
