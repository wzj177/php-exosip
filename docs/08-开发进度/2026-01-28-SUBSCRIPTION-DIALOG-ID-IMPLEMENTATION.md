# GB28181 订阅 Dialog ID 回写功能实施说明

> 日期: 2026-01-28  
> 状态: ✅ PHP 层完成,待 Gateway 层实现响应机制

## 📋 概述

实现 GB28181 SUBSCRIBE 请求的 dialog_id 回写功能,使订阅可以正确续期。

### 核心问题

之前的实现:
- Gb28181Client 订阅方法立即返回 boolean
- Gateway 发送 SUBSCRIBE 后无法将 dialog_id 返回给 PHP
- 数据库的 `catalog_dialog_id/alarm_dialog_id/position_dialog_id` 字段一直为空
- 续订任务无法使用 dialog_id 执行 REFRESH SUBSCRIBE

### 解决方案

采用 **Redis 响应队列 + 同步等待** 机制:

```
PHP (Gb28181Client)
  │
  ├─① LPUSH gb28181:commands (推送订阅命令)
  │
  ├─② BRPOP gb28181:response:{request_id} (阻塞等待响应,5秒超时)
  │
Gateway (CommandDispatcher)
  │
  ├─③ BLPOP gb28181:commands (消费命令)
  │
  ├─④ 调用 $sipServer->subscribe() 发送 SUBSCRIBE
  │
  ├─⑤ 收到设备 200 OK,从响应中提取 dialog_id
  │
  ├─⑥ LPUSH gb28181:response:{request_id} (推送响应)
       {
         "success": true,
         "dialog_id": "abc123def456",
         "subscription_id": 12345
       }
```

---

## ✅ 已完成的修改

### 1. Gb28181Client.php - SDK 层

**文件**: `examples/gbvr-iot/CoreW/Sdk/PSipGateway/Gb28181Client.php`

#### 1.1 修改 sendCommand() 支持等待响应

```php
public function sendCommand(
    string $deviceId, 
    string $action, 
    array $params = [], 
    bool $waitResponse = false,  // 新增参数
    int $timeout = 5             // 超时时间
): bool|array
```

**关键逻辑**:
- `$waitResponse=false`: 传统模式,立即返回 bool
- `$waitResponse=true`: 阻塞等待,返回 array 响应数据

#### 1.2 新增 waitResponse() 私有方法

```php
private function waitResponse(string $requestId, int $timeout = 5): array
{
    $responseKey = "gb28181:response:{$requestId}";
    
    // 使用 BRPOP 阻塞等待响应
    $response = $this->redis->brPop([$responseKey], $timeout);
    
    if (!$response || !isset($response[1])) {
        throw new \RuntimeException("Command timeout: no response from gateway after {$timeout}s");
    }
    
    $data = json_decode($response[1], true);
    
    // 清理响应键
    $this->redis->del($responseKey);
    
    return $data;
}
```

#### 1.3 修改订阅方法返回 dialog_id

**subscribeCatalog() / subscribeAlarm() / subscribeMobilePosition()**:

```php
public function subscribeCatalog(string $deviceId, array $params = []): array
{
    $expires = $params['expires'] ?? 3600;

    try {
        $response = $this->sendCommand($deviceId, 'subscribe_catalog', [
            'expires' => $expires,
        ], true, 5); // waitResponse=true, timeout=5s
        
        return [
            'success' => true,
            'device_id' => $deviceId,
            'event_type' => 'Catalog',
            'expires' => $expires,
            'dialog_id' => $response['dialog_id'] ?? null,      // ✅ 新增
            'subscription_id' => $response['subscription_id'] ?? null,
        ];
    } catch (\Exception $e) {
        return [
            'success' => false,
            'device_id' => $deviceId,
            'event_type' => 'Catalog',
            'expires' => $expires,
            'error' => $e->getMessage(),
        ];
    }
}
```

#### 1.4 新增 refreshSubscribe() 方法

```php
/**
 * 刷新订阅(续期)
 *
 * @param string $dialogId Dialog ID (从订阅时返回)
 * @param string $eventType 事件类型 (Catalog/Alarm/MobilePosition)
 * @param int $expires 新的有效期(秒)
 * @return array
 */
public function refreshSubscribe(string $dialogId, string $eventType, int $expires = 3600): array
{
    try {
        $response = $this->sendCommand('', 'refresh_subscribe', [
            'dialog_id' => $dialogId,
            'event_type' => $eventType,
            'expires' => $expires,
        ], true, 5);
        
        return [
            'success' => true,
            'dialog_id' => $dialogId,
            'event_type' => $eventType,
            'expires' => $expires,
        ];
    } catch (\Exception $e) {
        return [
            'success' => false,
            'dialog_id' => $dialogId,
            'event_type' => $eventType,
            'error' => $e->getMessage(),
        ];
    }
}
```

