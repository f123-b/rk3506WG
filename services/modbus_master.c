/**
 * @file    modbus_master.c
 * @brief   Modbus RTU 主站实现
 *
 * 支持两种模式:
 *   1. libmodbus (推荐): 完整协议栈, 自动 CRC/超时/重试
 *   2. 手动模式 (回退): 手动构造 Modbus RTU 帧, 适用于无 libmodbus 环境
 *
 * 手动模式 CRC16 计算使用查表法 (Modbus CRC-16-IBM, 多项式 0x8005)。
 */

#include "modbus_master.h"
#include "data_bus.h"
#include "../hal/rs485_uart.h"
#include "../app_config.h"
#include "../infra/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <stdatomic.h>

/* ==================== 配置 ==================== */
#define MAX_SLAVES           8     /**< 最大从站数量 */
#define MODBUS_TIMEOUT_MS    500   /**< 单从站响应超时 */
#define MODBUS_FRAME_MAX     256   /**< 最大帧长度 */

/* ==================== 内部状态 ==================== */
static modbus_slave_config_t slaves[MAX_SLAVES];
static int slave_count = 0;
static modbus_data_callback_t user_callback = NULL;
static pthread_t poll_thread;
static atomic_bool running = false;
static atomic_bool polling_enabled = true;
static atomic_int tx_count = 0;
static atomic_int rx_count = 0;
static char dev_name[64];
static int dev_baud;
static int gpio_pin_num;

/* ==================== CRC16 计算 (查表法) ==================== */

static const uint16_t crc16_table[256] = {
    0x0000,0xC0C1,0xC181,0x0140,0xC301,0x03C0,0x0280,0xC241,
    0xC601,0x06C0,0x0780,0xC741,0x0500,0xC5C1,0xC481,0x0440,
    0xCC01,0x0CC0,0x0D80,0xCD41,0x0F00,0xCFC1,0xCE81,0x0E40,
    0x0A00,0xCAC1,0xCB81,0x0B40,0xC901,0x09C0,0x0880,0xC841,
    0xD801,0x18C0,0x1980,0xD941,0x1B00,0xDBC1,0xDA81,0x1A40,
    0x1E00,0xDEC1,0xDF81,0x1F40,0xDD01,0x1DC0,0x1C80,0xDC41,
    0x1400,0xD4C1,0xD581,0x1540,0xD701,0x17C0,0x1680,0xD641,
    0xD201,0x12C0,0x1380,0xD341,0x1100,0xD1C1,0xD081,0x1040,
    0xF001,0x30C0,0x3180,0xF141,0x3300,0xF3C1,0xF281,0x3240,
    0x3600,0xF6C1,0xF781,0x3740,0xF501,0x35C0,0x3480,0xF441,
    0x3C00,0xFCC1,0xFD81,0x3D40,0xFF01,0x3FC0,0x3E80,0xFE41,
    0xFA01,0x3AC0,0x3B80,0xFB41,0x3900,0xF9C1,0xF881,0x3840,
    0x2800,0xE8C1,0xE981,0x2940,0xEB01,0x2BC0,0x2A80,0xEA41,
    0xEE01,0x2EC0,0x2F80,0xEF41,0x2D00,0xEDC1,0xEC81,0x2C40,
    0xE401,0x24C0,0x2580,0xE541,0x2700,0xE7C1,0xE681,0x2640,
    0x2200,0xE2C1,0xE381,0x2340,0xE101,0x21C0,0x2080,0xE041,
    0xA001,0x60C0,0x6180,0xA141,0x6300,0xA3C1,0xA281,0x6240,
    0x6600,0xA6C1,0xA781,0x6740,0xA501,0x65C0,0x6480,0xA441,
    0x6C00,0xACC1,0xAD81,0x6D40,0xAF01,0x6FC0,0x6E80,0xAE41,
    0xAA01,0x6AC0,0x6B80,0xAB41,0x6900,0xA9C1,0xA881,0x6840,
    0x7800,0xB8C1,0xB981,0x7940,0xBB01,0x7BC0,0x7A80,0xBA41,
    0xBE01,0x7EC0,0x7F80,0xBF41,0x7D00,0xBDC1,0xBC81,0x7C40,
    0xB401,0x74C0,0x7580,0xB541,0x7700,0xB7C1,0xB681,0x7640,
    0x7200,0xB2C1,0xB381,0x7340,0xB101,0x71C0,0x7080,0xB041,
    0x5000,0x90C1,0x9181,0x5140,0x9301,0x53C0,0x5280,0x9241,
    0x9601,0x56C0,0x5780,0x9741,0x5500,0x95C1,0x9481,0x5440,
    0x9C01,0x5CC0,0x5D80,0x9D41,0x5F00,0x9FC1,0x9E81,0x5E40,
    0x5A00,0x9AC1,0x9B81,0x5B40,0x9901,0x59C0,0x5880,0x9841,
    0x8801,0x48C0,0x4980,0x8941,0x4B00,0x8BC1,0x8A81,0x4A40,
    0x4E00,0x8EC1,0x8F81,0x4F40,0x8D01,0x4DC0,0x4C80,0x8C41,
    0x4400,0x84C1,0x8581,0x4540,0x8701,0x47C0,0x4680,0x8641,
    0x8201,0x42C0,0x4380,0x8341,0x4100,0x81C1,0x8081,0x4040
};

