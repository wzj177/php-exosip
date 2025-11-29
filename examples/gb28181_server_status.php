#!/usr/bin/env php
<?php
/**
 * GB28181 服务器状态查询脚本
 * 
 * 用法：
 *   php gb28181_server_status.php
 *   php gb28181_server_status.php /tmp/gb28181_server.pid
 */

$pidFile = $argv[1] ?? '/tmp/gb28181_server.pid';

// 检查 PID 文件是否存在
if (!file_exists($pidFile)) {
    echo "错误: PID 文件不存在: {$pidFile}\n";
    echo "提示: 服务器可能未运行，或 PID 文件路径不正确\n";
    exit(1);
}

// 读取 PID
$masterPid = (int)trim(file_get_contents($pidFile));
if ($masterPid <= 0) {
    echo "错误: 无效的 PID: {$masterPid}\n";
    exit(1);
}

// 检查 Master 进程是否存在
if (!posix_kill($masterPid, 0)) {
    echo "错误: Master 进程不存在 (PID: {$masterPid})\n";
    echo "提示: 服务器可能已停止，但 PID 文件未删除\n";
    exit(1);
}

// 获取进程状态
try {
    $status = ExoSip::getRunStatus($pidFile);
    
    if (!$status) {
        echo "错误: 无法获取进程状态\n";
        exit(1);
    }
    
    // 打印状态信息
    echo "\n";
    echo "=============================================\n";
    echo "  GB28181 Server Status\n";
    echo "=============================================\n";
    echo "  PID File: {$pidFile}\n";
    echo "\n";
    
    // Master 进程
    if (isset($status['master'])) {
        $master = $status['master'];
        echo "  [Master Process]\n";
        echo "    PID:        {$master['pid']}\n";
        echo "    Status:     {$master['status']}\n";
        
        if (isset($master['memory_rss_kb'])) {
            $mem_mb = round($master['memory_rss_kb'] / 1024, 2);
            echo "    Memory:     {$mem_mb} MB\n";
        }
        
        if (isset($master['fd_count'])) {
            echo "    FD Count:   {$master['fd_count']}\n";
        }
        echo "\n";
    }
    
    // Worker 进程
    if (isset($status['worker'])) {
        $worker = $status['worker'];
        echo "  [Worker Process]\n";
        echo "    PID:           {$worker['pid']}\n";
        echo "    Status:        {$worker['status']}\n";
        
        if (isset($worker['memory_rss_kb'])) {
            $mem_mb = round($worker['memory_rss_kb'] / 1024, 2);
            echo "    Memory:        {$mem_mb} MB\n";
        }
        
        if (isset($worker['fd_count'])) {
            echo "    FD Count:      {$worker['fd_count']}\n";
        }
        
        if (isset($worker['uptime'])) {
            $uptime = $worker['uptime'];
            $hours = floor($uptime / 3600);
            $minutes = floor(($uptime % 3600) / 60);
            $seconds = $uptime % 60;
            echo "    Uptime:        {$hours}h {$minutes}m {$seconds}s\n";
        }
        
        if (isset($worker['restart_count'])) {
            echo "    Restart Count: {$worker['restart_count']}\n";
        }
        echo "\n";
    }
    
    // Task 进程池
    if (isset($status['tasks']) && is_array($status['tasks'])) {
        echo "  [Task Worker Pool]\n";
        echo "    Total: " . count($status['tasks']) . " workers\n";
        echo "\n";
        
        foreach ($status['tasks'] as $task) {
            $taskId = $task['id'];
            $taskPid = $task['pid'];
            $taskStatus = $task['status'];
            
            $statusIcon = $taskStatus === 'running' ? '✓' : '✗';
            $memInfo = '';
            if (isset($task['memory_rss_kb'])) {
                $mem_mb = round($task['memory_rss_kb'] / 1024, 2);
                $memInfo = " ({$mem_mb} MB)";
            }
            
            echo "    Task-{$taskId}: PID {$taskPid} [{$statusIcon} {$taskStatus}]{$memInfo}\n";
        }
        echo "\n";
    }
    
    // Long Task 进程池
    if (isset($status['long_tasks']) && is_array($status['long_tasks'])) {
        echo "  [Long Task Worker Pool]\n";
        echo "    Total: " . count($status['long_tasks']) . " workers\n";
        echo "\n";
        
        foreach ($status['long_tasks'] as $task) {
            $taskId = $task['id'];
            $taskPid = $task['pid'];
            $taskStatus = $task['status'];
            
            $statusIcon = $taskStatus === 'running' ? '✓' : '✗';
            $memInfo = '';
            if (isset($task['memory_rss_kb'])) {
                $mem_mb = round($task['memory_rss_kb'] / 1024, 2);
                $memInfo = " ({$mem_mb} MB)";
            }
            
            echo "    LongTask-{$taskId}: PID {$taskPid} [{$statusIcon} {$taskStatus}]{$memInfo}\n";
        }
        echo "\n";
    }
    
    // 任务统计
    if (isset($status['tasks_posted']) || isset($status['tasks_failed'])) {
        echo "  [Task Statistics]\n";
        if (isset($status['tasks_posted'])) {
            echo "    Posted: {$status['tasks_posted']}\n";
        }
        if (isset($status['tasks_failed'])) {
            echo "    Failed: {$status['tasks_failed']}\n";
        }
        echo "\n";
    }
    
    echo "=============================================\n";
    echo "\n";
    
    // 返回状态码
    $allRunning = true;
    if (isset($status['master']) && $status['master']['status'] !== 'running') {
        $allRunning = false;
    }
    if (isset($status['worker']) && $status['worker']['status'] !== 'running') {
        $allRunning = false;
    }
    if (isset($status['tasks'])) {
        foreach ($status['tasks'] as $task) {
            if ($task['status'] !== 'running') {
                $allRunning = false;
                break;
            }
        }
    }
    
    exit($allRunning ? 0 : 1);
    
} catch (Exception $e) {
    echo "错误: " . $e->getMessage() . "\n";
    exit(1);
}
