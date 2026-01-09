# GB28181 信令操作完整指南

## 概述

本文档描述如何通过Redis队列向GB28181服务器发送各种信令命令。所有命令通过统一的`CommandDispatcher`处理。

## 命令格式

所有命令通过Redis的`gb28181:commands`队列发送,格式为:

```json
{
    "request_id": "唯一请求ID",
    "action": "操作类型",
    "device_id": "设备ID(20位)",
    "channel_id": "通道ID(20位,可选,默认等于device_id)",
    "timestamp": 时间戳,
    "params": {
        // 操作特定参数
    }
}
```

## 支持的操作

### 1. 实时视频点播

#### 开始实时视频

```php
$client->sendCommand(
    '34020000001320948622',  // 设备ID
    '34020000001310000001',  // 通道ID
    'start_live_video',
    []
);
```

**信令流程:**
1. 服务器 → 设备: INVITE (包含SDP,指定接收端口)
2. 设备 → 服务器: 200 OK (包含SDP,设备推流端口)
3. 服务器 → 设备: ACK 确认
4. 设备开始推送RTP视频流到服务器指定端口

**返回:**
```json
{
    "success": true,
    "request_id": "xxx",
    "dialog_id": "SIP会话ID",
    "media_port": 30000,
    "stream_url": "rtp://192.168.1.100:30000"
}
```

#### 停止实时视频

```php
$client->sendCommand(
    '34020000001320948622',
    '34020000001310000001',
    'stop_live_video',
    []
);
```

**信令流程:**
1. 服务器 → 设备: BYE
2. 设备 → 服务器: 200 OK
3. 设备停止推流

---

### 2. 录像回放

#### 开始录像回放

```php
$client->sendCommand(
    '34020000001320948622',
    '34020000001310000001',
    'start_playback',
    [
        'start_time' => '2024-12-01T00:00:00',  // 录像开始时间
        'end_time' => '2024-12-01T01:00:00',    // 录像结束时间
    ]
);
```

**信令流程:** 同实时视频,但SDP中session name为"Playback"

**返回:** 同实时视频

#### 停止录像回放

```php
$client->sendCommand(
    '34020000001320948622',
    '34020000001310000001',
    'stop_playback',
    []
);
```

---

### 3. 设备信息查询

#### 查询设备信息

```php
$client->sendCommand(
    '34020000001320948622',
    '',
    'query_device_info',
    []
);
```

**信令流程:**
1. 服务器 → 设备: MESSAGE (XML: DeviceInfo查询)
2. 设备 → 服务器: 200 OK (XML: 设备信息)

**设备响应示例:**
```xml
<?xml version="1.0" encoding="GB2312"?>
<Response>
  <CmdType>DeviceInfo</CmdType>
  <SN>1</SN>
  <DeviceID>34020000001320948622</DeviceID>
  <DeviceName>Camera 1</DeviceName>
  <Manufacturer>Hikvision</Manufacturer>
  <Model>DS-2CD2345FWD</Model>
  <Firmware>V5.6.3</Firmware>
  <Channel>1</Channel>
</Response>
```

#### 查询设备状态

```php
$client->sendCommand(
    '34020000001320948622',
    '',
    'query_device_status',
    []
);
```

**设备响应示例:**
```xml
<?xml version="1.0" encoding="GB2312"?>
<Response>
  <CmdType>DeviceStatus</CmdType>
  <SN>2</SN>
  <DeviceID>34020000001320948622</DeviceID>
  <Online>ONLINE</Online>
  <Status>OK</Status>
  <Encode>ON</Encode>
  <Record>ON</Record>
</Response>
```

#### 查询设备目录

```php
$client->sendCommand(
    '34020000001320948622',
    '',
    'query_catalog',
    []
);
```

**设备响应:** 返回所有通道信息(已实现)

---

### 4. 录像文件查询

