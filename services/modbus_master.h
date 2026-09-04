/**
 * @file    modbus_master.h
 * @brief   Modbus RTU 主站接口 — RS485 轮询工业传感器
 *
 * 原理:
 *   Modbus RTU 是工业自动化领域最常用的串行通信协议。
 *   主站 (Master) 通过 RS485 总线轮询从站 (Slave):
 *     主站发送: [从站地址][功能码][起始地址][寄存器数量][CRC校验]
 *     从站回复: [从站地址][功能码][字节数][数据...][CRC校验]
 *
 *   常用功能码:
 *     0x03 — 读保持寄存器 (Read Holding Registers)   → 读写参数
 *     0x04 — 读输入寄存器 (Read Input Registers)      → 只读传感器值
 *
 *   本模块支持两种模式:
 *     1. libmodbus (推荐): 完整协议栈, 自动 CRC/超时/重试
 *     2. 手动模式 (回退): 手动构造 Modbus RTU 帧, CRC16 查表法
 *
 * 轮询策略:
 *   多个从站按配置的 poll_interval_ms 循环轮询。
 *   单个从站超时 (默认500ms) 不影响其他从站。
 */

#ifndef SERVICES_MODBUS_MASTER_H
#define SERVICES_MODBUS_MASTER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 单个从站配置 */
typedef struct {
    int   slave_id;          /**< 从站地址 (1-247) */
    int   func_code;         /**< 功能码 (3=读保持寄存器, 4=读输入寄存器) */
    int   start_addr;        /**< 起始寄存器地址 */
    int   nb_regs;           /**< 寄存器数量 */
    int   poll_interval_ms;  /**< 轮询间隔 (毫秒), 默认 1000 */
    char  device_name[32];   /**< 设备名称 */
    char  data_format[32];   /**< 数据格式 (预留) */
} modbus_slave_config_t;

/** Modbus 数据回调 */
typedef void (*modbus_data_callback_t)(int slave_id, const char *device_name,
                                       uint16_t *regs, int nb_regs);

/** 初始化 Modbus RTU 主站 */
int modbus_master_init(const char *device, int baud, int gpio_pin);

/** 添加一个从站设备 */
int modbus_master_add_slave(const modbus_slave_config_t *config);

/** 设置数据回调 */
void modbus_master_set_callback(modbus_data_callback_t cb);

/** 启动后台轮询线程 */
int modbus_master_start(void);

/** 停止轮询 */
void modbus_master_stop(void);

/** 获取从站数量 */
int modbus_master_get_slave_count(void);

/** 运行时启用/暂停自动轮询，线程保持存活 */
void modbus_master_set_polling(bool enabled);

/** 获取当前自动轮询状态 */
bool modbus_master_is_polling(void);

#ifdef __cplusplus
}
#endif

#endif /* SERVICES_MODBUS_MASTER_H */
