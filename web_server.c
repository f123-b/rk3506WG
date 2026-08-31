/**
 * @file    web_server.c
 * @brief   RK3506 嵌入式 HTTP 服务器 (精简版)
 *
 * 路由分发到各 api_* 模块:
 *   GET  /api/sensor/current           → web_server 内部处理
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

/* 内部常量 */
#define HTTP_MAX_REQUEST_SIZE   4096
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>

/* ==================== 共享数据 (MQTT 回调→HTTP 线程) ==================== */
static pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;
static float shared_temp = 0.0f;
static float shared_humi = 0.0f;
static bool  shared_valid = false;
static time_t shared_last_update = 0;

static int  server_fd = -1;
static bool server_running = false;
static pthread_t server_thread;
static bool server_thread_valid = false;
static pthread_mutex_t server_mutex = PTHREAD_MUTEX_INITIALIZER;
static int active_clients = 0;
static pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t client_cond = PTHREAD_COND_INITIALIZER;

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
        case 413: status_text = "Payload Too Large"; break;
        case 500: status_text = "Internal Server Error"; break;
        default:  status_text = "Unknown"; break;
    }

    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.0 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, status_text, content_type, body_len);
    if (header_len <= 0 || (size_t)header_len >= sizeof(header)) return;

    size_t sent = 0;
    while (sent < (size_t)header_len) {
        ssize_t n = send(client_fd, header + sent, (size_t)header_len - sent,
                         MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return;
        sent += (size_t)n;
    }
    if (body && body_len > 0) {
        sent = 0;
        while (sent < body_len) {
            ssize_t n = send(client_fd, body + sent, body_len - sent,
                             MSG_NOSIGNAL);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) return;
            sent += (size_t)n;
        }
    }
}

/* ==================== 传感器 API (保留在 web_server 内) ==================== */

/** GET /api/sensor/current */
static void handle_api_current(int client_fd)
{
    char json[512];
    char time_str[32];
    struct tm *tm_info;
    pthread_mutex_lock(&data_mutex);
    time_t now = shared_last_update > 0 ? shared_last_update : time(NULL);
    float temp = shared_temp;
    float humi = shared_humi;
    bool valid = shared_valid;
    pthread_mutex_unlock(&data_mutex);

    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    strftime(time_str, sizeof(time_str), "%H:%M:%S", &tm_buf);

    int len = snprintf(json, sizeof(json),
        "{\"temperature\":%.1f,\"humidity\":%.1f,\"valid\":%s,\"time\":\"%s\"}",
        temp, humi, valid ? "true" : "false",
        time_str);
    if (len < 0) len = 0;
    if ((size_t)len >= sizeof(json)) len = (int)sizeof(json) - 1;
    web_send_response(client_fd, 200, "application/json", json, (size_t)len);
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
    int path_len;
    if (strcmp(path, "/") == 0) {
        path_len = snprintf(filepath, sizeof(filepath), "%s/index.html", HTTP_WWW_DIR);
    } else {
        path_len = snprintf(filepath, sizeof(filepath), "%s%s", HTTP_WWW_DIR, path);
    }
    if (path_len < 0 || (size_t)path_len >= sizeof(filepath)) {
        web_send_response(client_fd, 400, "text/plain", "Path Too Long", 14);
        return;
    }

    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        web_send_response(client_fd, 404, "text/plain", "Not Found", 9);
        return;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        web_send_response(client_fd, 500, "text/plain", "File Error", 10);
        return;
    }
    long fsize = ftell(fp);
    if (fsize < 0) {
        fclose(fp);
        web_send_response(client_fd, 500, "text/plain", "File Error", 10);
        return;
    }
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
    if (header_len <= 0 || (size_t)header_len >= sizeof(header)) {
        fclose(fp);
        return;
    }
    size_t sent = 0;
    while (sent < (size_t)header_len) {
        ssize_t n = send(client_fd, header + sent,
                         (size_t)header_len - sent, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            fclose(fp);
            return;
        }
        sent += (size_t)n;
    }

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        sent = 0;
        while (sent < n) {
            ssize_t written = send(client_fd, buf + sent, n - sent,
                                   MSG_NOSIGNAL);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) {
                fclose(fp);
                return;
            }
            sent += (size_t)written;
        }
    }

    fclose(fp);
}

/* ==================== HTTP 请求处理 ==================== */

static int find_header_end(const char *buf, size_t len, size_t *separator_len)
{
    if (!buf || !separator_len) return -1;
    for (size_t i = 0; i + 3 < len; i++) {
        if (memcmp(buf + i, "\r\n\r\n", 4) == 0) {
            *separator_len = 4;
            return (int)i;
        }
    }
    for (size_t i = 0; i + 1 < len; i++) {
        if (memcmp(buf + i, "\n\n", 2) == 0) {
            *separator_len = 2;
            return (int)i;
        }
    }
    return -1;
}

