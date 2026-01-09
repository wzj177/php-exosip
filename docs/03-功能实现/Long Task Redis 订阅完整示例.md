# Long Task Redis 订阅完整示例

## 概述

Long Task 是专为**长期运行的阻塞任务**设计的进程，例如：
- Redis 订阅（subscribe/blPop）
- Kafka 消费
- RabbitMQ 监听
- WebSocket 长连接

Long Task 可以通过 `sendToWorker($data)` 将收到的消息转发给 Worker 处理。

## 架构

```
Master Process
  ├─ Worker Process
  │    ├─ SIP 事件循环
  │    ├─ onPipeMessage 接收 Long Task 消息
  │    └─ Long Task Process (fork by Worker)
  │         ├─ Redis blPop (阻塞等待)
  │         ├─ 收到消息 → sendToWorker()
  │         └─ Worker 收到 → onPipeMessage 触发
  └─ Task Processes (N个)
```

## 核心代码

### 1. GB28181Handler.php 中实现

```php
public function handleWorkerStart(ExoSip $server): void
{
    echo " Worker started (PID: " . posix_getpid() . ")\n\n";

    $config = $this->config;
    $debug = $config['debug'] ?? false;
    
    $server->startLongTask(function() use ($server, $config, $debug) {
        echo "[LongTask-Redis] Started (PID: " . getmypid() . ")\n";
        static::runRedisSubscriberStatic($server, $config, $debug);
    });
}

private static function runRedisSubscriberStatic(ExoSip $server, array $config, bool $debug): void
{
    pcntl_async_signals(true);
    $shouldExit = false;
    
    pcntl_signal(SIGTERM, function() use (&$shouldExit) {
        $shouldExit = true;
    });
    
    $redis = null;
    $lastHeartbeat = 0;
    $heartbeatInterval = 20;
    
    while (!$shouldExit) {
        try {
            // 连接 Redis
            if (!$redis) {
                $redis = new \Redis();
                if (!$redis->pconnect($config['redis']['host'], $config['redis']['port'])) {
                    sleep(1);
                    continue;
                }
                echo "[LongTask] Redis connected\n";
            }
            
            // 心跳检查
            if (time() - $lastHeartbeat >= $heartbeatInterval) {
                $redis->ping();
                $lastHeartbeat = time();
            }
            
            // blPop（1秒超时，可响应信号）
            $result = $redis->blPop(['gb28181:commands'], 1);
            
            if ($result && is_array($result)) {
                $message = $result[1] ?? '';
                if ($message) {
                    $cmd = @json_decode($message, true);
                    if ($cmd) {
                        // ✅ 发送给 Worker
                        if ($server->sendToWorker($cmd)) {
                            if ($debug) echo "[LongTask] Forwarded command\n";
                        }
                    }
                }
            }
            
            pcntl_signal_dispatch();
            
        } catch (Exception $e) {
            echo "[LongTask] Error: {$e->getMessage()}\n";
            $redis = null;
            sleep(1);
        }
    }
}
```

### 2. Worker 接收消息

```php
public function handleOnPipeMessage(ExoSip $server, array $message): void
{
    $type = $message['type'] ?? 'unknown';
    
    switch ($type) {
        case 'ptz_command':
            $this->handlePtzCommand($server, $message);
            break;
            
        case 'playback':
            $this->handlePlayback($server, $message);
            break;
            
        default:
            echo "[Worker] Unknown command type: {$type}\n";
    }
}

private function handlePtzCommand(ExoSip $server, array $cmd): void
{
    $deviceId = $cmd['device_id'];
    $action = $cmd['action'];
    
    // 构造 SIP MESSAGE 发送给设备
    $xml = $this->buildPtzXml($action, $cmd['speed'] ?? 50);
    
    $server->sendMessage([
        'to' => "sip:{$deviceId}@{$this->realm}",
        'content_type' => 'Application/MANSCDP+xml',
        'body' => $xml
    ]);
    
    echo "[Worker] PTZ command sent to device {$deviceId}\n";
}
```

## 完整测试示例

见 `examples/test_redis_longtask.php`

## 测试步骤

### 1. 启动服务器

```bash
cd examples
php gb28181_server.php
```

### 2. 发送 Redis 命令

