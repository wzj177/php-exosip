# Task 进程中 $server 对象的安全性分析

## 问题

在 `onTask` 回调中,`$server` 参数是外层的 `$sipServer` 对象吗? 传递它是否安全?

```php
$sipServer = new ExoSip([...]);

$sipServer->onTask(function($server, $taskId, $data) {
    // ← 这个 $server 是什么? 能做什么? 有什么限制?
});
```

## 简短回答

**是同一个对象,但在不同进程中,因此:**

✅ **读操作安全** - 只读访问配置、状态  
⚠️ **写操作隔离** - 修改只影响 Task 进程自己  
❌ **SIP 操作禁止** - 不能调用 sendMessage/sendResponse 等

## 详细解析

### 1. fork() 内存模型

#### 进程创建流程

```c
// exosip_wrapper.c: sip_start_master_process()

// Step 1: Master 进程创建 Worker
pid_t worker_pid = fork();
if (worker_pid == 0) {
    // 这里是 Worker 进程
    ctx->is_worker = 1;
    // Worker 拥有 $sipServer 的完整副本
}

// Step 2: Master 进程创建 Task
pid_t task_pid = fork();
if (task_pid == 0) {
    // 这里是 Task 进程
    ctx->is_task = 1;
    // Task 也拥有 $sipServer 的完整副本
}
```

#### 内存布局

```
fork() 发生时刻:
┌──────────────────────────────────────────────────┐
│             Parent Process (Master)              │
│                                                  │
│  PHP 堆内存:                                      │
│  ┌────────────────────────────────────────────┐ │
│  │ $sipServer (ExoSip object)                 │ │
│  │  ├─ zend_object header                     │ │
│  │  ├─ ctx (SipContext*)    ──────┐           │ │
│  │  ├─ config (HashTable*)         │          │ │
│  │  ├─ onTask (zval)               │          │ │
│  │  └─ onTaskFinish (zval)         │          │ │
│  └────────────────────────────────┼───────────┘ │
│                                    │             │
│  C 堆内存:                          │             │
│  ┌────────────────────────────────▼───────────┐ │
│  │ SipContext                                 │ │
│  │  ├─ ctx (eXosip_t*)                        │ │
│  │  ├─ is_master = 1                          │ │
│  │  ├─ is_worker = 0                          │ │
│  │  ├─ is_task = 0                            │ │
│  │  ├─ task_callback (zval)                   │ │
│  │  └─ pipe_message_callback (zval)           │ │
│  └────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────┘

fork() 之后 (Copy-on-Write):
┌──────────────────────────┐  ┌──────────────────────────┐
│    Worker Process        │  │    Task Process          │
│                          │  │                          │
│  $sipServer (副本) ✓     │  │  $sipServer (副本) ✓     │
│  SipContext (副本) ✓     │  │  SipContext (副本) ✓     │
│                          │  │                          │
│  is_worker = 1           │  │  is_task = 1             │
│  eXosip_t* 已初始化 ✓    │  │  eXosip_t* = NULL ✗      │
│                          │  │                          │
│  处理 SIP 事件 ✓         │  │  处理阻塞任务 ✓          │
│  socketpair 读取 Task ✓  │  │  socketpair 发送结果 ✓   │
└──────────────────────────┘  └──────────────────────────┘
```

### 2. 关键差异

#### Worker 进程的 $server

```php
// Worker 中的 $server 可以:
✅ $server->getConfig()           // 读取配置
✅ $server->sendMessage()         // 发送 SIP 消息
✅ $server->sendResponse()        // 发送 SIP 响应
✅ $server->addTask()             // 分发任务给 Task
✅ $server->getProcessStatus()    // 获取进程信息
```

#### Task 进程的 $server

```php
// Task 中的 $server 只能:
✅ $server->getConfig()           // 读取配置 (fork 时的副本)
✅ $server->getProcessStatus()    // is_task = true
✅ $server->sendToWorker()        // 发送管道消息给 Worker

⚠️ $server->setConfig()           // 只影响 Task 进程自己
⚠️ 修改 $server 状态               // 其他进程看不到

❌ $server->sendMessage()         // 失败: Task 无 eXosip 上下文
❌ $server->sendResponse()        // 失败: 同上
❌ $server->addTask()             // 失败: Task 不能分发任务
❌ $server->run()                 // 卡死: Task 不处理事件循环
```

### 3. 为什么底层传递对象是安全的?

#### PHP 对象传递机制