---

### 2. SubscribeServiceImpl.php - 业务层

**文件**: `examples/gbvr-iot/CoreW/Business/Subscribe/Service/Impl/SubscribeServiceImpl.php`

#### 2.1 修改 applySubscribeConfig() 保存 dialog_id

**关键改动**:

```php
public function applySubscribeConfig(array $subscribeConfig): bool
{
    // ...

    $gb28181Client = $this->getGb28181Client();
    $updateFields = [];  // ✅ 用于收集要更新的字段

    // 目录订阅
    if ($subscribeConfig['event_catalog']) {
        try {
            $result = $gb28181Client->subscribeCatalog($deviceId, [
                'expires' => $subscribeConfig['subscribe_expires']
            ]);
            
            if ($result['success'] && !empty($result['dialog_id'])) {
                $updateFields['catalog_dialog_id'] = $result['dialog_id'];  // ✅ 保存 dialog_id
                Log::channel('sip')->info('目录订阅已下发', [
                    'device_id' => $deviceId,
                    'dialog_id' => $result['dialog_id']
                ]);
            }
        } catch (\Exception $e) {
            // 错误处理
        }
    } else {
        // 取消订阅时清空 dialog_id
        $gb28181Client->unsubscribeCatalog($deviceId);
        $updateFields['catalog_dialog_id'] = null;  // ✅ 清空
    }

    // 报警订阅、移动位置订阅同理...

    // 批量更新数据库
    if (!empty($updateFields)) {
        $updateFields['last_subscribed_at'] = date('Y-m-d H:i:s');
        $updateFields['subscription_expires_at'] = date('Y-m-d H:i:s', time() + $expires);
        
        $this->getDeviceSubscribeConfigDao()->update($subscribeConfig['id'], $updateFields);
    }
}
```

**改进点**:
1. 收集所有 dialog_id 字段到 `$updateFields` 数组
2. 订阅成功后保存 `catalog_dialog_id/alarm_dialog_id/position_dialog_id`
3. 取消订阅时设置为 `null`
4. 最后批量更新数据库 (减少 SQL 调用)

#### 2.2 修改 renewExpiringSubscriptions() 使用 dialog_id 续订

**关键改动**:

```php
public function renewExpiringSubscriptions(string $expireTime): int
{
    $configs = $this->getDeviceSubscribeConfigDao()->findExpiringConfigs($expireTime);
    $renewed = 0;
    $gb28181Client = $this->getGb28181Client();

    foreach ($configs as $config) {
        if (!$config['auto_renew'] || $config['status'] != 1) {
            continue;
        }
        
        $hasDialogId = false;

        try {
            // 目录订阅续期
            if ($config['event_catalog'] && !empty($config['catalog_dialog_id'])) {
                $result = $gb28181Client->refreshSubscribe(
                    $config['catalog_dialog_id'],  // ✅ 使用 dialog_id
                    'Catalog',
                    $config['subscribe_expires']
                );
                
                if ($result['success']) {
                    $hasDialogId = true;
                    Log::channel('sip')->info('目录订阅续期成功', [
                        'device_id' => $config['device_id'],
                        'dialog_id' => $config['catalog_dialog_id']
                    ]);
                }
            }

            // 报警、位置订阅同理...

            // 如果没有 dialog_id,重新执行完整订阅流程
            if (!$hasDialogId) {
                Log::channel('sip')->info('没有 dialog_id,重新执行订阅', [
                    'device_id' => $config['device_id']
                ]);
                $this->applySubscribeConfig($config);
            } else {
                // 仅更新过期时间
                $this->getDeviceSubscribeConfigDao()->update($config['id'], [
                    'subscription_expires_at' => date('Y-m-d H:i:s', time() + $config['subscribe_expires'])
                ]);
            }

            $renewed++;
        } catch (\Exception $e) {
            Log::channel('sip')->error('续订失败', [
                'device_id' => $config['device_id'],
                'error' => $e->getMessage()
            ]);
        }
    }

    return $renewed;
}
```

