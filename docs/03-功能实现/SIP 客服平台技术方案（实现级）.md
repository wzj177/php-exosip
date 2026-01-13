# SIP 客服平台技术方案（实现级）

> 适用场景：企业客服中心（呼入/呼出/外呼）、技能组排队、坐席软电话、IVR、自助语音、录音质检、转接/三方/咨询、与 CRM/工单系统联动。
>
> 本文偏工程落地：给出可选架构、信令/媒体处理方式、关键流程、选型建议、部署与运维要点。

---

## 1. 总体目标与边界

### 1.1 目标

- **可用性**：7x24，组件可水平扩展，支持故障切换。
- **容量**：支持并发坐席与并发通话（CPS、BHCA 可按业务估算）。
- **可观测性**：通话全链路追踪（SIP 信令、RTP 质量、录音、转接链路）。
- **业务能力**：排队/技能组、坐席状态、外显号、黑白名单、录音、质检、工单联动。
- **网络适配**：NAT/多出口/跨地域，WebRTC 前端或软电话客户端适配。

### 1.2 边界（避免不必要复杂度）

- **客服平台核心不等于流媒体平台**：客服语音/视频主要依赖 **RTP**；不强制需要 ZLMediaKit 这类媒体服务器。
- **若需要 WebRTC**：通常需要 WebRTC Gateway / SBC 或媒体中继（DTLS-SRTP ↔ SRTP/RTP）。
- **若需要录像/大规模转码/合成**：可单独引入媒体处理服务（例如录音转写、混音、质检），但不建议把它和 SIP 路由/排队强耦合。

---

## 2. 关键角色与组件（推荐分层）

### 2.1 组件拆分

1) **SIP 信令层（Routing / SBC / Registrar / Proxy）**
- 负责：注册、鉴权、路由、NAT 穿透策略、限流、防护。
- 可选实现：Kamailio / OpenSIPS / 自研轻量 SIP Router（或以 B2BUA 方案替代）。

2) **呼叫控制层（B2BUA / Call Control / PBX）**
- 负责：排队/技能组、坐席分配、保持/转接/三方、呼叫状态机、计费/话单。
- 可选实现：FreeSWITCH / Asterisk / 自研 B2BUA（需要严格状态机与兼容性测试）。

3) **媒体层（RTP/录音/中继/质量统计）**
- 负责：RTP 中继（必要时）、录音、DTMF、RTCP 统计、媒体质量（MOS、丢包、抖动）。
- 可选实现：RTPengine/RTProxy、FreeSWITCH 内建录音/混音、外部录音服务。

4) **坐席接入层（客户端）**
- 软电话：SIP UA（桌面/移动）
- 浏览器：WebRTC（需要 SIP ↔ WebRTC 适配）

5) **业务平台层（CRM/工单/质检/报表）**
- 负责：用户资料、工单、路由策略、质检规则、报表。
- 通常通过：REST/Webhook/WebSocket/消息队列与呼叫控制层对接。

### 2.2 两种主流架构路径

#### A. “SIP Proxy + PBX(B2BUA)” 的工业成熟方案（推荐）

- Kamailio/OpenSIPS：处理注册、鉴权、路由、防护、NAT、负载。
- FreeSWITCH/Asterisk：处理排队、转接、录音、IVR、会议。
- RTPengine：在需要 NAT 穿透、媒体锚定（media anchoring）时使用。

优点：成熟、稳定、生态完整；缺点：组件多、运维复杂度更高。

#### B. “单体 B2BUA + 必要的 SBC 能力” 的自研方案

- 一个核心 B2BUA 负责信令与业务状态机。
- 通过媒体中继或强制媒体锚定保证转接/录音。

优点：业务定制灵活、链路更直；缺点：协议兼容与稳定性成本高。

---

## 3. 信令与媒体：必须做对的点

### 3.1 SIP 只负责“建会话”，媒体靠 SDP/RTP

- SIP/SDP 决定媒体地址、编解码、DTMF、加密方式。
- RTP/RTCP 承载媒体与质量统计。

### 3.2 客服场景为何通常需要“媒体锚定（Media Anchoring）”

在 **转接/三方/录音/旁路监听/静音** 等场景中，如果媒体端到端直连，平台对媒体的控制能力会受限。

建议：
- **语音客服**：大多数生产系统选择媒体锚定在 PBX（FreeSWITCH/Asterisk）或 RTPengine。
- **录音**：在锚定点录音最稳定（双向混音/双声道）。

