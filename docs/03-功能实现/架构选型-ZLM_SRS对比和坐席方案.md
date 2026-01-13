# 客服平台架构选型：ZLM/SRS vs RTPengine、坐席侧方案对比

> 本文回答三个关键问题：
> 1. ZLM/SRS 能否用于客服平台？
> 2. 坐席软电话的作用和对接层级
> 3. 前端 WebRTC 方案如何实现？

---

## 1. ZLM (ZLMediaKit) / SRS 能否对接？⭐️

### 1.1 简短答案

- ❌ **不推荐作为主媒体服务器**（缺少客服必需的 SIP 信令能力）
- ✅ **可以作为录音存储后端**（RTPengine 录音 → 推流到 ZLM → HLS 回放）

### 1.2 详细对比

| 功能需求              | ZLM/SRS                     | RTPengine           | FreeSWITCH          |
|----------------------|----------------------------|---------------------|---------------------|
| **核心定位**          | 流媒体服务器（推拉流）        | 纯 RTP 代理          | 完整 SIP B2BUA + PBX |
| **SIP 信令栈**        | 仅 GB28181 基础（INVITE/BYE）| ❌ 无（需外部 Proxy）| ✅ 完整 RFC 3261    |
| **SIP REFER（转接）** | ❌ 不支持                   | ❌ 不支持           | ✅ 原生支持         |
| **SIP NOTIFY/SUBSCRIBE** | ❌ 不支持                | ❌ 不支持           | ✅ 支持             |
| **RTP 媒体中继**      | ✅ 支持                     | ✅ 高性能           | ✅ 支持             |
| **录音功能**          | ✅ 推流录制（RTMP/HLS/FLV） | ❌ 需自行开发       | ✅ 原生录音（WAV）  |
| **IVR 放音**          | ❌ 不支持                   | ❌ 不支持           | ✅ 完整 IVR 引擎    |
| **DTMF 识别**         | ❌ 不支持                   | ❌ 不支持           | ✅ RFC 2833/SIP INFO|
| **WebRTC 支持**       | ✅ WebRTC 推拉流（无 SIP）  | ✅ ICE/DTLS         | ✅ Verto/mod_rtc    |
| **混音（三方）**       | ❌ 不支持                   | ❌ 不支持           | ✅ 会议桥           |
| **适用场景**          | 视频监控、直播推流           | SIP 代理 + 媒体中继 | 客服/呼叫中心/PBX   |

### 1.3 ZLM/SRS 的正确用法（在客服场景）

```
[推荐架构] php-exosip + RTPengine + ZLM

客户 ←─ SIP ─→ php-exosip (B2BUA) ←─ SIP ─→ 坐席
                    ↓ RTP (实时媒体)
                RTPengine (录音 + 媒体锚定)
                    ↓ RTMP/HLS 推流
                ZLM/SRS (录音存储 + Web 回放)
                    ↓
              Web 前端播放录音（质检）
```

**ZLM/SRS 的角色**:
- ✅ 作为**录音归档服务器**（接收 RTPengine 的 RTMP 推流）
- ✅ 提供 **HLS/HTTP-FLV 回放**（质检人员在 Web 界面播放录音）
- ✅ **录音转码**（WAV → MP3/M4A）

**不适合做什么**:
- ❌ 替代 RTPengine 做 RTP 中继（ZLM 不处理 SIP REFER 转接信令）
- ❌ 作为主 SIP 服务器（ZLM 的 SIP 栈仅支持 GB28181 基础信令）

### 1.4 结论

✅ **推荐方案**: 
```
php-exosip (SIP 信令) 
  + RTPengine (RTP 中继 + 录音) 
  + ZLM/SRS (录音存储 + 回放)
```

❌ **不推荐**: 
```
ZLM/SRS 单独作为客服媒体服务器
  （缺少 SIP REFER/NOTIFY/INFO 等客服核心信令）
```

---

## 2. 坐席软电话的作用和对接层级 ⭐️

### 2.1 什么是坐席软电话？