```php
$client->sendCommand(
    '34020000001320948622',
    '34020000001310000001',
    'query_record',
    [
        'start_time' => '2024-12-01T00:00:00',
        'end_time' => '2024-12-01T23:59:59',
        'type' => 'all'  // all: 所有, time: 时间, alarm: 报警, manual: 手动
    ]
);
```

**信令流程:**
1. 服务器 → 设备: MESSAGE (XML: RecordInfo查询)
2. 设备 → 服务器: 200 OK (XML: 录像文件列表)

**设备响应示例:**
```xml
<?xml version="1.0" encoding="GB2312"?>
<Response>
  <CmdType>RecordInfo</CmdType>
  <SN>3</SN>
  <DeviceID>34020000001310000001</DeviceID>
  <SumNum>2</SumNum>
  <RecordList Num="2">
    <Item>
      <DeviceID>34020000001310000001</DeviceID>
      <Name>录像1</Name>
      <FilePath>/record/2024-12-01/video.mp4</FilePath>
      <Address>Disk1</Address>
      <StartTime>2024-12-01T08:00:00</StartTime>
      <EndTime>2024-12-01T10:00:00</EndTime>
      <Secrecy>0</Secrecy>
      <Type>time</Type>
    </Item>
  </RecordList>
</Response>
```

---

### 5. PTZ 云台控制

```php
// 向上移动
$client->sendCommand(
    '34020000001320948622',
    '34020000001310000001',
    'ptz_control',
    [
        'command' => 'up',     // up, down, left, right, zoom_in, zoom_out, stop
        'speed' => 5           // 速度: 1-255
    ]
);

// 向左移动
$client->sendCommand(
    '34020000001320948622',
    '34020000001310000001',
    'ptz_control',
    ['command' => 'left', 'speed' => 8]
);

// 放大
$client->sendCommand(
    '34020000001320948622',
    '34020000001310000001',
    'ptz_control',
    ['command' => 'zoom_in', 'speed' => 5]
);

// 停止
$client->sendCommand(
    '34020000001320948622',
    '34020000001310000001',
    'ptz_control',
    ['command' => 'stop']
);
```

**信令流程:**
1. 服务器 → 设备: MESSAGE (XML: DeviceControl/PTZCmd)
2. 设备 → 服务器: 200 OK

**PTZ命令XML示例:**
```xml
<?xml version="1.0" encoding="GB2312"?>
<Control>
  <CmdType>DeviceControl</CmdType>
  <SN>4</SN>
  <DeviceID>34020000001310000001</DeviceID>
  <PTZCmd>A50F85808000CB</PTZCmd>
</Control>
```

**PTZ命令格式说明:**
- 格式: `A50F[水平][垂直][变倍]00[校验]`
- 水平: 128(中) + 速度(左负右正)
- 垂直: 128(中) + 速度(下负上正)
- 变倍: 高4位放大,低4位缩小

---

### 6. 语音对讲

#### 开始语音对讲(Broadcast)

```php
$client->sendCommand(
    '34020000001320948622',
    '34020000001310000001',
    'start_broadcast',
    []
);
```

**信令流程(设备主动):**
1. 设备 → 服务器: INVITE (Subject包含broadcast,SDP说明音频接收参数)
2. 服务器 → 设备: 200 OK (SDP说明音频推送参数)
3. 设备 → 服务器: ACK
4. 服务器推送音频流到设备

**返回:**
```json
{
    "success": true,
    "request_id": "xxx",
    "dialog_id": "SIP会话ID",
    "audio_port": 50000,
    "ssrc": "0123456789",
    "push_url": "rtsp://127.0.0.1:554/broadcast/34020000001320948622_34020000001310000001?ssrc=0123456789",
    "push_command": "ffmpeg -re -i {input_file} -acodec pcm_alaw -ar 8000 -ac 1 -f rtsp 'rtsp://...'"
}
```

