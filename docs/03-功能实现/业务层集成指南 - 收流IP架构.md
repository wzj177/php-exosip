# 业务层集成指南 - 收流IP架构

**版本**: v2.4.0  
**更新日期**: 2026-01-08

---

## 一、架构概述

### 新架构: 通道 -> 流媒体服务器 -> 收流IP

```
┌─────────────────┐
│  gv_devices     │
│  (设备表)        │
└────────┬────────┘
         │ device_id
         │
┌────────▼────────┐
│  gv_channels    │
│  (通道表)        │
│  - media_server_id  ← 关联媒体服务器
└────────┬────────┘
         │ media_server_id
         │
┌────────▼────────┐
│ gv_media_servers│
│ (流媒体服务器表) │
│  - host         │ ← 管理API地址 (http://127.0.0.1:8080)
│  - stream_ip    │ ← 收流IP (192.168.1.100，用于SDP)
│  - status       │ ← running/stopped
└─────────────────┘
```

### 优势

1. **集中管理**: 所有媒体服务器的收流IP在一个表中配置
2. **灵活切换**: 通道可以动态切换媒体服务器
3. **负载均衡**: 支持多个媒体服务器分担负载
4. **简化配置**: 不需要在每个设备上配置 media_host

---

## 二、数据库迁移

### 2.1 添加 stream_ip 字段

**文件**: `CoreW/Database/Migrations/xxx_add_stream_ip_to_media_servers.php`

```php
<?php

use support\Db;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Database\Migrations\Migration;

class AddStreamIpToMediaServers extends Migration
{
    public function up()
    {
        Db::schema()->table('gv_media_servers', function (Blueprint $table) {
            $table->string('stream_ip', 64)->default('')->after('host')
                ->comment('收流IP（用于SDP，为空则使用host）');
        });
    }

    public function down()
    {
        Db::schema()->table('gv_media_servers', function (Blueprint $table) {
            $table->dropColumn('stream_ip');
        });
    }
}
```

### 2.2 执行迁移

```bash
cd /path/to/gbvr-iot
php webman migrate
```

---

## 三、业务层代码修改

### 3.1 Gb28181Service.php 方法签名更新

**位置**: `CoreW/Business/GB/Gb28181Service.php`

**修改前**:
```php
public function startLiveVideo(
    string $deviceId, 
    string $channelId, 
    string $ssrc, 
    int $zlmPort, 
    int $tcpMode,
    string $streamId
): bool
```

**修改后**:
```php
public function startLiveVideo(
    string $deviceId, 
    string $channelId, 
    string $ssrc, 
    int $zlmPort, 
    int $tcpMode,
    string $streamId,
    string $streamIp  // 新增: 收流IP
): bool {
    $command = [
        'action' => 'start_live_video',
        'device_id' => $deviceId,
        'channel_id' => $channelId,
        'timestamp' => time(),
        'params' => [
            'ssrc' => $ssrc,
            'zlm_port' => $zlmPort,
            'tcp_mode' => $tcpMode,
            'stream_id' => $streamId,
            'media_server_ip' => $streamIp,  // 新增: 传递收流IP
        ],
    ];
    
    return $this->redis->lPush('gb28181:commands', json_encode($command)) !== false;
}
```

**同样修改 startPlayback 方法** (添加第9个参数 `string $streamIp`)

---

### 3.2 GB28181StreamTrait.php 已完成

