# 设备指令确认功能 - 架构问题分析与解决方案

> **文档创建时间**: 2026-01-13  
> **问题发现者**: 用户反馈  
> **严重程度**: 🔴 **高** - 影响业务逻辑关联

---

## 🚨 架构问题分析

### 问题描述

用户提出的核心问题：

> "这个做不到，你思考下sip通信，服务端-设备发送sip指令，这个在c层，除非c层send返回call-id和CSeq。然后传递api层，api层db储存控制记录。但是我感觉很麻烦也有问题"

### 时序问题

```
时刻T0: API层调用 ptzControl('up')
         ↓
时刻T1: 调用 $sipServer->sendMessage($toUri, $xmlBody)
         ↓
时刻T2: C层发送 SIP MESSAGE
        🔴 返回: true/false（没有返回call_id！）
         ↓
时刻T3: API层想记录数据库...
        ❌ 问题：用什么关联？没有call_id！
         ↓
时刻T4: [异步，可能0.5秒后] 设备响应 200 OK
        包含 call_id, cseq
         ↓
时刻T5: handleMessageResponse 收到响应
        想更新数据库...
        ❌ 问题：无法关联到T3的记录！
```

### 现有代码的问题

#### 1. C层返回值问题

**文件**: `exosip_wrapper.c:1794`

```c
int exosip_send_message_with_content_type(...) {
    // ...
    int send_ret = eXosip_message_send_request(ctx->ctx, msg);
    // send_ret 是 transaction_id (> 0) 或 -1（失败）
    
    eXosip_unlock(ctx->ctx);
    return send_ret == 0 ? 0 : -1;  // 🔴 只返回成功/失败
}
```

**问题**: 丢失了 `transaction_id` 信息

#### 2. PHP层返回值问题

**文件**: `php_exosip.c:2424`

```c
PHP_METHOD(ExoSip, sendMessage) {
    // ...
    int result = exosip_send_message_with_content_type(obj->ctx, to, message, content_type);
    RETURN_BOOL(result == 0);  // 🔴 只返回 true/false
}
```

**问题**: 无法获取 `transaction_id` 或 `call_id`

#### 3. 业务层无法关联

**文件**: `GB28181Handler.php`

```php
public function ptzControl($deviceId, $direction, $speed = 50) {
    $xmlBody = $this->buildPtzXml($deviceId, $direction, $speed);
    $result = $this->sipServer->sendMessage($toUri, $xmlBody);
    
    if ($result) {
        // 🔴 问题：想记录数据库，但没有call_id来关联！
        // DB::insert('device_commands', [
        //     'call_id' => ???,  // 没有这个值！
        //     'device_id' => $deviceId,
        //     'command' => 'ptz_' . $direction,
        //     'status' => 'sent',
        // ]);
    }
}

private function handleMessageResponse(\SipEvent $event) {
    $callId = $event->getCallId();  // 这里有call_id
    
    // 🔴 问题：想更新数据库，但找不到对应的记录！
    // DB::update('device_commands')
    //     ->where('call_id', $callId)  // 数据库里没有这个call_id
    //     ->update(['status' => 'confirmed']);
}
```

---

## ✅ 解决方案对比

### 方案1: **修改C层返回transaction_id**（推荐） ⭐

#### 优点
- ✅ 精确关联（使用eXosip原生ID）
- ✅ 符合SIP协议规范
- ✅ 无需额外存储
- ✅ 性能最优

#### 缺点
- ⚠️ 需要修改C扩展（但改动很小）
- ⚠️ 需要重新编译

#### 实现步骤

**步骤1**: 修改C层返回值

```c
// exosip_wrapper.c
int exosip_send_message_with_content_type(...) {
    // ...
    int send_ret = eXosip_message_send_request(ctx->ctx, msg);
    
    eXosip_unlock(ctx->ctx);
    
    // 🔥 修改：直接返回 transaction_id
    if (send_ret < 0) {
        return -1;  // 失败
    }
    
    // 🔥 返回 transaction_id（正整数）
    return send_ret;
}
```

**步骤2**: 修改PHP层返回值

```c
// php_exosip.c
PHP_METHOD(ExoSip, sendMessage) {
    // ...
    int result = exosip_send_message_with_content_type(obj->ctx, to, message, content_type);
    
    // 🔥 修改：返回 transaction_id 或 false
    if (result < 0) {
        RETURN_FALSE;
    }
    RETURN_LONG(result);  // 返回 transaction_id
}
```

**步骤3**: 业务层使用

