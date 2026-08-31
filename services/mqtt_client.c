/**
 * @file    mqtt_client.c
 * @brief   MQTT 客户端实现 (从 main.c 提取)
 *
 * 重连策略 (指数退避):
 *   重连间隔按 5s → 10s → 30s → 60s 递增，
 *   第5次及之后使用 60s 周期。避免频繁重连耗尽服务器资源。
 */

#include "mqtt_client.h"
#include "../app_config.h"
#include "../infra/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdatomic.h>
#include <mosquitto.h>
#include <cjson/cJSON.h>

/* ==================== 模块状态 ==================== */
static struct mosquitto *mosq = NULL;
static atomic_bool connected = false;
static atomic_int  retry_count = 0;

static char broker_addr[128];
static int  broker_port;
static char topic_name[128];
static mqtt_data_callback_t user_data_callback = NULL;

/* 设备认证 (华为云IoT) */
static char device_id[64]   = {0};
static char device_secret[64] = {0};

/* ==================== MQTT 回调 ==================== */

static void on_connect_cb(struct mosquitto *m, void *obj, int rc)
{
    (void)obj;
    if (rc == 0) {
        atomic_store(&connected, true);
        atomic_store(&retry_count, 0);
        LOG_INFO("MQTT connected to %s:%d", broker_addr, broker_port);

        /* 订阅主题 */
        int sub_rc = mosquitto_subscribe(m, NULL, topic_name, 0);
        if (sub_rc == MOSQ_ERR_SUCCESS) {
            LOG_INFO("MQTT subscribed to %s", topic_name);
        } else {
            LOG_WARN("MQTT subscribe failed (rc=%d)", sub_rc);
        }
    } else {
        atomic_store(&connected, false);
        LOG_WARN("MQTT connect failed, rc=%d", rc);
    }
}

static void on_disconnect_cb(struct mosquitto *m, void *obj, int rc)
{
    (void)m; (void)obj;
    atomic_store(&connected, false);
    atomic_fetch_add(&retry_count, 1);
    LOG_WARN("MQTT disconnected (rc=%d), will reconnect", rc);
}

static void on_message_cb(struct mosquitto *m, void *obj,
                          const struct mosquitto_message *msg)
{
    (void)m; (void)obj;

    if (strcmp(msg->topic, topic_name) != 0) return;
    if (msg->payloadlen == 0) return;

    /* 拷贝 payload 并添加终止符 */
    char *payload = malloc(msg->payloadlen + 1);
    if (!payload) return;
    memcpy(payload, msg->payload, msg->payloadlen);
    payload[msg->payloadlen] = '\0';

    /* 解析 JSON: {"temperature": xx, "humidity": xx, "valid": true/false} */
    cJSON *root = cJSON_Parse(payload);
    if (root) {
        cJSON *t = cJSON_GetObjectItem(root, "temperature");
        cJSON *h = cJSON_GetObjectItem(root, "humidity");
        cJSON *v = cJSON_GetObjectItem(root, "valid");

        if (t && cJSON_IsNumber(t) && h && cJSON_IsNumber(h)) {
            float temp = (float)t->valuedouble;
            float humi = (float)h->valuedouble;
            bool valid = v ? cJSON_IsTrue(v) : true;

            LOG_DEBUG("MQTT data: T=%.1f H=%.0f valid=%d", temp, humi, valid);

            /* 通知上层回调 */
            if (user_data_callback) {
                user_data_callback(temp, humi, valid);
            }
        }
        cJSON_Delete(root);
    }
    free(payload);
}

/* ==================== 公开 API ==================== */

int mqtt_client_init(const char *broker, int port, const char *topic,
                     mqtt_data_callback_t data_cb)
{
    /* 保存参数 */
    strncpy(broker_addr, broker, sizeof(broker_addr) - 1);
    broker_port = port;
    strncpy(topic_name, topic, sizeof(topic_name) - 1);
    user_data_callback = data_cb;

    /* 初始化 Mosquitto 库 */
    mosquitto_lib_init();

    /* 创建客户端实例 (NULL client_id = 自动生成) */
    mosq = mosquitto_new(NULL, true, NULL);
    if (!mosq) {
        LOG_ERROR("mosquitto_new failed");
        return -1;
    }

    /* 注册回调 */
    mosquitto_connect_callback_set(mosq, on_connect_cb);
    mosquitto_disconnect_callback_set(mosq, on_disconnect_cb);
    mosquitto_message_callback_set(mosq, on_message_cb);

    /* 设置设备认证 (华为云IoT) */
    if (device_id[0] && device_secret[0]) {
        mosquitto_username_pw_set(mosq, device_id, device_secret);
        LOG_INFO("MQTT auth set: device_id=%s", device_id);
    }

    return 0;
}

