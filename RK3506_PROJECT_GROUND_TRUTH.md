# RK3506G Linux 环境监测与边缘数据网关 — Project Ground Truth

> 文档用途：`mmsw` 面试助手项目事实基线、RAG 检索、项目追问生成、回答事实校验  
> 文档原则：以当前源码调用链为最高优先级；注释、README 和历史文档只能作为辅助信息  
> 源码分支：`codex/upload-project-20260831`  
> 源码基线提交：`3b36dd3786eaff5ec31b55d1cc7cce765812637d`  
> 应用版本：`3.1.4`  
> 目标平台：RK3506G Linux  
> 本文不作为性能 benchmark 报告；未测量的数据不得由 AI 自动补造。

---

## 0. 文档使用规则

### 项目事实优先级

1. 当前源码中的真实调用链、配置值和数据结构，优先级最高。
2. 当前源码中的注释只能说明设计意图，不能单独证明功能已经接通。
3. README、旧面试知识库、旧 QA 与当前源码冲突时，以当前源码为准。
4. “存在函数或接口”不等于“当前产品链路已使用”。
5. “存在可选回调或配置字段”不等于“当前默认运行时已经启用”。
6. 未测试、未部署、未形成调用链的能力必须明确回答为“当前没有完成或没有证据”。

### 证据级别

| 证据级别 | 含义 |
| --- | --- |
| confirmed-code | 当前源码和调用链能够直接确认 |
| confirmed-user | 本人已经明确确认的职责或项目事实 |
| confirmed-document | 当前仓库文档能够确认，但需要避免超过源码能力边界 |
| pending | 当前没有足够证据，不能当成确定事实 |
| not-measured | 没有正式测量结果，不能给出具体性能数字 |

### AI 回答约束

- 不允许把设计意图写成当前实现。
- 不允许为了让项目“更高级”而增加 TLS、认证、消息队列、断线补传、数字签名、systemd、Docker、IPC、CAN FD、完整 DBC 等当前不存在的能力。
- 面试官询问当前未实现能力时，先说明当前边界，再给出“如果继续升级，我会怎么做”。
- 面试官询问历史问题时，优先使用本文“项目难点与问题解决记录”，不要根据通用知识虚构事故过程。

---

# 1. 项目基本信息

## 项目身份

| 字段 | 内容 | 证据级别 |
| --- | --- | --- |
| 项目名称 | RK3506G Linux 环境监测与边缘数据网关 / Environment Monitor | confirmed-code |
| 应用名 | `my_test` | confirmed-code |
| 当前版本 | `3.1.4` | confirmed-code |
| 硬件平台 | RK3506G | confirmed-code |
| 操作系统 | Linux | confirmed-code |
| 开发语言 | C11 | confirmed-code |
| 构建系统 | CMake | confirmed-code |
| 项目完整起止时间 | 未确认 | pending |
| 本人主要职责 | 根据当前 README 中的本人确认，项目由本人独立负责和完成，覆盖架构、底层适配、通信、UI、数据库、Web、OTA 和构建配置 | confirmed-user |

## 项目背景

项目运行在 RK3506G Linux 设备上，目标是在一个嵌入式应用中同时完成环境数据接收、工业总线接入、本地界面、数据持久化、HTTP 状态查询、时间同步和应用 OTA。项目重点不只是协议本身，而是 Linux 多线程 I/O、UI 线程边界、共享状态、数据库刷盘和升级切换之间的协作。

## 项目目标

将 MQTT、Modbus RTU、SocketCAN、LVGL、SQLite、HTTP、NTP 和应用 OTA 集成到一个单进程嵌入式应用中，并保证后台 I/O 不直接破坏 UI 线程边界，关键共享数据有明确互斥或原子保护，服务退出时能够回收线程和文件描述符。

## 应用场景

- Linux 边缘采集网关。
- 环境传感器数据接收和本地显示。
- RS485/Modbus RTU 设备接入。
- CAN 总线数据采集与简单信号解析。
- 本地 HTTP 运维状态查询。
- 应用程序远程 OTA 更新。

---

# 2. 硬件、软件和技术栈

## 硬件平台

| 字段 | 内容 | 证据级别 |
| --- | --- | --- |
| 主控平台 | RK3506G | confirmed-code |
| 显示 | DRM framebuffer/display path | confirmed-code |
| 触摸 | Linux evdev | confirmed-code |
| RS485 | `/dev/ttyS3`，默认 9600-8N1 | confirmed-code |
| RS485 方向控制 | sysfs GPIO，默认 GPIO0 | confirmed-code |
| CAN | SocketCAN `can0` | confirmed-code |
| CAN 默认波特率 | 500000 bps | confirmed-code |
| 看门狗 | `/dev/watchdog`，设备存在时启用 | confirmed-code |

## 软件平台

| 字段 | 内容 | 证据级别 |
| --- | --- | --- |
| 语言 | C11 | confirmed-code |
| GUI | LVGL | confirmed-code |
| 目标显示输入 | DRM + evdev | confirmed-code |
| 主机模拟 | SDL2 | confirmed-code |
| MQTT | libmosquitto | confirmed-code |
| JSON | cJSON | confirmed-code |
| 数据库 | SQLite | confirmed-code |
| 网络 | POSIX socket | confirmed-code |
| 线程 | pthread | confirmed-code |
| 构建 | CMake | confirmed-code |

## 核心技术

MQTT、Modbus RTU、RS485、SocketCAN、LVGL、SQLite、POSIX socket、pthread、互斥锁、原子变量、NTP、HTTP、SHA-256、HTTP Range、应用 OTA、原子 `rename`、`fsync`、健康标记与回滚。

---

# 3. 系统架构

## 系统架构

系统是一个单进程、多线程 Linux 应用。主线程负责 LVGL 初始化、页面创建、1 秒周期任务、500 ms OTA UI 轮询和事件循环。网络与工业通信模块使用独立线程或库内部线程完成 I/O。OTA 下载由临时后台线程执行，真正的应用替换阶段通过 `fork()` 创建独立 worker 进程，旧应用随后退出。

## 架构层级