### 3.3 NAT 穿透

- 纯 SIP 软电话：主要靠 SBC/Proxy 的 rport/received、Contact 修正、对称 RTP、中继。
- WebRTC：必须考虑 ICE（STUN/TURN）与 DTLS-SRTP，常见做法是：
  - 浏览器 ↔ WebRTC Gateway（ICE/DTLS-SRTP）
  - Gateway ↔ SIP/PBX（SIP/SDP + SRTP/RTP）

---

## 4. 核心业务能力设计

### 4.1 坐席状态模型（建议最小集合）

- Offline（未登录）
- Idle（空闲可分配）
- Ringing（振铃中）
- Talking（通话中）
- AfterCallWork（话后）
- Busy（置忙/小休）

状态变化要可审计，便于排班、质检、报表。

### 4.2 排队与技能组

- 队列配置：
  - 技能组（按业务线/语言/地区）
  - 分配策略：round-robin / least-recently / longest-idle / skills match
  - 等待音乐/排队提示
  - 超时溢出（溢出到其他队列/IVR/语音信箱）
- 关键指标：ASA（平均接起时长）、Abandon Rate（放弃率）、Service Level。

### 4.3 外呼与外显号

- 外呼：坐席或自动外呼任务触发呼叫。
- 外显号：需通过运营商 SIP Trunk / 网关配置 Caller ID；注意合规与风控。

---

## 5. 转接（Transfer）技术方案（重点）

客服场景“转接”分三类：

### 5.1 盲转（Blind Transfer）

- 含义：A 直接把来电转给 B，不咨询。
- SIP 实现方式：
  - **REFER**：A 向平台或对端发送 REFER，要求对端去呼叫 B。
  - **B2BUA 内部转接**：平台保持 A 的对话并新建到 B 的对话，然后释放其中一端。

工程建议：
- 若坐席在浏览器/WebRTC 或存在 NAT，推荐 **B2BUA 内部转接**（控制更强、更兼容）。

### 5.2 咨询转接（Attended / Consult Transfer）

流程：
1) A 与客户通话中
2) A 发起对 B 的咨询呼叫（客户侧保持或听音乐）
3) A 与 B 沟通后，执行完成转接

实现建议：
- 平台必须维护两路通话状态机：
  - 客户 <-> 平台 <-> A
  - 平台 <-> B
- 完成转接时：
  - 让客户与 B 建立媒体（通常通过平台锚定）
  - A 退出（BYE）

### 5.3 三方通话（Conference / 3-way）

- 常用于：主管协助、升级处理。
- 建议通过 PBX 的会议功能实现（混音/录音更稳定）。

---

## 6. Web 前端/软电话对接方案

### 6.1 软电话（SIP UA）方案

- 坐席端：桌面软电话/移动端 SIP SDK。
- 平台侧：Proxy/B2BUA/PBX。
- 优点：协议直接、成熟；缺点：客户端适配与 NAT 仍需处理。

### 6.2 浏览器（WebRTC）方案

推荐链路：
- Browser (WebRTC) ↔ WebRTC Gateway/SBC ↔ SIP PBX

关键点：
- DTLS-SRTP 与 SRTP/RTP 互通
- ICE/TURN（企业内网/跨地域必备）
- 编解码：Opus 与 PCMU/PCMA 互转（或网关侧协商）

实现选型：
- 使用成熟网关（如基于 Janus / mediasoup / 自研网关）
- 或使用支持 WebRTC 的 PBX/SBC 组件（取决于技术栈与许可）。

---

## 7. 录音、质检与合规

### 7.1 录音模式

- 双声道录音：客户/坐席分轨，利于质检。
- 混音录音：简单但可用性强。

建议：录音触发与文件落盘由 PBX/媒体锚定点完成，业务平台只接收元数据与存储地址。

### 7.2 语音质检

- ASR 转写（实时/离线）
- 关键词/敏感词检测
- 情绪识别（可选）

数据链路建议：消息队列（Kafka/RabbitMQ）承载录音完成事件与质检任务。

---

## 8. 话单（CDR）与事件总线

### 8.1 事件定义

建议以“呼叫事件流”作为平台对外输出：
- call.created / call.ringing / call.answered / call.hangup
- agent.state.changed
- queue.enter / queue.leave / queue.timeout
- recording.ready

