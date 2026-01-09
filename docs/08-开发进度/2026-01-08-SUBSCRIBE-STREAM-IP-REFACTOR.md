# SUBSCRIBE功能 + 收流IP架构重构进度

**日期**: 2026-01-08  
**版本**: v2.4.0 准备中  
**开发阶段**: Phase 1 底层能力建设 (已完成)

---

## 一、完成的工作

### 1.1 C 扩展层 SUBSCRIBE 支持 (已完成)

**修改文件**: 
- `exosip_wrapper.c` (+200行)
- `exosip_wrapper.h` (+50行)
- `php_exosip.c` (+150行)

**新增函数**:
```c
// SUBSCRIBE/NOTIFY 核心函数
int sip_send_subscribe(SipContext *ctx, const char *to_uri, const char *event_type, 
                       int expires, const char *xml_body);
int sip_refresh_subscribe(SipContext *ctx, int subscription_id, int expires);
int sip_cancel_subscribe(SipContext *ctx, int subscription_id);
int sip_send_notify_response(SipContext *ctx, int tid, int code);
SubscriptionInfo* sip_get_subscription(SipContext *ctx, int subscription_id);
SubscriptionInfo* sip_find_subscription(SipContext *ctx, const char *device_id, const char *event_type);
void sip_get_all_subscriptions(SipContext *ctx, zval *subscriptions_array);
void sip_cleanup_expired_subscriptions(SipContext *ctx);
```

**新增数据结构**:
```c
typedef struct {
    int subscription_id;      // eXosip subscription ID
    int dialog_id;           // Dialog ID (从 200 OK 中提取)
    char device_id[64];      // 设备 ID
    char event_type[32];     // 事件类型 (Catalog, Alarm, MobilePosition)
    int expires;             // 订阅有效期（秒）
    time_t created_at;       // 创建时间
    time_t last_refresh;     // 上次刷新时间
    time_t next_refresh;     // 下次刷新时间（expires - 300秒）
} SubscriptionInfo;
```

**新增 PHP 方法**:
```php
ExoSip::subscribe(string $toUri, string $eventType, int $expires = 3600, ?string $xmlBody = null): int|false
ExoSip::refreshSubscribe(int $subscriptionId, int $expires = 3600): bool
ExoSip::cancelSubscribe(int $subscriptionId): bool
ExoSip::getSubscriptions(): array
ExoSip::sendNotifyResponse(int $tid, int $code): bool
```

---

### 1.2 GB28181Handler.php 核心功能 (已完成)

**新增方法**:
```php
// 订阅管理
public function subscribeCatalog(string $deviceId, int $expires = 3600): int|false
public function subscribeAlarm(string $deviceId, int $expires = 3600): int|false  
public function subscribeMobilePosition(string $deviceId, int $expires = 3600): int|false
public function refreshSubscriptions(): void

// NOTIFY 处理
public function handleNotify(SipEvent $event): void

// 辅助方法
private function normalizeXmlEncoding(string $xml): string
private function parseNotifyBody(string $body, string $eventType): array
```

**事件绑定**:
```php
$handler->bindEvents();  // 自动绑定 onSubscribe 和 onNotify
```

---

### 1.3 架构调整：收流IP管理 (已完成)

**移除内容**:
- 设备表 `media_host` 字段
- Device.php 中 `mediaHost` 属性和相关方法
- 数据库迁移文件中的 media_host 字段

**新架构**:
```
通道 (gv_channels)
    └─> media_server_id
           └─> 流媒体服务器 (gv_media_servers)
                  ├─> host (管理API地址)
                  └─> stream_ip (收流IP，用于SDP)
```

**需要的数据库迁移**:
```sql
ALTER TABLE `gv_media_servers` 
ADD COLUMN `stream_ip` VARCHAR(64) NOT NULL DEFAULT '' 
COMMENT '收流IP（用于SDP，为空则使用host）' AFTER `host`;
```

**关键逻辑**:
```php
// GB28181StreamTrait.php
$streamIp = !empty($mediaServer['stream_ip']) 
    ? $mediaServer['stream_ip'] 
    : $mediaServer['host'];

// 传递到信令网关
$result = $this->getGb28181Service()->startLiveVideo(
    $deviceId, $channelId, $ssrc, $zlmPort, $tcpMode, $streamId,
    $streamIp  // 第7个参数
);
```

---

### 1.4 文档更新 (已完成)

