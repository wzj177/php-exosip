# SipEvent/SipSession API 修复总结

## 问题描述

在 `GB28181Handler.php` 中发现了多处 API 使用错误：

```php
// ❌ 错误用法
$callId = $event->getCallId();  // SipEvent 没有 getCallId() 方法！
$dialogId = $event->getDialogId();  // SipEvent 没有 getDialogId() 方法！
```

**根本原因：**
- `getCallId()` 是 `SipSession` 类的方法，不是 `SipEvent` 的方法
- `SipEvent` 提供 `getSession()` 来获取关联的 `SipSession` 对象
- 需要先获取 Session，再调用 Session 的方法

## 修复方案

### 1. 正确的 API 使用方式

```php
// ✅ 正确用法
$session = $event->getSession();  // 先获取 SipSession 对象
if ($session) {
    $callId = $session->getCallId();     // int: eXosip call_id
    $fromUri = $session->getFromUri();   // string: From URI
    $toUri = $session->getToUri();       // string: To URI
    $state = $session->getState();       // int: Session state/type
    $body = $session->getRawBody();      // string: Raw SDP body
    
    // Close session (send BYE)
    $session->close();
}
```

### 2. 修复的文件

#### `GB28181Handler.php` - 三处修复

**修复 1: handleBye()**
```php
// Before
$callId = $event->getCallId() ?? '';

// After
$session = $event->getSession();
$callId = $session ? $session->getCallId() : 0;
```

**修复 2: handleResponse()**
```php
// Before
$callId = $event->getCallId() ?? '';
$dialogId = $event->getDialogId() ?? 0;

// After
$session = $event->getSession();
$callId = $session ? $session->getCallId() : 0;
```

**修复 3: handleInviteResponse()**
```php
// Before
$callId = $event->getCallId() ?? '';
$dialogId = $event->getDialogId() ?? 0;

// After
$session = $event->getSession();
$callId = $session ? $session->getCallId() : 0;
```

#### `exosip.stub.php` - API 文档更新

1. **移除不存在的方法：**
   - ❌ `SipSession::getConnectionId()` - 未实现
   - ❌ `SipSession::getDialogId()` - 未实现（C 代码中有 dialog_id 字段但未暴露）
   - ❌ `SipEvent::getCallId()` - 从未存在
   - ❌ `SipEvent::getDialogId()` - 从未存在

2. **更新方法文档：**
   - ✅ `SipEvent::getSession()` - 添加重要使用说明
   - ✅ `SipSession::getCallId()` - 明确返回 int，非 SIP Call-ID header
   - ✅ `SipSession::getState()` - 明确返回 int（内部 type 值），非状态字符串

## API 设计说明

### SipEvent 类（事件对象）

**职责：** 携带 SIP 消息的基本信息

**可用方法：**
```php
$event->getType(): int              // 事件类型（EXOSIP_CALL_INVITE 等）
$event->getCode(): int              // SIP 响应码（200, 404 等）
$event->getFromUri(): ?string       // From URI
$event->getToUri(): ?string         // To URI
$event->getRequestUri(): ?string    // Request-URI
$event->getBody(): ?string          // 消息体（XML/SDP）
$event->getContentType(): ?string   // Content-Type
$event->getTid(): int               // 事务 ID（用于 sendResponse）
$event->getExpires(): int           // Expires 值
$event->getSession(): ?SipSession   // ✨ 关联的 Session 对象
$event->getConnection(): ?array     // 连接信息（IP/Port）
$event->getHeader(string): ?string  // 获取指定 SIP 头
$event->getSdp(): ?array            // 解析 SDP（使用原生解析器）
```

### SipSession 类（会话对象）

**职责：** 管理 SIP 会话（dialog）的生命周期，类似 Workerman 的 TcpConnection

**可用方法：**
```php
$session->getId(): int              // 内部 Session ID
$session->getCallId(): int          // eXosip call_id（用于 BYE 等操作）
$session->getFromUri(): ?string     // Session 的 From URI
$session->getToUri(): ?string       // Session 的 To URI
$session->getState(): int           // Session state/type（内部值）
$session->getRawBody(): ?string     // 原始 SDP 消息体
$session->close(): bool             // ✨ 关闭会话（发送 BYE）
```

### 为什么这样设计？

1. **职责分离：**
   - `SipEvent` = 事件通知（Request/Response）
   - `SipSession` = 会话管理（Dialog 生命周期）

