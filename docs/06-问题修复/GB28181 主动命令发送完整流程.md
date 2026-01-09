# GB28181 主动命令发送完整流程

## 概述

本文档描述 GB28181 系统中**主动命令发送**的完整数据流,以及**设备响应事件**的处理流程。

整个系统采用 **双向异步通信** 架构:
- **命令发送**: API → Redis → Gateway → 设备
- **事件响应**: 设备 → Gateway → Task Queue → API → 数据库

---

## 🏗️ 系统架构

```
┌───────────────────────────────────────────────────────────────────────┐
│                          gbvr-iot (API 项目)                           │
├───────────────────────────────────────────────────────────────────────┤
│                                                                        │
│  ┌──────────────┐       ┌────────────────┐      ┌──────────────────┐ │
│  │ GB28181Test  │──────▶│ Gb28181Client  │─────▶│  Redis 队列      │ │
│  │ (CLI Tool)   │       │     (SDK)      │      │ gb28181:commands │ │
│  └──────────────┘       └────────────────┘      └─────────┬────────┘ │
│         │                                                  │          │
│         └──────┐                                          │          │
│                ▼                                          │          │
│       ┌──────────────┐                                   │          │
│       │  ZLMClient   │ (分配端口/SSRC)                    │          │
│       └──────────────┘                                   │          │
│                                                           │          │
└───────────────────────────────────────────────────────────┼──────────┘
                                                            │
                                     Redis LPUSH            │
                                                            │
┌───────────────────────────────────────────────────────────┼──────────┐
│                      gb28181-gateway (信令网关)            │          │
├───────────────────────────────────────────────────────────┼──────────┤
│                                                           │          │
│       ┌──────────────────┐        BLPOP ◀─────────────────┘          │
│       │ RedisSubscriber  │──────▶ CommandDispatcher                  │
│       │  (LongTask)      │       │                                   │
│       └──────────────────┘       │                                   │
│                                  │                                   │
│              ┌───────────────────┴─────────────────┐                 │
│              │                                     │                 │
│              ▼                                     ▼                 │
│       ┌────────────┐                      ┌───────────────┐          │
│       │ ExoSip     │◀────INVITE───────────│   设备 SIP    │          │
│       │ (C扩展)    │                      │   eXosip2     │          │
│       │            │─────200 OK──────────▶│               │          │
│       │            │◀────ACK──────────────│               │          │
│       └─────┬──────┘                      └───────────────┘          │
│             │                                                         │
│             │ Event Callback                                         │
│             ▼                                                         │
│     ┌──────────────────┐                                             │
│     │ GB28181Handler   │                                             │
│     │  (事件处理器)     │                                             │
│     └─────────┬────────┘                                             │
│               │                                                       │
│               │ postTask() → Task Queue                              │
│               └────────────────┐                                     │
│                                │                                     │
└────────────────────────────────┼─────────────────────────────────────┘
                                 │
                                 │ HTTP POST
                                 │
┌────────────────────────────────┼─────────────────────────────────────┐
│                      gbvr-iot (API 项目)                │             │
├────────────────────────────────┼─────────────────────────────────────┤
│                                ▼                                     │
│                    ┌───────────────────────┐                         │
│                    │ GBServerHockController│                         │
│                    │   (Hook 接收器)       │                         │
│                    └───────────┬───────────┘                         │
│                                │                                     │
│                                ▼                                     │
│                     ┌─────────────────┐                              │
│                     │ DevicesService  │                              │
│                     └────────┬────────┘                              │
│                              │                                       │
│                              ▼                                       │
│                   ┌───────────────────┐                              │
│                   │    数据库 DAO      │                              │
│                   │  (Devices/        │                              │
│                   │   Channels/       │                              │
│                   │   Sessions)       │                              │
│                   └───────────────────┘                              │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘
```

---

## 📤 主动命令发送流程 (以实时视频为例)

### 1. 用户发起命令 (CLI Tool / API Controller)

**文件**: `examples/gbvr-iot/app/command/GB28181Test.php`