static int parse_content_length(const char *headers, size_t header_len,
                                size_t *content_length)
{
    const char *line = headers;
    const char *end = headers + header_len;
    *content_length = 0;

    while (line < end) {
        const char *line_end = memchr(line, '\n', (size_t)(end - line));
        const char *value = NULL;
        size_t line_len = line_end ? (size_t)(line_end - line)
                                   : (size_t)(end - line);
        if (line_len >= 15 && strncasecmp(line, "Content-Length:", 15) == 0) {
            value = line + 15;
            while (value < end && (*value == ' ' || *value == '\t')) value++;
            char number[32];
            size_t number_len = line + line_len - value;
            while (number_len > 0 && (value[number_len - 1] == '\r' ||
                                       value[number_len - 1] == ' ' ||
                                       value[number_len - 1] == '\t')) {
                number_len--;
            }
            if (number_len == 0 || number_len >= sizeof(number)) return -1;
            memcpy(number, value, number_len);
            number[number_len] = '\0';
            char *parse_end = NULL;
            errno = 0;
            unsigned long long parsed = strtoull(number, &parse_end, 10);
            if (errno == ERANGE || !parse_end || *parse_end != '\0' ||
                parsed > SIZE_MAX) return -1;
            *content_length = (size_t)parsed;
            return 0;
        }
        if (!line_end) break;
        line = line_end + 1;
    }
    return 0;
}

