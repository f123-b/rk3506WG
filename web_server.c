/**
 * @file    web_server.c
 * @brief   RK3506 嵌入式 HTTP 服务器 (精简版)
 *
 * 路由分发到各 api_* 模块:
 *   GET  /api/sensor/current           → web_server 内部处理
 *   GET  /api/sensor/history?hours=N   → web_server 内部处理
 *   GET  /api/system/info              → web/api_system.c
 *   GET  /api/health                   → web/api_system.c (新增)
 *   GET  /api/ota/check                → web/api_ota_web.c
 *   GET  /api/ota/status               → web/api_ota_web.c
 *   POST /api/ota/start                → web/api_ota_web.c
 *   GET  /                             → 静态文件 (www/index.html)
 *
 * 使用 POSIX socket + HTTP/1.0，零外部依赖
 */

#include "web_server.h"
#include "web/api_system.h"
#include "web/api_ota_web.h"
#include "web/api_device.h"
#include "web/api_status.h"
#include "ntp_sync.h"
#include "app_config.h"
#include "infra/logger.h"
#include "database.h"

/* 内部常量 */
#define HTTP_MAX_REQUEST_SIZE   4096
#define HTTP_MAX_RESPONSE_SIZE  65536
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <stdatomic.h>

/* ==================== 共享数据 (MQTT 回调→HTTP 线程) ==================== */
static pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;
static float shared_temp = 0.0f;
static float shared_humi = 0.0f;
static bool  shared_valid = false;
static time_t shared_last_update = 0;

static int  server_fd = -1;
static atomic_bool atomic_store(&server_running, false);
static pthread_t server_thread;

/* ==================== 辅助函数 ==================== */

static const char *get_mime_type(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0) return "text/html; charset=utf-8";
    if (strcmp(ext, ".css") == 0)  return "text/css; charset=utf-8";
    if (strcmp(ext, ".js") == 0)   return "application/javascript; charset=utf-8";
    if (strcmp(ext, ".json") == 0) return "application/json; charset=utf-8";
    if (strcmp(ext, ".png") == 0)  return "image/png";
    if (strcmp(ext, ".svg") == 0)  return "image/svg+xml";
    if (strcmp(ext, ".ico") == 0)  return "image/x-icon";
    return "application/octet-stream";
}

/** 公开的响应发送函数 (供 api_* 模块调用) */
void web_send_response(int client_fd, int status, const char *content_type,
                       const char *body, size_t body_len)
{
    char header[512];
    const char *status_text;
    switch (status) {
        case 200: status_text = "OK"; break;
        case 400: status_text = "Bad Request"; break;
        case 404: status_text = "Not Found"; break;
        case 405: status_text = "Method Not Allowed"; break;
        case 500: status_text = "Internal Server Error"; break;
        default:  status_text = "Unknown"; break;
    }

    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.0 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, status_text, content_type, body_len);

    send(client_fd, header, header_len, MSG_NOSIGNAL);
    if (body && body_len > 0) {
        send(client_fd, body, body_len, MSG_NOSIGNAL);
    }
}

/* ==================== 传感器 API (保留在 web_server 内) ==================== */

/** GET /api/sensor/current */
static void handle_api_current(int client_fd)
{
    char json[512];
    char time_str[32];

    float temp;
    float humi;
    bool valid;
    time_t last_update;

    pthread_mutex_lock(&data_mutex);
    temp = shared_temp;
    humi = shared_humi;
    valid = shared_valid;
    last_update = shared_last_update;
    pthread_mutex_unlock(&data_mutex);

    time_t now = last_update > 0 ? last_update : time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    strftime(time_str, sizeof(time_str), "%H:%M:%S", &tm_buf);

    int len = snprintf(json, sizeof(json),
        "{\"temperature\":%.1f,\"humidity\":%.1f,\"valid\":%s,\"time\":\"%s\"}",
        temp, humi, valid ? "true" : "false", time_str);

    web_send_response(client_fd, 200, "application/json", json, len);
}