### 8.2 对接方式

- 同步 API：查询会话、坐席状态、发起外呼、强拆、监听。
- 异步事件：WebHook 或 MQ。
- 坐席控制：WebSocket（前端实时状态/弹屏）。

---

## 9. 高可用与扩展

### 9.1 HA 原则

- SIP Proxy：无状态或轻状态，支持水平扩展。
- B2BUA/PBX：保持会话状态，需要粘性/会话路由或集群方案。
- 录音/质检：异步化，避免影响通话主链路。

### 9.2 关键容量指标

- CPS（Calls Per Second）
- BHCA（Busy Hour Call Attempts）
- 并发通话数
- 坐席并发数
- RTP 带宽（单通话 64kbps~100kbps 级别，视编码与封装）

---

## 10. 安全与风控

- SIP 账户强密码/鉴权、IP 白名单、注册频率限制。
- SBC 防护：SIP Flood、扫描、重放。
- SRTP（如有合规/公网）
- 通话录音权限与审计。

---

## 11. 运维与可观测性

### 11.1 必备观测

- SIP：注册成功率、4xx/5xx/6xx、重传、事务超时。
- RTP：丢包、抖动、RTT、MOS（估算）。
- 业务：排队时长、接通率、放弃率、坐席利用率。

### 11.2 抓包与排障

- 信令：SIP 抓包（tcpdump + Wireshark）
- 媒体：RTP/RTCP 统计、录音对齐
- NAT：检查 Via received/rport、Contact 修正、对称 RTP

---

## 12. 基于 php-exosip 扩展的客服平台实现路径（重点）

### 12.1 php-exosip 扩展能做什么

你的 **php-exosip 扩展** 已经实现了 **SIP 信令层的核心能力**，具体包括：

#### ✅ 已实现（信令层完整能力）

| 功能模块 | 具体能力 | 客服场景应用 |
|---------|---------|------------|
| **SIP 协议栈** | eXosip2 封装，UDP/TCP/TLS | 坐席注册、呼叫建立 |
| **注册管理** | REGISTER 处理、401 认证、Expires | 坐席账户登录/保活 |
| **呼叫控制** | INVITE/ACK/BYE/CANCEL | 呼入/呼出/挂断 |
| **消息传递** | MESSAGE 收发 | 坐席状态同步、指令下发 |
| **SDP 解析** | 原生 osip2 SDP 解析器 | 媒体协商、编解码识别、IP/Port 提取 |
| **响应处理** | 1xx-6xx 响应、自动 ACK | 呼叫状态跟踪 |
| **事件驱动** | 非阻塞事件循环、回调 | 高并发处理 |
| **进程模型** | Master-Worker-Task | 支持排队/外呼异步任务 |
| **状态管理** | Connection/Session 管理 | 坐席连接、通话会话跟踪 |

#### ✅ 已具备但需扩展的能力

| 功能 | 当前状态 | 客服场景需要做的 |
|------|---------|-----------------|
| **转接（REFER）** | 可发送/接收 REFER | 需要实现 B2BUA 状态机（维护两路通话） |
| **re-INVITE** | 支持发送/响应 | 用于保持/恢复、媒体重协商 |
| **多通话并发** | 支持 | 需要会话关联（坐席 ID ↔ Call ID） |

### 12.2 缺少的部分（需要补充的组件）

你的扩展**只负责 SIP 信令**，以下能力需要其他组件：

| 缺失能力 | 推荐方案 | 是否必须 |
|---------|---------|---------|
| **RTP 媒体传输** | RTPengine / FreeSWITCH / 自研 RTP Proxy | ✅ 必须（语音通话核心） |
| **录音** | 在媒体锚定点录音（RTPengine/FreeSWITCH） | ✅ 必须（合规/质检） |
| **排队与技能组** | PHP 层业务逻辑 + Redis | ✅ 必须 |
| **IVR 播报** | 媒体服务器播放预录音频 | ⚠️ 常用 |
| **DTMF 识别** | RTP 层识别（RFC 2833 / SIP INFO） | ⚠️ 常用（按键导航） |
| **WebRTC 接入** | WebRTC Gateway（Janus/mediasoup） | ⚠️ 可选（浏览器坐席） |
| **混音（三方通话）** | 媒体服务器混音 | ⚠️ 可选 |
| **质检/转写** | ASR 服务（阿里云/讯飞） | ⚠️ 可选 |

