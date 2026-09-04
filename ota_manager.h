/**
 * @file    ota_manager.h
 * @brief   企业级 OTA 远程固件升级管理器
 *
 * 功能:
 *   - HTTP 远程检查更新 (请求 version.json)
 *   - HTTP 下载固件包 (断点续传 + 进度回调)
 *   - SHA256 完整性校验 (带进度)
 *   - RSA-PSS + SHA-256 OTA manifest 数字签名验证 (OpenSSL EVP)
 *   - 版本号比较防止降级攻击
 *   - 下载超时/重试机制 (3次)
 *   - 备份+自动回滚机制
 *   - 差分补丁 (bspatch)
 *   - LVGL 进度条 + Web API 状态上报
 *
 * 安全设计:
 *   - SHA256 校验确保固件完整性
 *   - 设备端内置 RSA-PSS/SHA-256 验签，私钥只保留在发布服务器
 *   - 备份+回滚防止变砖
 *   - 版本号字符串比较防止降级
 *   - 固件大小限制 (默认 16MB)
 *   - 下载超时 + 3次重试
 *   - 断点续传减少流量浪费
 *
 * OTA 协议 (服务器端需提供):
 *   {base_url}/version.json  — 固件版本元数据
 *   {base_url}/{filename}    — 固件文件 (支持 Range 头)
 */

#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 枚举/常量 ==================== */

/** OTA 更新类型 */
typedef enum {
    OTA_TYPE_APP,           /**< 应用程序 (kill + 替换 + 重启) */
    OTA_TYPE_FIRMWARE,      /**< 完整固件镜像 (整机 reboot, 需要额外实现) */
} ota_type_t;

/** OTA 状态 */
typedef enum {
    OTA_IDLE,           /**< 空闲 */
    OTA_CHECKING,       /**< 正在检查更新 */
    OTA_DOWNLOADING,    /**< 正在下载 */
    OTA_VERIFYING,      /**< 正在校验 */
    OTA_APPLYING,       /**< 正在应用更新 */
    OTA_PATCHING,       /**< 正在应用差分补丁 */
    OTA_SUCCESS,        /**< 升级成功 (重启后生效) */
    OTA_FAILED,         /**< 升级失败 */
} ota_status_t;

/** OTA 错误码 */
typedef enum {
    OTA_ERR_NONE = 0,           /**< 无错误 */
    OTA_ERR_NETWORK,            /**< 网络错误 */
    OTA_ERR_SERVER,             /**< 服务器错误 (HTTP 非200) */
    OTA_ERR_VERIFY,             /**< SHA256/签名 校验失败 */
    OTA_ERR_SIZE,               /**< 固件大小超限 */
    OTA_ERR_VERSION,            /**< 版本号非法 (降级攻击) */
    OTA_ERR_WRITE,              /**< 写入文件失败 */
    OTA_ERR_TIMEOUT,            /**< 下载超时 */
    OTA_ERR_NO_UPDATE,          /**< 已是最新版本 */
    OTA_ERR_PATCH,              /**< 差分补丁应用失败 */
    OTA_ERR_BASE_MISMATCH,      /**< 基准版本不匹配 */
    OTA_ERR_SIGNATURE,          /**< 签名验证失败 */
    OTA_ERR_BUSY,               /**< OTA 操作进行中 */
} ota_error_t;

/** 版本信息 (从 version.json 解析) */
typedef struct {
    char    version[32];        /**< 版本号, 如 "2.1.0" */
    char    update_type[16];    /**< 更新类型: "app" 或 "firmware" */
    char    build_date[16];     /**< 构建日期 "2026-07-03" */
    char    filename[128];      /**< 全量固件/应用 文件名 */
    char    sha256[65];         /**< 全量文件 SHA256 校验和 (hex) */
    char    changelog[512];     /**< 更新日志 */
    char    signature[1025];    /**< manifest RSA 签名 (hex, 支持至 RSA-4096) */\n    char    signature_algorithm[32]; /**< 签名算法, 当前必须为 RSA-PSS-SHA256 */
    int64_t size;               /**< 全量文件大小 (字节) */
    bool    force_update;       /**< 是否强制更新 */

    /* 差分升级字段 (可选) */
    char    delta_url[256];     /**< 差分补丁下载路径 */
    char    delta_sha256[65];   /**< 差分补丁 SHA256 */
    int64_t delta_size;         /**< 差分补丁大小 (字节) */
    char    base_version[32];   /**< 差分基线版本 (补丁适用的旧版本) */
    bool    has_delta;          /**< 是否提供差分升级 */
} ota_version_info_t;

/** OTA 进度回调 (0-100, 描述文本) */
typedef void (*ota_progress_cb)(int percent, const char *msg);