**改进点**:
1. 优先使用 `refreshSubscribe(dialog_id)` 执行 REFRESH SUBSCRIBE (高效)
2. 如果 `dialog_id` 为空,降级为完整订阅流程 (兼容旧数据)
3. 续订成功后仅更新 `subscription_expires_at` 字段

---

### 3. SubscribeService.php - 接口层

**文件**: `examples/gbvr-iot/CoreW/Business/Subscribe/Service/SubscribeService.php`

#### 修复接口签名

```php
// 修改前
public function renewExpiringSubscriptions(): array;

// 修改后
public function renewExpiringSubscriptions(string $expireTime): int;
```

**说明**: 使实现类与接口签名一致

---

## ❌ 待实现的工作

### Gateway 层响应机制

**需要在 Gateway 的 CommandDispatcher 中实现**:

#### 1. handleSubscribeCatalog() 响应逻辑

```php
private function handleSubscribeCatalog(string $requestId, string $deviceId, array $params): array
{
    $expires = $params['expires'] ?? 3600;
    
    try {
        // 调用 SIP 层发送 SUBSCRIBE
        $subscriptionId = $this->sipServer->subscribe(
            "sip:{$deviceId}@{$ip}:{$port}",
            'Catalog',
            $expires
        );
        
        if ($subscriptionId < 0) {
            throw new \RuntimeException("Failed to send SUBSCRIBE");
        }
        
        // ⚠️ 这里需要等待 SUBSCRIBE 200 OK 响应
        // 从响应中提取 dialog_id
        $dialogId = $this->waitForSubscribeResponse($subscriptionId, 5); // 阻塞等待5秒
        
        // ✅ 推送响应到 Redis
        $response = [
            'success' => true,
            'dialog_id' => $dialogId,
            'subscription_id' => $subscriptionId,
            'request_id' => $requestId,
        ];
        
        $this->redis->lPush("gb28181:response:{$requestId}", json_encode($response));
        
        // 设置过期时间(防止内存泄漏)
        $this->redis->expire("gb28181:response:{$requestId}", 10);
        
        return $response;
        
    } catch (\Exception $e) {
        // 推送错误响应
        $errorResponse = [
            'success' => false,
            'error' => $e->getMessage(),
            'request_id' => $requestId,
        ];
        
        $this->redis->lPush("gb28181:response:{$requestId}", json_encode($errorResponse));
        $this->redis->expire("gb28181:response:{$requestId}", 10);
        
        throw $e;
    }
}
```

#### 2. waitForSubscribeResponse() 实现

**方案 A: 使用 eXosip 事件循环** (推荐)

```php
private function waitForSubscribeResponse(int $subscriptionId, int $timeout = 5): ?string
{
    $startTime = time();
    
    while (time() - $startTime < $timeout) {
        $event = $this->sipServer->getEvent(100); // 100ms 超时
        
        if (!$event) {
            continue;
        }
        
        // 检查是否是 SUBSCRIBE 200 OK 响应
        if ($event->getType() === EXOSIP_SUBSCRIPTION_ANSWERED) {
            if ($event->getSubscriptionId() === $subscriptionId) {
                // 从 SIP 响应中提取 dialog_id
                $dialogId = $event->getDialogId();
                return $dialogId;
            }
        }
    }
    
    throw new \RuntimeException("Timeout waiting for SUBSCRIBE response");
}
```

**方案 B: 使用 Promise/Future 模式**

```php
// 在发送 SUBSCRIBE 前注册回调
$promise = new Promise();
$this->subscriptionPromises[$subscriptionId] = $promise;

// 在 onSubscriptionAnswered 回调中解析
$this->sipServer->onSubscriptionAnswered = function($event) {
    $subscriptionId = $event->getSubscriptionId();
    if (isset($this->subscriptionPromises[$subscriptionId])) {
        $dialogId = $event->getDialogId();
        $this->subscriptionPromises[$subscriptionId]->resolve($dialogId);
    }
};

// 等待 Promise
$dialogId = $promise->wait(5000); // 5秒超时
```

#### 3. handleRefreshSubscribe() 实现

