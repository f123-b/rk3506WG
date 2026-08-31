/**
 * @file    ota_manager.c
 * @brief   应用程序 OTA 远程升级管理器
 *
 * 新增特性 (v3.1):
 *   - 断点续传 (HTTP Range)
 *   - 备份+自动回滚 (健康标志文件)
 *   - SHA256 进度回调
 *   - 应用签名验证回调框架
 *   - 线程安全 (并发锁 + 进度锁)
 *   - 鲁棒 JSON 解析
 *   - 独立更新 worker 错误处理
 */

#include "ota_manager.h"
#include "app_config.h"
#include "infra/sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/reboot.h>
#include <signal.h>

/* ==================== 配置常量 ==================== */
#define OTA_DEFAULT_SERVER       "http://192.168.5.10:9090"
#define OTA_DOWNLOAD_PATH        "/tmp/my_test_new"
#define OTA_MAX_APP_SIZE         (16 * 1024 * 1024)  /**< 最大应用包 16MB */
#define OTA_HTTP_TIMEOUT         30                   /**< HTTP 超时 (秒) */
#define OTA_DOWNLOAD_TIMEOUT     300                  /**< 下载超时 (秒) */
#define OTA_MAX_RETRIES          3                    /**< 最大重试次数 */
#define OTA_RECV_BUF_SIZE        8192                 /**< 接收缓冲区 */
#define OTA_PATCH_PATH           "/tmp/ota_patch"      /**< 差分补丁临时路径 */
#define OTA_PATCHED_PATH         "/tmp/my_test_new"    /**< 差分补丁输出 */
#define OTA_BSPATCH_BIN          "/oem/bspatch"        /**< bspatch 工具路径 */
#define OTA_HEALTH_FILE          "/tmp/ota_ok"          /**< 健康标志 */
#define OTA_ROLLBACK_GRACE_SEC   15                     /**< 回滚宽限期 (秒) */

/* App 模式默认配置 */
#ifndef OTA_APP_INSTALL_PATH
#define OTA_APP_INSTALL_PATH  "/oem/my_test"
#endif
/* ==================== 内部状态 ==================== */
static char  ota_server_url[256] = OTA_DEFAULT_SERVER;
static ota_version_info_t ota_remote_info;
static ota_progress_cb    ota_progress_callback = NULL;
static ota_signature_verify_cb ota_signature_callback = NULL;

static ota_status_t ota_state = OTA_IDLE;
static ota_error_t  ota_last_error  = OTA_ERR_NONE;
static char         ota_error_msg[256] = "";
static int          ota_download_progress = 0;
static bool         ota_cancelled = false;

/* App OTA 配置 */
static ota_type_t   ota_type = OTA_TYPE_APP;
static char         ota_app_install_path[128] = OTA_APP_INSTALL_PATH;

static pthread_mutex_t ota_mutex  = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t ota_lock   = PTHREAD_MUTEX_INITIALIZER;  /* 并发守护锁 */

/* ==================== 错误记录 (内部) ==================== */

static void set_error(ota_error_t err, const char *msg)
{
    ota_progress_cb progress_cb;
    char buf[128];

    pthread_mutex_lock(&ota_mutex);
    ota_last_error = err;
    if (msg) {
        strncpy(ota_error_msg, msg, sizeof(ota_error_msg) - 1);
        ota_error_msg[sizeof(ota_error_msg) - 1] = '\0';
    } else {
        ota_error_msg[0] = '\0';
    }
    progress_cb = ota_progress_callback;
    snprintf(buf, sizeof(buf), "ERR: %s", msg ? msg : "unknown");
    pthread_mutex_unlock(&ota_mutex);

    if (progress_cb) {
        progress_cb(-1, buf);
    }
}

static void ota_set_state(ota_status_t state)
{
    pthread_mutex_lock(&ota_mutex);
    ota_state = state;
    pthread_mutex_unlock(&ota_mutex);
}

static bool ota_is_cancelled(void)
{
    bool cancelled;
    pthread_mutex_lock(&ota_mutex);
    cancelled = ota_cancelled;
    pthread_mutex_unlock(&ota_mutex);
    return cancelled;
}

static void ota_set_progress(int progress)
{
    pthread_mutex_lock(&ota_mutex);
    ota_download_progress = progress;
    pthread_mutex_unlock(&ota_mutex);
}

/* ==================== 版本号比较 ==================== */

static int version_compare(const char *v1, const char *v2)
{
    int maj1 = 0, min1 = 0, pat1 = 0;
    int maj2 = 0, min2 = 0, pat2 = 0;
    sscanf(v1, "%d.%d.%d", &maj1, &min1, &pat1);
    sscanf(v2, "%d.%d.%d", &maj2, &min2, &pat2);
    if (maj1 != maj2) return maj1 - maj2;
    if (min1 != min2) return min1 - min2;
    return pat1 - pat2;
}

