#!/bin/bash
set -e

# Ubuntu/Debian 编译脚本 - 简化版

show_help() {
    echo "🔧 Ubuntu/Debian OSIP/eXOSIP 编译脚本"
    echo ""
    echo "用法: $0 [选项]"
    echo ""
    echo "选项:"
    echo "  --build-dir PATH    指定编译目录(默认: ./build_osip_src)"
    echo "  --output-dir PATH   指定库输出目录(默认: ./libs)"
    echo "  -h, --help          显示此帮助信息"
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

echo "🟢 Ubuntu/Debian OSIP/eXOSIP 编译脚本"
echo "============================================"
echo "📂 编译目录: $BUILD_DIR"
echo "📂 输出目录: $OUTPUT_DIR"

# ================= 修复点：依赖检测 =================
echo "🔍 检查系统依赖..."
MISSING_PACKAGES=()

for pkg in gcc make autoconf automake libtool pkg-config libc-ares-dev; do
    if ! dpkg -s "$pkg" >/dev/null 2>&1; then
        MISSING_PACKAGES+=("$pkg")
    fi
done

if [ ${#MISSING_PACKAGES[@]} -ne 0 ]; then
    echo "❌ 缺少必要的包: ${MISSING_PACKAGES[*]}"
    echo "请运行以下命令安装依赖:"
    echo "sudo apt update && sudo apt install -y ${MISSING_PACKAGES[*]}"
    exit 1
fi
# ==================================================

# 编译设置
CC="gcc"
CFLAGS_BASE="-O2 -fPIC"

mkdir -p "$OUTPUT_DIR"/{lib,include}
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# =============== 下载 ===============
OSIP_VER="5.3.0"
EXOSIP_VER="5.3.0"
VER_SHORT="$(echo "$OSIP_VER" | tr -d '.')"   # 5.3.0 → 530

if [ -d "osip2-${VER_SHORT}" ]; then
  OSIP_SRC="osip2-${VER_SHORT}"
  echo "📂 使用本地源目录: ${OSIP_SRC}"
elif [ -d "osip2" ]; then
  OSIP_SRC="osip2"
else
  echo "📥 下载 libosip2-${OSIP_VER}..."
  curl -# -L "https://www.antisip.com/download/exosip2/libosip2-${OSIP_VER}.tar.gz" | tar -xzf -
  mv "libosip2-${OSIP_VER}" osip2
  OSIP_SRC="osip2"
fi

if [ -d "eXosip2-${VER_SHORT}" ]; then
  EXOSIP_SRC="eXosip2-${VER_SHORT}"
  echo "📂 使用本地源目录: ${EXOSIP_SRC}"
elif [ -d "eXosip2" ]; then
  EXOSIP_SRC="eXosip2"
else
  echo "📥 下载 libexosip2-${EXOSIP_VER}..."
  curl -# -L "https://www.antisip.com/download/exosip2/libexosip2-${EXOSIP_VER}.tar.gz" | tar -xzf -
  mv "libexosip2-${EXOSIP_VER}" eXosip2
  EXOSIP_SRC="eXosip2"
fi

# =============== 编译 osip2 ===============
echo "🔧 编译 osip2 (${OSIP_SRC})..."
cd "${OSIP_SRC}"

./configure \
  CC="$CC" \
  CFLAGS="$CFLAGS_BASE" \
  --prefix="$OUTPUT_DIR" \
  --disable-shared \
  --enable-static \
  --enable-mt \
  --disable-dependency-tracking \
  || { echo "❌ osip2 configure 失败"; exit 1; }

make clean
make -j$(nproc)
make install

cd ..

# =============== 编译 eXosip2 ===============
echo "🔧 编译 eXosip2 (${EXOSIP_SRC})..."
cd "${EXOSIP_SRC}"

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
make -j$(nproc)
make install

cd ../..

echo ""
echo "✅ Ubuntu/Debian 编译成功!"
echo "📂 输出目录:$OUTPUT_DIR"
ls -lh "$OUTPUT_DIR/lib/"lib*.a 2>/dev/null || echo "没有找到静态库文件"