| 层级 | 模块 | 当前职责 |
| --- | --- | --- |
| HAL | `display_drm.c` | RK3506G 显示输出 |
| HAL | `touch_evdev.c` | 触摸输入 |
| HAL | `rs485_uart.c` | UART、RS485 收发方向控制 |
| HAL | `can_socket.c` | SocketCAN RAW socket |
| Service | `mqtt_client.c` | MQTT 连接、订阅、发布、消息解析 |
| Service | `modbus_master.c` | RTU 主站轮询、响应校验、寄存器发布 |
| Service | `can_manager.c` | CAN 接收线程、信号提取、Data Bus 发布 |
| Service | `data_bus.c` | 最新数据点缓存、查询、发布订阅 |
| Service | `data_recorder.c` | 温湿度内存缓冲和定时刷盘 |
| Storage | `database.c` | SQLite 建表、事务写入、查询函数 |
| UI | `ui/` | MQTT、Modbus、CAN、OTA 四页界面 |
| Web | `web_server.c` + `web/` | HTTP 静态文件和 API |
| Infra | `logger.c` | 线程安全日志和 1 MiB 轮转 |
| Infra | `watchdog.c` | `/dev/watchdog` 独立喂狗线程 |
| System | `ntp_sync.c` | UDP NTP 同步系统时间 |
| System | `ota_manager.c` | 更新检查、下载、校验、应用替换、回滚 |

## 关键架构事实

- MQTT 温湿度当前不经过 Data Bus；它通过 MQTT 回调直接进入 `on_sensor_data()`。
- Modbus 和 CAN 当前明确发布到 Data Bus。
- HTTP 的设备接口从 Data Bus 查询 Modbus/CAN 最新数据点。
- 温湿度 SQLite 记录当前来自 `on_sensor_data()` 直接调用 `data_recorder_record()`。
- 当前没有确认到“Data Bus 自动将所有 Modbus/CAN 数据写入 SQLite”的调用链。
- Data Recorder 不是独立线程。

---

# 4. 启动、主循环与退出

## 启动流程

当前 `main.c` 的实际启动顺序：

1. 记录应用启动时间。
2. 设置时区 `CST-8`。
3. 注册 `SIGINT` 和 `SIGTERM`。
4. 初始化日志。
5. 尝试初始化 60 秒看门狗。
6. 初始化 DRM/SDL 显示和 LVGL。
7. 初始化 evdev 触摸。
8. 创建 MQTT、Modbus、CAN、OTA 四个页面。
9. 创建 1 秒周期定时器和 500 ms OTA UI 定时器。
10. 立即刷新第一帧。
11. 写入 OTA 健康标记。
12. 创建 100 ms 一次性定时器，延迟初始化后台服务。
13. 进入 LVGL 主循环。

## 延迟服务初始化

100 ms 后执行的实际顺序：

1. `data_recorder_init()`，内部初始化 SQLite。
2. `web_server_start()`。
3. `ntp_sync_init()`。
4. 初始化 OTA 配置。
5. `data_bus_init()`。
6. 初始化并启动 MQTT。
7. 初始化 Modbus，添加 1 个默认从站并启动轮询线程。
8. 初始化 CAN，添加 1 个默认信号并启动接收线程。

## 退出流程

收到退出信号后：

- 取消 OTA。
- 停止 CAN manager。
- 停止 Modbus master。
- 停止 MQTT。
- 停止 NTP。
- 停止 HTTP server，并等待活跃客户端线程退出。
- 刷完 Data Recorder 缓冲并关闭数据库。
- 停止看门狗线程并关闭设备。
- 关闭触摸、显示和日志资源。

---

# 5. 线程与并发模型

## 线程模型

| 执行单元 | 是否存在 | 工作内容 | 退出方式 |
| --- | --- | --- | --- |
| 主线程 | 是 | LVGL、页面更新、周期任务 | `app_running=0` 后清理 |
| Mosquitto 网络线程 | 是 | MQTT 网络循环和回调 | `mosquitto_loop_stop()` |
| Modbus 轮询线程 | 是 | 顺序轮询从站 | atomic `running=false` + join |
| CAN 接收线程 | 是 | 1 秒超时读取 CAN、解析信号 | atomic `running=false` + join |
| NTP 线程 | 是 | 启动立即同步，随后每小时同步 | atomic flag + join |
| HTTP 监听线程 | 是 | `accept()` 客户端 | 关闭监听 fd + join |
| HTTP 客户端线程 | 是，detached | 单个请求处理 | 请求结束后自行退出；server stop 等待计数归零 |
| 看门狗线程 | 设备可用时存在 | 周期写 `/dev/watchdog` | atomic flag + join |
| UI OTA 线程 | 按需 | 检查或下载更新 | detached；完成后释放 OTA 锁 |
| Web OTA 线程 | 按需 | Web 触发后的检查、下载、应用 | detached；失败时释放 OTA 锁 |
| OTA apply worker 进程 | 按需 | 文件替换、新进程健康监控、回滚 | 独立进程退出 |
| Data Recorder 线程 | 不存在 | 当前仅有互斥缓冲和周期函数 | 不适用 |

## UI 线程边界

### 技术决策

LVGL 对象由主线程修改。后台 CAN 回调不会直接操作 LVGL，而是把最新原始帧和解析后的信号复制到 `can_ui_mutex` 保护的待处理缓冲，主线程的 1 秒定时器再更新 CAN 页面。

### 原因

LVGL 对象操作如果从多个线程直接并发执行，容易产生竞态、对象生命周期错误和难以复现的 UI 崩溃。当前实现通过“后台线程只写数据，主线程更新控件”收敛 UI 线程边界。

### 当前限制

当前缓冲保存的是“最新一条原始帧”和“最新一条解析信号”，不是完整消息队列。高频 CAN 数据可能被后来的数据覆盖，因此不能把它描述为无丢失事件队列。

---

# 6. 核心数据流

## MQTT 温湿度数据流

1. Mosquitto 网络线程收到 `MQTT_TOPIC` 消息。
2. `on_message_cb()` 使用 cJSON 解析 `temperature`、`humidity` 和可选 `valid`。
3. 调用 `on_sensor_data()`。
4. `sensor_mutex` 保护最新温湿度和 `g_new_data`。
5. 更新 HTTP 当前温湿度共享状态。
6. 调用 `data_recorder_record()` 写入内存缓冲。
7. 主线程 1 秒定时器发现新数据后更新 MQTT 页面和曲线。

### 重要边界

