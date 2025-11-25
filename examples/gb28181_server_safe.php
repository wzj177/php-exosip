<?php
/**
 * GB28181 视频监控服务器示例 - 使用安全的回调包装器
 * 
 * 演示如何使用 CallbackWrapper 来保护事件处理器,避免回调中的错误导致服务崩溃
 */

require_once __DIR__ . '/protocol/GB28181Handler.php';
require_once __DIR__ . '/protocol/CallbackWrapper.php';
require_once __DIR__ . '/DeviceManager.php';

// 初始化SIP服务器
$host = '0.0.0.0';
$port = 15060;
$protocol = 'udp';
$sipId = '34020000002000000001';
$sipRealm = '3402000000';

$sipServer = new ExoSip([
    'ua' => 'GB28181-Server/1.0',
    'ip' => '0.0.0.0',
    'port' => $port,
    'mode' => $protocol,
    'sipId' => $sipId,
    'sipRealm' => $sipRealm,
    'debug' => true,
    'pid_file' => '/tmp/gb28181_server.pid',
    'task_worker_num' => 1,
    'timer_interval' => 30000,
]);

// 创建GB28181协议处理器
$gb28181 = new GB28181Handler($sipServer, [
    'server_id' => $sipId,
    'server_domain' => $sipRealm,
    'heartbeat_timeout' => 180,
    'register_expires' => 3600,
    'catalog_auto_query' => true,
    'debug' => true,
]);

// ========================================
// 方式1: 使用 wrap() 包装单个方法
// ========================================
$sipServer->on('register', CallbackWrapper::wrap($gb28181, 'handleRegister'));
$sipServer->on('message', CallbackWrapper::wrap($gb28181, 'handleMessage'));
$sipServer->on('invite', CallbackWrapper::wrap($gb28181, 'handleInvite'));
$sipServer->on('ack', CallbackWrapper::wrap($gb28181, 'handleAck'));
$sipServer->on('bye', CallbackWrapper::wrap($gb28181, 'handleBye'));
$sipServer->on('response', CallbackWrapper::wrap($gb28181, 'handleResponse'));
$sipServer->on('error', CallbackWrapper::wrap($gb28181, 'handleError'));

// ========================================
// 方式2: 使用 wrapAll() 批量包装(注释掉,选择其一)
// ========================================
/*
$callbacks = CallbackWrapper::wrapAll($gb28181, [
    'handleRegister',
    'handleMessage',
    'handleInvite',
    'handleAck',
    'handleBye',
    'handleResponse',
    'handleError',
]);

$sipServer->on('register', $callbacks['handleRegister']);
$sipServer->on('message', $callbacks['handleMessage']);
$sipServer->on('invite', $callbacks['handleInvite']);
$sipServer->on('ack', $callbacks['handleAck']);
$sipServer->on('bye', $callbacks['handleBye']);
$sipServer->on('response', $callbacks['handleResponse']);
$sipServer->on('error', $callbacks['handleError']);
*/

// ========================================
// 方式3: 使用 safe() 内联包装
// ========================================
$sipServer->onTimer(function() use ($gb28181) {
    // 在回调内部使用 safe() 来执行可能出错的代码
    CallbackWrapper::safe(function() use ($gb28181) {
        $gb28181->checkDeviceTimeout();
        
        // 模拟可能的错误(测试用)
        // throw new Exception("Test error in timer");
    });
});

// 打印启动信息
echo "\n";
echo "=============================================\n";
echo "  GB28181 Server (Safe Callback Mode)\n";
echo "=============================================\n";
echo "  Server ID: {$sipId}\n";
echo "  Listening: {$host}:{$port} ({$protocol})\n";
echo "  Callback Protection: Enabled ✓\n";
echo "=============================================\n\n";

echo "[INFO] 服务器已启动(安全模式)，等待设备接入...\n\n";

// 启动服务
$sipServer->run();

echo "\n[Master] Server stopped\n";