```php
public function ptzControl($deviceId, $direction, $speed = 50) {
    $xmlBody = $this->buildPtzXml($deviceId, $direction, $speed);
    $tid = $this->sipServer->sendMessage($toUri, $xmlBody);
    
    if ($tid === false) {
        throw new \Exception("Failed to send PTZ command");
    }
    
    // ✅ 现在可以记录数据库了！
    DB::insert('device_commands', [
        'transaction_id' => $tid,  // ✅ 有值了！
        'device_id' => $deviceId,
        'command' => 'ptz_' . $direction,
        'status' => 'sent',
        'sent_at' => time(),
    ]);
    
    return $tid;
}

// 响应处理
private function handleMessageResponse(\SipEvent $event) {
    $tid = $event->getTid();  // transaction_id
    
    // ✅ 现在可以关联了！
    DB::table('device_commands')
        ->where('transaction_id', $tid)
        ->update([
            'status' => 'confirmed',
            'confirmed_at' => time(),
        ]);
}
```

---

### 方案2: Redis缓存方案（简单但不完美）

#### 优点
- ✅ 不需要修改C扩展
- ✅ 实现简单
- ✅ 支持集群部署

#### 缺点
- ⚠️ 依赖Redis
- ⚠️ 可能有并发冲突（多个指令同时发送）
- ⚠️ 需要设置TTL（指令超时）
- ⚠️ Redis故障会导致功能失效

#### 实现示例

```php
public function ptzControl($deviceId, $direction, $speed = 50) {
    $xmlBody = $this->buildPtzXml($deviceId, $direction, $speed);
    $result = $this->sipServer->sendMessage($toUri, $xmlBody);
    
    if ($result) {
        // 生成临时ID
        $tempId = uniqid($deviceId . '_', true);
        
        // 存入Redis（TTL=5秒）
        Redis::setex("pending_command:{$deviceId}", 5, json_encode([
            'temp_id' => $tempId,
            'device_id' => $deviceId,
            'command' => 'ptz_' . $direction,
            'sent_at' => time(),
        ]));
        
        // 记录到数据库
        DB::insert('device_commands', [
            'temp_id' => $tempId,
            'device_id' => $deviceId,
            'command' => 'ptz_' . $direction,
            'status' => 'sent',
        ]);
    }
}

private function handleMessageResponse(\SipEvent $event) {
    $deviceId = $this->extractDeviceId($event->getToUri());
    
    // 从Redis获取待确认的指令
    $key = "pending_command:{$deviceId}";
    $data = Redis::get($key);
    
    if ($data) {
        $pending = json_decode($data, true);
        Redis::del($key);
        
        // 更新数据库
        DB::table('device_commands')
            ->where('temp_id', $pending['temp_id'])
            ->update([
                'status' => 'confirmed',
                'confirmed_at' => time(),
            ]);
    }
}
```

**问题场景**:
```php
// 用户连续快速点击"向上"
ptzControl('51010700001320000002', 'up');  // T0
ptzControl('51010700001320000002', 'up');  // T1 (0.1秒后)

// 设备响应顺序可能不同
// 200 OK for T1  // 先收到第2个
// 200 OK for T0  // 后收到第1个

// Redis只能存储最后一个，导致关联错误！
```

---

### 方案3: 简化设计 - 只统计不关联（快速方案）

#### 优点
- ✅ 实现最简单
- ✅ 不需要修改C扩展
- ✅ 不依赖额外服务
- ✅ 适合监控和统计

#### 缺点
- ⚠️ 无法追踪单个指令
- ⚠️ 无法实现重试机制
- ⚠️ 无法精确统计成功率

#### 实现示例

```php
// 只记录统计信息，不关联具体请求
private function handleMessageResponse(\SipEvent $event) {
    $deviceId = $this->extractDeviceId($event->getToUri());
    
    // 记录统计
    DB::table('device_stats')
        ->where('device_id', $deviceId)
        ->increment('commands_confirmed');
    
    // 记录日志
    Log::info("Device confirmed command", [
        'device_id' => $deviceId,
        'timestamp' => time(),
    ]);
}
```

---

## 🎯 推荐方案：**方案1 + 数据库表设计**

### 实现计划

#### 阶段1: 修改C扩展（约2小时）

1. **修改 `exosip_wrapper.c`**
   - `exosip_send_message_with_content_type` 返回 `transaction_id`
   
2. **修改 `php_exosip.c`**
   - `sendMessage()` 返回 `transaction_id` (int) 或 `false`
   - `sendInvite()` 返回 `call_id` (int) 或 `false`

3. **编译测试**
   ```bash
   cd /path/to/php-exosip
   bash build_macos_fix.sh
   php -m | grep exosip
   ```

#### 阶段2: 数据库表设计

