/**
 * @file    ntp_sync.c
 * @brief   轻量级 NTP 时间同步客户端实现
 *
 * 使用原始 UDP socket 发送 NTP 协议包，解析服务器返回的 UTC 时间戳，
 * 调用 settimeofday() 更新 Linux 系统时钟。
 *
 * NTP 协议简要说明 (RFC 5905):
 *   - NTP 时间戳 = 自 1900-01-01 00:00:00 UTC 起的秒数 (64位定点数)
 *   - Unix 时间戳 = 自 1970-01-01 00:00:00 UTC 起的秒数
 *   - 两者差值 = 2208988800 秒 (70年 + 17个闰日)
 *   - 我们取 NTP 包中的 Transmit Timestamp (字节 40-43) 的整数部分
 */

#include "ntp_sync.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <stdatomic.h>

/* ==================== 配置 ==================== */
#define NTP_SYNC_INTERVAL  3600          /**< 校准间隔 (秒), 1小时 */
#define NTP_PORT           123           /**< NTP 标准端口 */
#define NTP_PACKET_SIZE    48            /**< NTP 请求/响应包大小 */
#define NTP_TIMEOUT_SEC    5             /**< 单个服务器超时 (秒) */
#define NTP_UNIX_DELTA     2208988800ULL /**< NTP epoch(1900) 到 Unix epoch(1970) 的差值 */

/* 多个 NTP 服务器，按优先级排列 */
static const char *ntp_servers[] = {
    "ntp.aliyun.com",        /* 阿里云 NTP (国内最快) */
    "ntp1.aliyun.com",       /* 阿里云 NTP 备用 */
    "pool.ntp.org",          /* 全球 NTP 池 */
    "time.google.com",       /* Google NTP */
};
#define NTP_SERVER_COUNT (sizeof(ntp_servers) / sizeof(ntp_servers[0]))

/* ==================== 状态 ==================== */
static pthread_t sync_thread;
static atomic_bool thread_running = false;
static bool ntp_synced = false;
static time_t last_sync_time = 0;
static pthread_mutex_t status_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool sync_thread_valid = false;

/* ==================== NTP 协议 ==================== */

/**
 * @brief 构造 NTP 客户端请求包 (48字节)
 *
 * LI=0, VN=4 (NTP v4), Mode=3 (Client)
 * 其他字段全部填 0，服务器会原样返回或填充实际值
 */
static void ntp_build_request(unsigned char *packet)
{
    memset(packet, 0, NTP_PACKET_SIZE);
    /* 第1字节: LI(2bit)=00, VN(3bit)=100, Mode(3bit)=011 → 0x23 */
    packet[0] = 0x23;
}

/**
 * @brief 向单个 NTP 服务器请求时间
 * @param server   服务器域名或 IP
 * @param result   输出: Unix 时间戳 (秒)
 * @return 0=成功, -1=失败
 */
