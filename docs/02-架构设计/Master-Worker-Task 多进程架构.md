# Master-Worker-Task 多进程架构

## 架构概述

```
┌─────────────────────────────────────────────┐
│ Master Process (监控进程)                    │
│ - 不处理业务逻辑                              │
│ - 监控 Worker 和 Task 进程                    │
│ - Worker 崩溃时自动重启                       │
└─────────┬───────────────────────────────────┘
          │
    ┌─────┴──────┐
    │            │
┌───▼─────────┐  ┌▼────────────────────────────┐
│ Worker (1个) │  │ Task Pool (可配置数量)      │
│             │  │                              │
│ SIP 事件循环 │  │ ┌──────┐ ┌──────┐ ┌──────┐ │
│ - REGISTER  │  │ │Task-0│ │Task-1│ │Task-2│ │
│ - MESSAGE   │──┼─▶│HTTP  │ │Redis │ │DB    │ │
│ - INVITE    │  │ └──────┘ └──────┘ └──────┘ │
│ - 定时器     │  │                              │
└─────────────┘  └──────────────────────────────┘
```

## 核心特性

### 1. 进程隔离
- Worker 崩溃不影响 Task
- Task 崩溃不影响 Worker
- 每个 Task 独立进程空间

### 2. 自动恢复
- Master 监控 Worker 健康
- Worker 异常退出自动重启
- 保证服务高可用

### 3. 非阻塞通信
- Worker → Task 使用 socketpair
- 双向通信，Task 可返回结果
- 非阻塞写入，不影响 SIP 事件循环

### 4. 异常保护
- onTask、onTaskFinish、onTimer 自动捕获异常
- 异常不会导致进程崩溃
- Worker 自动恢复继续运行

## 配置参数

```php
$sipServer = new ExoSip([
    'ua' => 'GB28181-Server/1.0',
    'ip' => '0.0.0.0',
    'port' => 5060,
    'mode' => 'udp',
    
    // Master-Worker-Task 配置
    'task_worker_num' => 4,      // Task 进程数量，默认 4
    'pid_file' => '/tmp/server.pid',  // PID 文件路径
    
    // 定时器配置
    'timer_interval' => 30000,   // 定时器间隔（毫秒），默认 1000
]);
```

## 回调接口

### onTask - Task 进程执行

在 **Task 进程**中执行，处理耗时操作（HTTP、数据库、Redis 等）

```php
$sipServer->onTask = function($taskId, $data) {
    // $taskId: 任务 ID（自增）
    // $data: Worker 投递的数据（array）
    
    echo "[Task] Processing task #{$taskId}\n";
    
    // 执行业务逻辑
    $type = $data['type'] ?? 'unknown';
    $payload = $data['payload'] ?? [];
    
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
            $db = new PDO(...);
            $stmt = $db->prepare("INSERT INTO catalog ...");
            $stmt->execute($payload);
            return ['success' => true, 'rows' => $stmt->rowCount()];
            
        case 'update_heartbeat':
            // Redis 操作
            $redis = new Redis();
            $redis->connect('127.0.0.1');
            $redis->setex("device:{$payload['device_id']}", 300, time());
            return ['success' => true];
            
        default:
            return ['success' => false, 'error' => 'Unknown task type'];
    }
};
```

**重点**：
- ✅ 返回值会自动传递给 `onTaskFinish`
- ✅ 可以返回任意类型（array、string、int、bool 等）
- ✅ 异常会被自动捕获，不会导致 Task 进程崩溃

### onTaskFinish - 任务完成回调

在 **Worker 进程**中执行，接收 Task 返回的结果

```php
$sipServer->onTaskFinish = function($taskId, $result) {
    // $taskId: 任务 ID
    // $result: onTask 的返回值
    
    echo "[Worker] Task #{$taskId} finished\n";
    
    if (isset($result['success']) && $result['success']) {
        echo "[Worker] Task completed successfully\n";
    } else {
        echo "[Worker] Task failed: " . ($result['error'] ?? 'Unknown error') . "\n";
    }
    
    // 可以根据结果执行后续操作
    // 例如：记录日志、更新状态、通知其他服务等
};
```

**重点**：
- ✅ 自动接收 `onTask` 的返回值
- ✅ **不需要** return true 或其他返回值
- ✅ 异常会被自动捕获

### onTimer - 定时器回调

在 **Worker 进程**中执行，定期触发