/* ==================== 鲁棒 JSON 字段提取 ==================== */
/**
 * @brief 从 JSON 字符串中提取字符串字段值
 *
 * 比简单的 strstr+strchr 更鲁棒:
 *   - 跳过 key 后的空白
 *   - 跳过 : 前后的空白
 *   - 正确处理转义字符
 *
 * @param json  JSON 字符串 (以 '\0' 结尾)
 * @param key   字段名 (如 "version")
 * @param out   输出缓冲区
 * @param out_size 输出缓冲区大小
 * @return true=找到, false=未找到
 */
static bool json_get_string(const char *json, const char *key,
                            char *out, size_t out_size)
{
    if (!json || !key || !out || out_size == 0) return false;

    /* 搜索 "\"key\"" */
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);

    const char *p = strstr(json, search);
    if (!p) return false;
    p += strlen(search);

    /* 跳过空白和冒号 */
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p == ':') p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;

    /* 跳过起始引号 */
    if (*p != '"') return false;
    p++;

    /* 复制直到结束引号 (处理转义) */
    size_t n = 0;
    while (*p && *p != '"' && n < out_size - 1) {
        if (*p == '\\' && *(p + 1)) {
            p++;
            switch (*p) {
                case '"':  out[n++] = '"';  break;
                case '\\': out[n++] = '\\'; break;
                case '/':  out[n++] = '/';  break;
                case 'n':  out[n++] = '\n'; break;
                case 'r':  out[n++] = '\r'; break;
                case 't':  out[n++] = '\t'; break;
                default:   out[n++] = *p;   break;
            }
        } else {
            out[n++] = *p;
        }
        p++;
    }
    out[n] = '\0';
    return true;
}

static int64_t json_get_int(const char *json, const char *key, int64_t default_val)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);

    const char *p = strstr(json, search);
    if (!p) return default_val;
    p += strlen(search);

    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p == ':') p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;

    return (int64_t)atoll(p);
}

static bool json_get_bool(const char *json, const char *key, bool default_val)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);

    const char *p = strstr(json, search);
    if (!p) return default_val;
    p += strlen(search);

    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p == ':') p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;

    if (strncmp(p, "true", 4) == 0) return true;
    return false;
}

/* ==================== HTTP 客户端 ==================== */

/**
 * @brief 解析 URL 中的 host, port, path
 */