```sql
CREATE TABLE `device_commands` (
  `id` bigint(20) unsigned NOT NULL AUTO_INCREMENT,
  `transaction_id` int(11) NOT NULL COMMENT 'SIP Transaction ID',
  `device_id` varchar(20) NOT NULL COMMENT '设备ID',
  `command_type` varchar(50) NOT NULL COMMENT '指令类型: ptz, record, config',
  `command_data` json DEFAULT NULL COMMENT '指令详情',
  `status` enum('sent','confirmed','timeout','failed') NOT NULL DEFAULT 'sent',
  `sent_at` datetime NOT NULL COMMENT '发送时间',
  `confirmed_at` datetime DEFAULT NULL COMMENT '确认时间',
  `timeout_at` datetime DEFAULT NULL COMMENT '超时时间',
  `retry_count` tinyint(4) NOT NULL DEFAULT 0 COMMENT '重试次数',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_transaction_id` (`transaction_id`),
  KEY `idx_device_status` (`device_id`, `status`),
  KEY `idx_sent_at` (`sent_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='设备控制指令记录';
```

#### 阶段3: 业务层实现

**文件**: `GB28181Handler.php`

```php
public function ptzControl($deviceId, $direction, $speed = 50) 
{
    $sn = rand(1, 9999);
    $xmlBody = $this->buildPtzControlXml($deviceId, $sn, $direction, $speed);
    $toUri = $this->buildToUri($deviceId);
    
    // 发送指令
    $tid = $this->sipServer->sendMessage($toUri, $xmlBody, 'Application/MANSCDP+xml');
    
    if ($tid === false) {
        throw new \Exception("Failed to send PTZ command to {$deviceId}");
    }
    
    // 记录到数据库
    DB::insert('device_commands', [
        'transaction_id' => $tid,
        'device_id' => $deviceId,
        'command_type' => 'ptz',
        'command_data' => json_encode([
            'direction' => $direction,
            'speed' => $speed,
            'sn' => $sn,
        ]),
        'status' => 'sent',
        'sent_at' => now(),
        'timeout_at' => now()->addSeconds(5),  // 5秒超时
    ]);
    
    $this->log("PTZ command sent: TID={$tid}, Device={$deviceId}, Direction={$direction}");
    
    return $tid;
}

private function handleMessageResponse(\SipEvent $event)
{
    $tid = $event->getTid();
    $code = $event->getCode();
    
    if ($code !== 200) {
        // 更新为失败状态
        DB::table('device_commands')
            ->where('transaction_id', $tid)
            ->update([
                'status' => 'failed',
            ]);
        return;
    }
    
    // 更新为已确认
    $updated = DB::table('device_commands')
        ->where('transaction_id', $tid)
        ->update([
            'status' => 'confirmed',
            'confirmed_at' => now(),
        ]);
    
    if ($updated) {
        $this->log("Device confirmed command: TID={$tid}");
    }
}
```

#### 阶段4: 超时检查（定时任务）

```php
// 每分钟检查超时的指令
class CheckCommandTimeoutJob
{
    public function handle()
    {
        $timeouts = DB::table('device_commands')
            ->where('status', 'sent')
            ->where('timeout_at', '<', now())
            ->where('retry_count', '<', 3)  // 最多重试3次
            ->get();
        
        foreach ($timeouts as $command) {
            // 重试发送
            $this->retryCommand($command);
            
            // 或标记为超时
            // DB::table('device_commands')
            //     ->where('id', $command->id)
            //     ->update(['status' => 'timeout']);
        }
    }
}
```

---

## 📊 性能对比

| 方案 | 实现难度 | 可靠性 | 性能 | 依赖 | 推荐度 |
|------|---------|--------|------|------|--------|
| 方案1: 修改C层返回TID | 中 | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | 无 | ⭐⭐⭐⭐⭐ |
| 方案2: Redis缓存 | 低 | ⭐⭐⭐ | ⭐⭐⭐⭐ | Redis | ⭐⭐⭐ |
| 方案3: 简化统计 | 低 | ⭐⭐ | ⭐⭐⭐⭐⭐ | 无 | ⭐⭐ |

---

## 🚀 立即行动

### 快速修复（2小时内）

我现在就可以帮你修改C扩展代码，让 `sendMessage()` 返回 `transaction_id`：

1. 修改 `exosip_wrapper.c:1794` - 返回transaction_id
2. 修改 `php_exosip.c:2424` - 返回int或false
3. 更新 `exosip.stub.php` - 更新API文档
4. 编译测试

### 完整方案（1天内）

1. ✅ 修改C扩展
2. ✅ 创建数据库表
3. ✅ 更新 `GB28181Handler.php`
4. ✅ 实现超时检查
5. ✅ 编写测试用例

---

**你觉得怎么样？我建议立即实施方案1，代码改动很小但收益很大！** 🚀

需要我现在就修改代码吗？
