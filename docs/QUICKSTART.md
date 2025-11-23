# 快速开始

## 5分钟上手

### 1. 编译安装

```bash
# 进入项目目录
cd php-exosip

# 编译扩展
phpize
./configure
make
sudo make install

# 启用扩展
echo "extension=exosip.so" | sudo tee /etc/php/8.2/mods-available/exosip.ini
sudo phpenmod exosip

# 验证安装
php -m | grep exosip
```

### 2. 第一个SIP服务器

创建 `server.php`：

```php
<?php
// 创建SIP服务器
$sip = new ExoSip([
    'host' => '0.0.0.0',
    'port' => 5060,
    'mode' => 'UDP'  // macOS推荐UDP
]);

// 处理REGISTER（设备注册）
$sip->onRegister = function($event) use ($sip) {
    $from = $event->getFromUri();
    echo "[REGISTER] {$from}\n";
    
    // 发送200 OK响应
    $sip->sendResponse($event->getTid(), 200);
};

// 处理MESSAGE（消息/心跳）
$sip->onMessage = function($event) use ($sip) {
    $from = $event->getFromUri();
    $body = $event->getBody();
    echo "[MESSAGE] From: {$from}\n";
    echo "Body: {$body}\n\n";
    
    $sip->sendResponse($event->getTid(), 200);
};

echo "SIP Server started on 0.0.0.0:5060\n";
echo "Press Ctrl+C to stop\n\n";

// 启动事件循环
$sip->run();
```

运行：

```bash
php server.php
```

### 3. 测试服务器

打开新终端，发送测试消息：

```bash
# 使用 nc 发送 SIP MESSAGE
cat <<EOF | nc -u localhost 5060
MESSAGE sip:test@localhost:5060 SIP/2.0
Via: SIP/2.0/UDP localhost:5060;branch=z9hG4bK776asdhds
From: <sip:user@localhost>;tag=1928301774
To: <sip:test@localhost>
Call-ID: a84b4c76e66710@localhost
CSeq: 1 MESSAGE
Content-Type: text/plain
Content-Length: 11

Hello World
EOF
```

应该看到服务器输出：

```
[MESSAGE] From: sip:user@localhost
Body: Hello World
```

### 4. SIP 客户端

创建 `client.php`：

```php
<?php
// 创建 SIP 客户端
$client = new ExoSipClient([
    'server_ip' => '127.0.0.1',
    'server_port' => 5060,
    'username' => 'device001',
    'mode' => 'UDP'
]);

// 启动并注册
$client->start();
$client->sendRegister();

// 等待注册成功
sleep(2);

if ($client->isRegistered()) {
    echo "✅ Registered!\n";
    
    // 发送消息
    $client->sendMessage('sip:server@domain', 'Hello!');
}

// 处理响应
$events = $client->processEvents(1000);
foreach ($events as $evt) {
    echo "Event: type={$evt['type']}\n";
}

$client->stop();
```

### 5. GB28181 示例

创建 `gb28181.php`：

```php
<?php
$devices = [];

$sip = new ExoSip([
    'port' => 5060,
    'sipId' => '34020000002000000001',
    'sipRealm' => '3402000000'
]);

// 设备注册
$sip->onRegister = function($event) use ($sip, &$devices) {
    preg_match('/sip:(\d+)@/', $event->getFromUri(), $m);
    $deviceId = $m[1] ?? 'unknown';
    
    $devices[$deviceId] = [
        'id' => $deviceId,
        'online' => true,
        'lastRegister' => time()
    ];
    
    echo "✅ Device {$deviceId} registered\n";
    $sip->sendResponse($event->getTid(), 200);
    
    // 查询设备目录
    $xml = <<<XML
<?xml version="1.0"?>
<Query>
<CmdType>Catalog</CmdType>
<SN>1</SN>
<DeviceID>{$deviceId}</DeviceID>
</Query>
XML;
    $sip->sendMessage("sip:{$deviceId}@3402000000", $xml, 'Application/MANSCDP+xml');
};

// Keepalive心跳
$sip->onMessage = function($event) use ($sip, &$devices) {
    $body = $event->getBody();
    $xml = @simplexml_load_string($body);
    
    if ($xml && (string)$xml->CmdType === 'Keepalive') {
        $deviceId = (string)$xml->DeviceID;
        $devices[$deviceId]['lastKeepalive'] = time();
        echo "💓 Keepalive from {$deviceId}\n";
        $sip->sendResponse($event->getTid(), 200);
    }
};

// 定时显示在线设备
pcntl_signal(SIGALRM, function() use (&$devices) {
    echo "\n📊 Online devices: " . count($devices) . "\n";
    pcntl_alarm(30);
});
pcntl_alarm(30);

echo "GB28181 Server started\n";
$sip->run();
```

### 5. 跨平台配置

创建 `cross_platform.php`：

```php
<?php
// 自动适配平台
$config = [
    'host' => '0.0.0.0',
    'port' => 5060,
    'mode' => match (PHP_OS_FAMILY) {
        'Darwin' => 'UDP',  // macOS
        'Windows' => 'UDP', // Windows
        'Linux' => 'TCP',   // Linux
        default => 'UDP'
    }
];

$sip = new ExoSip($config);
echo "Running on " . PHP_OS_FAMILY . " with {$config['mode']} mode\n";

$sip->onRegister = function($event) use ($sip) {
    $sip->sendResponse($event->getTid(), 200);
};

$sip->run();
```

## 下一步

1. 查看 [examples/](examples/) 目录获取更多示例
2. 阅读 [README.md](README.md) 了解完整API
3. 使用 [docs/exosip.stub.php](docs/exosip.stub.php) 获取IDE支持
4. 参考 [GB28181-Service](examples/GB28181-Service/) 了解C++实现

## 常见问题

**Q: macOS上TCP无响应？**  
A: macOS的eXosip2对TCP支持有限，建议使用UDP或Docker Linux环境。

**Q: 如何调试？**  
A: 启用debug模式：
```php
$sip = new ExoSip(['debug' => true]);
```

**Q: 如何部署生产环境？**  
A: 参考 [README.md#生产部署](README.md#生产部署) 章节。

---

**Ready to build!** 🚀

