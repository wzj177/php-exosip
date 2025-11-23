# Master-Worker-Task 架构实现步骤

## 架构设计

```
┌─────────────────────────────────────────────────────┐
│ Master Process (主进程)                              │
│ - fork 1个 Worker + N个 Task                        │
│ - 监控子进程健康 (SIGCHLD)                          │
│ - Worker 异常退出 → 自动重启                         │
│ - SIGTERM/SIGINT → 优雅关闭所有子进程                │
└────────────┬────────────────────────────────────────┘
             │
    ┌────────┴────────┐
    │                 │
┌───▼──────────┐  ┌──▼────────────────────────────────┐
│ Worker (1个) │  │ Task Pool (4-8个可配置)           │
│              │  │                                   │
│ SIP 事件循环 │  │ ┌─────────┐ ┌─────────┐           │
│ exosip_exec()├──┼→│ Task 1  │ │ Task 2  │ ...      │
│              │  │ │ HTTP    │ │ Timer   │           │
│ 非阻塞投递   │  │ └─────────┘ └─────────┘           │
│ postTask()   │  │ ┌─────────┐ ┌─────────┐           │
│              │  │ │ Task 3  │ │ Task 4  │           │
└──────────────┘  │ │ Redis   │ │ DB      │           │
                  │ └─────────┘ └─────────┘           │
                  └───────────────────────────────────┘
```

## 核心特性

1. **进程隔离**：Worker 崩溃不影响 Task，Task 崩溃不影响 Worker
2. **自动恢复**：Master 监控 Worker，异常退出时自动 fork 新进程
3. **非阻塞通信**：Worker → Task 使用管道，非阻塞写入
4. **负载均衡**：Task 池通过轮询或负载分发任务
5. **优雅关闭**：接收 SIGTERM 时，等待所有任务完成再退出

---

## 实现步骤

### 步骤 1：数据结构定义 (exosip_wrapper.h)

#### 1.1 任务消息结构

```c
// 任务类型常量
#define TASK_TYPE_HTTP_POST     1  // HTTP POST 请求
#define TASK_TYPE_HTTP_GET      2  // HTTP GET 请求
#define TASK_TYPE_TIMER         3  // 定时器任务
#define TASK_TYPE_REDIS_CMD     4  // Redis 命令
#define TASK_TYPE_DB_QUERY      5  // 数据库查询
#define TASK_TYPE_WEBHOOK       6  // Webhook 推送
#define TASK_TYPE_CUSTOM        99 // 自定义任务

// 任务消息结构（通过管道传递）
typedef struct {
    int type;                  // 任务类型 (TASK_TYPE_*)
    int priority;              // 优先级 (0=普通, 1=高, 2=紧急)
    unsigned long task_id;     // 任务ID (用于追踪)
    char data[2048];           // JSON payload
} task_msg_t;
```

**为什么 data 是 2048 字节？**
- 大部分任务（HTTP URL、设备ID、简单命令）都在 1KB 内
- 如果超过 2KB，建议用共享内存传递

#### 1.2 进程管理结构

```c
// 在 SipContext 中添加：
typedef struct {
    struct eXosip_t *ctx;
    
    // ... 现有字段 ...
    
    // === 进程管理 ===
    pid_t master_pid;          // Master 进程 PID
    pid_t worker_pid;          // Worker 进程 PID (仅在 Master 中有效)
    pid_t *task_pids;          // Task 进程 PID 数组
    int task_count;            // Task 进程数量 (默认 4)
    
    // === IPC 通信 ===
    int task_pipes[2];         // Worker → Task 的管道 [read_fd, write_fd]
    int next_task_index;       // 轮询分发任务的索引
    pthread_mutex_t pipe_lock; // 管道写入锁（避免并发写冲突）
    
    // === 进程状态 ===
    int is_master;             // 1=Master, 0=Worker/Task
    int is_worker;             // 1=Worker, 0=其他
    int is_task;               // 1=Task, 0=其他
    int task_worker_id;        // Task 进程编号 (0-N)
    
    // === 监控统计 ===
    unsigned long tasks_posted;   // 已投递任务数
    unsigned long tasks_failed;   // 投递失败数
    time_t worker_start_time;     // Worker 启动时间
    int worker_restart_count;     // Worker 重启次数
    
} SipContext;
```

---

### 步骤 2：Master 进程实现 (exosip_wrapper.c)

#### 2.1 信号处理器