**已更新文档**:
- `docs/03-功能实现/SUBSCRIBE_FEATURE_STATUS.md` - C层和PHP层实现标记为已完成
- `docs/03-功能实现/DEVICE_CONFIG_EXTEND.md` - 移除 media_host，添加收流IP架构说明
- `docs/04-待开发功能/C_EXTENSION_TODO.md` - SUBSCRIBE 任务标记完成
- `docs/04-待开发功能/PHP_GATEWAY_TODO.md` - 更新收流IP架构说明
- `examples/gbvr-iot/exosip.stub.php` - 添加 SUBSCRIBE 方法类型提示

**新增文档**:
- `docs/03-功能实现/BUSINESS_LAYER_INTEGRATION.md` - 业务层集成指南（收流IP架构详解）

---

## 二、待完成的工作 (Phase 2 - 业务层集成)

### 2.1 数据库迁移 (待业务层实现)

**文件**: `examples/gbvr-iot/CoreW/Database/Migrations/xxx_add_stream_ip_to_media_servers.php`

```php
Schema::table('gv_media_servers', function (Blueprint $table) {
    $table->string('stream_ip', 64)->default('')->after('host')
        ->comment('收流IP（用于SDP，为空则使用host）');
});
```

---

### 2.2 Gb28181Service.php 方法签名更新 (待业务层实现)

**需要修改的方法**:
```php
// 添加 $streamIp 参数（第7个参数）
public function startLiveVideo(
    string $deviceId, 
    string $channelId, 
    string $ssrc, 
    int $zlmPort, 
    int $tcpMode,
    string $streamId,
    string $streamIp  // 新增参数
): bool

public function startPlayback(
    string $deviceId,
    string $channelId,
    string $startTime,
    string $endTime,
    string $ssrc,
    int $zlmPort,
    int $tcpMode,
    string $streamId,
    string $streamIp  // 新增参数
): bool
```

**修改位置**: 在 `sendCommand()` 中传递 `media_server_ip` 参数：
```php
private function sendCommand(...): bool
{
    $command = [
        // ...
        'params' => array_merge($params, [
            'media_server_ip' => $streamIp,  // 添加到params
        ]),
    ];
    
    return $this->redis->lPush('gb28181:commands', json_encode($command)) !== false;
}
```

---

### 2.3 Gb28181Client.php SDK 扩展 (待业务层实现)

**新增方法**:
```php
// SUBSCRIBE 订阅管理
public function subscribeCatalog(string $deviceId, int $expires = 3600): array
public function subscribeAlarm(string $deviceId, int $expires = 3600): array
public function subscribeMobilePosition(string $deviceId, int $expires = 3600): array
public function cancelSubscription(string $deviceId, string $eventType): array
public function getSubscriptions(string $deviceId): array
```

---

### 2.4 GBServerHockController.php 回调扩展 (待业务层实现)

**新增回调类型**:
```php
// 目录变更通知
'catalog_update' => [
    'device_id' => '34020000001320000001',
    'sum_num' => 5,
    'devices' => [...]
]

// 报警事件通知
'alarm_event' => [
    'device_id' => '34020000001320000001',
    'alarm_method' => '1',  // 1=电话, 2=设备, 3=短信, 4=GPS
    'alarm_time' => '2026-01-08T10:30:00',
    'alarm_description' => '移动侦测报警',
    'longitude' => 116.407396,
    'latitude' => 39.904211
]

// 位置更新通知
'position_update' => [
    'device_id' => '34020000001320000001',
    'time' => '2026-01-08T10:30:00',
    'longitude' => 116.407396,
    'latitude' => 39.904211,
    'speed' => 60.5,
    'direction' => 90.0,
    'altitude' => 50.0
]
```

---

### 2.5 DeviceService.php 配置同步 (待业务层实现)

**新增方法**:
```php
public function syncDeviceConfig(array $device): bool
{
    // 1. 查询最新设备配置
    // 2. 发送到信令网关
    // 3. 记录同步结果
}
```

---

### 2.6 定时任务 (待业务层实现)

**文件**: `CoreW/Task/Gb28181SubscriptionTask.php`

```php
class Gb28181SubscriptionTask
{
    // 每小时刷新一次订阅（expires - 5分钟）
    public function refreshSubscriptions(): void
    
    // 清理过期订阅
    public function cleanupExpiredSubscriptions(): void
}
```

---

## 三、测试清单

### 3.1 C 扩展测试 (已完成)

- 编译通过：`bash build_macos_fix.sh`
- 加载成功：`php -m | grep exosip`
- API 可用：`php examples/test_subscribe.php` (需创建)

### 3.2 集成测试 (待执行)

**测试脚本**: `examples/test_subscribe_integration.php`

```php
// 1. 设备注册
// 2. 订阅目录
// 3. 等待 NOTIFY 
// 4. 解析目录变更
// 5. 刷新订阅
// 6. 取消订阅
```