**坐席软电话**（Linphone/MicroSIP/Zoiper）:
- **定位**: 坐席终端的 **SIP User Agent (UA)**
- **安装位置**: 坐席电脑桌面（Windows/macOS/Linux）
- **协议**: 标准 SIP/RTP（RFC 3261，非 WebRTC）
- **功能**: 
  - REGISTER 到 SIP 服务器（认证登录）
  - 接听来电（INVITE）
  - 发起外呼
  - 执行转接（发送 REFER）
  - 保持/恢复（发送 re-INVITE）

### 2.2 完整对接架构（4 层）

```
┌─────────────────────────────────────────────────────────────────┐
│ Layer 1: 客户侧 (外部来电)                                        │
│  PSTN 网关 / SIP Trunk / 移动运营商 SIP                          │
└────────────────────────────┬────────────────────────────────────┘
                             │ SIP (INVITE/BYE/ACK)
                             ↓
┌─────────────────────────────────────────────────────────────────┐
│ Layer 2: php-exosip (SIP B2BUA 层) ⭐️ 你的扩展                   │
│  角色：SIP 信令路由中心                                            │
│  - 接收外部 INVITE → 查询队列 → 分配坐席                          │
│  - 向坐席发起 INVITE（振铃）                                       │
│  - 处理转接（REFER）、保持（re-INVITE）                            │
│  - SDP 媒体协商（修改 IP/Port 指向 RTPengine）                    │
└────────────────────────────┬────────────────────────────────────┘
                             │ SIP (INVITE 到坐席)
                             │ To: sip:agent001@php-exosip-server
                             ↓
┌─────────────────────────────────────────────────────────────────┐
│ Layer 3: 坐席软电话 (Linphone/MicroSIP) ⭐️ 坐席终端              │
│  角色：坐席的话机（类似实体电话）                                   │
│  - 启动后 REGISTER sip:agent001@192.168.1.100:5060               │
│  - 收到 INVITE 后弹出来电提示                                     │
│  - 坐席点击「接听」→ 发送 200 OK                                  │
│  - 坐席点击「转接」→ 发送 REFER 请求                              │
└────────────────────────────┬────────────────────────────────────┘
                             │ RTP 媒体流（语音 G.711/G.729）
                             │ (SDP 中的 IP:Port 由 php-exosip 修改)
                             ↓
┌─────────────────────────────────────────────────────────────────┐
│ Layer 4: RTPengine/FreeSWITCH ⭐️ 媒体锚定层                      │
│  角色：RTP 中继 + 录音 + NAT 穿透                                  │
│  - 客户 RTP ⇄ RTPengine ⇄ 坐席 RTP                               │
│  - 双声道录音（客户/坐席分轨）                                     │
│  - 混音（三方通话时）                                              │
│  - 推流到 ZLM（可选）                                              │
└─────────────────────────────────────────────────────────────────┘
```

### 2.3 坐席软电话的配置示例

**Linphone 配置**（坐席侧）:
```
SIP 账号设置：
  - 账号ID: agent001
  - 密码: agent_password
  - 域: 192.168.1.100 (php-exosip 服务器 IP)
  - 端口: 5060
  - 传输: UDP
  - Expires: 3600

音频编解码：
  - 首选: PCMU (G.711u)
  - 备选: PCMA (G.711a)
  - 禁用: Opus/Speex（运营商通常不支持）

NAT 设置：
  - STUN: 可选（内网不需要）
  - 对称 RTP: 启用
```

**坐席登录流程（SIP 信令）**:
```
1. 坐席打开 Linphone → 输入账号/密码
2. Linphone 发送 REGISTER sip:agent001@192.168.1.100:5060
3. php-exosip 收到注册请求 → 验证密码 → 200 OK
4. Linphone 显示「已注册」
5. 坐席状态变为「空闲可接听」
```

### 2.4 关键点总结

1. **坐席软电话 = 终端层（Layer 3）**，不涉及业务逻辑
2. **php-exosip = 信令控制层（Layer 2）**，负责路由和队列
3. **坐席通过标准 SIP 协议对接**，无需额外开发（开箱即用）
4. **媒体不直连**，必须经过 RTPengine（保证录音和转接稳定）

