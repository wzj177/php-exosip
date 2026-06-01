# PHP-eXosip Extension

基于 eXosip2 的 PHP SIP 协议栈扩展，提供纯 OOP API，支持 GB28181、VoIP 等 SIP 应用开发。

## 特性

- ✅ **纯面向对象** - 简洁的 OOP API
- ✅ **事件驱动** - 支持所有 RFC 3261 SIP 方法
- ✅ **Master-Worker-Task 架构** - 多进程高并发支持
- ✅ **Task→Worker 管道通信** - 实时双向消息推送
- ✅ **TCP/UDP 双模式** - 完整的连接管理
- ✅ **GB28181 支持** - 国标视频监控协议
- ✅ **跨平台** - Linux/macOS/Windows
- ✅ **生产就绪** - 经过压测验证（1000+并发）

## 平台说明

本项目通过不同的 `config.m4` 支持不同操作系统：

| 文件 | 平台 | 说明 |
|------|------|------|
| `config.m4` | Linux (Ubuntu/CentOS/Debian 等) | 使用 `--start-group`/`--end-group` 处理 GNU ld 单遍链接 |
| `config_macos.m4` | macOS | 使用 macOS 框架库（CFNetwork 等），ld64 自动处理链接顺序 |

macOS 用户编译前需要切换配置文件：

```bash
cp config_macos.m4 config.m4
```

或使用 `build_macos_fix.sh` 自动处理。

> **注意**：macOS TCP 支持受限，推荐使用 UDP 模式。生产环境建议使用 Linux + TCP。

## 架构说明

本扩展提供两种运行模式：

### 1. Master-Worker-Task 多进程架构（推荐生产环境）

```
Master (监控) → Worker (SIP 事件循环) → Task Pool (异步任务)
                    ↕ (pipe 双向通信)
```

**优势**：
- ✅ SIP 事件循环永不阻塞
- ✅ Worker 崩溃自动恢复
- ✅ 高并发异步处理（HTTP、数据库、Redis）
- ✅ Task→Worker 实时推送（sendToWorker）
- ✅ TCP 连接管理（device_id ↔ fd 映射）

### 2. 单进程模式（适合小型应用）

直接使用 `run()` 方法，适合快速开发和测试。

## 快速开始

### 安装

```bash
# Ubuntu/Debian 一键编译
./build_ubuntu.sh --php-version=8.2

# 或手动编译
phpize
./configure --enable-exosip=/path/to/osip-build/libs
make
sudo make install

# 启用扩展
echo "extension=exosip.so" | sudo tee /etc/php/8.2/mods-available/exosip.ini
sudo phpenmod exosip
```

### 基础示例

```php
<?php
$sip = new ExoSip([
    'host' => '0.0.0.0',
    'port' => 5060,
    'mode' => 'UDP',  // UDP|TCP
]);

// 处理 REGISTER
$sip->onRegister = function($event) use ($sip) {
    echo "REGISTER from: " . $event->getFromUri() . "\n";
    $sip->sendResponse($event->getTid(), 200);
};

// 处理 MESSAGE  
$sip->onMessage = function($event) use ($sip) {
    $body = $event->getBody();
    echo "MESSAGE: " . $body . "\n";
    $sip->sendResponse($event->getTid(), 200);
};

// 启动事件循环
$sip->run();
```

### GB28181 服务端示例

```php
<?php
$sip = new ExoSip([
    'port' => 5060,
    'mode' => 'UDP',
    'sipId' => '34020000002000000001',
    'sipRealm' => '3402000000',
]);

// 设备注册处理
$sip->onRegister = function($event) use ($sip) {
    $deviceId = extractDeviceId($event->getFromUri());
    echo "Device registered: {$deviceId}\n";
    
    // 发送 200 OK
    $sip->sendResponse($event->getTid(), 200);
    
    // 查询设备目录
    $xml = <<<XML
<?xml version="1.0"?>
<Query>
<CmdType>Catalog</CmdType>
<SN>1</SN>
<DeviceID>{$deviceId}</DeviceID>
</Query>
XML;
    $sip->sendMessage("sip:{$deviceId}@3402000000", $xml);
};

// Keepalive 处理
$sip->onMessage = function($event) use ($sip) {
    $xml = simplexml_load_string($event->getBody());
    
    if ((string)$xml->CmdType === 'Keepalive') {
        echo "Keepalive from: {$xml->DeviceID}\n";
        $sip->sendResponse($event->getTid(), 200);
    }
};

$sip->run();
```

