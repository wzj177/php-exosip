# 示例代码索引

本目录包含 php-exosip 扩展的所有示例代码和测试文件。

## 📚 生产级示例

### [gb28181_server_safe.php](gb28181_server_safe.php) ⭐️
**生产推荐 - GB28181 服务器(安全模式)**
- 使用 CallbackWrapper 保护所有回调
- Master-Worker-Task 多进程架构
- 完整的错误处理
- 适合生产环境部署

```bash
php gb28181_server_safe.php
```

### [gb28181_server.php](gb28181_server.php)
**标准 GB28181 服务器**
- 设备注册和认证
- 心跳检测
- 目录查询
- 云台控制
- 单进程模式

## 🧪 测试和演示

### Master-Worker-Task 架构测试

#### [test_pipe_message.php](test_pipe_message.php) ⭐️
**Task→Worker 管道通信基础测试**
- sendToWorker() 使用演示
- onPipeMessage 回调示例
- 三种任务类型演示(query/update/batch)
- 进度推送示例

```bash
php test_pipe_message.php
```

#### [test_laravel_integration.php](test_laravel_integration.php) ⭐️
**Laravel 集成完整示例**
- Laravel → Redis → Task → Worker → Device 完整流程
- PTZ 控制命令异步处理
- 目录查询异步处理
- 录像控制异步处理
- Redis 轮询模拟

```bash
php test_laravel_integration.php
```

#### [test_task_server_safety.php](test_task_server_safety.php) ⭐️
**Task 进程安全性测试**
- $server 对象使用规则演示
- 进程隔离验证
- 安全/危险/禁止操作示例

```bash
php test_task_server_safety.php
```

### TCP 模式测试

#### [test_tcp_mode.php](test_tcp_mode.php) ⭐️
**TCP 传输模式完整测试**
- TCP 连接建立(onConnect)
- 设备注册时绑定 fd
- 消息处理时查询 fd
- 连接断开时解绑(onClose)
- 设备自动离线标记

```bash
php test_tcp_mode.php
```

#### [test_tcp_debug.php](test_tcp_debug.php)
**TCP 调试工具**
- TCP 事件详细日志
- 连接状态跟踪

### 客户端测试

#### [test_client.php](test_client.php)
**SIP 客户端基础测试**
- ExoSipClient 手动模式
- REGISTER/MESSAGE/INVITE
- 事件循环演示

```bash
php test_client.php
```

#### [gb28181_client.php](gb28181_client.php)
**GB28181 设备端模拟器**
- 设备注册
- Keepalive 心跳
- 目录上报
- PTZ 控制响应

### 集成测试

#### [test_gb28181_integration.php](test_gb28181_integration.php)
**GB28181 完整集成测试**
- 服务端+客户端完整交互
- 注册、心跳、查询流程
- 自动化测试脚本

#### [test_server_client.php](test_server_client.php)
**服务器-客户端通信测试**
- 基础 SIP 通信验证
- MESSAGE 方法测试

### 其他测试

#### [test_event_debug.php](test_event_debug.php)
**事件调试工具**
- 详细的事件日志
- 事件类型和内容输出
- 用于协议调试

#### [test_device_manager.php](test_device_manager.php)
**DeviceManager 单元测试**
- 设备添加/删除/更新
- 心跳检测
- 超时处理
- TCP 连接管理(bindConnection/unbindConnection)

#### [test_body_extraction.php](test_body_extraction.php)
**消息体提取测试**
- XML 解析验证
- GB28181 消息格式测试

## 📦 辅助类和工具

### [DeviceManager.php](DeviceManager.php) ⭐️
**设备管理器**
- 设备注册/注销
- 心跳超时检测
- 设备状态统计
- TCP 连接管理(Task 3 新增)

### [ServerDevice.php](ServerDevice.php)
**服务器端设备对象**
- 设备信息封装
- 状态管理
- 目录管理

### [Device.php](Device.php)
**客户端设备对象**
- 设备端模拟
- 状态机管理

### [Timer.php](Timer.php)
**定时器工具类**
- 定时任务管理
- 心跳定时器

### [protocol/](protocol/)
**GB28181 协议处理**
- GB28181Handler.php - 协议处理器 ⭐️
- CallbackWrapper.php - 回调包装器 ⭐️
- Message/ - 消息处理模块

### [sip_config.php](sip_config.php)
**SIP 配置示例**
- 服务器配置模板
- 常用配置说明

### [sip_session_management.php](sip_session_management.php)
**会话管理示例**
- SipSession 使用方法
- 会话生命周期管理

## 🎯 VoIP 示例

### [voip_call_server.php](voip_call_server.php)
**VoIP 呼叫服务器**
- INVITE 处理
- SDP 协商
- 呼叫管理

## 🌐 WebRTC 演示

