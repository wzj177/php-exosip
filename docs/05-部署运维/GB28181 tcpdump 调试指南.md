# GB28181 tcpdump 调试指南

本文用于排查 GB28181 信令和媒体流问题，重点覆盖：

- SIP 信令 UDP/TCP 注册、心跳、MESSAGE、INVITE
- RTP 媒体 UDP
- RTP over TCP 被动模式（设备主动连接平台/ZLM）
- RTP over TCP 主动模式（平台/ZLM 主动连接设备）
- 如何判断是设备未发包、网络未到达、扩展未收到事件，还是业务层未正确响应

## 1. 基本原则

排障时先分层，不要直接判断扩展或业务代码有问题。

链路顺序是：

```text
设备/NVR/IPC
  -> 网络/安全组/防火墙/NAT
  -> Linux 内核网卡收包
  -> eXosip/PHP 扩展
  -> PHP onRegister/onMessage/onInvite 回调
  -> 业务处理/数据库/API/ZLM
```

判断标准：

- `tcpdump` 看不到包：包没有到服务器网卡，优先查设备配置、协议、端口、网络。
- `tcpdump` 看到包，但扩展没有 `Event received`：查 eXosip/扩展监听协议、端口、进程、旧进程残留。
- 扩展有 `Event received`，但业务没注册/没响应：查 PHP 回调、Digest 认证、realm、密码、业务异常。
- 服务器发了 `401/200 OK`，设备仍不上线：查设备是否收到响应、认证参数、设备兼容性。

## 2. 启动前确认

确认当前加载的扩展版本：

```bash
php --ri exosip
```

应能看到类似：

```text
Version => 1.0.1-tcp-worker
Build date => May 24 2026 21:46:23
```

启动服务：

```bash
php webman gb28181:server start -d
```

确认日志中的协议：

```text
[Worker] eXosip listening on 0.0.0.0:15060 (UDP)
```

或：

```text
[Worker] eXosip listening on 0.0.0.0:15060 (TCP)
```

确认监听端口：

```bash
# UDP 信令
ss -lnup | grep ':15060'

# TCP 信令
ss -lntp | grep ':15060'

# TCP 已建立连接
ss -antp | grep ':15060'
```

注意：eXosip2 5.3 的一个 `eXosip_t` context 只能监听一个 transport，当前扩展只支持 `UDP` 或 `TCP`。需要同时提供 UDP/TCP 信令时，建议在 PHP 业务层用两份配置启动两个独立网关实例，例如 `gb28181-udp` 和 `gb28181-tcp`。

## 3. SIP 信令 UDP 抓包

服务配置：

```php
'transport' => 'UDP',
'sip_port' => 15060,
```

抓包命令：

```bash
tcpdump -ni any -s0 -A 'udp port 15060'
```

首次注册应看到：

```text
REGISTER sip:3402000000@...
Via: SIP/2.0/UDP ...
From: <sip:34020000001320456630@3402000000>
To: <sip:34020000001320456630@3402000000>
CSeq: 1 REGISTER
```

如果开启认证，正常流程通常是：

```text
设备 -> 平台: REGISTER
平台 -> 设备: 401 Unauthorized + WWW-Authenticate
设备 -> 平台: REGISTER + Authorization
平台 -> 设备: 200 OK
```

快速看 REGISTER 和响应：

```bash
tcpdump -ni any -s0 -A 'udp port 15060' | egrep -i 'REGISTER|SIP/2.0 401|SIP/2.0 200|Authorization|WWW-Authenticate|Via:|From:|To:|Call-ID|CSeq'
```

判断：

- 无任何输出：设备没有把 UDP 包发到本机 `15060`。
- 只有 REGISTER，无 401/200：扩展或业务没有响应，查扩展日志和 PHP 回调。
- 有 401，无第二次 REGISTER：设备未接受认证挑战，查 realm、密码、设备认证能力。
- 有第二次 REGISTER + Authorization，但返回 403：查 Digest 算法、realm、uri、密码。
- 有 200 OK，但平台页面不上线：查业务层设备状态更新、API 回调、数据库。

## 4. SIP 信令 TCP 抓包

服务配置：

```php
'transport' => 'TCP',
'sip_port' => 15060,
```

确认监听：

```bash
ss -lntp | grep ':15060'
```

看连接是否建立：

```bash
ss -antp | grep ':15060'
```

正常应看到 `ESTAB`：

```text
ESTAB 0 0 服务器IP:15060 设备IP:随机端口 users:(("php",pid=...,fd=...))
```

抓包：

```bash
tcpdump -ni any -s0 -A 'tcp port 15060'
```

