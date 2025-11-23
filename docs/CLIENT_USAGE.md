# ExoSipClient 使用说明

## 重要提示 ⚠️

**当前实现中，`start()` 方法会导致事件丢失！**

后台线程会消费所有事件，但只处理注册相关事件，其他事件（MESSAGE、INVITE 等）会被丢弃。

## 推荐使用模式

### ✅ 模式 1：手动模式（推荐）

**不使用后台线程，完全手动控制事件处理**

```php
<?php
$client = new ExoSipClient([
    'server_ip' => '127.0.0.1',
    'server_port' => 5060,
    'username' => 'device001',
    'password' => '12345678',
    'realm' => '3402000000',
    'mode' => 'UDP',
    'debug' => false
]);

// 不调用 start()
$client->sendRegister();

$registered = false;
$running = true;

while ($running) {
    // 手动处理事件
    $events = $client->processEvents(100); // 100ms 超时
    
    foreach ($events as $evt) {
        // 注册成功
        if ($evt['type'] == EXOSIP_REGISTRATION_SUCCESS) {
            $registered = true;
            echo "注册成功\n";
        }
        
        // 收到 MESSAGE 请求
        if ($evt['type'] == EXOSIP_MESSAGE_NEW) {
            echo "收到消息请求\n";
            // 处理消息
        }
        
        // 收到响应
        if (isset($evt['status_code'])) {
            echo "收到响应: {$evt['status_code']}\n";
        }
    }
    
    // 发送心跳
    if ($registered) {
        static $last_keepalive = 0;
        if (time() - $last_keepalive >= 30) {
            $client->sendMessage('sip:server@domain', 'Keepalive', 'text/plain');
            $last_keepalive = time();
        }
    }
    
    usleep(10000); // 10ms 休眠，避免 CPU 占用过高
}

$client->sendUnregister();
```

### ❌ 模式 2：后台线程模式（不推荐，有缺陷）

**使用 `start()` 会导致事件丢失**

```php
<?php
$client = new ExoSipClient([...]);

// 启动后台线程
$client->start();  // ⚠️ 后台线程会消费所有事件

$client->sendRegister();

sleep(2);

// 问题：processEvents() 收不到 MESSAGE 等业务事件
// 因为后台线程已经消费并丢弃了
$events = $client->processEvents(100);  // ❌ 返回空数组

$client->stop();
```

**问题分析**：
1. 后台线程调用 `eXosip_event_wait()` 消费所有事件
2. 只处理 `REGISTRATION_SUCCESS/FAILURE`
3. 其他事件（MESSAGE、INVITE、响应等）直接丢弃
4. 主线程 `processEvents()` 无法收到业务事件

## 使用技巧

### 1. 事件循环最佳实践

```php
while ($running) {
    $events = $client->processEvents(100);  // 短超时
    
    foreach ($events as $evt) {
        handleEvent($evt);
    }
    
    // 业务逻辑
    doBusinessLogic();
    
    usleep(10000);  // 10ms 休眠，减少 CPU 占用
}
```

**关键点**：
- 短超时（100ms）提高响应性
- 每次循环后 `usleep(10000)` 避免空转
- 平衡：响应延迟 vs CPU 占用

### 2. 注册状态管理

```php
$registered = false;

foreach ($events as $evt) {
    if ($evt['type'] == EXOSIP_REGISTRATION_SUCCESS) {
        $registered = true;
        echo "注册成功\n";
    }
    
    if ($evt['type'] == EXOSIP_REGISTRATION_FAILURE) {
        $registered = false;
        echo "注册失败，重试中...\n";
        $client->sendRegister();  // 自动重试
    }
}

// 使用本地状态判断
if ($registered) {
    // 发送业务消息
}
```

**不要使用 `isRegistered()`**：
- 该方法读取内部状态
- 只有后台线程（start）才会更新
- 手动模式需自行维护状态

### 3. 心跳发送