### 12.3 推荐架构：php-exosip 作为"SIP B2BUA 层"

```
                    ┌─────────────────────────────────────────┐
                    │         客服业务平台（PHP/Laravel）        │
                    │    排队、技能组、工单、报表、质检          │
                    └──────────────┬──────────────────────────┘
                                   │ REST/WebSocket/Redis
                    ┌──────────────▼──────────────────────────┐
                    │    php-exosip (SIP B2BUA + 状态机)      │
                    │  - 坐席注册/鉴权                         │
                    │  - 呼叫路由/转接                         │
                    │  - SDP 协商/转发                         │
                    │  - 会话状态管理                          │
                    └──────┬────────────────┬─────────────────┘
                           │                │
          ┌────────────────▼──┐         ┌──▼─────────────────┐
          │  RTPengine/       │         │  SIP Trunk         │
          │  FreeSWITCH       │         │  (运营商/网关)      │
          │  - RTP 中继       │         │  - 外线呼入/外呼    │
          │  - 录音           │         └────────────────────┘
          │  - DTMF           │
          │  - 混音(可选)      │
          └───────┬───────────┘
                  │
     ┌────────────▼────────────┐
     │  坐席客户端              │
     │  - 软电话 (SIP UA)      │
     │  - WebRTC (需网关)      │
     └─────────────────────────┘
```

### 12.4 完整实现方案（分阶段）

#### 阶段 1：最小可用版本（MVP）- 软电话坐席

**你需要实现的（基于 php-exosip）：**
```php
// SIP B2BUA 核心逻辑（在 Worker 进程）
$sipServer = new ExoSip([
    'ip' => '0.0.0.0',
    'port' => 5060,
    'mode' => 'udp',
    'task_worker_num' => 4
]);

// 1. 坐席注册
$sipServer->onRegister = function($event) use ($agentManager) {
    $agentId = $event->getFromUri();
    $agentManager->onlineAgent($agentId);
    // 响应 200 OK
    $this->sendResponse($event->getTid(), 200);
};

// 2. 呼入路由（SIP Trunk → 排队 → 坐席）
$sipServer->onInvite = function($event) use ($queueManager) {
    $callerId = extractCallerId($event->getFromUri());
    $sdp = $event->getBody();
    
    // Task: 排队分配坐席（可能阻塞）
    $taskId = $this->addTask([
        'action' => 'queue_call',
        'caller_id' => $callerId,
        'sdp' => $sdp
    ]);
};

// 3. Task 完成后：向坐席发起呼叫
$sipServer->onTaskFinish = function($server, $taskId, $result) {
    $agentUri = $result['agent_uri'];
    $sdp = $result['sdp'];
    
    // 向坐席发 INVITE（SDP = 客户的媒体信息）
    $callId = $server->sendInvite($agentUri, $sdp);
};

// 4. 坐席接听后，桥接媒体（通过 re-INVITE 或 RTPengine）
$sipServer->onResponse = function($event) use ($mediaController) {
    if ($event->getCode() == 200 && isAgentAnswer($event)) {
        $agentSdp = $event->getBody();
        
        // 告诉 RTPengine 桥接
        $mediaController->bridge($callerId, $agentId, $agentSdp);
        
        // 向客户发 200 OK + 修改后的 SDP
        $this->sendResponse($originalTid, 200, null, [
            'Content-Type' => 'application/sdp',
            'Body' => $modifiedSdp
        ]);
    }
};
```

**你需要额外部署：**
1. **RTPengine**（媒体中继 + 录音）
   ```bash
   # CentOS/Ubuntu 安装
   yum install rtpengine
   # 配置: 监听端口、录音路径
   ```

2. **Redis**（排队状态 + 坐席状态）
   ```php
   // 排队逻辑（在 Task 进程）
   $redis->zadd('queue:sales', time(), $callerId);
   $redis->set("agent:{$agentId}:state", 'idle');
   ```

3. **MySQL**（话单/工单/配置）

#### 阶段 2：高级功能

