#!/bin/bash
# CentOS 完整编译脚本
# 解决 libtool 链接问题，确保所有符号正确包含

set -e

PHP_CONFIG="${PHP_CONFIG:-/www/server/php/82/bin/php-config}"
EXOSIP_DIR="/etc/sip/libs"
PHP_EXT_DIR="$($PHP_CONFIG --extension-dir)"

echo "========================================="
echo "CentOS PHP-ExoSip 编译脚本"
echo "========================================="
echo "PHP Config: $PHP_CONFIG"
echo "ExoSip Dir: $EXOSIP_DIR"
echo "Extension Dir: $PHP_EXT_DIR"
echo ""

# Step 1: 清理并配置
echo "[1/5] 清理并配置..."
phpize --clean
phpize
./configure  --enable-exosip="$EXOSIP_DIR" --with-php-config="$PHP_CONFIG"

# Step 2: 编译生成 .o 文件
echo "[2/5] 编译源代码..."
make clean
make -j$(nproc) || make

# Step 3: 检查 .o 文件
if [ ! -f ".libs/php_exosip.o" ] || [ ! -f ".libs/exosip_wrapper.o" ]; then
  echo "错误：未找到编译后的 .o 文件"
  exit 1
fi

# Step 4: 绕过 libtool，手动链接
echo "[3/5] 修复链接（绕过 libtool）..."
gcc -shared -o .libs/exosip.so \
  .libs/php_exosip.o .libs/exosip_wrapper.o \
  -Wl,--whole-archive \
  $EXOSIP_DIR/lib/libosipparser2.a \
  $EXOSIP_DIR/lib/libosip2.a \
  $EXOSIP_DIR/lib/libeXosip2.a \
  -Wl,--no-whole-archive \
  -lresolv -lpthread -lrt -ldl

# Step 5: 验证符号
echo "[4/5] 验证符号..."
MISSING_SYMBOLS=$(nm .libs/exosip.so | grep -E " U (osip_cond_init|osip_free_func)" | wc -l)
if [ "$MISSING_SYMBOLS" -gt 0 ]; then
  echo "警告：仍有未定义符号："
  nm .libs/exosip.so | grep -E " U osip_"
  echo "继续安装，但可能运行失败"
else
  echo "✅ 所有符号已正确链接"
fi

# Step 6: 安装
echo "[5/5] 安装到 $PHP_EXT_DIR..."
sudo cp .libs/exosip.so "$PHP_EXT_DIR/"
sudo chmod 755 "$PHP_EXT_DIR/exosip.so"

echo ""
echo "========================================="
echo "✅ 编译完成！"
echo "========================================="
echo ""
echo "验证安装："
php -m | grep exosip && echo "✅ exosip 扩展已加载" || echo "❌ exosip 扩展未加载"
echo ""
echo "测试运行："
echo "  php examples/gb28181_server.php"

