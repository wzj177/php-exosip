# GB28181 + ZLMediaKit 集成指南

## 概述

本指南介绍如何使用原生 SDP 解析器 + ZLMediaKit 实现完整的 GB28181 视频监控对接。

---

## 架构图

```
┌─────────────┐         INVITE/SDP          ┌─────────────┐
│   GB28181   │  ─────────────────────────> │  PHP ExoSip │
│   设备端    │  <─────────────────────────  │   服务器    │
│             │         200 OK/SDP           │             │
└─────────────┘                              └──────┬──────┘
       │                                            │
       │ RTP Stream (PS/H.264/MPEG4)               │ HTTP API
       │ SSRC: 0100000001                          │
       ↓                                            ↓
┌─────────────┐                              ┌─────────────┐
│ ZLMediaKit  │  <────────────────────────── │ ZLM Client  │
│ 流媒体服务器 │         openRtpServer        │   (PHP)     │
│             │                              │             │
└──────┬──────┘                              └─────────────┘
       │
       │ 转换输出
       ├──> RTSP://ip:554/rtp/stream_id
       ├──> HTTP-FLV://ip:80/rtp/stream_id.live.flv
       ├──> HLS://ip:80/rtp/stream_id/hls.m3u8
       └──> WebRTC (可选)
```

---

## 核心流程

### 1. 实时视频点播

```
服务器                   设备                    ZLM
  │                       │                      │
  │  1. openRtpServer    │                      │
  ├──────────────────────────────────────────>  │
  │  <- port: 30000                              │
  │                       │                      │
  │  2. INVITE + SDP      │                      │
  │  (port: 30000)        │                      │
  ├────────────────────> │                      │
  │                       │                      │
  │  3. 200 OK + SDP      │                      │
  │  <- SSRC: 0100000001  │                      │
  │  <- IP: 192.168.1.200 │                      │
  │  <- Port: 6000        │                      │
  <─────────────────────┤                      │
  │                       │                      │
  │  4. ACK               │                      │
  ├────────────────────> │                      │
  │                       │                      │
  │                       │  5. RTP Stream       │
  │                       │  (SSRC: 0100000001)  │
  │                       ├─────────────────────>│
  │                       │                      │
  │                                              │
  │  6. getPlayUrls                              │
  ├──────────────────────────────────────────>  │
  │  <- RTSP/FLV/HLS URLs                        │
  <─────────────────────────────────────────────┤
```

### 2. 录像回放

```
服务器                   设备                    ZLM
  │                       │                      │
  │  1. INVITE + SDP      │                      │
  │  (回放请求)           │                      │
  ├────────────────────> │                      │
  │                       │                      │
  │  2. 200 OK + SDP      │                      │
  │  <- SSRC: 0200000002  │                      │
  │  <- IP: 192.168.1.200 │                      │
  │  <- Port: 6002        │                      │
  <─────────────────────┤                      │
  │                       │                      │
  │  3. startSendRtp      │                      │
  │  (SSRC, IP, Port)     │                      │
  ├──────────────────────────────────────────>  │
  │                       │                      │
  │                       │  4. RTP Stream       │
  │                       │  (SSRC: 0200000002)  │
  │                       <──────────────────────┤
  │                       │                      │
```

---

## 配置步骤

### Step 1: 安装 ZLMediaKit

```bash
# 克隆仓库
git clone --depth 1 https://github.com/ZLMediaKit/ZLMediaKit.git
cd ZLMediaKit

# 初始化子模块
git submodule update --init

# 编译
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# 启动
cd ../release/linux/Release
./MediaServer -d &
```

### Step 2: 配置 ZLMediaKit

编辑 `conf/config.ini`:

```ini
[api]
# API 密钥 (必须配置)
secret=035c73f7-bb6b-4889-a715-d9eb2d1925cc

# HTTP API 端口
apiPort=80

[general]
# 流媒体服务器 IP (内网 IP)
mediaServerId=your_server_id

[rtp]
# GB28181 RTP 接收配置
# 端口范围
port=30000-40000

# 是否启用 TCP
tcpEnable=1

# SSRC 检查
checkSource=1

[protocol]
# 启用的协议
enable_rtsp=1
enable_rtmp=1
enable_http_flv=1
enable_hls=1
enable_mp4=1

[rtsp]
port=554

[rtmp]
port=1935

[http]
port=80
```

### Step 3: 配置 PHP 服务器

```php
$config = [
    // SIP 配置
    'server_id' => '34020000002000000001',      // 服务器国标编码
    'server_domain' => '3402000000',            // 服务器域
    'server_ip' => '192.168.1.100',             // 服务器 IP
    'server_port' => 5060,
    'mode' => 'UDP',                            // UDP/TCP
    
    // ZLM 配置
    'zlm_host' => '127.0.0.1',                  // ZLM 地址
    'zlm_port' => 80,                           // ZLM HTTP API 端口
    'zlm_secret' => '035c73f7-bb6b-4889-a715-d9eb2d1925cc',  // 与 config.ini 一致
    
    'media_ip' => '192.168.1.100',              // 流媒体服务器 IP (SDP 中使用)
    'debug' => true,
];
```