```php
// 用户通过 CLI 工具选择 "开始实时视频"
private function handleStartLiveVideo(...)
{
    $deviceId = '34020000001320000001';
    $channelId = '34020000001320000002';
    
    // Step 1: 生成 SSRC (实际应从数据库分配)
    $ssrc = $this->generateSsrc();
    
    // Step 2: 向 ZLM 申请端口
    $portResult = $this->zlmClient->openRtpServer(0, $tcpMode);
    $zlmPort = $portResult['port'];  // 例如: 30000
    
    // Step 3: 发送 INVITE 命令到网关
    $this->sipClient->startLiveVideo($deviceId, $channelId, $ssrc, $zlmPort, $tcpMode);
}
```

---

### 2. Gb28181Client SDK 推送命令到 Redis

**文件**: `examples/gbvr-iot/CoreW/Sdk/PSipGateway/Gb28181Client.php`

```php
public function startLiveVideo(
    string $deviceId,
    string $channelId,
    string $ssrc,
    int $zlmPort,
    int $tcpMode = 1
): bool {
    return $this->sendCommand($deviceId, 'start_live_video', [
        'channel_id' => $channelId,
        'ssrc' => $ssrc,          // 平台 SSRC (数据库分配)
        'zlm_port' => $zlmPort,    // ZLM 端口
        'tcp_mode' => $tcpMode     // 传输模式
    ]);
}

private function sendCommand(string $deviceId, string $action, array $params = []): bool
{
    $command = [
        'request_id' => uniqid('req_'),
        'action' => 'start_live_video',
        'device_id' => '34020000001320000001',
        'channel_id' => '34020000001320000002',
        'timestamp' => 1733123456,
        'params' => [
            'ssrc' => '0A000001',
            'zlm_port' => 30000,
            'tcp_mode' => 1
        ]
    ];
    
    // 推送到 Redis 队列 (LPUSH)
    Redis::lPush('gb28181:commands', json_encode($command));
}
```

**Redis 队列数据**:
```json
{
  "request_id": "req_674a8c3012345",
  "action": "start_live_video",
  "device_id": "34020000001320000001",
  "channel_id": "34020000001320000002",
  "timestamp": 1733123456,
  "params": {
    "ssrc": "0A000001",
    "zlm_port": 30000,
    "tcp_mode": 1
  }
}
```

---

### 3. Gateway 订阅 Redis 命令 (LongTask)

**文件**: `examples/gb28181-gateway/src/Handlers/LongTask/RedisSubscriber.php`

```php
public function run($server, string $queueName = 'gb28181:commands', int $timeout = 1)
{
    while (!$shouldExit) {
        // BLPOP 阻塞读取 (1秒超时)
        $result = $redis->blPop(['gb28181:commands'], 1);
        
        if ($result && is_array($result)) {
            $message = $result[1];
            $command = json_decode($message, true);
            
            // 分发到 CommandDispatcher
            $this->commandDispatcher->dispatch($command);
        }
    }
}
```

---

### 4. CommandDispatcher 分发命令

**文件**: `examples/gb28181-gateway/src/Message/CommandDispatcher.php`

```php
public function dispatch(array $command): array
{
    $action = $command['action'];  // 'start_live_video'
    $deviceId = $command['device_id'];
    $channelId = $command['channel_id'];
    $params = $command['params'];
    
    // 检查设备是否在线
    $device = $this->deviceManager->getDevice($deviceId);
    if (!$device || !$device['registered']) {
        return ['success' => false, 'error' => 'Device offline'];
    }
    
    // 根据 action 分发
    switch ($action) {
        case 'start_live_video':
            return $this->handleStartLiveVideo(..., $params);
        case 'stop_live_video':
            return $this->handleStopLiveVideo(...);
        case 'query_catalog':
            return $this->handleQueryCatalog(...);
        // ... 其他命令
    }
}
```

---

### 5. 构建 SDP 并发送 INVITE

**文件**: `examples/gb28181-gateway/src/Message/CommandDispatcher.php`

