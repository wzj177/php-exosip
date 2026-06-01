#!/bin/bash

set -e

# 通用 Linux 编译脚本 - 支持自定义编译目录
# 用法: ./build_osip_linux.sh [--build-dir PATH] [--output-dir PATH]
# 默认: 当前目录 ./build_osip_src 或 ~/php-exosip-build/build_osip_src

show_help() {
    echo "🔧 通用 OSIP/eXOSIP 编译脚本"
    echo ""
    echo "用法: $0 [选项]"
    echo ""
    echo "选项:"
    echo "  --build-dir PATH    指定编译目录（默认: ./build_osip_src 或 ~/php-exosip-build/build_osip_src）"
    echo "  --output-dir PATH   指定库输出目录（默认: ./libs 或 ~/php-exosip-build/libs）"
    echo "  -h, --help          显示此帮助信息"
    echo ""
    echo "示例:"
    echo "  $0                              # 使用默认目录"
    echo "  $0 --build-dir /tmp/build       # 指定编译目录"
    echo "  $0 --output-dir ~/mylibs        # 指定输出目录"
}

# 默认值
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEFAULT_BUILD_DIR="$SCRIPT_DIR/build_osip_src"
DEFAULT_OUTPUT_DIR="$SCRIPT_DIR/libs"

# 解析参数
BUILD_DIR=""
OUTPUT_DIR=""

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

# 如果没有指定，使用智能默认值
if [ -z "$BUILD_DIR" ]; then
    # 检查当前目录是否可写
    if [ -w "$SCRIPT_DIR" ]; then
        BUILD_DIR="$DEFAULT_BUILD_DIR"
    else
        # 回退到 home 目录
        BUILD_DIR="$HOME/php-exosip-build/build_osip_src"
    fi
fi

if [ -z "$OUTPUT_DIR" ]; then
    # 检查当前目录是否可写
    if [ -w "$SCRIPT_DIR" ]; then
        OUTPUT_DIR="$DEFAULT_OUTPUT_DIR"
    else
        # 回退到 home 目录
        OUTPUT_DIR="$HOME/php-exosip-build/libs"
    fi
fi

echo "🐧 通用 Linux OSIP/eXOSIP 编译脚本"
echo "============================================"
echo "📂 编译目录: $BUILD_DIR"
echo "📂 输出目录: $OUTPUT_DIR"

# 检测系统类型
if [ -f /etc/os-release ]; then
    . /etc/os-release
    OS_ID=$ID
    OS_VERSION=$VERSION_ID
elif [ -f /etc/centos-release ]; then
    OS_ID="centos"
    OS_VERSION=$(rpm -q --queryformat '%{VERSION}' centos-release)
elif [ -f /etc/redhat-release ]; then
    OS_ID="rhel"
    OS_VERSION=$(rpm -q --queryformat '%{VERSION}' redhat-release)
else
    OS_ID="unknown"
    OS_VERSION="unknown"
fi

echo "🖥️  检测到系统: $OS_ID $OS_VERSION"