**转接实现（B2BUA 模式）：**
```php
// 坐席 A 请求转接到坐席 B
$sipServer->onMessage = function($event) use ($transferManager) {
    $xml = parseXml($event->getBody());
    
    if ($xml['action'] == 'transfer') {
        $targetAgent = $xml['target'];
        
        // 1. 保持客户（发 re-INVITE with sendonly）
        $this->sendInvite($customerId, generateHoldSdp());
        
        // 2. 呼叫坐席 B
        $this->addTask([
            'action' => 'call_agent',
            'agent' => $targetAgent,
            'caller_display' => $customerId  // 显示客户号码
        ]);
        
        // 3. B 接听后，桥接客户与 B，挂断 A
        // （状态机复杂，需要维护 call_id 映射）
    }
};
```

**WebRTC 坐席（需要额外组件）：**
- 部署 **Janus Gateway** 或 **mediasoup**
- 浏览器 ↔ WebRTC Gateway ↔ php-exosip ↔ RTPengine

#### 阶段 3：企业级增强

- **多机房/负载均衡**：Kamailio Dispatcher + 多个 php-exosip 实例
- **呼叫录音归档**：录音文件 → OSS → 质检平台
- **实时监控**：Prometheus + Grafana（SIP 指标、排队指标、录音率）

### 12.5 关键代码示例：SDP 与 RTPengine 对接

```php
// php-exosip 提取 SDP 信息
$sdp = $event->getSdp();  // 使用原生 parseSdp() 方法

// 获取客户端的媒体 IP/Port
$clientIp = $sdp['connection']['addr'];
$clientPort = $sdp['medias'][0]['port'];

// 调用 RTPengine offer（分配中继端口）
$rtpResponse = $rtpengine->offer([
    'call-id' => $callId,
    'from-tag' => $fromTag,
    'sdp' => $originalSdp,
    'replace' => ['origin', 'session-connection']
]);

$modifiedSdp = $rtpResponse['sdp'];  // RTPengine 返回的 SDP（媒体指向中继）

// 转发给坐席
$this->sendInvite($agentUri, $modifiedSdp);
```

### 12.6 总结：php-exosip 在客服平台中的定位

| 组件 | 你的扩展能做 | 需要外部组件 |
|------|------------|------------|
| **SIP 信令** | ✅ 完全胜任（注册/呼叫/转接/状态） | 无 |
| **媒体传输** | ❌ 不涉及 | RTPengine/FreeSWITCH |
| **录音** | ❌ 不涉及 | RTPengine/FreeSWITCH |
| **排队逻辑** | ✅ PHP 层实现（Task 进程） | Redis（状态共享） |
| **IVR** | ❌ 不涉及 | 媒体服务器 |
| **WebRTC** | ❌ 不直接支持 | WebRTC Gateway |
| **话单/报表** | ✅ PHP 层实现 | MySQL/ClickHouse |

**结论**：你的扩展 = **SIP B2BUA 层**，配合 RTPengine（媒体）+ Redis（状态）+ MySQL（数据），可以实现完整的企业客服平台。

### 12.7 关键架构问题澄清 ⭐️

#### Q1: ZLM (ZLMediaKit) 或 SRS 能否用于客服平台？

**简短答案**: ❌ **不推荐作为主媒体服务器**，但可以作为录音存储后端。

**详细对比**:

| 功能需求              | ZLM/SRS                     | RTPengine           | FreeSWITCH          |
|----------------------|----------------------------|---------------------|---------------------|
| **核心定位**          | 流媒体服务器（推拉流）        | 纯 RTP 代理          | 完整 SIP B2BUA + PBX |
| SIP 信令栈           | 仅 GB28181 基础（INVITE/BYE）| ❌ 无（需外部 Proxy）| ✅ 完整 RFC 3261    |
| SIP REFER（转接）    | ❌ 不支持                   | ❌ 不支持           | ✅ 原生支持         |
| RTP 媒体中继         | ✅ 支持                     | ✅ 高性能           | ✅ 支持             |
| 录音功能             | ✅ 推流录制（RTMP/HLS/FLV） | ❌ 需自行开发       | ✅ 原生录音（WAV）  |
| IVR 放音             | ❌ 不支持                   | ❌ 不支持           | ✅ 完整 IVR 引擎    |
| DTMF 识别            | ❌ 不支持                   | ❌ 不支持           | ✅ RFC 2833/SIP INFO|
| WebRTC 支持          | ✅ WebRTC 推拉流（无 SIP）  | ✅ ICE/DTLS         | ✅ Verto/mod_rtc    |
| **适用场景**          | 视频监控、直播推流           | SIP 代理 + 媒体中继 | 客服/呼叫中心/PBX   |

