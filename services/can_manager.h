/**
 * @file    can_manager.h
 * @brief   CAN 总线管理器 — 接收并解析 CAN 帧
 *
 * 原理:
 *   CAN (Controller Area Network) 是汽车和工业控制领域最常用的现场总线。
 *   每个 CAN 帧包含:
 *     - CAN ID (11bit 标准帧 / 29bit 扩展帧): 标识信号类型和优先级
 *     - DLC (0-8): 数据字节数
 *     - Data[8]: 有效数据
 *
 *   CAN 信号映射:
 *   一个 CAN 帧的 8 字节数据中可以编码多个物理信号。
 *   例如 CAN ID 0x123:
 *     Byte 0-1: 发动机转速 (uint16, 0.25 rpm/bit)
 *     Byte 2:   冷却液温度 (uint8, 1℃/bit, offset=-40)
 *
 *   本模块通过信号配置表 (can_signal_config_t) 将原始字节解析为物理值:
 *     physical_value = raw_value * scale + offset
 *
 *   CAN DBC 文件:
 *   汽车行业使用 DBC 文件描述 CAN 信号映射。本模块的手动配置等效于 DBC 的简化版。
 *   未来可扩展支持 DBC 文件解析 (CANopen/J1939)。
 *
 * 如何修改:
 *   - 添加信号: 调用 can_manager_add_signal()
 *   - 改滤波器: 调用 can_manager_set_filter()
 */

#ifndef SERVICES_CAN_MANAGER_H
#define SERVICES_CAN_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 配置结构 ==================== */

/** CAN 信号字节序 */
typedef enum {
    CAN_BYTE_ORDER_INTEL = 0,     /**< Intel / little-endian, start_bit 为 LSB 索引 */
    CAN_BYTE_ORDER_MOTOROLA = 1,  /**< Motorola / DBC big-endian, start_bit 为 MSB 索引 */
} can_byte_order_t;

/** CAN 信号映射配置 */
typedef struct {
    uint32_t can_id;        /**< CAN ID (标准帧 11bit, 扩展帧用 bit31=1 标记) */
    char     signal_name[32]; /**< 信号名称 (如 "发动机转速") */
    uint8_t  start_bit;     /**< Intel: LSB起始位; Motorola: DBC MSB起始位 */
    uint8_t  length;        /**< 数据长度 (bit, 1-32) */
    can_byte_order_t byte_order; /**< Intel/Motorola，零初始化默认 Intel */
    float    scale;         /**< 缩放系数 (物理值 = raw * scale + offset) */
    float    offset;        /**< 偏移量 */
    char     unit[16];      /**< 单位 (如 "rpm", "℃", "kPa") */
} can_signal_config_t;

/** CAN 数据回调 (解析后的物理值) */
typedef void (*can_data_callback_t)(uint32_t can_id, const char *signal_name,
                                     double value, const char *unit);

/** CAN 原始帧回调 (未解析的帧数据) */
typedef void (*can_raw_frame_callback_t)(uint32_t can_id, uint8_t dlc,
                                          const uint8_t *data);

/* ==================== API ==================== */

/**
 * @brief 初始化 CAN 管理器
 * @param ifname   接口名, 如 "can0"
 * @param bitrate  波特率 (bps), 如 500000
 * @return 0=成功, -1=失败
 */
int can_manager_init(const char *ifname, int bitrate);

/**
 * @brief 添加一个信号映射
 * @param config  信号配置
 * @return 0=成功, -1=失败 (信号数已满)
 */
int can_manager_add_signal(const can_signal_config_t *config);

/**
 * @brief 设置数据回调 (解析后的物理值)
 */
void can_manager_set_callback(can_data_callback_t cb);

/**
 * @brief 设置原始帧回调 (每次收到帧时调用, 含 dlc+data)
 */
void can_manager_set_raw_callback(can_raw_frame_callback_t cb);

/**
 * @brief 启动后台接收线程
 * @return 0=成功, -1=失败
 */
int can_manager_start(void);

/**
 * @brief 停止接收
 */
void can_manager_stop(void);

/**
 * @brief 获取信号配置数量
 */
int can_manager_get_signal_count(void);

#ifdef __cplusplus
}
#endif

#endif /* SERVICES_CAN_MANAGER_H */