### [webrtc-demo/](webrtc-demo/)
**WebRTC 集成演示**
- SIP-WebRTC 网关
- 媒体协商

## 📖 参考实现

### [exosip_wrapper_v1.c](exosip_wrapper_v1.c)
**C 封装早期版本**
- 历史参考代码

### [SipServer.cpp](SipServer.cpp) / [ua.c](ua.c)
**C/C++ 参考实现**
- eXosip2 原生 API 使用示例

### [workerman_headbeat.php](workerman_headbeat.php)
**Workerman 心跳示例**
- TCP 连接心跳检测
- 参考 Workerman 模式

## 🗂️ 文档

### [ARCHITECTURE.md](ARCHITECTURE.md)
**架构设计文档**
- 系统架构演进
- 设计决策说明

---

## 使用建议

### 快速开始
1. [gb28181_server_safe.php](gb28181_server_safe.php) - 直接运行看效果
2. [test_pipe_message.php](test_pipe_message.php) - 理解管道通信
3. [test_tcp_mode.php](test_tcp_mode.php) - 学习 TCP 模式

### 开发参考
- **服务端**: [gb28181_server_safe.php](gb28181_server_safe.php) + [DeviceManager.php](DeviceManager.php)
- **客户端**: [gb28181_client.php](gb28181_client.php) + [Device.php](Device.php)
- **协议处理**: [protocol/GB28181Handler.php](protocol/GB28181Handler.php)

### 测试验证
- **功能测试**: test_*.php 系列
- **集成测试**: [test_gb28181_integration.php](test_gb28181_integration.php)
- **压力测试**: 参考生产部署文档

### Laravel 集成
1. 参考 [test_laravel_integration.php](test_laravel_integration.php)
2. 使用 Task 进程处理 HTTP/DB/Redis
3. 使用 sendToWorker() 实时推送
4. 使用 onPipeMessage 接收推送

### TCP 模式部署
1. 参考 [test_tcp_mode.php](test_tcp_mode.php)
2. 使用 DeviceManager 管理连接
3. 在 handleRegister 中绑定 fd
4. 在 handleClose 中解绑 fd

---

## GB28181 协议类使用 (旧版文档)

<details>
<summary>点击展开旧版 GB28181.php 文档(已废弃,推荐使用 GB28181Handler)</summary>

### gb28181_server.php

完整的GB28181视频监控服务器实现，展示了：

- 设备注册处理
- 心跳保活
- 设备目录查询
- 视频点播 (INVITE)
- 设备控制 (PTZ)
- 报警信息处理

**运行方式：**

```bash
php gb28181_server.php
```

**输出示例：**

```
=================================
  GB28181 Video Server
=================================
Server ID: 34020000002000000001
Domain: 3402000000
Listening on: 0.0.0.0:5060
Transport: UDP
=================================

📱 GB28181设备注册: 34020000001320000001
✅ 设备注册成功，当前在线设备: 1
📤 发送目录查询: 34020000001320000001
💓 心跳: 34020000001320000001
📂 目录响应: 34020000001320000001
📊 设备总数: 4
  📹 34020000001320000001 - 前门摄像头
  📹 34020000001320000002 - 后门摄像头
  📹 34020000001320000003 - 停车场摄像头
  📹 34020000001320000004 - 大厅摄像头
```

### gb28181_server.php

完整的GB28181视频监控服务器实现，展示了：

- 设备注册处理
- 心跳保活
- 设备目录查询
- 视频点播 (INVITE)
- 设备控制 (PTZ)
- 报警信息处理

**运行方式：**

```bash
php gb28181_server.php
```

**输出示例：**

```
=================================
  GB28181 Video Server
=================================
Server ID: 34020000002000000001
Domain: 3402000000
Listening on: 0.0.0.0:5060
Transport: UDP
=================================

📱 GB28181设备注册: 34020000001320000001
✅ 设备注册成功，当前在线设备: 1
📤 发送目录查询: 34020000001320000001
💓 心跳: 34020000001320000001
📂 目录响应: 34020000001320000001
📊 设备总数: 4
  📹 34020000001320000001 - 前门摄像头
  📹 34020000001320000002 - 后门摄像头
  📹 34020000001320000003 - 停车场摄像头
  📹 34020000001320000004 - 大厅摄像头
```

## 协议类使用

### GB28181 协议类

`protocol/GB28181.php` 封装了完整的GB28181协议业务逻辑：

```php
<?php
require_once 'protocol/GB28181.php';

// 创建SIP服务器
$sipServer = new ExoSip();
$sipServer->init([
    'ip' => '0.0.0.0',
    'port' => 5060,
    'mode' => 'udp'
]);

// 创建GB28181协议处理器
$gb28181 = new GB28181($sipServer, [
    'server_id' => '34020000002000000001',
    'server_domain' => '3402000000',
    'heartbeat_timeout' => 180,
    'register_expires' => 3600,
    'catalog_auto_query' => true,
]);

// 绑定事件处理器
$gb28181->bindEvents();

// 启动服务器
$sipServer->run();
```

