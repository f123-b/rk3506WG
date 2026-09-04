/**
 * @file    data_recorder.c
 * @brief   数据记录器实现 — 批量写入 + 离线缓存
 */

#include "data_recorder.h"
#include "mqtt_client.h"
#include "../app_config.h"
#include "../infra/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#include "../database.h"

#define DATA_BUF_MAX         120  /**< MQTT 温湿度缓冲 */
#define DEVICE_DATA_BUF_MAX  256  /**< Modbus/CAN 通用数据缓冲 */

/* ==================== 模块状态 ==================== */
static data_record_t buffer[DATA_BUF_MAX];
static int buffered_count = 0;
static data_point_t device_buffer[DEVICE_DATA_BUF_MAX];
static int device_buffered_count = 0;
static time_t last_flush_time = 0;
static pthread_mutex_t rec_mutex = PTHREAD_MUTEX_INITIALIZER;

/* 离线缓存: MQTT 断连期间的数据标记 */
static int offline_count = 0;

/* ==================== 内部 flush ==================== */

/**
 * @brief 在已经持有 rec_mutex 的情况下刷新缓冲区
 * @note 仅允许由本文件内部调用，避免 data_recorder_record() 缓冲区满时
 *       再次进入 data_recorder_flush() 造成普通 pthread mutex 自锁。
 */
static void data_recorder_flush_locked(void)
{
    if (buffered_count == 0 && device_buffered_count == 0) return;

    int written = 0;
    for (int i = 0; i < buffered_count; i++) {
        data_record_t *rec = &buffer[i];
        if (rec->valid &&
            database_insert(rec->temperature, rec->humidity, rec->valid) == 0) {
            written++;
        }
    }

    int device_written = 0;
    for (int i = 0; i < device_buffered_count; i++) {
        data_point_t *pt = &device_buffer[i];
        const char *source = "unknown";
        switch (pt->source) {
            case DATA_SOURCE_MQTT:   source = "mqtt"; break;
            case DATA_SOURCE_MODBUS: source = "modbus"; break;
            case DATA_SOURCE_CAN:    source = "can"; break;
            default: break;
        }

        if (database_insert_device_data_at(pt->timestamp, source,
                                           pt->device_name, pt->point_name,
                                           pt->value, pt->unit, pt->valid) == 0) {
            device_written++;
        }
    }

    LOG_DEBUG("Flushed sensor=%d/%d device=%d/%d records to DB",
              written, buffered_count, device_written, device_buffered_count);
    buffered_count = 0;
    device_buffered_count = 0;
    last_flush_time = time(NULL);
}

/* ==================== 公开 API ==================== */

int data_recorder_init(void)
{
    int rc = database_init();
    if (rc != 0) {
        LOG_WARN("Database init failed, history will not be saved");
        return -1;
    }

    last_flush_time = time(NULL);
    buffered_count = 0;
    device_buffered_count = 0;
    offline_count = 0;

    /* 清理过期数据 */
    database_cleanup(DATA_KEEP_DAYS);

    LOG_INFO("Data recorder: buffer=%d, interval=%ds, keep=%dd",
             DATA_BUF_MAX, DB_WRITE_INTERVAL, DATA_KEEP_DAYS);
    return 0;
}

void data_recorder_record(float temp, float humi, bool valid)
{
    pthread_mutex_lock(&rec_mutex);

    /* 缓冲区满时在当前锁域内直接刷新，避免递归加锁死锁 */
    if (buffered_count >= DATA_BUF_MAX) {
        LOG_WARN("Data buffer full, flushing...");
        data_recorder_flush_locked();
    }

    /* 添加到缓冲区 */
    data_record_t *rec = &buffer[buffered_count];
    rec->timestamp = time(NULL);
    rec->temperature = temp;
    rec->humidity = humi;
    rec->valid = valid;
    buffered_count++;

    /* 标记离线记录 */
    if (!mqtt_client_is_connected()) {
        offline_count++;
    }

    pthread_mutex_unlock(&rec_mutex);
}

void data_recorder_record_data_point(const data_point_t *point)
{
    if (!point) return;

    pthread_mutex_lock(&rec_mutex);
    if (device_buffered_count >= DEVICE_DATA_BUF_MAX) {
        LOG_WARN("Device data buffer full, flushing...");
        data_recorder_flush_locked();
    }

    device_buffer[device_buffered_count++] = *point;
    pthread_mutex_unlock(&rec_mutex);
}

void data_recorder_flush(void)
{
    pthread_mutex_lock(&rec_mutex);
    data_recorder_flush_locked();
    pthread_mutex_unlock(&rec_mutex);
}

int data_recorder_get_buffered_count(void)
{
    int count;
    pthread_mutex_lock(&rec_mutex);
    count = buffered_count + device_buffered_count;
    pthread_mutex_unlock(&rec_mutex);
    return count;
}

void data_recorder_tick(void)
{
    time_t now = time(NULL);
    bool should_flush;

    pthread_mutex_lock(&rec_mutex);
    should_flush = (now - last_flush_time >= DB_WRITE_INTERVAL);
    pthread_mutex_unlock(&rec_mutex);

    if (should_flush) {
        data_recorder_flush();
    }
}

void data_recorder_cleanup(int keep_days)
{
    database_cleanup(keep_days);
    LOG_INFO("Cleaned up data older than %d days", keep_days);
}

void data_recorder_close(void)
{
    /* 退出前写入所有缓冲数据 */
    data_recorder_flush();

    if (offline_count > 0) {
        LOG_INFO("Session had %d offline-cached records", offline_count);
    }

    database_close();
    LOG_INFO("Data recorder closed");
}
