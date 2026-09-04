/**
 * @file    database.c
 * @brief   SQLite3 数据持久化实现
 *
 * 数据库 Schema:
 *   CREATE TABLE sensor_data (
 *     id          INTEGER PRIMARY KEY AUTOINCREMENT,
 *     timestamp   INTEGER NOT NULL,         -- Unix timestamp
 *     temperature REAL    NOT NULL,         -- 温度 ℃
 *     humidity    REAL    NOT NULL,         -- 湿度 %
 *     valid       INTEGER DEFAULT 1         -- 1=真实数据, 0=传感器异常
 *   );
 *   CREATE INDEX idx_sensor_time ON sensor_data(timestamp);
 */

#include "database.h"
#include "app_config.h"
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>

static sqlite3 *db = NULL;

/* ==================== 数据库初始化 ==================== */
int database_init(void)
{
    int rc = sqlite3_open(DB_PATH, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        db = NULL;
        return -1;
    }

    /* WAL 模式提高并发性能 */
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);

    /* 创建表 */
    const char *sql =
        "CREATE TABLE IF NOT EXISTS sensor_data ("
        "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  timestamp   INTEGER NOT NULL,"
        "  temperature REAL    NOT NULL,"
        "  humidity    REAL    NOT NULL,"
        "  valid       INTEGER DEFAULT 1"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_sensor_time ON sensor_data(timestamp);"
        "CREATE TABLE IF NOT EXISTS device_data ("
        "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  timestamp   INTEGER NOT NULL,"
        "  source      TEXT    NOT NULL,"
        "  device      TEXT    NOT NULL,"
        "  point_name  TEXT    NOT NULL,"
        "  value       REAL    NOT NULL,"
        "  unit        TEXT,"
        "  valid       INTEGER DEFAULT 1"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_device_data_time ON device_data(timestamp);"
        "CREATE INDEX IF NOT EXISTS idx_device_data_dev ON device_data(device);";

    char *err_msg = NULL;
    rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        return -1;
    }

    printf("Database initialized: %s\n", DB_PATH);
    return 0;
}

/* ==================== 插入记录 ==================== */
int database_insert(float temp, float humi, bool valid)
{
    if (!db) return -1;

    const char *sql =
        "INSERT INTO sensor_data (timestamp, temperature, humidity, valid) "
        "VALUES (?, ?, ?, ?);";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }

    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)time(NULL));
    sqlite3_bind_double(stmt, 2, (double)temp);
    sqlite3_bind_double(stmt, 3, (double)humi);
    sqlite3_bind_int(stmt, 4, valid ? 1 : 0);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE ? 0 : -1;
}

/* ==================== 查询历史 ==================== */
int database_query_history(int hours, sensor_record_t *records, int max_count)
{
    if (!db || !records) return 0;

    time_t since = time(NULL) - hours * 3600;

    /* 先取时间范围内最新的 max_count 条，再按时间升序返回，
     * 避免长时间范围查询只拿到最早的一段数据。 */
    const char *sql =
        "SELECT timestamp, temperature, humidity, valid FROM ("
        "  SELECT timestamp, temperature, humidity, valid "
        "  FROM sensor_data WHERE timestamp >= ? "
        "  ORDER BY timestamp DESC LIMIT ?"
        ") ORDER BY timestamp ASC;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return 0;
    }

    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)since);
    sqlite3_bind_int(stmt, 2, max_count);

    int count = 0;
    while (count < max_count && sqlite3_step(stmt) == SQLITE_ROW) {
        records[count].timestamp   = (time_t)sqlite3_column_int64(stmt, 0);
        records[count].temperature = (float)sqlite3_column_double(stmt, 1);
        records[count].humidity    = (float)sqlite3_column_double(stmt, 2);
        records[count].valid       = sqlite3_column_int(stmt, 3) != 0;
        count++;
    }

    sqlite3_finalize(stmt);
    return count;
}

/* ==================== 统计查询 ==================== */
int database_get_stats(int hours, sensor_stats_t *stats)
{
    if (!db || !stats) return -1;

    time_t since = time(NULL) - hours * 3600;

    const char *sql =
        "SELECT "
        "  AVG(temperature), MIN(temperature), MAX(temperature),"
        "  AVG(humidity),    MIN(humidity),    MAX(humidity),"
        "  COUNT(*)"
        "FROM sensor_data WHERE timestamp >= ?;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }

    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)since);

    int rc = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        stats->temp_avg = (float)sqlite3_column_double(stmt, 0);
        stats->temp_min = (float)sqlite3_column_double(stmt, 1);
        stats->temp_max = (float)sqlite3_column_double(stmt, 2);
        stats->humi_avg = (float)sqlite3_column_double(stmt, 3);
        stats->humi_min = (float)sqlite3_column_double(stmt, 4);
        stats->humi_max = (float)sqlite3_column_double(stmt, 5);
        stats->record_count = sqlite3_column_int(stmt, 6);
        rc = 0;
    }

    sqlite3_finalize(stmt);
    return rc;
}

/* ==================== 数据清理 ==================== */
int database_cleanup(int keep_days)
{
    if (!db) return -1;

    time_t cutoff = time(NULL) - keep_days * 86400;

    /* 清理 sensor_data 表 */
    const char *sql1 = "DELETE FROM sensor_data WHERE timestamp < ?;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql1, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, (sqlite3_int64)cutoff);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    /* 清理 device_data 表 */
    const char *sql2 = "DELETE FROM device_data WHERE timestamp < ?;";
    if (sqlite3_prepare_v2(db, sql2, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, (sqlite3_int64)cutoff);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    sqlite3_exec(db, "PRAGMA optimize;", NULL, NULL, NULL);
    return 0;
}

bool database_is_ready(void)
{
    return db != NULL;
}

/* ==================== 通用设备数据插入 ==================== */
int database_insert_device_data_at(time_t timestamp, const char *source,
                                    const char *device, const char *point_name,
                                    double value, const char *unit, bool valid)
{
    if (!db) return -1;

    const char *sql =
        "INSERT INTO device_data (timestamp, source, device, point_name, value, unit, valid) "
        "VALUES (?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }

    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)timestamp);
    sqlite3_bind_text(stmt, 2, source, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, device, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, point_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 5, value);
    sqlite3_bind_text(stmt, 6, unit ? unit : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, valid ? 1 : 0);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

int database_insert_device_data(const char *source, const char *device,
                                 const char *point_name, double value,
                                 const char *unit, bool valid)
{
    return database_insert_device_data_at(time(NULL), source, device,
                                          point_name, value, unit, valid);
}

/* ==================== 关闭数据库 ==================== */
void database_close(void)
{
    if (db) {
        sqlite3_close(db);
        db = NULL;
    }
}