```c
// 全局变量（Master 和 Worker 共享）
static volatile sig_atomic_t g_shutdown_flag = 0;  // 优雅关闭标志
static volatile sig_atomic_t g_worker_died = 0;    // Worker 死亡标志

// SIGCHLD 处理：子进程退出
static void sigchld_handler(int signo) {
    int status;
    pid_t pid;
    
    // 非阻塞等待所有退出的子进程
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (WIFEXITED(status)) {
            fprintf(stderr, "[Master] Child %d exited with status %d\n", 
                    pid, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            fprintf(stderr, "[Master] Child %d killed by signal %d\n", 
                    pid, WTERMSIG(status));
        }
        
        // 标记 Worker 死亡（Master 主循环会重启）
        g_worker_died = 1;
    }
}

// SIGTERM/SIGINT 处理：优雅关闭
static void sigterm_handler(int signo) {
    g_shutdown_flag = 1;
    fprintf(stderr, "[Master] Received signal %d, shutting down...\n", signo);
}
```

#### 2.2 Master 初始化

```c
// 启动 Master 进程（在 sip_init 中调用）
int sip_start_master(SipContext *ctx) {
    ctx->master_pid = getpid();
    ctx->is_master = 1;
    ctx->is_worker = 0;
    ctx->is_task = 0;
    
    // 设置信号处理
    signal(SIGCHLD, sigchld_handler);
    signal(SIGTERM, sigterm_handler);
    signal(SIGINT, sigterm_handler);
    signal(SIGPIPE, SIG_IGN); // 忽略管道断开信号
    
    fprintf(stderr, "[Master] Started (PID: %d)\n", ctx->master_pid);
    
    // 创建管道（Worker → Task 通信）
    if (pipe(ctx->task_pipes) == -1) {
        perror("[Master] pipe() failed");
        return -1;
    }
    
    // 设置管道为非阻塞
    int flags = fcntl(ctx->task_pipes[1], F_GETFL, 0);
    fcntl(ctx->task_pipes[1], F_SETFL, flags | O_NONBLOCK);
    
    // Fork Worker 进程
    ctx->worker_pid = fork_worker(ctx);
    if (ctx->worker_pid < 0) {
        return -1;
    }
    
    // Fork Task 进程池
    ctx->task_pids = malloc(ctx->task_count * sizeof(pid_t));
    for (int i = 0; i < ctx->task_count; i++) {
        ctx->task_pids[i] = fork_task_worker(ctx, i);
        if (ctx->task_pids[i] < 0) {
            return -1;
        }
    }
    
    return 0;
}
```

#### 2.3 Master 主循环

```c
// Master 监控循环（阻塞等待信号）
void master_main_loop(SipContext *ctx) {
    fprintf(stderr, "[Master] Entering monitor loop\n");
    
    while (!g_shutdown_flag) {
        // 检查 Worker 是否死亡
        if (g_worker_died) {
            fprintf(stderr, "[Master] Worker died, restarting...\n");
            
            // 重启 Worker
            ctx->worker_restart_count++;
            ctx->worker_pid = fork_worker(ctx);
            
            if (ctx->worker_pid < 0) {
                fprintf(stderr, "[Master] Failed to restart Worker, exiting\n");
                break;
            }
            
            g_worker_died = 0;
        }
        
        // 休眠 1 秒（减少 CPU 占用）
        sleep(1);
    }
    
    // 优雅关闭：发送 SIGTERM 给所有子进程
    fprintf(stderr, "[Master] Shutting down all children...\n");
    
    if (ctx->worker_pid > 0) {
        kill(ctx->worker_pid, SIGTERM);
    }
    
    for (int i = 0; i < ctx->task_count; i++) {
        if (ctx->task_pids[i] > 0) {
            kill(ctx->task_pids[i], SIGTERM);
        }
    }
    
    // 等待所有子进程退出（最多 5 秒）
    for (int i = 0; i < 5; i++) {
        int status;
        if (waitpid(-1, &status, WNOHANG) <= 0) {
            sleep(1);
        }
    }
    
    // 强制杀死残留进程
    if (ctx->worker_pid > 0) kill(ctx->worker_pid, SIGKILL);
    for (int i = 0; i < ctx->task_count; i++) {
        if (ctx->task_pids[i] > 0) kill(ctx->task_pids[i], SIGKILL);
    }
    
    fprintf(stderr, "[Master] Shutdown complete\n");
}
```

---

### 步骤 3：Worker 进程实现

#### 3.1 Fork Worker

