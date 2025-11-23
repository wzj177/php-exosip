#!/bin/bash

set -e

# Linux 编译脚本
PROJECT_DIR="$HOME/src/c-app/php-exosip"
LIB_OUTPUT_DIR="$PROJECT_DIR/libs"

# 检查 gcc
if ! command -v gcc &> /dev/null; then
    echo "❌ 未找到 GCC，请安装 GCC 或 Clang"
    exit 1
fi

CC="gcc"
CFLAGS_BASE="-O2 -fPIC"

mkdir -p "$LIB_OUTPUT_DIR"/{lib,include}

BUILD_DIR="./build_osip_src"
# rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# =============== 下载 ===============
if [ ! -d "osip2" ]; then
  echo "📥 下载 libosip2-5.3.0..."
  curl -# -L "https://www.antisip.com/download/exosip2/libosip2-5.3.0.tar.gz" | tar -xzf -
  mv libosip2-5.3.0 osip2
fi

if [ ! -d "eXosip2" ]; then
  echo "📥 下载 libexosip2-5.3.0..."
  curl -# -L "https://www.antisip.com/download/exosip2/libexosip2-5.3.0.tar.gz" | tar -xzf -
  mv libexosip2-5.3.0 eXosip2
fi

# =============== 编译 osip2 ===============
echo "🔧 编译 osip2 for Linux..."
cd osip2

./configure \
  CC="$CC" \
  CFLAGS="$CFLAGS_BASE" \
  --prefix="$LIB_OUTPUT_DIR" \
  --disable-shared \
  --enable-static \
  --disable-dependency-tracking \
  || { echo "❌ osip2 configure 失败"; exit 1; }

make clean
make -j$(nproc)
make install

cd ..

# =============== 编译 eXosip2 ===============
echo "🔧 编译 eXosip2 for Linux..."
cd eXosip2

./configure \
  CC="$CC" \
  CFLAGS="$CFLAGS_BASE -I$LIB_OUTPUT_DIR/include -DENABLE_MAIN_SOCKET" \
  LDFLAGS="-L$LIB_OUTPUT_DIR/lib" \
  --prefix="$LIB_OUTPUT_DIR" \
  --disable-shared \
  --enable-static \
  --disable-openssl \
  --disable-dependency-tracking \
  || { echo "❌ eXosip2 configure 失败"; exit 1; }

make clean
make -j$(nproc)
make install

cd ../..

echo "✅ 成功！输出目录：$LIB_OUTPUT_DIR"
echo "📂 头文件目录: $LIB_OUTPUT_DIR/include/"
echo "📚 库文件目录: $LIB_OUTPUT_DIR/lib/"

ls -lh "$LIB_OUTPUT_DIR/lib/"lib*.a
