/**
 * @file    rs485_uart.h
 * @brief   RS485 硬件驱动接口 — UART + GPIO 方向控制
 *
 * 原理:
 *   RS485 是半双工差分总线, 同一时刻只能发送或接收。
 *   MAX485 收发器通过 DE (Driver Enable) 和 RE (Receiver Enable) 引脚切换模式:
 *     - DE=1, RE=1 → 发送模式 (驱动器使能, 接收器禁用)
 *     - DE=0, RE=0 → 接收模式 (驱动器禁用, 接收器使能)
 *
 *   本模块将 DE 和 RE 短接后用一个 GPIO 控制:
 *     - rs485_set_tx_mode() → GPIO 拉高 → 发送
 *     - rs485_set_rx_mode() → GPIO 拉低 → 接收 (默认状态)
 *
 *   关键时序 (最容易出错的地方):
 *     发送前: GPIO拉高 → write() → tcdrain() 等待硬件发送完成 → GPIO拉低
 *     如果不等 tcdrain 就拉低, 数据还在 TX FIFO 里没发出去, 导致通信失败。
 *
 * 硬件连接 (参考 J9 连接器):
 *   UART3_RX (Pin23) → MAX485 RO
 *   UART3_TX (Pin24) → MAX485 DI
 *   GPIO25           → MAX485 DE/RE (短接)
 *
 * 如何修改:
 *   - 改串口: 修改 rs485_init() 的 device 参数
 *   - 改波特率: 修改 rs485_init() 的 baud 参数
 *   - 改方向引脚: 修改 rs485_init() 的 gpio_pin 参数
 */

#ifndef HAL_RS485_UART_H
#define HAL_RS485_UART_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 RS485 硬件
 * @param device    串口设备路径, 如 "/dev/ttyS3"
 * @param baud      波特率, 如 9600, 19200, 115200
 * @param gpiochip  GPIO 芯片号 (通常为 0)
 * @param gpio_pin  GPIO 引脚号 (J9 Pin25 = GPIO25)
 * @return 0=成功, -1=失败
 */
int rs485_init(const char *device, int baud, int gpiochip, int gpio_pin);

/**
 * @brief 切换到发送模式 (GPIO 拉高, 使能驱动器)
 */
void rs485_set_tx_mode(void);

/**
 * @brief 切换到接收模式 (GPIO 拉低, 使能接收器)
 */
void rs485_set_rx_mode(void);

/**
 * @brief 写入数据并等待发送完成 (自动切换方向)
 * @param data  数据缓冲
 * @param len   数据长度
 * @return 实际写入字节数, 错误返回 -1
 *
 * 流程: 切换到TX模式 → write → tcdrain → 切换到RX模式
 */
int rs485_write(const uint8_t *data, size_t len);

/**
 * @brief 读取数据
 * @param buf        接收缓冲
 * @param len        期望读取长度
 * @param timeout_ms 超时 (毫秒), -1=阻塞, 0=非阻塞
 * @return 实际读取字节数, 超时返回 0, 错误返回 -1
 */
int rs485_read(uint8_t *buf, size_t len, int timeout_ms);

/**
 * @brief 获取 UART 文件描述符（供底层诊断或兼容调用）
 * @return fd, 失败返回 -1
 */
int rs485_get_fd(void);

/**
 * @brief 刷新 UART 缓冲区
 */
void rs485_flush(void);

/**
 * @brief 关闭 RS485 设备
 */
void rs485_close(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_RS485_UART_H */
