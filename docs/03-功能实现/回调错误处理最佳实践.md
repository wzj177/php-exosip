# 回调错误处理最佳实践

## 问题背景

在使用 PHP ExoSip 扩展时,事件回调中的错误会被C层的 `zend_try/zend_catch` 捕获。这意味着:

1. **PHP层的 try-catch 无法捕获这些错误** - 错误已经在C层被拦截
2. **错误信息可能不够详细** - 只显示 "Unknown error" 或简单的错误消息
3. **错误堆栈可能丢失** - 难以定位问题所在

## 解决方案

### 方案1: 使用 CallbackWrapper 包装器(推荐)

我们提供了 `CallbackWrapper` 类来安全地执行回调,自动捕获异常并记录详细的错误日志。

#### 特性

- ✅ 自动捕获所有 `Throwable` 错误(包括 Error 和 Exception)
- ✅ 记录详细的错误堆栈
- ✅ 避免回调错误导致整个事件循环崩溃
- ✅ 支持多种使用方式

#### 使用方式

**方式1: 包装单个方法**

```php
require_once __DIR__ . '/protocol/CallbackWrapper.php';

$gb28181 = new GB28181Handler($sipServer, $config);

// 使用 wrap() 包装单个事件处理器
$sipServer->on('register', CallbackWrapper::wrap($gb28181, 'handleRegister'));
$sipServer->on('message', CallbackWrapper::wrap($gb28181, 'handleMessage'));
```

**方式2: 批量包装多个方法**

```php
// 使用 wrapAll() 批量包装
$callbacks = CallbackWrapper::wrapAll($gb28181, [
    'handleRegister',
    'handleMessage',
    'handleInvite',
    'handleAck',
    'handleBye',
    'handleResponse',
    'handleError',
]);

// 绑定回调
foreach ($callbacks as $event => $callback) {
    $eventName = str_replace('handle', '', $event);
    $eventName = strtolower($eventName);
    $sipServer->on($eventName, $callback);
}
```

**方式3: 内联包装**

```php
$sipServer->onTimer(function() use ($gb28181) {
    // 使用 safe() 执行可能出错的代码
    CallbackWrapper::safe(function() use ($gb28181) {
        $gb28181->checkDeviceTimeout();
        // 其他可能出错的操作...
    });
});
```

### 方案2: 手动 try-catch(不推荐)

虽然C层已经捕获了错误,但在PHP层添加 try-catch 仍然有意义:

```php
$sipServer->on('message', function($event) use ($gb28181) {
    try {
        $gb28181->handleMessage($event);
    } catch (\Throwable $e) {
        error_log("Message handler error: " . $e->getMessage());
        error_log($e->getTraceAsString());
    }
});
```

**缺点:**
- 需要在每个回调中手动添加
- 代码冗余,不够优雅
- 容易遗漏

## 错误日志输出

使用 `CallbackWrapper` 后,错误会输出详细信息:

```
[Callback Error] Error: Call to undefined method SipEvent::getMethod()
File: /path/to/GB28181Handler.php:713
Stack trace:
#0 /path/to/CallbackWrapper.php(23): GB28181Handler->handleResponse(Object(SipEvent))
#1 [internal function]: CallbackWrapper::{closure}(Object(SipEvent))
#2 /path/to/gb28181_server.php(65): ExoSip->run()
#3 {main}
```

## 完整示例

参见 `examples/gb28181_server_safe.php`

## 性能考虑

- `CallbackWrapper` 开销极小,仅在发生错误时才记录日志
- 建议在**生产环境**中使用,提高系统稳定性
- 开发环境可以选择不使用,直接暴露错误便于调试

## API 参考

### CallbackWrapper::safe()

```php
public static function safe(callable $callback, ...$args): mixed
```

安全执行回调函数,捕获异常并记录错误。

- **参数:**
  - `$callback` - 要执行的回调函数
  - `...$args` - 传递给回调的参数
- **返回:** 回调的返回值,发生错误时返回 `null`

### CallbackWrapper::wrap()

```php
public static function wrap(object $handler, string $method): callable
```

包装对象方法为安全的闭包。

- **参数:**
  - `$handler` - 事件处理器对象
  - `$method` - 方法名
- **返回:** 包装后的闭包

### CallbackWrapper::wrapAll()

```php
public static function wrapAll(object $handler, array $methods): array
```

批量包装多个方法。

- **参数:**
  - `$handler` - 事件处理器对象
  - `$methods` - 方法名数组
- **返回:** 包装后的回调数组

## 建议

1. **生产环境** - 强烈建议使用 `CallbackWrapper` 包装所有事件回调
2. **开发环境** - 可以选择直接绑定,快速暴露错误
3. **测试环境** - 建议使用包装器,验证错误处理的健壮性
4. **日志记录** - 配置好 `error_log` 路径,方便查看错误日志
