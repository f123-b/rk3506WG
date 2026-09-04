# 环境监测站 (Environment Monitor) v3.1.6

基于 **RK3506 + LVGL v9.1 + MQTT + SQLite3 + HTTP + NTP + OTA + Modbus + CAN** 的嵌入式物联网边缘网关系统。
4 标签页 UI（MQTT / Modbus / CAN / OTA），多协议工业传感器接入，Web 远程监控，App 热更新 OTA（备份回滚 + 断点续传 + 签名验证框架）。

> **v3.1.6 OTA 安全增强**：在 v3.1.5 工程修复基础上，正式接入 **RSA-PSS + SHA-256 数字签名**。设备端使用 OpenSSL EVP 和 `/oem/keys/ota_public_key.pem` 验证 signed manifest，只有验签通过后才信任版本号、更新类型、SHA256、差分参数和 `force_update`；默认强制签名并禁止已签名旧版本降级重放。仓库新增密钥生成/manifest 签名工具、篡改拒绝 CI 测试和 `docs/OTA_SIGNATURE.md`。

> **v3.1.5 工程修复**：修复 DataRecorder 递归锁死锁、LVGL 跨线程更新、MQTT/CAN/Modbus 共享状态竞争；MQTT/Modbus/CAN 统一接入 DataBus，Modbus/CAN 通过 RAM 缓冲写入 device_data；Web 历史接口改为真实 SQLite 数据；Watchdog 改为主循环心跳；补齐 CAN 扩展帧/Motorola 解析、真实健康检查、Host Simulation Stub 与 OTA 安全后端约束。

---

## 目录

