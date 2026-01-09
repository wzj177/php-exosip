# Fork 与内存复制机制详解

## 目录
- [问题的起源](#问题的起源)
- [PHP 闭包在 Fork 中的传递](#php-闭包在-fork-中的传递)
- [深入理解：Fork 的工作原理](#深入理解fork-的工作原理)
- [Copy-on-Write (COW) 机制](#copy-on-write-cow-机制)
- [PHP 对象在 Fork 后的状态](#php-对象在-fork-后的状态)
- [最佳实践](#最佳实践)
- [常见问题](#常见问题)

---

## 问题的起源

在开发 PHP-eXosip 扩展的 Master-Worker-Task 架构时，我们遇到了一个有趣的问题：

```php
public function handleWorkerStart(ExoSip $server): void
{
    echo "Worker started (PID: " . posix_getpid() . ")\n";

    // 捕获需要的变量到闭包
    $config = $this->config;
    $debug = $config['debug'] ?? false;
    
    $server->startLongTask(function() use ($server, $config, $debug) {
        echo "[LongTask-Redis] Started (PID: " . getmypid() . ")\n";
        
        // 直接在闭包中实现逻辑（不依赖 $this）
        static::runRedisSubscriberStatic($server, $config, $debug);
    });
}
```

**疑问**：Long Task 进程是通过 C 层的 `fork()` 创建的，那么它是如何拿到 `use ($server, $config, $debug)` 这些变量的？

这个问题的答案涉及到 Unix/Linux 系统的 `fork()` 机制、Copy-on-Write 优化，以及 PHP 闭包的内部实现。让我们从问题本身出发，逐步深入探讨。

---

## PHP 闭包在 Fork 中的传递

### 闭包的内存表示

要理解闭包如何在 fork 后仍然可用，首先需要了解 PHP 闭包在 C 层的结构：

```c
typedef struct _zend_closure {
    zend_function func;              // 函数体（字节码）
    zval this_ptr;                   // $this 指针（如果有）
    HashTable *static_variables;     // use() 捕获的变量
    zend_class_entry *called_scope;  // 调用作用域
} zend_closure;
```

当我们写下：

```php
$callback = function() use ($server, $config, $debug) {
    // 使用 $config 和 $debug
};
```

PHP 会创建一个 `zend_closure` 结构体，并将 `use()` 捕获的变量存储在 `static_variables` 哈希表中。

### 闭包变量的传递过程

在 `startLongTask()` 的 C 层实现中：

```c
PHP_METHOD(ExoSip, startLongTask) {
    zval *callback;
    
    // 1. 解析参数（获取闭包 zval）
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ZVAL(callback)
    ZEND_PARSE_PARAMETERS_END();
    
    // 2. Fork 创建子进程
    pid_t pid = fork();
    
    if (pid == 0) {
        // 子进程（Long Task）
        
        // 3. 闭包的内存已被复制（稍后详细解释）
        //    callback 指向的 zend_closure 结构体有效
        //    static_variables 哈希表也有效
        
        // 4. 调用闭包
        zval retval;
        zval args[0];
        
        call_user_function(NULL, NULL, callback, &retval, 0, args);
        
        // 5. 闭包执行时，从 static_variables 获取 $config 和 $debug
        //    这些变量的值可以正常访问
        
        _exit(0);
    }
}
```

### 内存视图

Fork 前后的内存状态：

```
Fork 前（Worker 进程）:
┌──────────────────────────────────────────┐
│ $callback (zval)                         │
│  └─> zend_closure                        │
│       ├─> func (闭包字节码)              │
│       └─> static_variables (HashTable)   │
│            ├─> "server" => $server       │
│            ├─> "config" => array         │
│            │    ├─> "host" => "127.0..."│
│            │    └─> "port" => 5060      │
│            └─> "debug" => true           │
└──────────────────────────────────────────┘

Fork 后（Long Task 进程）:
┌──────────────────────────────────────────┐
│ $callback (zval) - 副本                  │
│  └─> zend_closure - 副本                │
│       ├─> func (共享只读)                │
│       └─> static_variables (副本)        │
│            ├─> "server" => $server (副本)│
│            ├─> "config" => array (副本)  │
│            │    ├─> "host" => "127.0..."│
│            │    └─> "port" => 5060      │
│            └─> "debug" => true           │
└──────────────────────────────────────────┘
```

**关键发现**：Long Task 进程能访问到 `use()` 捕获的变量，是因为这些变量随着 `zend_closure` 结构体一起被复制到了子进程的内存空间中。

但这是如何实现的？答案就在 `fork()` 的工作机制中。

---

## 深入理解：Fork 的工作原理

### 基本概念

当调用 `fork()` 时，操作系统会创建当前进程的完整副本：

```c
pid_t pid = fork();

if (pid < 0) {
    // fork 失败
} else if (pid == 0) {
    // 子进程代码
    // 此时子进程拥有父进程内存空间的完整副本
} else {
    // 父进程代码
    // pid 是子进程的进程 ID
}
```

### 内存复制的范围

Fork 复制的内容包括：

1. **代码段 (Text Segment)**: 程序的机器指令（共享，只读）
2. **数据段 (Data Segment)**: 全局变量、静态变量
3. **堆 (Heap)**: 动态分配的内存
4. **栈 (Stack)**: 函数调用栈、局部变量
5. **文件描述符表**: 所有打开的文件描述符（socket、文件等）
6. **信号处理器**: 信号处理函数的注册信息
7. **环境变量**: 进程的环境变量

不复制的内容：

1. **进程 ID (PID)**: 子进程获得新的 PID
2. **父进程 ID (PPID)**: 子进程的 PPID 设置为父进程的 PID
3. **文件锁**: 父进程持有的文件锁不会被继承
4. **挂起的信号**: 信号队列不被继承
5. **定时器**: 父进程的定时器不会被继承

### 虚拟内存机制

Fork 后，子进程和父进程的虚拟地址空间是独立的，但初始时指向相同的物理内存页：

```
父进程虚拟地址空间          物理内存          子进程虚拟地址空间
┌─────────────────┐                        ┌─────────────────┐
│ 0x7f8a12345678  │────────┐              │ 0x7f8a12345678  │
│                 │        │              │                 │
│ int x = 100;    │        └──> 物理页 <──┤ int x = 100;    │
│                 │            (共享)     │                 │
└─────────────────┘                        └─────────────────┘
```

**这就是答案的关键**：子进程能访问到父进程的变量值，是因为 fork 复制了整个进程空间，包括 PHP 闭包的 `static_variables` 哈希表。

### pcntl_fork() 与 C 层 fork() 的关系

无论使用 C 层的 `fork()` 还是 PHP 的 `pcntl_fork()`，底层机制完全相同：

```php
// PHP 代码
$pid = pcntl_fork();

// 等价于 C 代码
pid_t pid = fork();
```

`pcntl_fork()` 只是对 `fork()` 系统调用的封装，内存复制机制没有任何区别。

---

## Copy-on-Write (COW) 机制

### 工作原理

COW 是一种优化技术，避免 fork 时立即复制所有内存：

1. **Fork 时**: 只复制页表（虚拟地址到物理地址的映射），不复制实际数据
2. **读取数据**: 父子进程共享同一物理内存页（只读）
3. **修改数据**: 触发页面错误（Page Fault），操作系统复制该页面，修改副本

### COW 的优势

```
场景 1: 只读访问（无复制开销）
┌─────────┐                    ┌─────────┐
│ 父进程   │                    │ 子进程   │
│         │                    │         │
│ read(x) │────> 物理内存 <────│ read(x) │
│         │      (共享)        │         │
└─────────┘                    └─────────┘

场景 2: 写入数据（触发 COW）
┌─────────┐     ┌─────────┐   ┌─────────┐
│ 父进程   │     │ 物理页1  │   │ 子进程   │
│         │────>│ (原始)   │   │         │
│ x = 100 │     └─────────┘   │ x = 200 │
│         │     ┌─────────┐   │         │
│         │     │ 物理页2  │<──│         │
│         │     │ (复制)   │   │         │
└─────────┘     └─────────┘   └─────────┘
```

### 性能影响

- **内存节省**: 只有被修改的页面才会被复制
- **速度优化**: Fork 操作本身非常快（只复制页表）
- **实际场景**: 如果子进程只读取数据而不修改，几乎没有内存开销

### 回答最初的问题

现在我们可以完整回答最初的问题了：

**Q: Long Task 进程如何拿到 `use ($server, $config, $debug)` 的？**

**A**: 
1. **闭包捕获**：`use()` 关键字将变量存储在闭包的 `static_variables` 哈希表中
2. **Fork 复制**：`fork()` 复制了整个进程空间，包括闭包结构体和哈希表
3. **COW 优化**：初始时父子进程共享物理内存，只读访问无复制开销
4. **独立修改**：如果子进程修改变量，触发 COW，复制该页面

因此，Long Task 进程能够正常访问闭包中的所有变量，就像它们本来就存在一样。



---

## PHP 对象在 Fork 后的状态

### 对象的内存结构

```c
typedef struct _php_exosip_obj {
    SipContext *ctx;     // 指向 C 层结构体的指针
    zend_object std;     // PHP 对象头
} php_exosip_obj;

typedef struct _sip_context {
    struct eXosip_t *exosip_ctx;  // eXosip 上下文
    int worker_sockfd;            // Worker 通信 socket
    int is_worker;                // 进程角色标志
    int is_long_task;
    // ... 其他字段
} SipContext;
```

### Fork 后的对象状态

```
Fork 前（父进程 - Worker）:
┌──────────────────────────────────┐
│ $server (php_exosip_obj)         │
│  ├─> ctx (指针) ─────────┐      │
│  └─> std (zend_object)    │      │
└───────────────────────────┼──────┘
                            │
                            ▼
        ┌──────────────────────────────────┐
        │ SipContext (C 结构体)             │
        │  ├─> exosip_ctx (已初始化)        │
        │  ├─> worker_sockfd = -1          │
        │  ├─> is_worker = 1               │
        │  └─> is_long_task = 0            │
        └──────────────────────────────────┘

Fork 后（子进程 - Long Task）:
┌──────────────────────────────────┐
│ $server (php_exosip_obj) - 副本  │
│  ├─> ctx (指针值相同) ───┐      │
│  └─> std (副本)           │      │
└───────────────────────────┼──────┘
                            │
                            ▼
        ┌──────────────────────────────────┐
        │ SipContext (C 结构体) - 副本     │
        │  ├─> exosip_ctx (损坏的线程状态) │
        │  ├─> worker_sockfd = sv[1] (新)  │
        │  ├─> is_worker = 0 (修改)        │
        │  └─> is_long_task = 1 (修改)     │
        └──────────────────────────────────┘
```

### 关键点

1. **PHP 对象被复制**: `php_exosip_obj` 结构体在子进程有独立副本
2. **指针值相同**: `ctx` 指针的值被复制，指向 `SipContext` 的副本
3. **C 结构体被复制**: `SipContext` 在子进程有独立副本
4. **需要重新初始化**: 某些资源（如线程、socket）在 fork 后状态损坏

### 特殊处理示例

```c
if (pid == 0) {
    // 子进程（Long Task）
    
    // 1. 修改进程角色标志
    obj->ctx->is_long_task = 1;
    obj->ctx->is_worker = 0;
    obj->ctx->is_task = 0;
    
    // 2. 设置新的通信 socket
    obj->ctx->worker_sockfd = sv[1];
    
    // 3. 清理损坏的资源
    if (obj->ctx->ctx) {
        obj->ctx->ctx = NULL;  // eXosip 上下文已损坏，置空
    }
    
    // 4. 关闭继承的文件描述符
    for (int j = 0; j < obj->ctx->task_count; j++) {
        close(obj->ctx->task_sockfds[j]);
    }
}
```

---

## 最佳实践

### 1. 避免在闭包中使用 $this

**错误示例**：

```php
public function handleWorkerStart() {
    // 错误：$this 会导致整个对象被捕获
    $this->server->startLongTask(function() {
        $this->someMethod();  // 子进程中对象状态可能不一致
    });
}
```

**正确示例**：

```php
public function handleWorkerStart() {
    // 正确：只捕获需要的变量
    $config = $this->config;
    $debug = $this->debug;
    
    $this->server->startLongTask(function() use ($config, $debug) {
        // 使用捕获的简单变量
        self::runTask($config, $debug);
    });
}

private static function runTask(array $config, bool $debug) {
    // 静态方法，不依赖对象状态
}
```

### 2. 只传递简单数据类型

推荐传递：
- 标量类型（int, string, bool, float）
- 数组（纯数据）
- 对象（如果确保 fork 安全）

避免传递：
- 资源类型（文件句柄、数据库连接）
- 包含资源的对象
- 复杂的对象图

### 3. 在子进程中重新创建资源

```php
private static function runLongTask(ExoSip $server, array $config) {
    // 重新创建数据库连接（不能使用父进程的连接）
    $redis = new Redis();
    $redis->connect($config['redis']['host']);
    
    // 重新初始化其他资源
    // ...
}
```

### 4. 正确处理文件描述符

```php
// C 层代码示例
if (pid == 0) {
    // 子进程
    
    // 关闭不需要的 socket（继承自父进程）
    close(sv[0]);  // 关闭父进程端
    
    // 只保留子进程需要的 socket
    ctx->worker_sockfd = sv[1];
    
    // 关闭其他继承的 fd
    for (int i = 0; i < ctx->task_count; i++) {
        close(ctx->task_sockfds[i]);
    }
}
```

### 5. 使用静态方法避免状态问题

```php
class Handler {
    private $state = [];  // 对象状态
    
    public function startLongTask() {
        $config = $this->getConfig();
        
        // 使用静态方法，不依赖对象状态
        $this->server->startLongTask(function() use ($config) {
            self::processInBackground($config);
        });
    }
    
    private static function processInBackground(array $config) {
        // 这里不能访问 $this->state
        // 但可以安全地在子进程中运行
    }
}
```

---

## 常见问题

### Q1: pcntl_fork() 和 C 层 fork() 有什么区别？

**答**: 没有本质区别。`pcntl_fork()` 是 PHP 提供的对 C `fork()` 的封装，底层调用的是同一个系统调用。内存复制机制完全相同。

```php
// PHP 代码
$pid = pcntl_fork();

// 等价于 C 代码
pid_t pid = fork();
```

### Q2: 为什么闭包能捕获变量？

**答**: PHP 的闭包使用 `use` 关键字捕获变量时，会将这些变量的**值**（或引用）存储在闭包对象的 `static_variables` 哈希表中。Fork 后，这个哈希表被复制到子进程，所以子进程能访问这些值。

### Q3: 子进程修改变量会影响父进程吗？

**答**: 不会。因为 COW 机制，当子进程修改变量时，会触发页面复制，父子进程的数据完全独立。

```php
$value = 100;

$pid = pcntl_fork();

if ($pid == 0) {
    // 子进程
    $value = 200;  // 修改不影响父进程
    exit(0);
}

// 父进程
echo $value;  // 仍然是 100
```

### Q4: 如何在子进程中与父进程通信？

**答**: 使用进程间通信（IPC）机制：

1. **Socket Pair**: `socketpair()` 创建双向通信管道
2. **管道**: `pipe()` 创建单向通信管道
3. **共享内存**: `shmop_open()` 等函数
4. **消息队列**: `msg_get_queue()` 等函数

本项目使用 Socket Pair：

```c
int sv[2];
socketpair(AF_UNIX, SOCK_STREAM, 0, sv);

pid_t pid = fork();

if (pid == 0) {
    // 子进程
    close(sv[0]);
    write(sv[1], data, len);  // 发送数据给父进程
} else {
    // 父进程
    close(sv[1]);
    read(sv[0], buffer, len);  // 接收子进程数据
}
```

### Q5: 为什么对象中的资源（如数据库连接）不能在子进程中使用？

**答**: 资源类型（如 socket、文件句柄、数据库连接）在 C 层通常包含：

1. **文件描述符**: 虽然被复制，但底层连接状态可能不一致
2. **线程状态**: Fork 只复制调用线程，其他线程的状态丢失
3. **锁状态**: 互斥锁等同步原语在子进程中状态未定义

正确做法是在子进程中重新创建这些资源。

### Q6: 如何判断代码是否 Fork 安全？

检查清单：

- [ ] 不依赖全局状态（除非是只读的）
- [ ] 不使用父进程的资源句柄
- [ ] 不依赖父进程的线程
- [ ] 使用 IPC 而不是共享内存进行通信
- [ ] 正确处理文件描述符（关闭不需要的）
- [ ] 在子进程中重新初始化资源

---

## 参考资料

### 系统调用文档

- `man 2 fork` - Fork 系统调用
- `man 2 socketpair` - Socket Pair 创建
- `man 7 signal` - 信号处理

### PHP 文档

- PHP PCNTL 扩展: https://www.php.net/manual/zh/book.pcntl.php
- PHP 闭包: https://www.php.net/manual/zh/class.closure.php

### 相关标准

- POSIX.1-2017: Fork 和进程管理
- Copy-on-Write: 操作系统原理

---

## 总结

1. **Fork 复制整个进程空间**: 包括代码、数据、堆、栈、文件描述符等
2. **Copy-on-Write 优化**: 只有被修改的内存页才会真正复制
3. **闭包变量传递**: 通过 `static_variables` 哈希表存储，Fork 后被复制
4. **对象状态**: PHP 对象被复制，但 C 层资源需要特殊处理
5. **最佳实践**: 使用静态方法、只传递简单数据、重新创建资源

无论使用 C 层 `fork()` 还是 PHP 的 `pcntl_fork()`，底层机制完全相同，理解这些原理有助于编写健壮的多进程 PHP 应用。