```php
private function handleRefreshSubscribe(string $requestId, array $params): array
{
    $dialogId = $params['dialog_id'] ?? null;
    $eventType = $params['event_type'] ?? null;
    $expires = $params['expires'] ?? 3600;
    
    if (!$dialogId || !$eventType) {
        throw new \InvalidArgumentException("Missing dialog_id or event_type");
    }
    
    try {
        // 调用 eXosip refreshSubscribe
        $result = $this->sipServer->refreshSubscribe($dialogId, $expires);
        
        if (!$result) {
            throw new \RuntimeException("Failed to refresh subscription");
        }
        
        // 推送成功响应
        $response = [
            'success' => true,
            'dialog_id' => $dialogId,
            'request_id' => $requestId,
        ];
        
        $this->redis->lPush("gb28181:response:{$requestId}", json_encode($response));
        $this->redis->expire("gb28181:response:{$requestId}", 10);
        
        return $response;
        
    } catch (\Exception $e) {
        // 推送错误响应
        $errorResponse = [
            'success' => false,
            'error' => $e->getMessage(),
            'request_id' => $requestId,
        ];
        
        $this->redis->lPush("gb28181:response:{$requestId}", json_encode($errorResponse));
        $this->redis->expire("gb28181:response:{$requestId}", 10);
        
        throw $e;
    }
}
```

#### 4. CommandDispatcher dispatch() 路由

```php
public function dispatch(array $command): void
{
    $action = $command['action'] ?? '';
    $requestId = $command['request_id'] ?? '';
    
    try {
        switch ($action) {
            case 'subscribe_catalog':
                $this->handleSubscribeCatalog($requestId, $command['device_id'], $command['params']);
                break;
                
            case 'subscribe_alarm':
                $this->handleSubscribeAlarm($requestId, $command['device_id'], $command['params']);
                break;
                
            case 'subscribe_mobile_position':
                $this->handleSubscribeMobilePosition($requestId, $command['device_id'], $command['params']);
                break;
                
            case 'refresh_subscribe':
                $this->handleRefreshSubscribe($requestId, $command['params']);
                break;
                
            // 其他命令...
        }
    } catch (\Exception $e) {
        Log::error("Command dispatch failed", [
            'action' => $action,
            'request_id' => $requestId,
            'error' => $e->getMessage()
        ]);
    }
}
```

---

## 🔄 完整流程示例

### 初次订阅流程

```
[PHP API]
  │
  ├─ 1. 用户在后台开启目录订阅
  │
  ├─ 2. SubscribeServiceImpl.applySubscribeConfig()
  │     ├─ Gb28181Client.subscribeCatalog()
  │     │   ├─ sendCommand('subscribe_catalog', waitResponse=true)
  │     │   │   ├─ LPUSH gb28181:commands (推送命令)
  │     │   │   └─ BRPOP gb28181:response:{req_abc123} (阻塞等待)
  │     │   │
  │
[Gateway]
  │     │   │
  │     │   ├─ 3. LongTask: BLPOP gb28181:commands (消费命令)
  │     │   │
  │     │   ├─ 4. CommandDispatcher.handleSubscribeCatalog()
  │     │   │   ├─ $sipServer->subscribe() → 发送 SUBSCRIBE
  │     │   │   ├─ waitForSubscribeResponse() → 等待 200 OK
  │     │   │   │   ├─ 提取 dialog_id = "abc123def456"
  │     │   │   │   └─ LPUSH gb28181:response:{req_abc123}
  │     │   │   │       {
  │     │   │   │         "success": true,
  │     │   │   │         "dialog_id": "abc123def456",
  │     │   │   │         "subscription_id": 12345
  │     │   │   │       }
  │     │   │   │
[PHP API]
  │     │   │
  │     │   ├─ 5. BRPOP 返回响应数据
  │     │   └─ 6. 返回 ['success' => true, 'dialog_id' => 'abc123def456']
  │     │
  │     ├─ 7. 保存 dialog_id 到数据库
  │     │   UPDATE gv_device_subscribe_config SET
  │     │     catalog_dialog_id = 'abc123def456',
  │     │     last_subscribed_at = NOW(),
  │     │     subscription_expires_at = NOW() + 3600
  │     │
  │     └─ ✅ 订阅完成
```

### 续订流程 (10分钟后)