/** GET /api/sensor/history?hours=N */
static void handle_api_history(int client_fd, int hours)
{
    /* 64 KiB 响应缓冲下限制为 512 条，保证 JSON 不越界。
     * database_query_history() 返回时间范围内最新的 N 条并按时间升序排列。 */
    enum { MAX_HISTORY_RECORDS = 512 };
    sensor_record_t records[MAX_HISTORY_RECORDS];
    int count = database_query_history(hours, records, MAX_HISTORY_RECORDS);

    char json[HTTP_MAX_RESPONSE_SIZE];
    char *p = json;
    size_t remaining = sizeof(json);

    int w = snprintf(p, remaining, "[");
    if (w < 0 || (size_t)w >= remaining) return;
    p += w; remaining -= (size_t)w;

    for (int i = 0; i < count; i++) {
        char time_buf[32];
        struct tm tm_buf;
        localtime_r(&records[i].timestamp, &tm_buf);
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_buf);

        w = snprintf(p, remaining,
            "%s{\"timestamp\":%ld,\"time\":\"%s\",\"temperature\":%.1f,"
            "\"humidity\":%.1f,\"valid\":%s}",
            i > 0 ? "," : "",
            (long)records[i].timestamp, time_buf,
            records[i].temperature, records[i].humidity,
            records[i].valid ? "true" : "false");

        if (w < 0 || (size_t)w >= remaining) break;
        p += w;
        remaining -= (size_t)w;
    }

    if (remaining > 1) {
        *p++ = ']';
        *p = '\0';
    } else {
        json[sizeof(json) - 2] = ']';
        json[sizeof(json) - 1] = '\0';
        p = json + sizeof(json) - 1;
    }

    web_send_response(client_fd, 200, "application/json", json, (size_t)(p - json));
}

/* ==================== 静态文件服务 ==================== */

static void handle_static_file(int client_fd, const char *path)
{
    /* 安全: 防止目录遍历 */
    if (strstr(path, "..") || strstr(path, "//")) {
        web_send_response(client_fd, 400, "text/plain", "Bad Request", 11);
        return;
    }

    char filepath[256];
    if (strcmp(path, "/") == 0) {
        snprintf(filepath, sizeof(filepath), "%s/index.html", HTTP_WWW_DIR);
    } else {
        snprintf(filepath, sizeof(filepath), "%s%s", HTTP_WWW_DIR, path);
    }

    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        web_send_response(client_fd, 404, "text/plain", "Not Found", 9);
        return;
    }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    rewind(fp);

    const char *mime = get_mime_type(filepath);

    char header[512];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n"
        "\r\n",
        mime, fsize);
    send(client_fd, header, header_len, MSG_NOSIGNAL);

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        send(client_fd, buf, n, MSG_NOSIGNAL);
    }

    fclose(fp);
}

/* ==================== HTTP 请求处理 ==================== */