```php
private function handleStartLiveVideo(..., array $params): array
{
    // 从 params 获取 API 分配的资源
    $ssrc = $params['ssrc'];       // '0A000001' (数据库分配)
    $zlmPort = $params['zlm_port']; // 30000 (ZLM 分配)
    $tcpMode = $params['tcp_mode']; // 1 (TCP 被动模式)
    
    // 构建 SDP (使用 API 分配的 SSRC 和端口)
    $sdp = $this->buildInviteSdp(
        $this->config['media_server_ip'],  // 192.168.1.100
        $zlmPort,                          // 30000
        'Play',                            // 播放
        'recvonly',                        // 只接收
        $ssrc                              // 0A000001
    );
    
    // 发送 INVITE
    $targetUri = "sip:{$channelId}@{$deviceIp}:{$devicePort}";
    $dialogId = $this->sipServer->sendInvite($targetUri, $sdp);
    
    // 保存会话信息 (用于后续 BYE)
    $this->activeSessions["{$deviceId}:{$channelId}:live"] = [
        'dialog_id' => $dialogId,
        'ssrc' => $ssrc,
        'zlm_port' => $zlmPort,
        ...
    ];
    
    return ['success' => true, 'dialog_id' => $dialogId];
}
```

**SIP INVITE 消息**:
```
INVITE sip:34020000001320000002@192.168.1.201:5060 SIP/2.0
Subject: 34020000001320000002:34020000001320000002,34020000002000000001:0
Content-Type: application/sdp

v=0
o=34020000002000000001 0 0 IN IP4 192.168.1.100
s=Play
c=IN IP4 192.168.1.100
t=0 0
m=video 30000 TCP/RTP/AVP 96
a=recvonly
a=rtpmap:96 PS/90000
y=0A000001
```

---

### 6. 设备响应 200 OK

**设备 SIP 响应**:
```
SIP/2.0 200 OK
Content-Type: application/sdp

v=0
o=34020000001320000002 0 0 IN IP4 192.168.1.201
s=Play
c=IN IP4 192.168.1.201
t=0 0
m=video 6000 TCP/RTP/AVP 96
a=sendonly
a=rtpmap:96 PS/90000
y=0B123456
```

---

## 📥 设备响应事件处理流程

### 7. Gateway 接收 200 OK 并触发回调

**文件**: `examples/gb28181-gateway/src/Handlers/GB28181Handler.php`

```php
private function handleInviteResponse(\SipEvent $event): void
{
    $session = $event->getSession();
    $callId = $session->getCallId();
    
    // 解析设备 SDP (获取设备实际推流信息)
    $sdp = $event->getSdp();
    $deviceIp = $sdp['connection']['addr'];      // 192.168.1.201
    $devicePort = $sdp['medias'][0]['port'];     // 6000
    $deviceSsrc = $sdp['gb28181']['ssrc'];       // 0B123456 (关键!)
    
    // 发送 ACK 确认
    $this->sipServer->sendAck($dialogId);
    
    // ⚠️ 关键步骤: 投递任务到 API 项目
    $this->postTask('media_ready', [
        'call_id' => $callId,
        'device_ip' => $deviceIp,
        'device_port' => $devicePort,
        'device_ssrc' => $deviceSsrc,  // ⭐ 设备实际使用的 SSRC
        'timestamp' => time()
    ]);
}
```

---

### 8. 任务队列投递到 API

**文件**: `examples/gb28181-gateway/src/Handlers/GB28181Handler.php`

```php
private function postTask(string $type, array $payload): void
{
    $taskData = [
        'type' => $type,           // 'media_ready'
        'payload' => $payload,
        'timestamp' => time()
    ];
    
    // HTTP POST 到 API Hook 接口
    // POST http://api.example.com/api/v2/gb28181/hook
    // Content-Type: application/json
    
    $this->taskServer->postTask($taskData);
}
```

**HTTP 请求 Body**:
```json
{
  "type": "media_ready",
  "payload": {
    "call_id": "abc123def456",
    "device_ip": "192.168.1.201",
    "device_port": 6000,
    "device_ssrc": "0B123456",
    "timestamp": 1733123460
  },
  "timestamp": 1733123460
}
```

---

### 9. API 接收 Hook 并处理

**文件**: `examples/gbvr-iot/app/api/v2/controller/GBServerHockController.php`