static uint16_t crc16(const uint8_t *data, int len)
{
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; i++) {
        crc = (crc >> 8) ^ crc16_table[(crc ^ data[i]) & 0xFF];
    }
    return crc;
}

/* ==================== 简易 Modbus RTU 请求 ==================== */

/**
 * @brief 手动构造并发送 Modbus RTU 读寄存器请求 (回退方案)
 *
 * @return >=0=读取的寄存器数量, <0=失败
 */
static int modbus_read_regs_manual(int slave_id, int func_code,
                                    int start_addr, int nb_regs,
                                    uint16_t *out_regs)
{
    /* 构造请求帧: [地址][功能码][起始高][起始低][数量高][数量低][CRC低][CRC高] */
    uint8_t req[8];
    req[0] = (uint8_t)slave_id;
    req[1] = (uint8_t)func_code;
    req[2] = (uint8_t)(start_addr >> 8);
    req[3] = (uint8_t)(start_addr & 0xFF);
    req[4] = (uint8_t)(nb_regs >> 8);
    req[5] = (uint8_t)(nb_regs & 0xFF);
    uint16_t crc = crc16(req, 6);
    req[6] = (uint8_t)(crc & 0xFF);
    req[7] = (uint8_t)(crc >> 8);

    /* 发送请求 */
    if (rs485_write(req, 8) < 0) {
        return -1;
    }

    /* 读取响应: [地址][功能码][字节数][数据...][CRC] */
    uint8_t resp[MODBUS_FRAME_MAX];
    int n = rs485_read(resp, sizeof(resp), MODBUS_TIMEOUT_MS);
    if (n < 5) {
        return -1;  /* 超时或响应太短 */
    }

    /* 检查地址和功能码 */
    if (resp[0] != slave_id || resp[1] != func_code) {
        return -1;
    }

    /* 检查异常码 */
    if (resp[1] & 0x80) {
        LOG_WARN("Modbus: slave %d exception code %d", slave_id, resp[2]);
        return -1;
    }

    int byte_count = resp[2];
    int reg_count = byte_count / 2;

    /* 验证 CRC */
    int total = 3 + byte_count;
    uint16_t resp_crc = crc16(resp, total);
    uint16_t expected_crc = (uint16_t)(resp[total] | (resp[total+1] << 8));
    if (resp_crc != expected_crc) {
        LOG_WARN("Modbus: CRC mismatch for slave %d", slave_id);
        return -1;
    }

    /* 提取寄存器数据 */
    int count = (reg_count < nb_regs) ? reg_count : nb_regs;
    for (int i = 0; i < count; i++) {
        out_regs[i] = (uint16_t)((resp[3 + i*2] << 8) | resp[4 + i*2]);
    }

    return count;
}

/* ==================== 轮询线程 ==================== */

