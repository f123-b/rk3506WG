/**
 * @file    ota_manager.h
 * @brief   应用程序 OTA 更新管理器
 *
 * 功能:
 *   - HTTP 远程检查更新 (请求 version.json)
 *   - HTTP 下载应用包 (断点续传 + 进度回调)
 *   - SHA256 完整性校验 (带进度)
 *   - 可选签名验证回调接口
 *   - 版本号比较
 *   - 下载超时/重试机制 (3次)
 *   - 备份+自动回滚机制
 *   - 可选差分补丁 (bspatch)
 *   - LVGL 进度条 + Web API 状态上报
 *
 * 安全设计:
 *   - SHA256 校验确保下载文件完整性
 *   - 可插拔签名验证回调
 *   - 备份+回滚防止变砖
 *   - 版本号字符串比较防止降级
 *   - 下载应用文件大小限制 (默认 16MB)
 *   - 下载超时 + 3次重试
 *   - 断点续传减少流量浪费
 *
 * OTA 协议 (服务器端需提供):
 *   {base_url}/version.json  — 应用版本元数据
 *   {base_url}/{filename}    — 应用文件 (支持 Range 头)
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
    OTA_TYPE_APP,           /**< 应用程序原子替换 + 健康检查回滚 */
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
    OTA_ERR_SIZE,               /**< 应用文件大小超限 */
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
    char    update_type[16];    /**< 更新类型: "app" */
    char    build_date[16];     /**< 构建日期 "2026-07-03" */
    char    filename[128];      /**< 全量应用文件名 */
    char    sha256[65];         /**< 全量文件 SHA256 校验和 (hex) */
    char    changelog[512];     /**< 更新日志 */
    char    signature[512];     /**< 应用签名 (hex, 可选) */
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
 * @brief 应用签名验证回调
 * @param app_path       下载的应用文件路径
 * @param signature_hex  期望的签名 (hex 字符串, 来自 version.json)
 * @return true=签名有效, false=签名无效
 *
 * 如果不设置此回调, 则跳过签名验证 (仅 SHA256)。
 * 实现示例: 读取公钥 → 验证 ECDSA/RSA 签名。
 */
typedef bool (*ota_signature_verify_cb)(const char *app_path,
                                        const char *signature_hex);

/* ==================== API ==================== */

/**
 * @brief 初始化 OTA 管理器
 * @param ota_server_url  OTA 服务器基础 URL (如 "http://192.168.5.10:9090")
 */
void ota_init(const char *ota_server_url);

/**
 * @brief 设置 OTA 更新类型（当前仅支持应用程序）
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
 * @brief 设置签名验证回调 (不设置则仅做 SHA256 校验)
 * @param cb 签名验证函数指针 (NULL=跳过签名验证)
 */
void ota_set_signature_verify_callback(ota_signature_verify_cb cb);

/**
 * @brief 写入健康标志文件 (应用启动后调用, 表示正常运行)
 *
 * OTA 应用更新后, 更新 worker 会检查此文件是否存在。
 * 如果新版本在更新 worker 的宽限期内未写入, worker 自动回滚到 .bak 备份。
 */
void ota_write_health_marker(void);

/**
 * @brief 检查是否有新版本
 * @param info  输出: 服务器端版本信息 (仅在返回 true 时有效)
 * @return true=有新版本可用, false=已是最新或检查失败
 */
bool ota_check_update(ota_version_info_t *info);

/**
 * @brief 开始下载并应用应用程序更新 (阻塞调用)
 * @return true=已启动应用替换流程（旧进程随后退出）, false=失败
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