```php
public function index(Request $request): Response
{
    $data = $request->post();
    $type = $data['type'] ?? '';
    $payload = $data['payload'] ?? [];
    
    try {
        switch ($type) {
            case 'media_ready':
                return $this->handleMediaReady($payload);
            case 'register':
                return $this->handleRegister($payload);
            case 'save_catalog':
                return $this->handleCatalog($payload);
            // ... 其他事件
        }
    } catch (\Exception $e) {
        Log::error("Hook处理失败: {$e->getMessage()}");
        return json(['success' => false, 'error' => $e->getMessage()]);
    }
}
```

---

### 10. 更新 ZLM SSRC 并生成播放地址

**文件**: `examples/gbvr-iot/app/api/v2/controller/GBServerHockController.php`

```php
private function handleMediaReady(array $payload): Response
{
    $callId = $payload['call_id'];
    $deviceSsrc = $payload['device_ssrc'];  // 0B123456
    
    // 1. 从数据库查询会话 (通过 call_id)
    $session = $this->devicesService->findSessionByCallId($callId);
    if (!$session) {
        throw new \Exception("Session not found: {$callId}");
    }
    
    $streamId = $session->stream_id;        // {device_id}_{channel_id}
    $platformSsrc = $session->platform_ssrc; // 0A000001 (数据库分配的)
    
    // 2. ⚠️ 关键步骤: 更新 ZLM SSRC
    //    告诉 ZLM 设备实际使用的 SSRC 是 0B123456
    //    这样 ZLM 才能正确接收 RTP 流
    $zlmResult = $this->zlmClient->updateRtpServerSsrc($streamId, $deviceSsrc);
    
    if (!$zlmResult['success']) {
        throw new \Exception("Failed to update ZLM SSRC");
    }
    
    // 3. 生成播放地址
    $playUrls = [
        'rtsp' => "rtsp://192.168.1.100:554/rtp/{$streamId}",
        'rtmp' => "rtmp://192.168.1.100:1935/rtp/{$streamId}",
        'flv' => "http://192.168.1.100/rtp/{$streamId}.live.flv",
        'hls' => "http://192.168.1.100/rtp/{$streamId}/hls.m3u8"
    ];
    
    // 4. 更新数据库会话状态
    $this->devicesService->updateSessionByCallId($callId, [
        'device_ssrc' => $deviceSsrc,
        'play_urls' => json_encode($playUrls),
        'status' => 'active',
        'updated_at' => date('Y-m-d H:i:s')
    ]);
    
    return json([
        'success' => true,
        'play_urls' => $playUrls
    ]);
}
```

---

## 🔄 完整时序图

```
User/CLI         API(gbvr-iot)         Redis Queue         Gateway          Device          ZLMediaKit
  │                    │                    │                  │                │                 │
  │  开始直播           │                    │                  │                │                 │
  ├──────────────────▶│                    │                  │                │                 │
  │                    │ 生成 SSRC          │                  │                │                 │
  │                    │ (数据库)           │                  │                │                 │
  │                    │                    │                  │                │                 │
  │                    │ 分配端口           │                  │                │                 │
  │                    ├────────────────────┼──────────────────┼────────────────┼────────────────▶│
  │                    │                    │                  │                │    openRtpServer│
  │                    │◀───────────────────┼──────────────────┼────────────────┼─────────────────┤
  │                    │  port: 30000       │                  │                │                 │
  │                    │                    │                  │                │                 │
  │                    │ LPUSH 命令         │                  │                │                 │
  │                    ├───────────────────▶│                  │                │                 │
  │                    │                    │                  │                │                 │
  │                    │                    │  BLPOP           │                │                 │
  │                    │                    │◀─────────────────┤                │                 │
  │                    │                    │                  │                │                 │
  │                    │                    │                  │ INVITE         │                 │
  │                    │                    │                  │ (SSRC: 0A..)   │                 │
  │                    │                    │                  ├───────────────▶│                 │
  │                    │                    │                  │                │                 │
  │                    │                    │                  │  200 OK        │                 │
  │                    │                    │                  │ (SSRC: 0B..)   │                 │
  │                    │                    │                  │◀───────────────┤                 │
  │                    │                    │                  │                │                 │
  │                    │                    │                  │ ACK            │                 │
  │                    │                    │                  ├───────────────▶│                 │
  │                    │                    │                  │                │                 │
  │                    │                    │                  │ postTask       │                 │
  │                    │                    │                  │ (media_ready)  │                 │
  │                    │◀───────────────────┼──────────────────┤                │                 │
  │                    │                    │                  │                │                 │
  │                    │ updateRtpServerSsrc│                  │                │                 │
  │                    │ (SSRC: 0B123456)   │                  │                │                 │
  │                    ├────────────────────┼──────────────────┼────────────────┼────────────────▶│
  │                    │◀───────────────────┼──────────────────┼────────────────┼─────────────────┤
  │                    │                    │                  │                │                 │
  │                    │ 生成播放地址        │                  │                │                 │
  │                    │                    │                  │                │                 │
  │   返回播放地址       │                    │                  │                │                 │
  │◀───────────────────┤                    │                  │                │                 │
  │                    │                    │                  │                │  RTP Stream     │
  │                    │                    │                  │                ├────────────────▶│
  │                    │                    │                  │                │  (SSRC: 0B..)   │
```

