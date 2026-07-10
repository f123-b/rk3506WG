/**
 * @file    ui_ota.h
 * @brief   OTA 升级 UI — LVGL 状态显示 + 后台线程管理
 *
 * 两步流程:
 *   1. "Check Update" (ui_ota_check) — 仅检查, 不下载, 返回结果显示在UI
 *   2. "Install Update" (ui_ota_start) — 下载+校验+安装, 完成后自动重启应用
 *
 * 状态显示:
 *   - "正在检查更新..." (黄色)
 *   - "下载中 45%" (蓝色)
 *   - "校验中..." (黄色)
 *   - "准备重启..." (绿色)
 *   - "升级失败: xxx" (红色)
 */

#ifndef UI_OTA_H
#define UI_OTA_H

#include <lvgl.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 仅检查更新 (不下载)
 * 创建后台线程执行 ota_check_update(), 结果通过 ui_ota_poll() 显示
 * @return true=启动成功, false=已在运行中
 */
bool ui_ota_check(void);

/**
 * @brief 下载并安装更新 (Check + Download + Apply)
 * 创建后台线程执行: ota_check_update() → ota_download_and_apply()
 * 安装完成后自动重启应用
 * @return true=启动成功, false=已在运行中
 */
bool ui_ota_start(void);

/**
 * @brief OTA 状态轮询定时器回调 (每 500ms 调用一次)
 * 读取 ota_manager 的当前状态并更新 OTA 页面 UI
 */
void ui_ota_poll(lv_timer_t *timer);

/**
 * @brief 检查 OTA 线程是否正在运行
 */
bool ui_ota_is_running(void);

/**
 * @brief 获取最近一次检查的新版本信息 (检查完成后调用)
 * @param version 输出版本号 (至少32字节)
 * @param changelog 输出变更日志 (至少512字节)
 * @return true=有新版本信息可用
 */
bool ui_ota_get_last_check(char *version, char *changelog);

#ifdef __cplusplus
}
#endif

#endif /* UI_OTA_H */
