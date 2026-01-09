# TCP 传输模式支持文档

## 概述

从 Task 3 开始,php-exosip 扩展完整支持 TCP 传输模式。与 UDP 的无状态特性不同,TCP 是面向连接的协议,每个设备维护一个独立的连接(文件描述符 fd)。

## UDP vs TCP 对比

| 特性 | UDP 模式 | TCP 模式 |
|------|---------|---------|
| 连接状态 | 无状态 | 有状态 |
| 套接字 | 所有设备共享一个 socket | 每个设备一个独立 fd |
| 消息发送 | 通过 IP:Port 定位 | 通过 fd 定位 |
| 连接管理 | 不需要 | 需要绑定/解绑 |
| 断线检测 | 仅依赖心跳超时 | TCP close + 心跳超时 |
| 适用场景 | 公网、简单环境 | 内网、需要可靠传输 |

## 核心机制

### 1. 双向映射

DeviceManager 维护两个映射表:

```php
// fd → deviceId
$fdToDevice = [
    10 => '34020000001320000001',
    11 => '34020000001320000002',
];

// deviceId → fd
$deviceToFd = [
    '34020000001320000001' => 10,
    '34020000001320000002' => 11,
];
```

### 2. 生命周期

```
TCP 连接建立
    ↓ onConnect(fd)
设备发送 REGISTER
    ↓ handleRegister(event)
    ↓ bindConnection(deviceId, fd)  ← 绑定
设备通信(MESSAGE/INVITE等)
    ↓ getFdByDevice(deviceId)      ← 查询 fd
    ↓ sendMessage(deviceId, ...)
TCP 连接断开
    ↓ onClose(fd)
    ↓ unbindConnectionByFd(fd)     ← 解绑
    ↓ markOffline(deviceId)
```

## API 说明

### DeviceManager 新增方法

#### bindConnection(deviceId, fd)
```php
/**
 * 绑定设备到 TCP 连接
 * 
 * @param string $deviceId 设备ID
 * @param int $fd 文件描述符
 */
public function bindConnection(string $deviceId, int $fd): void
```

**使用场景**: 设备注册成功后立即绑定

```php
$sipServer->onRegister(function($server, $event) use ($deviceManager) {
    $deviceId = extractDeviceId($event->getFromUri());
    $fd = $event->getFd();
    
    // 添加设备
    $deviceManager->addDevice($deviceId, [...]);
    
    // TCP 模式下绑定连接
    if ($server->getConfig()['mode'] === 'tcp') {
        $deviceManager->bindConnection($deviceId, $fd);
    }
});
```

#### unbindConnectionByDevice(deviceId)
```php
/**
 * 解绑设备的 TCP 连接
 * 
 * @param string $deviceId 设备ID
 */
public function unbindConnectionByDevice(string $deviceId): void
```

**使用场景**: 设备注销时主动解绑

#### unbindConnectionByFd(fd)
```php
/**
 * 解绑 TCP 连接(通过 fd)
 * 自动标记设备离线
 * 
 * @param int $fd 文件描述符
 */
public function unbindConnectionByFd(int $fd): void
```

**使用场景**: TCP 连接断开(onClose 事件)

```php
$sipServer->onClose(function($server, $event) use ($deviceManager) {
    $fd = $event->getFd();
    $deviceManager->unbindConnectionByFd($fd);
});
```

#### getFdByDevice(deviceId)
```php
/**
 * 通过设备ID获取文件描述符
 * 
 * @param string $deviceId 设备ID
 * @return int|null 文件描述符,如果未绑定返回 null
 */
public function getFdByDevice(string $deviceId): ?int
```

**使用场景**: 发送消息前获取 fd

```php
// TCP 模式下发送消息
$fd = $deviceManager->getFdByDevice($deviceId);
if ($fd !== null) {
    $server->sendMessage($deviceId, $body, ['fd' => $fd]);
} else {
    // 设备未连接或已断开
}
```

#### getDeviceByFd(fd)
```php
/**
 * 通过文件描述符获取设备ID
 * 
 * @param int $fd 文件描述符
 * @return string|null 设备ID,如果未绑定返回 null
 */
public function getDeviceByFd(int $fd): ?string
```

