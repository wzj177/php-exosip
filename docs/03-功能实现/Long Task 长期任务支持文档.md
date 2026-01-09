# Long Task 长期任务支持文档

## 概述

`startLongTask()` 是 php-exosip v2.3.0 新增的功能，用于启动允许永久阻塞的长期任务。

### 设计背景

在多进程架构中：
- **Worker 进程**: 处理 SIP 事件,不应阻塞
- **普通 Task 进程**: 处理短期异步任务(DB/HTTP),不应长期阻塞
- **长期 Task 进程**: 专门用于长期阻塞的任务(Redis订阅/消息队列等)

### 核心特性

| 特性 | 说明 |
|------|------|
| **永久阻塞** | 可以运行 `Redis::subscribe()` 等永久阻塞的操作 |
| **独立进程** | 不占用普通 Task 进程池 |
| **双向通信** | 通过 `sendToWorker()` 推送消息给 Worker |
| **Worker接收** | Worker 通过 `onPipeMessage` 接收推送 |
| **启动时机** | 必须在 `onWorkerStart` 中调用 |

---

## API 说明

### onWorkerStart 回调

Worker 进程启动时触发,用于初始化资源。

```php
public $onWorkerStart;  // callable(ExoSip $server): void
```

**参数**:
- `$server`: ExoSip 实例

**示例**:
```php
$sip->onWorkerStart = function($server) {
    echo "Worker started, PID: " . posix_getpid() . "\n";
    
    // 在这里启动长期任务
    $server->startLongTask(...);
};
```

---

### startLongTask() 方法

启动长期任务进程。

```php
public function startLongTask(callable $callback): bool
```

**参数**:
- `$callback`: 任务函数,接收 `$server` 参数

**返回值**:
- `true`: 成功启动
- `false`: 启动失败

**限制**:
- 只能在 `onWorkerStart` 中调用
- 每个 Worker 只能启动一个长期任务

**异常**:
- 在非 `onWorkerStart` 上下文调用会抛出异常

---

## 使用场景

### 场景 1: Redis 订阅

```php
$sip->onWorkerStart = function($server) {
    $server->startLongTask(function($server) {
        $redis = new Redis();
        $redis->pconnect('127.0.0.1', 6379);
        
        // subscribe() 会永久阻塞 - 没问题!
        $redis->subscribe(['gb28181:commands'], function($redis, $channel, $message) use ($server) {
            $data = json_decode($message, true);
            
            // 推送给 Worker 处理
            $server->sendToWorker([
                'type' => 'redis_command',
                'data' => $data
            ]);
        });
    });
};

// Worker 接收推送
$sip->onPipeMessage = function($server, $message) {
    if ($message['type'] === 'redis_command') {
        $data = $message['data'];
        
        switch ($data['type']) {
            case 'ptz_control':
                // 发送 PTZ 控制命令
                sendPtzCommand($data['device_id'], $data['command']);
                break;
                
            case 'query_catalog':
                // 查询设备目录
                queryCatalog($data['device_id']);
                break;
        }
    }
};
```

---

### 场景 2: Kafka 消费者

```php
$sip->onWorkerStart = function($server) {
    $server->startLongTask(function($server) {
        $conf = new RdKafka\Conf();
        $conf->set('group.id', 'gb28181-gateway');
        $conf->set('bootstrap.servers', 'localhost:9092');
        
        $consumer = new RdKafka\KafkaConsumer($conf);
        $consumer->subscribe(['device-events', 'alarm-events']);
        
        while (true) {
            $message = $consumer->consume(120 * 1000); // 阻塞 120 秒
            
            if ($message->err === RD_KAFKA_RESP_ERR_NO_ERROR) {
                $server->sendToWorker([
                    'type' => 'kafka_message',
                    'topic' => $message->topic_name,
                    'data' => json_decode($message->payload, true)
                ]);
            }
        }
    });
};
```

---

### 场景 3: RabbitMQ 消费者

