#!/bin/bash

set -e

# CentOS/RHEL 编译脚本 - 简化版
# 用法: ./build_osip_centos.sh [--build-dir PATH] [--output-dir PATH]

show_help() {
    echo "🔧 CentOS/RHEL OSIP/eXOSIP 编译脚本"
    echo ""
    echo "用法: $0 [选项]"
    echo ""
    echo "选项:"
    echo "  --build-dir PATH    指定编译目录（默认: ./build_osip_src）"
    echo "  --output-dir PATH   指定库输出目录（默认: ./libs）"
    echo "  -h, --help          显示此帮助信息"
    echo ""
    echo "示例:"
    echo "  $0                              # 使用默认目录"
    echo "  $0 --build-dir /tmp/build       # 指定编译目录"
    echo "  $0 --output-dir ~/mylibs        # 指定输出目录"
}

# 默认值
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build_osip_src"
OUTPUT_DIR="$SCRIPT_DIR/libs"

# 解析参数
while [[ $# -gt 0 ]]; do
    case $1 in
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --output-dir)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        -h|--help)
            show_help
            exit 0
            ;;
        *)
            echo "❌ 未知选项: $1"
            show_help
            exit 1
            ;;
    esac
done

echo "🐧 CentOS/RHEL OSIP/eXOSIP 编译脚本"
echo "============================================"
echo "📂 编译目录: $BUILD_DIR"
echo "📂 输出目录: $OUTPUT_DIR"

# 检查依赖
echo "🔍 检查系统依赖..."
MISSING_PACKAGES=()

for pkg in gcc make autoconf automake libtool pkgconfig curl libcares-devel; do
    if ! rpm -q "$pkg" &> /dev/null; then
        MISSING_PACKAGES+=("$pkg")
    fi
done

if [ ${#MISSING_PACKAGES[@]} -ne 0 ]; then
    echo "❌ 缺少必要的包: ${MISSING_PACKAGES[*]}"
    echo "请运行以下命令安装依赖:"
    echo "sudo yum groupinstall -y \"Development Tools\""
    echo "sudo yum install -y autoconf automake libtool pkgconfig curl libcares-devel"
    exit 1
fi

# 验证 automake 版本
AUTOMAKE_VERSION=$(automake --version | head -n1 | grep -oE '[0-9]+\.[0-9]+')
echo "📋 检测到 automake 版本: $AUTOMAKE_VERSION"

# 编译设置
CC="gcc"
CFLAGS_BASE="-O2 -fPIC"

mkdir -p "$OUTPUT_DIR"/{lib,include}
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
echo "🔧 编译 osip2（CentOS 7 autotools 修复）..."
cd osip2

# 重新生成 autotools 文件（解决 CentOS 7 automake 版本问题）
echo "🔄 重新生成 autotools 文件..."
if [ -f "configure.ac" ] || [ -f "configure.in" ]; then
    rm -f aclocal.m4 configure Makefile.in config.h.in
    find . -name "Makefile.in" -delete

    autoreconf -fiv || {
        echo "⚠️  autoreconf 失败，尝试手动步骤..."
        aclocal || { echo "❌ aclocal 失败"; exit 1; }
        autoheader || echo "⚠️ autoheader 跳过"
        automake --add-missing --copy || echo "⚠️ automake 跳过"
        autoconf || { echo "❌ autoconf 失败"; exit 1; }
    }
fi

./configure \
  CC="$CC" \
  CFLAGS="$CFLAGS_BASE" \
  --prefix="$OUTPUT_DIR" \
  --disable-shared \
  --enable-static \
  --disable-dependency-tracking \
  || { echo "❌ osip2 configure 失败"; exit 1; }

make clean
make -j$(nproc) || make
make install

cd ..

# =============== 编译 eXosip2 ===============
echo "🔧 编译 eXosip2（CentOS 7 autotools 修复）..."
cd eXosip2

# 重新生成 autotools 文件
echo "🔄 重新生成 eXosip2 autotools 文件..."
if [ -f "configure.ac" ] || [ -f "configure.in" ]; then
    rm -f aclocal.m4 configure Makefile.in config.h.in
    find . -name "Makefile.in" -delete

    autoreconf -fiv || {
        echo "⚠️  autoreconf 失败，尝试手动步骤..."
        aclocal -I "$OUTPUT_DIR/share/aclocal" || { echo "❌ aclocal 失败"; exit 1; }
        autoheader || echo "⚠️ autoheader 跳过"
        automake --add-missing --copy || echo "⚠️ automake 跳过"
        autoconf || { echo "❌ autoconf 失败"; exit 1; }
    }
fi

export PKG_CONFIG_PATH="$OUTPUT_DIR/lib/pkgconfig:$PKG_CONFIG_PATH"

./configure \
  CC="$CC" \
  CFLAGS="$CFLAGS_BASE -I$OUTPUT_DIR/include -DENABLE_MAIN_SOCKET" \
  LDFLAGS="-L$OUTPUT_DIR/lib" \
  PKG_CONFIG_PATH="$OUTPUT_DIR/lib/pkgconfig" \
  --prefix="$OUTPUT_DIR" \
  --disable-shared \
  --enable-static \
  --disable-openssl \
  --disable-dependency-tracking \
  || { echo "❌ eXosip2 configure 失败"; exit 1; }

make clean
make -j$(nproc) || make
make install

cd ../..

echo ""
echo "✅ CentOS/RHEL 编译成功！"
echo "📂 输出目录：$OUTPUT_DIR"
ls -lh "$OUTPUT_DIR/lib/"lib*.a 2>/dev/null || echo "没有找到静态库文件"