### 主要功能

#### 1. 设备注册

```php
// 自动处理设备注册，验证设备ID格式
// 注册成功后自动查询设备目录（如果启用catalog_auto_query）
```

#### 2. 消息处理

支持的GB28181命令类型：
- `Keepalive` - 设备心跳
- `Catalog` - 设备目录响应
- `DeviceInfo` - 设备信息响应
- `DeviceStatus` - 设备状态响应
- `Alarm` - 报警信息

#### 3. 主动查询

```php
// 查询设备目录
$gb28181->queryCatalog($deviceId);

// 查询设备信息
$gb28181->queryDeviceInfo($deviceId);

// PTZ控制
$gb28181->ptzControl($deviceId, $channelId, $ptzCommand);
```

#### 4. 统计信息

```php
// 获取在线设备列表
$onlineDevices = $gb28181->getOnlineDevices();

// 获取设备目录
$catalog = $gb28181->getDeviceCatalog($deviceId);

// 获取统计信息
$stats = $gb28181->getStats();
// [
//     'total_devices' => 10,
//     'online_devices' => 8,
//     'offline_devices' => 2,
//     'total_channels' => 32
// ]
```

## 自定义协议

可以参考 `GB28181.php` 创建自己的协议处理类：

```php
<?php
class MyProtocol {
    private $sipServer;
    
    public function __construct($sipServer, array $config = []) {
        $this->sipServer = $sipServer;
        $this->config = $config;
    }
    
    public function bindEvents() {
        $this->sipServer->onRegister = [$this, 'handleRegister'];
        $this->sipServer->onMessage = [$this, 'handleMessage'];
        $this->sipServer->onInvite = [$this, 'handleInvite'];
    }
    
    public function handleRegister($event) {
        // 自定义注册处理逻辑
    }
    
    public function handleMessage($event) {
        // 自定义消息处理逻辑
    }
    
    public function handleInvite($event) {
        // 自定义会话处理逻辑
    }
}
```

## 测试工具

### SIP客户端测试

可以使用以下工具测试服务器：

1. **SIPp** - SIP协议压力测试工具
```bash
sipp -sn uac 127.0.0.1:5060
```

2. **Linphone** - SIP软电话客户端

3. **GB28181模拟器** - 用于测试GB28181设备接入

### 抓包分析

```bash
# 使用tcpdump抓取SIP包
sudo tcpdump -i any -n port 5060 -w sip.pcap

# 使用Wireshark分析
wireshark sip.pcap
```

## 生产部署

### Supervisor 配置

```ini
[program:gb28181-server]
command=/usr/bin/php /path/to/gb28181_server.php
directory=/path/to/examples
autostart=true
autorestart=true
user=www-data
numprocs=1
redirect_stderr=true
stdout_logfile=/var/log/gb28181-server.log
stopwaitsecs=10
```

### Systemd 服务

```ini
[Unit]
Description=GB28181 Video Server
After=network.target

[Service]
Type=simple
User=www-data
WorkingDirectory=/path/to/examples
ExecStart=/usr/bin/php /path/to/gb28181_server.php
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

## 性能优化

### 1. PHP配置优化

```ini
; php.ini
memory_limit = 512M
max_execution_time = 0
opcache.enable=1
opcache.enable_cli=1
```

### 2. 系统参数优化

```bash
# 增加文件描述符限制
ulimit -n 65535

# 优化网络参数
sysctl -w net.core.rmem_max=16777216
sysctl -w net.core.wmem_max=16777216
```

### 3. 日志管理

建议使用日志轮转避免日志文件过大：

```bash
# /etc/logrotate.d/gb28181
/var/log/gb28181-server.log {
    daily
    rotate 7
    compress
    delaycompress
    missingok
    notifempty
}
```

## 故障排查

### 1. 端口被占用

```bash
# 检查端口占用
lsof -i :5060

# 或使用netstat
netstat -tulnp | grep 5060
```

### 2. 防火墙配置

```bash
# CentOS/RHEL
firewall-cmd --permanent --add-port=5060/udp
firewall-cmd --reload

# Ubuntu/Debian
ufw allow 5060/udp
```

### 3. 调试模式

在代码中启用详细日志：

```php
error_reporting(E_ALL);
ini_set('display_errors', 1);
```

## 更多资源

- [GB28181协议标准](https://www.gb28181.org/)
- [SIP协议 RFC 3261](https://tools.ietf.org/html/rfc3261)
- [eXosip2文档](http://savannah.nongnu.org/projects/exosip)
- [项目文档](../docs/)

## 贡献

欢迎提交示例代码和改进建议！