```php
$sip->onWorkerStart = function($server) {
    $server->startLongTask(function($server) {
        $connection = new AMQPStreamConnection('localhost', 5672, 'guest', 'guest');
        $channel = $connection->channel();
        $channel->queue_declare('gb28181_commands', false, true, false, false);
        
        $callback = function($msg) use ($server) {
            $data = json_decode($msg->body, true);
            
            $server->sendToWorker([
                'type' => 'rabbitmq_message',
                'data' => $data
            ]);
            
            $msg->ack();
        };
        
        $channel->basic_consume('gb28181_commands', '', false, false, false, false, $callback);
        
        // 永久阻塞等待消息
        while ($channel->is_consuming()) {
            $channel->wait();
        }
    });
};
```

---

## 完整示例

### Laravel + Redis 集成

```php
<?php
require_once __DIR__ . '/DeviceManager.php';
require_once __DIR__ . '/protocol/GB28181Handler.php';

class GB28181Gateway
{
    private $sipServer;
    private $handler;
    private $deviceManager;
    
    public function start()
    {
        $this->sipServer = new ExoSip();
        $this->deviceManager = new DeviceManager();
        $this->handler = new GB28181Handler($this->sipServer, $this->deviceManager);
        
        $this->sipServer->init([
            'server_id' => '34020000002000000001',
            'server_domain' => '3402000000',
            'ip' => '0.0.0.0',
            'port' => 5060,
            'worker_num' => 1,
            'task_worker_num' => 4
        ]);
        
        // ✅ Worker 启动回调
        $this->sipServer->onWorkerStart = function($server) {
            echo "Worker started\n";
            $this->startRedisSubscriber($server);
        };
        
        // ✅ 接收长期 Task 推送
        $this->sipServer->onPipeMessage = function($server, $message) {
            $this->handlePipeMessage($server, $message);
        };
        
        // 绑定 SIP 事件
        $this->handler->bindEvents();
        
        $this->sipServer->run();
    }
    
    private function startRedisSubscriber($server)
    {
        $server->startLongTask(function($server) {
            echo "[LongTask] Starting Redis subscriber...\n";
            
            $redis = new Redis();
            $redis->pconnect('127.0.0.1', 6379);
            
            $redis->subscribe(['gb28181:commands'], function($redis, $channel, $message) use ($server) {
                echo "[LongTask] Received: {$message}\n";
                
                $data = json_decode($message, true);
                
                $server->sendToWorker([
                    'type' => 'redis_command',
                    'data' => $data
                ]);
            });
        });
    }
    
    private function handlePipeMessage($server, $message)
    {
        if ($message['type'] !== 'redis_command') {
            return;
        }
        
        $data = $message['data'];
        $type = $data['type'] ?? '';
        $deviceId = $data['device_id'] ?? '';
        
        echo "[Worker] Handling: {$type} for {$deviceId}\n";
        
        switch ($type) {
            case 'ptz_control':
                // SIP 操作直接在 Worker 执行
                $this->handler->sendPtzCommand($deviceId, $data['command']);
                break;
                
            case 'query_catalog':
                $this->handler->queryCatalog($deviceId);
                break;
                
            case 'start_stream':
                $this->handler->startStream($deviceId, $data);
                break;
                
            case 'query_recordings':
                // DB 操作分发到普通 Task
                $server->task([
                    'type' => 'db_query_recordings',
                    'device_id' => $deviceId,
                    'start_time' => $data['start_time'],
                    'end_time' => $data['end_time']
                ]);
                break;
        }
    }
}

$gateway = new GB28181Gateway();
$gateway->start();
```

### Laravel 端推送命令