static int parse_url(const char *url, char *host, int *port, char *path)
{
    char tmp[512];
    strncpy(tmp, url, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    char *p = tmp;
    if (strncmp(p, "http://", 7) == 0) p += 7;

    char *slash = strchr(p, '/');
    if (slash) {
        strncpy(path, slash, 255); path[255] = '\0';
        *slash = '\0';
    } else {
        strcpy(path, "/");
    }

    char *colon = strchr(p, ':');
    if (colon) {
        *colon = '\0';
        *port = atoi(colon + 1);
    } else {
        *port = 80;
    }
    strncpy(host, p, 127); host[127] = '\0';
    return 0;
}

/* 写入回调类型 */
typedef bool (*http_write_cb)(const uint8_t *data, size_t len, void *ctx);

/**
 * @brief HTTP GET 请求 (增强版: 支持 Range 断点续传)
 *
 * @param url         完整 URL
 * @param wcb         写入回调
 * @param ctx         透传用户数据
 * @param status      输出: HTTP 状态码 (可为 NULL)
 * @param timeout     超时秒数
 * @param resume_from 断点续传起始字节 (0=从头下载)
 * @return 0=成功, -1=失败
 */
static int http_get_ex(const char *url, http_write_cb wcb, void *ctx,
                       int *status, int timeout, int64_t resume_from)
{
    char host[128], path[256];
    int port;

    if (parse_url(url, host, &port, path) != 0) return -1;

    /* 解析地址 */
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        struct hostent *he = gethostbyname(host);
        if (!he) {
            fprintf(stderr, "OTA: DNS lookup failed for %s\n", host);
            return -1;
        }
        memcpy(&addr.sin_addr, he->h_addr, he->h_length);
    }

    /* 创建 socket */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    /* 非阻塞 connect + select 超时 */
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    int rc = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    if (rc < 0 && errno != EINPROGRESS) {
        close(sock); return -1;
    }

    if (rc < 0) {
        fd_set wfds;
        struct timeval tv;
        FD_ZERO(&wfds);
        FD_SET(sock, &wfds);
        tv.tv_sec = (timeout > 0 ? timeout : OTA_HTTP_TIMEOUT);
        tv.tv_usec = 0;

        rc = select(sock + 1, NULL, &wfds, NULL, &tv);
        if (rc <= 0) {
            fprintf(stderr, "OTA: connect timeout (%ds)\n", timeout);
            close(sock); return -1;
        }

        int sock_err = 0;
        socklen_t len = sizeof(sock_err);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, &sock_err, &len);
        if (sock_err != 0) {
            fprintf(stderr, "OTA: connect failed: %s\n", strerror(sock_err));
            close(sock); return -1;
        }
    }

    /* 恢复阻塞 */
    fcntl(sock, F_SETFL, flags);

    /* 设置 recv/send 超时 */
    struct timeval rtv = {.tv_sec = timeout, .tv_usec = 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof(rtv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &rtv, sizeof(rtv));

    /* 构造 HTTP GET 请求 */
    char req[1024];
    int req_len;

    if (resume_from > 0) {
        req_len = snprintf(req, sizeof(req),
            "GET %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: RK3506-OTA/3.1\r\n"
            "Accept: */*\r\n"
            "Range: bytes=%lld-\r\n"
            "Connection: close\r\n"
            "\r\n",
            path, host, (long long)resume_from);
    } else {
        req_len = snprintf(req, sizeof(req),
            "GET %s HTTP/1.0\r\n"
            "Host: %s\r\n"
            "User-Agent: RK3506-OTA/3.1\r\n"
            "Accept: */*\r\n"
            "Connection: close\r\n"
            "\r\n",
            path, host);
    }

    /* 发送请求 */
    ssize_t sent_total = 0;
    while (sent_total < req_len) {
        ssize_t ns = send(sock, req + sent_total, req_len - sent_total, 0);
        if (ns <= 0) { close(sock); return -1; }
        sent_total += ns;
    }

    /* 接收响应 */
    char buf[OTA_RECV_BUF_SIZE];
    ssize_t n = recv(sock, buf, sizeof(buf) - 1, 0);
    if (n <= 0) { close(sock); return -1; }
    buf[n] = '\0';

    /* 解析 HTTP 头部 */
    int http_status = 0;
    char *header_end = strstr(buf, "\r\n\r\n");
    if (!header_end) {
        header_end = strstr(buf, "\n\n");
        if (!header_end) { close(sock); return -1; }
    }

    /* 首行: HTTP/1.x 200 OK / HTTP/1.1 206 Partial Content */
    char *first_line_end = strstr(buf, "\r\n");
    if (!first_line_end) first_line_end = strchr(buf, '\n');
    if (first_line_end) {
        char saved = *first_line_end;
        *first_line_end = '\0';
        sscanf(buf, "HTTP/%*s %d", &http_status);
        *first_line_end = saved;
    }
    if (status) *status = http_status;

    /* 传递头部之后的数据 */
    size_t hdr_end_offset = (size_t)(header_end - buf) + 4;
    if (hdr_end_offset < (size_t)n) {
        size_t body_len = n - hdr_end_offset;
        if (!wcb((uint8_t *)(buf + hdr_end_offset), body_len, ctx)) {
            close(sock); return -1;
        }
    }

    /* 200 或 206 都表示成功 */
    if (http_status == 200 || http_status == 206) {
        while ((n = recv(sock, buf, sizeof(buf), 0)) > 0) {
            if (!wcb((uint8_t *)buf, n, ctx)) {
                close(sock); return -1;
            }
        }
    }

    close(sock);
    return (http_status == 200 || http_status == 206) ? 0 : -1;
}

/** 简化版: 不使用 Range */
static int http_get(const char *url, http_write_cb wcb, void *ctx,
                    int *status, int timeout)
{
    return http_get_ex(url, wcb, ctx, status, timeout, 0);
}

/* ==================== HTTP GET → 内存 ==================== */

typedef struct {
    char  *buf;
    size_t cap;
    size_t len;
} mem_buf_t;

static bool mem_write_cb(const uint8_t *data, size_t len, void *ctx)
{
    mem_buf_t *mb = (mem_buf_t *)ctx;
    if (mb->len + len >= mb->cap) return false;
    memcpy(mb->buf + mb->len, data, len);
    mb->len += len;
    mb->buf[mb->len] = '\0';
    return true;
}

/* ==================== HTTP GET → 文件 (带进度 + 断点续传) ==================== */

typedef struct {
    FILE    *fp;
    int64_t  total;      /**< 预期总大小 */
    int64_t  received;   /**< 已接收 (含断点续传已有) */
    int64_t  skipped;    /**< 断点续传跳过的字节数 */
} file_dl_ctx_t;

static bool file_write_cb(const uint8_t *data, size_t len, void *ctx)
{
    file_dl_ctx_t *fc = (file_dl_ctx_t *)ctx;

    if (ota_is_cancelled()) return false;

    fc->received += len;

    /* 线程安全地更新进度 */
    if (fc->total > 0) {
        int pct = (int)(fc->received * 100 / fc->total);
        if (pct > 100) pct = 100;
        if (pct != ota_get_progress()) {
            ota_set_progress(pct);

            if (ota_progress_callback) {
                char msg[64];
                snprintf(msg, sizeof(msg), "Downloading %d%% (%lld/%lld)",
                         pct, (long long)fc->received, (long long)fc->total);
                ota_progress_callback(pct, msg);
            }
        }
    }

    if (fwrite(data, 1, len, fc->fp) != len) return false;
    return true;
}