```php
$sipServer->onTimer = function() {
    echo "[Worker] Timer tick\n";
    
    // 检查设备超时
    $timeoutDevices = checkDeviceTimeouts();
    
    // 清理过期数据
    cleanupExpiredData();
    
    // 返回 true 继续运行，false 停止定时器
    return true;
};
```

**重点**：
- ✅ 触发间隔由 `timer_interval` 配置（单位：毫秒）
- ✅ 返回 `true` 继续运行，`false` 停止定时器
- ✅ 异常会被自动捕获，不影响定时器继续运行

## 任务投递

### addTask() - 投递任务

在 **Worker 进程**中调用，将任务投递到 Task 进程池

```php
// 在 SIP 事件回调中投递任务
$sipServer->onRegister = function($event) use ($sipServer) {
    $deviceId = extractDeviceId($event->getFromUri());
    
    // 投递 Webhook 任务
    $taskId = $sipServer->addTask([
        'type' => 'webhook',
        'payload' => [
            'url' => 'http://api.example.com/device/register',
            'data' => [
                'device_id' => $deviceId,
                'timestamp' => time(),
            ]
        ]
    ]);
    
    echo "[Worker] Task #{$taskId} posted\n";
    
    // 继续处理 SIP 响应（非阻塞）
    $sipServer->sendResponse($event->getTid(), 200, 'OK');
};
```

**重点**：
- ✅ **只能在 Worker 进程中调用**
- ✅ **参数必须是 array**
- ✅ 返回任务 ID（自增长整数）
- ✅ 非阻塞，立即返回

## 完整示例

```php
<?php
require_once __DIR__ . '/protocol/GB28181Handler.php';

$sipServer = new ExoSip([
    'ua' => 'GB28181-Server/1.0',
    'ip' => '0.0.0.0',
    'port' => 5060,
    'mode' => 'udp',
    'pid_file' => '/tmp/gb28181_server.pid',
    
    // 多进程配置
    'task_worker_num' => 4,
    'timer_interval' => 30000,  // 30 秒
]);

// 创建 GB28181 协议处理器
$gb28181 = new GB28181Handler($sipServer, [
    'server_id' => '34020000002000000001',
    'server_domain' => '3402000000',
    'enable_webhook' => true,
]);

// 绑定事件处理器
$gb28181->bindEvents();

// Task 处理器
$sipServer->onTask = function($taskId, $data) {
    $type = $data['type'] ?? 'unknown';
    $payload = $data['payload'] ?? [];
    
    echo "[Task] Processing #{$taskId}: {$type}\n";
    
    try {
        switch ($type) {
            case 'webhook':
                $response = file_get_contents($payload['url'], false, stream_context_create([
                    'http' => [
                        'method' => 'POST',
                        'header' => 'Content-Type: application/json',
                        'content' => json_encode($payload['data']),
                        'timeout' => 5,
                    ]
                ]));
                return ['success' => true, 'response' => $response];
                
            case 'save_catalog':
                // 保存设备目录到数据库
                $db = new PDO('mysql:host=localhost;dbname=gb28181', 'user', 'pass');
                $stmt = $db->prepare("INSERT INTO device_catalog (device_id, catalog_data) VALUES (?, ?)");
                $stmt->execute([
                    $payload['device_id'],
                    json_encode($payload['devices'])
                ]);
                return ['success' => true, 'rows' => $stmt->rowCount()];
                
            case 'update_heartbeat':
                // 更新设备心跳到 Redis
                $redis = new Redis();
                $redis->connect('127.0.0.1');
                $redis->setex("heartbeat:{$payload['device_id']}", 300, time());
                return ['success' => true];
                
            default:
                return ['success' => false, 'error' => 'Unknown task type'];
        }
    } catch (Exception $e) {
        return ['success' => false, 'error' => $e->getMessage()];
    }
};

// Task 完成回调
$sipServer->onTaskFinish = function($taskId, $result) {
    if ($result['success']) {
        echo "[Worker] Task #{$taskId} completed successfully\n";
    } else {
        echo "[Worker] Task #{$taskId} failed: {$result['error']}\n";
    }
};

// 定时器
$sipServer->onTimer = function() use ($gb28181) {
    echo "[Worker] Checking device timeouts...\n";
    
    $timeoutDevices = $gb28181->processTimeouts();
    
    if (count($timeoutDevices) > 0) {
        echo "[Worker] Found " . count($timeoutDevices) . " timeout devices\n";
    }
    
    return true; // 继续运行
};

// 启动服务
echo "Starting GB28181 Multi-Process Server...\n";
$sipServer->run();

echo "[Master] Server stopped\n";
```

## 进程状态查询