**使用ffmpeg推流:**
```bash
# 从麦克风实时采集
ffmpeg -f alsa -i hw:0 -acodec pcm_alaw -ar 8000 -ac 1 -f rtsp 'rtsp://127.0.0.1:554/broadcast/...'

# 从音频文件推流
ffmpeg -re -i test.mp3 -acodec pcm_alaw -ar 8000 -ac 1 -f rtsp 'rtsp://127.0.0.1:554/broadcast/...'

# 从网络麦克风(WebRTC)推流
ffmpeg -f lavfi -i anullsrc -acodec pcm_alaw -ar 8000 -ac 1 -f rtsp 'rtsp://127.0.0.1:554/broadcast/...'
```

**音频参数:**
- 编码: PCMA(G.711A) 或 PCMU(G.711U)
- 采样率: 8000Hz
- 声道: 单声道(1)
- 码率: 64kbps

#### 停止语音对讲

```php
$client->sendCommand(
    '34020000001320948622',
    '34020000001310000001',
    'stop_broadcast',
    []
);
```

**信令流程:**
1. 服务器 → 设备: BYE
2. 设备 → 服务器: 200 OK
3. 停止音频推流

**注意事项:**
1. **设备兼容性**: 海康只支持UDP,大华支持TCP主动
2. **公网限制**: UDP模式无法用于公网对讲(NAT穿透)
3. **HTTPS要求**: Web浏览器采集音频需要HTTPS
4. **延迟控制**: 建议使用低延迟编码器参数

---

## 完整使用示例

### PHP客户端代码

```php
<?php

class GB28181Client
{
    private $redis;
    
    public function __construct(array $redisConfig)
    {
        $this->redis = new Redis();
        $this->redis->connect($redisConfig['host'], $redisConfig['port']);
        if (isset($redisConfig['password'])) {
            $this->redis->auth($redisConfig['password']);
        }
    }
    
    /**
     * 发送命令到GB28181服务器
     */
    public function sendCommand(
        string $deviceId, 
        string $channelId, 
        string $action, 
        array $params = []
    ): bool {
        $requestId = uniqid('req_');
        
        $command = [
            'request_id' => $requestId,
            'action' => $action,
            'device_id' => $deviceId,
            'channel_id' => $channelId ?: $deviceId,
            'timestamp' => time(),
            'params' => $params
        ];
        
        // 推送到Redis队列
        $result = $this->redis->lPush(
            'gb28181:commands', 
            json_encode($command)
        );
        
        return $result !== false;
    }
    
    /**
     * 开始实时视频
     */
    public function startLiveVideo(string $deviceId, string $channelId): bool
    {
        return $this->sendCommand($deviceId, $channelId, 'start_live_video');
    }
    
    /**
     * 停止实时视频
     */
    public function stopLiveVideo(string $deviceId, string $channelId): bool
    {
        return $this->sendCommand($deviceId, $channelId, 'stop_live_video');
    }
    
    /**
     * 开始录像回放
     */
    public function startPlayback(
        string $deviceId, 
        string $channelId,
        string $startTime,
        string $endTime
    ): bool {
        return $this->sendCommand($deviceId, $channelId, 'start_playback', [
            'start_time' => $startTime,
            'end_time' => $endTime
        ]);
    }
    
    /**
     * PTZ控制
     */
    public function ptzControl(
        string $deviceId, 
        string $channelId, 
        string $command, 
        int $speed = 5
    ): bool {
        return $this->sendCommand($deviceId, $channelId, 'ptz_control', [
            'command' => $command,
            'speed' => $speed
        ]);
    }
    
    /**
     * 查询录像
     */
    public function queryRecord(
        string $deviceId,
        string $channelId,
        string $startTime,
        string $endTime,
        string $type = 'all'
    ): bool {
        return $this->sendCommand($deviceId, $channelId, 'query_record', [
            'start_time' => $startTime,
            'end_time' => $endTime,
            'type' => $type
        ]);
    }
}

// 使用示例
$client = new GB28181Client([
    'host' => '127.0.0.1',
    'port' => 6379,
]);

// 1. 开始实时视频
$client->startLiveVideo(
    '34020000001320948622',  // 设备ID
    '34020000001310000001'   // 通道ID
);

// 2. PTZ控制
$client->ptzControl(
    '34020000001320948622',
    '34020000001310000001',
    'up',
    8
);

// 3. 查询录像
$client->queryRecord(
    '34020000001320948622',
    '34020000001310000001',
    '2024-12-01T00:00:00',
    '2024-12-01T23:59:59'
);

// 4. 开始回放
$client->startPlayback(
    '34020000001320948622',
    '34020000001310000001',
    '2024-12-01T08:00:00',
    '2024-12-01T10:00:00'
);

// 5. 停止回放
sleep(60);  // 观看60秒
$client->stopLiveVideo(
    '34020000001320948622',
    '34020000001310000001'
);
```