---

## 使用示例

### 实时视频点播

```php
require_once __DIR__ . '/gb28181_zlm_integration.php';

$server = new GB28181MediaServer($config);

// 在后台启动服务器
$pid = pcntl_fork();
if ($pid == 0) {
    $server->start();
    exit(0);
}

// 发起点播请求
sleep(1);  // 等待服务器启动

$deviceId = '34020000001320000001';
$channelId = '34020000001320000001';

$result = $server->startLiveVideo($deviceId, $channelId);

if ($result) {
    echo "点播成功!\n";
    echo "Call-ID: {$result['call_id']}\n";
    echo "Stream-ID: {$result['stream_id']}\n";
    echo "ZLM Port: {$result['zlm_port']}\n";
    
    // 等待设备响应 (在 handleResponse 中异步处理)
    sleep(5);
    
    // 获取播放地址
    $zlm = new ZLMClient([
        'host' => '127.0.0.1',
        'port' => 80,
        'secret' => '035c73f7-bb6b-4889-a715-d9eb2d1925cc',
    ]);
    
    $playUrls = $zlm->getPlayUrls($result['stream_id']);
    
    echo "\n播放地址:\n";
    echo "RTSP: {$playUrls['rtsp']}\n";
    echo "HTTP-FLV: {$playUrls['http_flv']}\n";
    echo "HLS: {$playUrls['hls']}\n";
    
    // 在浏览器中播放
    // flv.js: http://192.168.1.100/rtp/34020000001320000001_34020000001320000001.live.flv
    // hls.js: http://192.168.1.100/rtp/34020000001320000001_34020000001320000001/hls.m3u8
    
    // 30 秒后停止
    sleep(30);
    $server->stopLiveVideo($result['call_id']);
}
```

### 查询流列表

```php
$zlm = new ZLMClient($config);

// 获取所有 RTP 流
$streams = $zlm->getMediaList('rtp');

if ($streams && $streams['code'] === 0) {
    foreach ($streams['data'] as $stream) {
        echo "Stream: {$stream['app']}/{$stream['stream']}\n";
        echo "  Readers: {$stream['readerCount']}\n";
        echo "  Duration: {$stream['totalReaderCount']} seconds\n";
    }
}
```

### 录制流

```php
// 开始录制
$zlm->startRecord([
    'type' => 1,  // MP4
    'vhost' => '__defaultVhost__',
    'app' => 'rtp',
    'stream' => '34020000001320000001_34020000001320000001'
]);

// 30 秒后停止
sleep(30);

$zlm->stopRecord([
    'type' => 1,
    'vhost' => '__defaultVhost__',
    'app' => 'rtp',
    'stream' => '34020000001320000001_34020000001320000001'
]);

// 录像文件位置: release/linux/Release/www/record/...
```

---

## 原生 SDP 解析器使用

### 基本用法

```php
// 在 handleResponse 中
public function handleResponse(SipEvent $event): void
{
    if ($event->getCode() == 200) {
        // 🎯 使用原生 SDP 解析器
        $sdp = $event->getSdp();
        
        if ($sdp) {
            // 标准字段
            $deviceIp = $sdp['connection']['addr'];
            $devicePort = $sdp['medias'][0]['port'];
            $protocol = $sdp['medias'][0]['proto'];
            
            // GB28181 扩展
            $ssrc = $sdp['gb28181']['ssrc'] ?? null;
            
            // 通知 ZLM
            $this->notifyZLM($deviceIp, $devicePort, $ssrc);
        }
    }
}
```

### SDP 结构示例

```php
array(6) {
  ["version"]=> string(1) "0"
  
  ["origin"]=> array(6) {
    ["username"]=> "34020000001320000001"
    ["sess_id"]=> "0"
    ["sess_version"]=> "0"
    ["nettype"]=> "IN"
    ["addrtype"]=> "IP4"
    ["addr"]=> "192.168.1.100"
  }
  
  ["session_name"]=> "Play"
  
  ["connection"]=> array(3) {
    ["c_nettype"]=> "IN"
    ["c_addrtype"]=> "IP4"
    ["addr"]=> "192.168.1.100"  // 设备 IP
  }
  
  ["medias"]=> array(1) {
    [0]=> array(6) {
      ["media"]=> "video"
      ["port"]=> 6000            // 设备端口
      ["proto"]=> "RTP/AVP"
      ["payload"]=> "96 98 97"
      ["attributes"]=> array {
        ["recvonly"]=> NULL
        ["rtpmap"]=> "96 PS/90000"
      }
    }
  }
  
  ["gb28181"]=> array(2) {
    ["ssrc"]=> "0100000001"      // 设备 SSRC (关键!)
    ["f"]=> ""
  }
}
```

---

## ZLMediaKit API 参考

### openRtpServer - 分配 RTP 接收端口

