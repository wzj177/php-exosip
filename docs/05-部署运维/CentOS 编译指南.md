# CentOS 编译指南

## 问题总结

### 根本原因
libtool 在处理 `-Wl,--whole-archive` 时会重新排序或过滤链接参数，导致静态库符号未完全包含进 `exosip.so`。

### 症状
```
php: symbol lookup error: exosip.so: undefined symbol: osip_cond_init
```

### 解决方案
绕过 libtool 最后的链接步骤，使用 gcc 直接链接。

---

## 编译步骤（CentOS/Linux）

### 1. 编译 osip 库
```bash
cd /etc/sip/osip-build
bash build_osip_centos7.sh
```

### 2. 编译 PHP 扩展（生成 .o 文件）
```bash
cd /etc/php-exosip
phpize --clean
phpize
./configure --with-php-config=/www/server/php/82/bin/php-config --with-exosip=/etc/sip/libs
make clean
make -j$(nproc)
```

### 3. 修复链接（绕过 libtool）
```bash
bash link_fix.sh
```

### 4. 安装
```bash
sudo cp .libs/exosip.so /www/server/php/82/lib/php/extensions/no-debug-non-zts-20220829/
php -m | grep exosip
```

### 5. 测试
```bash
php examples/gb28181_server.php
```

---

## 技术细节

### 为什么手动 gcc 链接有效？

1. **完全控制参数顺序**
   ```bash
   gcc -shared -o exosip.so \
     .libs/*.o \                        # PHP 扩展对象文件
     -Wl,--whole-archive \              # 开始全量包含
       libosipparser2.a \               # 顺序1
       libosip2.a \                     # 顺序2（提供 osip_cond_init）
       libeXosip2.a \                   # 顺序3（依赖 osip_cond_init）
     -Wl,--no-whole-archive \           # 结束全量包含
     -lresolv -lpthread -lrt -ldl      # 系统库
   ```

2. **`--whole-archive` 强制包含所有符号**
   - 默认：链接器只提取当前需要的符号
   - 问题：循环依赖时，后面的符号无法反向引用
   - 解决：全量包含，避免顺序问题

3. **libtool 的干扰**
   - 重新排序参数
   - 过滤某些链接器标志
   - 使用内部链接逻辑

---

## 一键编译脚本

创建 `build_centos_all.sh`：
```bash
#!/bin/bash
set -e

echo "1. 编译 osip 库..."
cd /etc/sip/osip-build && bash build_osip_centos7.sh

echo "2. 编译 PHP 扩展..."
cd /etc/php-exosip
phpize --clean && phpize
./configure --with-php-config=/www/server/php/82/bin/php-config --with-exosip=/etc/sip/libs
make clean && make -j$(nproc)

echo "3. 修复链接..."
bash link_fix.sh

echo "4. 安装..."
sudo cp .libs/exosip.so /www/server/php/82/lib/php/extensions/no-debug-non-zts-20220829/

echo "✅ 完成！"
php -m | grep exosip
```

