<?php
/**
 * GB28181 多进程架构示例
 * 
 * 架构说明：
 * - Master 进程：监控子进程，Worker 崩溃自动重启
 * - Worker 进程：处理 SIP 事件循环，非阻塞
 * - Task 进程池：处理耗时业务（HTTP、数据库、Redis）
 */

require_once __DIR__ . '/Device.php';
require_once __DIR__ . '/DeviceManager.php';

// 配置信息
$config = [
    'ip' => '0.0.0.0',
    'port' => 5060,
    'mode' => 'udp',
    'server_id' => '34020000002000000001',
    'domain' => '3402000000',
    'expires' => 3600,
    
    // 多进程配置
    'task_worker_num' => 4,  // Task 进程数量
];

// 创建 ExoSip 实例
$exosip = new ExoSip($config);
$deviceManager = new DeviceManager();

// Webhook 配置
$webhookUrl = getenv('WEBHOOK_URL') ?: 'http://localhost:8080/api/gb28181/events';

echo "[Config] SIP Server: {$config['ip']}:{$config['port']}\n";
echo "[Config] Task Workers: {$config['task_worker_num']}\n";
echo "[Config] Webhook: {$webhookUrl}\n";

// ==================== Task 进程回调 ====================

/**
 * onTask: 在 Task 进程中执行（支持协程）
 * 
 * @param int $taskId 任务ID
 * @param array $data 任务数据
 * @return mixed 返回结果
 */
$exosip->onTask = function($taskId, $data) use ($webhookUrl) {
    $type = $data['type'] ?? 'unknown';
    
    echo "[Task] Processing task #{$taskId}, type={$type}\n";
    
    switch ($type) {
        case 'webhook':
            // HTTP POST 推送
            return sendWebhook($webhookUrl, $data['payload']);
            
        case 'heartbeat_check':
            // 检查设备心跳超时（可查询数据库）
            return checkDeviceHeartbeat($data);
            
        case 'device_query':
            // 设备信息查询（可查询 Redis/MySQL）
            return queryDeviceInfo($data['device_id']);
            
        default:
            return ['success' => false, 'error' => 'Unknown task type'];
    }
};

/**
 * onTaskFinish: 在 Worker 进程中执行
 * 
 * @param int $taskId 任务ID
 * @param mixed $result Task 返回的结果
 */
$exosip->onTaskFinish = function($taskId, $result) {
    echo "[Worker] Task #{$taskId} finished: " . json_encode($result) . "\n";
    
    if (isset($result['success']) && !$result['success']) {
        echo "[Worker] Task #{$taskId} failed: {$result['error']}\n";
    }
};

// ==================== SIP 事件回调 ====================

/**
 * REGISTER 注册事件
 */
$exosip->onRegister = function($event) use ($exosip, $deviceManager) {
    $deviceId = $event->getFromUser();
    $fromUri = $event->getFromUri();
    
    echo "[Worker] REGISTER from {$deviceId}\n";
    
    // 1. 更新设备管理器（内存）
    $device = $deviceManager->getDevice($deviceId);
    if (!$device) {
        $device = new Device($deviceId, $fromUri);
        $deviceManager->addDevice($device);
    }
    $device->updateHeartbeat();
    
    // 2. 投递任务：推送注册事件到 Webhook
    $taskId = $exosip->addTask([
        'type' => 'webhook',
        'payload' => [
            'event' => 'register',
            'device_id' => $deviceId,
            'from_uri' => $fromUri,
            'timestamp' => time(),
        ]
    ]);
    
    echo "[Worker] Posted webhook task #{$taskId}\n";
    
    // 3. 回复 200 OK
    $exosip->replyRegister($event, 200, 3600);
};

/**
 * MESSAGE 消息事件（心跳、目录、状态等）
 */
