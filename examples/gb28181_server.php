<?php
/**
 * GB28181 视频监控服务器示例
 * 
 * 演示如何使用 PHP ExoSip 扩展和 GB28181 协议类构建视频监控服务器
 */

require_once __DIR__ . '/protocol/GB28181Handler.php';
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
    'debug' => true
]);

// 创建GB28181协议处理器
$gb28181 = new GB28181Handler($sipServer, [
    'server_id' => $sipId,
    'server_domain' => $sipRealm,
    'heartbeat_timeout' => 180,
    'register_expires' => 3600,
    'catalog_auto_query' => false,
]);
// 绑定GB28181事件处理器
$gb28181->bindEvents();

// 打印启动信息
echo "=================================\n";
echo "  GB28181 Video Server\n";
echo "=================================\n";
echo "Server ID: {$sipId}\n";
echo "Domain: {$sipRealm}\n";
echo "Listening on: {$host}:{$port}\n";
echo "Transport: {$protocol}\n";
echo "=================================\n\n";

echo "[INFO] 服务器已启动，等待设备接入...\n\n";

// 使用事件驱动架构 - 阻塞式运行
$sipServer->run();
