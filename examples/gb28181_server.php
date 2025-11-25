<?php
/**
 * GB28181 视频监控服务器示例
 * 
 * 演示如何使用 PHP ExoSip 扩展和 GB28181 协议类构建视频监控服务器
 */

require_once __DIR__ . '/protocol/GB28181Handler.php';
require_once __DIR__ . '/protocol/CallbackWrapper.php';
require_once __DIR__ . '/DeviceManager.php';

// 初始化SIP服务器
$host = '0.0.0.0';
$port = 15060;
$protocol = 'udp'; // 支持 upd\tcp\all[upd+tcp]
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
    
    // Master-Worker-Task 多进程配置
    'task_worker_num' => 1,
    
    // 定时器配置（单位：毫秒）
    'timer_interval' => 30000,  // 30秒检查一次设备超时
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
// 绑定GB28181事件处理器
$gb28181->bindEvents();

// 定时器：检查设备心跳超时

// 打印启动信息
echo "\n";
echo "=============================================\n";
echo "  GB28181 Multi-Process Video Server\n";
echo "=============================================\n";
echo "  Architecture: Master + Worker + Task\n";
echo "  Server ID: {$sipId}\n";
echo "  Domain: {$sipRealm}\n";
echo "  Listening: {$host}:{$port} ({$protocol})\n";
echo "  Task Workers: 4\n";
echo "  Timer Interval: 30s\n";
echo "=============================================\n\n";

echo "[INFO] 服务器已启动，等待设备接入...\n\n";

// 启动多进程服务
$sipServer->run();

// Master 进程退出
echo "\n[Master] Server stopped\n";