/**
 * @brief 获取文件大小，失败返回 -1
 */
static int64_t file_size(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    if (!S_ISREG(st.st_mode)) return -1;
    return st.st_size;
}

/* ==================== 健康标志 ==================== */

void ota_write_health_marker(void)
{
    FILE *fp = fopen(OTA_HEALTH_FILE, "w");
    if (fp) {
        fprintf(fp, "%ld\n", (long)time(NULL));
        fclose(fp);
        printf("OTA: health marker written to %s\n", OTA_HEALTH_FILE);
    }
}

/* ==================== 并发守护锁 ==================== */

bool ota_try_lock(void)
{
    return (pthread_mutex_trylock(&ota_lock) == 0);
}

void ota_unlock(void)
{
    pthread_mutex_unlock(&ota_lock);
}

/* ==================== 公开 API 实现 ==================== */

void ota_init(const char *ota_server)
{
    pthread_mutex_lock(&ota_mutex);
    if (ota_server && ota_server[0]) {
        strncpy(ota_server_url, ota_server, sizeof(ota_server_url) - 1);
        ota_server_url[sizeof(ota_server_url) - 1] = '\0';
    }
    ota_state = OTA_IDLE;
    ota_cancelled = false;
    pthread_mutex_unlock(&ota_mutex);
    printf("OTA: server = %s, local version = %s, type = %s\n",
           ota_server_url, APP_VERSION,
           "app");
}

void ota_set_type(ota_type_t type)
{
    (void)type;
    pthread_mutex_lock(&ota_mutex);
    ota_type = type;
    pthread_mutex_unlock(&ota_mutex);
    printf("OTA: update type set to app\n");
}

ota_type_t ota_get_type(void)
{
    ota_type_t type;
    pthread_mutex_lock(&ota_mutex);
    type = ota_type;
    pthread_mutex_unlock(&ota_mutex);
    return type;
}

void ota_set_app_install_path(const char *path)
{
    if (path && path[0]) {
        pthread_mutex_lock(&ota_mutex);
        strncpy(ota_app_install_path, path, sizeof(ota_app_install_path) - 1);
        ota_app_install_path[sizeof(ota_app_install_path) - 1] = '\0';
        pthread_mutex_unlock(&ota_mutex);
        printf("OTA: app install path = %s\n", ota_app_install_path);
    }
}

void ota_set_progress_callback(ota_progress_cb cb)
{
    ota_progress_callback = cb;
}

void ota_set_signature_verify_callback(ota_signature_verify_cb cb)
{
    ota_signature_callback = cb;
    printf("OTA: signature verify %s\n", cb ? "enabled" : "disabled");
}

/**
 * @brief 应用差分补丁: bspatch <旧文件> <新文件> <补丁文件>
 */
static int ota_apply_delta_patch(void)
{
    printf("OTA (delta): applying bspatch...\n");
    ota_set_state(OTA_PATCHING);

    if (ota_progress_callback)
        ota_progress_callback(90, "Applying delta patch...");

    pid_t child = fork();
    if (child == 0) {
        execl(OTA_BSPATCH_BIN, OTA_BSPATCH_BIN,
              ota_app_install_path, OTA_PATCHED_PATH, OTA_PATCH_PATH,
              (char *)NULL);
        _exit(127);
    }
    int rc = -1;
    if (child > 0) {
        int status = 0;
        if (waitpid(child, &status, 0) == child &&
            WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            rc = 0;
        }
    }
    if (rc != 0) {
        printf("OTA (delta): bspatch failed\n");
        set_error(OTA_ERR_PATCH, "bspatch apply failed");
        unlink(OTA_PATCH_PATH);
        return -1;
    }

    /* 校验补丁输出 */
    if (ota_progress_callback)
        ota_progress_callback(94, "Verifying patched file...");

    char actual_sha256[SHA256_HEX_SIZE];
    if (sha256_file(OTA_PATCHED_PATH, actual_sha256) != 0) {
        set_error(OTA_ERR_VERIFY, "Patch result hash failed");
        unlink(OTA_PATCH_PATH);
        unlink(OTA_PATCHED_PATH);
        return -1;
    }

    printf("OTA (delta): patched SHA256 = %s\n", actual_sha256);

    if (strcasecmp(actual_sha256, ota_remote_info.sha256) != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "Patch SHA256 mismatch! expected:%s got:%s",
                 ota_remote_info.sha256, actual_sha256);
        set_error(OTA_ERR_VERIFY, msg);
        unlink(OTA_PATCH_PATH);
        unlink(OTA_PATCHED_PATH);
        return -1;
    }

    unlink(OTA_PATCH_PATH);
    printf("OTA (delta): verified OK\n");
    return 0;
}