当前 MQTT 温湿度链路没有调用 `data_bus_publish()`，不要回答成“所有协议统一先进入 Data Bus”。

## Modbus 数据流

1. Modbus 轮询线程按配置顺序轮询从站。
2. 手工构造 RTU 请求帧。
3. RS485 切换 TX，写 UART，`tcdrain()` 后切回 RX。
4. 等待响应，默认单次超时 500 ms。
5. 校验从站地址、功能码、异常响应、字节数、总长度和 CRC。
6. 将每个寄存器作为 `register_<地址>` 数据点发布到 Data Bus。
7. HTTP `/api/device/modbus` 可以读取这些最新数据。

### 重要边界

当前 `main.c` 没有注册 Modbus 数据回调去调用 `ui_page_modbus_update_slave()`。因此不能宣称“Modbus 实时寄存器值已经完整接到 LVGL 页面”。当前 Modbus UI 存在展示函数和固定设备信息，但真实轮询数据到该页面的调用链未接通。

## CAN 数据流

1. CAN 接收线程调用 `can_read_frame()`，读取 SocketCAN RAW 帧。
2. 每条完整帧触发 raw callback，复制最新帧到 UI 待处理缓冲。
3. 按已配置 CAN ID 查找信号。
4. 使用简化 Intel little-endian 逐位提取原始值。
5. 使用 `value = raw * scale + offset` 得到物理值。
6. 触发上层回调，把最新信号复制到 UI 待处理缓冲。
7. 同时将解析后的信号发布到 Data Bus。
8. 主线程 1 秒定时器消费最新帧和最新信号并更新 CAN 页面。
9. HTTP `/api/device/can` 可查询 Data Bus 中的 CAN 数据点。

---

# 7. MQTT 模块

## 模块职责

负责 MQTT 客户端创建、连接、订阅、消息解析、发布、连接状态和重连。

## 工作流程

- `mosquitto_new()` 创建 client。
- 注册 connect、disconnect、message 回调。
- `mosquitto_connect_async()` 发起异步连接。
- `mosquitto_loop_start()` 启动 Mosquitto 网络线程。
- 连接成功后订阅默认主题 `esp32c6/sensor`。
- 收到 JSON 后解析温湿度。

## 关键参数

| 参数 | 当前值 | 证据级别 |
| --- | --- | --- |
| Broker | `192.168.5.10` | confirmed-code |
| 端口 | `1883` | confirmed-code |
| Topic | `esp32c6/sensor` | confirmed-code |
| Keepalive | 60 s | confirmed-code |
| 默认认证 ID/Secret | 空 | confirmed-code |

## 重连

应用层 `mqtt_client_retry_tick()` 使用 5、10、30、60 秒的分段退避值，并由主线程 1 秒定时器周期调用。

## 当前限制

- 默认 MQTT 没有 TLS。
- 默认没有认证凭据。
- 当前远程认证链路不应宣传为完成：`mqtt_client_init()` 创建 Mosquitto client 时才尝试应用当前凭据，而 `main.c` 是在 init 之后才调用 `mqtt_client_set_auth()`；该 setter 当前只保存字符串，没有再次调用 Mosquitto 的 username/password API。
- MQTT UI 的手动 publish 使用 `temp`、`humi` 字段，而接收解析器期待 `temperature`、`humidity`；因此该按钮不应描述为“发布后可被自身同一解析链完整回环验证”。
- 没有 MQTT 断线消息补传队列。

---

# 8. Modbus RTU 与 RS485

## 模块职责

RK3506 作为 Modbus RTU 主站，通过 RS485 轮询配置的从站并将寄存器数据发布到 Data Bus。

## 默认从站

| 参数 | 当前值 |
| --- | --- |
| Slave ID | 1 |
| Function | 03 |
| Start Address | 0 |
| Register Count | 2 |
| Poll Interval | 1000 ms |
| Device Name | `Temp/Humi Sensor` |

## RTU 响应防御性校验

当前代码不是“收到数据就按寄存器读”，而是在访问数据区之前检查：

- 响应长度至少满足最小 RTU 响应。
- 从站地址是否匹配。
- 功能码是否为异常响应。
- 功能码是否与请求一致。
- 字节数是否为正且不超过缓冲边界。
- 字节数是否超过请求寄存器数量允许范围。
- `3 + byte_count + CRC` 是否落在实际接收长度内。
- CRC16 是否匹配。

## RS485 技术决策

发送路径：

1. `rs485_mutex` 加锁。
2. GPIO 切到发送模式。
3. UART `write()`。
4. `tcdrain()` 等待 UART FIFO 真正发完。
5. GPIO 切回接收模式。
6. 解锁。

`tcdrain()` 是关键，因为如果只等待 `write()` 返回就立刻切 RX，UART 内部仍可能有未发送完成的数据。

## 当前实现事实

- UART 使用 POSIX termios。
- 默认 9600-8N1。
- GPIO 方向控制的实际实现是 `/sys/class/gpio` sysfs。
- 源文件顶部注释提到 libgpiod，但当前代码中没有实际 libgpiod 调用链，因此不要宣称“当前使用 libgpiod”。
- 最大从站数 8。
- 单次响应超时 500 ms。
- 最大 RTU 帧缓冲 256 B。
- 从站轮询是单线程顺序执行。

## 当前限制

- 没有自动扫描总线功能。
- 没有完整的 UI 任意读寄存器产品链路。
- 测试发送按钮默认 `RS485_TEST_SEND_ENABLE=0`，不会生成。
- Modbus 实时寄存器值到 LVGL Modbus 页面尚未形成当前调用链。

---

# 9. CAN 与 SocketCAN

## 模块职责

初始化 Linux SocketCAN RAW socket，读取经典 CAN 帧，根据配置提取简单信号，并将数据发布到 Data Bus 和 UI 待处理缓冲。

## 当前接口配置

- 默认接口：`can0`。
- 默认波特率：500 kbit/s。
- 初始化阶段通过 `ip link` 尝试 down、设置 bitrate、`fd off`、`loopback on`、`restart-ms 100`，然后 up。
- 如果高级参数命令失败，会回退为只设置 bitrate。
- 创建 `PF_CAN/SOCK_RAW/CAN_RAW` socket 并绑定接口。
- 开启 `CAN_RAW_RECV_OWN_MSGS`。

## 默认信号配置