static int ntp_query_server(const char *server, time_t *result)
{
    int sock = -1;
    unsigned char packet[NTP_PACKET_SIZE];
    struct sockaddr_in addr;
    struct timeval tv;

    /* 解析域名 */
    struct hostent *host = gethostbyname(server);
    if (!host) {
        fprintf(stderr, "NTP: DNS lookup failed for %s\n", server);
        return -1;
    }

    /* 创建 UDP socket */
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("NTP socket");
        return -1;
    }

    /* 设置接收超时 */
    tv.tv_sec = NTP_TIMEOUT_SEC;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* 构造目标地址 */
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(NTP_PORT);
    memcpy(&addr.sin_addr, host->h_addr, host->h_length);

    /* 发送 NTP 请求 */
    ntp_build_request(packet);
    if (sendto(sock, packet, NTP_PACKET_SIZE, 0,
               (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("NTP sendto");
        close(sock);
        return -1;
    }

    /* 接收 NTP 响应 */
    socklen_t addr_len = sizeof(addr);
    ssize_t n = recvfrom(sock, packet, NTP_PACKET_SIZE, 0,
                         (struct sockaddr *)&addr, &addr_len);
    close(sock);

    if (n != NTP_PACKET_SIZE) {
        fprintf(stderr, "NTP: short response from %s (%zd bytes)\n", server, n);
        return -1;
    }

    /* 解析 Transmit Timestamp (字节 40-47, 64位定点数)
     * 整数部分 = (uint32_t)(packet[40]<<24 | packet[41]<<16 | packet[42]<<8 | packet[43])
     * 小数部分 = (uint32_t)(packet[44]<<24 | ...) — 我们不需要
     */
    uint32_t ntp_sec = ((uint32_t)packet[40] << 24) |
                       ((uint32_t)packet[41] << 16) |
                       ((uint32_t)packet[42] << 8)  |
                       ((uint32_t)packet[43]);

    /* NTP 时间戳 → Unix 时间戳 (减去 70 年偏移) */
    if (ntp_sec < NTP_UNIX_DELTA) {
        fprintf(stderr, "NTP: invalid timestamp from %s\n", server);
        return -1;
    }
    *result = (time_t)(ntp_sec - NTP_UNIX_DELTA);
    return 0;
}

/**
 * @brief 尝试从多个 NTP 服务器获取时间
 * @param result 输出: Unix UTC 时间戳
 * @return 0=成功, -1=全部失败
 */
static int ntp_query(time_t *result)
{
    for (size_t i = 0; i < NTP_SERVER_COUNT; i++) {
        if (ntp_query_server(ntp_servers[i], result) == 0) {
            printf("NTP: synced from %s\n", ntp_servers[i]);
            return 0;
        }
    }
    fprintf(stderr, "NTP: all %zu servers failed\n", NTP_SERVER_COUNT);
    return -1;
}

/* ==================== 系统时钟设置 ==================== */

/**
 * @brief 将 Unix UTC 时间戳写入 Linux 系统时钟
 *
 * 调用 settimeofday() 需要 root 权限。
 * 同时设置 TZ 环境变量使 localtime_r() 返回北京时间 (UTC+8)。
 */
static int ntp_set_system_time(time_t utc_time)
{
    struct timeval tv;
    tv.tv_sec = utc_time;
    tv.tv_usec = 0;

    if (settimeofday(&tv, NULL) < 0) {
        perror("NTP settimeofday");
        return -1;
    }

    /* 设置时区为北京时间 (CST = China Standard Time, UTC+8)
     * TZ=CST-8 的含义: CST 时区，比 UTC 早 8 小时
     * POSIX TZ 格式: std offset [dst [offset] [,start[/time],end[/time]]]
     * "CST-8" = 标准时间 CST，UTC+8 (= local = UTC + 8h = UTC + 28800s)
     */
    setenv("TZ", "CST-8", 1);
    tzset();

    return 0;
}

/* ==================== 后台同步线程 ==================== */

static void *ntp_sync_thread_func(void *arg)
{
    (void)arg;

    /* 启动后立即同步一次 */
    time_t utc_now;
    if (ntp_query(&utc_now) == 0) {
        ntp_set_system_time(utc_now);
        pthread_mutex_lock(&status_mutex);
        ntp_synced = true;
        last_sync_time = utc_now;
        pthread_mutex_unlock(&status_mutex);
        printf("NTP: initial sync OK, system time set to UTC %ld\n", (long)utc_now);
    } else {
        fprintf(stderr, "NTP: initial sync failed, will retry in %d seconds\n",
                NTP_SYNC_INTERVAL);
    }

    /* 周期性校准循环 */
    while (atomic_load(&thread_running)) {
        /* 睡眠 NTP_SYNC_INTERVAL 秒，但每 5 秒检查一次退出标志 */
        for (int i = 0; i < NTP_SYNC_INTERVAL / 5 && atomic_load(&thread_running); i++) {
            sleep(5);
        }
        if (!atomic_load(&thread_running)) break;

        if (ntp_query(&utc_now) == 0) {
            ntp_set_system_time(utc_now);
            pthread_mutex_lock(&status_mutex);
            ntp_synced = true;
            last_sync_time = utc_now;
            pthread_mutex_unlock(&status_mutex);
            printf("NTP: periodic sync OK\n");
        }
    }

    return NULL;
}

/* ==================== 公开 API ==================== */

int ntp_sync_init(void)
{
    if (atomic_load(&thread_running)) return 0;

    atomic_store(&thread_running, true);
    if (pthread_create(&sync_thread, NULL, ntp_sync_thread_func, NULL) != 0) {
        atomic_store(&thread_running, false);
        perror("NTP pthread_create");
        return -1;
    }
    sync_thread_valid = true;
    printf("NTP: sync thread started\n");
    return 0;
}

void ntp_sync_stop(void)
{
    atomic_store(&thread_running, false);
    if (sync_thread_valid) {
        pthread_join(sync_thread, NULL);
        sync_thread_valid = false;
    }
    printf("NTP: sync thread stopped\n");
}

void ntp_sync_get_status(bool *synced, time_t *last_sync)
{
    pthread_mutex_lock(&status_mutex);
    if (synced) *synced = ntp_synced;
    if (last_sync) *last_sync = last_sync_time;
    pthread_mutex_unlock(&status_mutex);
}

int ntp_sync_once(void)
{
    time_t utc_now;
    if (ntp_query(&utc_now) != 0) return -1;
    if (ntp_set_system_time(utc_now) != 0) return -1;

    pthread_mutex_lock(&status_mutex);
    ntp_synced = true;
    last_sync_time = utc_now;
    pthread_mutex_unlock(&status_mutex);
    return 0;
}
