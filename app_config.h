/**
 * @file    app_config.h
 * @brief   统一配置参数 — 所有可调参数集中管理
 *
 * 使用方法: 修改此文件中的宏定义即可改变程序行为，无需搜索代码。
 * 所有参数都有中文注释说明作用。
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* ==================== 应用版本号 (OTA 升级依据) ==================== */
#define APP_VERSION  "3.1.4"

/* ==================== MQTT 配置 ==================== */
#define MQTT_BROKER   "192.168.5.10"    /**< MQTT 服务器 (开发板本地) */
#define MQTT_PORT     1883              /**< MQTT 服务器端口 (默认1883) */
#define MQTT_TOPIC    "esp32c6/sensor"  /**< 订阅主题 */
#define MQTT_KEEPALIVE 60               /**< 心跳间隔 (秒) */

/* 本地 broker 不需要认证; 远程 broker 时填写以下字段 */
#define MQTT_DEVICE_ID   ""               /**< 设备ID (本地broker留空) */
#define MQTT_DEVICE_SECRET ""             /**< 设备密钥 (本地broker留空) */

/* ==================== HTTP 服务器配置 ==================== */
#define HTTP_PORT           8080        /**< Web 服务器端口 */
#define HTTP_MAX_CLIENTS    10          /**< 最大并发连接数 */
#define HTTP_WWW_DIR        "www"       /**< 静态文件目录 */

/* ==================== 数据存储配置 ==================== */
#define DB_WRITE_INTERVAL  60           /**< 数据库写入间隔 (秒) */
#define DATA_KEEP_DAYS     30           /**< 数据保留天数 */
#define DB_PATH             "sensor_data.db"  /**< 数据库文件路径 */

/* ==================== UI 配置 ==================== */
#define SCREEN_WIDTH        480         /**< 屏幕宽度 (像素) */
#define SCREEN_HEIGHT       800         /**< 屏幕高度 (像素) */
#define MAX_CHART_PTS       60          /**< 图表最大数据点数 */

/* ==================== NTP 配置 ==================== */
#define NTP_SYNC_INTERVAL   3600        /**< NTP 校准间隔 (秒) */

/* ==================== OTA 配置 ==================== */
#define OTA_DEFAULT_SERVER  "http://192.168.5.128:9090"  /**< OTA 服务器地址 */
#define OTA_DOWNLOAD_PATH   "/tmp/my_test_new"             /**< 固件/应用下载临时路径 */
#define OTA_APP_INSTALL_PATH  "/oem/my_test"               /**< App OTA: 应用安装目标路径 */
#define OTA_APP_STOP_CMD      "killall my_test"            /**< App OTA: 停止应用命令 */
#define OTA_APP_START_CMD     "/oem/my_test &"            /**< App OTA: 启动应用命令 */

/* ==================== 日志配置 ==================== */
#define LOG_FILE_PATH       "/tmp/my_test.log"  /**< 日志文件路径 (NULL=不写文件) */
#define LOG_LEVEL           3                   /**< 日志级别: 0=ERROR 1=WARN 2=INFO 3=DEBUG */

/* ==================== RS485 / Modbus 配置 ==================== */
#define MODBUS_DEVICE       "/dev/ttyS3"  /**< RS485 串口设备 */
#define MODBUS_BAUD         9600          /**< 波特率 (常用: 9600/19200/115200) */
#define MODBUS_GPIO_PIN     0             /**< 方向控制 GPIO (MAX485 DE/RE) — GPIO0_PA0, 原UART2_RX */
#define MODBUS_POLL_MS      1000          /**< 默认轮询间隔 (毫秒) */

/* ==================== CAN 总线配置 ==================== */
#define CAN_INTERFACE       "can0"        /**< SocketCAN 接口名 */
#define CAN_BITRATE         500000        /**< 波特率 (常用: 125k/250k/500k/1M) */

/* ==================== 测试模式配置 ==================== */
#define CAN_TEST_SEND_ENABLE    0       /**< CAN 测试发送: 周期性发送测试帧 (1=开启 0=关闭) */
#define CAN_TEST_ID             0x123   /**< 测试帧 CAN ID (标准帧 11bit) */
#define CAN_TEST_INTERVAL_MS    1000    /**< CAN 测试发送间隔 (毫秒) */
#define CAN_TEST_DATA_BYTES     8       /**< 测试帧数据字节数 (1-8) */

#define RS485_TEST_SEND_ENABLE  0       /**< RS485 测试发送: 周期性发送测试数据 (1=开启 0=关闭) */
#define RS485_TEST_INTERVAL_MS  2000    /**< RS485 测试发送间隔 (毫秒) */

/* ==================== 告警阈值 ==================== */
#define ALARM_TEMP_HIGH     35.0f   /**< 高温告警阈值 (℃) */
#define ALARM_HUMI_HIGH     90.0f   /**< 高湿告警阈值 (%) */

#endif /* APP_CONFIG_H */