| 参数 | 当前值 |
| --- | --- |
| CAN ID | `0x123` |
| start_bit | 0 |
| length | 16 |
| scale | 0.25 |
| offset | 0 |
| name | `Engine RPM` |
| unit | `rpm` |

## 解析能力边界

当前 `extract_signal()` 是简化实现：

- 按 Intel / little-endian 位序逐位提取。
- 不应宣传完整 Motorola big-endian 支持。
- `can_manager` 使用 `CAN_SFF_MASK` 进行 ID 匹配，因此当前信号解析按标准 11-bit ID 处理。
- 不应宣传完整扩展帧信号解析。
- HAL 初始化明确使用经典 CAN，`fd off`，不应宣传 CAN FD 数据链路。
- 当前没有 DBC 文件解析器。

## UI 并发设计

CAN 接收线程只复制最新数据；LVGL 更新在主线程完成。这个设计换取线程安全，但最新值缓冲会覆盖旧数据，因此不适合作为完整历史帧队列。

## 当前限制

- 测试发送控件默认 `CAN_TEST_SEND_ENABLE=0`。
- CAN Filter API 在 HAL 中存在，但当前 UI 没有完整 Filter 配置链路。
- 不存在“Auto:ON”之类自动控制产品能力。

---

# 10. Data Bus

## 模块职责

提供线程安全的最新数据点表、发布、查询和回调订阅 API，用于解耦部分工业总线生产者和 HTTP 查询消费者。

## 核心数据结构

每个 `data_point_t` 包含：

- id
- source
- device_name
- point_name
- value
- unit
- timestamp
- valid

## 当前真实唯一键

当前 `data_bus_publish()` 查找已有数据点时，只比较：

- `device_name`
- `point_name`

`source` 当前不是查重键的一部分。

因此虽然 `data_bus.h` 注释写了 `source + device_name + point_name` 三元组唯一标识，但真实实现不是三元组。面试中应以实现为准，不能回答成三元组唯一键。

## 并发设计

发布流程：

1. 对输入数据做本地拷贝和字符串终止保护。
2. `bus_mutex` 加锁。
3. 更新或新增数据点。
4. 在锁内复制更新后的数据和当前订阅回调快照。
5. 解锁。
6. 在锁外逐个执行回调。

## 技术决策

### 为什么回调放锁外

- 缩短 Data Bus 临界区。
- 慢回调不会长期占用总线 mutex。
- 回调如果再次调用 Data Bus API，不会因为同一把非递归 mutex 产生直接重入死锁。

### 代价

回调快照已经复制后，即使另一个线程执行 unsubscribe，该快照中的回调仍可能再执行一次。当前设计是“减少锁耦合”，不是强同步取消语义。

## 关键限制

| 参数 | 当前值 |
| --- | --- |
| 最大数据点 | 64 |
| 最大订阅者 | 16 |

当前订阅槽使用 `subscriber_count` 递增分配，unsubscribe 只把 active 置 false，不回收计数槽。因此生命周期内反复订阅/取消后可能最终到达 16 个槽上限。

## 当前实际业务链路

- Modbus → Data Bus。
- CAN → Data Bus。
- HTTP `/api/device/*` ← Data Bus。
- MQTT 温湿度当前不经过 Data Bus。
- SQLite 当前没有确认到 Data Bus subscriber 的持久化链路。

---

# 11. SQLite 与 Data Recorder

## 模块职责

`database.c` 负责 SQLite schema 和 SQL 操作；`data_recorder.c` 负责温湿度内存批量缓冲、定时 flush、数据保留清理。

## 数据库表

### `sensor_data`

- id
- timestamp
- temperature
- humidity
- valid
- timestamp index

### `device_data`

- id
- timestamp
- source
- device
- point_name
- value
- unit
- valid
- timestamp 和 device index

## 当前持久化调用链

温湿度：

MQTT 消息 → `on_sensor_data()` → `data_recorder_record()` → 内存缓冲 → `data_recorder_tick()`/满缓冲 flush → `database_insert_records()` → SQLite `sensor_data`。

## 关键技术决策

### 内存缓冲 + 批量事务

- 最大缓冲 120 条。
- 正常由主线程 1 秒定时器调用 `data_recorder_tick()`。
- 达到 `DB_WRITE_INTERVAL=60s` 时刷盘。
- 缓冲满时也强制 flush。
- SQLite 使用 `BEGIN IMMEDIATE TRANSACTION` 批量插入。
- 任意一条插入失败会 rollback。
- commit 失败也 rollback。
- 整个批次成功后才清空记录器缓冲。

### 数据库失败策略

- 批量写失败时保留当前内存缓冲。
- 如果缓冲已经满，强制 flush 仍失败，最新一条新记录会被丢弃，而不是覆盖旧缓冲。

## 并发事实

- Data Recorder 没有独立线程。
- MQTT 网络线程可能调用 `data_recorder_record()`。
- 主线程周期调用 `data_recorder_tick()`。
- `rec_mutex` 用于串行化记录器缓冲和 flush 路径。

## 当前限制

- `offline_count` 只统计 MQTT 断开期间产生的记录数量。
- 没有“断网后按序补发 MQTT”的实现。
- `database_query_history()` 和 `database_get_stats()` 函数存在，但当前 HTTP server 没有历史数据路由，因此不能宣传 Web 历史查询产品能力。
- `database_insert_device_data()` 函数存在，但当前没有确认 Modbus/CAN Data Bus 自动调用它；不能宣传所有设备数据已经持久化。
- 当前没有正式数据库吞吐 benchmark。

---

# 12. HTTP 服务

## 模块职责

使用 POSIX TCP socket 实现轻量 HTTP 服务，提供静态页面、当前传感器状态、系统状态、设备数据和 OTA API。

## 当前路由

| 方法 | 路径 | 当前作用 |
| --- | --- | --- |
| GET | `/api/sensor/current` | 当前 MQTT 温湿度共享状态 |
| GET | `/api/system/info` | 系统信息 |
| GET | `/api/health` | 健康状态 |
| GET | `/api/status` | 系统模块汇总状态 |
| GET | `/api/device/list` | Data Bus 全部最新数据点 |
| GET | `/api/device/modbus` | Data Bus 中 Modbus 数据点 |
| GET | `/api/device/can` | Data Bus 中 CAN 数据点 |
| GET | `/api/ota/check` | 同步检查是否有更新 |
| GET | `/api/ota/status` | OTA 状态和进度 |
| POST | `/api/ota/start` | 后台启动 OTA |
| GET | `/` 与其他静态路径 | `www` 目录静态资源 |

