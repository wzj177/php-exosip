# TCP模式修复方案 - 解决RTP流推送失败

## 问题分析

### 抓包数据显示

**平台发送的INVITE**:
```
c=IN IP4 10.20.2.8
m=video 33624 TCP/RTP/AVP 96 98 97
a=setup:active    ← 平台主动连接模式
```

**设备响应的200 OK**:
```
c=IN IP4 10.204.58.47  ← 设备内网IP
m=video 20002 TCP/RTP/AVP 96
a=setup:passive   ← 设备被动等待模式
```

### 根本原因

**不是TCP协商问题,而是NAT网络拓扑问题!**

#### RFC 4145 TCP协商标准

根据RFC 4145,TCP setup模式是**协商互补**的:

```
平台 setup:active  → 设备必须 setup:passive ✅ (设备响应正确!)
平台 setup:passive → 设备必须 setup:active
平台 setup:actpass → 设备可选 active 或 passive
```

**你的设备是合规的!** 它正确地响应了`passive`,这说明设备支持TCP协商。

#### 真正的问题: NAT穿透失败

1. **平台**: `a=setup:active` + IP: `10.20.2.8` - 表示平台会**主动连接**设备
2. **设备**: `a=setup:passive` + IP: `10.204.58.47` (内网) - 正确地等待平台连接
3. **问题**: 设备在**内网**,平台**无法直接访问**内网IP `10.204.58.47`!
4. **结果**: 
   - 平台尝试 TCP connect 到 `10.204.58.47:20002` → **网络不可达**
   - 设备一直等待连接 → **没有RTP流**
   - ZLM超时15秒 → **自动释放端口**

**这不是设备的问题,是网络架构导致的!**

## 解决方案

### 关键理解

**你的测试脚本可以选择TCP模式,说明:**

1. ✅ **设备支持TCP协商** - 设备正确响应了互补的setup模式
2. ✅ **设备支持多种模式** - 可以 active(主动) 或 passive(被动)
3. ⚠️ **问题在于网络环境** - 当前NAT环境下,`tcp_mode=2`(主动)不可用

### 为什么测试脚本可以选择?

```php
// GB28181Test.php - 第177行
$tcpModeQuestion = new ChoiceQuestion(
    '请选择 TCP 模式',
    [
        '0' => '0. UDP (局域网)',
        '1' => '1. TCP 被动 (推荐-公网)',
        '2' => '2. TCP 主动'
    ],
    '1'  // ← 默认是1 (被动模式)
);
```

**这三个选项都是有效的!** 但适用场景不同:

| 选项 | 设备要求 | 网络要求 | 场景 |
|------|---------|---------|------|
| `tcp_mode=0` (UDP) | 支持UDP | 无特殊要求 | ✅ 通用 |
| `tcp_mode=1` (TCP被动) | 设备能主动连接平台 | 平台IP可达 | ✅ **NAT环境** |
| `tcp_mode=2` (TCP主动) | 设备能被动接受连接 | **设备IP可达** | ❌ 仅内网 |

### 你的设备确实支持所有模式!

根据抓包,设备正确响应了`passive`,说明:
- ✅ 设备遵循RFC 4145标准
- ✅ 设备支持TCP协商
- ✅ 设备可以切换为`active`模式(当平台是passive时)

**问题**: 你选了`tcp_mode=2`,但设备在**内网**,平台无法连接到它。

### 方案1: 使用TCP被动模式 (推荐 ✅)

**修改**: 让设备主动连接平台

```php
// 在测试脚本中选择选项1
// 或修改代码默认值:
$tcpMode = 1;  // TCP被动模式
```

**SDP变化**:
```
平台INVITE:
a=setup:passive   ← 平台说"我监听,你连我"

设备200 OK:
a=setup:active    ← 设备说"好的,我主动连接你"
```

**流程**:
1. 平台在 `10.20.2.8:33624` 监听TCP连接 (ZLM开启TCP监听)
2. 设备主动连接到 `10.20.2.8:33624` ✅ (设备能访问平台公网IP)
3. TCP连接建立成功
4. 设备通过此TCP连接推送RTP流
5. ZLM接收到RTP流 ✅

### 方案2: 使用UDP模式 (最简单 ✅)

### 方案2: 使用UDP模式 (最简单 ✅)

**修改**: 不使用TCP,改用UDP

```php
// 在测试脚本中选择选项0
$tcpMode = 0;  // UDP模式
```