/**
 * @brief 应用 App 更新 (增强版: 备份 + 回滚)
 */
static int copy_file_sync(const char *src, const char *dst)
{
    int in_fd = open(src, O_RDONLY);
    if (in_fd < 0) return -1;

    int out_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (out_fd < 0) {
        close(in_fd);
        return -1;
    }

    char buf[8192];
    int rc = 0;
    for (;;) {
        ssize_t n = read(in_fd, buf, sizeof(buf));
        if (n == 0) break;
        if (n < 0) {
            if (errno == EINTR) continue;
            rc = -1;
            break;
        }
        ssize_t written = 0;
        while (written < n) {
            ssize_t w = write(out_fd, buf + written, (size_t)(n - written));
            if (w < 0 && errno == EINTR) continue;
            if (w <= 0) {
                rc = -1;
                break;
            }
            written += w;
        }
        if (rc != 0) break;
    }

    if (rc == 0 && fsync(out_fd) != 0) rc = -1;
    if (close(out_fd) != 0) rc = -1;
    close(in_fd);
    if (rc != 0) unlink(dst);
    return rc;
}

static void ota_start_binary(const char *path)
{
    execl(path, path, (char *)NULL);
    _exit(127);
}

static void sync_parent_directory(const char *path)
{
    char dir_path[256];
    snprintf(dir_path, sizeof(dir_path), "%s", path);
    char *slash = strrchr(dir_path, '/');
    if (!slash) {
        snprintf(dir_path, sizeof(dir_path), ".");
    } else if (slash == dir_path) {
        slash[1] = '\0';
    } else {
        *slash = '\0';
    }

    int dir_fd = open(dir_path, O_RDONLY);
    if (dir_fd >= 0) {
        fsync(dir_fd);
        close(dir_fd);
    }
}

static void ota_apply_worker(const char *src_path, const char *install_path)
{
    char backup_path[256];
    char temp_path[256];
    snprintf(backup_path, sizeof(backup_path), "%s.bak", install_path);
    snprintf(temp_path, sizeof(temp_path), "%s.new", install_path);

    unlink(OTA_HEALTH_FILE);
    bool had_backup = access(install_path, F_OK) == 0;
    if (had_backup) {
        unlink(backup_path);
        /* 保留旧 inode 的备份，同时让最终 rename(temp, install) 保持原子替换。 */
        if (link(install_path, backup_path) != 0 &&
            copy_file_sync(install_path, backup_path) != 0) {
            _exit(20);
        }
    }

    if (copy_file_sync(src_path, temp_path) != 0 ||
        rename(temp_path, install_path) != 0) {
        unlink(temp_path);
        if (had_backup) unlink(backup_path);
        _exit(21);
    }
    sync_parent_directory(install_path);
    chmod(install_path, 0755);
    unlink(src_path);
    sync();

    pid_t app_pid = fork();
    if (app_pid == 0) ota_start_binary(install_path);
    if (app_pid < 0) _exit(22);

    sleep(OTA_ROLLBACK_GRACE_SEC);
    if (access(OTA_HEALTH_FILE, F_OK) == 0) {
        unlink(OTA_HEALTH_FILE);
        if (had_backup) unlink(backup_path);
        _exit(0);
    }

    kill(app_pid, SIGTERM);
    waitpid(app_pid, NULL, 0);
    unlink(install_path);
    if (had_backup && rename(backup_path, install_path) == 0) {
        chmod(install_path, 0755);
        sync_parent_directory(install_path);
        sync();
        pid_t rollback_pid = fork();
        if (rollback_pid == 0) ota_start_binary(install_path);
    }
    _exit(23);
}

int ota_apply_app_update(void)
{
    char src_path[256];
    char install_path[256];
    snprintf(src_path, sizeof(src_path), "%s", OTA_DOWNLOAD_PATH);
    pthread_mutex_lock(&ota_mutex);
    snprintf(install_path, sizeof(install_path), "%s", ota_app_install_path);
    pthread_mutex_unlock(&ota_mutex);

    if (access(src_path, F_OK) != 0) {
        set_error(OTA_ERR_WRITE, "Update file not found");
        return -1;
    }

    if (ota_progress_callback) ota_progress_callback(98, "Applying atomic upgrade...");

    pid_t worker = fork();
    if (worker < 0) {
        set_error(OTA_ERR_WRITE, "Cannot create OTA worker");
        return -1;
    }
    if (worker == 0) ota_apply_worker(src_path, install_path);

    sync();
    _exit(0);
    return 0;
}