1. [大白话介绍](#大白话介绍)
2. [硬件和软件架构](#硬件和软件架构)
3. [目录结构](#目录结构)
4. [每个文件的作用](#每个文件的作用)
5. [代码修改指南](#代码修改指南)
6. [OTA 远程升级超详细教程](#ota-远程升级超详细教程)
7. [技术原理通俗讲解](#技术原理通俗讲解)
8. [常见问题排查](#常见问题排查)
9. [简历项目描述](#简历项目描述)
10. [面试准备问答](#面试准备问答)

---

## 大白话介绍

### 这个项目是做什么的？

想象你有一个 ESP32-C6 传感器放在房间里，每隔几秒测量一次温度和湿度。这个项目的程序运行在 RK3506 开发板上，它：

1. **接收数据** — 通过 MQTT 协议订阅 ESP32 发来的温湿度数据，同时支持 Modbus RTU (RS485) 和 CAN 总线接入工业传感器
2. **屏幕显示** — 在 480×800 的触摸屏上用 LVGL 图形库画出暗色 UI 界面，**4 个标签页**切换：MQTT 传感器 / Modbus 总线监控 / CAN 总线监控 / OTA 升级
3. **存储历史** — 每分钟把数据写入 SQLite 数据库，保留最近 30 天
4. **Web 监控** — 内置 HTTP 服务器，浏览器打开即可看到实时仪表盘（温湿度 + MQTT/Modbus/CAN/OTA 各子系统状态）
5. **自动对时** — 启动时从互联网 NTP 服务器获取准确的北京时间
6. **远程升级** — OTA 两步流程：① 检测更新 ② 下载安装。App 热更新 (3秒完成)。**v3.1 新增**: 备份+自动回滚、HTTP Range 断点续传、固件签名验证框架、并发锁守卫
7. **防烧屏** — 触摸空闲 30s 自动降低背光，120s 关闭背光，触控唤醒
8. **开机自启** — Mosquitto MQTT Broker + 应用程序均开机自启动

### 我为什么需要看这个文档？

如果你是**嵌入式开发新手**，拿到这个项目代码不知道怎么下手，这篇文档就是为你准备的。每一行配置、每一个文件都会用大白话解释清楚。

---

## 硬件和软件架构

### 硬件连接

```
ESP32-C6 (传感器节点)    RS485传感器 (温湿度变送器)    CAN设备 (发动机/PLC)
      │ WiFi                    │ RS485                   │ CAN
      ▼                         ▼                         ▼
MQTT Broker              Modbus RTU 主站            CAN 管理器
(192.168.5.10:1883)      (/dev/ttyS3, 9600)        (can0, 500kbps, loopback)
      │                         │                         │
      └─────────────────────────┼─────────────────────────┘
                                │ 以太网
                                ▼
┌──────────────────────────────────────────────────────────────┐
│              RK3506G 开发板 (本设备)                            │
│  CPU: ARM Cortex-A7, 1GHz  |  RAM: 128MB DDR3                 │
│  屏幕: 480×800 LCD + 触摸   |  静态IP: 192.168.5.10            │
│                                                               │
│  运行 my_test v3.1.3:                                          │
│  ┌─────────────────────────────────────────────────────────┐  │
│  │ 4-Tab LVGL UI (DRM DIRECT, 480×800)                      │  │
│  │  [MQTT] [Modbus] [CAN] [OTA]                             │  │
│  │ MQTT Client (Mosquitto)  |  NTP Sync (UDP :123)          │  │
│  │ Modbus Master (RS485)    |  CAN Manager (SocketCAN)       │  │
│  │ HTTP Server (:8080)      |  OTA Manager v3.1 (增强版)     │  │
│  │ SQLite3 (sensor_data.db) |  Backlight Ctrl (防烧屏)       │  │
│  │ SHA256 模块              |  Health Marker (健康标志)      │  │
│  └─────────────────────────────────────────────────────────┘  │
│  Mosquitto MQTT Broker (:1883) — 开机自启                      │
└──────────────┬───────────────────────────────────────────────┘
               │ HTTP :8080
               ▼
    电脑浏览器访问 http://192.168.5.10:8080
```

### 软件分层架构

本项目的代码采用**六层架构**设计，每层有明确的职责：

```
┌─────────────────────────────────────────────────────────────┐
│                       main.c                                 │  ← 入口 (~330 行)
│              启动各层模块, 主循环调度                         │
├──────────┬──────────┬──────────┬────────────┬───────────────┤
│   ui/    │   web/   │ services/│  storage/  │     hal/      │  ← 功能层
│ LVGL界面 │ HTTP API │ 业务服务  │  数据存储   │  硬件抽象层    │
├──────────┴──────────┴──────────┴────────────┴───────────────┤
│                          infra/                              │  ← 基础设施
│                 日志系统 / 看门狗                              │
└─────────────────────────────────────────────────────────────┘
```

这种分层的好处是：**改 UI 不影响数据库，改网络协议不影响屏幕显示，各模块独立开发、独立测试**。

---

## 目录结构

```
my_test/
│
├── main.c                     # 程序入口: 4标签页切换, 服务初始化, 背光控制, 主循环
├── app_config.h               # 所有可修改的参数 (改参数只看这个文件!)
├── CMakeLists.txt             # 编译配置 (ARM交叉+Host模拟双模式)
├── sqlite3.c / sqlite3.h      # SQLite3 数据库源码 (amalgamation, 不用改)
├── README.md                  # 本文件
│
├── hal/                       # 【硬件抽象层】
│   ├── display_drm.c/h        #   DRM 显示驱动 (480x800 DIRECT 零拷贝)
│   ├── touch_evdev.c/h        #   触摸屏驱动 (/dev/input/event0)
│   ├── rs485_uart.c/h         #   RS485 UART + GPIO0 方向控制 (MAX485)
│   ├── can_socket.c/h         #   SocketCAN: loopback + restart-ms 自动恢复
│   ├── display_sdl.c/h        #   [Host] SDL2 模拟显示
│   ├── can_socket_stub.c      #   [Host] CAN 桩 (模拟收发)
│   └── rs485_uart_stub.c      #   [Host] RS485 桩 (模拟收发)
│
├── services/                  # 【服务层】核心业务逻辑
│   ├── mqtt_client.c/h        #   MQTT 客户端: 支持本地Broker/云端+设备认证
│   ├── data_recorder.c/h      #   数据记录器: 内存缓冲→批量写SQLite
│   ├── data_bus.c/h           #   数据总线: 统一汇聚 MQTT/Modbus/CAN
│   ├── modbus_master.c/h      #   Modbus RTU 主站: 多从站轮询
│   ├── can_manager.c/h        #   CAN 管理器: 信号解析 + 原始帧回调
│   └── ota_manager.c/h        #   OTA 管理器: 双模式 + SHA256 + 后台脚本安装
│
├── ui/                        # 【UI 层】LVGL 4标签页界面
│   ├── ui_page_mqtt.c/h       #   标签0-MQTT: 温湿度曲线+IP显示+按钮
│   ├── ui_page_modbus.c/h     #   标签1-Modbus: 设备卡片+TX/RX统计+按钮
│   ├── ui_page_can.c/h        #   标签2-CAN: 信号仪表+帧显示+TX/RX统计+按钮
│   ├── ui_page_ota.c/h        #   标签3-OTA: 版本信息+进度条+检测/安装按钮
│   ├── ui_ota.c/h             #   OTA 后台线程: 两步流程(检测|安装)+轮询更新
│   └── ui_dashboard.c/h       #   [兼容] 旧版仪表盘组件
│
├── web/                       # 【Web 层】HTTP 服务器 + REST API
│   ├── web_server.c/h         #   HTTP/1.0 核心: 路由分发+静态文件+多线程
│   ├── api_system.c/h         #   系统API: /api/system/info + /api/health
│   ├── api_ota_web.c/h        #   OTA API: check/status/start
│   ├── api_device.c/h         #   设备API: /api/device/list + modbus + can
│   └── api_status.c/h         #   综合状态API: /api/status (MQTT+Modbus+CAN+OTA)
│
├── storage/                   # 【存储层】
│   ├── database.c/h           #   SQLite3 封装: 建表/插入/查询/清理/统计
│   └── config_file.c/h        #   配置文件读写 (JSON, 零依赖解析)
│
├── infra/                     # 【基础设施】
│   ├── logger.c/h             #   分级日志: ERROR/WARN/INFO/DEBUG 双输出
│   ├── watchdog.c/h           #   硬件看门狗 /dev/watchdog
│   └── sha256.c/h             #   SHA-256: FIPS 180-4 纯C零依赖 + 进度回调
│
└── www/                       # Web 前端
    └── index.html             # 综合仪表盘: 温湿度+子系统状态+OTA按钮
```

**文件统计**: 共 29 个源文件 (.c), 27 个头文件 (.h), 1 个前端 HTML

---

## 每个文件的作用

### app_config.h — 所有可调参数

> **一句话**: 你想改什么参数，先来这个文件找。

**原理**: 把程序中所有"可以改的数字和地址"集中在一个头文件里，用 `#define` 宏定义。编译器会把所有用到宏的地方自动替换成对应的值。好处是改一个文件就能改变整个程序的行为。

**如何修改** (示例):
```c
#define APP_VERSION  "3.1.3"          // 每次发布新版本记得改这个
#define MQTT_BROKER  "192.168.5.10"    // MQTT 服务器地址 (本地或云端)
#define HTTP_PORT    8080              // Web 服务器端口
#define CAN_INTERFACE "can0"           // SocketCAN 接口名
#define CAN_BITRATE  500000            // CAN 波特率
#define MODBUS_DEVICE "/dev/ttyS3"     // RS485 串口设备
#define MODBUS_BAUD  9600              // Modbus 波特率
#define ALARM_TEMP_HIGH  35.0f         // 高温告警阈值 (℃)
#define OTA_DEFAULT_SERVER  "http://192.168.5.128:9090"  // OTA 服务器
#define MQTT_DEVICE_ID   ""            // 设备认证ID (本地broker留空)
#define MQTT_DEVICE_SECRET ""          // 设备认证密钥 (本地broker留空)
```

### main.c — 程序入口

> **一句话**: 程序从这里开始运行，负责创建4标签页UI、延迟初始化服务、主循环刷新屏幕。

**核心设计**:
- **4 标签页切换**: 自定义标签栏 (MQTT/Modbus/CAN/OTA)，通过 `LV_OBJ_FLAG_HIDDEN` 控制页面显隐，支持左右滑动
- **延迟服务初始化**: 屏幕先显示 (lv_refr_now)，100ms后一次性 timer 执行所有服务初始化，避免黑屏等待
- **防烧屏**: touch回调中检测空闲，30s→dim(20)，120s→off(0)，触控唤醒恢复到正常亮度(200)
- **CAN 双回调**: 信号解析回调 + 原始帧回调 (显示TX/RX帧数据)
- **IP 显示**: 每10秒通过 getifaddrs() 刷新，显示在MQTT状态栏

**初始化顺序**:
```
时区 → 日志 → 看门狗 → DRM → LVGL → 触摸 → 4页面创建 → 按钮绑定
→ 定时器(1s+500ms) → lv_refr_now(立即渲染首帧)
→ ★ ota_write_health_marker() ← 关键! 2秒内写入, OTA回滚脚本据此判断
→ 延迟初始化timer(100ms)
→ 主循环
```
延迟初始化内: data_recorder → web_server → NTP → OTA → MQTT(set_auth) → data_bus → Modbus → CAN

### hal/display_drm.c — DRM 显示驱动

> **一句话**: 负责打开屏幕、分配显存、让 LVGL 能"画"到屏幕上。

**原理** (通俗版):
1. 打开 `/dev/dri/card0` (Linux 把显卡抽象成文件)
2. 询问内核: "屏幕分辨率是多少?"
3. 内核回答: "480×800, 竖屏"
4. 程序说: "给我一块这么大的内存当画布" (dumb buffer)
5. 内核分配显存, 程序用 `mmap` 映射到自己能访问的地址空间
6. LVGL 直接往这块内存里写像素数据, 显卡自动扫描输出到屏幕

**DRM DIRECT 模式**: LVGL 直接渲染到 framebuffer, 零拷贝, 最省内存。适合静态 UI 场景 (不像视频播放需要双缓冲)。

### hal/touch_evdev.c — 触摸屏驱动

> **一句话**: 读取触摸屏坐标, 告诉 LVGL "用户手指在哪里"。

**原理**: Linux 把触摸屏抽象成 `/dev/input/eventX` 文件。程序以非阻塞模式 (`O_NONBLOCK`) 读取触摸事件，没有新事件时立即返回，不会卡住主循环。

### hal/rs485_uart.c — RS485 UART 驱动

> **一句话**: 控制 RS485 收发器的方向引脚 (DE/RE)，通过 UART 收发 Modbus 数据帧。

**原理**: RS485 是半双工总线，MAX485 芯片通过 DE (Driver Enable) 和 RE (Receiver Enable) 引脚切换收发方向。发送前拉高 GPIO → 发送数据 → 拉低 GPIO 回到接收模式。

### hal/can_socket.c — CAN Socket 驱动

> **一句话**: 初始化 Linux SocketCAN 接口，收发 CAN 数据帧。

**原理**: Linux 内核自带 SocketCAN 子系统，通过标准 `socket(PF_CAN, SOCK_RAW, CAN_RAW)` API 操作 CAN 总线。支持 **loopback 模式**（自应答，无需外部节点即可测试发送）+ **restart-ms 100**（bus-off 自动恢复）。

### services/mqtt_client.c — MQTT 客户端

> **一句话**: 连接 MQTT 服务器, 订阅传感器主题, 收到数据后通过回调通知其他模块。

**原理**:
- **MQTT 协议**: 发布/订阅模型, Broker (消息代理) 是中心
- **重连机制**: 断线后自动重连, 间隔逐渐拉长 (5s→10s→30s→60s, "指数退避")
- **设备认证**: 支持 `mqtt_client_set_auth(device_id, device_secret)` → `mosquitto_username_pw_set()`，兼容华为云IoT等平台
- **数据解析**: 收到 JSON `{"temperature":25.3,"humidity":65,"valid":true}` → cJSON 解析 → 回调通知主程序

### services/ntp_sync.c — NTP 时间同步

> **一句话**: 从互联网 NTP 服务器获取准确的北京时间, 校准系统时钟。

**原理**: 
1. 构造 48 字节的 NTP 请求包 → UDP socket 发送到 NTP 服务器 :123
2. 服务器回复 48 字节响应 → 解析时间戳 → 转换为 Unix 时间
3. `settimeofday()` 写入系统时钟 → `TZ=CST-8` 环境变量 → 北京时间

**多服务器切换**: ntp.aliyun.com → ntp1.aliyun.com → pool.ntp.org → time.google.com

### services/ota_manager.c — OTA 升级管理器 v3.1 (全面增强)

> **一句话**: 支持两种升级模式 — 完整固件升级 (reboot) 和 App 热更新 (秒级替换, 无需重启)。

**v3.1 新增特性**:

| 特性 | 说明 |
|------|------|
| 🔄 **备份+自动回滚** | 覆盖前备份 `.bak`, 新进程在 `lv_refr_now` 后立即写 `/tmp/ota_ok` (~2秒), 后台脚本等15秒后检查, 未检测到则自动回滚 |
| 📡 **断点续传** | HTTP Range 头支持, 网络中断后从断点继续下载 |
| 🔒 **并发锁守卫** | `ota_try_lock()`/`ota_unlock()` 防止重复启动OTA |
| ✍️ **签名验证框架** | 可插拔回调 `ota_signature_verify_cb`, 支持 RSA/ED25519/HMAC |
| 📊 **SHA256 进度** | `sha256_file_ex()` 带进度回调, 大文件校验不"卡住" |
| 🧵 **线程安全进度** | `ota_download_progress` 更新加 mutex 保护 |
| 📋 **鲁棒 JSON 解析** | 处理空白/转义字符, 比简单 strstr 更可靠 |
| 📝 **脚本错误上报** | 所有 cp/chmod/启动 操作输出到 `/tmp/ota_error.log` |
| 🧩 **SHA256 独立模块** | 拆分到 `infra/sha256.c/h`, 可复用, 可单测 |

**两种模式对比**:

| 对比 | Firmware 模式 | **App 热更新模式** |
|------|-------------|-------------------|
| 下载内容 | 完整固件镜像 (~16MB) | 应用程序二进制 (~1MB) |
| 应用方式 | reboot 整机 | kill 旧进程 → 替换文件 → 重启进程 |
| 耗时 | 1-2 分钟 | 3-5 秒 |
| 适用场景 | 内核/rootfs 更新 | 应用功能迭代 |
| version.json | `"type": "firmware"` | `"type": "app"` |

**App 热更新流程 (v3.1 增强版)**:
1. HTTP GET `{server}/version.json` → 鲁棒 JSON 解析 → 检测到新版本
2. HTTP GET (支持 Range 断点续传) 下载新的 `my_test` → 进度实时上报
3. SHA256 校验 (带进度回调, 大文件不卡UI) → 可选签名验证
4. 后台安装脚本 `/tmp/ota_apply.sh`:
   ```
   ① 清理旧健康标志 (rm /tmp/ota_ok)
   ② 备份当前版本 (cp → .bak)
   ③ sleep 2 (等待旧进程退出)
   ④ 替换二进制 (cp + chmod + sync)
   ⑤ 启动新版本
   ⑥ sleep 15 等待健康标志 (给新进程充足的初始化时间)
   ⑦ 若 /tmp/ota_ok 存在 → 升级成功, 清理 .bak
   ⑧ 若 /tmp/ota_ok 不存在 → 自动回滚到 .bak, 重启旧版本
   ```
   ★ 关键: 新进程在 `lv_refr_now` 后立即调用 `ota_write_health_marker()` (~2秒内),
   不等待延迟服务初始化完成, 确保后台脚本在15秒宽限期内能检测到健康标志。
5. `system("sh /tmp/ota_apply.sh &")` → 启动后台脚本
6. `_exit(0)` → 退出当前进程
7. 后台脚本自动完成 (含回滚保护, ~20秒)

整个过程**无需整机重启**。后台脚本方案确保 kill 当前进程后安装步骤不丢失。

**安全设计 (v3.1 增强)**:
- SHA256 校验 (FIPS 180-4, 独立 infra/sha256 模块, 带进度回调)
- 可插拔固件签名验证 (RSA/ED25519/HMAC)
- 备份+自动回滚 (健康标志文件机制, 防止变砖)
- 版本号语义化比较防降级
- 固件大小限制 (16MB)
- 非阻塞 connect + select 超时控制
- 3 次自动重试 + HTTP Range 断点续传
- 并发锁守卫 (pthread_mutex_trylock)

### services/data_recorder.c — 数据记录器

> **一句话**: 接收传感器数据, 在内存中缓冲, 每 60 秒批量写入 SQLite。

**原理**: 内存缓冲最多 120 条记录, 到期后一次性 INSERT (用事务包裹, 比逐条写入快 10 倍以上)。减少 NAND Flash 写入次数以延长寿命。

### services/data_bus.c — 数据总线

> **一句话**: 统一汇聚 MQTT、Modbus、CAN 三种数据源，统一格式后广播给 UI 和存储层。

**原理**: 发布/订阅模式 — 数据源 (MQTT/Modbus/CAN) 调用 `data_bus_publish()` 发布数据点，订阅者 (UI/存储) 通过 `data_bus_subscribe()` 注册回调。解耦数据生产者和消费者。

### services/modbus_master.c — Modbus RTU 主站

> **一句话**: 通过 RS485 总线轮询从站设备 (如温湿度变送器)，读取保持寄存器数据。

**原理**: Modbus RTU 是工业传感器最常用的协议。主站发送请求帧 `[从站ID][功能码][起始地址][寄存器数量][CRC16]`，从站返回数据帧。支持多从站轮询，可配置轮询间隔。

### services/can_manager.c — CAN 总线管理器

> **一句话**: 接收 CAN 总线数据帧，按信号配置 (起始位/长度/缩放因子) 解析出物理值。

**原理**: SocketCAN 接收原始 CAN 帧 (8 字节 data)，根据信号定义裁剪位段、应用 scale+offset，转换为有物理意义的数值 (如发动机转速、车速)。支持 J1939 等多帧协议扩展。

### ui/ui_page_mqtt.c — MQTT 传感器页面 (标签0)

> **一句话**: 温湿度卡片 + 24h 曲线图 + 状态栏(IP/时钟/MQTT状态) + 4个操作按钮。

### ui/ui_page_modbus.c — Modbus 总线监控页面 (标签1)

> **一句话**: 从站设备卡片 (ID/Func/Regs/Status) + TX/RX 统计进度条 + 4个操作按钮 (Send/Read/Auto Poll/Clear)。

### ui/ui_page_can.c — CAN 总线监控页面 (标签2)

> **一句话**: TX/RX 统计 + 信号仪表 (Engine RPM) + 最近帧显示 (TX+RX) + 4个按钮 (Send/Listen/Filter/Clear)。

### ui/ui_page_ota.c — OTA 升级页面 (标签3)

> **一句话**: 版本+服务器信息 + 状态标签 + 进度条 + 两步操作按钮 (检测更新 / 下载安装)。

### ui/ui_ota.c — OTA 后台管理

> **一句话**: 管理 OTA 升级的后台线程，两步流程：① `ui_ota_check()` 仅检查版本 ② `ui_ota_start()` 下载+安装。

### web/web_server.c — HTTP 服务器

> **一句话**: 解析 HTTP 请求, 分发到不同的 API 处理器。

**原理**: 使用 POSIX raw socket (零外部依赖) 实现 HTTP/1.0 服务器，多线程处理并发请求。

**路由表**:

| 方法 | 路径 | 处理函数 | 说明 |
|------|------|---------|------|
| GET | `/api/sensor/current` | web_server.c 内部 | 当前温湿度 |
| GET | `/api/sensor/history?hours=N` | web_server.c 内部 | 历史数据 |
| GET | `/api/system/info` | api_system.c | 系统信息 (含 OTA 状态) |
| GET | `/api/health` | api_system.c | 健康检查 (MQTT/NTP/磁盘) |
| GET | `/api/ota/check` | api_ota_web.c | OTA 检查更新 (仅查询, 不下载) |
| GET | `/api/ota/status` | api_ota_web.c | OTA 进度查询 |
| POST | `/api/ota/start` | api_ota_web.c | 触发 OTA 下载安装 (异步) |
| GET | `/api/device/list` | api_device.c | 设备列表 |
| GET | `/api/device/modbus` | api_device.c | Modbus 设备数据 |
| GET | `/api/device/can` | api_device.c | CAN 设备数据 |
| GET | `/api/status` | api_status.c | **综合状态**: MQTT+Modbus+CAN+OTA+System |
| GET | `/` | web_server.c 内部 | Web 仪表盘 (www/index.html) |

### web/api_status.c — 综合状态 API

> **一句话**: 一个请求返回所有子系统的运行状态，供 Web 仪表盘使用。

**返回结构**: `{mqtt:{connected,retries,broker,topic}, modbus:{active,data_points,tx_count}, can:{active,data_points,tx_count,rx_count}, ota:{status,progress,version,server}, system:{version,uptime,ntp_ok}}`

### storage/database.c — SQLite3 操作

> **一句话**: 封装 SQLite3 的常用操作: 建表/插入/查询/统计/清理。

**原理**: 使用 SQLite3 的 prepared statement 防止 SQL 注入, WAL 模式提升并发性能。

### infra/logger.c — 日志系统

> **一句话**: 提供 LOG_ERROR/WARN/INFO/DEBUG 四个级别的日志宏。

### infra/sha256.c — SHA-256 哈希模块

> **一句话**: 纯 C 零依赖的 SHA-256 算法实现 (FIPS 180-4)，支持流式计算和文件哈希进度回调。

**API**: `sha256_init/update/final` (流式) + `sha256_file()` (便捷) + `sha256_file_ex()` (带进度回调, 用于 OTA 大文件校验)。

### infra/watchdog.c — 看门狗

> **一句话**: 死机后自动重启系统, 不需要人去按复位键。

---

## 代码修改指南

### 改参数

1. 打开 `app_config.h`
2. 找到对应的 `#define`
3. 修改值, 保存
4. 重新编译: `cd build && make -j$(nproc)`
5. 部署新的 `my_test` 到开发板

### 改 UI 颜色

1. 打开对应的 `ui/ui_page_*.c` 文件
2. 搜索 `lv_color_hex(0x` — 这些都是颜色值
3. 找到你想改的颜色:
   - `0x0a0e17` = 深色背景
   - `0xf59e0b` = 温度/Modbus 强调色 (黄色)
   - `0x06b6d4` = 湿度/CAN 接收色 (青色)
   - `0x3b82f6` = MQTT/TX 蓝色
   - `0x10b981` = CAN 绿色
4. 替换为新的颜色值 (RGB888 格式: 0xRRGGBB)

### 改 MQTT

1. 打开 `app_config.h`, 修改 `MQTT_BROKER` / `MQTT_PORT` / `MQTT_TOPIC`
2. 如果 JSON 格式变了, 修改 `services/mqtt_client.c` 中的 `on_message_cb()` 函数

### 改 OTA 服务器地址

1. 打开 `app_config.h`, 修改 `OTA_DEFAULT_SERVER`
2. 或者在 `main.c` 中调用 `ota_init("http://你的IP:端口")`

### 添加 Modbus 从站

1. 打开 `main.c`，找到 `modbus_master_init()` 附近的代码
2. 复制从站配置模板:
```c
modbus_slave_config_t slv;
memset(&slv, 0, sizeof(slv));
slv.slave_id = 2;              // 新从站的 Modbus ID
slv.func_code = 3;             // 读保持寄存器
slv.start_addr = 0;
slv.nb_regs = 4;               // 读取寄存器数量
slv.poll_interval_ms = 1000;  // 轮询间隔
strncpy(slv.device_name, "新传感器", sizeof(slv.device_name) - 1);
modbus_master_add_slave(&slv);
```

### 添加 CAN 信号

1. 打开 `main.c`，找到 `can_manager_init()` 附近的代码
2. 复制信号配置模板:
```c
can_signal_config_t sig;
memset(&sig, 0, sizeof(sig));
sig.can_id = 0x18F00100;       // CAN ID
sig.start_bit = 16;            // 信号起始位
sig.length = 16;               // 信号位长度
sig.scale = 0.125f;            // 缩放因子
sig.offset = 0;                // 偏移量
strncpy(sig.signal_name, "车速", sizeof(sig.signal_name) - 1);
strncpy(sig.unit, "km/h", sizeof(sig.unit) - 1);
can_manager_add_signal(&sig);
```

### 调整防烧屏背光

`main.c` 中的背光控制常量:
```c
#define BL_NORMAL     200    // 正常亮度 (0-255)
#define BL_DIM         20    // 空闲30秒后微亮
#define BL_OFF          0    // 空闲120秒后关闭
#define IDLE_DIM_SEC   30    // 空闲多少秒后降低背光
#define IDLE_OFF_SEC  120    // 空闲多少秒后关闭背光
```

### Mosquitto 开机自启

板子上已配置 `/etc/init.d/pre_init/S01mosquitto`:
```sh
#!/bin/sh
start() {
    mosquitto -c /etc/mosquitto/mosquitto.conf -d
}
```
配置允许匿名访问 + 监听所有接口 (`/etc/mosquitto/mosquitto.conf`):
```
listener 1883 0.0.0.0
allow_anonymous true
```

---

## OTA 远程升级超详细教程

### 两种升级模式

本项目支持两种 OTA 模式，通过 `version.json` 中的 `type` 字段自动识别：

| 模式 | type 值 | 流程 | 耗时 | 适用 |
|------|---------|------|------|------|
| **App 热更新** (推荐) | `"app"` | 下载 → 校验 → kill → 替换 → 重启进程 | 3~5秒 | 日常应用更新 |
| 固件整机升级 | `"firmware"` | 下载 → 校验 → reboot | 1~2分钟 | 内核/rootfs 更新 |

### 第 1 步: 理解 OTA 流程 (App 模式)

```
RK3506 设备                          OTA 服务器 (你的电脑)
    │                                      │
    │  ① GET /version.json                  │
    │ ────────────────────────────────────> │
    │  ② 返回: {"version":"3.1.0","type":"app",...}
    │ <──────────────────────────────────── │
    │                                      │
    │  ③ 比较版本: 3.1.0 > 3.0.0 → 需要升级 │
    │     识别 type=app → App热更新模式      │
    │                                      │
    │  ④ GET /my_test_v3.1.0               │
    │ ────────────────────────────────────> │
    │  ⑤ 返回: 应用文件 (~1.1MB)             │
    │ <──────────────────────────────────── │
    │                                      │
    │  ⑥ SHA256 校验下载的文件              │
    │  ⑦ killall + rename + 重启 (3秒完成!)  │
```

### 第 2 步: 在电脑上搭建 OTA 服务器

```bash
# 1. 创建 OTA 目录
mkdir -p ~/ota_server && cd ~/ota_server

# 2. 创建 version.json (App 热更新模式)
cat > version.json << 'EOF'
{
  "device": "RK3506",
  "version": "3.2.0",
  "type": "app",
  "build_date": "2026-07-09",
  "filename": "my_test_v3.2.0",
  "size": 1200000,
  "sha256": "稍后填入",
  "signature": "",
  "changelog": "OTA v3.1增强: 备份回滚+断点续传+签名验证+并发锁+JSON增强",
  "force_update": false,

  "delta_url": "my_test_v3.1.0_to_v3.2.0.patch",
  "delta_sha256": "稍后填入(差分包sha256)",
  "delta_size": 50000,
  "base_version": "3.1.0"
}
EOF

# 3. 复制你刚编译的应用二进制到 OTA 目录
cp build/my_test ~/ota_server/my_test_v3.1.0

# 4. 计算 SHA256 哈希值
sha256sum ~/ota_server/my_test_v3.1.0 | awk '{print $1}'
# 输出类似: a1b2c3d4e5f6... (64个十六进制字符)

# 5. 把 SHA256 值填回 version.json 的 "sha256" 字段
#    用你喜欢的文本编辑器打开 version.json, 粘贴到 "sha256" 的值
#    同时也更新 "size" 字段为实际文件大小: ls -l ~/ota_server/my_test_v3.1.0

# 6. 启动 HTTP 服务器 (Python 内置的简易服务器)
python3 -m http.server 9090
# 输出: Serving HTTP on 0.0.0.0 port 9090
```

### 第 3 步: 在开发板上配置 OTA

`app_config.h` 中的关键配置:

```c
#define OTA_DEFAULT_SERVER  "http://192.168.5.128:9090"  // 改成你电脑的局域网 IP
#define OTA_APP_INSTALL_PATH  "/oem/my_test"              // 应用安装路径
#define OTA_APP_STOP_CMD      "killall my_test"            // 停止旧进程
#define OTA_APP_START_CMD     "/oem/my_test &"            // 启动新进程
```

App 模式在 `main.c` 中默认启用:
```c
ota_init(OTA_DEFAULT_SERVER);
ota_set_type(OTA_TYPE_APP);
ota_set_app_install_path(OTA_APP_INSTALL_PATH);
```

### 第 4 步: 触发升级（两步操作）

**Step 1 — 检测更新**（仅查询，不下载，不重启）:
- 设备屏幕: 切到 OTA 标签页 → 点击 **Check Update**
- Web 仪表盘: 点击 **🔍 检测更新** 按钮
- curl: `curl http://开发板IP:8080/api/ota/check`

**Step 2 — 下载安装**（下载 + SHA256校验 + 自动重启）:
- 设备屏幕: 点击 **Start Upgrade**
- Web 仪表盘: 点击 **⬇️ 下载安装** 按钮
- curl: `curl -X POST http://开发板IP:8080/api/ota/start`

### 第 5 步: 验证升级成功

App 热更新完成后，应用会自动重启。浏览器刷新后检查 `/api/system/info` 中的 `version` 字段。

### OTA 常见问题

| 问题 | 可能原因 | 解决方法 |
|------|---------|---------|
| "无法连接到 OTA 服务器" | 开发板 ping 不通电脑 / IP 配置错误 | 检查 `OTA_DEFAULT_SERVER` IP, 确认网络互通, 关闭防火墙 |
| "服务器未找到 version.json (404)" | version.json 不在 HTTP 根目录 | 确保在 `python3 -m http.server` 的启动目录下有 version.json |
| "SHA256 校验失败!" | version.json 中的 sha256 和实际文件不匹配 | 重新 `sha256sum` 计算并更新 version.json |
| "固件大小超限" | 固件超过 16MB | 检查编译产物, 正常应该 1-2MB |
| "已是最新版本" | 服务器版本号 ≤ 本地版本号 | version.json 中的 version 必须大于 `APP_VERSION` |
| App 热更新后进程没起来 | 安装路径不对 / 后台脚本未执行 | `ls -la /oem/my_test` 检查文件+权限; `cat /tmp/ota_error.log` 查看错误日志 |
| OTA 下载后设备无响应 | killall 杀掉了OTA线程自身 | v3.1 使用后台脚本 `sh /tmp/ota_apply.sh &` 解决，确保安装步骤不丢失 |
| 新版本启动后自动回到旧版本 | 健康标志未在15秒内写入 (新进程启动太慢) | 检查 DRM/LVGL 初始化是否正常; `cat /tmp/ota_error.log` 查看回滚日志; 健康标志在 `lv_refr_now` 后立即写入 (~2秒), 不依赖延迟服务初始化 |
| 下载网络中断后从头开始 | 旧版不支持断点续传 | v3.1 支持 HTTP Range 头，自动检测已有文件大小并续传 |
| 重复触发OTA导致异常 | 旧版无并发保护 | v3.1 使用 `ota_try_lock()` 互斥锁，重复触发返回 "OTA already in progress" |
| 签名验证失败 | version.json 中 signature 字段错误 | 检查签名生成算法; v3.1.6 默认 `OTA_REQUIRE_SIGNATURE=1`，不能留空；使用 `tools/ota_sign_manifest.py` 生成签名版 `version.json` |
| OTA 后 HTTP 无响应 | killall 导致旧连接的 CLOSE-WAIT 堆积 | `killall my_test && /oem/my_test &` 重启即可清除; 这是 POSIX socket 服务器的已知局限 |

### OTA 实测记录 (2026-07-09)

| 轮次 | 源版本 → 目标版本 | 结果 | 说明 |
|------|-------------------|------|------|
| 1 | 3.0.0 → 3.1.1 | ❌ 回滚 | 健康标志在延迟初始化末尾写入, 耗时 > 5s, 触发误回滚 |
| 2 | 3.0.0 → 3.1.2 | ✅ 成功 | 健康标志移到 `lv_refr_now` 后立即写入 (~2s), 宽限期增至15s |
| 3 | 3.1.2 → 3.1.3 | ✅ 成功 | 修复 `ota_get_local_version()` 的 `APP_VERSION` include, 所有API版本一致 |

**关键教训**: OTA 健康标志必须在进程启动的**最早阶段**写入 (DRM+LVGL 就绪后), 不能在延迟服务初始化之后。

---

## 技术原理通俗讲解

### DRM (Direct Rendering Manager)

> 把显卡内存直接映射到程序能访问的地址, LVGL 直接画到"屏幕内存"上。零拷贝, 性能最高。

### NTP (Network Time Protocol)

> 互联网"对表"协议。从 NTP 服务器获取精确时间, 减去 2208988800 (70年秒数) 得到 Unix 时间戳。

### MQTT (Message Queuing Telemetry Transport)

> 物联网的消息"微信群"。设备都连接到一个 Broker (群主)，发布/订阅模式实现数据分发。

### Modbus RTU

> 工业传感器最常用的通信协议。主站轮询从站，使用 RS485 半双工总线 (两根差分信号线 A/B + 地)。主站发送请求帧 → 从站返回数据帧，CRC16 校验。

### CAN (Controller Area Network)

> 汽车和工业自动化领域的主流总线。差分信号 (CAN_H/CAN_L)，多主模式，优先级仲裁 (ID 越小优先级越高)。Linux 通过 SocketCAN 子系统提供原生支持。

### LVGL (Light and Versatile Graphics Library)

> 嵌入式 GUI 库, C 语言编写, 对象树组织控件, 支持多种渲染后端 (DRM/fbdev/SDL)。

### OTA (Over-The-Air) 升级

> 通过 WiFi/网线把新固件/应用下载到设备上, 自动完成升级。

**App 热更新 vs 固件升级**: App 热更新只替换应用程序二进制 (~1MB)，通过 kill+rename+restart 在 3-5 秒内完成升级。固件升级替换整个系统镜像 (~16MB)，需要 reboot，耗时 1-2 分钟。日常功能迭代推荐 App 热更新模式。

### SQLite3 WAL 模式

> Write-Ahead Logging: 写入时先写到日志文件, 再批量写入主数据库。读写不互斥, 崩溃恢复更安全。

---

## 常见问题排查

### 编译问题

| 错误 | 原因 | 解决 |
|------|------|------|
| `fatal error: lvgl.h: No such file` | 找不到 LVGL 头文件 | 检查 CMakeLists.txt 中的 `SYSROOT` 路径 |
| `undefined reference to 'mosquitto_connect'` | 没有链接 mosquitto 库 | 检查 `target_link_libraries` 是否包含 `mosquitto` |
| `cc: unrecognized option '-mfloat-abi=hard'` | CMake 缓存了 host gcc 而非交叉编译器 | 重新 `cmake .. -DCMAKE_C_COMPILER=arm-buildroot-linux-gnueabihf-gcc` |

### 运行问题

| 现象 | 原因 | 解决 |
|------|------|------|
| 屏幕不显示 | DRM 初始化失败 | 检查 `/dev/dri/card0` 是否存在 |
| 触摸无效 | 触摸设备未探测到 | `ls /dev/input/event*` 确认设备节点 |
| 时间显示 "1970-01-01" | NTP 同步失败 | 检查网络连通性, 程序需 root 权限 (settimeofday) |
| MQTT 一直 "重连中" | Broker 地址不对或未启动 | `netstat -tln \| grep 1883` 检查 mosquitto |
| Web 页面打不开 | HTTP server 卡死 (OTA killall 副作用) | `killall my_test && /oem/my_test &` 重启 |
| CAN 发送失败 ENOBUFS | 总线无其他节点ACK | 启用 loopback 模式 (`ip link set can0 type can loopback on`) |
| Modbus 无数据 | 串口/GPIO 未初始化 | 检查 `/dev/ttyS3` 和 GPIO 权限 |
| Web仪表盘无法访问 | Host与板子不在同一子网 | 检查板子IP (`ifconfig eth0`), Host应能 `ping 192.168.5.10` |
| OTA卡在MQTT页面 | 旧版OTA一键执行了killall→重启默认标签 | 新版已拆为两步: 检测 + 安装分离 |

---

## 简历项目描述

> **基于 RK3506 的物联网边缘网关系统** (独立开发)
>
> 技术栈: **C, LVGL v9.1, MQTT, SQLite3, HTTP, NTP, Modbus RTU, CAN, DRM, POSIX 多线程, CMake**
>
> - 在 ARM Linux 平台 (Linux 6.1 / Buildroot / CMake / C11) 完成嵌入式应用全栈开发
> - 采用**六层模块化架构**: HAL硬件抽象层 → Services服务层 → UI(4标签页) → Web网络层 → Storage存储层 → Infra基础设施
> - 实现**多协议传感器数据采集**: 4标签页暗色主题 UI (DRM DIRECT 零拷贝, 480×800) + MQTT (Mosquitto + 设备认证) + Modbus RTU (RS485, GPIO方向控制) + CAN (SocketCAN, loopback模式), 通过数据总线统一汇聚
> - **从零设计企业级双模 OTA (v3.1 全面增强)**: ① App热更新 (后台脚本原子替换+秒级重启) ② 固件整机升级 — SHA256校验 (FIPS 180-4 独立模块+进度回调) + **RSA-PSS/SHA-256 signed manifest 验签** + **备份+自动回滚** (健康标志) + **HTTP Range 断点续传** + **并发锁守卫** + 鲁棒JSON解析 + 版本防降级 + select超时 + 3次重试 + LVGL进度 + Web API + 两步操作流程
> - **嵌入式 HTTP/1.0 服务器** (POSIX raw socket): REST API (JSON) + 静态文件 + CORS + 多线程 + 综合状态API (/api/status, 聚合 MQTT/Modbus/CAN/OTA/System)
> - **NTP 时间同步** (UDP) + 防烧屏背光控制 (空闲渐变→触控唤醒) + Mosquitto开机自启
> - **可靠性**: 硬件看门狗 + 分级日志 + SQLite WAL + MQTT 指数退避重连 + pthread_mutex

---

## 面试准备问答

| 问题 | 参考回答 |
|------|---------|
| **为什么用 SQLite 而非 MySQL?** | 嵌入式场景资源有限 (128MB RAM), SQLite 零配置/零服务/事务支持, 分钟级小数据量完全满足。MySQL 需要独立进程, 不适合嵌入式。 |
| **OTA 安全如何保证?** | 六重保障: ① SHA256 校验完整性 (FIPS 180-4, 独立模块+进度回调) ② 可插拔签名验证框架 (支持 RSA/ED25519/HMAC) ③ 备份+自动回滚 (健康标志在 `lv_refr_now` 后立即写入, 15秒宽限期, 防止变砖) ④ 版本号语义化比较防降级攻击 ⑤ 固件大小限制 (16MB) 防溢出 ⑥ 非阻塞 connect + select 超时 + 3次重试。App 热更新模式额外优势: 后台脚本原子替换, cp+sync 确保落盘。 |
| **为什么做 App 热更新而非只做固件升级?** | 固件升级需要 reboot, 中断服务 1-2 分钟。App 热更新只替换应用程序二进制, 通过 kill+rename+restart 在 3-5 秒内完成, Web/MQTT/屏幕自动恢复。Linux 内核支持热替换可执行文件 (inode 引用计数保护), 安全性有保障。 |
| **Modbus vs CAN 各适用什么场景?** | Modbus RTU (RS485) 适合楼宇/环境监测 (温湿度、CO2), 成本低、距离远 (1200m)。CAN 总线适合汽车/工业自动化 (发动机、PLC), 实时性好、多主模式、差分信号抗干扰强。 |
| **为什么用数据总线模式?** | 解耦数据生产者和消费者。MQTT/Modbus/CAN 只管采集数据发布到总线, UI/存储只管从总线订阅。新增一种数据源不需要改 UI 代码, 符合 SOLID 原则。 |
| **MQTT 为什么选 QoS 0?** | 传感器数据每分钟上报, 偶尔丢一两条不影响业务。QoS 1/2 需要 ACK 和重传, 增加带宽和延迟。 |
| **如何保证线程安全?** | 核心数据用 pthread_mutex 保护: MQTT 回调 (写) / HTTP API (读) / LVGL 定时器 (读) / Modbus 轮询 (写) 都通过互斥锁访问共享数据。 |
| **DRM DIRECT 模式 vs 双缓冲?** | DIRECT 模式零拷贝/低延迟/零额外内存。静态仪表盘场景 DIRECT 最优。 |
| **为什么要做分层架构?** | 每层独立, 改 UI 不影响数据库, 改协议不影响屏幕。符合 SOLID 原则。 |
| **CAN loopback 是什么?** | SocketCAN 的 loopback 模式让控制器自应答发送的帧。单节点测试时无需外部ACK即可发送，生产环境可关闭。配合 restart-ms 100 实现 bus-off 自动恢复。 |
| **如何防止 LCD 烧屏?** | 触摸空闲计时: 30s→背光降至20, 120s→背光关闭。触控检测到按下时立即恢复亮度+重置计时。通过 `/sys/class/backlight/backlight/brightness` 控制。 |
| **OTA 备份回滚怎么实现?** | ① 覆盖前备份 `.bak` ② 新进程在 `lv_refr_now` 后立即写入 `/tmp/ota_ok` (~2秒, 不等待服务初始化) ③ 后台脚本 `sleep 15` 后检查: 存在→升级成功清理备份; 不存在→`cp .bak` 回滚并重启旧版本。健康标志写在早期是关键, 否则服务初始化耗时可能导致误回滚。 |
| **断点续传怎么实现?** | 下载前 `file_size(dl_path)` 获取已有字节数, HTTP 请求头加 `Range: bytes={existing}-`, 文件以追加模式 (`ab`) 打开。服务器返回 206 Partial Content。已有完整文件则跳过下载直接进入校验。 |
| **如何防止并发OTA?** | `ota_try_lock()` 使用 `pthread_mutex_trylock()`, 如果锁已被持有立即返回 false。Web API 检测到锁被占用时返回 `{"success":false,"error":"OTA already in progress"}`。后台线程完成(或失败)后调用 `ota_unlock()`。 |

---

## 构建和部署

```bash
# 构建 ARM 目标 (首次 cmake 后直接 make)
cd my_test/build && make -j$(nproc)
# 产物: build/my_test (ARM 32-bit ELF, ~1.2MB)

# 注意: 如果 CMake 缓存了错误的编译器 (例如 host gcc),
# 报错 "unrecognized option '-mfloat-abi=hard'" 时需重新 cmake

# 部署到开发板 (via ADB)
adb push build/my_test /oem/my_test
adb push www/index.html /www/index.html
adb shell "ln -sf /www /oem/www"     # 确保 web 服务器能找到静态文件
adb shell "killall my_test; /oem/my_test &"

# Web 仪表盘
# 浏览器打开 http://192.168.5.10:8080

# 查看日志
adb shell "tail -f /tmp/my_test.log"

# 计算 SHA256 (用于 OTA 服务器)
sha256sum build/my_test | awk '{print $1}'

# 搭建 OTA 服务器 (在 Host 上)
mkdir -p /tmp/ota_server
cp build/my_test /tmp/ota_server/my_test_v3.1.4
# 创建 version.json (参考上面的OTA教程)
python3 -m http.server 9090 -d /tmp/ota_server

# 触发 OTA 升级 (via curl)
curl http://192.168.5.10:8080/api/ota/check        # 检测更新
curl -X POST http://192.168.5.10:8080/api/ota/start  # 下载安装
```

## 依赖

| 依赖 | 用途 | 备注 |
|------|------|------|
| **LVGL v9** | 图形库 | Buildroot 自带 |
| **Mosquitto** | MQTT 客户端库 | Buildroot 自带 |
| **cJSON** | JSON 解析 | Buildroot 自带 |
| **libdrm** | DRM 显示接口 | Buildroot 自带 |
| **SQLite3** | 数据库 | amalgamation, 编译进项目 |
| **libpthread** | POSIX 线程 | 系统自带 |
| **OpenSSL / libcrypto** | RSA-PSS + SHA-256 OTA 数字签名验签 | v3.1.6 新增 |

---

*文档更新: 2026-09-04 | 版本: v3.1.6*