## 并发模型

- 一个 HTTP server 监听线程。
- 每个已接受客户端创建一个 detached 线程。
- `active_clients` 限制最大活跃客户端。
- stop 时关闭监听 fd、join server thread，再等待 detached 客户端计数归零。

## 防御性处理

- 最大请求缓冲 4096 B。
- 客户端接收超时 5 秒。
- 支持根据 `Content-Length` 继续接收请求体。
- 超过缓冲容量返回 413。
- 仅支持 GET 和 POST。
- `send()` 使用循环处理短写。
- 静态路径拒绝 `..` 和 `//`。

## 关键参数

| 参数 | 当前值 |
| --- | --- |
| HTTP 端口 | 8080 |
| 最大并发客户端 | 10 |
| 最大请求缓冲 | 4096 B |
| 单客户端接收超时 | 5 s |

## 当前限制

- 明文 HTTP。
- 没有 HTTPS。
- 没有用户认证。
- 没有 Token。
- `Access-Control-Allow-Origin: *`。
- 不应作为安全公网服务宣传。
- 不支持完整 HTTP/1.1 特性集合，当前响应按 HTTP/1.0 `Connection: close` 模式实现。

---

# 13. LVGL UI

## 模块职责

提供 480×800 本地 UI，使用顶部自定义标签栏和四个独立页面容器：MQTT、Modbus、CAN、OTA。

## UI 设计事实

- 没有使用 `lv_tabview`，而是自定义四按钮 TabBar。
- 支持点击切页和屏幕左右滑动切页。
- 目标设备使用 DRM/evdev。
- 主机模拟使用 SDL2。
- 1 秒定时器更新温湿度、时钟、IP、MQTT 状态、CAN 待处理数据，并驱动 Data Recorder 和 MQTT retry tick。
- 500 ms 定时器更新 OTA 状态。

## 防烧屏

- 30 秒无触摸后亮度降到 20。
- 120 秒无触摸后亮度降到 0。
- 触摸后恢复到 200。
- 当前背光路径固定为 `/sys/class/backlight/backlight/brightness`。

## 当前 UI 能力边界

- MQTT 页面真实温湿度更新链路已接通。
- CAN 原始帧/信号到 UI 的最新值链路已接通。
- Modbus 页面存在寄存器展示函数，但当前 `main.c` 未注册 Modbus callback，因此实时寄存器值到页面的链路未接通。
- RS485/CAN 测试发送控件默认关闭。

---

# 14. NTP

## 模块职责

使用原始 UDP socket 构造 48 字节 NTP v4 client 请求，读取 Transmit Timestamp，转换成 Unix 时间，并调用 `settimeofday()` 更新系统时钟。

## 当前服务器顺序

1. `ntp.aliyun.com`
2. `ntp1.aliyun.com`
3. `pool.ntp.org`
4. `time.google.com`

## 关键参数

- NTP 端口 123。
- 单服务器超时 5 秒。
- 启动后立即同步一次。
- 周期同步间隔 3600 秒。
- 等待期间每 5 秒检查一次退出标志，避免 stop 被 1 小时 sleep 卡住。

## 当前限制

- `settimeofday()` 需要足够系统权限。
- 当前实现取服务器 Transmit Timestamp 的整数秒部分，没有做完整的往返时延/偏移算法。
- 没有正式测量时间同步误差。

---

# 15. Watchdog

## 模块职责

设备存在时打开 `/dev/watchdog` 并创建独立喂狗线程。

## 当前流程

- `main()` 调用 `watchdog_init(60)`。
- 喂狗线程大约每 `timeout/2` 秒写入一次字符。
- stop 时先让线程退出，再写入魔术字符 `V` 并关闭 fd。

## 重要能力边界

当前看门狗是“独立线程固定周期喂狗”，不是“各业务任务分别汇报健康状态后统一决定是否喂狗”。因此：

- 如果业务线程挂死但看门狗线程仍在运行，当前实现仍可能继续喂狗。
- 不能把它描述为任务级 watchdog、分任务喂狗或完整软件健康监控。
- `watchdog_init()` 失败不会阻止主程序继续运行。

---

# 16. OTA

## 模块职责

当前 OTA 仅针对应用程序二进制，不是系统固件分区升级。

## 更新检查

1. 请求 `<server>/version.json`。
2. 解析 version、type、build_date、filename、sha256、changelog、signature、size、force_update。
3. 解析可选 delta_url、delta_sha256、base_version、delta_size。
4. 只接受 `type=app`。
5. 比较语义格式的主/次/补丁版本号。
6. 没有更新时返回 already latest。

## 下载

- 支持 HTTP Range 断点请求。
- 现有部分文件小于目标大小时尝试从已有大小继续。
- 下载文件大小上限 16 MiB。
- 单次 HTTP connect/请求有超时控制。
- 下载总超时参数 300 秒。
- 最大尝试次数 3。
- 下载后检查实际文件大小。

## 完整性校验

- 全量文件使用 version.json 的 SHA-256。
- 差分补丁先校验补丁 SHA-256，应用 `bspatch` 后再校验最终文件 SHA-256。

## 数字签名事实

代码存在 `ota_set_signature_verify_callback()` 框架，并且当 callback 已注册且 version.json 包含 signature 时会调用该 callback。

但是当前 `main.c` 没有注册数字签名验证 callback。因此当前默认运行时只有 SHA-256 完整性校验，不能宣传“已经完成数字签名真实性校验”。

## 差分更新事实

代码存在差分补丁路径：

- 只有 `base_version == APP_VERSION` 时使用 delta。
- 依赖 `/oem/bspatch`。
- 如果 base 不匹配会回退全量包。

当前文档没有目标板差分升级成功率或部署验证记录，因此应回答为“代码路径已实现，目标部署结果需单独验证”。

## 应用替换和回滚