```c
pid_t fork_worker(SipContext *ctx) {
    pid_t pid = fork();
    
    if (pid == 0) {
        // === 子进程：Worker ===
        ctx->is_master = 0;
        ctx->is_worker = 1;
        ctx->is_task = 0;
        ctx->worker_start_time = time(NULL);
        
        // 关闭管道读端（Worker 只写）
        close(ctx->task_pipes[0]);
        
        // 设置进程标题（便于监控）
        // setproctitle("php-exosip: worker");
        
        fprintf(stderr, "[Worker] Started (PID: %d)\n", getpid());
        
        // 运行 SIP 事件循环
        worker_main_loop(ctx);
        
        // Worker 退出
        fprintf(stderr, "[Worker] Exiting (PID: %d)\n", getpid());
        _exit(0);
    }
    
    return pid; // 父进程返回子进程 PID
}
```

#### 3.2 Worker 主循环

```c
void worker_main_loop(SipContext *ctx) {
    signal(SIGTERM, sigterm_handler);
    signal(SIGINT, sigterm_handler);
    
    ctx->running = 1;
    
    while (!g_shutdown_flag && ctx->running) {
        // 调用 eXosip 事件循环（超时 100ms）
        eXosip_event_t *evt = eXosip_event_wait(ctx->ctx, 0, 100);
        
        if (evt) {
            // 处理 SIP 事件（可能需要投递 Task）
            handle_sip_event(ctx, evt);
            eXosip_event_free(evt);
        }
        
        // 检查定时器（如需要）
        if (ctx->timer_interval_ms > 0) {
            if (sip_check_and_fire_timer(ctx)) {
                // 触发 onTimer 回调（可能投递 Task）
            }
        }
    }
    
    fprintf(stderr, "[Worker] Loop exited\n");
}
```

---

### 步骤 4：Task 进程池实现

#### 4.1 Fork Task Worker

```c
pid_t fork_task_worker(SipContext *ctx, int worker_id) {
    pid_t pid = fork();
    
    if (pid == 0) {
        // === 子进程：Task Worker ===
        ctx->is_master = 0;
        ctx->is_worker = 0;
        ctx->is_task = 1;
        ctx->task_worker_id = worker_id;
        
        // 关闭管道写端（Task 只读）
        close(ctx->task_pipes[1]);
        
        fprintf(stderr, "[Task-%d] Started (PID: %d)\n", worker_id, getpid());
        
        // 运行任务处理循环
        task_worker_loop(ctx);
        
        fprintf(stderr, "[Task-%d] Exiting (PID: %d)\n", worker_id, getpid());
        _exit(0);
    }
    
    return pid;
}
```

#### 4.2 Task 主循环

```c
void task_worker_loop(SipContext *ctx) {
    signal(SIGTERM, sigterm_handler);
    
    int read_fd = ctx->task_pipes[0];
    task_msg_t msg;
    
    while (!g_shutdown_flag) {
        // 阻塞读取任务消息
        ssize_t n = read(read_fd, &msg, sizeof(msg));
        
        if (n == sizeof(msg)) {
            // 处理任务
            handle_task(ctx, &msg);
        } else if (n == 0) {
            // 管道关闭（Writer 退出）
            fprintf(stderr, "[Task-%d] Pipe closed, exiting\n", 
                    ctx->task_worker_id);
            break;
        } else if (n < 0) {
            if (errno == EINTR) continue; // 信号中断，重试
            perror("[Task] read() failed");
            break;
        }
    }
}
```

#### 4.3 任务处理器

```c
void handle_task(SipContext *ctx, task_msg_t *msg) {
    fprintf(stderr, "[Task-%d] Processing task type=%d, id=%lu\n",
            ctx->task_worker_id, msg->type, msg->task_id);
    
    switch (msg->type) {
        case TASK_TYPE_HTTP_POST:
            do_http_post(msg->data);
            break;
            
        case TASK_TYPE_HTTP_GET:
            do_http_get(msg->data);
            break;
            
        case TASK_TYPE_TIMER:
            do_timer_job(msg->data);
            break;
            
        case TASK_TYPE_REDIS_CMD:
            do_redis_command(msg->data);
            break;
            
        case TASK_TYPE_WEBHOOK:
            do_webhook_push(msg->data);
            break;
            
        default:
            fprintf(stderr, "[Task-%d] Unknown task type: %d\n",
                    ctx->task_worker_id, msg->type);
    }
}
```

#### 4.4 HTTP 任务实现（libcurl）

