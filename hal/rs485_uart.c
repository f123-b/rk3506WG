/**
 * @file    rs485_uart.c
 * @brief   RS485 硬件驱动实现
 *
 * 使用标准 POSIX termios 配置串口参数, libgpiod 控制 GPIO 方向。
 * 如果系统中没有 libgpiod, 回退到 sysfs GPIO 接口 (/sys/class/gpio)。
 */

#include "rs485_uart.h"
#include "../infra/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <stdbool.h>
#include <sys/select.h>
#include <pthread.h>

/* ==================== 内部状态 ==================== */
static int uart_fd = -1;
static int gpio_pin_num = -1;
static pthread_mutex_t rs485_mutex = PTHREAD_MUTEX_INITIALIZER;

/* 尝试使用 sysfs GPIO (回退方案, /sys/class/gpio) */
static bool use_sysfs_gpio = false;
static char sysfs_gpio_path[64];

/* ==================== 公开 API ==================== */

int rs485_init(const char *device, int baud, int gpiochip, int gpio_pin)
{
    (void)gpiochip;

    /* 1. 打开 UART 设备 */
    uart_fd = open(device, O_RDWR | O_NOCTTY);
    if (uart_fd < 0) {
        LOG_ERROR("RS485: cannot open %s: %s", device, strerror(errno));
        return -1;
    }

    /* 2. 配置串口参数 */
    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(uart_fd, &tty) != 0) {
        LOG_ERROR("RS485: tcgetattr failed: %s", strerror(errno));
        close(uart_fd);
        return -1;
    }

    /* 波特率 */
    speed_t speed;
    switch (baud) {
        case 9600:   speed = B9600;   break;
        case 19200:  speed = B19200;  break;
        case 38400:  speed = B38400;  break;
        case 57600:  speed = B57600;  break;
        case 115200: speed = B115200; break;
        default:     speed = B9600;   break;
    }
    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);

    /* 8N1 (Modbus RTU 标准): 8数据位, 无校验, 1停止位 */
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~PARENB;        /* 无校验 */
    tty.c_cflag &= ~CSTOPB;        /* 1 停止位 */
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;            /* 8 数据位 */
    tty.c_cflag &= ~CRTSCTS;       /* 无硬件流控 */

    /* 原始模式: 无行缓冲, 不做字符处理 */
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);  /* 无软件流控 */
    tty.c_oflag &= ~OPOST;

    /* 超时: 1 分秒 (100ms) */
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1;

    if (tcsetattr(uart_fd, TCSANOW, &tty) != 0) {
        LOG_ERROR("RS485: tcsetattr failed: %s", strerror(errno));
        close(uart_fd);
        return -1;
    }

    /* 3. 初始化 GPIO 方向控制引脚 */
    gpio_pin_num = gpio_pin;

    /* 尝试 sysfs GPIO 导出 */
    snprintf(sysfs_gpio_path, sizeof(sysfs_gpio_path),
             "/sys/class/gpio/gpio%d/value", gpio_pin);

    /* 先尝试直接写 (可能已导出) */
    int test_fd = open(sysfs_gpio_path, O_WRONLY);
    if (test_fd < 0) {
        /* 未导出, 尝试导出 */
        int export_fd = open("/sys/class/gpio/export", O_WRONLY);
        if (export_fd >= 0) {
            char num_str[16];
            int len = snprintf(num_str, sizeof(num_str), "%d", gpio_pin);
            if (write(export_fd, num_str, len) > 0) {
                usleep(100000); /* 等内核创建 sysfs 节点 */

                /* 设置方向为输出 */
                char dir_path[64];
                snprintf(dir_path, sizeof(dir_path),
                         "/sys/class/gpio/gpio%d/direction", gpio_pin);
                int dir_fd = open(dir_path, O_WRONLY);
                if (dir_fd >= 0) {
                    write(dir_fd, "out", 3);
                    close(dir_fd);
                }
                use_sysfs_gpio = true;
            }
            close(export_fd);
        }
    } else {
        close(test_fd);
        use_sysfs_gpio = true;

        /* 确保 GPIO 方向为输出 (修复: 之前这里漏掉了) */
        char dir_path[64];
        snprintf(dir_path, sizeof(dir_path),
                 "/sys/class/gpio/gpio%d/direction", gpio_pin);
        int dir_fd = open(dir_path, O_WRONLY);
        if (dir_fd >= 0) {
            write(dir_fd, "out", 3);
            close(dir_fd);
            LOG_INFO("RS485: GPIO%d direction set to output", gpio_pin);
        }
    }

    /* 默认: 接收模式 (GPIO 拉低) */
    rs485_set_rx_mode();

    LOG_INFO("RS485: %s %d baud, GPIO%d dir control (sysfs=%s)",
             device, baud, gpio_pin, use_sysfs_gpio ? "yes" : "failed");
    return 0;
}