**关键逻辑**:
```php
protected function startLiveVideoCore(...): array
{
    // 1. 检查通道是否关联媒体服务器
    if ($channel['media_server_id'] === MediaServerType::NONE->value) {
        throw new \InvalidArgumentException('通道未关联媒体服务器', 400);
    }
    
    // 2. 获取媒体服务器信息
    $mediaServer = $this->getMediaServerService()->getMediaServerByServerId($channel['media_server_id']);
    if (!$mediaServer) {
        throw new \InvalidArgumentException('媒体服务器不存在', 404);
    }
    
    // 3. 检查媒体服务器状态
    if ($mediaServer['status'] !== 'running') {
        throw new \InvalidArgumentException('媒体服务器未运行', 503);
    }
    
    // 4. 获取收流IP（优先 stream_ip，否则 host）
    $streamIp = !empty($mediaServer['stream_ip']) 
        ? $mediaServer['stream_ip'] 
        : $mediaServer['host'];
        
    if (empty($streamIp)) {
        throw new \InvalidArgumentException('媒体服务器缺少收流IP配置', 500);
    }
    
    // 5. 传递到信令网关
    $result = $this->getGb28181Service()->startLiveVideo(
        $deviceId, $channelId, $ssrc, $zlmPort, $tcpMode, $streamId,
        $streamIp  // 第7个参数
    );
}
```

---

### 3.3 CommandDispatcher.php 已完成

**关键修改**:
```php
private function handleStartLiveVideo(...): array
{
    // 从 params 获取收流IP（由 gbvr-iot 传入）
    $mediaServerIp = $params['media_server_ip'] ?? null;
    if (!$mediaServerIp) {
        return $this->errorResponse($requestId, 'Missing media_server_ip in params');
    }
    
    // 构建 SDP（使用传入的收流IP）
    $sdp = SdpBuilder::buildLiveVideoSdp(
        serverId: $this->config['server_id'],
        mediaIp: $mediaServerIp,  // 使用从params传入的收流IP
        mediaPort: $zlmPort,
        ssrc: $ssrc,
        tcpMode: $tcpMode
    );
    
    // ...
}
```

---

## 四、配置示例

### 4.1 流媒体服务器配置

**数据库记录**:
```sql
INSERT INTO gv_media_servers (id, name, host, port, secret, stream_ip, status) VALUES
(1, 'ZLM-1', '127.0.0.1', 8080, 'your_secret', '192.168.1.100', 'running'),
(2, 'ZLM-2', '172.16.0.10', 8080, 'your_secret', '172.16.0.10', 'running');
```

**说明**:
- `host`: ZLM 管理 API 地址（HTTP接口）
- `stream_ip`: 设备推流的目标IP（写入SDP的c=行）
- `status`: 健康状态（由监控任务更新）

---

### 4.2 通道配置

**关联媒体服务器**:
```sql
UPDATE gv_channels 
SET media_server_id = 1  -- 关联到 ZLM-1
WHERE device_id = '34020000001320000001' 
  AND channel_id = '34020000001310000001';
```

---

## 五、API 调用流程

### 5.1 开始实时视频

**请求**:
```http
POST /api/devices/34020000001320000001/channels/34020000001310000001/live/start
```

**流程**:
```
1. DeviceController::startLiveVideo()
   |
2. GB28181StreamTrait::startLiveVideoCore()
   |
   +-- 查询设备和通道
   +-- 检查通道的 media_server_id
   +-- 查询 gv_media_servers 表
   +-- 检查媒体服务器状态
   +-- 获取 stream_ip (优先使用stream_ip，否则host)
   |
3. Gb28181Service::startLiveVideo($streamIp)
   |
   +-- 构造命令: params['media_server_ip'] = $streamIp
   +-- 推送到 Redis: gb28181:commands
   |
4. Gateway CommandDispatcher::handleStartLiveVideo()
   |
   +-- 从 params 获取 media_server_ip
   +-- 构建 SDP: c=IN IP4 $mediaServerIp
   +-- 发送 INVITE
```

**响应**:
```json
{
    "code": 0,
    "message": "success",
    "data": {
        "stream_id": "34020000001320000001_34020000001310000001",
        "ssrc": "0100000001",
        "zlm_port": 30000,
        "tcp_mode": 1
    }
}
```

---

### 5.2 错误处理

**场景 1: 通道未关联媒体服务器**
```json
{
    "code": 400,
    "message": "通道未关联媒体服务器"
}
```

