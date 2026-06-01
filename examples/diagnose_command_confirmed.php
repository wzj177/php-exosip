#!/usr/bin/env php
<?php
/**
 * 快速诊断脚本 - 检查 command_confirmed 功能是否正常
 * 
 * 使用方法:
 * php examples/diagnose_command_confirmed.php
 */

echo "\n";
echo "==============================================\n";
echo "  command_confirmed 功能诊断工具\n";
echo "==============================================\n\n";

$baseDir = __DIR__;
$passed = 0;
$failed = 0;

// 测试1: 检查 C 扩展是否加载
echo "[1/6] 检查 PHP eXosip 扩展...\n";
if (extension_loaded('exosip')) {
    echo "  ✓ exosip 扩展已加载\n";
    $passed++;
} else {
    echo "  ✗ exosip 扩展未加载\n";
    echo "    运行: php -d extension=./modules/exosip.so -m | grep exosip\n";
    $failed++;
}

// 测试2: 检查 ExoSip 类是否可用
echo "\n[2/6] 检查 ExoSip 类...\n";
if (class_exists('ExoSip')) {
    echo "  ✓ ExoSip 类可用\n";
    $passed++;
    
    // 检查关键方法
    $methods = ['sendMessage', 'sendResponse', 'run', 'processEvents'];
    $missing = [];
    foreach ($methods as $method) {
        if (!method_exists('ExoSip', $method)) {
            $missing[] = $method;
        }
    }
    
    if (empty($missing)) {
        echo "  ✓ 所有核心方法存在\n";
    } else {
        echo "  ✗ 缺少方法: " . implode(', ', $missing) . "\n";
        $failed++;
    }
} else {
    echo "  ✗ ExoSip 类不存在\n";
    $failed++;
}

// 测试3: 检查 SipEvent 类
echo "\n[3/6] 检查 SipEvent 类...\n";
if (class_exists('SipEvent')) {
    echo "  ✓ SipEvent 类可用\n";
    $passed++;
    
    // 检查关键方法
    $methods = ['getCode', 'getCallId', 'getToUri', 'getHeader'];
    $missing = [];
    foreach ($methods as $method) {
        if (!method_exists('SipEvent', $method)) {
            $missing[] = $method;
        }
    }
    
    if (empty($missing)) {
        echo "  ✓ 所有 SipEvent 方法存在\n";
    } else {
        echo "  ✗ 缺少方法: " . implode(', ', $missing) . "\n";
        $failed++;
    }
} else {
    echo "  ✗ SipEvent 类不存在\n";
    $failed++;
}

// 测试4: 检查 GB28181Handler 类
echo "\n[4/6] 检查 GB28181Handler 类...\n";
$handlerPath = $baseDir . '/gbvr-iot/Gb28181Gateway/src/Handlers/GB28181Handler.php';
if (file_exists($handlerPath)) {
    echo "  ✓ GB28181Handler 文件存在\n";
    $content = file_get_contents($handlerPath);
    
    // 检查关键方法
    $methods = [
        'handleResponse' => '响应分发器',
        'handleMessageResponse' => 'MESSAGE 200 OK 处理',
        'handleInviteResponse' => 'INVITE 200 OK 处理',
    ];
    
    foreach ($methods as $method => $desc) {
        if (strpos($content, "function {$method}") !== false) {
            echo "  ✓ {$method}() 已定义 ({$desc})\n";
            $passed++;
        } else {
            echo "  ✗ {$method}() 未定义\n";
            $failed++;
        }
    }
} else {
    echo "  ✗ GB28181Handler 文件不存在: {$handlerPath}\n";
    $failed++;
}

// 测试5: 检查 Hook API Controller
echo "\n[5/6] 检查 Hook API Controller...\n";
$controllerPath = $baseDir . '/gbvr-iot/app/api/v2/controller/GBServerHookController.php';
if (file_exists($controllerPath)) {
    echo "  ✓ GBServerHookController 文件存在\n";
    $content = file_get_contents($controllerPath);
    
    // 检查 command_confirmed 场景
    if (strpos($content, "'command_confirmed'") !== false) {
        echo "  ✓ command_confirmed 场景已注册\n";
        $passed++;
    } else {
        echo "  ✗ command_confirmed 场景未注册\n";
        echo "    需要在 match(\$scene) 中添加:\n";
        echo "    'command_confirmed' => \$this->handleCommandConfirmed(\$body),\n";
        $failed++;
    }
    
    // 检查 handleCommandConfirmed 方法
    if (strpos($content, 'function handleCommandConfirmed') !== false) {
        echo "  ✓ handleCommandConfirmed() 方法已定义\n";
        $passed++;
    } else {
        echo "  ✗ handleCommandConfirmed() 方法未定义\n";
        $failed++;
    }
} else {
    echo "  ✗ GBServerHookController 文件不存在: {$controllerPath}\n";
    $failed++;
}

// 测试6: 检查 postTask 功能
echo "\n[6/6] 检查 postTask 功能...\n";
if (file_exists($handlerPath)) {
    $content = file_get_contents($handlerPath);
    
    if (strpos($content, 'postTask') !== false && 
        strpos($content, "'command_confirmed'") !== false) {
        echo "  ✓ postTask('command_confirmed') 调用存在\n";
        $passed++;
    } else {
        echo "  ✗ postTask('command_confirmed') 调用缺失\n";
        echo "    需要在 handleMessageResponse 中调用:\n";
        echo "    \$this->postTask('command_confirmed', [...]);\n";
        $failed++;
    }
} else {
    $failed++;
}

// 总结
echo "\n==============================================\n";
echo "  诊断结果\n";
echo "==============================================\n";
echo "通过: {$passed} 项\n";
echo "失败: {$failed} 项\n\n";

if ($failed === 0) {
    echo "✅ 所有检查通过！功能应该正常工作。\n\n";
    echo "下一步:\n";
    echo "1. 运行测试脚本: php examples/test_command_confirmed.php\n";
    echo "2. 使用真实设备测试 PTZ 控制\n";
    echo "3. 检查 Hook API 日志确认收到通知\n";
} else {
    echo "❌ 发现 {$failed} 个问题，请按照上述提示修复。\n\n";
}

echo "==============================================\n\n";
