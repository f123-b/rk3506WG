/**
 * @file    can_socket_stub.c
 * @brief   CAN socket stub — host 模拟, 收发操作返回模拟结果
 */
#include "can_socket.h"
#include "../infra/logger.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static bool initialized = false;

int can_init(const char *ifname, int bitrate)
{
    (void)ifname; (void)bitrate;
    initialized = true;
    LOG_INFO("CAN stub: %s %d bps (simulated)", ifname, bitrate);
    return 0;
}

int can_set_filter(uint32_t can_id, uint32_t mask)
{
    (void)can_id; (void)mask;
    return 0;
}

int can_read_frame(can_frame_t *frame, int timeout_ms)
{
    (void)timeout_ms;
    if (!frame || !initialized) return -1;
    usleep(100000); /* 模拟等待 */
    /* 每隔几次返回一个模拟 RX 帧 */
    static int call = 0;
    call++;
    if (call % 3 == 0) {
        /* 交替不同 ID 的帧 */
        static int rpm = 2000;
        rpm += ((call * 7) % 200) - 100;
        if (rpm < 800) rpm = 800;
        if (rpm > 7500) rpm = 7500;

        frame->can_id = 0x100;
        frame->can_dlc = 8;
        /* 模拟发动机转速: 字节0-1 = RPM (小端) */
        frame->data[0] = rpm & 0xFF;
        frame->data[1] = (rpm >> 8) & 0xFF;
        frame->data[2] = 0x33; frame->data[3] = 0x44;
        frame->data[4] = 0x55; frame->data[5] = 0x66;
        frame->data[6] = 0x77; frame->data[7] = 0x88;
        printf("[CAN SIM] RX ID=0x100 DLC=8 RPM=%d\n", rpm);
        return 1;
    }
    return 0;
}

int can_write_frame(const can_frame_t *frame)
{
    if (!frame || !initialized) return -1;
    char data_str[64];
    int off = 0;
    for (int i = 0; i < frame->can_dlc && i < 8; i++)
        off += snprintf(data_str + off, sizeof(data_str) - off, "%02X ", frame->data[i]);
    printf("[CAN SIM] TX ID=0x%03X DLC=%d [%s]\n", frame->can_id, frame->can_dlc, data_str);
    return 0;
}

int  can_get_fd(void) { return -1; }
void can_close(void) { initialized = false; }
