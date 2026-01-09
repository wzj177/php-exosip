# CentOS 编译说明

## 问题根源

### undefined symbol: osip_cond_init / osip_free_func

**原因**：libtool 在处理静态库链接时，未正确应用 `-Wl,--whole-archive` 标志，导致部分符号未被包含。

**症状**：
```
php: symbol lookup error: exosip.so: undefined symbol: osip_cond_init
```

**解决**：绕过 libtool，直接用 gcc 链接。

---

## 快速编译（推荐）

```bash
cd /path/to/php-exosip
bash build_centos_complete.sh
```

该脚本会：
1. 运行 phpize 和 configure
2. 编译生成 `.o` 文件
3. 用 gcc 直接链接（绕过 libtool）
4. 验证符号
5. 安装到 PHP 扩展目录

---

## 手动编译步骤

### 1. 配置
```bash
phpize --clean
phpize
./configure \
  --with-php-config=/www/server/php/82/bin/php-config \
  --with-exosip=/etc/sip/libs
```

### 2. 编译（生成 .o）
```bash
make clean
make -j$(nproc)
```

### 3. 修复链接
```bash
gcc -shared -o .libs/exosip.so \
  .libs/php_exosip.o .libs/exosip_wrapper.o \
  -Wl,--whole-archive \
  /etc/sip/libs/lib/libosipparser2.a \
  /etc/sip/libs/lib/libosip2.a \
  /etc/sip/libs/lib/libeXosip2.a \
  -Wl,--no-whole-archive \
  -lresolv -lpthread -lrt -ldl
```

### 4. 验证
```bash
nm .libs/exosip.so | grep osip_cond_init
# 应显示：0000000000xxxxxx T osip_cond_init
# 如果显示 U，说明链接失败
```

### 5. 安装
```bash
sudo cp .libs/exosip.so /www/server/php/82/lib/php/extensions/no-debug-non-zts-20220829/
php -m | grep exosip
```

---

## 技术细节

### 为什么需要 --whole-archive？

静态库链接的默认行为：
- 链接器只提取"当前需要"的符号
- 如果 A 依赖 B，B 依赖 C，必须按 C → B → A 顺序
- 否则后面的符号无法反向引用

我们的依赖链：
```
exosip.so → libeXosip2.a → libosip2.a → libosipparser2.a
                  ↓
            osip_cond_init (在 libosip2.a 中定义)
```

`--whole-archive` 强制包含所有符号，避免顺序问题。

### 为什么 libtool 会失败？

libtool 的行为：
1. 重新排序链接参数
2. 过滤某些"不安全"的链接器标志
3. 使用内部逻辑处理 `.a` 文件
4. 可能丢弃 `-Wl,--whole-archive`

结果：即使在 `config.m4` 中设置了 `EXTRA_LDFLAGS`，最终 gcc 命令中没有 `--whole-archive`。

### 手动 gcc 为什么成功？

完全控制链接参数：
```bash
gcc -shared \
  <对象文件> \
  -Wl,--whole-archive <静态库按顺序> -Wl,--no-whole-archive \
  <系统库>
```

链接器严格按此顺序处理，所有符号都被包含。

---

## 不同平台对比

| 平台 | exosip 版本 | 编译方式 | 问题 |
|------|------------|---------|------|
| macOS | 5.2.0 | 标准 Makefile | ✅ 无问题 |
| Linux (CentOS) | 5.3.0 | **需要 gcc 手动链接** | ❌ libtool 符号丢失 |

---

## 环境变量

可通过环境变量自定义路径：

```bash
PHP_CONFIG=/path/to/php-config \
EXOSIP_DIR=/path/to/exosip/libs \
bash build_centos_complete.sh
```

---

## 验证安装

```bash
# 检查扩展加载
php -m | grep exosip

# 检查符号
nm $(php-config --extension-dir)/exosip.so | grep osip_cond_init

# 运行测试
php examples/gb28181_server.php
```

