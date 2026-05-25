# PHP-eXosip Codebase Analysis
## Key Patterns & Conventions for AI Agent Productivity

---

## 1. CORE ARCHITECTURE: Master-Worker-Task Pattern

### Pattern Overview
```
Master (supervisor) 
  └─→ Worker (SIP event loop, runs continuously)
      └─→ Task Pool (async blocking operations, fork-isolated)
```

**Key Architecture Files:**
- [`docs/02-架构设计/Master-Worker-Task 多进程架构.md`](docs/02-架构设计/Master-Worker-Task%20多进程架构.md) - Full architecture explanation
- [`exosip_wrapper.c`](exosip_wrapper.c) - C layer process management & fork logic (lines 1-100 for init)
- [`php_exosip.c`](php_exosip.c) - PHP class definitions & Zend API bindings

### Critical Pattern #1: Process Boundaries & Communication
**Rule**: Worker owns SIP context; Tasks communicate via **pipe only** (not direct SIP calls)

```php
// ✅ SAFE: Worker → Task via addTask()
$sipServer->addTask(['action' => 'query_db', 'device_id' => '123']);

// ✅ SAFE: Task → Worker via sendToWorker() (pushes result)
$server->sendToWorker(['result' => $data]);

// ✅ SAFE: Worker receives Task push via onPipeMessage
$sipServer->onPipeMessage(function($server, $data) {
    // Handle Task result
});

// ❌ UNSAFE in Task: Direct SIP calls fail in fork-isolated process
// $server->sendMessage(...);  // Crashes - no eXosip context
// $server->sendResponse(...); // Crashes - no SIP state
```

### Critical Pattern #2: Callback Isolation & Exception Safety
**Rule**: Always wrap callbacks with `CallbackWrapper::wrap()` in production

```php
// ✅ PRODUCTION: Exceptions caught, prevents Worker crash
$sipServer->onRegister(CallbackWrapper::wrap(function($event) {
    // Any exception here is caught by wrapper
    throw new Exception("Device validation failed");
    // Worker continues running
}));

// ❌ RISKY: Unhandled exception crashes Worker process
$sipServer->onRegister(function($event) {
    throw new Exception("This will crash Worker!");
});
```

**Note**: `CallbackWrapper` implementation location: Not found in git (generated or in C layer), but usage is consistent across all examples in `examples/test_*.php`

### Key Configuration Parameters
```php
new ExoSip([
    'task_worker_num' => 4,      // Number of Task processes
    'timer_interval' => 30000,   // Timer callback interval (ms)
    'pid_file' => '/tmp/x.pid',  // For monitoring
    'mode' => 'udp|tcp',         // Transport mode
]);
```

---

## 2. BUILD SYSTEM: Platform-Specific Multi-Stage Pipeline

### Build Dependency Chain
```
osip-build/build_osip_<platform>.sh  (Stage 1: Compile osip libraries)
    ↓
build_<platform>_fix.sh (Stage 2: Compile extension, link statically)
    ↓
.libs/exosip.so (Final extension)
```

### Platform-Specific Scripts
| File | Platform | Key Notes |
|------|----------|-----------|
| `build_macos_fix.sh` | macOS (Apple Silicon/Intel) | Uses `libtool`, hardcoded PHP path `/opt/homebrew/Cellar/php@8.2/` |
| `build_centos_complete.sh` | CentOS 7+ | Manual gcc linking with `--whole-archive`, uses environment variables |
| `build_ubuntu.sh` | Ubuntu/Debian | Apt package management |

### Key Build Principles
**#1 Static Linking Required**: osip libraries (`libeXosip2.a`, `libosip2.a`, `libosipparser2.a`) are statically linked into extension

```bash
# osip-build/build_osip_macos.sh compiles to:
osip-build/libs/lib/*.a

# build_macos_fix.sh links with:
-L$LIB_DIR -leXosip2 -losip2 -losipparser2
```

**#2 Platform-Specific PHP Paths**: Each build script has hardcoded PHP include paths
- macOS: `/opt/homebrew/Cellar/php@8.2/8.2.28/include/php`
- CentOS: Uses `php-config --includes` or similar