**ZLM/SRS 的正确用法**（在客服场景）:
```
[客服平台架构] 推荐方案

客户 ←─ SIP ─→ php-exosip (B2BUA) ←─ SIP ─→ 坐席软电话
                    ↓ RTP (录音)
                FreeSWITCH/RTPengine ──推流──→ ZLM/SRS (录音存储 + HLS 回放)
                                                  ↓
                                            Web 前端播放录音
```

**结论**: 
- ✅ **推荐**: php-exosip (SIP) + RTPengine/FreeSWITCH (媒体) + ZLM (录音存储)
- ❌ **不推荐**: ZLM/SRS 替代 RTPengine（缺少客服必需的 SIP REFER/NOTIFY 等信令）

---

#### Q2: 坐席软电话的作用和对接层级

**坐席软电话（Linphone/MicroSIP）的作用**:
- **定位**: 坐席终端的 **SIP User Agent (UA)**
- **功能**: REGISTER 到 php-exosip、接听/拨打电话、保持/转接
- **协议**: 标准 SIP/RTP（非 WebRTC）

**完整对接架构（4 层）**:

```
┌─────────────────────────────────────────────────────────────────┐
│ Layer 1: 客户侧                                                  │
│  PSTN 网关 / SIP Trunk / 移动运营商 SIP                          │
└────────────────────────────┬────────────────────────────────────┘
                             │ SIP (INVITE/BYE/ACK)
                             ↓
┌─────────────────────────────────────────────────────────────────┐
│ Layer 2: php-exosip (SIP B2BUA 层) ⭐️ 你的扩展                   │
│  - 路由逻辑（IVR、队列、坐席分配）                                │
│  - SIP 信令终结和转发                                             │
│  - SDP 媒体协商（提取 IP/Port/Codec）                            │
└────────────────────────────┬────────────────────────────────────┘
                             │ SIP (INVITE 到坐席)
                             ↓
┌─────────────────────────────────────────────────────────────────┐
│ Layer 3: 坐席软电话 (Linphone/MicroSIP) ⭐️ 坐席终端              │
│  - REGISTER sip:agent001@php-exosip-server:5060                  │
│  - 接听来电（INVITE）                                             │
│  - 发起外呼                                                       │
│  - 执行转接（REFER）                                              │
└────────────────────────────┬────────────────────────────────────┘
                             │ RTP 媒体流（语音）
                             ↓
┌─────────────────────────────────────────────────────────────────┐
│ Layer 4: 媒体服务器 (RTPengine/FreeSWITCH) ⭐️ 媒体锚定          │
│  - RTP 中继（客户 ⇄ 坐席）                                        │
│  - 录音（存储到本地或推流到 ZLM）                                 │
│  - 混音（三方通话）                                               │
└─────────────────────────────────────────────────────────────────┘
```

**关键点**:
1. **坐席软电话 = Layer 3（终端层）**，通过标准 SIP 协议 REGISTER 到 php-exosip
2. **php-exosip 不直接处理 RTP**，通过 SDP 指向 RTPengine 的 IP/Port
3. **媒体锚定**: 所有 RTP 流必须经过 RTPengine（保证录音和转接不断流）

---

#### Q3: 前端是否用 WebRTC + SIP 发起和接听？

**答案**: ✅ **可以**，但需要 WebRTC Gateway 或 FreeSWITCH。

**两种前端坐席方案对比**:

##### 方案 A: 传统软电话（推荐用于快速验证）

```
浏览器（管理后台）
    ↓ HTTP/WebSocket (状态查询、工单系统)
php-exosip (SIP B2BUA)
    ↓ SIP/RTP
坐席软电话 (Linphone/MicroSIP)  ← 坐席电脑本地安装
```

**优点**: 
- ✅ 实现简单，无需 WebRTC Gateway
- ✅ 音频质量稳定（直连 RTP）
- ✅ 延迟低

**缺点**:
- ❌ 需要坐席安装软件
- ❌ 防火墙/NAT 配置复杂

##### 方案 B: WebRTC 浏览器坐席（企业级方案）

```
浏览器（Vue/React 前端）
    ↓ WebRTC (DTLS-SRTP + ICE)
WebRTC Gateway (Janus/FreeSWITCH Verto)
    ↓ SIP/RTP (转换为标准 SIP)
php-exosip (SIP B2BUA)
    ↓ SIP/RTP
客户侧 (PSTN/SIP Trunk)
```

