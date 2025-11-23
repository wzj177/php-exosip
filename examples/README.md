# PHP-ExoSIP 示例

本目录包含了 PHP-ExoSIP 扩展的使用示例。

## 目录结构

- `gb28181_server.php` - GB28181视频监控服务器示例
- `voip_call_server.php` - VoIP语音通话服务器示例 (待实现)
- `protocol/` - 协议处理类目录
  - `GB28181.php` - GB28181协议处理类

## GB28181 示例

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