#### getAllConnections()
```php
/**
 * 获取所有 TCP 连接信息
 * 
 * @return array ['device_id' => fd, ...]
 */
public function getAllConnections(): array
```

#### hasConnection(deviceId)
```php
/**
 * 检查设备是否有活跃的 TCP 连接
 * 
 * @param string $deviceId 设备ID
 * @return bool
 */
public function hasConnection(string $deviceId): bool
```

### SipEvent 新增方法

#### getFd()
```php
/**
 * 获取事件关联的文件描述符(TCP 模式)
 * 
 * @return int 文件描述符,UDP 模式返回 0
 */
public function getFd(): int
```

### ExoSip 新增事件

#### onConnect
```php
/**
 * TCP 连接建立事件
 * 
 * @param callable $callback function($server, $event)
 */
$sipServer->onConnect(function($server, $event) {
    $fd = $event->getFd();
    $conn = $event->getConnection();
    echo "New connection: fd={$fd}, from {$conn['ip']}:{$conn['port']}\n";
});
```

#### onClose
```php
/**
 * TCP 连接关闭事件
 * 
 * @param callable $callback function($server, $event)
 */
$sipServer->onClose(function($server, $event) {
    $fd = $event->getFd();
    echo "Connection closed: fd={$fd}\n";
    
    // 清理连接绑定
    $deviceManager->unbindConnectionByFd($fd);
});
```

## 完整示例

### 基础 TCP 服务器

```php
<?php
require_once 'DeviceManager.php';

$deviceManager = new DeviceManager();

$sipServer = new ExoSip([
    'mode' => 'tcp',  // ← 启用 TCP 模式
    'ip' => '0.0.0.0',
    'port' => 5060,
    // ...其他配置
]);

// 1. 新连接建立
$sipServer->onConnect(function($server, $event) {
    $fd = $event->getFd();
    echo "New TCP connection: fd={$fd}\n";
});

// 2. 设备注册(绑定连接)
$sipServer->onRegister(function($server, $event) use ($deviceManager) {
    $deviceId = extractDeviceId($event);
    $fd = $event->getFd();
    
    // 添加设备
    $deviceManager->addDevice($deviceId, [...]);
    
    // 绑定 TCP 连接
    $deviceManager->bindConnection($deviceId, $fd);
    
    $server->sendResponse($event->getTid(), 200, 'OK');
});

// 3. 消息处理
$sipServer->onMessage(function($server, $event) use ($deviceManager) {
    $deviceId = extractDeviceId($event);
    
    // 处理消息...
    $deviceManager->recordHeartbeat($deviceId);
    
    $server->sendResponse($event->getTid(), 200, 'OK');
});

// 4. 连接断开(解绑)
$sipServer->onClose(function($server, $event) use ($deviceManager) {
    $fd = $event->getFd();
    $deviceId = $deviceManager->getDeviceByFd($fd);
    
    echo "Device {$deviceId} disconnected\n";
    
    // 解绑并标记离线
    $deviceManager->unbindConnectionByFd($fd);
});

$sipServer->run();
```

### 主动发送消息(TCP)

```php
// 发送 PTZ 控制命令
function sendPtzControl($sipServer, $deviceManager, $deviceId, $command) {
    // 检查连接
    if (!$deviceManager->hasConnection($deviceId)) {
        throw new Exception("Device not connected");
    }
    
    // 获取 fd
    $fd = $deviceManager->getFdByDevice($deviceId);
    
    // 构造消息体
    $body = buildPtzXml($command);
    
    // 发送(TCP 模式下需要传递 fd)
    $result = $sipServer->sendMessage($deviceId, $body, [
        'fd' => $fd,
        'method' => 'MESSAGE',
    ]);
    
    return $result;
}
```

## GB28181Handler 集成

GB28181Handler 已自动处理 TCP 模式:

```php
$gb28181 = new GB28181Handler($sipServer, [...]);

// handleRegister 自动检测 TCP 模式并绑定连接
// handleClose 自动解绑连接并标记设备离线

// 使用 GB28181Handler 时无需手动管理连接
$sipServer->onRegister([$gb28181, 'handleRegister']);
$sipServer->onMessage([$gb28181, 'handleMessage']);
$sipServer->onClose([$gb28181, 'handleClose']);
```