**SDP变化**:
```
平台INVITE:
m=video 33624 RTP/AVP 96  ← UDP传输
a=recvonly
(无 a=setup 字段,UDP不需要协商)

设备200 OK:
m=video 20002 RTP/AVP 96
a=sendonly
```

**流程**:
1. 平台在 `10.20.2.8:33624` 监听UDP
2. 设备直接向 `10.20.2.8:33624` 发送UDP包 ✅ (无需建立连接)
3. ZLM接收到RTP流 ✅

### 为什么不推荐tcp_mode=2?

**TCP主动模式的要求**:
```
平台: a=setup:active  → "我要连接你的IP和端口"
设备: a=setup:passive → "我在 10.204.58.47:20002 等你"
                        ↑
                      内网IP,平台无法访问!
```

**仅适合内网直连**:
- ✅ 平台和设备在同一局域网
- ✅ 设备有公网IP
- ❌ 设备在NAT后面 (你的情况)

## TCP模式对比表

| 模式 | tcp_mode | INVITE SDP | 设备响应 | 谁主动连接? | 适用场景 |
|------|----------|-----------|---------|------------|---------|
| **TCP被动** | 1 | `a=setup:passive` | `a=setup:active` | **设备→平台** | ✅ **NAT环境(推荐)** |
| **TCP主动** | 2 | `a=setup:active` | `a=setup:passive` | **平台→设备** | ❌ **仅内网直连** |
| **UDP** | 0 | 无setup字段 | 无setup字段 | 无需连接 | ✅ **所有场景** |

## 设备协商能力分析

根据你的抓包,设备表现:

```
收到: a=setup:active  (平台主动)
响应: a=setup:passive (设备被动) ✅ RFC 4145合规
```

**结论**:
- ✅ 设备支持TCP协商
- ✅ 设备能正确响应互补的setup模式
- ✅ 设备理论上也支持 active 模式(当平台是passive时)

**你的理解是对的!** 设备是可以协商的,测试脚本的三个选项都是有效的,只是受网络环境限制,当前只能用模式0或1。

## 修改位置

### 1. 检查调用SdpBuilder的代码

找到调用 `SdpBuilder::buildInviteSdp()` 的地方,修改 `tcp_mode` 参数:

```php
use Gb28181\GateWay\Message\SdpBuilder;

// ❌ 错误: tcp_mode=2 (主动模式)
$sdp = SdpBuilder::buildInviteSdp([
    'server_id' => $config['server_id'],
    'media_ip' => $config['zlm']['media_server_ip'],
    'media_port' => $zlmPort,
    'session_name' => 'Play',
    'mode' => 'recvonly',
    'ssrc' => $ssrc,
    'tcp_mode' => 2,  // ← 这里是问题!
]);

// ✅ 正确: tcp_mode=1 (被动模式) 
$sdp = SdpBuilder::buildInviteSdp([
    'server_id' => $config['server_id'],
    'media_ip' => $config['zlm']['media_server_ip'],
    'media_port' => $zlmPort,
    'session_name' => 'Play',
    'mode' => 'recvonly',
    'ssrc' => $ssrc,
    'tcp_mode' => 1,  // ← 改为1
]);
```

### 2. 或者改用UDP模式

```php
$sdp = SdpBuilder::buildInviteSdp([
    // ...
    'tcp_mode' => 0,  // ← UDP模式,最简单
]);
```

## ZLM配置检查

确保ZLM配置支持TCP被动模式:

```ini
[rtp_proxy]
port=10000
timeoutSec=15
port_range=30000-40000

# TCP被动模式需要ZLM支持TCP监听
```

## 测试验证

修改后重新测试:

1. **重启网关服务**
2. **发起实时视频请求**
3. **抓包查看INVITE**:
   - 应该看到 `a=setup:passive`
4. **查看ZLM日志**:
   - 应该看到 "TCP连接建立" 或 "收到RTP包"
5. **检查流状态**:
   - ZLM端口不再超时释放
   - 能够播放视频流

## 参考: AKStream的实现

AKStream使用 `IsPassive` 配置控制TCP模式:

```csharp
// SipServer.cs 第500-510行
if (pushMediaInfo.PushStreamSocketType == PushStreamSocketType.TCP)
{
    if (Common.SipServerConfig.IsPassive != null && 
        Common.SipServerConfig.IsPassive == false)
    {
        media.AddExtra("a=setup:active");  // 主动模式
    }
    else
    {
        media.AddExtra("a=setup:passive"); // 被动模式(默认)
    }
    media.Transport = "TCP/RTP/AVP";
    media.AddExtra("a=connection:new");
}
```

**默认使用被动模式**,这是正确的设计!
