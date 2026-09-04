/**
 * @file    can_socket.h
 * @brief   CAN 总线硬件驱动接口 — Linux SocketCAN
 *
 * 原理:
 *   SocketCAN 是 Linux 内核的原生 CAN 子系统, 将 CAN 控制器抽象为网络接口。
 *   应用程序通过标准 BSD socket API 操作 CAN 总线:
 *     - socket(PF_CAN, SOCK_RAW, CAN_RAW) → 创建 CAN 套接字
 *     - bind(sock, &addr, sizeof(addr))   → 绑定到 can0 接口
 *     - read(sock, &frame, sizeof(frame)) → 接收 CAN 帧
 *     - write(sock, &frame, sizeof(frame))→ 发送 CAN 帧
 *
 *   CAN 帧结构 (struct can_frame):
 *     can_id:   CAN ID (标准帧 11bit: 0x000-0x7FF, 扩展帧 29bit: 0x00000000-0x1FFFFFFF)
 *     can_dlc:  数据长度 (0-8 字节)
 *     data[8]:  8 字节数据
 *
 *   初始化前需要先用 ip 命令配置 CAN 接口:
 *     ip link set can0 type can bitrate 500000
 *     ip link set can0 up
 *
 * 硬件连接 (参考 J9 连接器):
 *   CAN0_RX (Pin20) → SN65HVD230 RXD
 *   CAN0_TX (Pin21) → SN65HVD230 TXD
 *
 * 如何修改:
 *   - 改接口名: 修改 can_init() 的 ifname 参数
 *   - 改波特率: 修改 can_init() 的 bitrate 参数
 */

#ifndef HAL_CAN_SOCKET_H
#define HAL_CAN_SOCKET_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** CAN ID 掩码 (与 linux/can.h 兼容) */
#define CAN_SFF_MASK  0x7FF        /**< 标准帧 ID 掩码 (11bit) */
#define CAN_EFF_MASK  0x1FFFFFFF   /**< 扩展帧 ID 掩码 (29bit) */
#define CAN_EFF_FLAG  0x80000000U  /**< 扩展帧标志位 */

/** CAN 帧结构 (与 linux/can.h 兼容) */
typedef struct {
    uint32_t can_id;    /**< CAN ID (bit31=1 表示扩展帧) */
    uint8_t  can_dlc;   /**< 数据长度 (0-8) */
    uint8_t  data[8];   /**< 数据字节 */
    uint8_t  padding[3]; /**< 对齐填充 */
} can_frame_t;

/**
 * @brief 初始化 CAN 接口 (SocketCAN)
 *
 * 注意: 调用前需先用 ip 命令配置 CAN 接口:
 *   system("ip link set can0 type can bitrate 500000");
 *   system("ip link set can0 up");
 *
 * @param ifname    接口名, 如 "can0"
 * @param bitrate   波特率 (bps), 如 500000
 * @return 0=成功, -1=失败
 */
int can_init(const char *ifname, int bitrate);

/**
 * @brief 设置 CAN 帧接收过滤器
 * @param can_id  要接收的 CAN ID
 * @param mask    掩码 (1=比较, 0=忽略)
 * @return 0=成功, -1=失败
 */
int can_set_filter(uint32_t can_id, uint32_t mask);

/**
 * @brief 读取一个 CAN 帧
 * @param frame      输出: CAN 帧
 * @param timeout_ms 超时 (毫秒), -1=阻塞, 0=非阻塞
 * @return >0=成功, 0=超时, <0=错误
 */
int can_read_frame(can_frame_t *frame, int timeout_ms);

/**
 * @brief 发送一个 CAN 帧
 * @param frame  要发送的 CAN 帧
 * @return 0=成功, -1=失败
 */
int can_write_frame(const can_frame_t *frame);

/**
 * @brief 获取 socket 文件描述符 (供 select/poll 使用)
 */
int can_get_fd(void);

/** 获取底层实际成功发送/接收的 CAN 帧计数 */
int can_get_tx_count(void);
int can_get_rx_count(void);

/**
 * @brief 关闭 CAN 接口
 */
void can_close(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_CAN_SOCKET_H */
