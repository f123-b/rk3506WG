# RK3506 OTA 数字签名接入说明

## 1. 当前方案

v3.1.6 起，OTA 默认启用强制数字签名：

- 算法：**RSA-PSS + SHA-256**
- 实现：OpenSSL EVP / libcrypto
- 设备端仅保存 **PEM 公钥**
- 发布服务器保存 **RSA 私钥**
- `version.json` 中的关键 OTA 元数据会先被规范化成 manifest，再进行数字签名
- 设备只有在 manifest 验签成功后，才会信任版本号、更新类型、SHA256、差分信息和 `force_update`
- 下载完成后再使用已签名 manifest 中的 SHA256 校验实际制品
- 差分升级会先校验补丁 SHA256，应用补丁后再次校验最终完整文件 SHA256

默认配置位于 `app_config.h`：

```c
#define OTA_REQUIRE_SIGNATURE      1
#define OTA_PUBLIC_KEY_PATH        "/oem/keys/ota_public_key.pem"
#define OTA_SIGNATURE_ALGORITHM    "RSA-PSS-SHA256"
#define OTA_ALLOW_SIGNED_DOWNGRADE 0
```

## 2. 为什么签名 manifest，而不是只签二进制

只对应用二进制做签名，可以证明文件来自官方，但无法保护 `version.json` 里的更新决策字段。

v3.1.6 签名覆盖：

```text
OTA-MANIFEST-V1
version=<version>
type=<app|firmware>
build_date=<date>
filename=<artifact>
size=<bytes>
sha256=<artifact sha256>
force_update=<0|1>
delta_url=<delta filename or empty>
delta_sha256=<delta sha256 or empty>
delta_size=<bytes or 0>
base_version=<version or empty>
```

因此攻击者无法在不破坏签名的情况下修改：

- 目标版本号
- App / Firmware 更新类型
- 文件名和大小
- 完整制品 SHA256
- 强制更新标志
- 差分补丁地址、哈希、大小、基线版本

## 3. 生成 RSA 密钥

在安全的发布机上执行：

```bash
chmod +x tools/generate_ota_keys.sh
./tools/generate_ota_keys.sh ~/rk3506_ota_keys
```

会生成：

```text
~/rk3506_ota_keys/ota_private_key.pem
~/rk3506_ota_keys/ota_public_key.pem
```

要求：

- **私钥绝对不要提交到 GitHub**
- 私钥只放在发布服务器 / CI Secret / HSM
- 设备只部署公钥

将公钥部署到 RK3506：

```bash
adb shell "mkdir -p /oem/keys"
adb push ~/rk3506_ota_keys/ota_public_key.pem /oem/keys/ota_public_key.pem
adb shell "chmod 644 /oem/keys/ota_public_key.pem"
```

## 4. 生成签名版 version.json

例如发布 App v3.1.7：

```bash
python3 tools/ota_sign_manifest.py \
  --artifact build/my_test \
  --private-key ~/rk3506_ota_keys/ota_private_key.pem \
  --version 3.1.7 \
  --type app \
  --changelog "修复通信稳定性并优化 OTA"
```

脚本会自动：

1. 计算应用文件大小
2. 计算 SHA256
3. 构造规范化 manifest
4. 使用 RSA-PSS/SHA-256 私钥签名
5. 把二进制签名转换为 hex
6. 生成 `version.json`

示例：

```json
{
  "version": "3.1.7",
  "type": "app",
  "build_date": "2026-09-04",
  "filename": "my_test",
  "size": 1234567,
  "sha256": "...",
  "force_update": false,
  "changelog": "...",
  "delta_url": "",
  "delta_sha256": "",
  "delta_size": 0,
  "base_version": "",
  "signature_alg": "RSA-PSS-SHA256",
  "signature": "..."
}
```

## 5. 差分升级签名

如果有差分包：

```bash
python3 tools/ota_sign_manifest.py \
  --artifact build/my_test \
  --private-key ~/rk3506_ota_keys/ota_private_key.pem \
  --version 3.1.7 \
  --type app \
  --delta-file my_test_3.1.6_to_3.1.7.patch \
  --base-version 3.1.6
```

最终完整文件的 `sha256`、补丁 `delta_sha256` 和 `base_version` 都在同一份签名 manifest 中。

设备流程：

```text
version.json
    ↓
RSA-PSS manifest 验签
    ↓
判断版本/更新类型/是否使用差分
    ↓
下载 full 或 delta
    ↓
校验 signed sha256 / delta_sha256
    ↓
若为 delta：bspatch
    ↓
再次校验最终完整文件 sha256
    ↓
应用更新
```

## 6. 验签失败行为

以下任意情况都会拒绝 OTA：

- `OTA_REQUIRE_SIGNATURE=1` 但没有 `signature`
- 缺少 `signature_alg`
- 算法不是 `RSA-PSS-SHA256`
- 公钥文件不存在
- 公钥不是 RSA/RSA-PSS
- signature 不是合法 hex
- 任意 manifest 字段被修改
- 私钥与设备公钥不匹配
- 下载文件 SHA256 与 signed manifest 不一致
- 差分补丁最终结果 SHA256 不一致

## 7. 防降级

默认：

```c
#define OTA_ALLOW_SIGNED_DOWNGRADE 0
```

即使旧版本拥有合法官方签名，也不能默认降级，降低旧 signed manifest 被重放的风险。

同版本如果 `force_update=true`，仍可用于重新安装当前版本。

如果产品确实需要官方签名降级，可显式改为：

```c
#define OTA_ALLOW_SIGNED_DOWNGRADE 1
```

不建议在正式量产版本长期开启。

## 8. CI 自动测试

GitHub Actions 会自动：

- 编译 Host Simulation
- 编译 OpenSSL RSA-PSS 验签模块
- 生成临时 RSA 密钥
- 对测试 manifest 签名
- 验证正确签名必须成功
- 修改 manifest 内容后验证必须失败

因此后续修改签名实现时，可以自动发现“签名失效仍被接受”一类安全回归。

## 9. Buildroot / RK3506 依赖

ARM 目标新增：

```text
OpenSSL libcrypto
```

CMake 已加入：

```text
infra/signature_verify.c
crypto
```

RK3506 的 Buildroot/sysroot 需要启用 OpenSSL 开发库和 `libcrypto.so`。

## 10. 安全边界

当前签名机制解决的是 **OTA 制品和关键升级元数据的真实性、完整性以及默认防降级**。

仍建议产品化时继续增加：

- HTTPS/TLS，避免明文传输和流量分析
- 公钥轮换 / key-id
- 私钥放 HSM 或受保护 CI Secret
- A/B 系统分区与 Bootloader rollback index
- 硬件安全启动 Secure Boot
- 单调版本计数器或 eFuse 防物理降级