1. OTA 下载线程完成校验后调用 apply。
2. `fork()` 创建 OTA apply worker 进程。
3. 旧应用执行 `sync()` 后 `_exit(0)`。
4. worker 为当前安装文件创建 `.bak`；优先 hard link，失败时同步复制。
5. 新应用复制到同目录 `.new` 临时文件，并 `fsync()`。
6. `rename(temp, install)` 原子替换正式路径。
7. 同步父目录并设置 0755。
8. worker `fork()` 启动新应用。
9. 等待 15 秒健康宽限期。
10. 如果看到 `/tmp/ota_ok`，删除健康文件和备份，更新成功。
11. 如果没有健康标记，终止新应用，恢复 `.bak` 并启动回滚版本。

## 健康标记真实语义

新应用在 `main.c` 完成显示/LVGL/UI 创建并立即刷新第一帧后，就调用 `ota_write_health_marker()`；此时 100 ms 延迟服务初始化还没有执行。

因此当前健康判定证明的是“新应用基本 UI 启动到健康标记位置”，不是“MQTT、Modbus、CAN、HTTP、NTP 等所有后台服务已经成功启动”。面试中不能把 15 秒健康检查描述成完整服务级 readiness probe。

## OTA 并发控制

- UI OTA 和 Web OTA 都使用 `ota_try_lock()` 防止并发更新。
- UI 检查/安装使用 detached pthread。
- Web `/api/ota/start` 也使用 detached pthread。
- 真正文件替换使用独立 worker 进程。

## 当前安全边界

- OTA server 默认是 HTTP，不是 HTTPS。
- SHA-256 只能保证内容完整性，不提供来源真实性。
- 当前默认没有数字签名 callback。
- 因此不能将当前 OTA 描述为完整安全启动链或密码学可信更新系统。

---

# 17. 日志与可观测性

## 日志

- 默认路径 `/tmp/my_test.log`。
- 同时输出 stderr 和文件。
- 日志写入使用 `log_mutex`。
- 每条文件日志立即 `fflush()`。
- 文件达到 1 MiB 后轮转为 `.1`。
- 只保留一个 `.1` 备份。

## HTTP 状态

当前存在 `/api/health`、`/api/status`、`/api/system/info` 等接口，可用于查看部分运行状态。

## 当前限制

- 没有 Prometheus metrics。
- 没有集中式日志上传。
- 没有 trace/span。
- 没有正式长期稳定性指标数据。

---

# 18. 项目难点与问题解决记录

## 项目难点 1：Data Bus 回调与锁耦合

### 问题

如果发布函数持有 Data Bus mutex 时直接执行订阅回调，慢回调会扩大临界区；回调如果重新访问 Data Bus，还可能发生重入死锁。

### 原因

共享数据表和订阅者表需要锁保护，但回调执行时间和行为不可控，不适合放在总线内部临界区。

### 解决方案

当前实现只在锁内更新数据点并复制回调快照，然后释放 mutex，再执行回调。

### 最终结果

当前源码已经把共享状态保护与业务回调执行分开，降低锁持有时间，并允许回调重新调用 Data Bus API。

### 技术取舍

取消订阅不能强制撤销已经复制到快照中的一次在途回调。

---

## 项目难点 2：Data Recorder 批量刷盘与失败保留

### 问题

逐条写 SQLite 会产生更多事务和 I/O 开销；同时如果批量提交失败后直接清空内存，会造成数据丢失。

### 原因

嵌入式存储更适合控制事务频率，并且数据库短时失败不能自动等价为数据应丢弃。

### 解决方案

使用最大 120 条内存缓冲，按 60 秒或缓冲满触发批量事务。只有批量事务成功才把 `buffered_count` 清零。

### 最终结果

当前数据库失败时保留原缓冲；只有“缓冲已满且强制 flush 仍失败”时才丢弃最新到达记录。

### 当前边界

这不是网络断线补传队列，只是本地数据库写入缓冲。

---

## 项目难点 3：CAN 后台线程与 LVGL 线程安全

### 问题

CAN 数据从接收线程到达，而 LVGL 页面由主线程管理。如果接收回调直接改 UI，存在跨线程竞态。

### 原因

I/O 线程和 UI 事件线程生命周期、调用时机不同。

### 解决方案

CAN 回调只把最新帧/信号复制到 `can_ui_mutex` 保护的待处理结构，主线程 1 秒定时器读取后更新 LVGL。

### 最终结果

当前 CAN 接收线程不直接调用 LVGL 更新函数。

### 技术取舍

使用最新值槽而不是消息队列，因此高频数据可能覆盖未显示的上一条数据。

---

## 项目难点 4：Modbus 异常帧和边界检查

### 问题

串口通信可能收到超时、短帧、异常响应、错误功能码、错误长度或 CRC 错误。如果直接按预期寄存器数量取数据，可能读取错误内容甚至越界。

### 解决方案

在解析寄存器之前依次检查最小长度、地址、异常功能码、功能码、byte count、总响应长度和 CRC。

### 最终结果

当前解析路径在访问数据区前完成主要边界判断，失败时返回错误并为该 Modbus 设备发布无效状态点。

---

## 项目难点 5：RS485 半双工方向切换

### 问题

`write()` 返回只代表数据进入内核/UART 发送路径，不代表最后一个字节已经离开 UART。如果立即把 DE/RE 切回接收，可能截断尾部数据。

### 解决方案

发送时先切 TX，写入后调用 `tcdrain()` 等待 UART 发送完成，再切回 RX。

### 最终结果

当前方向切换放在同一个 `rs485_mutex` 临界路径中，避免并发读写破坏 TX/RX 状态。

---

## 项目难点 6：HTTP 服务退出与 detached 客户端线程

### 问题

HTTP 客户端线程是 detached，如果停止 server 后立即释放共享资源，仍在执行的客户端线程可能继续访问这些资源。

### 原因

Detached thread 不能逐个 join。

### 解决方案

使用 `active_clients` + `client_cond` 统计活跃线程；stop 先关闭监听 socket 并 join server thread，再等待客户端计数归零。

### 最终结果

`web_server_stop()` 返回前会等待已经接受的 detached 客户端线程结束。

---

## 项目难点 7：OTA 文件替换的一致性和回滚

### 问题

直接覆盖正在运行的应用文件，在掉电、写失败或新版本无法启动时容易留下不可用程序。

### 原因

升级同时涉及下载完整性、文件落盘、路径切换、新进程启动和失败恢复，任何一步中断都可能使安装路径失效。

### 解决方案

