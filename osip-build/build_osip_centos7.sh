#!/bin/bash

set -e

# CentOS 7 编译脚本 - 解决 automake/autoconf 依赖问题
PROJECT_DIR="$HOME/src/c-app/php-exosip"
LIB_OUTPUT_DIR="$PROJECT_DIR/libs"

echo "🐧 CentOS 7 OSIP/eXOSIP 编译脚本"
echo "============================================"

# 检查系统
if [ ! -f /etc/centos-release ]; then
    echo "⚠️  此脚本专为 CentOS 7 设计"
fi

# 检查并安装必要的依赖
echo "🔍 检查系统依赖..."

# 检查基本工具
MISSING_PACKAGES=()

if ! command -v gcc &> /dev/null; then
    MISSING_PACKAGES+=("gcc")
fi

if ! command -v make &> /dev/null; then
    MISSING_PACKAGES+=("make")
fi

if ! command -v autoconf &> /dev/null; then
    MISSING_PACKAGES+=("autoconf")
fi

if ! command -v automake &> /dev/null; then
    MISSING_PACKAGES+=("automake")
fi

if ! command -v libtool &> /dev/null; then
    MISSING_PACKAGES+=("libtool")
fi

if ! command -v pkg-config &> /dev/null; then
    MISSING_PACKAGES+=("pkgconfig")
fi

# 检查 c-ares 开发包
if ! pkg-config --exists libcares 2>/dev/null && ! [ -f /usr/include/ares.h ]; then
    MISSING_PACKAGES+=("libcares-devel")
fi

if [ ${#MISSING_PACKAGES[@]} -ne 0 ]; then
    echo "❌ 缺少必要的包: ${MISSING_PACKAGES[*]}"
    echo "请运行以下命令安装依赖:"
    echo "sudo yum groupinstall -y \"Development Tools\""
    echo "sudo yum install -y autoconf automake libtool pkgconfig curl libcares-devel"
    echo ""
    read -p "是否现在自动安装这些依赖? (y/N): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        echo "🔧 正在安装依赖..."
        sudo yum groupinstall -y "Development Tools" || { echo "❌ 安装开发工具失败"; exit 1; }
        sudo yum install -y autoconf automake libtool pkgconfig curl libcares-devel || { echo "❌ 安装依赖失败"; exit 1; }
        echo "✅ 依赖安装完成"
    else
        echo "❌ 请手动安装依赖后重新运行此脚本"
        exit 1
    fi
fi

# 验证 automake 版本
AUTOMAKE_VERSION=$(automake --version | head -n1 | grep -oE '[0-9]+\.[0-9]+')
echo "📋 检测到 automake 版本: $AUTOMAKE_VERSION"

CC="gcc"
CFLAGS_BASE="-O2 -fPIC"

mkdir -p "$LIB_OUTPUT_DIR"/{lib,include}

BUILD_DIR="./build_osip_src"
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
echo "🔧 编译 osip2 for CentOS 7..."
cd osip2

# 重新生成 configure 脚本以解决版本不匹配问题
echo "🔄 重新生成 autotools 文件..."
if [ -f "configure.ac" ] || [ -f "configure.in" ]; then
    # 清理旧的自动生成文件
    rm -f aclocal.m4 configure Makefile.in config.h.in
    find . -name "Makefile.in" -delete
    
    # 重新生成
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
  --prefix="$LIB_OUTPUT_DIR" \
  --disable-shared \
  --enable-static \
  --disable-dependency-tracking \
  || { echo "❌ osip2 configure 失败"; exit 1; }

make clean
make -j$(nproc) || make
make install

cd ..

# =============== 编译 eXosip2 ===============
echo "🔧 编译 eXosip2 for CentOS 7..."
cd eXosip2

# 重新生成 configure 脚本
echo "🔄 重新生成 eXosip2 autotools 文件..."
if [ -f "configure.ac" ] || [ -f "configure.in" ]; then
    # 清理旧的自动生成文件
    rm -f aclocal.m4 configure Makefile.in config.h.in
    find . -name "Makefile.in" -delete
    
    # 重新生成
    autoreconf -fiv || { 
        echo "⚠️  autoreconf 失败，尝试手动步骤..."
        aclocal -I "$LIB_OUTPUT_DIR/share/aclocal" || { echo "❌ aclocal 失败"; exit 1; }
        autoheader || echo "⚠️ autoheader 跳过"
        automake --add-missing --copy || echo "⚠️ automake 跳过"
        autoconf || { echo "❌ autoconf 失败"; exit 1; }
    }
fi

# 设置 PKG_CONFIG_PATH 以找到 osip2
export PKG_CONFIG_PATH="$LIB_OUTPUT_DIR/lib/pkgconfig:$PKG_CONFIG_PATH"

./configure \
  CC="$CC" \
  CFLAGS="$CFLAGS_BASE -I$LIB_OUTPUT_DIR/include -DENABLE_MAIN_SOCKET" \
  LDFLAGS="-L$LIB_OUTPUT_DIR/lib" \
  PKG_CONFIG_PATH="$LIB_OUTPUT_DIR/lib/pkgconfig" \
  --prefix="$LIB_OUTPUT_DIR" \
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
echo "✅ CentOS 7 编译成功！"
echo "📂 输出目录：$LIB_OUTPUT_DIR"
echo "📂 头文件目录: $LIB_OUTPUT_DIR/include/"
echo "📚 库文件目录: $LIB_OUTPUT_DIR/lib/"
echo ""
echo "🔍 生成的库文件:"
ls -lh "$LIB_OUTPUT_DIR/lib/"lib*.a 2>/dev/null || echo "没有找到静态库文件"

echo ""
echo "📋 编译信息:"
echo "   - GCC 版本: $(gcc --version | head -n1)"
echo "   - Automake 版本: $(automake --version | head -n1)"
echo "   - Autoconf 版本: $(autoconf --version | head -n1)"