**#3 libtool Complications**: macOS uses `libtool --mode=compile` for object compilation; CentOS bypasses libtool entirely with manual `gcc -shared`

### Build Checklist for Agents
1. **Before compiling extension**: Run osip-build first
   ```bash
   cd osip-build && bash build_osip_macos.sh && cd ..
   ```
2. **Check if .a files exist**:
   ```bash
   ls -la osip-build/libs/lib/libeXosip2.a
   ```
3. **Use correct platform build script** (macOS/CentOS/Ubuntu have different patterns)
4. **Verify PHP version** matches build script assumptions
5. **After compilation**: Check symbol resolution
   ```bash
   nm .libs/exosip.so | grep osip_free_func
   ```

---

## 3. TESTING PATTERNS: Example-Driven Validation

### Test File Organization
All tests are in `examples/test_*.php` (no PHPUnit framework - direct execution)

**Categories:**

| Pattern | Files | Purpose |
|---------|-------|---------|
| **Pipe Communication** | `test_pipe_message.php` | Worker↔Task async messaging |
| **TCP Mode** | `test_tcp_mode.php` | Device connection binding (fd↔device_id) |
| **Task Safety** | `test_task_server_safety.php` | Validate fork isolation & safe operations |
| **GB28181 Protocol** | `test_command_confirmed.php`, `test_gb28181_integration.php` | Full GB28181 workflows |
| **Integration** | `test_laravel_integration.php` | Redis queue + callback handling |
| **Client Mode** | `test_client.php` | Non-blocking client loop pattern |

### Test Execution Pattern
```bash
# Run directly (no framework)
php examples/test_pipe_message.php

# Expected output: Demonstrates Master-Worker-Task flow
# [Worker-12345] REGISTER from device: 34020000001320000001
# [Task-12346] Received task #1
# [Worker-12345] Task #1 finished
```

### Key Test Insights for Agents
1. **Tests are runnable examples**, not unit tests - run them to understand real behavior
2. **All production examples use CallbackWrapper::wrap()** - this is mandatory
3. **DeviceManager class** appears in multiple tests - centralized device state management
4. **Examples show both correct AND incorrect patterns** - `test_task_server_safety.php` explicitly shows what NOT to do

---

## 4. C/PHP INTEGRATION: Zend API Patterns

### Entry Points & Class Definitions

**File Structure:**
- [`php_exosip.c`](php_exosip.c): PHP class methods, Zend API bindings
- [`php_exosip.h`](php_exosip.h): Header file
- [`exosip_wrapper.h`](exosip_wrapper.h): C structures & function signatures  
- [`exosip_wrapper.c`](exosip_wrapper.c): Core C implementation (5000+ lines)

### Class Hierarchy
```
ExoSip (main class)
  ├─ Properties: onRegister, onMessage, onTask, onTimer, etc. (PHP callbacks)
  ├─ Methods: init(), run(), processEvents(), addTask(), sendMessage(), etc.
  └─ Static methods: getRunStatus() (process monitoring)

SipEvent (event object)
  ├─ Properties: type, tid, did, from, to, body
  └─ Methods: getFromUri(), getToUri(), getBody(), getTid(), etc.

ExoSipClient (client mode)
  ├─ Methods: start(), stop(), processEvents(), sendMessage(), sendRegister()
  └─ Use case: Non-blocking client loops (see test_client.php)
```

### Zend API Binding Pattern
```c
// In php_exosip.c:
const zend_function_entry exosip_methods[] = {
    ZEND_ME(ExoSip, __construct, arginfo_exosip_construct, ZEND_ACC_PUBLIC)
    ZEND_ME(ExoSip, run, arginfo_exosip_quit, ZEND_ACC_PUBLIC)
    ZEND_ME(ExoSip, processEvents, arginfo_exosip_processevents, ZEND_ACC_PUBLIC)
    ZEND_ME(ExoSip, addTask, arginfo_exosip_addtask, ZEND_ACC_PUBLIC)
    // ... more methods
    PHP_FE_END
};

// arginfo defines parameter types (for PHP 8 static analysis)
ZEND_BEGIN_ARG_INFO_EX(arginfo_exosip_init, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, config, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

// Implementation
ZEND_METHOD(ExoSip, run) {
    // Zend API to extract object
    zend_object *obj_ptr = Z_OBJ_P(getThis());
    
    // Cast to internal structure
    SipContext *ctx = (SipContext*)((char*)obj_ptr - obj_ptr->handlers->offset);
    
    // Business logic...
}
```