---

## 四、技术细节

### 4.1 订阅自动刷新机制

```php
// GB28181Handler.php
public function refreshSubscriptions(): void
{
    $subs = $this->sipServer->getSubscriptions();
    $now = time();
    
    foreach ($subs as $sub) {
        // 提前5分钟刷新
        if ($sub['next_refresh'] <= $now) {
            $this->sipServer->refreshSubscribe($sub['subscription_id'], 3600);
        }
    }
}
```

**调用位置**:
- Worker 定时器: `$sipServer->onTimer = function() { $handler->refreshSubscriptions(); }`
- Laravel Task: 每小时执行一次

---

### 4.2 字符集转换

```php
// GB28181Handler.php
private function normalizeXmlEncoding(string $xml): string
{
    // 检测编码
    if (preg_match('/<\?xml[^>]*encoding=["\']?(GB2312|GBK)["\']?/i', $xml)) {
        // GB2312 -> UTF-8
        $xml = mb_convert_encoding($xml, 'UTF-8', 'GB2312');
        $xml = preg_replace('/(encoding=["\']?)(GB2312|GBK)(["\']?)/i', '${1}UTF-8${3}', $xml);
    }
    return $xml;
}
```

---

### 4.3 收流IP传递流程

```
1. Webman API
   └─> DeviceController::startLiveVideo()
       └─> Trait::startLiveVideoCore()
           └─> 查询 gv_media_servers.stream_ip
               └─> Gb28181Service::startLiveVideo($streamIp)
                   └─> Redis: gb28181:commands
                       └─> Gateway: CommandDispatcher::handleStartLiveVideo()
                           └─> params['media_server_ip']
                               └─> SdpBuilder::buildLiveVideoSdp($mediaIp)
                                   └─> SDP: c=IN IP4 $streamIp
```

---

## 五、下一步计划

### 短期 (本周内)

1. 业务层实现数据库迁移
2. 更新 Gb28181Service.php 方法签名
3. 创建 Gb28181Client.php SDK 方法
4. 测试完整的订阅流程

### 中期 (本月内)

1. GBServerHockController.php 回调扩展
2. 编写集成测试
3. 生产环境部署验证
4. 监控和告警配置

### 长期 (下个月)

1. GB28181-2022 版本新特性
2. 性能优化和压力测试
3. 多媒体服务器负载均衡
4. 高可用架构部署

---

## 六、关键决策记录

### 决策 1: 收流IP架构调整

**原方案**: 每个设备配置 media_host
```
设备 -> media_host (配置复杂，不灵活)
```

**新方案**: 通过流媒体服务器表管理
```
通道 -> media_server_id -> stream_ip (集中管理，灵活切换)
```

**优势**:
- 集中配置管理
- 支持多媒体服务器负载均衡
- 通道级别灵活切换服务器
- 减少设备表字段

### 决策 2: SUBSCRIBE 订阅管理方式

**方案**: C 层管理订阅状态 + PHP 层业务逻辑

**理由**:
- C 层维护 subscription_id <-> dialog_id 映射
- C 层自动处理 NOTIFY 的 SIP 层响应 (200 OK)
- PHP 层解析 NOTIFY body 并分发业务逻辑
- 清晰的职责分离，便于维护

### 决策 3: 字符集处理

**方案**: 在 PHP 层统一转换为 UTF-8

**理由**:
- C 层不关心字符集（透明传输）
- PHP mb_convert_encoding 更灵活
- 业务层统一使用 UTF-8
- 避免 C 层依赖 iconv 库

---

## 七、文件清单

### 已修改文件 (10个)

**C 扩展层**:
1. `exosip_wrapper.c` - SUBSCRIBE 核心实现
2. `exosip_wrapper.h` - 数据结构和函数声明
3. `php_exosip.c` - PHP 方法绑定

**PHP 协议层**:
4. `examples/protocol/GB28181Handler.php` - 订阅管理和 NOTIFY 处理
5. `examples/gbvr-iot/Gb28181Gateway/src/Message/CommandDispatcher.php` - 从 params 获取 stream_ip

**业务层**:
6. `examples/gbvr-iot/CoreW/Business/Devices/Traits/GB28181StreamTrait.php` - 媒体服务器检查和 stream_ip 传递
7. `examples/gbvr-iot/CoreW/Domain/Devices/Entity/Device.php` - 移除 mediaHost
8. `examples/gbvr-iot/CoreW/Database/Migrations/xxx_add_media_host_to_devices.php` - 移除 media_host 字段