## 测试

运行 TCP 模式测试:

```bash
php examples/test_tcp_mode.php
```

测试覆盖:
- ✅ TCP 连接建立 (onConnect)
- ✅ 设备注册时绑定 (bindConnection)
- ✅ 消息处理时查询 fd (getFdByDevice)
- ✅ 连接断开时解绑 (unbindConnectionByFd)
- ✅ 设备自动离线标记

## 注意事项

### 1. 模式检测

```php
$mode = $sipServer->getConfig()['mode'] ?? 'udp';
if ($mode === 'tcp' || $mode === 'tls') {
    // TCP 特有逻辑
    $fd = $event->getFd();
    $deviceManager->bindConnection($deviceId, $fd);
}
```

### 2. fd 有效性

```php
$fd = $event->getFd();
if ($fd > 0) {
    // 有效的 TCP 连接
} else {
    // UDP 模式或无效 fd
}
```

### 3. 连接重用

如果同一设备重新连接(新的 fd),`bindConnection()` 会自动解绑旧连接:

```php
// 第一次连接: deviceId ↔ fd=10
$deviceManager->bindConnection('device123', 10);

// 设备断开重连: deviceId ↔ fd=15
$deviceManager->bindConnection('device123', 15);
// 自动解绑 fd=10,绑定 fd=15
```

### 4. 并发连接

同一设备不应同时建立多个 TCP 连接。如果检测到,会记录警告并使用新连接。

### 5. 心跳超时

TCP 模式下仍需心跳检测:
- TCP close 检测物理断线(网线拔出、进程崩溃)
- 心跳超时检测逻辑断线(设备卡死但连接未断)

建议同时使用两种机制:

```php
// 1. TCP 断开立即处理
$sipServer->onClose(function($server, $event) use ($deviceManager) {
    $deviceManager->unbindConnectionByFd($event->getFd());
});

// 2. 定时检查心跳超时
$sipServer->onTimer(function() use ($deviceManager) {
    $deviceManager->checkTimeout();
});
```

## 故障排查

### Q: 设备注册成功但 fd 为 0?

A: 确认服务器配置为 TCP 模式:

```php
$sipServer = new ExoSip([
    'mode' => 'tcp',  // ← 必须设置
]);
```

### Q: 发送消息失败?

A: 检查设备是否有活跃连接:

```php
if (!$deviceManager->hasConnection($deviceId)) {
    echo "Device not connected\n";
    return;
}

$fd = $deviceManager->getFdByDevice($deviceId);
if ($fd === null) {
    echo "Device fd not found\n";
    return;
}
```

### Q: 连接断开但设备仍显示在线?

A: 确保绑定了 onClose 事件:

```php
$sipServer->onClose([$gb28181, 'handleClose']);
```

### Q: 设备重连后无法通信?

A: 检查是否正确更新了 fd 绑定:

```php
// 每次 REGISTER 都应重新绑定
$deviceManager->bindConnection($deviceId, $event->getFd());
```

## 性能考虑

- **连接数限制**: TCP 受系统 fd 上限限制,通常为几千到几万
- **内存占用**: 每个 TCP 连接需要维护缓冲区,比 UDP 占用更多内存
- **并发性**: TCP 需要为每个连接维护状态,CPU 开销略高于 UDP

建议:
- 小规模部署(<1000设备): TCP 更可靠
- 大规模部署(>5000设备): UDP 更高效
- 混合模式: 支持 TCP 和 UDP 同时监听(未来特性)

## 总结

Task 3 实现了完整的 TCP 传输模式支持,核心特性:

✅ 双向映射: `device_id ↔ fd`  
✅ 生命周期管理: 绑定、查询、解绑  
✅ 事件驱动: onConnect、onClose  
✅ 自动离线: TCP 断开自动标记设备离线  
✅ GB28181 集成: 开箱即用  
✅ 测试覆盖: 完整的测试示例  

与 Task 1(C层管道) 和 Task 2(PHP API) 配合,现在可以构建完整的 Laravel + GB28181 + TCP 网关系统。