---

## 媒体流接收

### RTP流处理

服务器收到实时视频或回放的INVITE响应后,会在指定端口接收RTP流:

```bash
# 使用ffmpeg接收并转换
ffmpeg -protocol_whitelist "file,udp,rtp" \
    -i rtp://192.168.1.100:30000 \
    -c copy output.mp4

# 使用gstreamer接收
gst-launch-1.0 \
    udpsrc port=30000 caps="application/x-rtp" ! \
    rtpjitterbuffer ! \
    rtpmp2tdepay ! \
    tsdemux ! \
    h264parse ! \
    avdec_h264 ! \
    autovideosink
```

### 端口分配

- 起始端口: `media_server_port_start` (默认30000)
- 结束端口: `media_server_port_end` (默认40000)
- RTP/RTCP: 每个会话占用2个端口(偶数RTP,奇数RTCP)

---

## 错误处理

### 常见错误

1. **设备不在线**
```json
{
    "success": false,
    "request_id": "xxx",
    "error": "Device not registered: 34020000001320948622"
}
```

2. **参数缺失**
```json
{
    "success": false,
    "request_id": "xxx",
    "error": "Missing start_time or end_time"
}
```

3. **会话不存在**
```json
{
    "success": false,
    "request_id": "xxx",
    "error": "No active live video session"
}
```

---

## 配置说明

### GB28181Handler配置

```php
$config = [
    'server_id' => '34020000002000000001',
    'server_domain' => '3402000000',
    'heartbeat_timeout' => 180,
    'check_interval' => 30,
    'register_expires' => 3600,
    
    // 媒体服务器配置
    'media_server_ip' => '192.168.1.100',
    'media_server_port_start' => 30000,
    'media_server_port_end' => 40000,
    
    'debug' => true,
];
```

---

## 注意事项

1. **设备ID和通道ID格式**: 必须是20位数字
2. **时间格式**: ISO 8601格式 `YYYY-MM-DDTHH:MM:SS`
3. **端口管理**: 生产环境需实现完整的端口池管理
4. **会话超时**: 默认1小时后自动清理
5. **并发控制**: 单个通道同时只能有一个视频会话
6. **编码格式**: 设备响应XML使用GB2312编码(已自动处理)

---

## 调试

### 启用调试模式

```php
$config['debug'] = true;
```

### 查看日志

```
[2024-12-01 10:00:00] [INFO] [CommandDispatcher] Dispatch command: start_live_video
[2024-12-01 10:00:00] [INFO] [CommandDispatcher] Start live video: 34020000001310000001
[2024-12-01 10:00:00] [INFO] [CommandDispatcher] Live video session created: 34020000001320948622:34020000001310000001:live (Dialog: 1, Port: 30000)
```

### 查看活跃会话

```php
$sessions = $handler->getCommandDispatcher()->getActiveSessions();
print_r($sessions);
```

输出:
```
Array
(
    [34020000001320948622:34020000001310000001:live] => Array
        (
            [request_id] => req_12345
            [dialog_id] => 1
            [device_id] => 34020000001320948622
            [channel_id] => 34020000001310000001
            [type] => live
            [media_port] => 30000
            [started_at] => 1701417600
        )
)
```