bool ota_check_update(ota_version_info_t *info)
{
    if (!info) return false;

    pthread_mutex_lock(&ota_mutex);
    ota_state = OTA_CHECKING;
    ota_last_error = OTA_ERR_NONE;
    ota_error_msg[0] = '\0';
    ota_cancelled = false;
    pthread_mutex_unlock(&ota_mutex);

    if (ota_progress_callback) ota_progress_callback(0, "Checking for updates...");

    /* 构造 URL */
    char url[512];
    snprintf(url, sizeof(url), "%s/version.json", ota_server_url);

    /* HTTP GET */
    char resp[8192];
    memset(resp, 0, sizeof(resp));
    mem_buf_t mb = { .buf = resp, .cap = sizeof(resp), .len = 0 };
    int http_status = 0;

    if (http_get(url, mem_write_cb, &mb, &http_status, OTA_HTTP_TIMEOUT) != 0 ||
        (http_status != 200 && http_status != 206)) {
        ota_set_state(OTA_FAILED);
        if (http_status == 404) {
            set_error(OTA_ERR_SERVER, "version.json not found (404)");
        } else if (http_status == 0) {
            set_error(OTA_ERR_NETWORK, "Cannot connect to OTA server");
        } else {
            char msg[64];
            snprintf(msg, sizeof(msg), "Server returned HTTP %d", http_status);
            set_error(OTA_ERR_SERVER, msg);
        }
        return false;
    }

    /* 鲁棒 JSON 解析 */
    memset(info, 0, sizeof(*info));

    json_get_string(resp, "version",     info->version, sizeof(info->version));
    json_get_string(resp, "type",        info->update_type, sizeof(info->update_type));
    json_get_string(resp, "build_date",  info->build_date, sizeof(info->build_date));
    json_get_string(resp, "filename",    info->filename, sizeof(info->filename));
    json_get_string(resp, "sha256",      info->sha256, sizeof(info->sha256));
    json_get_string(resp, "changelog",   info->changelog, sizeof(info->changelog));
    json_get_string(resp, "signature",   info->signature, sizeof(info->signature));
    info->size         = json_get_int(resp, "size", 0);
    info->force_update = json_get_bool(resp, "force_update", false);

    /* 差分字段 */
    json_get_string(resp, "delta_url",    info->delta_url, sizeof(info->delta_url));
    json_get_string(resp, "delta_sha256", info->delta_sha256, sizeof(info->delta_sha256));
    json_get_string(resp, "base_version", info->base_version, sizeof(info->base_version));
    info->delta_size = json_get_int(resp, "delta_size", 0);

    info->has_delta = (info->delta_url[0] != '\0' &&
                       info->delta_sha256[0] != '\0' &&
                       info->delta_size > 0 &&
                       info->base_version[0] != '\0');

    printf("OTA: remote version=%s, type=%s, local=%s, size=%lld, has_delta=%d\n",
           info->version, info->update_type, APP_VERSION,
           (long long)info->size, info->has_delta);

    if (info->signature[0]) {
        printf("OTA: application signature present (%zu chars)\n",
               strlen(info->signature));
    }

    /* 根据 version.json 中的 type 自动切换 OTA 模式 */
    if (strcmp(info->update_type, "app") == 0) {
        pthread_mutex_lock(&ota_mutex);
        ota_type = OTA_TYPE_APP;
        pthread_mutex_unlock(&ota_mutex);
        printf("OTA: auto-detected app update mode\n");
    } else if (info->update_type[0] != '\0') {
        set_error(OTA_ERR_SERVER, "Unsupported OTA update type");
        ota_set_state(OTA_FAILED);
        return false;
    }

    /* 保存远程信息 */
    pthread_mutex_lock(&ota_mutex);
    memcpy(&ota_remote_info, info, sizeof(ota_remote_info));
    pthread_mutex_unlock(&ota_mutex);

    /* 版本比较 */
    if (version_compare(info->version, APP_VERSION) <= 0 && !info->force_update) {
        set_error(OTA_ERR_NO_UPDATE, "Already latest version");
        ota_set_state(OTA_IDLE);
        if (ota_progress_callback) ota_progress_callback(100, "Already latest version");
        return false;
    }

    pthread_mutex_lock(&ota_mutex);
    ota_state = OTA_IDLE;
    ota_last_error = OTA_ERR_NONE;
    ota_error_msg[0] = '\0';
    pthread_mutex_unlock(&ota_mutex);

    if (ota_progress_callback) ota_progress_callback(100, "New version found!");
    return true;
}

/* SHA256 校验进度回调适配 */
typedef struct {
    ota_progress_cb user_cb;
    int             base_pct;   /**< 基础百分比 (如 95) */
} sha256_progress_ctx_t;

static void sha256_progress_adapter(void *user_ctx, int64_t bytes, int64_t total)
{
    sha256_progress_ctx_t *spc = (sha256_progress_ctx_t *)user_ctx;
    if (!spc || !spc->user_cb) return;

    if (total > 0) {
        int pct = (int)(bytes * 5 / total);  /* 5% 范围 (95→100) */
        spc->user_cb(spc->base_pct + pct, "Verifying SHA256...");
    }
}