```php
// Laravel Controller
use Illuminate\Support\Facades\Redis;

class DeviceController extends Controller
{
    public function ptzControl(Request $request)
    {
        $command = [
            'type' => 'ptz_control',
            'device_id' => $request->device_id,
            'command' => $request->command,
            'speed' => $request->speed ?? 5
        ];
        
        // 推送到 Redis
        Redis::publish('gb28181:commands', json_encode($command));
        
        return response()->json(['status' => 'sent']);
    }
    
    public function startStream(Request $request)
    {
        $command = [
            'type' => 'start_stream',
            'device_id' => $request->device_id,
            'channel_id' => $request->channel_id,
            'ssrc' => generateSSRC()
        ];
        
        Redis::publish('gb28181:commands', json_encode($command));
        
        return response()->json(['status' => 'sent']);
    }
}
```

---

## 架构图

```
┌─────────────────────────────────────────────────────────────┐
│                         Master Process                       │
└────┬──────────────────────────────────────────────┬─────────┘
     │                                               │
     ▼                                               ▼
┌─────────────────┐                      ┌──────────────────────┐
│  Worker Process │                      │   Task Process Pool  │
│                 │                      │  ┌────────────────┐  │
│  ┌───────────┐  │                      │  │  Task 1 (DB)   │  │
│  │SIP Events │  │◄─────────────────────┼──│  Task 2 (HTTP) │  │
│  │  Loop     │  │  addTask()           │  │  Task 3 (API)  │  │
│  └───────────┘  │                      │  │  Task 4 (...)  │  │
│                 │                      │  └────────────────┘  │
│  ┌───────────┐  │                      └──────────────────────┘
│  │onPipe     │  │◄────────┐
│  │Message    │  │         │ sendToWorker()
│  └───────────┘  │         │
└─────────────────┘         │
     │                      │
     │ startLongTask()      │
     ▼                      │
┌─────────────────────────────────┐
│      Long Task Process          │
│  ┌───────────────────────────┐  │
│  │  Redis::subscribe()       │──┘
│  │  (永久阻塞 - OK!)         │
│  └───────────────────────────┘  │
│                                 │
│  - 不占用普通 Task 池           │
│  - 可以永久阻塞                 │
│  - 推送消息给 Worker            │
└─────────────────────────────────┘
```

---

## 时间线示例

```
时刻  Worker进程              长期Task进程           普通Task进程
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
0ms   run() 启动              
      ↓
10ms  onWorkerStart 触发     
      ↓
15ms  startLongTask()  ──────→ fork() 创建进程
                               ↓
20ms  继续 SIP 事件循环        Redis::subscribe()
      ↓                       (永久阻塞等待...)
30ms  处理 SIP REGISTER       
50ms  处理 SIP MESSAGE         
...                           
                              
1000ms 处理 SIP INVITE         收到 Redis 消息!
                               ↓
                               sendToWorker()  ──────┐
1010ms onPipeMessage 触发                            │
      ↓                                              │
      处理 PTZ 命令                                  │
      ↓                                              │
1015ms 发送 SIP MESSAGE                              │
                                                     │
1020ms                         继续等待消息...        │
                                                     │
1050ms                                         处理普通Task
                                               (DB查询)
```

---

## 注意事项

### ✅ 正确用法

```php
// ✅ 在 onWorkerStart 中启动
$sip->onWorkerStart = function($server) {
    $server->startLongTask(function($server) {
        // 可以永久阻塞
        $redis->subscribe(...);
    });
};
```

### ❌ 错误用法

```php
// ❌ 不要在 run() 之前调用
$sip->init([...]);
$sip->startLongTask(...);  // 错误! Worker 还没启动
$sip->run();

// ❌ 不要在普通 Task 中启动
$sip->onTask = function($server, $taskId, $data) {
    $server->startLongTask(...);  // 错误! 不支持
};

// ❌ 不要在事件回调中启动
$sip->onRegister = function($event) use ($sip) {
    $sip->startLongTask(...);  // 错误! 不支持
};
```

### 资源管理