```
[Cron Task]
  │
  ├─ 1. SubscriptionRenewTask 定时执行
  │
  ├─ 2. SubscribeServiceImpl.renewExpiringSubscriptions()
  │     ├─ 查询 subscription_expires_at < NOW() + 300 (5分钟内过期)
  │     │
  │     ├─ 发现 device_id=34020000001320000001
  │     │   catalog_dialog_id = 'abc123def456'
  │     │
  │     ├─ 3. Gb28181Client.refreshSubscribe('abc123def456', 'Catalog', 3600)
  │     │   ├─ sendCommand('refresh_subscribe', waitResponse=true)
  │     │   │   ├─ LPUSH gb28181:commands
  │     │   │   └─ BRPOP gb28181:response:{req_def789}
  │     │   │
[Gateway]
  │     │   │
  │     │   ├─ 4. CommandDispatcher.handleRefreshSubscribe()
  │     │   │   ├─ $sipServer->refreshSubscribe(dialog_id, expires)
  │     │   │   │   → 发送 SUBSCRIBE with Expires: 3600
  │     │   │   │      (在已有 dialog 上续期)
  │     │   │   │
  │     │   │   └─ LPUSH gb28181:response:{req_def789}
  │     │   │       {"success": true, "dialog_id": "abc123def456"}
  │     │   │
[PHP API]
  │     │   │
  │     │   ├─ 5. BRPOP 返回成功
  │     │   └─ 6. 返回 ['success' => true]
  │     │
  │     ├─ 7. 更新过期时间
  │     │   UPDATE gv_device_subscribe_config SET
  │     │     subscription_expires_at = NOW() + 3600
  │     │
  │     └─ ✅ 续订完成 (无需重新保存 dialog_id)
```

---

## 📊 数据库字段使用说明

### gv_device_subscribe_config 表

| 字段 | 类型 | 说明 | 使用场景 |
|------|------|------|----------|
| `catalog_dialog_id` | VARCHAR(64) | 目录订阅的 Dialog ID | 初次订阅时保存,续订时使用 |
| `alarm_dialog_id` | VARCHAR(64) | 报警订阅的 Dialog ID | 同上 |
| `position_dialog_id` | VARCHAR(64) | 位置订阅的 Dialog ID | 同上 |
| `last_subscribed_at` | DATETIME | 最后订阅时间 | 记录初次订阅和续订时间 |
| `subscription_expires_at` | DATETIME | 订阅过期时间 | 续订任务查询条件 |

**生命周期**:

1. **初次订阅**: 
   - `catalog_dialog_id` 从 `NULL` → `"abc123def456"`
   - `last_subscribed_at` = NOW()
   - `subscription_expires_at` = NOW() + 3600

2. **续订**:
   - `catalog_dialog_id` 保持不变 (除非重新订阅)
   - `subscription_expires_at` = NOW() + 3600

3. **取消订阅**:
   - `catalog_dialog_id` = NULL
   - `subscription_expires_at` = NULL

4. **重新订阅**:
   - `catalog_dialog_id` 更新为新值 (新的 dialog)

---

## ⚠️ 关键注意事项

### 1. 超时处理

**问题**: 如果设备离线或网络不通,SUBSCRIBE 请求会超时。

**解决**:
- PHP 层设置 5 秒超时 (BRPOP)
- Gateway 层等待 SUBSCRIBE 200 OK 也设置 5 秒超时
- 超时后返回错误响应: `{"success": false, "error": "Timeout"}`
- PHP 捕获异常后记录日志,跳过该设备

### 2. Dialog ID 失效

**场景**: 
- 设备重启后 dialog_id 失效
- 续订时 REFRESH SUBSCRIBE 返回 481 Call/Transaction Does Not Exist

**解决**:
```php
// renewExpiringSubscriptions() 中已处理
try {
    $result = $gb28181Client->refreshSubscribe($dialogId, ...);
    if (!$result['success']) {
        // 降级为完整订阅流程
        $this->applySubscribeConfig($config);
    }
} catch (\Exception $e) {
    // 重新订阅
    $this->applySubscribeConfig($config);
}
```

### 3. 并发问题

**问题**: 多个进程同时续订同一设备。

**解决**: 使用 Redis 分布式锁

```php
$lockKey = "subscription_lock:{$deviceId}";
$lockAcquired = $this->redis->set($lockKey, '1', ['NX', 'EX' => 10]);

if (!$lockAcquired) {
    Log::info("Device subscription is locked by another process", ['device_id' => $deviceId]);
    return;
}

try {
    // 执行续订逻辑
} finally {
    $this->redis->del($lockKey);
}
```

### 4. Redis 响应键过期

**问题**: 响应键未清理导致内存泄漏。

**解决**: 已在 PHP 层设置自动清理

```php
// Gb28181Client::waitResponse()
$this->redis->del($responseKey); // BRPOP 后立即删除
```

Gateway 层也应设置过期:

```php
$this->redis->expire("gb28181:response:{$requestId}", 10); // 10秒后自动过期
```

### 5. 请求 ID 冲突

**问题**: `uniqid('req_', true)` 生成的 ID 可能重复。

**解决**: 使用更强的唯一性保证

```php
// 改进版
$requestId = sprintf('req_%s_%d', uniqid('', true), getmypid());
```

