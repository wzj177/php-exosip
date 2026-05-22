#!/bin/bash
set -e

# Ubuntu/Debian 编译脚本 - 稳定检测版
# 用法: ./build_osip_ubuntu.sh [--build-dir PATH] [--output-dir PATH]

show_help() {
    echo "🔧 Ubuntu/Debian OSIP/eXOSIP 编译脚本"
    echo ""
    echo "用法: $0 [选项]"
    echo ""
    echo "选项:"
    echo "  --build-dir PATH    指定编译目录(默认: ./build_osip_src)"
    echo "  --output-dir PATH   指定库输出目录(默认: ./libs)"
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

echo "🟢 Ubuntu/Debian OSIP/eXOSIP 编译脚本"
echo "============================================"
echo "📂 编译目录: $BUILD_DIR"
echo "📂 输出目录: $OUTPUT_DIR"

# 检查依赖
echo "🔍 检查系统依赖..."
MISSING_PACKAGES=()

# 所有需要包名级验证的依赖
REQUIRED_PKGS=(gcc make autoconf automake libtool curl libc-ares-dev)

for pkg in "${REQUIRED_PKGS[@]}"; do
    # 使用 dpkg-query 检查安装状态，并移除底层架构名后缀(如 :amd64)的影响
    if ! dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null | grep -q "ok installed"; then
        # 针对 pkg-config / pkgconf 的平替策略
        if [ "$pkg" = "pkg-config" ]; then
            if dpkg-query -W -f='${Status}' "pkgconf" 2>/dev/null | grep -q "ok installed"; then
                continue
            fi
        fi
        MISSING_PACKAGES+=("$pkg")
    fi
done

# 单独对 pkg-config 补充加入队列（如果上一步循环里没算进去）
if ! dpkg-query -W -f='${Status}' "pkg-config" 2>/dev/null | grep -q "ok installed" && \
   ! dpkg-query -W -f='${Status}' "pkgconf" 2>/dev/null | grep -q "ok installed"; then
    MISSING_PACKAGES+=("pkg-config")
fi

if [ ${#MISSING_PACKAGES[@]} -ne 0 ]; then
    # 去重
    UNIQUE_MISSING=($(echo "${MISSING_PACKAGES[@]}" | tr ' ' '\n' | sort -u | tr '\n' ' '))
    echo "❌ 缺少必要的包: ${UNIQUE_MISSING[*]}"
    echo "请运行以下命令安装依赖:"
    echo "sudo apt update && sudo apt install -y ${UNIQUE_MISSING[*]}"
    exit 1
fi

# 编译设置
CC="gcc"
CFLAGS_BASE="-O2 -fPIC"

mkdir -p "$OUTPUT_DIR"/{lib,include}
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# =============== 下载 ===============
if [ ! -d "osip2" ]; then
  echo "📥 下载 libosip2-5.3.0..."
  curl -# -L "https://antisip.com" | tar -xzf -
  mv libosip2-5.3.0 osip2
fi

if [ ! -d "eXosip2" ]; then
  echo "📥 下载 libexosip2-5.3.0..."
  curl -# -L "https://antisip.com" | tar -xzf -
  mv libexosip2-5.3.0 eXosip2
fi

# =============== 编译 osip2 ===============
echo "🔧 编译 osip2..."
cd osip2

./configure \
  CC="$CC" \
  CFLAGS="$CFLAGS_BASE" \
  --prefix="$OUTPUT_DIR" \
  --disable-shared \
  --enable-static \
  --disable-dependency-tracking \
  || { echo "❌ osip2 configure 失败"; exit 1; }

make clean
make -j$(nproc)
make install

cd ..

# =============== 编译 eXosip2 ===============
echo "🔧 编译 eXosip2..."
cd eXosip2

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
echo "📂 输出目录: $OUTPUT_DIR"
ls -lh "$OUTPUT_DIR/lib/"lib*.a 2>/dev/null || echo "没有找到静态库文件"
