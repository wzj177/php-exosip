#!/bin/bash

set -e

cd "$(dirname "$0")"

echo "=== macOS PHP eXosip 扩展编译修复版 ==="

LIB_DIR="$PWD/osip-build/libs/lib"

echo "0. 确保静态库存在..."
if [[ ! -f "$LIB_DIR/libeXosip2.a" ]]; then
    echo "重新编译 osip 库..."
    cd osip-build && bash build_osip_macos.sh && cd ..
fi

echo "1. 运行 phpize 和 configure..."
phpize --clean
phpize
./configure --enable-exosip="$PWD/osip-build"

echo ""
echo "2. 编译生成 .o 文件..."
make clean 2>/dev/null || true

if [[ ! -f "$LIB_DIR/libeXosip2.a" ]]; then
    echo "make clean 删除了库文件，重新编译..."
    cd osip-build && bash build_osip_macos.sh && cd ..
fi

/bin/sh ./libtool --tag=CC --mode=compile cc \
  -I. -I. -I./include -I./main -I. \
  -I/opt/homebrew/Cellar/php@8.2/8.2.28/include/php \
  -I/opt/homebrew/Cellar/php@8.2/8.2.28/include/php/main \
  -I/opt/homebrew/Cellar/php@8.2/8.2.28/include/php/TSRM \
  -I/opt/homebrew/Cellar/php@8.2/8.2.28/include/php/Zend \
  -I/opt/homebrew/Cellar/php@8.2/8.2.28/include/php/ext \
  -I/opt/homebrew/Cellar/php@8.2/8.2.28/include/php/ext/date/lib \
  -I$PWD/osip-build/libs/include \
  -I$PWD/osip-build/libs/include/eXosip2 \
  -I$PWD/osip-build/libs/include/osip2 \
  -I$PWD/osip-build/libs/include/osipparser2 \
  -DHAVE_CONFIG_H -g -O2 -D_GNU_SOURCE -DZEND_COMPILE_DL_EXT=1 \
  -c php_exosip.c -o php_exosip.lo

/bin/sh ./libtool --tag=CC --mode=compile cc \
  -I. -I. -I./include -I./main -I. \
  -I/opt/homebrew/Cellar/php@8.2/8.2.28/include/php \
  -I/opt/homebrew/Cellar/php@8.2/8.2.28/include/php/main \
  -I/opt/homebrew/Cellar/php@8.2/8.2.28/include/php/TSRM \
  -I/opt/homebrew/Cellar/php@8.2/8.2.28/include/php/Zend \
  -I/opt/homebrew/Cellar/php@8.2/8.2.28/include/php/ext \
  -I/opt/homebrew/Cellar/php@8.2/8.2.28/include/php/ext/date/lib \
  -I$PWD/osip-build/libs/include \
  -I$PWD/osip-build/libs/include/eXosip2 \
  -I$PWD/osip-build/libs/include/osip2 \
  -I$PWD/osip-build/libs/include/osipparser2 \
  -DHAVE_CONFIG_H -g -O2 -D_GNU_SOURCE -DZEND_COMPILE_DL_EXT=1 \
  -c exosip_wrapper.c -o exosip_wrapper.lo

if [[ ! -f ".libs/php_exosip.o" ]] || [[ ! -f ".libs/exosip_wrapper.o" ]]; then
    echo "❌ .o 文件生成失败"
    exit 1
fi

echo ""
echo "3. 手动链接（绕过 libtool）..."
cc -bundle -o modules/exosip.so \
  .libs/php_exosip.o \
  .libs/exosip_wrapper.o \
  "$LIB_DIR/libosipparser2.a" \
  "$LIB_DIR/libosip2.a" \
  "$LIB_DIR/libeXosip2.a" \
  -framework CFNetwork \
  -framework CoreFoundation \
  -framework SystemConfiguration \
  -lresolv \
  -undefined dynamic_lookup

if [[ ! -f "modules/exosip.so" ]]; then
    echo "❌ 链接失败"
    exit 1
fi

echo ""
echo "4. 检查符号..."
echo "检查 osip_free_func:"
nm -g modules/exosip.so | grep osip_free_func | head -3

echo ""
echo "5. 测试加载..."
php -d extension=./modules/exosip.so -m | grep -i exosip && echo "✅ 扩展加载成功" || echo "❌ 扩展加载失败"

echo ""
echo "✅ 编译完成: modules/exosip.so"

make install
echo "✅ 安装完成"

rm -f /opt/homebrew/lib/php/pecl/20220829/exosip.so && cp  /opt/homebrew/Cellar/php@8.2/8.2.28/pecl/20220829/exosip.so /opt/homebrew/lib/php/pecl/20220829 && php -m | grep -i exosip