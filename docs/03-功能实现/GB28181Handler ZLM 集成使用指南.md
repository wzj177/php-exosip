# GB28181Handler ZLM 集成使用指南

## 概述

`GB28181Handler` 现已完整集成 ZLMediaKit，支持：
- ✅ 实时视频点播
- ✅ 自动 SDP 解析（原生 C 层）
- ✅ SSRC 自动提取
- ✅ 多协议播放（RTSP/FLV/HLS）
- ✅ 会话自动清理
- ✅ 视频录制

---

## 快速开始

### 1. 配置参数

在创建 `GB28181Handler` 时添加 ZLM 配置：

```php
$config = [
    // 原有的 GB28181 配置
    'server_id' => '34020000002000000001',
    'server_domain' => '3402000000',
    'server_ip' => '192.168.1.100',
    'server_port' => 5060,
    'device_password' => '12345678',
    
    // 🎯 新增: ZLM 配置
    'zlm_enabled' => true,                              // 启用 ZLM
    'zlm_host' => '127.0.0.1',                          // ZLM 地址
    'zlm_port' => 80,                                   // ZLM HTTP API 端口
    'zlm_secret' => '035c73f7-bb6b-4889-a715-d9eb2d1925cc',  // API 密钥
    
    // 🎯 流媒体配置
    'media_server_ip' => '192.168.1.100',              // SDP 中使用的 IP
    'media_server_port_start' => 30000,
    'media_server_port_end' => 40000,
];

$handler = new GB28181Handler($sipServer, $config);
```

### 2. 启动服务器

```bash
# 1. 启动 ZLMediaKit
cd ZLMediaKit/release/linux/Release
./MediaServer -d

# 2. 启动 GB28181 服务器
php examples/gb28181_zlm_server.php
```

### 3. 发起视频点播

```php
// 方法 1: 在 Handler 中直接调用
$result = $handler->startLiveVideo(
    '34020000001320000001',  // 设备 ID
    '34020000001320000001',  // 通道 ID
    [
        'tcp_mode' => 0,      // 0=UDP, 1=TCP被动, 2=TCP主动
        'enable_mp4' => 1,    // 录制 MP4
        'enable_audio' => false,
    ]
);

if ($result) {
    echo "Call-ID: {$result['call_id']}\n";
    echo "Stream-ID: {$result['stream_id']}\n";
    echo "ZLM Port: {$result['zlm_port']}\n";
}
```

### 4. 获取播放地址

```php
// 等待设备 200 OK 后
sleep(2);

$playUrls = $handler->getPlayUrls($result['call_id']);

if ($playUrls) {
    echo "RTSP: {$playUrls['rtsp']}\n";
    echo "HTTP-FLV: {$playUrls['http_flv']}\n";
    echo "HLS: {$playUrls['hls']}\n";
}
```

### 5. 停止视频

```php
$handler->stopLiveVideo($result['call_id']);
```

---

## 完整流程说明

### 实时视频点播流程

```
┌──────────┐         ┌──────────┐         ┌──────────┐
│  Handler │         │   ZLM    │         │  Device  │
└────┬─────┘         └────┬─────┘         └────┬─────┘
     │                    │                     │
     │ 1. startLiveVideo()│                     │
     ├────────────────────>                     │
     │                    │                     │
     │ 2. openRtpServer   │                     │
     │    (分配端口)       │                     │
     │<────────────────────                     │
     │   port: 30000      │                     │
     │                    │                     │
     │ 3. INVITE + SDP    │                     │
     │    (port: 30000)   │                     │
     ├───────────────────────────────────────> │
     │                    │                     │
     │                    │ 4. 200 OK + SDP     │
     │                    │    (SSRC: 0100000001)│
     │<─────────────────────────────────────────┤
     │                    │                     │
     │ 5. handleInviteResponse()               │
     │    - 原生 SDP 解析  │                     │
     │    - 提取 SSRC      │                     │
     │    - 生成播放地址   │                     │
     │                    │                     │
     │ 6. ACK             │                     │
     ├───────────────────────────────────────> │
     │                    │                     │
     │                    │ 7. RTP Stream       │
     │                    │<────────────────────┤
     │                    │   (SSRC: 0100000001)│
     │                    │                     │
     │ 8. getPlayUrls()   │                     │
     │    - RTSP          │                     │
     │    - HTTP-FLV      │                     │
     │    - HLS           │                     │
```

### 关键点

#### 1. **原生 SDP 解析器自动调用**

在 `handleInviteResponse()` 中：

```php
// 🎯 自动解析设备 200 OK 中的 SDP
$sdp = $event->getSdp();

// ✅ 自动处理 GB28181 扩展（y= 字段）
$ssrc = $sdp['gb28181']['ssrc'];  // "0100000001"

// ✅ 提取标准字段
$deviceIp = $sdp['connection']['addr'];
$devicePort = $sdp['medias'][0]['port'];
```

#### 2. **ZLM 端口分配**

```php
// ZLM 自动分配可用端口
$zlmResult = $this->zlmClient->openRtpServer([
    'stream_id' => '34020000001320000001_34020000001320000001_1701234567',
    'tcp_mode' => 0,
    'port' => 0,  // 0 = 自动分配
]);

// 返回: ['code' => 0, 'port' => 30000]
```

#### 3. **SSRC 匹配**

- 服务器在 INVITE SDP 中发送 `y=0200000001`（服务器 SSRC）
- 设备在 200 OK SDP 中回复 `y=0100000001`（设备 SSRC）
- ZLM 根据设备 SSRC 识别和接收 RTP 流

