/**
 * @file    rs485_uart_stub.c
 * @brief   RS485 UART stub — host 模拟, 无硬件操作, 所有调用返回成功
 */
#include "rs485_uart.h"
#include "../infra/logger.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int rs485_init(const char *device, int baud, int gpiochip, int gpio_pin)
{
    (void)device; (void)baud; (void)gpiochip; (void)gpio_pin;
    LOG_INFO("RS485 stub: %s %d baud (simulated)", device, baud);
    return 0;
}

void rs485_set_tx_mode(void) { /* stub */ }
void rs485_set_rx_mode(void) { /* stub */ }

int rs485_write(const uint8_t *data, size_t len)
{
    /* 打印前32字节便于调试 */
    char preview[64];
    size_t n = len < 30 ? len : 30;
    memcpy(preview, data, n);
    preview[n] = '\0';
    /* strip newlines for clean log */
    for (size_t i = 0; i < n; i++)
        if (preview[i] == '\r' || preview[i] == '\n') preview[i] = ' ';
    printf("[RS485 SIM] %zu bytes: %s\n", len, preview);
    return (int)len;
}

int rs485_read(uint8_t *buf, size_t len, int timeout_ms)
{
    (void)buf; (void)len; (void)timeout_ms;
    usleep(10000); /* 模拟等待 */
    return 0; /* 无数据 */
}

int  rs485_get_fd(void) { return -1; }
void rs485_flush(void) { /* stub */ }
void rs485_close(void) { /* stub */ }