---

## 3. 前端 WebRTC + SIP 方案 ⭐️

### 3.1 两种坐席接入方案对比

| 维度          | 方案 A: 软电话           | 方案 B: WebRTC 浏览器      |
|--------------|-------------------------|--------------------------|
| **安装要求**  | 需要安装客户端软件       | 无需安装（打开浏览器即用）  |
| **技术栈**    | SIP UA (标准协议)        | WebRTC + JsSIP/SIP.js    |
| **对接难度**  | ⭐️ 简单（开箱即用）      | ⭐️⭐️⭐️ 复杂（需要 Gateway）|
| **音频质量**  | ✅ 稳定（G.711）         | ⚠️ 依赖网络（Opus 自适应） |
| **NAT 穿透**  | ⚠️ 需要配置             | ✅ 内置 ICE/STUN/TURN     |
| **适用场景**  | 传统客服中心、固定坐席   | 远程坐席、移动办公         |
| **成本**      | 低（无额外服务器）       | 高（需 WebRTC Gateway）    |

### 3.2 方案 A: 传统软电话（推荐 MVP）

**架构**:
```
坐席管理后台（Web）
    ↓ HTTP/WebSocket (状态查询、工单系统)
php-exosip (SIP B2BUA)
    ↓ SIP/RTP
坐席软电话 (Linphone/MicroSIP)  ← 坐席电脑本地安装
```

**优点**: 
- ✅ 实现简单，无需 WebRTC Gateway
- ✅ 音频质量稳定（G.711 固定码率）
- ✅ 延迟低（端到端 SIP/RTP）
- ✅ 成本低（无额外服务器）

**缺点**:
- ❌ 坐席需要安装软件
- ❌ 防火墙/NAT 配置复杂
- ❌ 不支持浏览器直接使用

**推荐用于**: 
- 快速验证 MVP
- 传统客服中心（固定坐席）
- 对音频质量要求高的场景

---

### 3.3 方案 B: WebRTC 浏览器坐席（企业级）

**完整架构**:

```
┌─────────────────────────────────────────────────────────────────┐
│  坐席前端 (Vue/React)                                             │
│  - JsSIP/SIP.js (SIP over WebSocket)                            │
│  - WebRTC API (getUserMedia, RTCPeerConnection)                 │
└────────────────────────────┬────────────────────────────────────┘
                             │ WSS (SIP Signaling)
                             │ + DTLS-SRTP (Media)
                             ↓
┌─────────────────────────────────────────────────────────────────┐
│  WebRTC Gateway (FreeSWITCH Verto / Janus / Asterisk)           │
│  - WebSocket ⇄ UDP SIP 转换                                      │
│  - DTLS-SRTP ⇄ SRTP/RTP 转换                                    │
│  - ICE Candidate 处理                                            │
└────────────────────────────┬────────────────────────────────────┘
                             │ SIP/RTP (标准协议)
                             ↓
┌─────────────────────────────────────────────────────────────────┐
│  php-exosip (SIP B2BUA)                                          │
│  - 接收来自 Gateway 的 SIP INVITE                                 │
│  - 路由到队列/坐席分配                                             │
│  - SDP 媒体协商                                                   │
└────────────────────────────┬────────────────────────────────────┘
                             │ SIP/RTP
                             ↓
┌─────────────────────────────────────────────────────────────────┐
│  RTPengine / FreeSWITCH                                          │
│  - RTP 中继（WebRTC Gateway ⇄ 客户侧）                           │
│  - 录音                                                           │
└─────────────────────────────────────────────────────────────────┘
```

### 3.4 前端 WebRTC 代码示例（JsSIP）

**坐席登录 + 接听来电**:

