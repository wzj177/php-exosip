# PHP ExoSip 扩展 Bug 修复总结

## 问题现象
服务器启动成功，GB28181设备注册成功并打印"设备注册成功"后，**立即出现 segmentation fault**。

## 根本原因分析

### 核心问题：对象生命周期管理错误

PHP 扩展中有 **3 处关键的内存管理错误**：

#### 错误 1: 回调函数中对象被过早释放
**位置**: `php_exosip.c` - `run()` 和 `runGBServer()` 方法

**问题代码**:
```c
// 创建事件对象
zval sip_event_obj;
object_init_ex(&sip_event_obj, sip_event_ce);

// 传递给 PHP 回调
zval result = php_exosip_call_event_handler(callback, &sip_event_obj);

// ❌ 立即释放对象，但 PHP 回调可能还在使用！
zval_ptr_dtor(&sip_event_obj);
```

**为什么会崩溃**:
1. `handleRegister()` 回调接收 `$event` 对象
2. 在回调中调用 `$event->getTid()` 等方法
3. 但在 C 层，对象已经被 `zval_ptr_dtor()` 释放
4. PHP 访问已释放的内存 → **segmentation fault**

**正确做法**:
```c
// 增加引用计数，让 PHP 回调可以安全使用
Z_TRY_ADDREF(sip_event_obj);

// 传递给 PHP 回调
zval result = php_exosip_call_event_handler(callback, &sip_event_obj);

// 释放 C 层的引用（但 PHP 层可能仍持有）
zval_ptr_dtor(&sip_event_obj);
```

#### 错误 2: processEvents() 中的引用计数泄漏
**位置**: `php_exosip.c` - `processEvents()` 方法

**问题代码**:
```c
ZEND_HASH_FOREACH_VAL(events_ht, event_data) {
    zval sip_event_obj;
    object_init_ex(&sip_event_obj, sip_event_ce);
    
    // ... 填充数据 ...
    
    add_next_index_zval(return_value, &sip_event_obj);
    
    // ❌ 缺少清理栈上的引用
    
} ZEND_HASH_FOREACH_END();
```

**为什么会泄漏**:
- `object_init_ex()` 创建对象，引用计数 = 1
- `add_next_index_zval()` 添加到数组，引用计数 = 2
- 循环继续，栈上的 `sip_event_obj` 被覆盖
- **栈上的引用永远不会被释放**

**正确做法**:
```c
add_next_index_zval(return_value, &sip_event_obj);

// ✅ 释放栈上的引用（数组仍持有对象）
zval_ptr_dtor(&sip_event_obj);
```

#### 错误 3: SipSession 对象创建方式错误（已修复）
这个在之前的修复中已解决，从栈上 `zval` 改为堆上 `zend_object*`。

---

## 修复内容

### 修复 1: run() 方法 (第1075行)
```c
if (callback) {
    // ✅ 增加引用计数
    Z_TRY_ADDREF(sip_event_obj);
    
    zval result = php_exosip_call_event_handler(callback, &sip_event_obj);
    // ...
}

// ✅ 安全释放
zval_ptr_dtor(&sip_event_obj);
```

### 修复 2: runGBServer() 方法 (第1486行)
```c
// ✅ 增加引用计数
Z_TRY_ADDREF(sip_event_obj);

zval retval;
zval params[1];
ZVAL_COPY_VALUE(&params[0], &sip_event_obj);

// ... 调用 PHP 函数 ...

// ✅ 安全释放
zval_ptr_dtor(&sip_event_obj);
```

### 修复 3: processEvents() 方法 (第980行)
```c
add_next_index_zval(return_value, &sip_event_obj);

// ✅ 释放栈上引用
zval_ptr_dtor(&sip_event_obj);
```

---

## 引用计数流程图

### 正确的引用计数管理

```
创建对象:
  object_init_ex(&obj)           → refcount = 1

传递给 PHP:
  Z_TRY_ADDREF(obj)              → refcount = 2
  call_php_function(&obj)        → PHP 使用对象
  
清理 C 引用:
  zval_ptr_dtor(&obj)            → refcount = 1
  
PHP 结束使用:
  (PHP 自动管理)                  → refcount = 0 → 对象销毁
```

---

## 如何验证修复

### 方法 1: 正常运行
```bash
# 清理端口
lsof -ti:15060 | xargs kill -9
sleep 1

# 运行服务器
php examples/gb28181_server.php
```

**预期结果**:
- ✅ 服务器启动成功
- ✅ 设备注册成功
- ✅ 打印"设备注册成功"
- ✅ **无 segmentation fault**
- ✅ 服务器持续运行

### 方法 2: 使用调试脚本
```bash
chmod +x run_debug.sh
./run_debug.sh
```

### 方法 3: 使用 lldb 调试（如果仍然崩溃）
```bash
chmod +x debug_server.sh
./debug_server.sh
```

这会在崩溃时自动显示堆栈信息。

---

## 调试工具说明

### 1. run_debug.sh
- 自动清理端口
- 启用 core dump
- 显示退出状态
- 提供下一步调试建议

### 2. debug_server.sh
- 使用 lldb 运行 PHP
- 崩溃时显示完整堆栈
- 显示所有线程状态
- 显示变量值

### 3. enable_coredump.sh
- 生成 core dump 文件
- 自动分析崩溃点
- 保存崩溃现场

---

## 技术要点总结

### PHP 扩展开发的黄金法则

1. **传递给 PHP 的对象必须增加引用计数**
   ```c
   Z_TRY_ADDREF(obj);  // 或 Z_ADDREF(obj)
   ```

2. **栈上的 zval 使用完必须清理**
   ```c
   zval_ptr_dtor(&obj);
   ```

3. **对象创建要用正确的方式**
   ```c
   // ✅ 正确：直接创建 zend_object
   zend_object *obj = create_object(ce);
   
   // ❌ 错误：栈上 zval 用于长期持有
   zval obj;
   object_init_ex(&obj, ce);
   some_struct->obj = &obj;  // ❌ 危险！
   ```

4. **理解引用计数语义**
   - `object_init_ex()`: refcount = 1
   - `add_next_index_zval()`: refcount +1
   - `Z_ADDREF()`: refcount +1
   - `zval_ptr_dtor()`: refcount -1
   - `zend_object_release()`: refcount -1

---

## 如果仍有问题

1. **运行调试脚本获取堆栈信息**:
   ```bash
   ./debug_server.sh
   ```

2. **检查引用计数**:
   在 `php_exosip.c` 中添加调试打印：
   ```c
   php_printf("DEBUG: refcount = %d\n", GC_REFCOUNT(&obj->std));
   ```

3. **使用 valgrind**（如果可用）:
   ```bash
   USE_ZEND_ALLOC=0 valgrind --leak-check=full php examples/gb28181_server.php
   ```

---

## 修改历史

- **2024-11-19**: 修复 run()/runGBServer() 回调对象生命周期问题
- **2024-11-19**: 修复 processEvents() 引用计数泄漏
- **2024-11-19**: 修复 SipSession 对象创建方式

---

## 结论

这次修复解决了 **PHP 扩展中最常见的内存管理错误**：

1. ❌ 对象传递给 PHP 后立即释放
2. ❌ 栈上引用没有清理导致泄漏
3. ❌ 对象生命周期管理混乱

这些都是 PHP 扩展开发中的经典陷阱。现在的代码遵循了正确的引用计数管理模式，应该不会再出现 segmentation fault。