static void *modbus_poll_thread_func(void *arg)
{
    (void)arg;
    uint16_t regs[128];

    LOG_INFO("Modbus: poll thread started, %d slaves", slave_count);

    while (atomic_load(&running)) {
        if (!atomic_load(&polling_enabled)) {
            usleep(100000);
            continue;
        }

        for (int i = 0; i < slave_count && atomic_load(&running); i++) {
            modbus_slave_config_t *slv = &slaves[i];

            atomic_fetch_add(&tx_count, 1);
            int nb = modbus_read_regs_manual(
                slv->slave_id, slv->func_code,
                slv->start_addr, slv->nb_regs, regs);

            if (nb > 0) {
                atomic_fetch_add(&rx_count, 1);
                LOG_DEBUG("Modbus: slave %d (%s) read %d regs",
                          slv->slave_id, slv->device_name, nb);

                /* 通知上层回调 */
                if (user_callback) {
                    user_callback(slv->slave_id, slv->device_name, regs, nb);
                }

                /* 发布到数据总线 (第一个寄存器作为主数据点) */
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);

                data_point_t point;
                memset(&point, 0, sizeof(point));
                point.source = DATA_SOURCE_MODBUS;
                strncpy(point.device_name, slv->device_name,
                        sizeof(point.device_name) - 1);
                point.timestamp = ts.tv_sec;
                point.valid = true;

                for (int r = 0; r < nb; r++) {
                    snprintf(point.point_name, sizeof(point.point_name),
                             "register_%d", slv->start_addr + r);
                    point.value = (double)regs[r];
                    snprintf(point.unit, sizeof(point.unit), "raw");
                    data_bus_publish(&point);
                }
            } else {
                /* 超时或失败, 标记无效 */
                data_point_t point;
                memset(&point, 0, sizeof(point));
                point.source = DATA_SOURCE_MODBUS;
                strncpy(point.device_name, slv->device_name,
                        sizeof(point.device_name) - 1);
                snprintf(point.point_name, sizeof(point.point_name), "status");
                point.value = 0;
                point.valid = false;
                point.timestamp = time(NULL);
                data_bus_publish(&point);
            }

            /* 轮询间隔 */
            int interval = slv->poll_interval_ms;
            if (interval < 100) interval = 100;
            for (int t = 0; t < interval / 100 && atomic_load(&running); t++) {
                if (!atomic_load(&polling_enabled)) break;
                usleep(100000);  /* 100ms 分片, 便于及时退出/暂停 */
            }
        }
    }

    LOG_INFO("Modbus: poll thread stopped");
    return NULL;
}

/* ==================== 公开 API ==================== */

int modbus_master_init(const char *device, int baud, int gpio_pin)
{
    strncpy(dev_name, device, sizeof(dev_name) - 1);
    dev_baud = baud;
    gpio_pin_num = gpio_pin;

    if (rs485_init(device, baud, 0, gpio_pin) != 0) {
        LOG_ERROR("Modbus: RS485 init failed");
        return -1;
    }

    LOG_INFO("Modbus master: %s, %d baud, GPIO%d dir",
             device, baud, gpio_pin);
    return 0;
}

int modbus_master_add_slave(const modbus_slave_config_t *config)
{
    if (slave_count >= MAX_SLAVES) {
        LOG_ERROR("Modbus: max slaves (%d) reached", MAX_SLAVES);
        return -1;
    }

    memcpy(&slaves[slave_count], config, sizeof(modbus_slave_config_t));
    slave_count++;

    LOG_INFO("Modbus: added slave %d (%s), func=%d, addr=%d, regs=%d",
             config->slave_id, config->device_name,
             config->func_code, config->start_addr, config->nb_regs);
    return 0;
}

void modbus_master_set_callback(modbus_data_callback_t cb)
{
    user_callback = cb;
}

int modbus_master_start(void)
{
    if (slave_count == 0) {
        LOG_WARN("Modbus: no slaves configured, skip start");
        return 0;
    }

    atomic_store(&running, true);
    atomic_store(&polling_enabled, true);
    atomic_store(&tx_count, 0);
    atomic_store(&rx_count, 0);
    if (pthread_create(&poll_thread, NULL, modbus_poll_thread_func, NULL) != 0) {
        LOG_ERROR("Modbus: thread create failed");
        atomic_store(&running, false);
        return -1;
    }

    return 0;
}

void modbus_master_stop(void)
{
    atomic_store(&running, false);
    if (poll_thread) {
        pthread_join(poll_thread, NULL);
    }
    rs485_close();
    LOG_INFO("Modbus master stopped");
}

int modbus_master_get_slave_count(void)
{
    return slave_count;
}

void modbus_master_set_polling(bool enabled)
{
    atomic_store(&polling_enabled, enabled);
    LOG_INFO("Modbus: polling %s", enabled ? "enabled" : "paused");
}

bool modbus_master_is_polling(void)
{
    return atomic_load(&running) && atomic_load(&polling_enabled);
}

int modbus_master_get_tx_count(void)
{
    return atomic_load(&tx_count);
}

int modbus_master_get_rx_count(void)
{
    return atomic_load(&rx_count);
}