```javascript
// 1. 初始化 JsSIP UA（连接到 WebRTC Gateway）
import JsSIP from 'jssip';

const socket = new JsSIP.WebSocketInterface('wss://webrtc-gateway.example.com:8089/ws');
const ua = new JsSIP.UA({
  sockets: [socket],
  uri: 'sip:agent001@example.com',
  password: 'agent_password',
  display_name: '坐席001'
});

ua.start();

// 2. 监听连接状态
ua.on('connected', () => {
  console.log('✅ 坐席已注册');
  updateAgentStatus('idle');  // 通知后台坐席空闲
});

// 3. 监听来电
ua.on('newRTCSession', (e) => {
  const session = e.session;
  
  if (session.direction === 'incoming') {
    // 获取来电信息
    const caller = session.remote_identity.uri.user;
    
    // 弹出接听 UI
    showIncomingCallDialog({
      caller: caller,
      callerName: session.remote_identity.display_name,
      
      // 坐席点击「接听」
      onAccept: () => {
        session.answer({
          mediaConstraints: { 
            audio: true,  // 启用麦克风
            video: false  // 客服场景通常无需视频
          },
          pcConfig: {
            iceServers: [
              { urls: 'stun:stun.l.google.com:19302' },
              { urls: 'turn:turn.example.com:3478', username: 'user', credential: 'pass' }
            ]
          }
        });
        
        updateAgentStatus('talking');  // 更新坐席状态
      },
      
      // 坐席点击「拒绝」
      onReject: () => {
        session.terminate();
      }
    });
  }
});

// 4. 监听通话结束
ua.on('ended', (e) => {
  console.log('📞 通话结束');
  updateAgentStatus('after_call_work');  // 话后处理状态
  
  // 5秒后自动回到空闲
  setTimeout(() => {
    updateAgentStatus('idle');
  }, 5000);
});

// 5. 坐席发起外呼
function makeCall(phoneNumber) {
  const session = ua.call(`sip:${phoneNumber}@example.com`, {
    mediaConstraints: { audio: true, video: false },
    pcConfig: { iceServers: [...] }
  });
  
  session.on('confirmed', () => {
    console.log('✅ 通话已建立');
  });
}

// 6. 转接功能（发送 REFER）
function transferCall(session, targetAgent) {
  session.refer(`sip:${targetAgent}@example.com`);
}
```

### 3.5 WebRTC Gateway 选型

| 方案                  | SIP 支持         | 复杂度    | 适用场景          |
|----------------------|-----------------|----------|------------------|
| **FreeSWITCH Verto** | ✅ 原生（mod_verto）| ⭐️⭐️⭐️  | 企业级客服（推荐）|
| **Janus SIP Plugin** | ✅ 原生          | ⭐️⭐️⭐️⭐️ | 高性能定制化      |
| **Asterisk**         | ✅ 原生（chan_pjsip）| ⭐️⭐️    | 中小型呼叫中心    |
| **自研**             | 需要实现 SIP 栈    | ⭐️⭐️⭐️⭐️⭐️| 不推荐            |

### 3.6 完整信令流程（WebRTC 坐席接听来电）

```
步骤 1: 客户拨打 400-XXX-XXXX
  └─→ SIP Trunk 转换为 INVITE sip:400xxxxxxx@php-exosip

步骤 2: php-exosip 收到 INVITE
  └─→ 查询队列 → 分配坐席 agent001

步骤 3: php-exosip 发送 INVITE 到 WebRTC Gateway
  └─→ INVITE sip:agent001@webrtc-gateway:5060

步骤 4: WebRTC Gateway 推送 WebSocket 消息
  └─→ 浏览器前端收到来电事件

步骤 5: 前端弹出接听 UI，坐席点击「接听」
  └─→ JsSIP 发送 200 OK (WebSocket)

步骤 6: WebRTC Gateway 转换为标准 SIP
  └─→ 200 OK (UDP) → php-exosip

步骤 7: php-exosip 转发 200 OK → 客户侧
  └─→ 通话建立

步骤 8: 媒体流转换
  客户 (RTP G.711) ⇄ RTPengine ⇄ Gateway (转码) ⇄ 浏览器 (Opus WebRTC)
```

### 3.7 前端工程化建议