```c
#include <curl/curl.h>

// HTTP POST 实现
void do_http_post(const char *json_data) {
    // 解析 JSON: {"url":"http://...", "body":"..."}
    // 这里简化处理，实际需要 JSON 解析库（如 cJSON）
    
    CURL *curl = curl_easy_init();
    if (!curl) return;
    
    // 示例：POST 到固定 URL
    curl_easy_setopt(curl, CURLOPT_URL, "http://api.example.com/webhook");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L); // 5秒超时
    
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "[Task] curl failed: %s\n", curl_easy_strerror(res));
    }
    
    curl_easy_cleanup(curl);
}
```

---

### 步骤 5：任务投递接口 (Worker → Task)

#### 5.1 C 层投递函数

```c
// Worker 进程调用：投递任务到 Task 池
int sip_post_task(SipContext *ctx, int task_type, const char *json_data) {
    if (!ctx->is_worker) {
        fprintf(stderr, "[ERROR] postTask can only be called in Worker\n");
        return -1;
    }
    
    task_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    
    msg.type = task_type;
    msg.priority = 0;
    msg.task_id = ++ctx->tasks_posted;
    strncpy(msg.data, json_data, sizeof(msg.data) - 1);
    
    // 加锁（避免并发写冲突）
    pthread_mutex_lock(&ctx->pipe_lock);
    
    // 非阻塞写入
    ssize_t n = write(ctx->task_pipes[1], &msg, sizeof(msg));
    
    pthread_mutex_unlock(&ctx->pipe_lock);
    
    if (n != sizeof(msg)) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            fprintf(stderr, "[Worker] Task pipe full, task dropped\n");
        } else {
            perror("[Worker] write() to task pipe failed");
        }
        ctx->tasks_failed++;
        return -1;
    }
    
    return 0;
}
```

---

### 步骤 6：PHP 层接口 (php_exosip.c)

#### 6.1 添加 postTask 方法

```c
PHP_METHOD(ExoSip, postTask) {
    zend_long task_type;
    char *data;
    size_t data_len;
    
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_LONG(task_type)
        Z_PARAM_STRING(data, data_len)
    ZEND_PARSE_PARAMETERS_END();
    
    php_exosip_obj *obj = php_exosip_from_obj(Z_OBJ_P(getThis()));
    
    if (!obj->ctx) {
        RETURN_FALSE;
    }
    
    // 调用 C 层投递函数
    int ret = sip_post_task(obj->ctx, (int)task_type, data);
    
    RETURN_BOOL(ret == 0);
}
```

#### 6.2 添加配置参数

```php
// PHP 使用示例
$config = [
    'ip' => '0.0.0.0',
    'port' => 5060,
    'mode' => 'udp',
    
    // 新增：进程配置
    'task_worker_num' => 4,  // Task 进程数量（默认 4）
];

$exosip = new ExoSip($config);
```

---

### 步骤 7：完整使用示例

#### 7.1 PHP 示例：gb28181_multiprocess.php

```php
<?php
require_once __DIR__ . '/sip_config.php';

$config['task_worker_num'] = 4;

$exosip = new ExoSip($config);
$gb28181 = new GB28181Handler($exosip);

// SIP 事件回调
$exosip->onMessage = function($event) use ($exosip, $gb28181) {
    $from = $event->getFromUri();
    $body = $event->getMessageBody();
    
    echo "[Worker] Received MESSAGE from {$from}\n";
    
    // 解析 GB28181 消息
    $gb28181->handleMessage($event);
    
    // 投递 Webhook 任务（异步推送）
    $exosip->postTask(TASK_TYPE_WEBHOOK, json_encode([
        'event' => 'sip_message',
        'from' => $from,
        'body' => $body,
        'timestamp' => time()
    ]));
};

// 定时器回调（每 5 秒）
$exosip->onTimer = function() use ($exosip, $gb28181) {
    echo "[Worker] Timer tick\n";
    
    // 检查设备心跳
    $offlineDevices = $gb28181->checkHeartbeat();
    
    foreach ($offlineDevices as $deviceId) {
        // 投递任务：推送离线事件
        $exosip->postTask(TASK_TYPE_HTTP_POST, json_encode([
            'url' => 'http://api.example.com/device-offline',
            'device_id' => $deviceId,
            'timestamp' => time()
        ]));
    }
    
    return true; // 继续运行
};

// 启动服务（Master 进程不会返回）
echo "[Master] Starting GB28181 Server...\n";
$exosip->run();
```

---

## 技术要点总结

### 1. 进程模型