#### 4. **会话自动清理**

```php
// 设备发送 BYE 时自动触发
public function handleBye($event) {
    $callId = $event->getCallId();
    $this->cleanupSession($callId);  // ✅ 自动释放 ZLM 端口
}
```

---

## API 参考

### `startLiveVideo()`

启动实时视频点播。

**参数:**
- `$deviceId` (string): 设备国标编码（20位）
- `$channelId` (string): 通道国标编码（20位）
- `$options` (array, 可选):
  - `tcp_mode` (int): 传输模式，默认 0
    - `0`: UDP
    - `1`: TCP 被动模式（设备主动连接）
    - `2`: TCP 主动模式（ZLM 主动连接）
  - `enable_mp4` (int): 是否录制 MP4，默认 1
  - `enable_audio` (bool): 是否包含音频，默认 false

**返回:**
- 成功: `['call_id' => string, 'stream_id' => string, 'zlm_port' => int]`
- 失败: `null`

**示例:**

```php
$result = $handler->startLiveVideo(
    '34020000001320000001',
    '34020000001320000001',
    [
        'tcp_mode' => 0,
        'enable_mp4' => 1,
        'enable_audio' => false,
    ]
);
```

---

### `stopLiveVideo()`

停止实时视频。

**参数:**
- `$callId` (string): 会话 Call-ID

**返回:**
- `bool`: 成功返回 true

**示例:**

```php
$handler->stopLiveVideo('abc123def456');
```

---

### `getPlayUrls()`

获取播放地址。

**参数:**
- `$callId` (string): 会话 Call-ID

**返回:**
- 成功: `['rtsp' => string, 'http_flv' => string, 'hls' => string]`
- 失败: `null`

**示例:**

```php
$urls = $handler->getPlayUrls('abc123def456');

if ($urls) {
    echo "RTSP: {$urls['rtsp']}\n";
    echo "FLV: {$urls['http_flv']}\n";
    echo "HLS: {$urls['hls']}\n";
}
```

---

### `getSession()`

获取会话详细信息。

**参数:**
- `$callId` (string): 会话 Call-ID

**返回:**
- 成功: 会话数组
- 失败: `null`

**示例:**

```php
$session = $handler->getSession('abc123def456');

if ($session) {
    echo "设备 ID: {$session['device_id']}\n";
    echo "通道 ID: {$session['channel_id']}\n";
    echo "Stream ID: {$session['stream_id']}\n";
    echo "ZLM Port: {$session['zlm_port']}\n";
    echo "设备 SSRC: {$session['ssrc']}\n";
    echo "设备 IP: {$session['device_ip']}\n";
    echo "设备端口: {$session['device_port']}\n";
}
```

---

### `getAllSessions()`

获取所有活动会话。

**返回:**
- `array`: 所有会话数组 `[call_id => session_info]`

**示例:**

```php
$sessions = $handler->getAllSessions();

echo "活动会话数: " . count($sessions) . "\n";
foreach ($sessions as $callId => $session) {
    echo "Call-ID: {$callId}\n";
    echo "  Device: {$session['device_id']}\n";
    echo "  Stream: {$session['stream_id']}\n";
}
```

---

## 浏览器播放

### HTTP-FLV (推荐)

```html
<!DOCTYPE html>
<html>
<head>
    <script src="https://cdn.jsdelivr.net/npm/flv.js/dist/flv.min.js"></script>
</head>
<body>
    <video id="videoElement" controls width="800"></video>
    <script>
        var flvPlayer = flv.createPlayer({
            type: 'flv',
            url: 'http://192.168.1.100/rtp/34020000001320000001_34020000001320000001_1701234567.live.flv'
        });
        flvPlayer.attachMediaElement(document.getElementById('videoElement'));
        flvPlayer.load();
        flvPlayer.play();
    </script>
</body>
</html>
```

### HLS

```html
<video id="video" controls></video>
<script src="https://cdn.jsdelivr.net/npm/hls.js/dist/hls.min.js"></script>
<script>
    var video = document.getElementById('video');
    var hls = new Hls();
    hls.loadSource('http://192.168.1.100/rtp/34020000001320000001_34020000001320000001_1701234567/hls.m3u8');
    hls.attachMedia(video);
</script>
```

---

## 故障排查

### 问题 1: 设备推流但 ZLM 无流

**原因**: SSRC 不匹配

**解决**:
1. 检查设备 SDP 是否包含 `y=` 字段
2. 查看 `handleInviteResponse` 日志中的 SSRC
3. 确认 ZLM 配置 `checkSource=1`

### 问题 2: 播放地址 404

**原因**: 设备尚未推流或 Stream ID 错误

**解决**:
1. 等待 2-3 秒（设备启动推流需要时间）
2. 检查 ZLM 流列表: `curl "http://127.0.0.1:80/index/api/getMediaList?secret=YOUR_SECRET"`
3. 查看 ZLM 日志: `tail -f MediaServer.log`

### 问题 3: Call-ID 不存在

**原因**: 会话已被清理或未正确创建

**解决**:
1. 使用 `getAllSessions()` 查看活动会话
2. 检查设备是否在线: `getOnlineDevices()`
3. 查看 SIP 日志中的 INVITE 发送结果

---

## 完整示例项目

参考 `examples/gb28181_zlm_server.php` 查看完整的命令行服务器实现。

---

**文档版本**: 1.0  
**更新日期**: 2025-11-30
