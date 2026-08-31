/**
 * @file    api_system.h
 * @brief   系统 API 处理器 — /api/system/info + /api/health
 *
 * 从 web_server.c 中提取，遵循单一职责原则。
 *
 * API 端点:
 *   GET /api/system/info  → 设备信息 (版本/NTP/MQTT/OTA/磁盘/内存)
 *   GET /api/health       → 健康检查 (各子系统状态汇总)
 *
 * 健康检查原理:
 *   返回 JSON 格式的各子系统状态，监控系统可据此判断设备是否健康:
 *     - mqtt:     connected / disconnected
 *     - ntp:      synced / unsynced
 *     - database: ok / error
 *     - disk:     ok (可用空间>10%) / warning / critical
 *     - uptime:   运行秒数
 */

#ifndef WEB_API_SYSTEM_H
#define WEB_API_SYSTEM_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 处理 GET /api/system/info
 * @param client_fd  客户端 socket 文件描述符
 */
void api_system_handle_info(int client_fd);

/**
 * @brief 处理 GET /api/health (健康检查)
 * @param client_fd  客户端 socket 文件描述符
 */
void api_system_handle_health(int client_fd);

#ifdef __cplusplus
}
#endif

#endif /* WEB_API_SYSTEM_H */