```php
$keepalive_interval = 30;  // 秒
$last_keepalive = time();

while ($running) {
    $events = $client->processEvents(100);
    // 处理事件...
    
    if ($registered && time() - $last_keepalive >= $keepalive_interval) {
        $xml = "<?xml version=\"1.0\"?><Notify>...</Notify>";
        $client->sendMessage('sip:server@domain', $xml, 'Application/MANSCDP+xml');
        $last_keepalive = time();
    }
    
    usleep(10000);
}
```

### 4. 完整 GB28181 设备示例

```php
<?php
$deviceId = '34020000001320000001';
$serverId = '34020000002000000001';
$realm = '3402000000';

$client = new ExoSipClient([
    'server_ip' => '127.0.0.1',
    'server_port' => 5060,
    'username' => $deviceId,
    'password' => '12345678',
    'realm' => $realm,
    'mode' => 'UDP',
    'from_uri' => "sip:{$deviceId}@{$realm}"
]);

$client->sendRegister();

$registered = false;
$last_keepalive = time();

while (true) {
    $events = $client->processEvents(100);
    
    foreach ($events as $evt) {
        // 注册成功
        if ($evt['type'] == EXOSIP_REGISTRATION_SUCCESS) {
            $registered = true;
            echo "设备已注册\n";
        }
        
        // 收到平台查询
        if ($evt['type'] == EXOSIP_MESSAGE_NEW) {
            // 解析 XML，判断查询类型
            // 响应 Catalog、DeviceInfo 等
            $catalogXml = buildCatalogResponse($deviceId);
            $client->sendMessage("sip:{$serverId}@{$realm}", $catalogXml, 'Application/MANSCDP+xml');
        }
    }
    
    // 发送心跳
    if ($registered && time() - $last_keepalive >= 30) {
        $keepaliveXml = buildKeepaliveXml($deviceId);
        $client->sendMessage("sip:{$serverId}@{$realm}", $keepaliveXml, 'Application/MANSCDP+xml');
        $last_keepalive = time();
    }
    
    usleep(10000);
}
```

## 未来改进方向

### 选项 1：移除 start() 方法

简化 API，只保留手动模式：

```php
class ExoSipClient {
    // 移除 start() 和 stop()
    public function processEvents(int $timeout_ms = 0): array;
    // 其他方法保持不变
}
```

### 选项 2：修复后台线程

实现事件队列，让后台线程暂存事件：

```c
// 后台线程暂存事件到队列
static void* client_event_thread(void *arg) {
    while (running) {
        eXosip_event_t *evt = eXosip_event_wait();
        
        // 暂存到队列
        pthread_mutex_lock(&queue_lock);
        event_queue_push(evt);
        pthread_mutex_unlock(&queue_lock);
    }
}

// processEvents 从队列读取
int client_process_events(ClientContext *ctx, zval *arr) {
    pthread_mutex_lock(&queue_lock);
    while (!event_queue_empty()) {
        eXosip_event_t *evt = event_queue_pop();
        // 转换为 PHP 数组
    }
    pthread_mutex_unlock(&queue_lock);
}
```

### 选项 3：回调模式

提供回调接口，后台线程调用 PHP 回调：

```php
$client->onMessage = function($evt) {
    echo "收到消息\n";
};

$client->onResponse = function($evt) {
    echo "收到响应: {$evt['status_code']}\n";
};

$client->start();  // 后台线程调用回调
$client->wait();   // 阻塞等待
```

## 总结

- ✅ **当前推荐**：不使用 `start()`，纯手动模式
- ❌ **避免**：`start()` + `processEvents()` 组合
- ⚠️ **注意**：手动模式需自行维护注册状态
- 📝 **提示**：循环中添加 `usleep()` 避免 CPU 占用过高

完整示例请参考：
- `examples/gb28181_client.php` - 基础示例
- `examples/gb28181_client_manual.php` - 完整演示

