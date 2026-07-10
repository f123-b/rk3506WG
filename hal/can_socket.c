/**
 * @file    can_socket.c
 * @brief   SocketCAN 硬件驱动实现
 */

#include "can_socket.h"
#include "../infra/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>

/* ==================== 内部状态 ==================== */
static int can_sock = -1;

/* ==================== 公开 API ==================== */

int can_init(const char *ifname, int bitrate)
{
    /* 1. 使用 ip 命令配置 CAN 接口 (经典 CAN 2.0, 最大兼容性)
     *    rk3576_canfd 是 CAN FD 控制器, 但默认使用经典模式兼容 USB CAN 分析仪.
     *    如需 CAN FD, 将 fd off 改为 fd on 并设置 dbitrate.
     *
     *    先 down → 设置参数 → up (三步缺一不可)
     */
    char cmd[128];

    /* 先关闭接口 */
    snprintf(cmd, sizeof(cmd), "ip link set %s down 2>/dev/null", ifname);
    system(cmd);
    usleep(50000);

    /* 经典 CAN 模式 + loopback + bus-off 自动恢复
     *   fd off:      经典 CAN 2.0 (不要 FD, 兼容 USB CAN 分析仪)
     *   loopback on: 控制器自应答, 无需外部节点即可发送
     *   restart-ms:  总线关闭后自动恢复 (默认不自动恢复)
     */
    snprintf(cmd, sizeof(cmd),
             "ip link set %s type can bitrate %d fd off loopback on restart-ms 100 2>/dev/null",
             ifname, bitrate);
    int rc = system(cmd);
    if (rc != 0) {
        /* 回退: 不带 fd off / loopback / restart-ms */
        snprintf(cmd, sizeof(cmd),
                 "ip link set %s type can bitrate %d 2>/dev/null",
                 ifname, bitrate);
        system(cmd);
    }
    usleep(50000);

    /* 启动接口 */
    snprintf(cmd, sizeof(cmd), "ip link set %s up 2>&1", ifname);
    rc = system(cmd);
    if (rc != 0) {
        LOG_ERROR("CAN: failed to bring %s up (rc=%d)", ifname, rc);
    }
    usleep(100000); /* 等接口 up */

    /* 2. 创建 CAN RAW socket */
    can_sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (can_sock < 0) {
        LOG_ERROR("CAN: socket(PF_CAN) failed: %s", strerror(errno));
        return -1;
    }

    /* 3. 获取接口索引 */
    struct ifreq ifr;
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';

    if (ioctl(can_sock, SIOCGIFINDEX, &ifr) < 0) {
        LOG_ERROR("CAN: ioctl(SIOCGIFINDEX, %s) failed: %s",
                  ifname, strerror(errno));
        close(can_sock);
        can_sock = -1;
        return -1;
    }

    /* 4. 绑定到接口 */
    struct sockaddr_can addr;
    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(can_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("CAN: bind(%s) failed: %s", ifname, strerror(errno));
        close(can_sock);
        can_sock = -1;
        return -1;
    }

    /* 5. 启用接收自己的帧 (用于回环测试) */
    int recv_own = 1;
    setsockopt(can_sock, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS,
               &recv_own, sizeof(recv_own));

    LOG_INFO("CAN: %s initialized, bitrate=%d bps", ifname, bitrate);
    return 0;
}

int can_set_filter(uint32_t can_id, uint32_t mask)
{
    if (can_sock < 0) return -1;

    struct can_filter filter;
    filter.can_id = can_id;
    filter.can_mask = mask;

    if (setsockopt(can_sock, SOL_CAN_RAW, CAN_RAW_FILTER,
                   &filter, sizeof(filter)) < 0) {
        LOG_ERROR("CAN: set filter failed: %s", strerror(errno));
        return -1;
    }

    return 0;
}

int can_read_frame(can_frame_t *frame, int timeout_ms)
{
    if (can_sock < 0 || !frame) return -1;

    /* 超时处理 */
    if (timeout_ms > 0) {
        fd_set rfds;
        struct timeval tv;
        FD_ZERO(&rfds);
        FD_SET(can_sock, &rfds);
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        int rc = select(can_sock + 1, &rfds, NULL, NULL, &tv);
        if (rc <= 0) return 0;
    }

    /* 读取 CAN 帧 */
    struct can_frame raw_frame;
    ssize_t n = read(can_sock, &raw_frame, sizeof(raw_frame));
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        LOG_ERROR("CAN read: %s", strerror(errno));
        return -1;
    }

    if (n != sizeof(struct can_frame)) {
        LOG_WARN("CAN: short frame (%zd bytes)", n);
        return -1;
    }

    /* 转换到自定义帧结构 */
    frame->can_id = raw_frame.can_id;
    frame->can_dlc = raw_frame.can_dlc;
    memcpy(frame->data, raw_frame.data, 8);

    return 1;
}

int can_write_frame(const can_frame_t *frame)
{
    if (can_sock < 0 || !frame) return -1;

    struct can_frame raw_frame;
    raw_frame.can_id = frame->can_id;
    raw_frame.can_dlc = frame->can_dlc;
    memcpy(raw_frame.data, frame->data, 8);

    ssize_t n = write(can_sock, &raw_frame, sizeof(raw_frame));
    if (n != sizeof(struct can_frame)) {
        LOG_ERROR("CAN write: %s", strerror(errno));
        return -1;
    }

    return 0;
}

int can_get_fd(void)
{
    return can_sock;
}

void can_close(void)
{
    if (can_sock >= 0) {
        close(can_sock);
        can_sock = -1;
    }
    LOG_INFO("CAN closed");
}