### Critical C-PHP Data Flow
1. **PHP callbacks stored as zval** → Zend engine calls them via `zend_call_function()`
2. **eXosip events** → Converted to PHP objects with SipEvent class entry
3. **Process fork** → PHP objects copied to Task process memory (full VM fork)
4. **Task results** → Serialized and sent via socketpair to Worker
5. **Worker receives** → Deserialized and passed to onTaskFinish callback

### Memory Management Rule
**CRITICAL**: Do NOT redefine osip allocators
```c
// ❌ WRONG - causes symbol conflicts
osip_set_allocators(my_malloc, my_realloc, my_free);

// ✅ CORRECT - use osip's defaults
// Leave osip's memory management alone
```

---

## 5. DOCUMENTATION: Well-Structured Reference

### Primary Documentation Structure

**Architecture Docs** (`docs/02-架构设计/`):
- `Master-Worker-Task 多进程架构.md` - Architecture overview + config
- `Socket Fork 架构设计文档.md` - fork() timing & memory model
- `Single-threaded + queue + event loop.md` - Alternative design (not recommended)

**Implementation Guides** (`docs/03-功能实现/`):
- `TCP 传输模式支持文档.md` - Device connection management for TCP mode
- `GB28181 信令操作完整指南.md` - All GB28181 commands & workflows
- `Task 进程中 $server 对象的安全性分析.md` - fork() process isolation details
- `GB28181 + ZLMediaKit 集成指南.md` - Media server integration

**Development Guide** (`docs/07-开发指南/README.md`):
- Index of all docs with cross-references
- Quick start for new contributors

### Key Documentation Insights
1. **All critical decisions documented** - fork timing, TCP connection binding, callback safety
2. **Examples are integration-focused** - GB28181, ZLMediaKit, Laravel, Redis
3. **Platform-specific notes scattered** - macOS TCP limitations mentioned in multiple places
4. **No auto-generated API docs** - arginfo in C code serves as documentation

---

## 6. COMMON PAIN POINTS & SOLUTIONS

### Pain Point #1: macOS TCP Mode Instability

**Problem**: TCP mode on macOS uses kqueue with known limitations

**Evidence**: README.md platform support table marks macOS TCP as "⚠️"

**Solution**:
```php
// ✅ macOS development: Use UDP
$sip = new ExoSip(['mode' => 'udp']);

// ✅ Linux production: Use TCP (more reliable)
$sip = new ExoSip(['mode' => 'tcp']);
```

### Pain Point #2: PHP Path Hardcoding in Build Scripts

**Problem**: Build scripts hardcode PHP paths; breaks if PHP installed elsewhere

**Current Path**: `/opt/homebrew/Cellar/php@8.2/8.2.28/include/php` (macOS)

**Solution**:
```bash
# Use php-config instead (more portable)
PHP_INCLUDE_DIR=$(php-config --include-dir)

# Or set environment variable before running build script
export PHP_CONFIG=/custom/path/php-config
bash build_macos_fix.sh
```

### Pain Point #3: Fork Timing & FD Inheritance

**Problem**: Socket must be created BEFORE fork, else child processes don't inherit connections

**Current Implementation**: ✅ Correct - socket created in `sip_init()` before fork

**Risk**: If someone refactors and moves socket creation after fork → complete failure

**How to Verify**: See `Socket Fork 架构设计文档.md` - documents exact timing

### Pain Point #4: Worker Crash from Exceptions

**Problem**: Unhandled exceptions in callbacks crash entire Worker process