$exosip->onMessage = function($event) use ($exosip, $deviceManager) {
    $deviceId = $event->getFromUser();
    $body = $event->getMessageBody();
    
    echo "[Worker] MESSAGE from {$deviceId}\n";
    
    // 更新心跳时间
    $device = $deviceManager->getDevice($deviceId);
    if ($device) {
        $device->updateHeartbeat();
    }
    
    // 解析 XML 消息
    $xml = @simplexml_load_string($body);
    if (!$xml) {
        echo "[Worker] Invalid XML body\n";
        return;
    }
    
    $cmdType = (string)$xml->CmdType;
    
    switch ($cmdType) {
        case 'Keepalive':
            // 心跳消息
            echo "[Worker] Keepalive from {$deviceId}\n";
            
            // 投递任务：更新数据库心跳时间
            $exosip->addTask([
                'type' => 'heartbeat_check',
                'device_id' => $deviceId,
                'timestamp' => time(),
            ]);
            break;
            
        case 'Catalog':
            // 设备目录
            $deviceList = [];
            if (isset($xml->DeviceList->Item)) {
                foreach ($xml->DeviceList->Item as $item) {
                    $deviceList[] = [
                        'device_id' => (string)$item->DeviceID,
                        'name' => (string)$item->Name,
                        'status' => (string)$item->Status,
                    ];
                }
            }
            
            echo "[Worker] Catalog: " . count($deviceList) . " devices\n";
            
            // 投递任务：保存目录到数据库
            $exosip->addTask([
                'type' => 'webhook',
                'payload' => [
                    'event' => 'catalog',
                    'device_id' => $deviceId,
                    'devices' => $deviceList,
                    'timestamp' => time(),
                ]
            ]);
            break;
            
        case 'DeviceStatus':
            // 设备状态
            $status = [
                'online' => (string)$xml->Online,
                'status' => (string)$xml->Status,
            ];
            
            echo "[Worker] DeviceStatus: online={$status['online']}\n";
            
            $exosip->addTask([
                'type' => 'webhook',
                'payload' => [
                    'event' => 'device_status',
                    'device_id' => $deviceId,
                    'status' => $status,
                    'timestamp' => time(),
                ]
            ]);
            break;
            
        default:
            echo "[Worker] Unknown CmdType: {$cmdType}\n";
    }
    
    // 回复 200 OK
    $exosip->replyMessage($event, 200);
};

/**
 * 定时器回调（每 30 秒）
 */
$exosip->onTimer = function() use ($deviceManager, $exosip) {
    static $tick = 0;
    $tick++;
    
    echo "[Worker] Timer tick #{$tick}\n";
    
    // 每 30 秒检查一次离线设备
    $offlineDevices = $deviceManager->getOfflineDevices(60); // 60秒超时
    
    if (!empty($offlineDevices)) {
        echo "[Worker] Found " . count($offlineDevices) . " offline devices\n";
        
        foreach ($offlineDevices as $device) {
            // 投递任务：推送离线事件
            $exosip->addTask([
                'type' => 'webhook',
                'payload' => [
                    'event' => 'device_offline',
                    'device_id' => $device->getDeviceId(),
                    'last_heartbeat' => $device->getLastHeartbeat(),
                    'timestamp' => time(),
                ]
            ]);
            
            // 从管理器中移除
            $deviceManager->removeDevice($device->getDeviceId());
        }
    }
    
    // 输出统计信息
    $stats = $deviceManager->getStats();
    echo "[Worker] Online devices: {$stats['total']}\n";
    
    return true; // 继续运行
};

// ==================== 辅助函数（在 Task 进程中执行） ====================

function sendWebhook($url, $payload) {
    $ch = curl_init();
    curl_setopt($ch, CURLOPT_URL, $url);
    curl_setopt($ch, CURLOPT_POST, true);
    curl_setopt($ch, CURLOPT_POSTFIELDS, json_encode($payload));
    curl_setopt($ch, CURLOPT_HTTPHEADER, [
        'Content-Type: application/json',
    ]);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_setopt($ch, CURLOPT_TIMEOUT, 5);
    
    $response = curl_exec($ch);
    $httpCode = curl_getinfo($ch, CURLINFO_HTTP_CODE);
    $error = curl_error($ch);
    curl_close($ch);
    
    if ($error) {
        return ['success' => false, 'error' => $error];
    }
    
    return [
        'success' => $httpCode >= 200 && $httpCode < 300,
        'http_code' => $httpCode,
        'response' => $response,
    ];
}

function checkDeviceHeartbeat($data) {
    // 这里可以更新 Redis/MySQL
    // 示例：简单返回成功
    return [
        'success' => true,
        'device_id' => $data['device_id'],
        'timestamp' => $data['timestamp'],
    ];
}

function queryDeviceInfo($deviceId) {
    // 这里可以查询数据库
    return [
        'success' => true,
        'device_id' => $deviceId,
        'info' => [
            'name' => 'Device ' . $deviceId,
            'status' => 'online',
        ],
    ];
}

// ==================== 启动服务 ====================

// 设置定时器（30 秒）
$exosip->setTimerInterval(30000); // 30秒

// 启动服务（Master 进程会 fork Worker + Task）
echo "\n";
echo "===========================================\n";
echo "  GB28181 Multi-Process Server Starting   \n";
echo "===========================================\n";
echo "  Architecture: Master + Worker + Task     \n";
echo "  SIP: {$config['ip']}:{$config['port']}  \n";
echo "  Task Workers: {$config['task_worker_num']}\n";
echo "===========================================\n";
echo "\n";

$exosip->run();

// Master 进程退出后会执行到这里
echo "[Master] Server stopped\n";
