/**
 * @file    data_bus.h
 * @brief   统一数据总线 — 多数据源汇聚 + 线程安全发布/订阅
 *
 * 设计目标:
 *   项目从单一 MQTT 传感器扩展到 MQTT + Modbus + CAN 三种数据源后,
 *   需要一个统一的数据汇聚层来解耦"数据生产"和"数据消费"。
 *
 *   数据总线模式:
 *     MQTT/Modbus/CAN 作为 Producer (生产者) → publish 到 DataBus
 *     LVGL/Web/SQLite 作为 Consumer (消费者) → subscribe 接收更新
 *
 *   所有数据统一为 data_point_t 结构, 用 source + device_name + point_name
 *   三元组唯一标识一个数据点。
 *
 * 线程安全:
 *   所有公开 API 内部使用 pthread_mutex 保护, 可在任意线程调用。
 *   data_bus_publish() 只在锁内更新数据并复制回调快照，回调本身在锁外执行；
 *   回调可以安全地再次访问 Data Bus。
 *
 * 如何修改:
 *   - 添加新数据源: 在 data_source_t 枚举中新增类型
 *   - 修改缓冲区大小: 修改 DATA_BUS_MAX_POINTS
 *   - 添加更多消费者: 调用 data_bus_subscribe() 注册回调
 */

#ifndef SERVICES_DATA_BUS_H
#define SERVICES_DATA_BUS_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 数据定义 ==================== */

#define DATA_BUS_MAX_POINTS  64    /**< 最大数据点数 */
#define DATA_BUS_NAME_LEN    32    /**< 设备名/点名最大长度 */

/** 数据来源 */
typedef enum {
    DATA_SOURCE_MQTT   = 0,  /**< MQTT 传感器 (温湿度) */
    DATA_SOURCE_MODBUS = 1,  /**< RS485 Modbus 从站 */
    DATA_SOURCE_CAN    = 2,  /**< CAN 总线帧 */
} data_source_t;

/** 通用数据点 */
typedef struct {
    uint32_t id;                       /**< 数据点唯一 ID (自动分配) */
    data_source_t source;              /**< 数据来源 */
    char     device_name[DATA_BUS_NAME_LEN];  /**< 设备名称 */
    char     point_name[DATA_BUS_NAME_LEN];   /**< 数据点名称 (如 "temperature") */
    double   value;                    /**< 数值 */
    char     unit[16];                 /**< 单位 (如 "℃", "%", "rpm") */
    time_t   timestamp;                /**< 时间戳 */
    bool     valid;                    /**< 数据有效性 */
} data_point_t;

/** 数据更新回调: 每当有数据点被发布时调用 */
typedef void (*data_bus_callback_t)(const data_point_t *point, void *user_data);

/* ==================== API ==================== */

/**
 * @brief 初始化数据总线
 */
void data_bus_init(void);

/**
 * @brief 发布/更新一个数据点
 *
 * 如果 device_name + point_name 已存在, 则更新值和时间戳;
 * 否则创建新数据点。
 *
 * @param point  数据点 (内部会拷贝, 调用后可释放)
 */
void data_bus_publish(const data_point_t *point);

/**
 * @brief 订阅数据更新 (注册回调)
 * @param cb        回调函数
 * @param user_data 透传给回调的用户数据
 * @return 订阅 ID (用于取消订阅), -1 表示失败
 */
int data_bus_subscribe(data_bus_callback_t cb, void *user_data);

/**
 * @brief 取消订阅
 * @param sub_id  订阅 ID
 */
void data_bus_unsubscribe(int sub_id);

/**
 * @brief 获取指定设备的最新数据点
 * @param device_name  设备名称
 * @param point_name   数据点名称
 * @param point        输出: 数据点 (可 NULL, 只检查是否存在)
 * @return true=找到, false=不存在
 */
bool data_bus_get_latest(const char *device_name, const char *point_name,
                         data_point_t *point);

/**
 * @brief 获取所有最新数据点
 * @param points     输出数组
 * @param max_count  最大数量
 * @return 实际返回的数据点数
 */
int data_bus_get_all(data_point_t *points, int max_count);

/**
 * @brief 获取指定来源的所有数据点
 * @param source     数据来源
 * @param points     输出数组
 * @param max_count  最大数量
 * @return 实际返回的数据点数
 */
int data_bus_get_by_source(data_source_t source, data_point_t *points,
                           int max_count);

/**
 * @brief 打印所有数据点 (调试用)
 */
void data_bus_dump(void);

#ifdef __cplusplus
}
#endif

#endif /* SERVICES_DATA_BUS_H */