/**
 * @brief 自定义 OTA manifest 签名验证回调
 * @param manifest       规范化后的 OTA manifest 原文
 * @param manifest_len   manifest 长度
 * @param signature_hex  version.json 中的十六进制签名
 * @return true=签名有效, false=签名无效
 *
 * 默认情况下无需设置该回调，系统会使用 OpenSSL EVP +
 * RSA-PSS/SHA-256 和 OTA_PUBLIC_KEY_PATH 中的 PEM 公钥进行验签。
 * 只有需要替换为安全芯片、HSM 或其他算法时才覆盖此回调。
 */
typedef bool (*ota_signature_verify_cb)(const uint8_t *manifest,
                                        size_t manifest_len,
                                        const char *signature_hex);

/**
 * @brief 平台固件写入回调
 * @param image_path 已下载并通过完整性/签名校验的固件镜像
 * @return true=已经安全写入目标分区并完成启动切换准备
 *
 * 固件分区布局与 Bootloader/A-B 策略强依赖具体产品，因此核心 OTA 不直接
 * 执行危险的 dd 命令，而由 BSP 层注册平台实现。
 */
typedef bool (*ota_firmware_apply_cb)(const char *image_path);

/* ==================== API ==================== */

/**
 * @brief 初始化 OTA 管理器
 * @param ota_server_url  OTA 服务器基础 URL (如 "http://192.168.5.10:9090")
 */
void ota_init(const char *ota_server_url);

/**
 * @brief 设置 OTA 更新类型 (固件 or 应用程序)
 */
void ota_set_type(ota_type_t type);

/**
 * @brief 获取当前 OTA 更新类型
 */
ota_type_t ota_get_type(void);

/**
 * @brief 设置应用程序安装路径 (仅 App 模式有效)
 * @param path  安装路径，如 "/oem/my_test"
 */
void ota_set_app_install_path(const char *path);

/**
 * @brief 设置进度回调 (用于 LVGL UI 更新)
 * @param cb 回调函数，下载/校验时调用，percent 0-100
 */
void ota_set_progress_callback(ota_progress_cb cb);

/**
 * @brief 设置自定义 manifest 签名验证回调
 *
 * 默认使用内置 RSA-PSS/SHA-256 + PEM 公钥验签。
 * 传 NULL 恢复内置验签实现。
 */
void ota_set_signature_verify_callback(ota_signature_verify_cb cb);

/**
 * @brief 覆盖 OTA 公钥路径
 * @param path PEM 公钥文件路径；NULL/空字符串恢复 app_config.h 默认路径
 */
void ota_set_public_key_path(const char *path);

/** 注册平台 Firmware OTA 写入后端；未注册时 Firmware 模式会安全失败 */
void ota_set_firmware_apply_callback(ota_firmware_apply_cb cb);

/**
 * @brief 写入健康标志文件 (应用启动后调用, 表示正常运行)
 *
 * OTA 应用更新后, 后台脚本会检查此文件是否存在。
 * 如果新版本启动后 5 秒内未写入, 脚本自动回滚到 .bak 备份。
 */
void ota_write_health_marker(void);

/**
 * @brief 检查是否有新版本
 * @param info  输出: 服务器端版本信息 (仅在返回 true 时有效)
 * @return true=有新版本可用, false=已是最新或检查失败
 */
bool ota_check_update(ota_version_info_t *info);

/**
 * @brief 开始下载并应用固件更新 (阻塞调用)
 * @return true=升级成功并准备重启, false=失败
 */
bool ota_download_and_apply(void);

/**
 * @brief 获取当前 OTA 状态 (线程安全)
 */
ota_status_t ota_get_status(void);

/**
 * @brief 获取 OTA 下载进度 0-100 (线程安全)
 */
int ota_get_progress(void);

/**
 * @brief 获取最后一次错误码
 */
ota_error_t ota_get_last_error(void);

/**
 * @brief 获取最后一次错误描述
 */
const char *ota_get_last_error_msg(void);

/**
 * @brief 尝试锁定 OTA 操作 (防止并发)
 * @return true=锁定成功可执行, false=已有OTA在进行中
 */
bool ota_try_lock(void);

/**
 * @brief 解锁 OTA 操作
 */
void ota_unlock(void);

/**
 * @brief 触发系统重启
 */
void ota_reboot(void);

/**
 * @brief 获取本地当前版本号
 */
void ota_get_local_version(char *buf, int max_len);

/**
 * @brief 取消当前 OTA 操作
 */
void ota_cancel(void);

#ifdef __cplusplus
}
#endif

#endif /* OTA_MANAGER_H */