```c
// php_exosip.c: 调用 Task 回调

// 准备参数
zval args[3];
ZVAL_OBJ(&args[0], &obj->std);  // ← 传递 ExoSip 对象
ZVAL_LONG(&args[1], task_id);
// ... unserialize task_data to args[2]

// 调用 PHP 回调
zend_fcall_info fci;
fci.param_count = 3;
fci.params = args;
zend_call_function(&fci, &fcc);
```

**关键点:**
1. **传递的是对象引用** (zend_object*),不是深拷贝
2. **在 Task 进程中**,这个引用指向 Task 进程的内存空间
3. **写操作仅修改 Task 的副本**,不影响 Worker

#### 内存隔离验证

```php
// Worker 进程
$counter = 0;

$sipServer->onTask(function($server, $taskId, $data) use (&$counter) {
    // Task 进程
    $counter = 999;  // ← 只修改 Task 进程的 $counter
    echo "Task counter: {$counter}\n";  // 输出 999
});

$sipServer->onTaskFinish(function($server, $taskId, $result) use (&$counter) {
    // Worker 进程
    echo "Worker counter: {$counter}\n";  // 仍然是 0!
});
```

### 4. C 层的实现保证

#### Task 进程的 ctx 状态

```c
// exosip_wrapper.c: sip_task_loop()

void sip_task_loop(SipContext *ctx, int sockfd) {
    // Task 进程的标识
    ctx->is_task = 1;
    ctx->is_worker = 0;
    ctx->is_master = 0;
    
    // ❌ Task 没有 eXosip 上下文
    ctx->ctx = NULL;  // eXosip_t* 为 NULL
    
    // ✓ Task 有 socketpair 用于通信
    ctx->task_sockfd = sockfd;
    
    while (!g_shutdown_flag) {
        // 读取任务
        task_msg_t msg;
        ssize_t n = read(sockfd, &msg, sizeof(msg));
        
        if (n > 0) {
            // 调用 PHP 回调
            call_php_task_callback(ctx, msg.task_id, msg.data);
        }
    }
}
```

#### Worker 进程的 ctx 状态

```c
// exosip_wrapper.c: Worker 事件循环

// Worker 有完整的 eXosip 上下文
ctx->is_worker = 1;
ctx->ctx = eXosip_malloc();  // ✓ 已初始化
eXosip_init(ctx->ctx);
eXosip_listen_addr(ctx->ctx, ...);

// Worker 处理 SIP 事件
while (ctx->running) {
    eXosip_event_t *evt = eXosip_event_wait(ctx->ctx, 0, 100);
    if (evt) {
        // 调用 PHP 回调 (onRegister, onMessage, ...)
    }
}
```

### 5. 安全操作指南

#### ✅ Task 中推荐的操作

```php
$sipServer->onTask(function($server, $taskId, $data) {
    // 1. 只读配置
    $config = $server->getConfig();
    $ip = $config['ip'];
    
    // 2. 检查进程类型
    $status = $server->getProcessStatus();
    if ($status['is_task']) {
        // 确认在 Task 进程中
    }
    
    // 3. 执行阻塞 I/O
    $result = querySomeDatabaseOrAPI();
    
    // 4. 发送结果回 Worker
    $server->sendToWorker(['type' => 'result', 'data' => $result]);
    
    // 5. 返回任务结果
    return ['success' => true, 'data' => $result];
});
```

#### ⚠️ Task 中有限制的操作

```php
$sipServer->onTask(function($server, $taskId, $data) {
    // setConfig() 只影响 Task 进程
    $server->setConfig(['custom' => 'value']);
    // Worker 看不到这个修改!
    
    // 修改外部变量也是隔离的
    global $someVariable;
    $someVariable = 'changed';
    // Worker 中的 $someVariable 不变!
});
```

#### ❌ Task 中禁止的操作

```php
$sipServer->onTask(function($server, $taskId, $data) {
    // ❌ 会失败: Task 无 eXosip 上下文
    $server->sendMessage($deviceId, $body);
    
    // ❌ 会失败: Task 无 eXosip 上下文
    $server->sendResponse($tid, 200, 'OK');
    
    // ❌ 会失败: Task 不能分发任务
    $server->addTask(['action' => 'subtask']);
    
    // ❌ 会卡死: Task 不应进入事件循环
    $server->run();
    
    // ❌ 无意义: Task 无 eXosip fd
    $server->getFd();
});
```

### 6. 实际应用场景

#### 场景 1: 查询数据库