static void *handle_client(void *arg)
{
    int client_fd = (int)(intptr_t)arg;
    char request[HTTP_MAX_REQUEST_SIZE];

    struct timeval tv = {.tv_sec = 5, .tv_usec = 0};
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ssize_t n = recv(client_fd, request, sizeof(request) - 1, 0);
    if (n <= 0) {
        close(client_fd);
        return NULL;
    }
    request[n] = '\0';

    /* 解析请求行 */
    char method[16], path[256];
    if (sscanf(request, "%15s %255s", method, path) != 2) {
        web_send_response(client_fd, 400, "text/plain", "Bad Request", 11);
        close(client_fd);
        return NULL;
    }

    /* 提取 POST body */
    char *body = NULL;
    if (strcmp(method, "POST") == 0) {
        char *body_start = strstr(request, "\r\n\r\n");
        if (body_start) body = body_start + 4;
    }

    /* 仅支持 GET 和 POST */
    if (strcmp(method, "GET") != 0 && strcmp(method, "POST") != 0) {
        web_send_response(client_fd, 405, "text/plain",
                          "Method Not Allowed", 18);
        close(client_fd);
        return NULL;
    }

    /* ===== 路由分发 ===== */

    if (strcmp(path, "/api/sensor/current") == 0) {
        handle_api_current(client_fd);
    }
    else if (strncmp(path, "/api/sensor/history", 19) == 0) {
        int hours = 24;
        char *q = strchr(path, '?');
        if (q) {
            char *h = strstr(q, "hours=");
            if (h) hours = atoi(h + 6);
        }
        if (hours < 1) hours = 1;
        if (hours > 168) hours = 168;
        handle_api_history(client_fd, hours);
    }
    else if (strcmp(path, "/api/system/info") == 0) {
        api_system_handle_info(client_fd);
    }
    else if (strcmp(path, "/api/health") == 0) {
        api_system_handle_health(client_fd);
    }
    else if (strcmp(path, "/api/ota/check") == 0) {
        api_ota_handle_check(client_fd);
    }
    else if (strcmp(path, "/api/ota/status") == 0) {
        api_ota_handle_status(client_fd);
    }
    else if (strcmp(path, "/api/ota/start") == 0 &&
             strcmp(method, "POST") == 0) {
        api_ota_handle_start(client_fd, body);
    }
    else if (strcmp(path, "/api/device/list") == 0) {
        api_device_handle_list(client_fd);
    }
    else if (strcmp(path, "/api/device/modbus") == 0) {
        api_device_handle_modbus(client_fd);
    }
    else if (strcmp(path, "/api/device/can") == 0) {
        api_device_handle_can(client_fd);
    }
    else if (strcmp(path, "/api/status") == 0) {
        api_status_handle(client_fd);
    }
    else {
        handle_static_file(client_fd, path);
    }

    shutdown(client_fd, SHUT_RDWR);
    close(client_fd);
    return NULL;
}

/* ==================== 服务器主循环 ==================== */

static void *server_loop(void *arg)
{
    int port = (int)(intptr_t)arg;
    struct sockaddr_in addr = {0};

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        LOG_ERROR("HTTP socket: %s", strerror(errno));
        atomic_store(&server_running, false);
        return NULL;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("HTTP bind: %s", strerror(errno));
        close(server_fd);
        atomic_store(&server_running, false);
        return NULL;
    }

    if (listen(server_fd, HTTP_MAX_CLIENTS) < 0) {
        LOG_ERROR("HTTP listen: %s", strerror(errno));
        close(server_fd);
        atomic_store(&server_running, false);
        return NULL;
    }

    LOG_INFO("HTTP server on port %d", port);
    atomic_store(&server_running, true);

    while (server_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr,
                               &client_len);

        if (client_fd < 0) {
            if (errno == EINTR) continue;
            break;
        }

        pthread_t thread;
        pthread_create(&thread, NULL, handle_client,
                       (void *)(intptr_t)client_fd);
        pthread_detach(thread);
    }

    close(server_fd);
    server_fd = -1;
    return NULL;
}

/* ==================== 公开 API ==================== */

int web_server_start(int port)
{
    if (atomic_load(&server_running)) return 0;

    if (pthread_create(&server_thread, NULL, server_loop,
                       (void *)(intptr_t)port) != 0) {
        return -1;
    }

    for (int i = 0; i < 50 && !atomic_load(&server_running); i++) {
        usleep(100000);
    }

    return atomic_load(&server_running) ? 0 : -1;
}

void web_server_stop(void)
{
    atomic_store(&server_running, false);
    if (server_fd >= 0) {
        shutdown(server_fd, SHUT_RDWR);
        close(server_fd);
    }
    pthread_join(server_thread, NULL);
}

void web_server_update_data(float temp, float humi, bool valid)
{
    pthread_mutex_lock(&data_mutex);
    shared_temp = temp;
    shared_humi = humi;
    shared_valid = valid;
    shared_last_update = time(NULL);
    pthread_mutex_unlock(&data_mutex);
}

bool web_server_is_running(void)
{
    return atomic_load(&server_running);
}