void rs485_set_tx_mode(void)
{
    if (!use_sysfs_gpio) return;

    int fd = open(sysfs_gpio_path, O_WRONLY);
    if (fd >= 0) {
        write(fd, "1", 1);
        close(fd);
    }
    /* 给 MAX485 一点时间切换模式 (通常 <1us, 保守 50us) */
    usleep(50);
}

void rs485_set_rx_mode(void)
{
    if (!use_sysfs_gpio) return;

    int fd = open(sysfs_gpio_path, O_WRONLY);
    if (fd >= 0) {
        write(fd, "0", 1);
        close(fd);
    }
}

int rs485_write(const uint8_t *data, size_t len)
{
    pthread_mutex_lock(&rs485_mutex);
    if (uart_fd < 0) {
        pthread_mutex_unlock(&rs485_mutex);
        return -1;
    }

    rs485_set_tx_mode();

    ssize_t n = write(uart_fd, data, len);
    if (n < 0) {
        LOG_ERROR("RS485 write: %s", strerror(errno));
        rs485_set_rx_mode();
        pthread_mutex_unlock(&rs485_mutex);
        return -1;
    }

    /* 等待 UART TX FIFO 清空 (最关键的一步!) */
    tcdrain(uart_fd);

    rs485_set_rx_mode();

    pthread_mutex_unlock(&rs485_mutex);
    return (int)n;
}

int rs485_read(uint8_t *buf, size_t len, int timeout_ms)
{
    pthread_mutex_lock(&rs485_mutex);
    if (uart_fd < 0) {
        pthread_mutex_unlock(&rs485_mutex);
        return -1;
    }

    if (timeout_ms > 0) {
        fd_set rfds;
        struct timeval tv;
        FD_ZERO(&rfds);
        FD_SET(uart_fd, &rfds);
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        int rc = select(uart_fd + 1, &rfds, NULL, NULL, &tv);
        if (rc <= 0) {
            pthread_mutex_unlock(&rs485_mutex);
            return 0; /* 超时或无数据 */
        }
    }

    ssize_t n = read(uart_fd, buf, len);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            pthread_mutex_unlock(&rs485_mutex);
            return 0;
        }
        LOG_ERROR("RS485 read: %s", strerror(errno));
        pthread_mutex_unlock(&rs485_mutex);
        return -1;
    }
    pthread_mutex_unlock(&rs485_mutex);
    return (int)n;
}

int rs485_get_fd(void)
{
    return uart_fd;
}

void rs485_flush(void)
{
    if (uart_fd >= 0) {
        tcflush(uart_fd, TCIOFLUSH);
    }
}

void rs485_close(void)
{
    pthread_mutex_lock(&rs485_mutex);
    rs485_set_rx_mode();

    if (uart_fd >= 0) {
        close(uart_fd);
        uart_fd = -1;
    }

    /* 取消 GPIO 导出 */
    if (use_sysfs_gpio && gpio_pin_num >= 0) {
        int unexport_fd = open("/sys/class/gpio/unexport", O_WRONLY);
        if (unexport_fd >= 0) {
            char num_str[16];
            int len = snprintf(num_str, sizeof(num_str), "%d", gpio_pin_num);
            write(unexport_fd, num_str, len);
            close(unexport_fd);
        }
    }

    pthread_mutex_unlock(&rs485_mutex);
    LOG_INFO("RS485 closed");
}