### SIP 客户端示例（手动模式 - 推荐）

```php
<?php
// 创建 SIP 客户端
$client = new ExoSipClient([
    'server_ip' => '127.0.0.1',
    'server_port' => 5060,
    'username' => 'device001',
    'password' => '123456',
    'realm' => '3402000000',
    'mode' => 'UDP'
]);

// 注意：不使用 start()，改用手动模式避免后台线程消费事件

// 发送注册
$client->sendRegister();

$registered = false;

// 事件循环
while (true) {
    // 手动处理事件（非阻塞）
    $events = $client->processEvents(100);
    
    foreach ($events as $evt) {
        // 注册成功
        if ($evt['type'] == EXOSIP_REGISTRATION_SUCCESS) {
            echo "Registered successfully!\n";
            $registered = true;
        }
        
        // 收到响应
        if (isset($evt['status_code'])) {
            echo "Response: {$evt['status_code']}\n";
        }
    }
    
    // 发送消息
    if ($registered) {
        $client->sendMessage('sip:server@domain', 'Hello!');
        break;
    }
    
    usleep(10000); // 10ms 休眠
}

// 注销
$client->sendUnregister();
sleep(1);
```

**重要说明**：
- ❌ **不要使用 `start()`** - 会导致后台线程消费事件，主线程收不到业务消息
- ✅ **使用 `processEvents()`** - 手动轮询事件，完全掌控事件处理
- ⚠️ **添加 `usleep()`** - 避免空循环导致 CPU 占用 100%

## API 文档

### ExoSip 类

#### 构造函数

```php
public function __construct(?array $config = null)
```

**配置参数**：
- `host` (string): 监听地址，默认 `0.0.0.0`
- `port` (int): 监听端口，默认 `5060`
- `mode` (string): 传输模式 `UDP|TCP`，默认 `UDP`
- `sipId` (string): SIP 服务器 ID
- `sipRealm` (string): SIP 认证域
- `sipPass` (string): SIP 认证密码
- `debug` (bool): 调试模式，默认 `false`

**Master-Worker-Task 配置**（可选）：
- `task_worker_num` (int): Task 进程数量，默认 `4`
- `timer_interval` (int): 定时器间隔（毫秒），默认 `1000`
- `pid_file` (string): PID 文件路径，例如 `/tmp/server.pid`

**示例**：
```php
// 单进程模式
$sip = new ExoSip([
    'host' => '0.0.0.0',
    'port' => 5060,
    'mode' => 'UDP',
]);

// Master-Worker-Task 模式
$sip = new ExoSip([
    'host' => '0.0.0.0',
    'port' => 5060,
    'mode' => 'UDP',
    'task_worker_num' => 4,      // 4个异步任务进程
    'timer_interval' => 30000,   // 30秒定时器
    'pid_file' => '/tmp/gb28181_server.pid',
]);
```

#### 核心方法

```php
// 启动事件循环（阻塞）
public function run(): bool

// 停止服务器
public function stop(): bool

// 非阻塞获取事件（已废弃：服务端生产请使用 run()）
public function processEvents(int $timeout = 0): array

// 发送 SIP 消息
public function sendMessage(string $to, string $message, ?string $contentType = null): bool

// 发送 SIP 响应
public function sendResponse(int $tid, int $code, ?string $reason = null): bool

// 获取统计信息
public function getStats(): array

// Master-Worker-Task 专用方法
public function addTask(array $data): int  // 投递异步任务，返回任务ID
public function getProcessStatus(): array  // 获取进程状态（内部调用）

// 静态方法
public static function getRunStatus(string $pidFile): array  // 从外部查询进程状态
```