**具体技术栈**:

| 组件               | 技术选型                           | 作用                          |
|-------------------|-----------------------------------|------------------------------|
| **前端**           | Vue/React + JsSIP/SIP.js          | WebRTC 信令（SIP over WebSocket）|
| **信令转换**       | Janus/FreeSWITCH/Asterisk         | WebSocket ⇄ UDP SIP 转换      |
| **媒体转换**       | 同上（DTLS-SRTP ⇄ RTP）           | 加密 WebRTC ⇄ 明文 RTP       |
| **SIP B2BUA**     | php-exosip                        | 路由、队列、转接逻辑          |
| **媒体锚定**       | RTPengine/FreeSWITCH              | RTP 中继 + 录音               |

**完整信令流程**（WebRTC 坐席接听来电）:

```
1. 客户拨打 → php-exosip 收到 INVITE
2. php-exosip 查询坐席状态 → 分配给坐席 agent001
3. php-exosip 发送 INVITE sip:agent001@webrtc-gateway
4. WebRTC Gateway 推送 WebSocket 通知到浏览器
5. 浏览器弹出来电提示，坐席点击「接听」
6. 浏览器通过 JsSIP 发送 SIP 200 OK (WebSocket)
7. WebRTC Gateway 转换为 UDP SIP 200 OK → php-exosip
8. php-exosip 转发 200 OK → 客户侧
9. 媒体协商: WebRTC (DTLS-SRTP) ⇄ Gateway ⇄ RTPengine ⇄ 客户 (RTP)
```

**前端代码示例**（JsSIP）:

```javascript
// 浏览器坐席 WebRTC 接入
import JsSIP from 'jssip';

const socket = new JsSIP.WebSocketInterface('wss://webrtc-gateway.example.com:8089/ws');
const ua = new JsSIP.UA({
  sockets: [socket],
  uri: 'sip:agent001@example.com',
  password: 'agent_password'
});

ua.start();

// 监听来电
ua.on('newRTCSession', (e) => {
  const session = e.session;
  
  if (session.direction === 'incoming') {
    // 弹出接听 UI
    showIncomingCallDialog({
      caller: session.remote_identity.uri.user,
      onAccept: () => {
        session.answer({
          mediaConstraints: { audio: true, video: false },
          pcConfig: { iceServers: [{ urls: 'stun:stun.l.google.com:19302' }] }
        });
      },
      onReject: () => session.terminate()
    });
  }
});
```

**推荐 WebRTC Gateway 选择**:

| 方案                  | 优点                           | 缺点                    | 适用场景          |
|----------------------|-------------------------------|------------------------|------------------|
| **FreeSWITCH Verto** | 原生 SIP ⇄ WebRTC 转换         | 配置复杂               | 企业级客服平台    |
| **Janus Gateway**    | 高性能、插件化                 | 需要自行开发 SIP 插件   | 定制化需求       |
| **Asterisk**         | 成熟稳定，完整 PBX 功能        | 较重，性能一般         | 中小型呼叫中心    |
| **Kamailio + RTPengine** | 超高性能，专业 SIP Proxy   | 配置极复杂             | 运营商级别       |

**MVP 推荐方案**:
- **阶段 1（验证）**: 坐席软电话（Linphone）+ php-exosip + RTPengine
- **阶段 2（生产）**: 浏览器 WebRTC（JsSIP）+ FreeSWITCH Verto + php-exosip + RTPengine

---

## 13. 与现有系统（含本仓库）对齐的建议

- 若你当前的目标是“GB28181 信令网关 + 客服语音平台”：建议将两者拆成两个域：
  - GB28181：以设备信令与视频为主（SIP+SDP+RTP 到媒体服务器）。
  - 客服平台：以呼叫控制与坐席为主（SIP+RTP），通常不复用同一套媒体服务器。

- 本仓库提供的 Master-Worker-Task 模型可借鉴到客服平台的工程组织：
  - Worker：SIP 事件循环（不阻塞）
  - Task：CRM/工单/质检/外呼任务等阻塞操作
  - Pipe：Task → Worker 下发呼叫控制指令（如外呼、转接、强拆）

---

## 14. 最小可落地版本（MVP）建议

基于 **php-exosip + RTPengine + Redis + MySQL** 的最简实现：

### 技术栈

