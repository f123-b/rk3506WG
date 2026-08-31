/**
 * @file    data_recorder.c
 * @brief   数据记录器实现 — 内存批量写入 SQLite
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

#define DATA_BUF_MAX  120  /**< 缓冲区最大条数；实际时长取决于采样频率 */

/* ==================== 模块状态 ==================== */
static data_record_t buffer[DATA_BUF_MAX];
static int buffered_count = 0;
static time_t last_flush_time = 0;
static pthread_mutex_t rec_mutex = PTHREAD_MUTEX_INITIALIZER;

/* MQTT 断连期间的记录统计，不提供断线补传。 */
static int offline_count = 0;

static void data_recorder_flush_locked(void)
{
    if (buffered_count == 0) return;

    sensor_record_t records[DATA_BUF_MAX];
    int valid_count = 0;
    for (int i = 0; i < buffered_count; i++) {
        if (!buffer[i].valid) continue;
        records[valid_count].timestamp = buffer[i].timestamp;
        records[valid_count].temperature = buffer[i].temperature;
        records[valid_count].humidity = buffer[i].humidity;
        records[valid_count].valid = buffer[i].valid;
        valid_count++;
    }

    if (valid_count == 0 || database_insert_records(records, valid_count) == 0) {
        LOG_DEBUG("Flushed %d/%d records to DB", valid_count, buffered_count);
        buffered_count = 0;
        last_flush_time = time(NULL);
    } else {
        LOG_ERROR("Database batch insert failed; retaining %d buffered records",
                  buffered_count);
    }
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

    /* 缓冲区溢出时强制 flush */
    if (buffered_count >= DATA_BUF_MAX) {
        LOG_WARN("Data buffer full, flushing...");
        data_recorder_flush_locked();
    }

    if (buffered_count >= DATA_BUF_MAX) {
        LOG_ERROR("Data buffer remains full after flush; dropping newest record");
        pthread_mutex_unlock(&rec_mutex);
        return;
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
    count = buffered_count;
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

    pthread_mutex_lock(&rec_mutex);
    int session_offline_count = offline_count;
    pthread_mutex_unlock(&rec_mutex);
    if (session_offline_count > 0) {
        LOG_INFO("Session had %d offline-cached records", session_offline_count);
    }

    database_close();
    LOG_INFO("Data recorder closed");
}