```php
$sip->onWorkerStart = function($server) {
    $server->startLongTask(function($server) {
        // ✅ 使用持久连接
        $redis = new Redis();
        $redis->pconnect('127.0.0.1', 6379);
        
        // ✅ 添加错误处理
        try {
            $redis->subscribe(['channel'], function($redis, $channel, $msg) use ($server) {
                try {
                    $server->sendToWorker(['data' => $msg]);
                } catch (Exception $e) {
                    error_log("sendToWorker failed: {$e->getMessage()}");
                }
            });
        } catch (Exception $e) {
            error_log("Redis subscribe failed: {$e->getMessage()}");
        }
    });
};
```

---

## 调试技巧

### 查看进程树

```bash
# 查看进程关系
pstree -p $(pidof php)

# 输出示例:
# php(12345)─┬─php(12346) Worker
#            ├─php(12347) Task 1
#            ├─php(12348) Task 2
#            ├─php(12349) Task 3
#            ├─php(12350) Task 4
#            └─php(12351) Long Task (Redis subscriber)
```

### 添加日志

```php
$sip->onWorkerStart = function($server) {
    error_log("[Worker] PID: " . posix_getpid());
    
    $server->startLongTask(function($server) {
        error_log("[LongTask] PID: " . posix_getpid());
        error_log("[LongTask] Parent PID: " . posix_getppid());
        
        // 订阅逻辑...
    });
};
```

### 监控推送

```php
$sip->onPipeMessage = function($server, $message) {
    $time = date('Y-m-d H:i:s');
    error_log("[{$time}] Received pipe message: " . json_encode($message));
    
    // 处理消息...
};
```

---

## FAQ

### Q1: 可以启动多个长期 Task 吗?

**A**: 目前每个 Worker 只支持一个长期 Task。如果需要订阅多个源,可以在一个 Task 中处理:

```php
$server->startLongTask(function($server) {
    // 方案1: 使用 pcntl_fork 在 Long Task 内部再 fork
    $pid1 = pcntl_fork();
    if ($pid1 == 0) {
        // 子进程: Redis 订阅
        $redis->subscribe(...);
        exit(0);
    }
    
    $pid2 = pcntl_fork();
    if ($pid2 == 0) {
        // 子进程: Kafka 消费
        $consumer->consume(...);
        exit(0);
    }
    
    // 父进程: 等待子进程
    pcntl_wait($status);
});
```

### Q2: Long Task 进程崩溃怎么办?

**A**: 目前不会自动重启。建议添加外部监控:

```php
$sip->onWorkerStart = function($server) {
    $server->startLongTask(function($server) {
        while (true) {
            try {
                $redis->subscribe(...);
            } catch (Exception $e) {
                error_log("Subscribe failed: {$e->getMessage()}");
                sleep(5);  // 重连延迟
                // 继续循环,重新订阅
            }
        }
    });
};
```

### Q3: 性能如何?

**A**: 
- **内存**: 每个长期 Task ~10-20MB (取决于 PHP 版本)
- **CPU**: 阻塞时几乎不占用
- **通信延迟**: sendToWorker() < 1ms

### Q4: 与 Swoole Process 有什么区别?

**A**:

| 特性 | startLongTask | Swoole Process |
|------|---------------|----------------|
| 独立进程 | ✅ | ✅ |
| 永久阻塞 | ✅ | ✅ |
| 推送消息 | sendToWorker() | write() |
| 进程管理 | 自动 | 手动 |
| 重启机制 | 无 | 可配置 |

---

## 更新日志

### v2.3.0 (2025-01-25)
- ✨ 新增 `onWorkerStart` 回调
- ✨ 新增 `startLongTask()` 方法
- 📝 完善文档和示例

---

## 相关文档

- [Master-Worker-Task 架构](MASTER_WORKER_TASK_IMPLEMENTATION.md)
- [Task→Worker 通信](MASTER_WORKER_TASK.md)
- [sendToWorker() API](../docs/exosip.stub.php)
- [完整示例](../examples/test_redis_subscriber.php)