---

## 🔑 关键技术要点

### 1. SSRC 的两次分配

| 阶段 | SSRC 来源 | 用途 | 值 |
|------|----------|------|-----|
| **INVITE 前** | API 数据库分配 | 告诉设备**期望的** SSRC | `0A000001` |
| **200 OK 后** | 设备 SDP 返回 | 设备**实际使用**的 SSRC | `0B123456` |
| **media_ready** | 更新到 ZLM | 让 ZLM 知道设备真实 SSRC | `0B123456` |

**为什么需要两个 SSRC?**
- GB28181 规范中,平台会在 INVITE 中告诉设备期望使用的 SSRC (`y=` 字段)
- 但设备可以**忽略**平台的 SSRC,在 200 OK 中返回自己的 SSRC
- 因此需要在 `media_ready` Hook 中更新 ZLM,否则 ZLM 无法接收流

---

### 2. 端口分配时机

| 组件 | 分配时机 | 端口用途 |
|------|---------|---------|
| **ZLM** | INVITE 前 | 接收设备 RTP 流 (例如: 30000) |
| **设备** | 200 OK 中返回 | 设备推流的源端口 (例如: 6000) |

**重要**: 端口必须在 INVITE 前分配,因为 SDP 中需要包含 ZLM 的接收端口。

---

### 3. Redis 队列的 BLPOP

```php
// 阻塞式读取,超时时间 1 秒 (可响应信号)
$result = $redis->blPop(['gb28181:commands'], 1);
```

**优势**:
- 低延迟 (命令立即处理)
- 低 CPU 占用 (阻塞等待,不消耗 CPU)
- 支持优雅退出 (1 秒超时可响应 SIGTERM)

---

### 4. postTask 的作用

**文件**: `GB28181Handler::postTask()`

```php
private function postTask(string $type, array $payload): void
{
    $taskId = $this->taskServer->postTask([
        'type' => $type,
        'payload' => $payload,
        'timestamp' => time()
    ]);
}
```

**用途**:
- 将 SIP 事件异步发送到 API 项目
- 避免阻塞 SIP 事件循环
- API 项目负责数据库操作和业务逻辑

**支持的事件类型**:
- `register`: 设备注册
- `unregister`: 设备注销
- `update_heartbeat`: 心跳更新
- `save_catalog`: 目录保存
- `media_ready`: 媒体流就绪 ⭐
- `session_bye`: 会话关闭
- `device_status`: 设备状态
- `alarm`: 设备告警

---

## 📋 支持的命令列表

| 命令 | 作用 | 参数 |
|------|------|------|
| `start_live_video` | 开始实时视频 | ssrc, zlm_port, tcp_mode |
| `stop_live_video` | 停止实时视频 | - |
| `start_playback` | 开始录像回放 | start_time, end_time, ssrc, zlm_port |
| `stop_playback` | 停止录像回放 | - |
| `query_catalog` | 查询设备目录 | - |
| `query_device_info` | 查询设备信息 | - |
| `query_device_status` | 查询设备状态 | - |
| `query_record` | 查询录像文件 | start_time, end_time, type |
| `ptz_control` | PTZ 云台控制 | command, speed |

---

## 🧪 测试流程

### 启动系统