bool ota_download_and_apply(void)
{
    char download_url[512];
    char expected_sha256[65];
    bool use_delta = false;

    /* 检查差分升级 */
    if (ota_remote_info.has_delta &&
        strcmp(ota_remote_info.base_version, APP_VERSION) == 0) {
        use_delta = true;
        printf("OTA: delta upgrade available! base=%s, delta_size=%lld\n",
               ota_remote_info.base_version, (long long)ota_remote_info.delta_size);
    } else if (ota_remote_info.has_delta) {
        printf("OTA: delta found but base mismatch (expected=%s, have=%s), "
               "falling back to full\n",
               ota_remote_info.base_version, APP_VERSION);
    }

    /* 构造 URL */
    if (use_delta) {
        snprintf(download_url, sizeof(download_url), "%s/%s",
                 ota_server_url, ota_remote_info.delta_url);
        strncpy(expected_sha256, ota_remote_info.delta_sha256, sizeof(expected_sha256));
        expected_sha256[sizeof(expected_sha256) - 1] = '\0';
    } else {
        snprintf(download_url, sizeof(download_url), "%s/%s",
                 ota_server_url, ota_remote_info.filename);
        strncpy(expected_sha256, ota_remote_info.sha256, sizeof(expected_sha256));
        expected_sha256[sizeof(expected_sha256) - 1] = '\0';
    }

    int64_t dl_size = use_delta ? ota_remote_info.delta_size : ota_remote_info.size;
    if (dl_size > OTA_MAX_APP_SIZE) {
        set_error(OTA_ERR_SIZE, "File size exceeds limit");
        ota_set_state(OTA_FAILED);
        return false;
    }

    const char *dl_path = use_delta ? OTA_PATCH_PATH : OTA_DOWNLOAD_PATH;

    printf("OTA: downloading %s (delta=%d, size=%lld)\n",
           download_url, use_delta, (long long)dl_size);

    /* ===== 阶段1: 下载 (支持断点续传) ===== */
    pthread_mutex_lock(&ota_mutex);
    ota_state = OTA_DOWNLOADING;
    ota_download_progress = 0;
    ota_cancelled = false;
    pthread_mutex_unlock(&ota_mutex);

    if (ota_progress_callback)
        ota_progress_callback(0, use_delta ? "Downloading delta..." : "Downloading application...");

    /* 检查是否已有部分下载 */
    int64_t existing = file_size(dl_path);
    if (existing > 0 && existing < dl_size) {
        printf("OTA: resuming download from byte %lld / %lld (%.1f%%)\n",
               (long long)existing, (long long)dl_size,
               (double)existing * 100.0 / dl_size);
        if (ota_progress_callback) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Resuming from %.0f%%...",
                     (double)existing * 100.0 / dl_size);
            ota_progress_callback((int)(existing * 100 / dl_size), msg);
        }
    } else if (existing >= dl_size) {
        /* 文件已完整, 跳过下载 */
        printf("OTA: file already complete (%lld bytes), skipping download\n",
               (long long)existing);
        if (ota_progress_callback)
            ota_progress_callback(90, "File already downloaded");
        goto verify_stage;
    } else if (existing > 0) {
        /* 文件异常 (比预期大), 删除重下 */
        printf("OTA: existing file too large (%lld > %lld), restarting\n",
               (long long)existing, (long long)dl_size);
        unlink(dl_path);
        existing = 0;
    }

    /* 重试循环 */
    int retry;
    bool download_ok = false;
    for (retry = 0; retry < OTA_MAX_RETRIES && !download_ok && !ota_is_cancelled(); retry++) {
        if (retry > 0 && ota_progress_callback) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Retry %d/%d...", retry + 1, OTA_MAX_RETRIES);
            ota_progress_callback(0, msg);
        }

        /* 重新获取当前文件大小 (可能因重试变化) */
        int64_t resume_pos = file_size(dl_path);
        if (resume_pos < 0) resume_pos = 0;

        FILE *fp = fopen(dl_path, (resume_pos > 0) ? "ab" : "wb");
        if (!fp) {
            set_error(OTA_ERR_WRITE, "Cannot create download file");
            ota_set_state(OTA_FAILED);
            return false;
        }

        file_dl_ctx_t fc = {
            .fp = fp, .total = dl_size,
            .received = resume_pos, .skipped = resume_pos
        };

        int http_status = 0;
        int rc = http_get_ex(download_url, file_write_cb, &fc,
                             &http_status, OTA_DOWNLOAD_TIMEOUT, resume_pos);
        fclose(fp);

        if (rc == 0 && (http_status == 200 || http_status == 206)) {
            /* 验证下载完整性 */
            int64_t actual_size = file_size(dl_path);
            if (actual_size == dl_size) {
                download_ok = true;
            } else {
                fprintf(stderr, "OTA: download incomplete: %lld/%lld bytes\n",
                        (long long)actual_size, (long long)dl_size);
                if (retry < OTA_MAX_RETRIES - 1) sleep(2);
            }
        } else if (retry < OTA_MAX_RETRIES - 1) {
            fprintf(stderr, "OTA: download failed (HTTP %d), retrying...\n",
                    http_status);
            sleep(2);
        }
    }

    if (!download_ok) {
        set_error(OTA_ERR_NETWORK, "Download failed (retried 3 times)");
        ota_set_state(OTA_FAILED);
        return false;
    }

