<?php
/**
 * GB28181 客户端（模拟设备）
 * 模拟GB28181设备注册、心跳、目录查询响应等
 */


// 设备配置
$deviceId = '34020000001320000001';  // 设备ID
$serverId = '34020000002000000001';  // 平台ID（暂不使用）
$realm = '3402000000';               // 域（行政区划码，10位）
$localIp = '127.0.0.1';              // 设备本地IP（可选）

// 创建 SIP 客户端
// GB28181 模式：通过 realm 自动启用
// - 有 realm: From = sip:设备ID@行政区划码 (GB28181)
// - 无 realm: From = sip:username@IP:port (通用 SIP)
// - local_port = 0: 自动分配端口（推荐）
$client = new ExoSipClient([
    'server_ip' => '127.0.0.1',
    'server_port' => 15060,
    'local_ip' => $localIp,                             // 可选，用于 Contact
    'local_port' => 0,                                  // 0 = 自动分配端口
    'username' => $deviceId,                            // 设备ID
    'password' => '12345678',
    'realm' => $realm,                                  // 行政区划码 → 启用 GB28181 模式
    'mode' => 'UDP',
    'expires' => 3600,
    'debug' => true
]);

echo "=== GB28181 Device Client ===\n";
echo "Device ID: $deviceId\n";
echo "Server ID: $serverId\n";
echo "Realm: $realm\n\n";

// 注意：不使用 start() 避免后台线程消费事件
// 改用手动模式：循环调用 processEvents()

// 发送注册（带重试）
echo "[SEND] Registering device...\n";
$rid = $client->sendRegister();
if ($rid < 0) {
    echo "[ERROR] Failed to send REGISTER (rid=$rid)\n";
    echo "[INFO] This usually means:\n";
    echo "  1. eXosip failed to build REGISTER message (check from/proxy/contact format)\n";
    echo "  2. Network initialization failed\n";
    echo "[INFO] Retrying up to 3 times...\n";
    
    $retry_count = 0;
    while ($retry_count < 3 && $rid < 0) {
        sleep(1);
        $rid = $client->sendRegister();
        $retry_count++;
        echo "[RETRY] Attempt $retry_count: rid=$rid\n";
    }
    
    if ($rid < 0) {
        echo "[FATAL] Cannot send REGISTER after retries, exiting\n";
        exit(1);
    }
}
echo "[OK] REGISTER sent (rid=$rid)\n";

// 等待注册结果（10秒超时）
echo "[WAIT] Waiting for registration response...\n";
$reg_wait_start = time();
$reg_timeout = 10;
$got_reg_response = false;

while (time() - $reg_wait_start < $reg_timeout && !$got_reg_response) {
    $events = $client->processEvents(100);
    
    foreach ($events as $evt) {
        if ($evt['type'] == EXOSIP_REGISTRATION_SUCCESS) {
            echo "[OK] Registration successful!\n";
            $registered = true;
            $got_reg_response = true;
            break;
        }
        
        if ($evt['type'] == EXOSIP_REGISTRATION_FAILURE) {
            echo "[ERROR] Registration failed";
            if (isset($evt['status_code'])) {
                echo " - Status: {$evt['status_code']}";
                if (isset($evt['reason'])) {
                    echo " ({$evt['reason']})";
                }
            }
            echo "\n";
            $registered = false;
            $got_reg_response = true;
            break;
        }
    }
    
    if (!$got_reg_response) {
        usleep(50000); // 50ms
    }
}

if (!$got_reg_response) {
    echo "[WARNING] No registration response received within {$reg_timeout}s\n";
    echo "[INFO] This indicates:\n";
    echo "  1. SIP server is NOT running on 127.0.0.1:15060\n";
    echo "  2. Server is not responding to REGISTER requests\n";
    echo "  3. Network/firewall blocking UDP packets\n";
    echo "[INFO] Continuing anyway (will retry registration periodically)...\n\n";
}

// 心跳发送函数
function sendKeepalive($client, $deviceId, $serverId, $realm) {
    $xml = <<<XML
<?xml version="1.0"?>
<Notify>
<CmdType>Keepalive</CmdType>
<SN>1</SN>
<DeviceID>{$deviceId}</DeviceID>
<Status>OK</Status>
</Notify>
XML;
    
    $to = "sip:{$serverId}@{$realm}";
    $client->sendMessage($to, $xml, 'Application/MANSCDP+xml');
    echo "[KEEPALIVE] Sent\n";
}

