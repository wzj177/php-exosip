# GB28181 完整解决方案

这是一个完整的GB28181国标实现，采用微服务架构，包含信令网关和API业务系统。

## 架构特点

✅ **职责分离**：信令网关只处理SIP协议，API项目处理业务逻辑  
✅ **SSRC管理**：数据库保证SSRC唯一性，避免流冲突  
✅ **事件驱动**：通过Hook和Redis实现松耦合通信  
✅ **多协议支持**：RTSP/RTMP/FLV/HLS/WebRTC播放  
✅ **生产就绪**：73%功能完成，核心流程已实现

## 快速开始

### 前置依赖

```bash
# 1. 安装依赖
brew install pkg-config osip exosip  # macOS
# 或
yum install libosip2-devel libeXosip2-devel  # CentOS

# 2. 启动基础服务
# MySQL (端口3306)
# Redis (端口6379)
# ZLMediaKit (端口80, RTP端口30000-40000)
```

### 一键启动

```bash
cd examples
./start_gb28181.sh
```

### 手动启动

```bash
# 1. 初始化数据库
mysql -u root -p gbvr_iot < gbvr-iot/database/migrations/gb28181_tables.sql

# 2. 启动信令网关
cd examples
php gb28181_server.php

# 3. 启动API服务
cd gbvr-iot
php start.php start
```

## 核心流程

### 1. 设备注册

```
设备 --[SIP REGISTER]--> 信令网关 --[Hook推送]--> API --[存储]--> MySQL
```

### 2. 启动直播

```
客户端 --[HTTP POST]--> API
         ↓
    [生成SSRC + 分配ZLM端口]
         ↓
    [Redis发布命令]
         ↓
    信令网关 --[SIP INVITE + SDP]--> 设备
         ↓
    设备 --[200 OK + 设备SSRC]--> 信令网关
         ↓
    [Hook推送media_ready] --> API
         ↓
    [更新ZLM设备SSRC]
         ↓
    设备 --[RTP推流]--> ZLMediaKit
         ↓
    [多协议播放]
```

## RTP传输模式

支持三种RTP传输模式，适用于不同网络环境：

| 模式 | tcp_mode | 适用场景 | NAT穿透 | 延迟 |
|------|----------|----------|---------|------|
| UDP | 0 | 局域网 | ❌困难 | 最低 |
| **TCP被动** | 1 | **公网推荐** | ✅简单 | 中等 |
| TCP主动 | 2 | 特殊场景 | ❌困难 | 中等 |

**配置方式：**
```php
// config/zlm.php
return [
    'default_tcp_mode' => 1,  // 公网推荐TCP被动模式
];
```

**模式说明：**
- **UDP模式**：延迟最低，但公网需要映射整个端口段（30000-40000）
- **TCP被动模式**：设备主动连接服务器，公网只需开放服务器端口，推荐
- **TCP主动模式**：服务器连接设备，需要设备端口映射，适用于语音对讲