int mqtt_client_start(void)
{
    if (!mosq) return -1;

    int rc = mosquitto_reconnect_delay_set(mosq, true, 60, false);
    if (rc != MOSQ_ERR_SUCCESS) {
        LOG_WARN("MQTT reconnect delay setup failed (rc=%d)", rc);
    }

    rc = mosquitto_connect_async(mosq, broker_addr, broker_port,
                                 MQTT_KEEPALIVE);
    if (rc != MOSQ_ERR_SUCCESS) {
        LOG_WARN("MQTT initial connect failed (rc=%d), will retry", rc);
    }

    rc = mosquitto_loop_start(mosq);
    if (rc != MOSQ_ERR_SUCCESS) {
        LOG_ERROR("mosquitto_loop_start failed (rc=%d)", rc);
        return -1;
    }

    return 0;
}

void mqtt_client_set_auth(const char *id, const char *secret)
{
    if (!id || !secret) return;
    strncpy(device_id, id, sizeof(device_id) - 1);
    strncpy(device_secret, secret, sizeof(device_secret) - 1);
    LOG_INFO("MQTT auth credentials stored for device: %s", device_id);
}

bool mqtt_client_is_connected(void)
{
    return atomic_load(&connected);
}

int mqtt_client_get_retry_count(void)
{
    return atomic_load(&retry_count);
}

void mqtt_client_reconnect(void)
{
    if (!mosq) return;

    atomic_store(&retry_count, 0);
    int rc = mosquitto_reconnect_async(mosq);
    if (rc != MOSQ_ERR_SUCCESS) {
        LOG_WARN("MQTT manual reconnect failed (rc=%d)", rc);
    }
}

int mqtt_client_publish(const char *topic, const char *payload,
                        int qos, bool retain)
{
    if (!mosq || !connected) return -1;
    if (!topic || !payload) return -1;

    int rc = mosquitto_publish(mosq, NULL, topic,
                               (int)strlen(payload), payload, qos, retain);
    if (rc != MOSQ_ERR_SUCCESS) {
        LOG_WARN("MQTT publish to %s failed (rc=%d)", topic, rc);
        return -1;
    }

    LOG_DEBUG("MQTT publish: %s = %s", topic, payload);
    return 0;
}

/**
 * @brief 周期性调用此函数进行退避重连
 *
 * 应由 LVGL 定时器或主循环定时器调用 (例如每 5 秒一次)。
 * 当连接断开且重试次数>0时，按退避策略重连。
 */
void mqtt_client_retry_tick(void)
{
    if (!mosq || atomic_load(&connected)) return;

    /* 计算退避延迟 */
    int delays[] = {5, 10, 30, 60};  /* 秒 */
    int retries = atomic_load(&retry_count);
    int idx = (retries > 0 && retries <= 4) ? (retries - 1) : 3;
    int delay = delays[idx];

    /* 仅当距离上次重连超过退避间隔时才重试 */
    static int tick_count = 0;
    tick_count++;
    if (tick_count >= delay) {
        tick_count = 0;
        int attempt = atomic_fetch_add(&retry_count, 1) + 1;
        LOG_INFO("MQTT reconnect attempt #%d (delay=%ds)", attempt, delay);

        int rc = mosquitto_reconnect_async(mosq);
        if (rc == MOSQ_ERR_SUCCESS) {
            LOG_INFO("MQTT reconnected");
        }
    }
}

void mqtt_client_stop(void)
{
    if (mosq) {
        mosquitto_loop_stop(mosq, true);
        mosquitto_destroy(mosq);
        mosq = NULL;
    }
    mosquitto_lib_cleanup();
    atomic_store(&connected, false);
    LOG_INFO("MQTT client stopped");
}
