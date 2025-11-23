#!/bin/bash
# 修复版本：确保符号可见性

set -e

PROJECT_DIR="/etc/sip"
LIB_OUTPUT_DIR="$PROJECT_DIR/libs"
BUILD_DIR="./build_osip_src"

cd "$PROJECT_DIR/osip-build"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# 编译 osip2 - 添加符号可见性选项
if [ -d "osip2" ]; then
  cd osip2
  make clean 2>/dev/null || true
  
  ./configure \
    CC="gcc" \
    CFLAGS="-O2 -fPIC -fvisibility=default" \
    --prefix="$LIB_OUTPUT_DIR" \
    --disable-shared \
    --enable-static \
    --disable-dependency-tracking
  
  make -j$(nproc) || make
  make install
  cd ..
fi

# 编译 eXosip2
if [ -d "eXosip2" ]; then
  cd eXosip2
  make clean 2>/dev/null || true
  
  export PKG_CONFIG_PATH="$LIB_OUTPUT_DIR/lib/pkgconfig:$PKG_CONFIG_PATH"
  
  ./configure \
    CC="gcc" \
    CFLAGS="-O2 -fPIC -I$LIB_OUTPUT_DIR/include -DENABLE_MAIN_SOCKET -fvisibility=default" \
    LDFLAGS="-L$LIB_OUTPUT_DIR/lib" \
    PKG_CONFIG_PATH="$LIB_OUTPUT_DIR/lib/pkgconfig" \
    --prefix="$LIB_OUTPUT_DIR" \
    --disable-shared \
    --enable-static \
    --disable-openssl \
    --disable-dependency-tracking
  
  make -j$(nproc) || make
  make install
  cd ..
fi

echo "✅ 重新编译完成，检查符号："
nm "$LIB_OUTPUT_DIR/lib/libosipparser2.a" | grep "osip_free_func" | grep -v " U "