**Master-Worker-Task 回调**：
```php
// 异步任务处理（在 Task 进程中执行）
public $onTask = function(int $taskId, array $data) {
    // 执行耗时操作（HTTP、数据库、Redis）
    $result = doSomeWork($data);
    
    // 实时推送结果给 Worker（不用等 return）
    $this->sendToWorker(['type' => 'progress', 'data' => $result]);
    
    return ['status' => 'success'];
};

// 任务完成回调（在 Worker 进程中执行）
public $onTaskFinish = function(int $taskId, $result) {
    // 处理 onTask 的 return 值
    echo "Task #{$taskId} finished: " . json_encode($result) . "\n";
};

// 管道消息回调（在 Worker 进程中执行）
public $onPipeMessage = function($server, $data) {
    // 处理 Task 通过 sendToWorker() 推送的消息
    if ($data['type'] === 'progress') {
        echo "Progress update: " . json_encode($data['data']) . "\n";
    }
};

// 定时器（在 Worker 进程中执行）
public $onTimer = function() {
    // 定期检查设备超时、清理资源等
    return true;  // 返回 true 继续运行，false 停止
};
```

#### 事件回调

```php
// 核心 SIP 方法
public $onRegister;   // REGISTER 事件
public $onInvite;     // INVITE 事件
public $onMessage;    // MESSAGE 事件
public $onBye;        // BYE 事件
public $onAck;        // ACK 事件
public $onCancel;     // CANCEL 事件
public $onOptions;    // OPTIONS 事件

// SIP 扩展
public $onInfo;       // INFO 事件
public $onSubscribe;  // SUBSCRIBE 事件
public $onNotify;     // NOTIFY 事件

// 响应和错误
public $onResponse;   // 响应事件（1xx-6xx）
public $onTimeout;    // 超时事件
public $onError;      // 错误事件
```

### SipEvent 类

```php
// 基础信息
public function getType(): int
public function getCode(): int
public function getTid(): int

// URI 信息
public function getFromUri(): ?string
public function getToUri(): ?string
public function getRequestUri(): ?string

// 消息内容
public function getBody(): ?string
public function getContentType(): ?string

// 会话信息
public function getSession(): ?SipSession

// GB28181 扩展
public function getExpires(): int  // Expires 头值
```

### SipSession 类

```php
public function getId(): ?int
public function getCallId(): ?int
public function getFromUri(): ?string
public function getToUri(): ?string
public function getRawBody(): ?string  // 原始消息体（含XML）
```

### ExoSipClient 类

#### 构造函数

```php
public function __construct(?array $config = null)
```

**配置参数**：
- `server_ip` (string, 必填): 服务器 IP
- `server_port` (int): 服务器端口，默认 `5060`
- `username` (string, 必填): 用户名/设备 ID
- `password` (string): 认证密码
- `realm` (string): 认证域
- `mode` (string): 传输模式 `UDP|TCP`，默认 `UDP`
- `local_ip` (string): 本地绑定 IP，可选
- `local_port` (int): 本地端口，`0` 自动分配
- `from_uri` (string): 自定义 From URI
- `expires` (int): 注册过期时间（秒），默认 `3600`
- `debug` (bool): 调试模式，默认 `false`

#### 核心方法

```php
// 启动客户端
public function start(): bool

// 停止客户端
public function stop(): bool

// 发送注册
public function sendRegister(): int

// 发送注销
public function sendUnregister(): int

// 发送消息
public function sendMessage(string $to_uri, string $body, ?string $content_type = null): int

// 发起呼叫/会话
public function sendInvite(string $to_uri, ?string $sdp = null): int

// 挂断呼叫/会话
public function sendBye(int $did, int $cid): int

// 发送 OPTIONS
public function sendOptions(string $to_uri): int

// 检查是否已注册
public function isRegistered(): bool

// 获取统计信息
public function getStats(): array

// 处理事件（非阻塞）
public function processEvents(int $timeout_ms = 0): array
```

## 应用场景

### 1. GB28181 国标视频监控

#### 服务端（平台侧）
- ✅ 设备注册和认证
- ✅ 目录查询
- ✅ 实时预览（INVITE/SDP）
- ✅ 云台控制（PTZ）
- ✅ 录像回放
- ✅ Keepalive 心跳