```bash
# 在另一个终端
redis-cli
> lpush gb28181:commands '{"type":"ptz_command","device_id":"34020000001110000001","action":"up","speed":50}'
```

### 3. 查看日志

```
[LongTask] Received command: ptz_command
[LongTask] Forwarded command to Worker
[Worker] ✅ Received from Long Task!
[Worker] PTZ command sent to device 34020000001110000001
```

## 关键点

### ✅ DO

1. **使用 blPop 而不是 subscribe**
   - `blPop` 有超时机制，可以响应 SIGTERM
   - `subscribe` 完全阻塞，无法优雅退出

2. **使用静态方法**
   - 避免依赖 `$this`（fork 后对象状态不确定）
   - 通过 `use ($server, $config)` 传递必要变量

3. **设置信号处理器**
   - `pcntl_async_signals(true)`
   - `pcntl_signal(SIGTERM, ...)`
   - 定期调用 `pcntl_signal_dispatch()`

4. **心跳机制**
   - 定期 `$redis->ping()` 检查连接
   - 连接失败时重连

### ❌ DON'T

1. **不要在 Long Task 中 fork 子进程**
   - 会导致进程树混乱
   - 心跳用定时器，不要用 fork

2. **不要使用 subscribe**
   - 无法响应 SIGTERM
   - 无法优雅退出

3. **不要依赖 $this**
   - fork 后对象状态不确定
   - 使用静态方法或闭包变量

## 性能说明

- **Long Task → Worker 延迟**: < 1ms
- **内存开销**: 每个 Long Task ~5MB
- **适用场景**: 需要持续监听外部消息源

## 故障处理

### Long Task 无法发送消息

检查：
1. `long_task_worker_num` 是否配置
2. `startLongTask` 是否在 `onWorkerStart` 中调用
3. 是否正确使用 `sendToWorker($data)`

### Worker 收不到消息

检查：
1. `onPipeMessage` 回调是否设置
2. 回调签名是否正确：`function($data)` 或 `function($server, $data)`
3. Long Task 是否真的发送了消息（查看日志）

## 对比其他方案

| 方案 | 优点 | 缺点 |
|------|------|------|
| Long Task | 真正的长连接<br/>Worker 可直接处理<br/>低延迟 | 进程数增加 |
| 定时器轮询 | 简单 | 高延迟<br/>资源浪费 |
| 外部进程 | 独立运行 | 进程间通信复杂<br/>需要额外管理 |

## 完整流程图

```
Redis
  │
  │ lpush gb28181:commands
  ▼
Long Task Process
  │ blPop (阻塞等待)
  │ 收到消息
  │ json_decode
  ▼
  $server->sendToWorker($cmd)
  │ (通过 socketpair)
  ▼
Worker Process
  │ 事件循环检测到消息
  │ onPipeMessage 触发
  ▼
  handlePtzCommand()
  │ 构造 SIP MESSAGE
  ▼
  $server->sendMessage($sip_msg)
  │ (发送给设备)
  ▼
GB28181 设备
  │ 收到PTZ控制命令
  │ 执行云台动作
```

## 扩展应用

### Kafka 消费

```php
$server->startLongTask(function() use ($server, $config) {
    $consumer = new \RdKafka\Consumer();
    $consumer->subscribe(['gb28181-commands']);
    
    while (true) {
        $message = $consumer->consume(120*1000);
        if ($message->err) continue;
        
        $cmd = json_decode($message->payload, true);
        $server->sendToWorker($cmd);
    }
});
```

### RabbitMQ 监听

```php
$server->startLongTask(function() use ($server, $config) {
    $connection = new AMQPConnection($config['rabbitmq']);
    $channel = $connection->channel();
    $channel->queue_declare('gb28181_commands');
    
    $callback = function($msg) use ($server) {
        $cmd = json_decode($msg->body, true);
        $server->sendToWorker($cmd);
        $msg->ack();
    };
    
    $channel->basic_consume('gb28181_commands', '', false, false, false, false, $callback);
    
    while ($channel->is_consuming()) {
        $channel->wait();
    }
});
```

## 总结

Long Task 提供了一种优雅的方式来处理长期阻塞的外部消息监听任务，与 Worker 的 SIP 事件处理无缝集成。