verify_stage:
    /* ===== 阶段2: SHA256 校验 (带进度回调) ===== */
    ota_set_state(OTA_VERIFYING);

    if (ota_progress_callback) ota_progress_callback(95, "Verifying SHA256...");

    char actual_sha256[SHA256_HEX_SIZE];
    sha256_progress_ctx_t spc = { .user_cb = ota_progress_callback, .base_pct = 95 };

    if (sha256_file_ex(dl_path, actual_sha256,
                       sha256_progress_adapter, &spc) != 0) {
        set_error(OTA_ERR_VERIFY, "SHA256 calculation failed");
        ota_set_state(OTA_FAILED);
        return false;
    }

    printf("OTA: actual SHA256   = %s\n", actual_sha256);
    printf("OTA: expected SHA256 = %s\n", expected_sha256);

    if (strcasecmp(actual_sha256, expected_sha256) != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "SHA256 mismatch! expected:%s got:%s",
                 expected_sha256, actual_sha256);
        set_error(OTA_ERR_VERIFY, msg);
        ota_set_state(OTA_FAILED);
        unlink(dl_path);
        return false;
    }

    printf("OTA: SHA256 verified OK\n");

    /* ===== 阶段2.5: 签名验证 (可选, 通过回调) ===== */
    if (ota_signature_callback && ota_remote_info.signature[0]) {
        if (ota_progress_callback)
            ota_progress_callback(96, "Verifying signature...");

        printf("OTA: verifying application signature...\n");
        if (!ota_signature_callback(dl_path, ota_remote_info.signature)) {
            set_error(OTA_ERR_SIGNATURE, "Signature verification failed");
            ota_set_state(OTA_FAILED);
            unlink(dl_path);
            return false;
        }
        printf("OTA: signature verified OK\n");
    }

    /* ===== 阶段2.6: 差分补丁应用 ===== */
    if (use_delta) {
        if (ota_apply_delta_patch() != 0) {
            ota_set_state(OTA_FAILED);
            return false;
        }
        unlink(OTA_DOWNLOAD_PATH);
        if (rename(OTA_PATCHED_PATH, OTA_DOWNLOAD_PATH) != 0) {
            set_error(OTA_ERR_WRITE, "Rename patched file failed");
            ota_set_state(OTA_FAILED);
            return false;
        }
        printf("OTA (delta): patched file ready at %s\n", OTA_DOWNLOAD_PATH);
    }

    /* ===== 阶段3: 应用更新 ===== */
    ota_set_state(OTA_APPLYING);

    if (ota_apply_app_update() != 0) {
        ota_set_state(OTA_FAILED);
        return false;
    }
    /* ota_apply_app_update exits the old process after starting the worker. */
    return false;
}

/* ==================== 状态查询 (线程安全) ==================== */

ota_status_t ota_get_status(void)
{
    ota_status_t s;
    pthread_mutex_lock(&ota_mutex);
    s = ota_state;
    pthread_mutex_unlock(&ota_mutex);
    return s;
}

int ota_get_progress(void)
{
    int p;
    pthread_mutex_lock(&ota_mutex);
    p = ota_download_progress;
    pthread_mutex_unlock(&ota_mutex);
    return p;
}

ota_error_t ota_get_last_error(void)
{
    ota_error_t e;
    pthread_mutex_lock(&ota_mutex);
    e = ota_last_error;
    pthread_mutex_unlock(&ota_mutex);
    return e;
}

const char *ota_get_last_error_msg(void)
{
    static _Thread_local char message[sizeof(ota_error_msg)];
    pthread_mutex_lock(&ota_mutex);
    strncpy(message, ota_error_msg, sizeof(message) - 1);
    message[sizeof(message) - 1] = '\0';
    pthread_mutex_unlock(&ota_mutex);
    return message;
}

void ota_reboot(void)
{
    printf("OTA: rebooting system...\n");
    sync();
    sleep(1);
    if (reboot(RB_AUTOBOOT) != 0) {
        fprintf(stderr, "OTA: reboot failed: %s\n", strerror(errno));
    }
}

void ota_get_local_version(char *buf, int max_len)
{
    snprintf(buf, max_len, "%s", APP_VERSION);
}

void ota_cancel(void)
{
    pthread_mutex_lock(&ota_mutex);
    ota_cancelled = true;
    ota_state = OTA_IDLE;
    pthread_mutex_unlock(&ota_mutex);
}
