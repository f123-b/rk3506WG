/**
 * @file    api_device.h
 *   GET /api/device/list         → 所有设备及其最新数据
 *   GET /api/device/modbus       → Modbus 设备数据
 *   GET /api/device/can          → CAN 信号数据
 *
 * 端点:
 *   GET /api/device/list         → 所有设备及其最新数据
 *   GET /api/device/modbus       → Modbus 设备数据
 *   GET /api/device/can          → CAN 信号数据
 */

#ifndef WEB_API_DEVICE_H
#define WEB_API_DEVICE_H

#ifdef __cplusplus
extern "C" {
#endif

/** 处理 GET /api/device/list */
void api_device_handle_list(int client_fd);

/** 处理 GET /api/device/modbus */
void api_device_handle_modbus(int client_fd);

/** 处理 GET /api/device/can */
void api_device_handle_can(int client_fd);

#ifdef __cplusplus
}
#endif

#endif /* WEB_API_DEVICE_H */
