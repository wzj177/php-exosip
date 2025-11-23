# 测试工具

## C 测试程序

用于独立测试 eXosip2 库的基础功能，不依赖 PHP 扩展。

### 编译

```bash
make -f Makefile.test
```

### UDP 测试

```bash
./test_exosip_udp
```

### TCP 测试

```bash
./test_exosip_tcp
```

### 测试客户端

```bash
# UDP 测试
echo "TEST" | nc -u localhost 5060

# TCP 测试
echo "TEST" | nc localhost 5060
```

## 说明

- `test_exosip_tcp.c` - TCP 模式测试（Linux 推荐）
- `test_exosip_udp.c` - UDP 模式测试（跨平台）
- `Makefile.test` - 测试程序编译脚本

## 平台限制

- **Linux**: TCP/UDP 均支持
- **macOS**: UDP 稳定，TCP 受限（kqueue 兼容性）
- **Windows**: 仅 UDP