**技术栈**:
- 前端框架: Vue 3 / React 18
- SIP 库: JsSIP 3.x
- WebRTC: 原生 RTCPeerConnection
- 状态管理: Pinia / Zustand
- 通信: WebSocket (SIP 信令) + REST (业务 API)

**关键模块**:
```typescript
// agent-client.ts - 坐席客户端封装
export class AgentClient {
  private ua: JsSIP.UA;
  private currentSession: JsSIP.RTCSession | null = null;

  constructor(config: {
    wsServer: string;
    username: string;
    password: string;
  }) {
    const socket = new JsSIP.WebSocketInterface(config.wsServer);
    this.ua = new JsSIP.UA({
      sockets: [socket],
      uri: `sip:${config.username}@example.com`,
      password: config.password
    });
    
    this.setupEventHandlers();
  }

  private setupEventHandlers() {
    this.ua.on('newRTCSession', this.handleIncomingCall);
    this.ua.on('registered', () => this.updateStatus('idle'));
    this.ua.on('unregistered', () => this.updateStatus('offline'));
  }

  login() {
    this.ua.start();
  }

  answer(session: JsSIP.RTCSession) {
    session.answer({
      mediaConstraints: { audio: true, video: false }
    });
  }

  transfer(targetAgent: string) {
    if (this.currentSession) {
      this.currentSession.refer(`sip:${targetAgent}@example.com`);
    }
  }

  hangup() {
    this.currentSession?.terminate();
  }
}
```

---

## 4. 三种架构方案横向对比

| 方案                          | 坐席接入     | 媒体服务器        | 录音回放      | 实现难度  | 推荐场景      |
|-------------------------------|------------|----------------|-------------|---------|-------------|
| **A: php-exosip + RTPengine + 软电话** | Linphone    | RTPengine      | 本地文件    | ⭐️⭐️    | MVP 快速验证 |
| **B: php-exosip + FreeSWITCH + 软电话** | Linphone    | FreeSWITCH     | WAV + 推流 ZLM | ⭐️⭐️⭐️  | 企业生产    |
| **C: php-exosip + FreeSWITCH + WebRTC** | 浏览器      | FreeSWITCH Verto| HLS 回放    | ⭐️⭐️⭐️⭐️ | 远程办公    |

---

## 5. 推荐实施路径

### 阶段 1: MVP（2-3 周）
- ✅ php-exosip + RTPengine + Linphone
- ✅ 1 个队列 + 3 个坐席
- ✅ 录音存本地

### 阶段 2: 生产化（4-6 周）
- ✅ 集成 FreeSWITCH（IVR + 混音）
- ✅ 录音推流到 ZLM（HLS 质检回放）
- ✅ 多队列 + 技能组

### 阶段 3: 企业级（8-12 周）
- ✅ WebRTC 浏览器坐席
- ✅ 咨询转接 + 三方通话
- ✅ ASR 质检 + 情绪识别

---

## 6. 常见问题 FAQ

**Q: 为什么不直接用 ZLM 做媒体服务器？**  
A: ZLM 缺少 SIP REFER（转接）、NOTIFY（状态通知）等客服核心信令，只能作为录音存储后端。

**Q: 坐席软电话和 WebRTC 能否混用？**  
A: ✅ 可以！php-exosip 同时支持两种坐席，通过不同的 SIP URI 区分（如 agent001 是软电话，webrtc_agent001 是浏览器）。

**Q: 录音必须经过 RTPengine 吗？**  
A: ✅ 是的！如果客户和坐席直连 RTP，转接时媒体会断开，录音也无法保证完整。

**Q: 浏览器坐席延迟会很高吗？**  
A: ⚠️ 正常情况 150-300ms（可接受），但跨地域/弱网可能 500ms+，需要优化 TURN 服务器部署。

**Q: FreeSWITCH vs Asterisk 怎么选？**  
A: FreeSWITCH 性能更高（10K+ 并发），Asterisk 生态更成熟（插件多）。客服场景推荐 FreeSWITCH。
