/**
 * @file    api_status.h
 * @brief   综合状态 API — Modbus / CAN / OTA / MQTT 聚合
 */
#ifndef WEB_API_STATUS_H
#define WEB_API_STATUS_H

#ifdef __cplusplus
extern "C" {
#endif

/** GET /api/status — 返回所有子系统状态的聚合 JSON */
void api_status_handle(int client_fd);

#ifdef __cplusplus
}
#endif

#endif /* WEB_API_STATUS_H */
