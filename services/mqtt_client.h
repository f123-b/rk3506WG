/**
 * @file    mqtt_client.h
 * @brief   MQTT 客户端接口 (Mosquitto 封装)
 *
 * 原理:
 *   MQTT (Message Queuing Telemetry Transport) 是物联网最常用的消息协议。
 *   它基于 TCP 长连接 + 发布/订阅 (Pub/Sub) 模型:
 *     - Broker (消息代理): 中转服务器, 如 Mosquitto/EMQX
 *     - Publisher (发布者): 发送消息到指定 Topic
 *     - Subscriber (订阅者): 接收感兴趣的 Topic 消息
 *
 *   本模块使用 libmosquitto 库实现 MQTT 客户端:
 *     - 自动连接 + 退避重连 (5s → 10s → 30s → 60s)
 *     - JSON 消息解析 (cJSON)
 *     - 线程安全的数据回调
 *     - MQTT 状态查询
 *
 * 如何修改:
 *   - 修改 MQTT Broker 地址: 编辑 app_config.h 中的 MQTT_BROKER
 *   - 修改订阅主题: 编辑 app_config.h 中的 MQTT_TOPIC
 *   - 连接多主题: 在 on_connect 回调中添加更多 mosquitto_subscribe 调用
 */

#ifndef SERVICES_MQTT_CLIENT_H
#define SERVICES_MQTT_CLIENT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** MQTT 数据回调: 收到新传感器数据时调用 */
typedef void (*mqtt_data_callback_t)(float temperature, float humidity,
                                     bool valid);

/**
 * @brief 初始化 MQTT 客户端并连接 Broker
 * @param broker     MQTT Broker 地址 (如 "192.168.5.10")
 * @param port       Broker 端口 (默认 1883)
 * @param topic      订阅主题 (如 "esp32c6/sensor")
 * @param data_cb    收到传感器数据时的回调函数 (可 NULL)
 * @return 0=成功, -1=失败
 */
int mqtt_client_init(const char *broker, int port, const char *topic,
                     mqtt_data_callback_t data_cb);

/**
 * @brief 设置设备认证信息 (华为云IoT等平台需要)
 * @param device_id     设备ID (用户名)
 * @param device_secret 设备密钥 (密码, 或用于生成HMAC签名)
 * @note  推荐在 mqtt_client_init() 之前调用；也支持运行中更新，下一次连接生效
 */
void mqtt_client_set_auth(const char *device_id, const char *device_secret);

/**
 * @brief 启动 MQTT 后台网络循环
 * @return 0=成功, -1=失败
 */
int mqtt_client_start(void);

/**
 * @brief 获取连接状态
 */
bool mqtt_client_is_connected(void);

/**
 * @brief 获取重连次数 (0=未重连或已连接)
 */
int mqtt_client_get_retry_count(void);

/**
 * @brief 手动触发重连
 */
void mqtt_client_reconnect(void);

void mqtt_client_retry_tick(void);

/**
 * @brief 停止并清理 MQTT 客户端
 */
void mqtt_client_stop(void);

/**
 * @brief 发布消息到 MQTT Broker
 * @param topic   主题
 * @param payload 消息内容 (JSON 字符串)
 * @param qos     QoS 级别 (0/1/2), 默认 0
 * @param retain  是否保留消息
 * @return 0=成功, -1=失败
 */
int mqtt_client_publish(const char *topic, const char *payload,
                        int qos, bool retain);

#ifdef __cplusplus
}
#endif

#endif /* SERVICES_MQTT_CLIENT_H */