2. **生命周期不同：**
   - Event 对象：临时，回调结束后销毁
   - Session 对象：持久，可保存并在后续操作中使用

3. **类似 Workerman 设计：**
   ```php
   // Workerman
   $connection->id
   $connection->close()
   
   // ExoSip
   $session->getId()
   $session->close()
   ```

## C 代码实现细节

### SipSession 结构体（php_exosip.c）

```c
typedef struct _php_sip_session_obj {
    SessionInfo *session_info;  // 指向 exosip_wrapper.h 的 SessionInfo
    zend_object std;
} php_sip_session_obj;

// SessionInfo 包含（exosip_wrapper.h）：
typedef struct {
    int id;          // Session ID
    int call_id;     // eXosip call_id
    int dialog_id;   // eXosip dialog_id（未暴露到 PHP）
    int type;        // Session type（作为 getState() 返回值）
    char from_uri[256];
    char to_uri[256];
    char raw_body[8192];
} SessionInfo;
```

### SipEvent 结构体（php_exosip.c）

```c
typedef struct _php_sip_event_obj {
    int event_type;
    int event_code;
    int response_code;
    int tid;
    int expires;
    char *from_uri;
    char *to_uri;
    char *request_uri;
    char *body;
    char *content_type;
    php_sip_session_obj *session;  // 关联的 Session 对象（可能为 NULL）
    zval connection;               // 连接信息数组
    zval headers;                  // SIP 头字段数组
    zend_object std;
} php_sip_event_obj;
```

### 为什么没有 getDialogId()？

在 C 代码中 `SessionInfo` 包含 `dialog_id` 字段，但没有暴露到 PHP API：

```c
// php_exosip.c 中只实现了这些方法
const zend_function_entry sip_session_methods[] = {
    PHP_ME(SipSession, getId, ...)
    PHP_ME(SipSession, getCallId, ...)       // ✅ 已实现
    PHP_ME(SipSession, getFromUri, ...)
    PHP_ME(SipSession, getToUri, ...)
    PHP_ME(SipSession, getState, ...)
    PHP_ME(SipSession, getRawBody, ...)
    PHP_ME(SipSession, close, ...)
    PHP_FE_END
    // ❌ 没有 getDialogId
    // ❌ 没有 getConnectionId
};
```

**原因：** 目前 `close()` 方法已经自动处理 BYE 操作，不需要手动传递 dialog_id。

## 后续建议

### 1. 可选：添加 getDialogId() 方法

如果需要更细粒度的会话控制，可以在 `php_exosip.c` 中添加：

```c
PHP_METHOD(SipSession, getDialogId) {
    php_sip_session_obj *obj = php_sip_session_from_obj(Z_OBJ_P(getThis()));
    if (obj->session_info) {
        RETURN_LONG(obj->session_info->dialog_id);
    }
    RETURN_NULL();
}

// 然后在方法表中注册
const zend_function_entry sip_session_methods[] = {
    // ...
    PHP_ME(SipSession, getDialogId, arginfo_sipsession_getdialogid, ZEND_ACC_PUBLIC)
    // ...
};
```

### 2. 改进 getState() 返回值

当前 `getState()` 返回内部 `type` 值（int），可以改为返回字符串：

```c
PHP_METHOD(SipSession, getState) {
    php_sip_session_obj *obj = php_sip_session_from_obj(Z_OBJ_P(getThis()));
    if (obj->session_info) {
        switch (obj->session_info->type) {
            case 1: RETURN_STRING("invite");
            case 2: RETURN_STRING("subscribe");
            case 3: RETURN_STRING("refer");
            default: RETURN_STRING("unknown");
        }
    }
    RETURN_NULL();
}
```

### 3. 完善 stub 文件测试

使用 PHPStan 等工具验证 stub 文件的准确性：

```bash
composer require --dev phpstan/phpstan
phpstan analyse -l 8 examples/gb28181-gateway/
```

## 总结

- ✅ 修复了 3 处 API 使用错误
- ✅ 更新了 stub 文件，移除不存在的方法
- ✅ 明确了 SipEvent 和 SipSession 的职责边界
- ✅ 文档化了正确的 API 使用方式
- ⚠️ `getCallId()` 返回 int（eXosip internal ID），不是 SIP Call-ID header 字符串
- ⚠️ `getState()` 返回 int（type），不是 "active"/"idle" 等字符串

**关键要点：** 
Session 相关操作必须通过 `$event->getSession()` 获取 SipSession 对象后进行！