详见：[RTP传输模式详解](gbvr-iot/docs/GB28181_INTEGRATION_GUIDE.md#一rtp传输模式详解)

## API接口

### 设备管理

```bash
# 获取设备列表
GET /api/v2/gb28181/devices

# 查询设备目录
POST /api/v2/gb28181/devices/{device_id}/catalog
```

### 流控制

```bash
# 开始直播
POST /api/v2/gb28181/channels/start-live
{
  "device_id": "34020000001320000001",
  "channel_id": "34020000001320000002"
}

# 获取播放地址
GET /api/v2/gb28181/channels/play-urls?stream_id=xxx

# 停止直播
POST /api/v2/gb28181/channels/stop-live
{
  "device_id": "34020000001320000001",
  "channel_id": "34020000001320000002"
}

# 云台控制
POST /api/v2/gb28181/channels/ptz
{
  "device_id": "34020000001320000001",
  "channel_id": "34020000001320000002",
  "command": "up",
  "speed": 50
}
```

## 配置文件

### 信令网关 (examples/gb28181_server.php)

```php
$config = [
    'sip_server' => [
        'ip' => '0.0.0.0',
        'port' => 5060,
        'domain' => '3402000000',
        'server_id' => '34020000002000000001',
    ],
    'hook_url' => 'http://127.0.0.1:8787/api/v2/gb/server/hock',
    'redis' => [
        'host' => '127.0.0.1',
        'port' => 6379,
        'channel' => 'gb28181:commands',
    ],
];
```

### API项目 (gbvr-iot/config/zlm.php)

```php
return [
    'host' => '127.0.0.1',
    'port' => 80,
    'secret' => 'your_secret_key',
    'rtp_port_start' => 30000,
    'rtp_port_end' => 40000,
    'default_tcp_mode' => 1,  // 0=UDP, 1=TCP被动(推荐), 2=TCP主动
];
```

**公网部署防火墙配置：**
```bash
# TCP被动模式只需开放服务器RTP端口
firewall-cmd --add-port=30000-40000/tcp --permanent
firewall-cmd --reload
```

## 目录结构

```
php-exosip/
├── examples/
│   ├── gb28181_server.php           # 信令网关主程序
│   ├── protocol/
│   │   ├── GB28181Handler.php       # 核心SIP处理（已移除ZLM依赖）
│   │   ├── CommandDispatcher.php    # Redis命令调度（支持外部SSRC）
│   │   ├── DeviceManager.php        # 设备会话管理
│   │   └── MessageHandler.php       # SIP消息处理
│   ├── start_gb28181.sh             # 一键启动脚本
│   ├── stop_gb28181.sh              # 停止脚本
│   └── gbvr-iot/                    # API业务项目
│       ├── app/api/v2/controller/
│       │   ├── GBServerHockController.php       # Hook接收器
│       │   ├── GB28181DeviceController.php      # 设备管理API
│       │   └── GB28181StreamController.php      # 流控制API
│       ├── CoreW/Sdk/ZLMediaKit/
│       │   └── ZLMClient.php        # ZLM SDK客户端
│       ├── config/
│       │   ├── zlm.php              # ZLM配置
│       │   └── routes/v1.php        # 路由配置
│       ├── database/migrations/
│       │   └── gb28181_tables.sql   # 数据库表结构
│       └── docs/
│           └── GB28181_INTEGRATION_GUIDE.md  # 完整集成文档
```

## 已完成功能 ✅

- [x] 设备注册/注销
- [x] 心跳保活
- [x] 设备目录查询
- [x] 实时视频点播（含SSRC管理）
- [x] 停止视频
- [x] 云台控制（PTZ）
- [x] 历史录像回放
- [x] 设备状态查询
- [x] 报警订阅
- [x] 多协议播放（RTSP/RTMP/FLV/HLS/WebRTC）
- [x] Hook事件推送
- [x] Redis命令调度
- [x] ZLM端口管理
- [x] SSRC唯一性保证

## 待完善功能 🚧

- [ ] 录像下载
- [ ] 语音对讲
- [ ] 设备配置查询/设置
- [ ] 移动位置订阅
- [ ] 多ZLM负载均衡
- [ ] 流媒体质量监控
- [ ] 心跳超时检测
- [ ] 会话异常恢复

## 调试方法

### 查看日志

```bash
# 信令网关日志
tail -f logs/gb28181.log

# API日志
tail -f gbvr-iot/runtime/logs/sip.log

# ZLM日志
tail -f /path/to/ZLMediaKit/log/MediaServer.log
```

### 测试Redis命令

```bash
redis-cli
> SUBSCRIBE gb28181:commands
> PUBLISH gb28181:commands '{"command":"query_catalog","device_id":"34020000001320000001"}'
```

### 查看ZLM流

```bash
curl "http://127.0.0.1/index/api/getMediaList?secret=your_secret"
```

## 关键修复记录

### ✅ SSRC管理重构 (2024)

**问题**：SSRC硬编码为"0100000001"导致多路流冲突

**解决方案**：
1. 移除信令网关中的ZLM依赖
2. 在API项目的`device_channels`表中为每个通道生成唯一SSRC
3. `CommandDispatcher`改为接收外部传入的SSRC和ZLM端口
4. 通过Redis将SSRC从API传递给信令网关
5. 设备200 OK后通过Hook回传设备SSRC给API更新ZLM

**文件改动**：
- `GB28181Handler.php`: 移除ZLMClient依赖（3处修改）
- `CommandDispatcher.php`: 支持外部SSRC（4个函数修改）
- `GB28181StreamController.php`: 生成SSRC并分配ZLM端口
- `GBServerHockController.php`: 处理media_ready事件并更新ZLM

## 与AKStream对比

| 功能 | 本项目 | AKStream |
|------|--------|----------|
| 架构 | 微服务（网关+API） | 单体C# |
| SSRC管理 | 数据库唯一性 | 自增管理 |
| 通信方式 | Hook + Redis | 直接调用 |
| 扩展性 | 高（松耦合） | 中（紧耦合） |
| 生产就绪度 | 73% | 100% |

## 常见问题

**Q: 启动失败？**  
A: 检查依赖服务（MySQL/Redis/ZLM）是否运行，端口是否冲突

**Q: 直播无画面？**  
A: 
1. 检查ZLM是否收到RTP流：`curl http://127.0.0.1/index/api/getMediaList`
2. 检查SSRC是否正确：查看API日志中的`media_ready`处理
3. 使用VLC测试RTSP地址

**Q: 设备注册不上？**  
A: 检查设备配置的服务器IP、端口、国标ID是否正确

## 文档

- [完整集成指南](gbvr-iot/docs/GB28181_INTEGRATION_GUIDE.md)
- [API接口文档](gbvr-iot/docs/GB28181_API.md)
- [架构设计文档](docs/架构升级.md)

## License

MIT License

## 支持

遇到问题请查看文档或提交Issue。