**文档**:
9. `docs/03-功能实现/DEVICE_CONFIG_EXTEND.md` - 更新收流IP架构
10. `docs/03-功能实现/BUSINESS_LAYER_INTEGRATION.md` - 业务层集成指南
11. `docs/04-待开发功能/C_EXTENSION_TODO.md` - 标记已完成
12. `docs/04-待开发功能/PHP_GATEWAY_TODO.md` - 更新架构说明
13. `docs/03-功能实现/SUBSCRIBE_FEATURE_STATUS.md` - 更新状态
14. `examples/gbvr-iot/exosip.stub.php` - 添加类型提示

---

## 八、待业务层实现的文件 (预计5个)

### 必须实现
1. `CoreW/Database/Migrations/xxx_add_stream_ip_to_media_servers.php` - 数据库迁移
2. `CoreW/Business/GB/Gb28181Service.php` - 更新方法签名（添加 $streamIp 参数）
3. `CoreW/Business/Devices/Service/Gb28181Client.php` - 添加 SDK 方法

### 可选实现
4. `CoreW/Business/Devices/Service/DeviceService.php` - 配置同步方法
5. `CoreW/Task/Gb28181SubscriptionTask.php` - 定时刷新任务

---

## 九、验证步骤

### Step 1: 编译测试 (已完成)

```bash
cd /Users/jiechengyang/src/c-app/php-exosip
bash build_macos_fix.sh
php -m | grep exosip
```

### Step 2: 功能测试 (待执行)

```bash
# 测试 C 扩展 SUBSCRIBE API
php examples/test_subscribe.php

# 测试完整流程
php examples/test_subscribe_integration.php
```

### Step 3: 业务层集成 (待执行)

```bash
# 运行数据库迁移
php webman migrate

# 测试 API
curl -X POST http://localhost:8787/api/gb28181/subscribe/catalog \
  -d '{"device_id": "34020000001320000001"}'
```

---

## 十、性能影响评估

### 内存占用
- 每个订阅: ~256 字节
- 最大订阅数: 1024 个（MAX_SUBSCRIPTIONS）
- 总内存增加: <1MB

### CPU 占用
- 订阅刷新: 每小时一次，几乎无影响
- NOTIFY 处理: 仅在事件发生时触发
- 预计 CPU 增加: <1%

### 网络开销
- SUBSCRIBE 请求: ~500 字节/次
- NOTIFY 请求: 取决于 body 大小（通常 <5KB）
- 刷新流量: 平均 0.5KB/小时/订阅

---

## 十一、后续优化方向

### 短期优化
1. 订阅状态持久化（防止 Worker 重启丢失）
2. NOTIFY 消息队列化（解耦业务处理）
3. 订阅优先级管理

### 长期优化
1. 集群模式订阅共享（Redis 订阅状态同步）
2. 订阅流量监控和告警
3. 智能订阅策略（根据设备在线率调整）

---

## 十二、参考文档

### 核心文档
- `docs/03-功能实现/SUBSCRIBE_FEATURE_STATUS.md` - 功能状态
- `docs/03-功能实现/BUSINESS_LAYER_INTEGRATION.md` - 业务层集成指南
- `docs/04-待开发功能/国标设备扩展功能-订阅.md` - GB28181 订阅规范

### 架构文档
- `docs/02-架构设计/MASTER_WORKER_TASK.md` - 多进程架构
- `docs/02-架构设计/SOCKET_FORK_ARCHITECTURE.md` - Socket 管理

### 部署文档
- `docs/05-部署运维/QUICKSTART.md` - 快速开始
- `docs/05-部署运维/BUILD_CENTOS.md` - CentOS 编译

---

## 十三、问题和风险

### 已知问题
1. SUBSCRIBE 订阅在 Worker 重启后丢失（需要持久化）
2. NOTIFY 消息可能包含 GB2312 编码（已处理）
3. 多订阅场景下的性能未测试

### 风险缓解
1. 在 onWorkerStart 中重新订阅关键设备
2. 字符集自动检测和转换
3. 限制单设备订阅数量（<10个）

---

## 十四、开发团队备注

### 已完成 (AI + C/PHP协议层)
- C 扩展 SUBSCRIBE 支持
- GB28181Handler 订阅管理
- 收流IP架构重构
- 相关文档更新

### 待完成 (业务层开发人员)
- 数据库迁移执行
- Gb28181Service 方法更新
- Gb28181Client SDK 扩展
- Hook 回调实现
- 集成测试编写

**预计工作量**: 2-3 个工作日  
**复杂度**: 中等（主要是数据传递，业务逻辑简单）

---

**文档创建日期**: 2026-01-08  
**下次更新**: 业务层实现完成后