当前实现采用 SHA-256、旧版本 `.bak`、同目录 `.new` 临时文件、`fsync()`、原子 `rename()`、父目录同步、新应用健康标记和 15 秒超时回滚。

### 最终结果

文件替换阶段不再依赖 shell `cp`/`killall` 安装脚本，核心切换由 C 代码和独立 worker 完成。

### 当前边界

健康标记写在后台服务初始化之前，因此当前回滚判断不是完整业务健康检查；数字签名也未默认接通。

---

## 项目难点 8：线程退出响应时间

### 问题

后台线程如果一次 sleep 很久或永久阻塞在 I/O，程序停止时 join 会长时间等待。

### 解决方案

- CAN 读使用 1 秒 timeout。
- Modbus 轮询等待按 100 ms 分片检查 running。
- NTP 一小时周期等待按 5 秒分片。
- HTTP stop 关闭监听 socket 打断 accept。

### 最终结果

主要常驻线程都有周期性退出检查或可打断 I/O 路径，避免正常退出无限等待。

---

# 19. 关键技术决策

| 技术决策 | 当前做法 | 原因/取舍 |
| --- | --- | --- |
| UI 线程安全 | 后台线程写共享数据，主线程改 LVGL | 避免跨线程 UI 竞态；代价是最新值槽会覆盖旧数据 |
| Data Bus 回调 | 锁内快照，锁外回调 | 缩短临界区，允许回调重入 |
| SQLite | 内存批量 + transaction | 降低事务频率；失败保留缓冲 |
| Modbus | 手工 RTU 组帧和校验 | 控制请求/响应细节；需要自己承担协议边界检查 |
| RS485 | `tcdrain()` 后切 RX | 防止 UART 尾部被截断 |
| HTTP | 轻量 POSIX socket | 依赖少；但不是完整、安全的 Web server |
| CAN UI | 最新值缓冲 | 实现简单、线程边界清晰；不是无丢失队列 |
| OTA 安装 | `.bak` + `.new` + fsync + rename | 降低半写入风险，支持回滚 |
| OTA 并发 | 全局 try-lock | 防止 UI/Web 同时启动升级 |
| 服务初始化 | UI 先显示，后台服务延迟 100 ms | 减少开机首屏被网络/设备初始化阻塞 |

---

# 20. 关键参数

| 参数 | 当前值 | 证据级别 |
| --- | --- | --- |
| APP_VERSION | 3.1.4 | confirmed-code |
| 屏幕尺寸 | 480 × 800 | confirmed-code |
| 图表点数 | 60 | confirmed-code |
| MQTT Broker | 192.168.5.10 | confirmed-code |
| MQTT Port | 1883 | confirmed-code |
| MQTT Topic | esp32c6/sensor | confirmed-code |
| MQTT Keepalive | 60 s | confirmed-code |
| HTTP Port | 8080 | confirmed-code |
| HTTP 最大客户端 | 10 | confirmed-code |
| HTTP 请求缓冲 | 4096 B | confirmed-code |
| DB 文件 | sensor_data.db | confirmed-code |
| DB flush 间隔 | 60 s | confirmed-code |
| Data Recorder 最大缓冲 | 120 条 | confirmed-code |
| 数据保留 | 30 天 | confirmed-code |
| Data Bus 最大点数 | 64 | confirmed-code |
| Data Bus 最大订阅槽 | 16 | confirmed-code |
| Modbus UART | /dev/ttyS3 | confirmed-code |
| Modbus 波特率 | 9600 | confirmed-code |
| Modbus 默认轮询 | 1000 ms | confirmed-code |
| Modbus 最大从站 | 8 | confirmed-code |
| Modbus 响应超时 | 500 ms | confirmed-code |
| Modbus 帧缓冲 | 256 B | confirmed-code |
| CAN 接口 | can0 | confirmed-code |
| CAN 波特率 | 500000 bps | confirmed-code |
| CAN 最大信号配置 | 32 | confirmed-code |
| NTP 同步间隔 | 3600 s | confirmed-code |
| NTP 单服务器超时 | 5 s | confirmed-code |
| Watchdog main 请求超时 | 60 s | confirmed-code |
| OTA 最大应用大小 | 16 MiB | confirmed-code |
| OTA HTTP 超时 | 30 s | confirmed-code |
| OTA 下载超时 | 300 s | confirmed-code |
| OTA 最大下载尝试 | 3 | confirmed-code |
| OTA 回滚宽限期 | 15 s | confirmed-code |
| OTA 健康文件 | /tmp/ota_ok | confirmed-code |
| OTA 安装路径 | /oem/my_test | confirmed-code |
| 日志文件 | /tmp/my_test.log | confirmed-code |
| 日志轮转阈值 | 1 MiB | confirmed-code |

---

# 21. 当前能力边界与禁止扩展回答

## 未实现或当前调用链未接通

- MQTT 断线后的待发送消息补传。
- MQTT TLS。
- 已验证可用的远程 MQTT 用户名/密码认证链路。
- HTTPS。
- HTTP 用户认证。
- HTTP Token。
- Web 历史数据产品接口。
- Data Bus 全量数据自动持久化 SQLite。
- Modbus 自动扫描。
- Modbus 实时寄存器值到当前 LVGL Modbus 页面完整链路。
- CAN DBC 解析。
- CAN Motorola big-endian 通用信号解析。
- CAN 扩展帧信号解析能力证明。
- CAN FD 数据处理链。
- 完整 OTA 数字签名真实性验证。
- secure boot / trusted boot。
- systemd unit。
- Docker 镜像。
- 进程间 IPC 队列、共享内存或管道架构。
- Prometheus/链路追踪。
- 按业务任务健康状态决定是否喂狗的 task watchdog。

## 代码存在但不能等价宣传为产品能力