#### 客户端（设备侧）
- ✅ 设备注册到平台
- ✅ 定时发送 Keepalive
- ✅ 响应平台查询命令
- ✅ 接受 INVITE 推流
- ✅ PTZ 控制响应

**配合使用**：
- ZLMediaKit（流媒体服务器）
- Redis（设备状态管理）
- MySQL（设备数据存储）

### 2. VoIP 语音通话

- ✅ 用户注册
- ✅ 呼叫建立（INVITE）
- ✅ 媒体协商（SDP）
- ✅ 呼叫转移（REFER）
- ✅ 会议管理

### 3. SIP 即时消息

- ✅ MESSAGE 方法
- ✅ 状态呈现（PUBLISH/SUBSCRIBE/NOTIFY）
- ✅ 在线状态
- ✅ 文件传输

### 4. 通用 SIP 服务器

- ✅ 注册服务器
- ✅ 代理服务器
- ✅ B2BUA
- ✅ 负载均衡

## 性能

### 测试环境
- 系统：Ubuntu 22.04 LTS
- 配置：2核 4GB
- PHP：8.2

### 压测结果

| 指标 | 值 |
|------|------|
| 并发设备 | 1000+ |
| CPU 使用 | 45-60% |
| 内存使用 | 1.2GB |
| 响应延迟(P99) | 120ms |
| 注册 QPS | 150+ |
| 心跳 QPS | 80+ |
| 运行时长 | 24h 无崩溃 |

## 生产部署

### Master-Worker-Task 模式（推荐）

```php
<?php
// production/gb28181_server.php
require_once __DIR__ . '/GB28181Handler.php';

$sipServer = new ExoSip([
    'ua' => 'GB28181-Server/1.0',
    'ip' => '0.0.0.0',
    'port' => 5060,
    'mode' => 'udp',
    
    // 多进程配置
    'task_worker_num' => 4,
    'timer_interval' => 30000,  // 30秒
    'pid_file' => '/tmp/gb28181_server.pid',
]);

// 绑定业务处理器
$gb28181 = new GB28181Handler($sipServer, [
    'server_id' => '34020000002000000001',
    'server_domain' => '3402000000',
]);
$gb28181->bindEvents();

// 异步任务处理
$sipServer->onTask = function($taskId, $data) {
    $type = $data['type'] ?? 'unknown';
    $payload = $data['payload'] ?? [];
    
    try {
        switch ($type) {
            case 'webhook':
                // HTTP 请求
                $result = file_get_contents($payload['url'], false, stream_context_create([
                    'http' => [
                        'method' => 'POST',
                        'header' => 'Content-Type: application/json',
                        'content' => json_encode($payload['data']),
                        'timeout' => 5,
                    ]
                ]));
                return ['success' => true, 'response' => $result];
                
            case 'save_catalog':
                // 数据库操作
                $db = new PDO('mysql:host=localhost;dbname=gb28181', 'user', 'pass');
                $stmt = $db->prepare("INSERT INTO catalog ...");
                $stmt->execute($payload);
                return ['success' => true];
                
            default:
                return ['success' => false, 'error' => 'Unknown task'];
        }
    } catch (Exception $e) {
        return ['success' => false, 'error' => $e->getMessage()];
    }
};

// 任务完成回调
$sipServer->onTaskFinish = function($taskId, $result) {
    if ($result['success']) {
        error_log("Task #{$taskId} completed successfully");
    } else {
        error_log("Task #{$taskId} failed: {$result['error']}");
    }
};

// 定时器
$sipServer->onTimer = function() use ($gb28181) {
    $gb28181->processTimeouts();
    return true;
};

$sipServer->run();
```

**进程状态查询**：
```php
<?php
// production/status.php
$status = ExoSip::getRunStatus('/tmp/gb28181_server.pid');

echo "Master PID: {$status['master']['pid']}\n";
echo "Worker PID: {$status['worker']['pid']}\n";
echo "Worker Memory: " . round($status['worker']['memory_rss_kb'] / 1024, 2) . " MB\n";
echo "Task Workers: " . count($status['tasks']) . "\n";
```

### 单进程模式