---

## 🧪 测试建议

### 单元测试

```php
// tests/Unit/Gb28181ClientTest.php

public function testSubscribeCatalogReturnsDialogId()
{
    // Mock Redis 响应
    $this->redis->shouldReceive('brPop')
        ->once()
        ->with(['gb28181:response:req_test123'], 5)
        ->andReturn([
            'gb28181:response:req_test123',
            json_encode([
                'success' => true,
                'dialog_id' => 'test_dialog_123',
                'subscription_id' => 456
            ])
        ]);
    
    $client = new Gb28181Client($this->redis, $this->config);
    $result = $client->subscribeCatalog('34020000001320000001');
    
    $this->assertTrue($result['success']);
    $this->assertEquals('test_dialog_123', $result['dialog_id']);
}

public function testSubscribeTimeout()
{
    $this->redis->shouldReceive('brPop')
        ->once()
        ->andReturn(null); // 超时
    
    $this->expectException(\RuntimeException::class);
    $this->expectExceptionMessage('Command timeout');
    
    $client = new Gb28181Client($this->redis, $this->config);
    $client->subscribeCatalog('34020000001320000001');
}
```

### 集成测试

```php
// tests/Integration/SubscriptionFlowTest.php

public function testFullSubscriptionFlow()
{
    // 1. 模拟设备在线
    $this->mockDeviceOnline('34020000001320000001');
    
    // 2. 创建订阅配置
    $config = $this->subscribeService->saveSubscribeConfig(
        '34020000001320000001',
        null,
        ['event_catalog' => true, 'subscribe_expires' => 3600]
    );
    
    // 3. 验证 dialog_id 已保存
    $this->assertNotEmpty($config['catalog_dialog_id']);
    
    // 4. 模拟时间流逝 (接近过期)
    $this->travel(55)->minutes();
    
    // 5. 触发续订任务
    $renewed = $this->subscribeService->renewExpiringSubscriptions(
        date('Y-m-d H:i:s', time() + 300)
    );
    
    $this->assertEquals(1, $renewed);
    
    // 6. 验证过期时间已更新
    $configAfterRenew = $this->subscribeService->getSubscribeConfig('34020000001320000001');
    $this->assertGreaterThan(
        time() + 3000,
        strtotime($configAfterRenew['subscription_expires_at'])
    );
}
```

---

## 📝 后续优化建议

### 1. 异步化改进

当前方案使用同步等待 (BRPOP),会阻塞 5 秒。可改用异步:

```php
// 立即返回,后台处理
$client->subscribeCatalogAsync($deviceId, function($result) {
    if ($result['success']) {
        // 更新数据库
        $this->dao->update($id, ['catalog_dialog_id' => $result['dialog_id']]);
    }
});
```

### 2. 批量订阅优化

当前是一个一个设备串行处理,可改为批量:

```php
// 一次推送多个订阅命令
$client->batchSubscribe([
    ['device_id' => 'xxx1', 'event_type' => 'Catalog'],
    ['device_id' => 'xxx2', 'event_type' => 'Alarm'],
]);

// Gateway 批量返回
$responses = $client->waitBatchResponse($requestIds, 10); // 10秒等待所有响应
```

### 3. 健康检查

定期检查订阅状态:

```php
// Gb28181SubscriptionTask.php (hourly)
public function execute(): void
{
    // 检查 dialog_id 是否仍然有效
    $configs = $this->dao->getAllActiveSubscriptions();
    
    foreach ($configs as $config) {
        if (!$this->isDialogValid($config['catalog_dialog_id'])) {
            // Dialog 失效,重新订阅
            $this->subscribeService->applySubscribeConfig($config);
        }
    }
}
```

---

## 📚 相关文档

- [GB28181 订阅功能支持状态](../03-功能实现/GB28181 订阅功能支持状态.md)
- [Master-Worker-Task 架构](../02-架构设计/MASTER_WORKER_TASK.md)
- [Long Task Redis 订阅完整示例](../03-功能实现/Long Task Redis 订阅完整示例.md)
- [设备配置扩展方案](../03-功能实现/设备配置扩展方案.md)

---

**实施完成度**: 80%

- ✅ PHP 层 (Gb28181Client, SubscribeServiceImpl, Interface)
- ❌ Gateway 层 (CommandDispatcher, waitForSubscribeResponse)
- ❌ 测试用例
- ❌ 文档完善

**预计剩余工作量**: 2-3 小时 (Gateway 响应机制实现)
