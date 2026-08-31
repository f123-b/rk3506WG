/**
 * @file    web_server.h
 * @brief   RK3506 嵌入式 HTTP 服务器 — REST API + Web 仪表盘
 *
 * API 端点由 web_server.c 分发到 web/api_* 模块，包含当前数据、设备数据、
 * 系统健康、状态汇总、静态文件和应用 OTA 接口；不包含历史数据接口。
 *
 * 使用 POSIX socket + 自定义 HTTP/1.0 解析器，零外部依赖
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动 HTTP 服务器
 * @param port 监听端口 (默认 8080)
 * @return 0=成功, -1=失败
 */
int web_server_start(int port);

/**
 * @brief 停止 HTTP 服务器
 */
void web_server_stop(void);

/**
 * @brief 更新当前传感器数据（供 MQTT 回调调用）
 * @param temp  温度值
 * @param humi  湿度值
 * @param valid 数据有效性
 */
void web_server_update_data(float temp, float humi, bool valid);

/**
 * @brief 获取服务器运行状态
 * @return true=运行中, false=已停止
 */
bool web_server_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* WEB_SERVER_H */