只看握手和 SIP 文本：

```bash
tcpdump -ni any -s0 -A 'tcp port 15060' | egrep -i 'Flags|REGISTER|SIP/2.0 401|SIP/2.0 200|Authorization|WWW-Authenticate|Via:|From:|To:|Call-ID|CSeq'
```

判断：

- 没有 SYN：设备没有发 TCP 到本机端口。
- 有 SYN，无 ESTAB：防火墙、安全组、本机监听、回程网络有问题。
- 有 ESTAB，无 REGISTER：设备连接建立后没有发送 SIP 注册。
- 有 REGISTER，但扩展无 `Event received`：查 eXosip TCP 事件处理、扩展是否加载新版本、是否旧进程残留。
- 有 REGISTER 且扩展收到，但业务不上线：按认证和业务流程查。

## 5. 按设备 IP 抓包

当某台设备没有任何 REGISTER 时，不要只抓端口，先按设备公网出口 IP 抓全量：

```bash
tcpdump -ni any -s0 -A 'host 设备公网IP'
```

如果不知道设备公网 IP：

- 看 LiveQing 或其他平台里显示的来源 IP。
- 看路由器/NAT 出口。
- 临时让设备注册到可控服务，记录来源地址。

按常见 SIP 端口一起抓：

```bash
tcpdump -ni any -s0 -A 'host 设备公网IP and (udp port 5060 or udp port 15060 or tcp port 5060 or tcp port 15060)'
```

如果看到设备打到了 `5060`，说明设备端口配置未生效或仍使用默认端口。

如果看到设备打 TCP，而当前服务是 UDP，说明协议配置不一致。

## 6. 与 LiveQing 对比

同一台服务器上 LiveQing 能注册，只能说明网络大方向可达，不能直接证明设备现在打到了 PHP 服务。

对比 LiveQing 注册成功时的这些字段：

```text
来源 IP
来源端口
传输协议 UDP/TCP
设备 ID
SIP 服务器 ID
SIP 域/realm
目标服务器 IP
目标端口
```

设备从 LiveQing 切回本服务后，必须重新保存/重启国标配置。很多 NVR/IPC 不会立刻重新 REGISTER。

建议操作：

```text
关闭国标启用 -> 保存
开启国标启用 -> 保存
必要时重启设备/NVR
```

## 7. RTP 媒体 UDP 抓包

SIP 注册正常后，点播/回放/对讲涉及媒体流。媒体端口通常由 ZLM/openRtpServer 分配，不一定是 SIP 端口。

先从业务日志或 API 返回里拿到 RTP 端口，例如：

```text
local_port=30000
ssrc=0100000001
```

抓 UDP RTP：

```bash
tcpdump -ni any -s0 -vv 'udp port 30000'
```

同时抓 RTP/RTCP 一对端口：

```bash
tcpdump -ni any -s0 -vv 'udp port 30000 or udp port 30001'
```

判断：

- SIP INVITE/200 OK/ACK 都正常，但 RTP 端口无包：设备没有推流，查 SDP IP/端口、NAT、设备媒体配置。
- 有 UDP 包但 ZLM 无流：查 SSRC、payload type、ZLM openRtpServer 参数、流 ID。
- 只有 RTCP 或少量 UDP：设备媒体协商失败或推流中断。

保存 pcap：

```bash
tcpdump -ni any -s0 -w /tmp/gb28181-rtp-udp.pcap 'udp port 30000 or udp port 30001'
```

## 8. RTP over TCP 被动模式

GB28181 TCP 被动媒体模式通常含义是：平台/ZLM 监听 TCP 端口，设备主动连接平台推 RTP over TCP。

SDP 常见字段：

```text
m=video 30000 TCP/RTP/AVP 96
a=setup:passive
```

这里的 `passive` 指平台被动等待连接，设备应主动连平台的媒体端口。

确认 ZLM/平台监听媒体端口：

```bash
ss -lntp | grep ':30000'
```

抓 TCP 媒体端口：

```bash
tcpdump -ni any -s0 -vv 'tcp port 30000'
```

看是否建立连接：

```bash
ss -antp | grep ':30000'
```

正常应看到：

```text
ESTAB 0 0 服务器IP:30000 设备IP:随机端口
```

判断：

- 没有 SYN：设备没有按 SDP 连接平台媒体端口，查 SDP IP/端口是否设备可达。
- 有 SYN，无 ESTAB：网络/防火墙/安全组/监听问题。
- ESTAB 后无数据：设备建立连接但未推 RTP，查 ACK、SSRC、设备媒体状态。
- ESTAB 且有大量 TCP 数据，但 ZLM 无流：查 RTP over TCP 封装、SSRC、ZLM tcp_mode 参数。

