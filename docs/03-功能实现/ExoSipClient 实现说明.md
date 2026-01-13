# ExoSipClient 实现说明

## 概述

新增 `ExoSipClient` 类，提供完整的 SIP 客户端功能，与 `ExoSip` 服务端类配合，实现完整的 SIP 通信能力。

## 新增文件

### C 代码
- `exosip_wrapper.h` - 新增 `ClientContext` 和 `ClientConfig` 结构体
- `exosip_wrapper.c` - 新增客户端函数实现（约 600 行）
- `php_exosip.c` - 新增 `ExoSipClient` PHP 类绑定（约 370 行）

### PHP 示例
- `examples/test_client.php` - 基础客户端测试
- `examples/gb28181_client.php` - GB28181 设备模拟
- `examples/test_server_client.php` - 服务端+客户端集成测试

### 文档
- `docs/exosip.stub.php` - 新增 `ExoSipClient` 类型定义
- `README.md` - 新增客户端 API 文档
- `QUICKSTART.md` - 新增客户端快速开始
- `PROJECT_STRUCTURE.md` - 更新项目结构

## 核心功能

### 1. 客户端初始化

```php
$client = new ExoSipClient([
    'server_ip' => '127.0.0.1',      // 必填
    'server_port' => 5060,           // 默认 5060
    'username' => 'device001',       // 必填
    'password' => '123456',          // 可选
    'realm' => '3402000000',         // 可选
    'mode' => 'UDP',                 // UDP|TCP
    'local_port' => 0,               // 0 自动分配
    'expires' => 3600,               // 注册过期时间
    'debug' => true                  // 调试模式
]);
```

### 2. 注册管理

```php
// 启动客户端
$client->start();

// 发送注册
$rid = $client->sendRegister();

// 检查注册状态
if ($client->isRegistered()) {
    echo "Registered!";
}

// 注销
$client->sendUnregister();
```

### 3. 发送请求

```php
// 发送 MESSAGE
$tid = $client->sendMessage('sip:server@domain', 'Hello!', 'text/plain');

// 发起 INVITE（呼叫）
$cid = $client->sendInvite('sip:user@domain', $sdp);

// 发送 BYE（挂断）
$client->sendBye($did, $cid);

// 发送 OPTIONS
$tid = $client->sendOptions('sip:server@domain');
```

### 4. 事件处理（非阻塞）

```php
// 获取事件（超时 100ms）
$events = $client->processEvents(100);

foreach ($events as $evt) {
    // 事件类型
    echo "Type: {$evt['type']}\n";
    
    // 响应
    if (isset($evt['status_code'])) {
        echo "Status: {$evt['status_code']} {$evt['reason']}\n";
    }
    
    // 请求
    if (isset($evt['method'])) {
        echo "Method: {$evt['method']}\n";
    }
}
```

### 5. 统计信息

```php
$stats = $client->getStats();
print_r($stats);
// [
//     'registered' => 1,
//     'request_count' => 10,
//     'response_count' => 8,
//     'timeout_count' => 0,
//     'register_time' => 1234567890
// ]
```

## 架构设计

### C 层实现

```
ClientContext 结构体
├── eXosip_t *ctx          - eXosip2 上下文
├── ClientConfig config    - 配置信息
├── pthread_t event_thread - 事件线程
├── pthread_mutex_t lock   - 互斥锁
├── int running            - 运行状态
├── int registered         - 注册状态
└── 统计信息
    ├── request_count
    ├── response_count
    └── timeout_count
```

### 事件线程

```c
client_event_thread()
├── eXosip_event_wait()        - 等待事件
├── eXosip_automatic_action()  - 自动处理
└── 更新状态
    ├── registered
    ├── register_time
    └── 统计计数
```

### PHP 类绑定

```
ExoSipClient PHP 类
├── __construct()      - 初始化
├── start()            - 启动
├── stop()             - 停止
├── sendRegister()     - 注册
├── sendUnregister()   - 注销
├── sendMessage()      - 发送消息
├── sendInvite()       - 发起呼叫
├── sendBye()          - 挂断
├── sendOptions()      - OPTIONS
├── isRegistered()     - 检查状态
├── getStats()         - 统计信息
└── processEvents()    - 处理事件
```