static int read_http_request(int client_fd, char *request, size_t capacity,
                             size_t *request_len, size_t *body_offset,
                             size_t *body_len)
{
    size_t received = 0;
    size_t separator_len = 0;
    int header_offset = -1;

    while (header_offset < 0) {
        if (received + 1 >= capacity) return -2;
        ssize_t n = recv(client_fd, request + received,
                         capacity - received - 1, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        received += (size_t)n;
        request[received] = '\0';
        header_offset = find_header_end(request, received, &separator_len);
    }

    size_t content_length = 0;
    if (parse_content_length(request, (size_t)header_offset,
                             &content_length) != 0) return -2;
    size_t body_start = (size_t)header_offset + separator_len;
    if (body_start >= capacity) return -2;
    if (content_length > capacity - body_start - 1) return -2;

    while (received < body_start + content_length) {
        ssize_t n = recv(client_fd, request + received,
                         capacity - received - 1, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        received += (size_t)n;
        request[received] = '\0';
    }

    *request_len = received;
    *body_offset = body_start;
    *body_len = content_length;
    return 0;
}

static void close_client(int client_fd)
{
    shutdown(client_fd, SHUT_RDWR);
    close(client_fd);
    pthread_mutex_lock(&client_mutex);
    if (active_clients > 0) active_clients--;
    pthread_cond_broadcast(&client_cond);
    pthread_mutex_unlock(&client_mutex);
}

static void *handle_client(void *arg)
{
    int client_fd = (int)(intptr_t)arg;
    char request[HTTP_MAX_REQUEST_SIZE];

    struct timeval tv = {.tv_sec = 5, .tv_usec = 0};
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    size_t request_len = 0;
    size_t body_offset = 0;
    size_t body_len = 0;
    int read_rc = read_http_request(client_fd, request, sizeof(request),
                                    &request_len, &body_offset, &body_len);
    if (read_rc == -2) {
        web_send_response(client_fd, 413, "text/plain", "Payload Too Large", 17);
        close_client(client_fd);
        return NULL;
    }
    if (read_rc != 0) {
        close_client(client_fd);
        return NULL;
    }
    request[request_len] = '\0';

    /* 解析请求行 */
    char method[16], path[256];
    if (sscanf(request, "%15s %255s", method, path) != 2) {
        web_send_response(client_fd, 400, "text/plain", "Bad Request", 11);
        close_client(client_fd);
        return NULL;
    }

    /* 提取 POST body */
    char *body = body_len > 0 ? request + body_offset : NULL;

    /* 仅支持 GET 和 POST */
    if (strcmp(method, "GET") != 0 && strcmp(method, "POST") != 0) {
        web_send_response(client_fd, 405, "text/plain",
                          "Method Not Allowed", 18);
        close_client(client_fd);
        return NULL;
    }

    /* ===== 路由分发 ===== */

    if (strcmp(path, "/api/sensor/current") == 0) {
        handle_api_current(client_fd);
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

    close_client(client_fd);
    return NULL;
}

/* ==================== 服务器主循环 ==================== */

static void *server_loop(void *arg)
{
    int port = (int)(intptr_t)arg;
    struct sockaddr_in addr = {0};

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        LOG_ERROR("HTTP socket: %s", strerror(errno));
        pthread_mutex_lock(&server_mutex);
        server_running = false;
        pthread_mutex_unlock(&server_mutex);
        return NULL;
    }

    pthread_mutex_lock(&server_mutex);
    server_fd = listen_fd;
    bool stop_requested = !server_running;
    pthread_mutex_unlock(&server_mutex);
    if (stop_requested) {
        close(listen_fd);
        return NULL;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("HTTP bind: %s", strerror(errno));
        close(listen_fd);
        pthread_mutex_lock(&server_mutex);
        server_fd = -1;
        server_running = false;
        pthread_mutex_unlock(&server_mutex);
        return NULL;
    }

    if (listen(listen_fd, HTTP_MAX_CLIENTS) < 0) {
        LOG_ERROR("HTTP listen: %s", strerror(errno));
        close(listen_fd);
        pthread_mutex_lock(&server_mutex);
        server_fd = -1;
        server_running = false;
        pthread_mutex_unlock(&server_mutex);
        return NULL;
    }

    LOG_INFO("HTTP server on port %d", port);
    pthread_mutex_lock(&server_mutex);
    if (!server_running) {
        server_fd = -1;
        pthread_mutex_unlock(&server_mutex);
        close(listen_fd);
        return NULL;
    }
    server_running = true;
    pthread_mutex_unlock(&server_mutex);

    for (;;) {
        pthread_mutex_lock(&server_mutex);
        bool running = server_running;
        pthread_mutex_unlock(&server_mutex);
        if (!running) break;

        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr,
                               &client_len);

        if (client_fd < 0) {
            if (errno == EINTR) continue;
            break;
        }

        pthread_mutex_lock(&client_mutex);
        bool client_slot_available = active_clients < HTTP_MAX_CLIENTS;
        if (client_slot_available) active_clients++;
        pthread_mutex_unlock(&client_mutex);
        if (!client_slot_available) {
            close(client_fd);
            continue;
        }

        pthread_t thread;
        if (pthread_create(&thread, NULL, handle_client,
                           (void *)(intptr_t)client_fd) != 0) {
            pthread_mutex_lock(&client_mutex);
            active_clients--;
            pthread_cond_broadcast(&client_cond);
            pthread_mutex_unlock(&client_mutex);
            close(client_fd);
            continue;
        }
        pthread_detach(thread);
    }

    pthread_mutex_lock(&server_mutex);
    if (server_fd == listen_fd) {
        server_fd = -1;
        close(listen_fd);
    }
    server_running = false;
    pthread_mutex_unlock(&server_mutex);
    return NULL;
}

/* ==================== 公开 API ==================== */

int web_server_start(int port)
{
    pthread_mutex_lock(&server_mutex);
    if (server_thread_valid) {
        pthread_mutex_unlock(&server_mutex);
        return 0;
    }
    server_running = true;
    pthread_mutex_unlock(&server_mutex);

    if (pthread_create(&server_thread, NULL, server_loop,
                       (void *)(intptr_t)port) != 0) {
        pthread_mutex_lock(&server_mutex);
        server_running = false;
        pthread_mutex_unlock(&server_mutex);
        return -1;
    }

    pthread_mutex_lock(&server_mutex);
    server_thread_valid = true;
    pthread_mutex_unlock(&server_mutex);

    for (int i = 0; i < 50 && !web_server_is_running(); i++) {
        usleep(100000);
    }

    if (web_server_is_running()) return 0;

    pthread_mutex_lock(&server_mutex);
    bool join_thread = server_thread_valid;
    pthread_t thread = server_thread;
    pthread_mutex_unlock(&server_mutex);
    if (join_thread) {
        pthread_join(thread, NULL);
        pthread_mutex_lock(&server_mutex);
        server_thread_valid = false;
        pthread_mutex_unlock(&server_mutex);
    }
    return -1;
}

void web_server_stop(void)
{
    pthread_mutex_lock(&server_mutex);
    bool join_thread = server_thread_valid;
    pthread_t thread = server_thread;
    server_running = false;
    int fd = server_fd;
    server_fd = -1;
    pthread_mutex_unlock(&server_mutex);

    if (fd >= 0) {
        shutdown(fd, SHUT_RDWR);
        close(fd);
    }
    if (join_thread) {
        pthread_join(thread, NULL);
        pthread_mutex_lock(&server_mutex);
        server_thread_valid = false;
        pthread_mutex_unlock(&server_mutex);
    }

    /* 客户端线程是 detached 的；等待其释放共享服务资源后再返回。 */
    pthread_mutex_lock(&client_mutex);
    while (active_clients > 0) {
        pthread_cond_wait(&client_cond, &client_mutex);
    }
    pthread_mutex_unlock(&client_mutex);
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
    bool running;
    pthread_mutex_lock(&server_mutex);
    running = server_running;
    pthread_mutex_unlock(&server_mutex);
    return running;
}