**场景 2: 媒体服务器不存在**
```json
{
    "code": 404,
    "message": "媒体服务器不存在"
}
```

**场景 3: 媒体服务器未运行**
```json
{
    "code": 503,
    "message": "媒体服务器未运行"
}
```

**场景 4: 缺少收流IP配置**
```json
{
    "code": 500,
    "message": "媒体服务器缺少收流IP配置"
}
```

---

## 六、测试清单

### 6.1 单元测试

- [ ] 流媒体服务器状态检查
- [ ] stream_ip 优先级逻辑（stream_ip -> host）
- [ ] 异常抛出和错误码
- [ ] 参数传递完整性

### 6.2 集成测试

- [ ] 完整点播流程（设备推流成功）
- [ ] stream_ip 在 SDP 中正确使用
- [ ] 多通道并发点播
- [ ] 媒体服务器切换

### 6.3 边界测试

- [ ] stream_ip 为空时回退到 host
- [ ] 媒体服务器离线时拒绝点播
- [ ] 通道未关联服务器时提示错误

---

## 七、监控和运维

### 7.1 关键指标

```php
// 在 Trait 中添加日志
Log::channel('sip')->info('Media server selected', [
    'media_server_id' => $mediaServer['id'],
    'host' => $mediaServer['host'],
    'stream_ip' => $streamIp,
    'status' => $mediaServer['status'],
]);
```

### 7.2 告警规则

- 媒体服务器 status != 'running' 时告警
- stream_ip 为空且 host 为空时告警
- 点播失败率 >10% 时告警

---

## 八、常见问题

### Q1: stream_ip 和 host 有什么区别？

**A**: 
- `host`: ZLM 管理 API 地址（用于调用 HTTP 接口）
- `stream_ip`: 设备推流目标IP（写入 SDP 的 c= 行）

**示例**:
```
host = '127.0.0.1:8080'        (ZLM API: http://127.0.0.1:8080/index/api/...)
stream_ip = '192.168.1.100'    (SDP: c=IN IP4 192.168.1.100)
```

### Q2: 为什么要优先使用 stream_ip？

**A**: 
- 支持 NAT 穿透（ZLM 在内网，需要暴露公网IP）
- 支持多网卡场景（管理网和数据网分离）
- 兼容旧配置（stream_ip 为空时回退到 host）

### Q3: 如何配置多个流媒体服务器？

**A**: 
```sql
-- 服务器1 (本地测试)
INSERT INTO gv_media_servers (name, host, stream_ip, status) 
VALUES ('ZLM-Local', '127.0.0.1:8080', '192.168.1.100', 'running');

-- 服务器2 (生产环境)
INSERT INTO gv_media_servers (name, host, stream_ip, status) 
VALUES ('ZLM-Prod', '172.16.0.10:8080', '203.0.113.50', 'running');

-- 通道分配策略（在业务代码中实现）
-- 根据设备区域、负载情况等分配到不同服务器
```

### Q4: stream_ip 为空会怎样？

**A**: 会自动回退到 `host` 字段（去除端口号）
```php
$streamIp = !empty($mediaServer['stream_ip']) 
    ? $mediaServer['stream_ip'] 
    : preg_replace('/:\d+$/', '', $mediaServer['host']);  // 移除端口
```

---

## 九、下一步工作

### 必须完成 (业务层开发人员)

1. 执行数据库迁移
2. 更新 Gb28181Service.php 方法签名
3. 测试完整的点播流程

### 可选完成

1. 实现媒体服务器健康检查
2. 实现负载均衡策略
3. 添加监控和告警

---

## 十、参考文档

- `docs/03-功能实现/GB28181_ZLM_INTEGRATION.md` - ZLM 集成指南
- `docs/03-功能实现/DEVICE_CONFIG_EXTEND.md` - 设备配置扩展方案
- `docs/04-待开发功能/PHP_GATEWAY_TODO.md` - PHP 网关待办任务

---

**文档维护者**: AI Assistant  
**业务层负责人**: 待指定
