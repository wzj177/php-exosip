#!/bin/bash

set -e

# macOS 编译脚本
PROJECT_DIR="/Users/jiechengyang/src/c-app/php-exosip/osip-build"
LIB_OUTPUT_DIR="$PROJECT_DIR/libs"

# 检查Xcode命令行工具
if ! command -v cc &> /dev/null; then
    echo "❌ 未找到C编译器，请安装 Xcode Command Line Tools"
    echo "💡 运行: xcode-select --install"
    exit 1
fi

CC="cc"
CFLAGS_BASE="-arch arm64 -arch x86_64 -mmacosx-version-min=10.15 -fPIC -fvisibility=default"

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
echo "🔧 编译 osip2 for macOS..."
cd osip2

./configure \
  CC="$CC" \
  CFLAGS="$CFLAGS_BASE" \
  --prefix="$LIB_OUTPUT_DIR" \
  --disable-shared \
  --enable-static \
  --disable-dependency-tracking \
  || { echo "❌ osip2 configure 失败"; exit 1; }
# ./configure \
#   CC="cc" \
#   CFLAGS="-arch arm64 -O2 -fPIC" \
#   --prefix="$LIB_OUTPUT_DIR" \
#   --disable-shared \
#   --enable-static \
#   --disable-dependency-tracking \
#   || { echo "❌ osip2 configure 失败"; exit 1; }
make clean
make -j$(sysctl -n hw.ncpu)
make install

cd ..

# =============== 编译 eXosip2 ===============
echo "🔧 编译 eXosip2 for macOS..."

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
# ./configure \
#   CC="cc" \
#   CFLAGS="-arch arm64 -O2 -fPIC -I$LIB_OUTPUT_DIR/include" \
#   LDFLAGS="-L$LIB_OUTPUT_DIR/lib" \
#   --prefix="$LIB_OUTPUT_DIR" \
#   --disable-shared \
#   --enable-static \
#   --disable-openssl \
#   --disable-dependency-tracking \
#   || { echo "❌ eXosip2 configure 失败"; exit 1; }

make clean
make -j$(sysctl -n hw.ncpu)
make install

cd ../..

echo "✅ 成功！输出目录：$LIB_OUTPUT_DIR"
echo "📂 头文件目录: $LIB_OUTPUT_DIR/include/"
echo "📚 库文件目录: $LIB_OUTPUT_DIR/lib/"

ls -lh "$LIB_OUTPUT_DIR/lib/"lib*.a

echo ""
echo "🔍 检查库架构:"
lipo -info "$LIB_OUTPUT_DIR/lib/libeXosip2.a"
lipo -info "$LIB_OUTPUT_DIR/lib/libosip2.a"
lipo -info "$LIB_OUTPUT_DIR/lib/libosipparser2.a"

echo ""
echo "🔍 检查符号可见性:"
nm -g "$LIB_OUTPUT_DIR/lib/libosipparser2.a" | grep osip_free_func || echo "⚠️  osip_free_func 未找到"

echo ""
echo "✅ 现在可以运行: cd .. && phpize && ./configure --enable-exosip=\"\$PWD/osip-build\" && make clean && make"