**用途**: 实时视频点播

```php
$result = $zlm->openRtpServer([
    'stream_id' => '34020000001320000001_34020000001320000001',
    'tcp_mode' => 0,  // 0: UDP, 1: TCP 被动, 2: TCP 主动
    'port' => 0,      // 0: 自动分配
    'enable_mp4' => 1 // 1: 录制
]);

// 返回:
// ['code' => 0, 'port' => 30000]
```

### startSendRtp - 主动推流到设备

**用途**: 录像回放

```php
$result = $zlm->startSendRtp([
    'stream' => 'playback_stream_id',
    'ssrc' => '0200000002',          // 从设备 SDP 提取
    'dst_url' => '192.168.1.200',    // 设备 IP
    'dst_port' => 6002,              // 设备端口
    'is_udp' => 1
]);
```

### closeRtpServer - 关闭 RTP 端口

```php
$zlm->closeRtpServer('34020000001320000001_34020000001320000001');
```

### getMediaList - 查询流列表

```php
$streams = $zlm->getMediaList('rtp');  // 只查 RTP 流
```

### closeStream - 强制关闭流

```php
$zlm->closeStream([
    'schema' => 'rtp',
    'app' => 'rtp',
    'stream' => '34020000001320000001_34020000001320000001',
    'force' => 1
]);
```

---

## 常见问题

### Q1: 设备推流后 ZLM 没有流？

**A**: 检查以下几点：
1. 设备 SDP 中的 SSRC 是否正确提取
2. ZLM 的 `checkSource=1` 配置要求 SSRC 匹配
3. 网络防火墙是否开放端口
4. 查看 ZLM 日志: `tail -f MediaServer.log`

### Q2: 如何在浏览器播放？

**A**: 使用 HTTP-FLV (推荐):
```html
<script src="https://cdn.jsdelivr.net/npm/flv.js/dist/flv.min.js"></script>
<video id="videoElement"></video>
<script>
var flvPlayer = flv.createPlayer({
    type: 'flv',
    url: 'http://192.168.1.100/rtp/34020000001320000001_34020000001320000001.live.flv'
});
flvPlayer.attachMediaElement(document.getElementById('videoElement'));
flvPlayer.load();
flvPlayer.play();
</script>
```

### Q3: SSRC 为什么这么重要？

**A**: 
- GB28181 强制要求设备在 200 OK 中包含 SSRC (y= 字段)
- ZLM 使用 SSRC 识别和区分不同设备的 RTP 流
- 多路复用场景下必须用 SSRC 匹配流
- 缺失 SSRC 会导致 ZLM 无法正确接收流

### Q4: 录像文件存储在哪里？

**A**: 
```
ZLMediaKit/release/linux/Release/www/record/
  └── rtp/
      └── 34020000001320000001_34020000001320000001/
          └── 2025-11-30/
              └── 14-30-00.mp4
```

---

## 性能优化

### 1. ZLM 配置优化

```ini
[general]
# 最大线程数
maxWorkThreadNum=4

[rtp]
# RTP 缓存大小 (MB)
rtpMaxSize=100

# 超时时间 (秒)
timeoutSec=15

[hls]
# HLS 分片时长 (秒)
segDur=2
# HLS 分片数量
segNum=5
```

### 2. PHP 并发处理

使用 Master-Worker-Task 模式处理 ZLM API 调用：

```php
$sip->onTask = function($taskId, $data) use ($zlm) {
    if ($data['type'] === 'open_rtp_server') {
        return $zlm->openRtpServer($data['params']);
    }
    // 其他耗时操作
};

// Worker 中投递任务
$taskId = $sip->addTask([
    'type' => 'open_rtp_server',
    'params' => ['stream_id' => $streamId]
]);
```

---

## 监控和调试

### 查看 ZLM 状态

```bash
# HTTP API
curl "http://127.0.0.1:80/index/api/getServerConfig?secret=YOUR_SECRET"

# 查看流列表
curl "http://127.0.0.1:80/index/api/getMediaList?secret=YOUR_SECRET"

# 查看线程负载
curl "http://127.0.0.1:80/index/api/getThreadsLoad?secret=YOUR_SECRET"
```

### PHP 调试

```php
// 开启调试模式
$config['debug'] = true;

// 查看 SDP 解析结果
$sdp = $event->getSdp();
var_dump($sdp);

// 检查会话状态
var_dump($server->sessions);
```

---

## 参考资料

- [ZLMediaKit GitHub](https://github.com/ZLMediaKit/ZLMediaKit)
- [ZLMediaKit HTTP API](https://github.com/ZLMediaKit/ZLMediaKit/wiki/MediaServer%E6%94%AF%E6%8C%81%E7%9A%84HTTP-API)
- [GB/T 28181-2016 标准](https://openstd.samr.gov.cn/)
- [原生 SDP 解析器文档](./SDP_PARSER_NATIVE.md)

---

**文档版本**: 1.0  
**更新日期**: 2025-11-30
