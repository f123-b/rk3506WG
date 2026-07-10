/**
 * @file    ui_dashboard.h
 * @brief   仪表盘 UI 接口 — LVGL 主屏幕
 *
 * 包含所有 LVGL 控件的创建和更新逻辑:
 *   - 顶部状态栏 (IP / MQTT / 时钟)
 *   - 温湿度传感器卡片
 *   - 实时曲线图 (LVGL Chart)
 *   - 底部功能按钮栏
 *
 * 原理:
 *   LVGL (Light and Versatile Graphics Library) 是嵌入式图形库。
 *   所有 UI 元素以对象树 (parent-child) 方式组织:
 *     lv_scr_act()              ← 根屏幕对象
 *       ├── status_bar          ← 状态栏容器
 *       │   ├── ip_label        ← IP 地址
 *       │   ├── mqtt_dot        ← MQTT 状态灯
 *       │   └── clock_label     ← 时钟
 *       ├── card_temp           ← 温度卡片
 *       ├── card_humi           ← 湿度卡片
 *       ├── chart_cont          ← 图表容器
 *       │   └── chart           ← LVGL 曲线图
 *       └── buttons[]           ← 4个功能按钮
 *
 *   数据通过 lv_timer (定时器) 驱动刷新:
 *     - 每 1 秒: 更新时钟、传感器数值、图表
 *     - 每 10 秒: 更新 IP 地址
 *     - 每 500ms: 更新 MQTT 状态灯动画
 *
 * 如何修改:
 *   - 修改颜色: 编辑本文件中的 lv_color_hex() 调用
 *   - 修改布局: 编辑 CARD_MARGIN/CARD_W/CHART_Y 等常量
 *   - 添加新控件: 在 ui_dashboard_create() 中添加创建代码
 */

#ifndef UI_DASHBOARD_H
#define UI_DASHBOARD_H

#include <lvgl.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 布局常量 (可修改) ==================== */
#define STATUS_H    50   /**< 状态栏高度 */
#define BTN_BAR_H   90   /**< 按钮栏高度 */
#define CARD_MARGIN 8    /**< 卡片间距 */
#define CARD_W      228  /**< 卡片宽度 */
#define CARD_H      100  /**< 卡片高度 */
#define BUS_BAR_H   90   /**< CAN/RS485 数据面板高度 */

/** 计算值 (无需修改) */
#define CONTENT_H   (SCREEN_HEIGHT - STATUS_H - BTN_BAR_H)
#define BUS_BAR_Y   (STATUS_H + CARD_H + CARD_MARGIN)
#define CHART_Y     (BUS_BAR_Y + BUS_BAR_H + CARD_MARGIN)
#define CHART_H     (CONTENT_H - CARD_H - BUS_BAR_H - CARD_MARGIN * 3)

/**
 * @brief 创建完整的仪表盘 UI
 *
 * 调用后所有 LVGL 控件被创建并显示。
 * 必须在 LVGL 初始化后调用。
 */
void ui_dashboard_create(void);

/**
 * @brief 更新温度显示
 * @param temp  温度值 (℃), NAN 表示无效
 */
void ui_dashboard_update_temp(float temp);

/**
 * @brief 更新湿度显示
 * @param humi  湿度值 (%), NAN 表示无效
 */
void ui_dashboard_update_humi(float humi);

/**
 * @brief 更新时钟显示 (自动使用当前系统时间)
 */
void ui_dashboard_update_clock(void);

/**
 * @brief 更新 IP 地址显示
 */
void ui_dashboard_update_ip(void);

/**
 * @brief 更新 MQTT 状态指示
 * @param connected 是否已连接
 * @param retry_count 重连次数 (0=未重连)
 */
void ui_dashboard_set_mqtt_status(bool connected, int retry_count);

/**
 * @brief 更新图表数据点
 * @param temp  新温度数据点
 * @param humi  新湿度数据点
 * @param valid 数据是否有效
 */
void ui_dashboard_add_chart_point(float temp, float humi, bool valid);

/**
 * @brief 获取按钮对象 (供外部注册事件)
 * @param index 0-3
 */
lv_obj_t *ui_dashboard_get_button(int index);

/**
 * @brief 获取 MQTT 状态灯对象 (供动画使用)
 */
lv_obj_t *ui_dashboard_get_mqtt_dot(void);

/**
 * @brief 获取 OTA 状态标签
 */
lv_obj_t *ui_dashboard_get_ota_label(void);

/**
 * @brief 检查是否有告警条件 (温度过高/湿度过高)
 * @param temp  当前温度
 * @param humi  当前湿度
 * @return true=有告警
 */
bool ui_dashboard_check_alarm(float temp, float humi);

/**
 * @brief 显示告警弹窗
 * @param msg 告警消息
 */
void ui_dashboard_show_alarm(const char *msg);

/**
 * @brief 更新 CAN 总线状态和数据
 * @param can_id   当前 CAN ID
 * @param tx_cnt   发送计数
 * @param rx_cnt   接收计数
 * @param tx_ok    上次发送是否成功
 */
void ui_dashboard_update_can(uint32_t can_id, int tx_cnt, int rx_cnt, bool tx_ok);

/**
 * @brief 更新 RS485/Modbus 总线状态和数据
 * @param tx_cnt   发送计数
 * @param tx_ok    上次发送是否成功
 */
void ui_dashboard_update_rs485(int tx_cnt, bool tx_ok);

/**
 * @brief 设置 CAN 状态指示灯
 * @param active   true=活跃(绿) false=错误(红)
 */
void ui_dashboard_set_can_led(bool active);

/**
 * @brief 设置 RS485 状态指示灯
 * @param active   true=活跃(绿) false=错误(红)
 */
void ui_dashboard_set_rs485_led(bool active);

#ifdef __cplusplus
}
#endif

#endif /* UI_DASHBOARD_H */
