<?php
/**
 * GB28181 视频监控服务器示例（使用内置定时器）
 * 
 * 演示如何使用 C 层定时器实现心跳检测和设备清理
 */

require_once __DIR__ . '/protocol/GB28181Handler.php';

// 加载配置
$sipConfig = require __DIR__ . '/sip_config.php';

// 初始化SIP服务器（设置定时器间隔）
$sipServer = new ExoSip(array_merge($sipConfig, [
    'timerInterval' => $sipConfig['check_interval'] * 1000,  // 转换为毫秒
]));

// 创建GB28181协议处理器
$gb28181 = new GB28181Handler($sipServer, $sipConfig);

// 绑定GB28181事件处理器
$gb28181->bindEvents();

// 设置定时器回调（用于心跳检测和设备清理）
$sipServer->onTimer = function() use ($gb28181) {
    // 调用 GB28181Handler 的 tick() 方法
    $gb28181->tick();
};

// 打印启动信息
echo "=================================\n";
echo "  GB28181 Video Server (Timer)\n";
echo "=================================\n";
echo "Server ID: {$sipConfig['server_id']}\n";
echo "Domain: {$sipConfig['server_domain']}\n";
echo "Listening on: {$sipConfig['listen_addr']}:{$sipConfig['sip_port']}\n";
echo "Transport: {$sipConfig['transport']}\n";
echo "Timer Interval: {$sipConfig['check_interval']}s\n";
echo "=================================\n\n";

echo "[INFO] 服务器已启动，等待设备接入...\n";
echo "[INFO] 使用 C 层定时器处理心跳检测\n\n";

// 使用事件驱动架构 - 阻塞式运行（内置定时器）
$sipServer->run();