## 应用场景

### 1. GB28181 设备模拟

```php
// 模拟 GB28181 设备
$client = new ExoSipClient([
    'server_ip' => '192.168.1.100',
    'server_port' => 5060,
    'username' => '34020000001320000001',  // 设备 ID
    'realm' => '3402000000',
    'mode' => 'UDP'
]);

$client->start();
$client->sendRegister();

// 发送 Keepalive 心跳
function sendKeepalive($client, $deviceId, $serverId) {
    $xml = "<?xml version=\"1.0\"?>
<Notify>
<CmdType>Keepalive</CmdType>
<DeviceID>{$deviceId}</DeviceID>
<Status>OK</Status>
</Notify>";
    
    $client->sendMessage(
        "sip:{$serverId}@3402000000",
        $xml,
        'Application/MANSCDP+xml'
    );
}
```

### 2. VoIP 客户端

```php
// VoIP 用户注册
$client = new ExoSipClient([
    'server_ip' => 'sip.example.com',
    'username' => 'user001',
    'password' => 'secret',
    'realm' => 'example.com'
]);

$client->start();
$client->sendRegister();

// 发起呼叫
$sdp = generate_sdp(...);  // 生成 SDP
$cid = $client->sendInvite('sip:user002@example.com', $sdp);
```

### 3. 自动化测试

```php
// 在同一进程中测试服务端和客户端
$server = new ExoSip(['port' => 5060]);
$client = new ExoSipClient(['server_ip' => '127.0.0.1']);

$client->start();
$client->sendRegister();

// 交替处理事件
for ($i = 0; $i < 100; $i++) {
    $server->processEvents(50);
    $client->processEvents(50);
    usleep(100000);
}
```

## 平台支持

| 平台 | UDP | TCP | 推荐 |
|------|-----|-----|------|
| **Linux** | ✅ | ✅ | TCP 或 UDP |
| **macOS** | ✅ | ⚠️ | UDP（TCP 受限）|
| **Windows** | ✅ | ❌ | UDP |

> 客户端遵循与服务端相同的平台限制。

## 与 GB28181-Service 对比

| 功能 | GB28181-Service (C++) | php-exosip (PHP) |
|------|----------------------|------------------|
| 设备注册 | `SipDevice::StartSipClient()` | `$client->sendRegister()` |
| 心跳发送 | 内置线程 | PHP 定时器 |
| 消息发送 | `eXosip_message_send_request` | `$client->sendMessage()` |
| 事件处理 | `SipRecvEventThread()` | `$client->processEvents()` |
| 目录响应 | `OnMessageNew()` 解析 | PHP SimpleXML |

**优势**：
- ✅ PHP 层面的灵活性
- ✅ 简化的 API
- ✅ 与服务端一致的编程模型
- ✅ 易于集成到现有 PHP 项目

## 测试建议

### 1. 基础功能测试

```bash
# 启动服务器
php examples/test_event_debug.php

# 启动客户端（另一终端）
php examples/test_client.php
```

### 2. GB28181 测试

```bash
# 启动 GB28181 服务器
php examples/gb28181_server.php

# 启动设备模拟（另一终端）
php examples/gb28181_client.php
```

### 3. 集成测试

```bash
# 单进程测试服务端+客户端
php examples/test_server_client.php
```

## 编译与安装

```bash
# 重新编译（包含客户端代码）
make clean
phpize
./configure
make
sudo make install

# 重新加载扩展
sudo service php8.2-fpm reload
# 或
php -m | grep exosip
```

## 未来扩展

- ⬜ 自动重连机制
- ⬜ SDP 生成和解析辅助函数
- ⬜ 更多事件回调支持（onResponse, onTimeout 等）
- ⬜ 会话管理增强（Dialog 对象）

---

**客户端功能已完成，可投入测试** ✅

