/**
 * @file    can_manager.c
 * @brief   CAN 总线管理器实现
 */

#include "can_manager.h"
#include "data_bus.h"
#include "../hal/can_socket.h"
#include "../infra/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

/* ==================== 配置 ==================== */
#define MAX_SIGNALS  32   /**< 最大信号配置数 */

/* ==================== 内部状态 ==================== */
static can_signal_config_t signals[MAX_SIGNALS];
static int signal_count = 0;
static can_data_callback_t user_cb = NULL;
static can_raw_frame_callback_t raw_cb = NULL;
static pthread_t recv_thread;
static bool running = false;
static char if_name[16];

/* ==================== CAN 帧解析 ==================== */

/**
 * @brief 从 CAN 帧数据中提取指定位置和长度的信号值
 *
 * 支持 Intel (little-endian) 和 Motorola (big-endian) 两种字节序。
 * 简化实现: 假设 Intel 格式 (LSB first), 信号不跨字节边界。
 *
 * @param data       CAN 帧 8 字节数据
 * @param start_bit  起始位
 * @param length     位长度
 * @return 提取的原始值
 */
static bool extract_signal(const uint8_t *data, uint8_t dlc,
                           uint8_t start_bit, uint8_t length,
                           can_byte_order_t byte_order, uint64_t *out)
{
    if (!data || !out || length == 0 || length > 64) return false;

    uint64_t raw = 0;

    if (byte_order == CAN_BYTE_ORDER_INTEL) {
        for (uint8_t i = 0; i < length; i++) {
            int bit_pos = (int)start_bit + i;
            int byte_idx = bit_pos / 8;
            int bit_idx = bit_pos % 8;
            if (byte_idx >= dlc) return false;
            if (data[byte_idx] & (1u << bit_idx)) {
                raw |= (1ULL << i);
            }
        }
    } else {
        /* DBC Motorola saw-tooth: 同一字节 bit7→bit0，跨字节后跳到下一字节 bit7 */
        int bit_pos = start_bit;
        for (uint8_t i = 0; i < length; i++) {
            if (bit_pos < 0) return false;
            int byte_idx = bit_pos / 8;
            int bit_idx = bit_pos % 8;
            if (byte_idx >= dlc) return false;

            raw = (raw << 1) | ((data[byte_idx] >> bit_idx) & 0x1u);
            bit_pos = (bit_idx == 0) ? (bit_pos + 15) : (bit_pos - 1);
        }
    }

    *out = raw;
    return true;
}

/* ==================== 接收线程 ==================== */

static void *can_recv_thread_func(void *arg)
{
    (void)arg;
    can_frame_t frame;

    LOG_INFO("CAN: receiver thread started, %d signals", signal_count);

    while (running) {
        int rc = can_read_frame(&frame, 1000);  /* 1秒超时, 便于退出检查 */
        if (rc <= 0) continue;

        bool frame_extended = (frame.can_id & CAN_EFF_FLAG) != 0;
        uint32_t frame_id = frame.can_id &
            (frame_extended ? CAN_EFF_MASK : CAN_SFF_MASK);
        uint32_t callback_id = frame_id |
            (frame_extended ? CAN_EFF_FLAG : 0);

        if (raw_cb) {
            raw_cb(callback_id, frame.can_dlc, frame.data);
        }

        for (int i = 0; i < signal_count; i++) {
            can_signal_config_t *sig = &signals[i];

            bool sig_extended = (sig->can_id & CAN_EFF_FLAG) != 0;
            uint32_t sig_id = sig->can_id &
                (sig_extended ? CAN_EFF_MASK : CAN_SFF_MASK);
            if (frame_extended != sig_extended || frame_id != sig_id) continue;

            uint64_t raw;
            if (!extract_signal(frame.data, frame.can_dlc,
                                sig->start_bit, sig->length,
                                sig->byte_order, &raw)) {
                LOG_WARN("CAN: invalid signal layout for ID=0x%X %s",
                         frame_id, sig->signal_name);
                continue;
            }

            double value = (double)raw * sig->scale + sig->offset;

            LOG_DEBUG("CAN: ID=0x%X %s = %.2f %s",
                      frame_id, sig->signal_name, value, sig->unit);

            /* 回调通知 */
            if (user_cb) {
                user_cb(callback_id, sig->signal_name, value, sig->unit);
            }

            /* 发布到数据总线 */
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);

            data_point_t point;
            memset(&point, 0, sizeof(point));
            point.source = DATA_SOURCE_CAN;
            snprintf(point.device_name, sizeof(point.device_name),
                     "CAN_0x%X", frame_id);
            strncpy(point.point_name, sig->signal_name,
                    sizeof(point.point_name) - 1);
            point.value = value;
            strncpy(point.unit, sig->unit, sizeof(point.unit) - 1);
            point.timestamp = ts.tv_sec;
            point.valid = true;
            data_bus_publish(&point);
        }
    }

    LOG_INFO("CAN: receiver thread stopped");
    return NULL;
}

/* ==================== 公开 API ==================== */

int can_manager_init(const char *ifname, int bitrate)
{
    strncpy(if_name, ifname, sizeof(if_name) - 1);

    if (can_init(ifname, bitrate) != 0) {
        LOG_ERROR("CAN: init failed");
        return -1;
    }

    LOG_INFO("CAN manager: %s, %d bps", ifname, bitrate);
    return 0;
}

int can_manager_add_signal(const can_signal_config_t *config)
{
    if (signal_count >= MAX_SIGNALS) {
        LOG_ERROR("CAN: max signals (%d) reached", MAX_SIGNALS);
        return -1;
    }

    memcpy(&signals[signal_count], config, sizeof(can_signal_config_t));
    signal_count++;

    LOG_INFO("CAN: added signal ID=0x%X %s (start=%d, len=%d, order=%s, scale=%.3f, offset=%.1f) [%s]",
             config->can_id, config->signal_name,
             config->start_bit, config->length,
             config->byte_order == CAN_BYTE_ORDER_MOTOROLA ? "Motorola" : "Intel",
             config->scale, config->offset, config->unit);
    return 0;
}

void can_manager_set_callback(can_data_callback_t cb)
{
    user_cb = cb;
}

void can_manager_set_raw_callback(can_raw_frame_callback_t cb)
{
    raw_cb = cb;
}

int can_manager_start(void)
{
    if (signal_count == 0) {
        LOG_WARN("CAN: no signals configured, skip start");
        return 0;
    }

    running = true;
    if (pthread_create(&recv_thread, NULL, can_recv_thread_func, NULL) != 0) {
        LOG_ERROR("CAN: thread create failed");
        running = false;
        return -1;
    }

    return 0;
}

void can_manager_stop(void)
{
    running = false;
    if (recv_thread) {
        pthread_join(recv_thread, NULL);
    }
    can_close();
    LOG_INFO("CAN manager stopped");
}

int can_manager_get_signal_count(void)
{
    return signal_count;
}
