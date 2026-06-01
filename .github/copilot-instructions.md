# PHP-eXosip Extension - AI Coding Guidelines

## Project Overview

PHP extension wrapping eXosip2 (SIP protocol stack) for building GB28181 video surveillance systems and VoIP applications. Hybrid C extension + PHP architecture with production-grade multi-process support.

**Tech Stack**: C (eXosip2, osip2), PHP 8.2+, Master-Worker-Task architecture

## Architecture Essentials

### Master-Worker-Task Pattern (Critical)

```
Master (monitor) → Worker (SIP event loop, non-blocking) ⇄ Task Pool (async I/O)
                                                           ↓
                                                    sendToWorker() pushes
```

**Key Points**:
- Worker runs SIP event loop (`eXosip_event_wait()`) - NEVER block here
- Task processes handle HTTP/DB/Redis via `addTask()` → `onTask` callback
- Bidirectional pipe: Worker→Task (addTask), Task→Worker (`sendToWorker()`)
- Fork happens AFTER SIP socket creation - see [docs/02-架构设计/SOCKET_FORK_ARCHITECTURE.md](../docs/02-架构设计/SOCKET_FORK_ARCHITECTURE.md)

**Example**:
```php
$sip = new ExoSip(['task_worker_num' => 4, 'timer_interval' => 30000]);
$sip->onTask(function($server, $taskId, $data) {
    // Task process: blocking ops OK here
    $result = http_request($data['url']); 
    $server->sendToWorker(['type' => 'notify', 'result' => $result]); // Push to Worker
});
$sip->onPipeMessage(function($server, $data) {
    // Worker receives Task push
    echo "Got: {$data['type']}";
});
```

### TCP vs UDP Modes

- **UDP**: Cross-platform, default, no connection state
- **TCP**: Linux production (active/passive), requires device_id ↔ fd mapping in `onConnect`/`onClose`
- macOS TCP has kqueue limitations - prefer UDP for local dev
- See [docs/03-功能实现/TCP_MODE_SUPPORT.md](../docs/03-功能实现/TCP_MODE_SUPPORT.md)

### GB28181 Protocol

Chinese video surveillance standard built on SIP. Key flows:
- REGISTER → 401 (digest auth) → REGISTER with credentials
- MESSAGE for Catalog/DeviceInfo/Keepalive (XML body)
- INVITE for live/playback streams (SDP negotiation with ZLMediaKit)

## Build & Development Workflow

### Platform-Specific Builds

**macOS**:
```bash
cd osip-build && bash build_osip_macos.sh && cd ..
bash build_macos_fix.sh  # Handles static linking + symbol resolution
```

**CentOS**:
```bash
cd osip-build && bash build_osip_centos7.sh && cd ..
bash build_centos_complete.sh
```

**Critical**: osip libraries must be built BEFORE extension. Extension links statically against `osip-build/libs/lib/*.a`

### Testing Patterns

- `examples/test_pipe_message.php` - Task↔Worker communication
- `examples/test_tcp_mode.php` - TCP connection lifecycle (onConnect/onClose)
- `examples/test_task_server_safety.php` - Fork safety validation
- `examples/gb28181_server_safe.php` - Production GB28181 server with CallbackWrapper

**Run tests**: `php examples/<test_file>.php` (no PHPUnit - direct execution)

## Code Conventions

### C Extension Layer

- Use `CallbackWrapper::wrap()` in PHP callbacks to catch exceptions (prevents Worker crashes)
- Memory: osip lib manages allocations - do NOT redefine `osip_free_func`
- Thread safety: `pthread_mutex_lock(&ctx->lock)` for shared state in [exosip_wrapper.c](../exosip_wrapper.c)
- See [php_exosip.c](../php_exosip.c) for Zend API patterns (arginfo, class entries)

### PHP Layer Patterns

**Callbacks**:
```php
// Always wrap callbacks in production
$sip->onRegister = CallbackWrapper::wrap(function($event) use ($sip) {
    $deviceId = extractDeviceId($event->getFromUri());
    $sip->sendResponse($event->getTid(), 200);
});
```

**Task Safety**:
- `$server` in Task callbacks is fork-isolated - ONLY use `sendToWorker()`, DO NOT call `sendMessage()`/`sendInvite()`
- Worker owns SIP context - Task communicates via pipe only

**Client Mode**:
- Use `processEvents()` loop, NOT `start()` - background threads consume events
- See [examples/test_client.php](../examples/test_client.php)

## Critical File References

- [exosip_wrapper.c](../exosip_wrapper.c): Core C implementation (SIP init, callbacks, fork logic)
- [php_exosip.c](../php_exosip.c): PHP class definitions, method bindings
- [docs/02-架构设计/MASTER_WORKER_TASK.md](../docs/02-架构设计/MASTER_WORKER_TASK.md): Architecture deep dive
- [docs/03-功能实现/GB28181_COMMAND_GUIDE.md](../docs/03-功能实现/GB28181_COMMAND_GUIDE.md): All GB28181 commands
- [docs/04-待开发功能/C_EXTENSION_TODO.md](../docs/04-待开发功能/C_EXTENSION_TODO.md): Backlog (20-30 days)

## Common Pitfalls

1. **Blocking Worker**: Never do HTTP/DB in `onRegister`/`onMessage` - use `addTask()`
2. **Task SIP calls**: `$server->sendMessage()` in Task fails - use `sendToWorker()` + handle in Worker
3. **macOS TCP**: Known unstable - test with UDP on macOS, deploy TCP on Linux
4. **Symbol conflicts**: Static linking order matters - see [build_macos_fix.sh](../build_macos_fix.sh) line 83-88
5. **Fork timing**: Socket must exist BEFORE fork - changing this breaks fd inheritance

## Integration Points

- **ZLMediaKit**: Media server for GB28181 streams - see [docs/03-功能实现/GB28181_ZLM_INTEGRATION.md](../docs/03-功能实现/GB28181_ZLM_INTEGRATION.md)
- **Laravel**: Use Redis queue for async commands - [examples/test_laravel_integration.php](../examples/test_laravel_integration.php)
- **Workerman**: Task process compatible with Workerman timers/cron

## When Editing

- **Adding SIP methods**: Add arginfo in [php_exosip.c](../php_exosip.c), implement in [exosip_wrapper.c](../exosip_wrapper.c), update [examples/](../examples/)
- **New callbacks**: Register in `setup_callbacks()` ([exosip_wrapper.c:3200+](../exosip_wrapper.c)), expose via PHP property
- **Build changes**: Update BOTH `build_macos_fix.sh` and `build_centos_complete.sh`
- **Documentation**: Update [docs/README.md](../docs/README.md) index + specific topic in [docs/03-功能实现/](../docs/03-功能实现/)

## Quick Start for New Contributors

1. Build osip libs: `cd osip-build && bash build_osip_<platform>.sh`
2. Build extension: `bash build_<platform>_fix.sh`
3. Test basic: `php examples/test_pipe_message.php`
4. Read: [docs/07-开发指南/README.md](../docs/07-开发指南/README.md) + [docs/02-架构设计/MASTER_WORKER_TASK.md](../docs/02-架构设计/MASTER_WORKER_TASK.md)