保存 pcap：

```bash
tcpdump -ni any -s0 -w /tmp/gb28181-rtp-tcp-passive.pcap 'tcp port 30000'
```

## 9. RTP over TCP 主动模式

TCP 主动媒体模式通常含义是：设备监听 TCP 端口，平台/ZLM 主动连接设备。

SDP 常见字段：

```text
m=video 20002 TCP/RTP/AVP 96
a=setup:active
```

实际方向要以业务约定和 SDP 为准。排障重点是确认谁应该发起 TCP 连接。

如果平台主动连接设备，服务器上抓：

```bash
tcpdump -ni any -s0 -vv 'host 设备IP and tcp'
```

看本机是否发出 SYN：

```text
服务器IP:随机端口 > 设备IP:设备媒体端口 Flags [S]
```

判断：

- 本机没有 SYN：平台/ZLM 没有发起连接，查业务是否调用 startSendRtp 或 ZLM 参数。
- 本机有 SYN，设备不回 SYN-ACK：设备媒体端口不可达、防火墙/NAT、设备未监听。
- ESTAB 后无数据：媒体方向或 RTP 封装不匹配。

## 10. 同时抓 SIP 和媒体

排查点播/回放/对讲时，建议同时保存 SIP 和 RTP 包：

```bash
tcpdump -ni any -s0 -w /tmp/gb28181-sip-media.pcap \
'(udp port 15060 or tcp port 15060 or udp port 30000 or udp port 30001 or tcp port 30000)'
```

如果媒体端口是动态的，先抓 SIP，读 SDP 中的 `m=video` 端口，再抓媒体端口。

提取 SDP：

```bash
tcpdump -ni any -s0 -A 'udp port 15060 or tcp port 15060' | egrep -i 'INVITE|SIP/2.0 200|m=video|c=IN|a=setup|y='
```

关键字段：

```text
c=IN IP4 x.x.x.x       # 媒体目标 IP
m=video 30000 ...      # 媒体目标端口和协议
a=setup:passive/active # TCP 模式连接方向
y=0100000001           # SSRC
```

## 11. 常见问题判断表

| 现象 | 优先判断 |
| --- | --- |
| `ss` 有 UDP listen，但设备不上线 | 抓 `udp port 15060` 看 REGISTER 是否到达 |
| `tcpdump` 没任何 REGISTER | 设备目标 IP/端口/协议错误，或设备未重新注册 |
| LiveQing 能注册，本服务抓不到包 | 设备仍打 LiveQing、端口/协议未切换、配置未保存 |
| 抓到 REGISTER，扩展无 `Event received` | 查扩展版本、监听协议、旧进程、eXosip 事件 |
| 抓到 REGISTER，平台回 401，设备不再发 | 查 realm、密码、设备认证兼容 |
| 抓到 REGISTER + Authorization，平台回 403 | 查 Digest 计算：username、realm、uri、nonce、password |
| 平台回 200，页面仍离线 | 查业务状态更新、API hook、数据库 |
| SIP 正常，RTP UDP 无包 | 设备未推流、SDP IP/端口不可达 |
| RTP TCP 被动无 SYN | 设备没有主动连接平台媒体端口 |
| RTP TCP 主动 SYN 无响应 | 设备媒体端口不可达或未监听 |

## 12. 推荐排障流程

1. 确认扩展版本：

```bash
php --ri exosip
```

2. 确认服务监听协议：

```bash
ss -lnup | grep ':15060'
ss -lntp | grep ':15060'
```

3. 抓 SIP：

```bash
tcpdump -ni any -s0 -A 'udp port 15060 or tcp port 15060'
```

4. 如果某台设备无包，按设备 IP 抓：

```bash
tcpdump -ni any -s0 -A 'host 设备公网IP'
```

5. SIP 注册成功后，从 INVITE/200 OK SDP 中找媒体端口：

```bash
tcpdump -ni any -s0 -A 'udp port 15060 or tcp port 15060' | egrep -i 'm=video|c=IN|a=setup|y='
```

6. 按媒体模式抓 RTP：

```bash
# UDP RTP
tcpdump -ni any -s0 -vv 'udp port 媒体端口 or udp port 媒体端口+1'

# TCP RTP
tcpdump -ni any -s0 -vv 'tcp port 媒体端口'
```

7. 保存 pcap 后用 Wireshark 分析：

```bash
tcpdump -ni any -s0 -w /tmp/gb28181-debug.pcap \
'udp port 15060 or tcp port 15060 or udp portrange 30000-31000 or tcp portrange 30000-31000'
```