# 系统特定的依赖安装
install_dependencies_ubuntu() {
    echo "🔍 检查 Ubuntu/Debian 依赖..."

    MISSING_PACKAGES=()

    # 检查基本工具
    for pkg in gcc make autoconf automake libtool pkg-config curl; do
        if ! dpkg -l | grep -q "^ii  $pkg "; then
            MISSING_PACKAGES+=("$pkg")
        fi
    done

    # 检查 c-ares 开发包
    if ! dpkg -l | grep -q "^ii  libcares-dev "; then
        MISSING_PACKAGES+=("libc-ares-dev")
    fi

    if [ ${#MISSING_PACKAGES[@]} -ne 0 ]; then
        echo "❌ 缺少必要的包: ${MISSING_PACKAGES[*]}"
        echo "请运行以下命令安装依赖:"
        echo "sudo apt update"
        echo "sudo apt install -y build-essential autoconf automake libtool pkg-config curl libc-ares-dev"
        echo ""
        read -p "是否现在自动安装这些依赖? (y/N): " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            echo "🔧 正在安装依赖..."
            sudo apt update || { echo "❌ 更新软件源失败"; exit 1; }
            sudo apt install -y build-essential autoconf automake libtool pkg-config curl libc-ares-dev || { echo "❌ 安装依赖失败"; exit 1; }
            echo "✅ 依赖安装完成"
        else
            echo "❌ 请手动安装依赖后重新运行此脚本"
            exit 1
        fi
    fi
}

install_dependencies_centos() {
    echo "🔍 检查 CentOS/RHEL 依赖..."

    MISSING_PACKAGES=()

    # 检查基本工具
    for pkg in gcc make autoconf automake libtool pkgconfig curl; do
        if ! rpm -q "$pkg" &> /dev/null; then
            MISSING_PACKAGES+=("$pkg")
        fi
    done

    # 检查 c-ares 开发包
    if ! rpm -q libcares-devel &> /dev/null && ! [ -f /usr/include/ares.h ]; then
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

    # 验证 automake 版本（CentOS 7 特有问题）
    AUTOMAKE_VERSION=$(automake --version | head -n1 | grep -oE '[0-9]+\.[0-9]+')
    echo "📋 检测到 automake 版本: $AUTOMAKE_VERSION"
}

# 根据系统类型安装依赖
case "$OS_ID" in
    ubuntu|debian)
        install_dependencies_ubuntu
        ;;
    centos|rhel|fedora)
        install_dependencies_centos
        ;;
    *)
        echo "⚠️  未知系统类型，尝试使用通用依赖检查..."
        install_dependencies_centos  # 默认使用 CentOS 方式
        ;;
esac

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
echo "🔧 编译 osip2 for Linux..."
cd osip2

# CentOS 7/RHEL 7 需要重新生成 autotools 文件
if [[ "$OS_ID" == "centos" || "$OS_ID" == "rhel" ]] && [[ "$OS_VERSION" == "7"* ]]; then
    echo "🔄 重新生成 autotools 文件（CentOS 7 特殊处理）..."
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
echo "🔧 编译 eXosip2 for Linux..."
cd eXosip2

# CentOS 7/RHEL 7 需要重新生成 autotools 文件
if [[ "$OS_ID" == "centos" || "$OS_ID" == "rhel" ]] && [[ "$OS_VERSION" == "7"* ]]; then
    echo "🔄 重新生成 eXosip2 autotools 文件（CentOS 7 特殊处理）..."
    if [ -f "configure.ac" ] || [ -f "configure.in" ]; then
        # 清理旧的自动生成文件
        rm -f aclocal.m4 configure Makefile.in config.h.in
        find . -name "Makefile.in" -delete

        # 重新生成
        autoreconf -fiv || {
            echo "⚠️  autoreconf 失败，尝试手动步骤..."
            aclocal -I "$OUTPUT_DIR/share/aclocal" || { echo "❌ aclocal 失败"; exit 1; }
            autoheader || echo "⚠️ autoheader 跳过"
            automake --add-missing --copy || echo "⚠️ automake 跳过"
            autoconf || { echo "❌ autoconf 失败"; exit 1; }
        }
    fi
fi

# 设置 PKG_CONFIG_PATH 以找到 osip2
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
echo "✅ Linux 编译成功！"
echo "📂 输出目录：$OUTPUT_DIR"
echo "📂 头文件目录: $OUTPUT_DIR/include/"
echo "📚 库文件目录: $OUTPUT_DIR/lib/"
echo ""
echo "🔍 生成的库文件:"
ls -lh "$OUTPUT_DIR/lib/"lib*.a 2>/dev/null || echo "没有找到静态库文件"

echo ""
echo "📋 编译信息:"
echo "   - 系统: $OS_ID $OS_VERSION"
echo "   - GCC 版本: $(gcc --version | head -n1)"
echo "   - 编译器: $CC"
echo "   - 核心数: $(nproc)"