### getProcessStatus() - 内部查询

在运行中的进程内查询状态：

```php
$status = $sipServer->getProcessStatus();

print_r($status);
// [
//   'master' => ['pid' => 12345, 'status' => 'running'],
//   'worker' => ['pid' => 12346, 'status' => 'running', 'uptime' => 120],
//   'tasks' => [
//     ['id' => 0, 'pid' => 12347, 'status' => 'running'],
//     ['id' => 1, 'pid' => 12348, 'status' => 'running'],
//     ...
//   ],
//   'current_process' => 'worker',
//   'tasks_posted' => 1234,
//   'tasks_failed' => 5
// ]
```

### ExoSip::getRunStatus() - 外部查询

从外部脚本查询运行状态：

```php
<?php
// gb28181_server_status.php

$pidFile = '/tmp/gb28181_server.pid';
$status = ExoSip::getRunStatus($pidFile);

echo "Master PID: {$status['master']['pid']}\n";
echo "Worker PID: {$status['worker']['pid']}\n";
echo "Master Memory: " . round($status['master']['memory_rss_kb'] / 1024, 2) . " MB\n";
echo "Worker Memory: " . round($status['worker']['memory_rss_kb'] / 1024, 2) . " MB\n";
echo "Worker FD Count: {$status['worker']['fd_count']}\n";

foreach ($status['tasks'] as $task) {
    echo "Task-{$task['id']}: PID {$task['pid']}, Memory " . 
         round($task['memory_rss_kb'] / 1024, 2) . " MB\n";
}
```

运行：
```bash
$ php gb28181_server_status.php

=============================================
  GB28181 Server Status
=============================================
  PID File: /tmp/gb28181_server.pid

  [Master Process]
    PID:        12345
    Status:     running
    Memory:     45.32 MB
    FD Count:   15

  [Worker Process]
    PID:           12346
    Status:        running
    Memory:        62.18 MB
    FD Count:      128
    Uptime:        0h 5m 23s
    Restart Count: 0

  [Task Worker Pool]
    Total: 4 workers

    Task-0: PID 12347 [✓ running] (38.45 MB)
    Task-1: PID 12348 [✓ running] (35.21 MB)
    Task-2: PID 12349 [✓ running] (36.89 MB)
    Task-3: PID 12350 [✓ running] (37.12 MB)

=============================================
```

## 进程管理

### 启动服务

```bash
$ php gb28181_server.php

=============================================
  GB28181 Multi-Process Video Server
=============================================
  Architecture: Master + Worker + Task
  Server ID: 34020000002000000001
  Domain: 3402000000
  Listening: 0.0.0.0:5060 (udp)
  Task Workers: 4
  Timer Interval: 30s
=============================================

[INFO] 服务器已启动，等待设备接入...
[Master] SIP Server started with 4 Task workers
[Worker] Started PID=12346
[Task-0] Started PID=12347
[Task-1] Started PID=12348
[Task-2] Started PID=12349
[Task-3] Started PID=12350
[Worker] Initializing eXosip and entering SIP event loop (PID=12346)
[Worker] eXosip listening on 0.0.0.0:5060 (udp)
```

### 停止服务

```bash
# 优雅停止
$ kill -TERM $(cat /tmp/gb28181_server.pid)

# 强制停止
$ kill -9 $(cat /tmp/gb28181_server.pid)
```

### 查看进程树

```bash
$ pstree -p $(cat /tmp/gb28181_server.pid)

php(12345)─┬─php(12346)  # Worker
           ├─php(12347)  # Task-0
           ├─php(12348)  # Task-1
           ├─php(12349)  # Task-2
           └─php(12350)  # Task-3
```

### 查看端口绑定

```bash
$ lsof -i :5060

COMMAND  PID  USER   FD   TYPE DEVICE SIZE/OFF NODE NAME
php    12346  user   10u  IPv4  xxxxx      0t0  UDP *:5060
```

**注意**：只有 Worker 进程绑定端口，Master 和 Task 不绑定。

## 最佳实践

### 1. Task 数量配置

```php
// CPU 密集型任务（计算、加密等）
'task_worker_num' => CPU核心数

// I/O 密集型任务（HTTP、数据库、文件读写）
'task_worker_num' => CPU核心数 * 2

// 混合型（推荐）
'task_worker_num' => 4  // 默认值
```

### 2. 定时器间隔

```php
// 心跳检查（GB28181 标准：3 分钟超时）
'timer_interval' => 30000  // 30 秒检查一次

// 频繁检查
'timer_interval' => 5000   // 5 秒

// 低频检查
'timer_interval' => 60000  // 1 分钟
```