```php
$sipServer->onTask(function($server, $taskId, $data) {
    // ✅ 安全: 阻塞的数据库查询
    $pdo = new PDO('mysql:host=localhost;dbname=gb28181');
    $stmt = $pdo->prepare('SELECT * FROM devices WHERE id = ?');
    $stmt->execute([$data['device_id']]);
    $device = $stmt->fetch();
    
    // ✅ 通过管道推送结果
    $server->sendToWorker([
        'type' => 'device_info',
        'device' => $device
    ]);
    
    return ['success' => true];
});
```

#### 场景 2: 调用 HTTP API

```php
$sipServer->onTask(function($server, $taskId, $data) {
    // ✅ 安全: 阻塞的 HTTP 请求
    $ch = curl_init();
    curl_setopt($ch, CURLOPT_URL, 'https://api.example.com/devices');
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    $response = curl_exec($ch);
    curl_close($ch);
    
    $result = json_decode($response, true);
    
    // ✅ 推送给 Worker
    $server->sendToWorker([
        'type' => 'api_result',
        'data' => $result
    ]);
    
    return ['success' => true];
});
```

#### 场景 3: Redis 订阅 (错误示范)

```php
// ❌ 错误: 在 Task 中订阅 Redis (会阻塞整个 Task)
$sipServer->onTask(function($server, $taskId, $data) {
    $redis = new Redis();
    $redis->connect('127.0.0.1', 6379);
    
    // ❌ subscribe() 会无限阻塞,导致 Task 无法处理其他任务
    $redis->subscribe(['channel'], function($redis, $channel, $message) {
        // 永远不会返回...
    });
});

// ✅ 正确: 使用定时轮询
$sipServer->onTask(function($server, $taskId, $data) {
    if ($data['action'] === 'poll_redis') {
        $redis = new Redis();
        $redis->connect('127.0.0.1', 6379);
        
        // 非阻塞读取
        $message = $redis->brpop(['queue'], 1);  // 1秒超时
        
        if ($message) {
            $server->sendToWorker([
                'type' => 'redis_message',
                'data' => $message
            ]);
        }
        
        return ['success' => true];
    }
});

// Worker 定期触发轮询
$sipServer->onTimer(function() use ($sipServer) {
    $sipServer->addTask(['action' => 'poll_redis']);
});
```

## 总结

### 问题回答

> **Q1: $server 就是外层的 sipServer 吗?**

A: 是同一个对象,但在不同进程的内存空间中,是独立的副本。

> **Q2: 传递它不会有问题吧?**

A: 传递本身是安全的,但要注意:
- ✅ 只读操作完全安全
- ⚠️ 写操作只影响当前进程
- ❌ SIP 相关操作会失败

> **Q3: 底层应该只是借用这个对象的内存地址吧?**

A: 不完全准确。底层传递的是对象引用,但由于 fork() 的 Copy-on-Write 机制,每个进程有独立的内存空间。PHP 的引用计数在进程间是独立的。

> **Q4: 应该是不能改变和入侵 sipServer 的?**

A: 正确! Task 进程对 `$server` 的任何修改都**不会影响** Worker 进程的 `$sipServer`。这是进程隔离的保证。

### 核心原则

**Task 进程中使用 $server 的黄金法则:**

1. **只读为主** - 读取配置、状态信息
2. **sendToWorker** - 主动推送结果给 Worker
3. **return 结果** - 被动返回给 onTaskFinish
4. **避免 SIP 操作** - 不调用 sendMessage/sendResponse
5. **理解隔离性** - 写操作不影响其他进程

### 类比理解

把 Task 进程想象成**外包团队**:

```
┌─────────────────────────────────────────────────┐
│              Worker (正式员工)                   │
│  - 拥有公司资源 (eXosip 上下文)                  │
│  - 处理核心业务 (SIP 协议)                       │
│  - 分配任务给外包 (addTask)                     │
└────────────┬────────────────────────────────────┘
             │ 通过邮件沟通 (socketpair)
             ↓
┌─────────────────────────────────────────────────┐
│              Task (外包团队)                     │
│  - 拿到任务副本 ($server 副本)                   │
│  - 只能读公司信息 (getConfig)                    │
│  - 不能代表公司对外 (sendMessage ✗)              │
│  - 完成后提交报告 (return + sendToWorker)        │
└─────────────────────────────────────────────────┘
```

外包团队看到的文档是副本,他们的涂改不会影响原件! 这就是进程隔离。
