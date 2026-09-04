/**
 * @file    data_bus.c
 * @brief   统一数据总线实现
 */

#include "data_bus.h"
#include "../infra/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* ==================== 内部常量 ==================== */
#define MAX_SUBSCRIBERS  16   /**< 最大订阅者数量 */

/* ==================== 内部状态 ==================== */
static data_point_t  points[DATA_BUS_MAX_POINTS];
static int           point_count = 0;
static pthread_mutex_t bus_mutex = PTHREAD_MUTEX_INITIALIZER;

/* 订阅者 */
typedef struct {
    data_bus_callback_t callback;
    void               *user_data;
    bool                active;
} subscriber_t;

static subscriber_t subscribers[MAX_SUBSCRIBERS];
static int subscriber_count = 0;

/* ==================== 公开 API ==================== */

void data_bus_init(void)
{
    pthread_mutex_lock(&bus_mutex);

    memset(points, 0, sizeof(points));
    point_count = 0;

    memset(subscribers, 0, sizeof(subscribers));
    subscriber_count = 0;

    pthread_mutex_unlock(&bus_mutex);
    LOG_INFO("DataBus: initialized (max %d points, %d subscribers)",
             DATA_BUS_MAX_POINTS, MAX_SUBSCRIBERS);
}

void data_bus_publish(const data_point_t *point)
{
    if (!point) return;

    /* 回调列表与数据点都在锁内做快照，回调本身在解锁后执行。
     * 这样订阅者可以安全调用 DataBus API，也不会因慢 I/O 长时间占用总线锁。 */
    data_bus_callback_t callbacks[MAX_SUBSCRIBERS];
    void *callback_user_data[MAX_SUBSCRIBERS];
    int callback_count = 0;
    data_point_t updated_copy;

    pthread_mutex_lock(&bus_mutex);

    /* source + device_name + point_name 共同定义一个数据点，避免不同协议同名设备冲突 */
    int idx = -1;
    for (int i = 0; i < point_count; i++) {
        if (points[i].source == point->source &&
            strcmp(points[i].device_name, point->device_name) == 0 &&
            strcmp(points[i].point_name, point->point_name) == 0) {
            idx = i;
            break;
        }
    }

    if (idx >= 0) {
        points[idx].value = point->value;
        points[idx].valid = point->valid;
        points[idx].timestamp = point->timestamp;
        strncpy(points[idx].unit, point->unit, sizeof(points[idx].unit) - 1);
        points[idx].unit[sizeof(points[idx].unit) - 1] = '\0';
    } else if (point_count < DATA_BUS_MAX_POINTS) {
        idx = point_count;
        memcpy(&points[idx], point, sizeof(data_point_t));
        points[idx].id = (uint32_t)idx;
        point_count++;
    } else {
        pthread_mutex_unlock(&bus_mutex);
        LOG_WARN("DataBus: full, dropping point %s/%s",
                 point->device_name, point->point_name);
        return;
    }

    updated_copy = points[idx];
    for (int i = 0; i < subscriber_count && callback_count < MAX_SUBSCRIBERS; i++) {
        if (subscribers[i].active && subscribers[i].callback) {
            callbacks[callback_count] = subscribers[i].callback;
            callback_user_data[callback_count] = subscribers[i].user_data;
            callback_count++;
        }
    }

    pthread_mutex_unlock(&bus_mutex);

    for (int i = 0; i < callback_count; i++) {
        callbacks[i](&updated_copy, callback_user_data[i]);
    }
}

int data_bus_subscribe(data_bus_callback_t cb, void *user_data)
{
    pthread_mutex_lock(&bus_mutex);

    if (subscriber_count >= MAX_SUBSCRIBERS) {
        pthread_mutex_unlock(&bus_mutex);
        LOG_WARN("DataBus: subscriber slots full");
        return -1;
    }

    int id = subscriber_count;
    subscribers[id].callback = cb;
    subscribers[id].user_data = user_data;
    subscribers[id].active = true;
    subscriber_count++;

    pthread_mutex_unlock(&bus_mutex);
    return id;
}

void data_bus_unsubscribe(int sub_id)
{
    pthread_mutex_lock(&bus_mutex);
    if (sub_id >= 0 && sub_id < subscriber_count) {
        subscribers[sub_id].active = false;
    }
    pthread_mutex_unlock(&bus_mutex);
}

bool data_bus_get_latest(const char *device_name, const char *point_name,
                         data_point_t *point)
{
    pthread_mutex_lock(&bus_mutex);

    for (int i = 0; i < point_count; i++) {
        if (strcmp(points[i].device_name, device_name) == 0 &&
            strcmp(points[i].point_name, point_name) == 0) {
            if (point) memcpy(point, &points[i], sizeof(data_point_t));
            pthread_mutex_unlock(&bus_mutex);
            return true;
        }
    }

    pthread_mutex_unlock(&bus_mutex);
    return false;
}

int data_bus_get_all(data_point_t *out, int max_count)
{
    pthread_mutex_lock(&bus_mutex);

    int count = (point_count < max_count) ? point_count : max_count;
    if (out && count > 0) {
        memcpy(out, points, count * sizeof(data_point_t));
    }

    pthread_mutex_unlock(&bus_mutex);
    return count;
}

int data_bus_get_by_source(data_source_t source, data_point_t *out,
                           int max_count)
{
    pthread_mutex_lock(&bus_mutex);

    int count = 0;
    for (int i = 0; i < point_count && count < max_count; i++) {
        if (points[i].source == source) {
            if (out) {
                memcpy(&out[count], &points[i], sizeof(data_point_t));
            }
            count++;
        }
    }

    pthread_mutex_unlock(&bus_mutex);
    return count;
}

void data_bus_dump(void)
{
    pthread_mutex_lock(&bus_mutex);

    LOG_INFO("=== DataBus Dump (%d points) ===", point_count);
    for (int i = 0; i < point_count; i++) {
        data_point_t *p = &points[i];
        const char *src;
        switch (p->source) {
            case DATA_SOURCE_MQTT:   src = "MQTT";   break;
            case DATA_SOURCE_MODBUS: src = "Modbus"; break;
            case DATA_SOURCE_CAN:    src = "CAN";    break;
            default:                 src = "?";      break;
        }
        LOG_INFO("  [%s] %s/%s = %.2f %s (valid=%d)",
                 src, p->device_name, p->point_name,
                 p->value, p->unit, p->valid);
    }

    pthread_mutex_unlock(&bus_mutex);
}