### 3. 错误处理

```php
$sipServer->onTask = function($taskId, $data) {
    try {
        // 业务逻辑
        $result = doSomething($data);
        return ['success' => true, 'data' => $result];
        
    } catch (PDOException $e) {
        // 数据库错误
        error_log("[Task #{$taskId}] Database error: " . $e->getMessage());
        return ['success' => false, 'error' => 'Database error'];
        
    } catch (Exception $e) {
        // 其他错误
        error_log("[Task #{$taskId}] Error: " . $e->getMessage());
        return ['success' => false, 'error' => $e->getMessage()];
    }
};
```

### 4. 任务超时控制

```php
$sipServer->onTask = function($taskId, $data) {
    // HTTP 请求超时
    $context = stream_context_create([
        'http' => [
            'timeout' => 5,  // 5 秒超时
        ]
    ]);
    
    $result = @file_get_contents($url, false, $context);
    
    if ($result === false) {
        return ['success' => false, 'error' => 'Request timeout'];
    }
    
    return ['success' => true, 'response' => $result];
};
```

### 5. 资源清理

```php
$sipServer->onTask = function($taskId, $data) {
    $db = null;  // 确保每次都创建新连接
    
    try {
        $db = new PDO(...);
        // 执行查询
        $result = $db->query(...);
        
        return ['success' => true, 'data' => $result->fetchAll()];
        
    } finally {
        $db = null;  // 关闭连接
    }
};
```

## 常见问题

### Q1: Worker 进程崩溃怎么办？

**A**: Master 会自动重启 Worker 进程，无需人工干预。

```
[Master] Worker 12346 exited
[Master] Restarting worker
[Worker] Started PID=12351
```

### Q2: Task 进程可以投递任务吗？

**A**: 不可以，`addTask()` 只能在 Worker 进程中调用。

### Q3: onTask 可以抛出异常吗？

**A**: 可以，异常会被自动捕获，返回错误信息给 `onTaskFinish`。

### Q4: 如何监控服务运行状态？

**A**: 使用 `ExoSip::getRunStatus()` 查询进程状态，或使用 systemd/supervisor 等进程管理工具。

### Q5: 如何调试 Task 进程？

**A**: 在 `onTask` 中使用 `error_log()` 记录日志，日志会输出到 PHP 错误日志文件。

```php
$sipServer->onTask = function($taskId, $data) {
    error_log("[Task-{$taskId}] Received data: " . json_encode($data));
    // ...
};
```

## 性能优化

### 1. 减少序列化开销

```php
// ❌ 投递大量数据
$sipServer->addTask([
    'type' => 'webhook',
    'payload' => $largeArray  // 避免传递大数组
]);

// ✅ 只传递关键信息
$sipServer->addTask([
    'type' => 'webhook',
    'device_id' => $deviceId,  // 只传递 ID，在 Task 中查询
]);
```

### 2. 连接池

```php
// Task 进程启动时初始化连接池
class ConnectionPool {
    private static $redis = null;
    private static $db = null;
    
    public static function getRedis() {
        if (!self::$redis) {
            self::$redis = new Redis();
            self::$redis->connect('127.0.0.1');
        }
        return self::$redis;
    }
    
    public static function getDB() {
        if (!self::$db) {
            self::$db = new PDO(...);
        }
        return self::$db;
    }
}

$sipServer->onTask = function($taskId, $data) {
    $redis = ConnectionPool::getRedis();
    // 使用连接...
};
```

### 3. 批量处理

```php
// 累积任务，批量执行
$sipServer->onTask = function($taskId, $data) {
    static $buffer = [];
    
    $buffer[] = $data;
    
    // 每 10 个任务批量写入
    if (count($buffer) >= 10) {
        $db = new PDO(...);
        $db->beginTransaction();
        foreach ($buffer as $item) {
            // 批量插入
        }
        $db->commit();
        $buffer = [];
    }
    
    return ['success' => true];
};
```

## 总结

Master-Worker-Task 架构的核心优势：

- ✅ **高性能**：SIP 事件循环永不阻塞
- ✅ **高可用**：Worker 崩溃自动恢复
- ✅ **易扩展**：Task 数量可动态配置
- ✅ **易维护**：进程隔离，故障定位简单

适用场景：
- GB28181 视频监控平台（1000+ 设备）
- SIP 网关/代理服务器
- VoIP 呼叫中心
- 任何需要高并发的 SIP 服务
