/**
 * @file    mqtt_client_stub.c
 * @brief   MQTT 客户端 stub — host 模拟, 定时产生模拟传感器数据
 *
 * 不需要 libmosquitto, 通过定时器产生模拟温湿度数据。
 */
#include "mqtt_client.h"
#include "../infra/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

static mqtt_data_callback_t g_cb = NULL;
static bool g_running = false;
static bool g_connected = false;
static pthread_t g_thread;

static void *sim_thread(void *arg)
{
    (void)arg;
    float t = 25.0f;
    float phase = 0.0f;
    while (g_running) {
        g_connected = true;
        /* 模拟温度: 正弦波 23~29℃ + 随机抖动，1秒更新一次 */
        phase += 0.15f;
        t = 25.5f + sinf(phase) * 2.5f + ((rand() % 100) - 50) * 0.03f;
        if (t < 22.0f) t = 22.0f;
        if (t > 30.0f) t = 30.0f;
        float h = 65.0f + cosf(phase * 0.7f) * 8.0f + ((rand() % 100) - 50) * 0.1f;
        if (h < 50.0f) h = 50.0f;
        if (h > 80.0f) h = 80.0f;
        if (g_cb) g_cb(t, h, true);
        printf("[MQTT SIM] temp=%.1f humi=%.0f\n", t, h);
        sleep(1);
    }
    return NULL;
}

int mqtt_client_init(const char *broker, int port, const char *topic,
                      mqtt_data_callback_t cb)
{
    (void)broker; (void)port; (void)topic;
    g_cb = cb;
    g_running = true;
    srand((unsigned)time(NULL));
    pthread_create(&g_thread, NULL, sim_thread, NULL);
    LOG_INFO("MQTT stub: simulated sensor data, broker=%s", broker);
    return 0;
}

int mqtt_client_start(void) { return 0; }
bool mqtt_client_is_connected(void) { return g_connected; }
int  mqtt_client_get_retry_count(void) { return 0; }
void mqtt_client_reconnect(void) { g_connected = true; }
void mqtt_client_retry_tick(void) { /* stub */ }

int mqtt_client_publish(const char *topic, const char *payload, int qos, bool retain)
{
    (void)topic; (void)payload; (void)qos; (void)retain;
    printf("[MQTT SIM] publish: %s\n", payload);
    return 0;
}

void mqtt_client_stop(void)
{
    g_running = false;
    if (g_thread) pthread_join(g_thread, NULL);
}