```php
<?php
$sip = new ExoSip(['port' => 5060]);
$sip->onRegister = function($event) { /* ... */ };
$sip->run();
```

**注意**：单进程模式适合小型应用（<100 并发），生产环境推荐使用 Master-Worker-Task 模式。

### Systemd 服务

```ini
[Unit]
Description=GB28181 SIP Server (Master-Worker-Task)
After=network.target

[Service]
Type=simple
User=www-data
WorkingDirectory=/opt/gb28181
ExecStart=/usr/bin/php /opt/gb28181/production/gb28181_server.php
ExecReload=/bin/kill -HUP $MAINPID
PIDFile=/tmp/gb28181_server.pid
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

**操作**：
```bash
# 安装服务
sudo cp gb28181.service /etc/systemd/system/
sudo systemctl daemon-reload

# 启动服务
sudo systemctl start gb28181

# 查看状态
sudo systemctl status gb28181

# 开机自启
sudo systemctl enable gb28181

# 查看日志
sudo journalctl -u gb28181 -f
```

### Docker 部署

```dockerfile
FROM ubuntu:22.04
RUN apt-get update && apt-get install -y php8.2 php8.2-dev
COPY exosip.so /usr/lib/php/8.2/
CMD ["php", "server.php"]
```

## 目录结构

```
php-exosip/
├── php_exosip.c          # PHP 扩展主代码（类定义、Zend API 绑定）
├── exosip_wrapper.c      # eXosip2 封装（核心 SIP 逻辑、多进程架构）
├── exosip_wrapper.h      # C 头文件（结构体、枚举、函数声明）
├── php_exosip.h          # PHP 扩展头文件
├── ServerInfo.h           # 服务器配置结构体
├── Client.h              # 客户端配置结构体
├── config.m4             # 编译配置（Linux）
├── config_macos.m4       # 编译配置（macOS）
├── build_ubuntu.sh       # Ubuntu 一键编译脚本
├── build_centos.sh       # CentOS 一键编译脚本
├── osip-build/           # osip/eXosip2 编译脚本
├── examples/             # 使用示例
└── tests/                # C 测试程序
```

## 常见问题

### 1. macOS TCP 模式无响应？

**原因**：eXosip2 在 macOS 上 TCP 支持有限（kqueue 部分支持）

**解决**：使用 UDP 模式或 Docker Linux 环境

```php
$sip = new ExoSip([
    'mode' => PHP_OS_FAMILY === 'Darwin' ? 'UDP' : 'TCP'
]);
```

### 2. 端口被占用？

```bash
# 查看占用进程
sudo lsof -i :5060

# 杀死进程
sudo kill -9 <PID>
```

### 3. 性能优化？

**使用 Master-Worker-Task 架构**（推荐）：
```php
$sip = new ExoSip([
    'task_worker_num' => 4,      // 4个异步任务进程
    'timer_interval' => 30000,   // 30秒定时器
]);

// HTTP/数据库/Redis 等耗时操作在 Task 进程中执行，不阻塞 SIP 事件循环
$sip->onTask = function($taskId, $data) {
    // 异步执行
    return doSomeWork($data);
};
```

### 4. 调试技巧？

```php
// 启用调试日志
$sip = new ExoSip(['debug' => true]);

// 使用 tcpdump 抓包
sudo tcpdump -i any -n -A port 5060

// 使用 wireshark 分析
wireshark -i any -f "port 5060"
```

## 开发指南

### 编译选项

```bash
# 调试模式
./configure CFLAGS="-g -O0"
make