| 进程类型 | 数量 | 职责 | 退出处理 |
|---------|------|------|---------|
| Master | 1 | 监控子进程、自动重启 | 接收 SIGTERM 后优雅关闭 |
| Worker | 1 | SIP 事件循环、投递任务 | Master 自动重启 |
| Task | 4-8 | 处理耗时操作（HTTP/DB/Redis） | 进程隔离，崩溃不影响 Worker |

### 2. IPC 通信

- **管道（pipe）**：Worker → Task（单向）
- **非阻塞写入**：防止 Worker 被慢速 Task 阻塞
- **轮询分发**：多个 Task 进程共享一个管道（内核自动负载均衡）

### 3. 信号处理

- **SIGCHLD**：Master 捕获，检测子进程死亡
- **SIGTERM/SIGINT**：优雅关闭，等待任务完成
- **SIGPIPE**：忽略（防止管道写入崩溃）

### 4. 错误恢复

- Worker 崩溃 → Master 自动 fork 新 Worker
- Task 崩溃 → 不影响 Worker，任务丢失（可记录日志）
- Master 崩溃 → 整个系统停止（需外部监控，如 systemd）

### 5. 性能优化

- 管道缓冲区：默认 64KB（可调整为 1MB）
- Task 数量：根据 CPU 核心数调整（建议 4-8 个）
- HTTP 超时：5 秒（防止慢速 API 拖累）

---

## 下一步实现顺序

1. ✅ **步骤 1**：定义数据结构（task_msg_t、SipContext 扩展）
2. ✅ **步骤 2**：实现 Master 进程（信号处理、监控循环）
3. ✅ **步骤 3**：实现 Worker 进程（SIP 事件循环）
4. ✅ **步骤 4**：实现 Task 进程池（任务处理）
5. ✅ **步骤 5**：实现任务投递接口（C 层）
6. ✅ **步骤 6**：实现 PHP 层接口（postTask 方法）
7. ✅ **步骤 7**：编写测试示例（验证重启、任务分发）

---

## 潜在问题与解决方案

### Q1：管道满了怎么办？

**问题**：Worker 投递任务过快，Task 处理不过来，管道缓冲区满（64KB）

**方案**：
- 非阻塞写入 + 丢弃策略（记录日志）
- 或使用共享内存队列（如 System V 消息队列）

### Q2：Task 如何返回结果给 Worker？

**问题**：某些任务需要返回数据（如查询设备状态）

**方案**：
- 方案 A：使用 Redis 作为中间存储（Task 写入，Worker 读取）
- 方案 B：使用双向管道（socketpair）
- 方案 C：共享内存 + 信号量

### Q3：Master 如何知道哪个 Task 崩溃了？

**问题**：SIGCHLD 只告诉进程退出，不知道是 Worker 还是 Task

**方案**：
- 在 sigchld_handler 中通过 PID 判断
- 记录所有子进程 PID 到 Master 的映射表

### Q4：Worker 重启后，原有连接怎么办？

**问题**：eXosip 状态在 Worker 进程内存中，重启后丢失

**方案**：
- 设备会重新 REGISTER（超时后自动重连）
- 或使用共享内存保存关键状态（复杂，暂不建议）

### Q5：如何平滑重启（不丢失请求）？

**问题**：Worker 重启时，新消息可能丢失

**方案**：
- 双 Worker 模式（热备份）
- 或使用 SO_REUSEPORT（需 Linux 3.9+）

---

## 编译和测试

### 编译命令

```bash
cd /Users/jiechengyang/src/c-app/php-exosip
make clean
make
sudo make install
```

### 测试命令

```bash
# 启动服务
php examples/gb28181_multiprocess.php

# 查看进程树
pstree -p $(pgrep -f gb28181_multiprocess)

# 模拟 Worker 崩溃
kill -9 <worker_pid>

# 观察 Master 是否自动重启
```

---

## 总结

这套架构的核心思想：
1. **Master 只管理，不干活**（监控 + 重启）
2. **Worker 只处理 SIP，不做耗时操作**（事件循环 + 投递）
3. **Task 专注业务，进程隔离**（HTTP/DB/Redis）

**优势**：
- Worker 永远不阻塞（100ms 事件循环）
- Task 崩溃不影响 SIP 协议
- Master 自动恢复 Worker，无需人工干预

**适用场景**：
- GB28181 国标平台（1000+ 设备）
- SIP 网关/代理服务器
- 任何需要高可用的 SIP 服务

---

**准备好实现了吗？告诉我你是否认可这套方案！** 🚀