| 代码/字段 | 正确解释 |
| --- | --- |
| `data_bus.h` 中 MQTT producer 注释 | 是设计意图；当前 MQTT 实际链路不 publish Data Bus |
| `data_bus.h` 中三元组唯一说明 | 注释与实现不一致；实现实际按 device_name + point_name 查重 |
| `database_query_history()` | 数据库函数存在；当前没有 HTTP 历史路由 |
| `database_insert_device_data()` | 函数存在；当前没有确认 Data Bus 自动调用链 |
| `ui_page_modbus_update_slave()` | UI 函数存在；当前 main 未注册 Modbus callback |
| `can_set_filter()` | HAL API 存在；当前 UI 没有完整 Filter 产品链 |
| CAN 源码注释“支持 Intel/Motorola” | 当前函数实现是简化 Intel little-endian；不要宣称完整 Motorola |
| RS485 源码注释提到 libgpiod | 当前实际代码使用 sysfs GPIO，不要宣称 libgpiod 已接入 |
| OTA `signature` 字段与 callback | 验证框架存在；当前 main 未注册 callback |
| OTA delta 字段 | 代码路径存在；依赖 bspatch 和服务器数据，未提供正式目标板成功率 |
| `offline_count` | 仅统计 MQTT 断开期间记录，不是补传队列 |
| Watchdog 后台喂狗线程 | 不是分任务健康监控 |

---

# 22. 测试与验证状态

## 已能从源码确认

- Host simulation 构建路径存在。
- ARM RK3506 交叉编译路径存在。
- 主机模拟通过 stub 隔离 RS485、CAN、MQTT、NTP、HTTP、DB、OTA、watchdog 等目标依赖。
- 目标路径链接 LVGL、Mosquitto、pthread、DRM、cJSON 等库。

## 性能指标

没有正式 benchmark 数据。当前不能给出以下确定数字：

- MQTT 最大消息吞吐。
- Modbus 最大设备数下的实际总轮询周期。
- CAN 最大稳定接收帧率。
- HTTP RPS。
- SQLite 每秒写入条数。
- OTA 成功率。
- 长时间运行 MTBF。
- 内存峰值和 CPU 峰值。

证据级别：not-measured。

## 主机模拟的证明边界

Host simulation 使用大量 stub，主要用于 UI 和部分业务逻辑开发，不能证明真实 RK3506 上的：

- DRM/evdev。
- RS485 电气和方向控制。
- CAN 硬件通信。
- MQTT 真实网络。
- NTP settimeofday 权限。
- SQLite 真正落盘行为。
- `/dev/watchdog`。
- OTA 文件替换和回滚。

---

# 23. 面试回答事实锚点

## 如果被问“这个项目是做什么的”

事实锚点：RK3506G Linux、单进程多线程、环境监测与边缘网关、MQTT、Modbus RTU、SocketCAN、LVGL、SQLite、HTTP、NTP、应用 OTA。

## 如果被问“项目最难的地方”

优先从以下真实工程点选择：

- Data Bus 锁外回调。
- CAN 后台线程与 LVGL 主线程边界。
- SQLite 批量事务和失败保留。
- Modbus 异常帧防御性解析。
- RS485 `tcdrain()` 和半双工方向控制。
- HTTP detached client 退出回收。
- OTA 原子替换、健康检查和回滚。

## 如果被问“为什么用 Data Bus”

正确回答核心：当前用于统一 Modbus/CAN 最新数据点模型并给 HTTP 查询；不要说 MQTT 和 SQLite 也已经完全接入 Data Bus。

## 如果被问“断网怎么办”

正确边界：MQTT 客户端有重连；温湿度可以继续写本地记录器的前提是本地仍收到数据。当前没有 MQTT 待发送消息断线补传。`offline_count` 只是统计，不是补传。

## 如果被问“OTA 安全吗”

正确边界：当前有 SHA-256、原子替换、备份和回滚；默认服务器是 HTTP，数字签名 callback 没有注册，所以不能称为完整安全 OTA。进一步升级应增加 HTTPS、签名、公钥信任根和 anti-rollback。

## 如果被问“看门狗怎么做”

正确边界：当前是独立线程定期喂 `/dev/watchdog`，不是分任务喂狗。若面向更高可靠性版本，应改为各关键任务更新 heartbeat，由 supervisor 判断所有关键任务健康后才喂硬件狗。

---

# 24. 源码证据索引

| 主题 | 主要源码 |
| --- | --- |
| 应用版本与参数 | `app_config.h` |
| 构建 | `CMakeLists.txt` |
| 启动、UI、共享状态、延迟服务初始化 | `main.c` |
| MQTT | `services/mqtt_client.c` |
| Modbus | `services/modbus_master.c` |
| RS485 | `hal/rs485_uart.c` |
| CAN | `services/can_manager.c`、`hal/can_socket.c` |
| Data Bus | `services/data_bus.c`、`services/data_bus.h` |
| Data Recorder | `services/data_recorder.c` |
| SQLite | `database.c` |
| HTTP | `web_server.c`、`web/api_system.c`、`web/api_device.c`、`web/api_status.c` |
| OTA Web | `web/api_ota_web.c` |
| OTA 核心 | `ota_manager.c` |
| OTA UI thread | `ui/ui_ota.c` |
| Modbus UI | `ui/ui_page_modbus.c` |
| CAN UI | `ui/ui_page_can.c`、`main.c` |
| NTP | `ntp_sync.c` |
| Watchdog | `infra/watchdog.c` |
| Logger | `infra/logger.c` |

---

# 25. 检索关键词

RK3506、RK3506G、Linux 网关、环境监测、边缘网关、C11、CMake、LVGL、DRM、evdev、pthread、mutex、atomic、MQTT、Mosquitto、Modbus RTU、RS485、CRC16、tcdrain、SocketCAN、CAN RAW、CAN 标准帧、Data Bus、数据总线、锁外回调、SQLite、WAL、批量事务、HTTP、POSIX socket、NTP、settimeofday、watchdog、OTA、HTTP Range、SHA256、bspatch、fsync、rename、健康标记、rollback、回滚、线程安全、退出清理、项目难点、并发、死锁、重入。

---

# 26. 最终事实声明

本文是当前 `codex/upload-project-20260831` 源码基线下用于面试助手的项目事实文档。任何后续回答如果与本文冲突，应重新查看最新源码，而不是继续沿用旧的 `PROJECT_INTERVIEW_KNOWLEDGE_BASE.md`、`PROJECT_INTERVIEW_QA.md` 或历史 README 说法。

当前最需要避免的五类错误回答：

1. 把 MQTT、Modbus、CAN 全部说成已经统一经过 Data Bus。
2. 把 Data Recorder 说成独立线程。
3. 把 Modbus 实时寄存器值说成已经完整接入 LVGL 页面。
4. 把 OTA SHA-256 校验说成已经完成数字签名安全验证。
5. 把后台固定喂狗说成分任务健康监控。
