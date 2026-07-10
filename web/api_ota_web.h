/**
 * @file    api_ota_web.h
 *   GET  /api/ota/check  → 检查是否有新版本
 *   GET  /api/ota/status → 获取当前 OTA 状态和下载进度
 *   POST /api/ota/start  → 触发 OTA 下载升级 (异步, 立即返回)
 *
 * 从 web_server.c 中提取的 OTA 相关 API 端点:
 *   GET  /api/ota/check  → 检查是否有新版本
 *   GET  /api/ota/status → 获取当前 OTA 状态和下载进度
 *   POST /api/ota/start  → 触发 OTA 下载升级 (异步, 立即返回)
 */

#ifndef WEB_API_OTA_H
#define WEB_API_OTA_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 处理 GET /api/ota/check
 */
void api_ota_handle_check(int client_fd);

/**
 * @brief 处理 GET /api/ota/status
 */
void api_ota_handle_status(int client_fd);

/**
 * @brief 处理 POST /api/ota/start
 * @param body  POST body (可 NULL)
 */
void api_ota_handle_start(int client_fd, const char *body);

#ifdef __cplusplus
}
#endif

#endif /* WEB_API_OTA_H */