// 目录查询响应函数
function sendCatalogResponse($client, $deviceId, $serverId, $realm, $sn) {
    $xml = <<<XML
<?xml version="1.0"?>
<Response>
<CmdType>Catalog</CmdType>
<SN>{$sn}</SN>
<DeviceID>{$deviceId}</DeviceID>
<SumNum>2</SumNum>
<DeviceList Num="2">
<Item>
<DeviceID>{$deviceId}01</DeviceID>
<Name>Camera 01</Name>
<Manufacturer>PHP-Manufacturer</Manufacturer>
<Model>PHP-Model</Model>
<Status>ON</Status>
</Item>
<Item>
<DeviceID>{$deviceId}02</DeviceID>
<Name>Camera 02</Name>
<Manufacturer>PHP-Manufacturer</Manufacturer>
<Model>PHP-Model</Model>
<Status>ON</Status>
</Item>
</DeviceList>
</Response>
XML;
    
    $to = "sip:{$serverId}@{$realm}";
    $client->sendMessage($to, $xml, 'Application/MANSCDP+xml');
    echo "[CATALOG] Response sent (SN: $sn)\n";
}

// 设备信息响应函数
function sendDeviceInfoResponse($client, $deviceId, $serverId, $realm, $sn) {
    $xml = <<<XML
<?xml version="1.0"?>
<Response>
<CmdType>DeviceInfo</CmdType>
<SN>{$sn}</SN>
<DeviceID>{$deviceId}</DeviceID>
<Result>OK</Result>
<DeviceName>PHP Test Device</DeviceName>
<Manufacturer>PHP-Manufacturer</Manufacturer>
<Model>PHP-Model-1.0</Model>
<Firmware>v1.0.0</Firmware>
<Channel>2</Channel>
</Response>
XML;
    
    $to = "sip:{$serverId}@{$realm}";
    $client->sendMessage($to, $xml, 'Application/MANSCDP+xml');
    echo "[DEVICEINFO] Response sent (SN: $sn)\n";
}

// 主循环
$keepalive_interval = 30;  // 30秒心跳
$last_keepalive = 0;
$registered = false;  // 跟踪注册状态
$running = true;
$register_retry_interval = 30; // 30秒重新注册
$last_register_attempt = time();

echo "\n[INFO] Starting main loop (Press Ctrl+C to stop)...\n\n";

while ($running) {
    // 手动处理事件（不使用后台线程）
    $events = $client->processEvents(100);  // 100ms 超时
    
    foreach ($events as $evt) {
        // 注册成功
        if ($evt['type'] == EXOSIP_REGISTRATION_SUCCESS) {
            if (!$registered) {
                echo "[OK] Registration successful!\n";
            }
            $registered = true;
            $last_register_attempt = time();
        }
        
        // 注册失败
        if ($evt['type'] == EXOSIP_REGISTRATION_FAILURE) {
            echo "[ERROR] Registration failed";
            if (isset($evt['status_code'])) {
                echo " - Status: {$evt['status_code']}";
                if (isset($evt['reason'])) {
                    echo " ({$evt['reason']})";
                }
            }
            echo "\n";
            $registered = false;
        }
        
        // 收到请求（服务器查询）
        if (isset($evt['method'])) {
            echo "[RECV] Request: {$evt['method']}\n";
            
            if ($evt['method'] == 'MESSAGE') {
                // 这里需要解析消息体，判断是什么查询
                // 简化处理：假设收到了 Catalog 查询
                // 实际需要从事件中提取消息体并解析 XML
                echo "[TODO] Parse MESSAGE body to handle query\n";
            }
        }
        
        // 收到响应
        if (isset($evt['status_code'])) {
            echo "[RECV] Response: {$evt['status_code']}";
            if (isset($evt['reason'])) {
                echo " {$evt['reason']}";
            }
            echo "\n";
        }
    }
    
    // 如果未注册且距离上次尝试超过间隔，重新发送注册
    if (!$registered && (time() - $last_register_attempt >= $register_retry_interval)) {
        echo "[RETRY] Re-attempting registration...\n";
        $rid = $client->sendRegister();
        if ($rid < 0) {
            echo "[ERROR] Failed to send REGISTER (rid=$rid)\n";
        }
        $last_register_attempt = time();
    }
    
    // 发送心跳（使用本地状态判断）
    if ($registered) {
        $now = time();
        if ($now - $last_keepalive >= $keepalive_interval) {
            sendKeepalive($client, $deviceId, $serverId, $realm);
            $last_keepalive = $now;
        }
    }
    
    // 检查运行时间（演示60秒后退出）
    static $start_time = null;
    if ($start_time === null) {
        $start_time = time();
    }
    
    if (time() - $start_time > 60) {
        echo "\n[INFO] Demo timeout (60s), exiting...\n";
        $running = false;
    }
    
    // 短暂休眠，避免 CPU 占用过高
    usleep(10000); // 10ms
}

// 清理
echo "\n[SEND] Unregistering...\n";
$client->sendUnregister();
sleep(1);

// 不需要调用 stop()，因为没有后台线程
echo "[OK] Client stopped\n";

// 显示统计
$stats = $client->getStats();
echo "\n=== Final Statistics ===\n";
echo "Requests sent: {$stats['request_count']}\n";
echo "Responses received: {$stats['response_count']}\n";
echo "Timeouts: {$stats['timeout_count']}\n";

