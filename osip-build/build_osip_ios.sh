#!/bin/bash

set -e

PROJECT_DIR="/Users/jiechengyang/src/workerman-app/gb28181-openapi"
LIB_OUTPUT_DIR="$PROJECT_DIR/libs/exts"
IOS_MIN_VERSION="12.0"
IOS_SDK_VERSION="17.0"

XCODE_PATH=$(xcode-select -p)
SDK_PATH="$XCODE_PATH/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS${IOS_SDK_VERSION}.sdk"

if [ ! -d "$SDK_PATH" ]; then
  echo "❌ 未找到 iOS SDK: $SDK_PATH"
  echo "💡 运行 \`xcodebuild -showsdks\` 查看可用版本"
  exit 1
fi

CC="$XCODE_PATH/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
CFLAGS_BASE="-arch arm64 -isysroot $SDK_PATH -miphoneos-version-min=$IOS_MIN_VERSION"

mkdir -p "$LIB_OUTPUT_DIR"/{lib,include}

BUILD_DIR="./build_osip_ios"
#rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# =============== 下载（使用 antisip.com 官方地址） ===============
if [ ! -d "osip2" ]; then
  echo "📥 下载 libosip2-5.3.0..."
  curl -# -L "https://www.antisip.com/download/exosip2/libosip2-5.3.0.tar.gz" | tar -xzf -
  mv libosip2-5.3.0 osip2
fi

if [ ! -d "eXosip2" ]; then
  echo "📥 下载 libexosip2-5.3.0..."  # 注意：文件名全小写
  curl -# -L "https://www.antisip.com/download/exosip2/libexosip2-5.3.0.tar.gz" | tar -xzf -
  mv libexosip2-5.3.0 eXosip2        # 目录名也是小写
fi

# =============== 编译 osip2 ===============
echo "🔧 编译 osip2..."
cd osip2

./configure \
  CC="$CC" \
  CFLAGS="$CFLAGS_BASE" \
  --host=arm-apple-darwin \
  --prefix="$LIB_OUTPUT_DIR" \
  --disable-shared \
  --enable-static \
  --disable-dependency-tracking \
  || { echo "❌ osip2 configure 失败"; exit 1; }

make clean
make -j$(sysctl -n hw.ncpu)
make install

cd ..

# =============== 编译 eXosip2 ===============
echo "🔧 编译 eXosip2..."

cd eXosip2

./configure \
  CC="$CC" \
  CFLAGS="$CFLAGS_BASE -I$LIB_OUTPUT_DIR/include -DENABLE_MAIN_SOCKET" \
  LDFLAGS="-L$LIB_OUTPUT_DIR/lib -framework CFNetwork -framework CoreFoundation -framework MobileCoreServices" \
  --host=arm-apple-darwin \
  --prefix="$LIB_OUTPUT_DIR" \
  --disable-shared \
  --enable-static \
  --disable-openssl \
  --disable-dependency-tracking \
  || { echo "❌ eXosip2 configure 失败"; exit 1; }

make clean
make -j$(sysctl -n hw.ncpu)
make install

cd ../..

echo "✅ 成功！输出目录：$LIB_OUTPUT_DIR"
ls "$LIB_OUTPUT_DIR/include/"
ls "$LIB_OUTPUT_DIR/lib/"
lipo -info "$LIB_OUTPUT_DIR/lib/libeXosip2.a"