| 组件           | 技术选型                     | 作用                     |
|---------------|-----------------------------|-----------------------|
| SIP 信令      | php-exosip 扩展              | 注册、路由、转接      |
| 媒体中继      | RTPengine 或 FreeSWITCH      | RTP 代理 + 录音          |
| 坐席终端      | Linphone/MicroSIP（阶段1）<br>浏览器 WebRTC（阶段2） | 坐席话机/接听拨打 |
| 队列状态      | Redis                       | 坐席状态、排队队列        |
| 持久化        | MySQL                       | CDR、配置、工单          |
| 录音存储（可选）| ZLM/SRS                    | HLS 回放、录音归档       |

### 功能范围
- ✅ 呼入：运营商 SIP Trunk → 排队 → 坐席振铃 → 接听
- ✅ 坐席：注册/登录、空闲/忙碌状态、手动置忙
- ✅ 录音：双声道录音（客户/坐席分轨）
- ✅ 盲转：A 直接转给 B（B2BUA 内部转接）
- ✅ 话单：呼叫开始/结束时间、时长、录音路径
- ✅ 对接：REST API（发起外呼、查询坐席状态）+ Webhook（话单推送）

### 部署架构
```
Inte6. 推荐下一步（如果你要继续推进）

### 16.1 基于 php-exosip 的推荐路线

**优先级 P0（核心能力）**
1. 部署 RTPengine（媒体中继 + 录音）
2. 实现 B2BUA 状态机（维护客户/坐席两路通话）
3. 实现排队与坐席分配（PHP + Redis）
4. 测试 SIP 软电话对接（Linphone/MicroSIP）

**优先级 P1（生产必备）**
5. 话单/CDR（MySQL）
6. 盲转功能（B2BUA 内部转接）
7. 监控/告警（SIP 成功率、排队时长）
8. 录音归档（OSS/NAS）

**优先级 P2（增强功能）**
9. 咨询转接（保持 + 三方协商）
10. WebRTC 坐席（部署 Janus Gateway）
11. IVR 导航（集成 FreeSWITCH）
12. ASR 质检（录音 → 转写 → 关键词）

### 16.2 技术选型建议

| 场景 | 推荐方案 | 原因 |
|------|---------|------|
| **快速验证** | php-exosip + RTPengine + 软电话 | 轻量、可控 |
| **企业生产** | php-exosip + FreeSWITCH + Redis + MySQL | FreeSWITCH 录音/IVR 更成熟 |
| **超大规模** | Kamailio + FreeSWITCH + 自研排队 | 需要专业团队 |

### 16.3 避坑指南

- ❌ **不要**：用 php-exosip 直接处理 RTP（扩展不涉及媒体层）
- ❌ **不要**：在 Worker 进程阻塞 I/O（排队/查库放 Task 进程）
- ❌ **不要**：忽略 NAT 穿透（RTPengine 可解决 90% 问题）
- ✅ **务必**：录音走媒体锚定点（不要依赖客户端/坐席自己录）
- ✅ **务必**：话单实时入库（不要等通话结束才写，防丢失）
- ✅ **务必**：压测验证（模拟 100 并发通话，检查 SIP/RTP 稳定性）
```

### 开发周期估算（单人）
- SIP B2BUA 核心：2-3 周
- 排队/技能组：1 周
- 录音/话单：1 周
- 转接功能：1-2 周
- 测试/调优：1-2 周
- **总计**：6-9 周

---

## 14. 术语速查

- **Proxy/Registrar**：SIP 注册与路由
- **B2BUA**：双向用户代理（平台完全控制呼叫）
- **SBC**：会话边界控制（安全、NAT、媒体锚定）
- **RTP/RTCP**：媒体与质量统计
- **REFER**：转接相关 SIP 方法
- **re-INVITE**：更新会话 SDP（媒体重协商）
- **ICE/STUN/TURN**：WebRTC 穿透与中继

---

## 15. 推荐下一步（如果你要继续推进）

- 明确坐席形态：SIP 软电话 vs WebRTC。
- 明确是否必须媒体锚定：录音/质检/旁路监听强烈建议锚定。
- 选型落地：
  - 成熟路线：Kamailio/OpenSIPS + FreeSWITCH/Asterisk + RTPengine
  - 自研路线：B2BUA 状态机 + RTP 中继/录音 + 完整兼容性测试矩阵