```bash
# 1. 启动 ZLMediaKit
cd /path/to/ZLMediaKit
./MediaServer -d

# 2. 启动 Redis
redis-server

# 3. 启动信令网关
cd examples/gb28181-gateway
php gb28181_server.php

# 4. 启动 API 服务
cd examples/gbvr-iot
php start.php start
```

---

### 运行 CLI 测试工具

```bash
cd examples/gbvr-iot
php webman gb:test
```

**CLI 菜单**:
```
1. 查询设备目录
2. 查询设备信息
3. 查询设备状态
4. 查询录像文件
5. 开始实时视频        ⭐ 核心功能
6. 停止实时视频
7. 开始录像回放
8. 停止录像回放
9. PTZ 云台控制
10. 查看会话信息
```

---

### 调试日志

**Gateway 日志** (`examples/gb28181-gateway/logs/`):
```
[2024-12-02 10:00:00] [INFO] Dispatch command: start_live_video (Device: 34020000001320000001)
[2024-12-02 10:00:01] [INFO] Start live video: 34020000001320000002
[2024-12-02 10:00:01] [INFO] INVITE sent: Dialog=123
[2024-12-02 10:00:02] [INFO] 收到 INVITE 200 OK: Call-ID=abc123
[2024-12-02 10:00:02] [INFO] 设备 SSRC: 0B123456
[2024-12-02 10:00:02] [INFO] 投递任务 #456: media_ready
```

**API 日志** (`examples/gbvr-iot/runtime/logs/`):
```
[2024-12-02 10:00:02] [INFO] Hook received: media_ready
[2024-12-02 10:00:02] [INFO] Updating ZLM SSRC: streamId=xxx, ssrc=0B123456
[2024-12-02 10:00:03] [INFO] Play URLs generated: rtsp://192.168.1.100:554/rtp/xxx
```

---

## 🐛 常见问题

### 1. 设备响应 200 OK 但没有流

**原因**: ZLM 不知道设备的真实 SSRC

**解决**:
1. 检查 `media_ready` Hook 是否触发
2. 确认 `updateRtpServerSsrc()` 被调用
3. 查看 ZLM 日志: `curl http://127.0.0.1/index/api/getMediaList`

---

### 2. 命令发送后无响应

**原因**: Redis 连接失败或 Gateway 未启动

**排查**:
```bash
# 检查 Redis 队列
redis-cli LLEN gb28181:commands

# 检查 Gateway 进程
ps aux | grep gb28181_server

# 查看 Gateway 日志
tail -f examples/gb28181-gateway/logs/debug.log
```

---

### 3. INVITE 超时

**原因**: 设备离线或网络不通

**排查**:
```php
// 检查设备注册状态
$device = $this->devicesService->findDeviceById($deviceId);
if ($device->status !== 'online') {
    throw new \Exception('Device offline');
}

// 检查网络连通性
ping $deviceIp
```

---

## 📚 参考文档

- [GB28181_COMMAND_GUIDE.md](./GB28181_COMMAND_GUIDE.md) - 命令格式详细说明
- [GB28181_HANDLER_ZLM_USAGE.md](./GB28181_HANDLER_ZLM_USAGE.md) - ZLM 集成使用
- [CALLBACK_ERROR_HANDLING.md](./CALLBACK_ERROR_HANDLING.md) - 事件回调错误处理
- [LOGGER_USAGE.md](./LOGGER_USAGE.md) - 日志系统使用

---

## ✅ 总结

**主动命令流程** (5 步):
1. CLI Tool / API → Gb28181Client SDK
2. SDK → Redis Queue (LPUSH)
3. Gateway RedisSubscriber → CommandDispatcher (BLPOP)
4. CommandDispatcher → ExoSip (INVITE)
5. ExoSip → 设备 (SIP 消息)

**响应事件流程** (4 步):
1. 设备 → ExoSip (200 OK)
2. ExoSip → GB28181Handler (Event Callback)
3. GB28181Handler → Task Queue → API (postTask)
4. API → DevicesService → 数据库 (更新状态)

**关键技术点**:
- ✅ Redis BLPOP 实现低延迟命令分发
- ✅ SSRC 二次确认机制 (INVITE → 200 OK → ZLM)
- ✅ postTask 异步事件推送
- ✅ ZLM 端口/SSRC 动态管理