**Example**:
```php
// ❌ CRASHES WORKER - unhandled exception
$sip->onMessage = function($event) {
    $data = json_decode($event->getBody()); // Can throw
    // If throws, Worker dies
};

// ✅ SAFE - exception caught by wrapper
$sip->onMessage = CallbackWrapper::wrap(function($event) {
    $data = json_decode($event->getBody());
    // If throws, caught by wrapper, Worker continues
});
```

### Pain Point #5: Missing osip Libraries During Build

**Problem**: Build fails silently if osip-build hasn't run

**Symptom**:
```
error: libeXosip2.a not found
Link fails with missing symbols: osip_malloc
```

**Solution**: Always verify pre-build
```bash
# Before running build_macos_fix.sh:
test -f osip-build/libs/lib/libeXosip2.a || \
  (cd osip-build && bash build_osip_macos.sh)
```

### Pain Point #6: TCP Mode Device Connection Binding

**Problem**: TCP mode requires manual fd↔device_id mapping; UDP doesn't

**Pattern** (see `test_tcp_mode.php`):
```php
// TCP Mode: MUST bind connection in onRegister
$sip->onRegister(function($event) {
    $fd = $event->getFd();  // Get file descriptor
    $deviceId = extractDeviceId($event->getFromUri());
    
    $deviceManager->bindConnection($deviceId, $fd);
    // Now can send responses to this device via fd
});

// Connection cleanup on disconnect
$sip->onClose(function($event) {
    $fd = $event->getFd();
    $deviceManager->unbindConnectionByFd($fd);
});
```

---

## 7. CRITICAL SETUP CHECKLIST FOR AGENTS

### Phase 1: Environment Check
- [ ] PHP version 8.2+ installed
- [ ] PHP development headers available
- [ ] `php-config` binary in PATH
- [ ] `phpize` command available
- [ ] Compiler (gcc/clang) available

### Phase 2: osip Build
```bash
cd osip-build
bash build_osip_macos.sh  # or build_osip_centos7.sh
cd ..
ls osip-build/libs/lib/libeXosip2.a  # Verify
```

### Phase 3: Extension Build
```bash
# Run correct platform script
bash build_macos_fix.sh      # macOS
bash build_centos_complete.sh # CentOS
```

### Phase 4: Verification
```bash
# Check extension loads
php -r "new ExoSip();"

# Check classes available
php -r "echo ExoSip::class, PHP_EOL;"
```

### Phase 5: Run Example Test
```bash
php examples/test_pipe_message.php
# Should output task processing + pipe communication
```

---

## 8. QUICK REFERENCE: Key File Locations

| Purpose | File | Lines |
|---------|------|-------|
| PHP class methods | [`php_exosip.c`](php_exosip.c) | 3474-3700+ |
| C implementation | [`exosip_wrapper.c`](exosip_wrapper.c) | 1-100 (init), 1500+ (callbacks) |
| Process management | [`exosip_wrapper.c`](exosip_wrapper.c) | ~2000+ (fork logic) |
| Data structures | [`exosip_wrapper.h`](exosip_wrapper.h) | Full header |
| GB28181 logic | [`examples/GB28181_2022_Methods.php`](examples/GB28181_2022_Methods.php) | Protocol spec |
| Master-Worker-Task | [`docs/02-架构设计/Master-Worker-Task 多进程架构.md`](docs/02-架构设计/Master-Worker-Task%20多进程架构.md) | Full spec |
| Callback safety | [`docs/03-功能实现/Task 进程中 $server 对象的安全性分析.md`](docs/03-功能实现/Task%20进程中%20$server%20对象的安全性分析.md) | Full analysis |

---

## Summary: 3 Key Productivity Insights

1. **Master-Worker-Task is NOT optional** - Process isolation prevents cascading failures; always use `addTask()` for blocking operations, never block in callbacks

2. **Build system is finicky on different platforms** - Run osip-build FIRST, verify .a files exist, use correct platform script; macOS TCP is broken, use UDP for development

3. **Always wrap callbacks with CallbackWrapper** - Unhandled exceptions crash Worker; production examples all use wrapper; this is a **mandatory safety pattern**