# 生产模式
./configure CFLAGS="-O2"
make
```

### 运行测试

```bash
# C 测试程序
cd tests && bash build_test.sh
./test_exosip_udp
./test_exosip_tcp
```

## 参考文档

- [eXosip2 官网](http://savannah.nongnu.org/projects/exosip)
- [RFC 3261 - SIP](https://tools.ietf.org/html/rfc3261)
- [GB/T 28181-2016](https://openstd.samr.gov.cn/)
- [ZLMediaKit](https://github.com/ZLMediaKit/ZLMediaKit) - 流媒体服务器

## 许可证

MIT License

## 贡献

欢迎提交 Issue 和 Pull Request。

## 更新日志

### v2.3.0 (2025-01-25) 🚀

**长期任务支持 (Long Task):**

#### ✅ onWorkerStart 回调
- Worker 进程启动时触发
- 用于初始化资源、启动长期任务

#### ✅ startLongTask() 方法
- 创建专用的长期任务进程
- 允许永久阻塞(Redis::subscribe、Kafka consume 等)
- 不占用普通 Task 进程池
- 通过 sendToWorker() 推送消息给 Worker

**典型应用场景:**
- ✅ Redis PubSub 订阅 (真正的 subscribe,不是 blPop!)
- ✅ Kafka 消费者 (长连接消费)
- ✅ RabbitMQ 消费者 (队列监听)
- ✅ WebSocket 长连接
- ✅ 任何需要永久阻塞的场景

**架构优势:**
```
Worker (SIP 事件) + Task Pool (短期任务) + Long Task (订阅/队列)
         ↕                    ↕                        ↕
   不阻塞,实时         DB/HTTP/API         Redis/Kafka/MQ
                                          (永久阻塞 OK!)
```

**文档和示例:**
- 📖 [Long Task 完整文档](docs/LONG_TASK_SUPPORT.md)
- 🧪 [Redis 订阅示例](examples/test_redis_subscriber.php)
- 📝 [API 文档](docs/exosip.stub.php)

---

### v2.2.0 (2024-11-25) 🎉

**三大核心功能完成:**

#### ✅ Task 1: C 层管道通信
- 实现 Task→Worker 双向通信机制
- 修改 `task_msg_t` 和 `task_result_t` 添加 `type` 字段
- 实现 `sip_task_send_to_worker()` 函数
- 修改 `sip_handle_task_result()` 区分消息类型

#### ✅ Task 2: PHP API 层
- 添加 `onPipeMessage` 回调(Worker 接收 Task 推送)
- 实现 `sendToWorker($data)` 方法(Task 推送给 Worker)
- 完整的属性读写处理器和初始化
- 创建测试示例: `test_pipe_message.php`, `test_laravel_integration.php`

#### ✅ Task 3: TCP 传输模式支持
- DeviceManager 添加双向映射: `device_id ↔ fd`
- 实现 8 个连接管理方法(bind/unbind/get/has)
- GB28181Handler 自动处理 TCP 连接
- SipEvent 添加 `getFd()` 方法
- 创建完整文档: `TCP_MODE_SUPPORT.md`

**架构增强:**
- ✅ Laravel 集成架构: Laravel → Redis → Task → Worker → Device
- ✅ 进程安全文档: `TASK_SERVER_OBJECT_SAFETY.md`
- ✅ 完整测试覆盖: 管道通信、Laravel 集成、TCP 模式

**文档完善:**
- 更新 `exosip.stub.php` 添加所有新 API
- 创建 `docs/README.md` 文档索引
- 整理并归类所有技术文档

### v2.1.0 (2024)
- ✅ **Master-Worker-Task 多进程架构**
  - Master 监控进程（自动恢复 Worker）
  - Worker SIP 事件循环（永不阻塞）
  - Task Pool 异步任务池（HTTP/数据库/Redis）
- ✅ **异步任务处理** - onTask/onTaskFinish 回调
- ✅ **定时器支持** - onTimer 回调（可配置间隔）
- ✅ **进程状态查询** - getProcessStatus/getRunStatus
- ✅ **资源监控** - 内存、FD、运行时长统计
- ✅ **异常保护** - 回调异常自动捕获，不崩溃

### v2.0.0 (2024)
- ✅ 纯 OOP API 重构
- ✅ 事件驱动架构
- ✅ GB28181 完整支持
- ✅ 跨平台优化
- ✅ 生产环境验证

### v1.0.0
- 初始版本

---

**Ready for Production** ✨

**完整功能清单:**
- ✅ Master-Worker-Task 多进程架构
- ✅ Task→Worker 实时管道通信
- ✅ TCP/UDP 双模式连接管理
- ✅ GB28181 国标协议完整支持
- ✅ Laravel 集成架构
- ✅ 异常安全保护

查看完整文档：[docs/README.md](docs/README.md)
