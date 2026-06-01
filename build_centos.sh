#!/bin/bash
# php-exosip 统一编译脚本 - CentOS/RHEL/Rocky/AlmaLinux
# 支持 PHP 7.x 和 PHP 8.x，灵活参数配置
# 包含 libtool 绕过链接 + autoreconf 版本兼容处理
#
# 用法示例：
#   ./build_centos.sh --php-version=8.2
#   ./build_centos.sh --php-config=/www/server/php/82/bin/php-config
#   ./build_centos.sh --php-version=7.4 --exosip-dir=/opt/sip/libs
#   ./build_centos.sh --php-version=8.1 --rebuild-libs --jobs=8
#   ./build_centos.sh --php-version=8.2 --no-install-deps

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OSIP_VERSION="5.3.0"
EXOSIP_VERSION="5.3.0"

# ============================================================
# 默认参数
# ============================================================
PHP_CONFIG=""
PHP_VERSION=""
EXOSIP_DIR="${SCRIPT_DIR}/osip-build/libs"
REBUILD_LIBS=false
INSTALL_DIR=""
JOBS=$(nproc 2>/dev/null || echo 4)
INSTALL_DEPS=true

# ============================================================
# 帮助信息
# ============================================================
usage() {
    cat <<EOF
用法: $(basename "$0") [选项]

选项:
  --php-version=X.Y        PHP 版本（如 7.4, 8.0, 8.1, 8.2, 8.3）
  --php-config=/path       php-config 路径（优先于 --php-version）
  --exosip-dir=/path       预编译 osip/exosip 库目录（跳过库编译）
  --rebuild-libs           强制重新编译 osip/exosip 库
  --install-dir=/path      PHP 扩展安装目录（默认 php-config --extension-dir）
  --jobs=N                 并行编译数（默认：CPU 核心数）
  --no-install-deps        跳过 yum/dnf 安装依赖
  --help                   显示此帮助

自动查找 php-config 的路径（按优先级）：
  - 系统 PATH 中的 php-configX.Y
  - /usr/bin/php-configX.Y
  - /www/server/php/XY/bin/php-config（宝塔面板）
  - /opt/remi/phpXY/root/usr/bin/php-config（Remi 仓库）
  - /opt/plesk/php/X.Y/bin/php-config（Plesk 面板）

示例:
  ./$(basename "$0") --php-version=8.2
  ./$(basename "$0") --php-config=/www/server/php/82/bin/php-config
  ./$(basename "$0") --php-version=7.4 --exosip-dir=/etc/sip/libs
  ./$(basename "$0") --php-version=8.1 --rebuild-libs --jobs=8
  ./$(basename "$0") --php-version=8.2 --install-dir=/usr/lib64/php/modules
EOF
}

# ============================================================
# 解析参数
# ============================================================
for arg in "$@"; do
    case "$arg" in
        --php-version=*)    PHP_VERSION="${arg#*=}" ;;
        --php-config=*)     PHP_CONFIG="${arg#*=}" ;;
        --exosip-dir=*)     EXOSIP_DIR="${arg#*=}" ;;
        --rebuild-libs)     REBUILD_LIBS=true ;;
        --install-dir=*)    INSTALL_DIR="${arg#*=}" ;;
        --jobs=*)           JOBS="${arg#*=}" ;;
        --no-install-deps)  INSTALL_DEPS=false ;;
        --help|-h)          usage; exit 0 ;;
        *)
            echo "❌ 未知参数: $arg"
            usage
            exit 1
            ;;
    esac
done

# ============================================================
# 查找 php-config
# ============================================================
find_php_config() {
    local version="$1"
    local major_minor="${version/./}"  # "8.2" -> "82"

    local candidates=(
        "php-config${version}"
        "php-config-${version}"
        "/usr/bin/php-config${version}"
        "/usr/bin/php-config-${version}"
        "/usr/local/bin/php-config${version}"
        "/usr/local/bin/php-config-${version}"
        "/www/server/php/${major_minor}/bin/php-config"
        "/opt/remi/php${major_minor}/root/usr/bin/php-config"
        "/opt/plesk/php/${version}/bin/php-config"
        "/opt/cpanel/ea-php${major_minor}/root/usr/bin/php-config"
    )

    for candidate in "${candidates[@]}"; do
        local resolved
        resolved="$(command -v "$candidate" 2>/dev/null || echo "")"
        if [ -n "$resolved" ] && [ -x "$resolved" ]; then
            echo "$resolved"
            return 0
        fi
        if [[ "$candidate" == /* ]] && [ -x "$candidate" ]; then
            echo "$candidate"
            return 0
        fi
    done

    # 最后尝试系统默认 php-config
    if command -v php-config &>/dev/null; then
        local sys_ver
        sys_ver="$(php-config --version 2>/dev/null | cut -d. -f1,2)"
        if [ -z "$version" ] || [ "$sys_ver" = "$version" ]; then
            command -v php-config
            return 0
        fi
    fi

    return 1
}

if [ -z "$PHP_CONFIG" ]; then
    if [ -n "$PHP_VERSION" ]; then
        if ! PHP_CONFIG="$(find_php_config "$PHP_VERSION")"; then
            echo "❌ 未找到 PHP ${PHP_VERSION} 的 php-config"
            echo "   请使用 --php-config=/path/to/php-config 显式指定"
            exit 1
        fi
    else
        if ! PHP_CONFIG="$(command -v php-config 2>/dev/null)"; then
            echo "❌ 未找到 php-config，请安装 PHP 开发包或使用 --php-config 指定路径"
            exit 1
        fi
    fi
fi

[ -x "$PHP_CONFIG" ] || { echo "❌ php-config 不可执行: $PHP_CONFIG"; exit 1; }

DETECTED_VERSION="$("$PHP_CONFIG" --version)"
PHP_EXT_DIR="${INSTALL_DIR:-$("$PHP_CONFIG" --extension-dir)}"
PHPIZE_BIN="$(dirname "$PHP_CONFIG")/phpize"
[ -x "$PHPIZE_BIN" ] || PHPIZE_BIN="$(command -v phpize 2>/dev/null)" || { echo "❌ 未找到 phpize"; exit 1; }

echo "========================================="
echo "php-exosip 编译脚本 (CentOS/RHEL)"
echo "========================================="
echo "PHP 版本:      $DETECTED_VERSION"
echo "php-config:    $PHP_CONFIG"
echo "phpize:        $PHPIZE_BIN"
echo "ExoSip 目录:   $EXOSIP_DIR"
echo "扩展安装目录:  $PHP_EXT_DIR"
echo "并行数:        $JOBS"
echo "========================================="

# ============================================================
# 检测包管理器
# ============================================================
detect_pkg_manager() {
    if command -v dnf &>/dev/null; then
        echo "dnf"
    elif command -v yum &>/dev/null; then
        echo "yum"
    else
        echo ""
    fi
}

PKG_MGR="$(detect_pkg_manager)"

# ============================================================
# 安装系统依赖
# ============================================================
if $INSTALL_DEPS; then
    echo ""
    echo "[依赖] 检查并安装系统依赖..."

    if [ -z "$PKG_MGR" ]; then
        echo "⚠️ 未找到 yum/dnf，跳过依赖安装"
    else
        NEED_PKGS=()

        command -v gcc      &>/dev/null || NEED_PKGS+=("gcc")
        command -v make     &>/dev/null || NEED_PKGS+=("make")
        command -v autoconf &>/dev/null || NEED_PKGS+=("autoconf")
        command -v automake &>/dev/null || NEED_PKGS+=("automake")
        command -v libtool  &>/dev/null || NEED_PKGS+=("libtool")
        command -v curl     &>/dev/null || NEED_PKGS+=("curl")

        # c-ares 开发包
        pkg-config --exists libcares 2>/dev/null || \
            [ -f /usr/include/ares.h ] || \
            NEED_PKGS+=("c-ares-devel")

        if [ ${#NEED_PKGS[@]} -gt 0 ]; then
            echo "   安装缺失包: ${NEED_PKGS[*]}"
            sudo "$PKG_MGR" groupinstall -y "Development Tools" 2>/dev/null || \
            sudo "$PKG_MGR" install -y gcc gcc-c++ make 2>/dev/null || true
            sudo "$PKG_MGR" install -y autoconf automake libtool pkgconfig curl c-ares-devel "${NEED_PKGS[@]}" 2>/dev/null || \
            sudo "$PKG_MGR" install -y autoconf automake libtool pkgconfig curl c-ares-devel || true
        else
            echo "   ✅ 依赖已满足"
        fi

        # 打印 automake 版本（CentOS 7 可能版本过旧）
        if command -v automake &>/dev/null; then
            echo "   automake 版本: $(automake --version | head -n1)"
        fi
    fi
fi

# ============================================================
# autoreconf 辅助函数（CentOS 7 autotools 版本兼容）
# ============================================================
run_autoreconf() {
    local dir="$1"
    local extra_aclocal_dir="${2:-}"

    echo "   重新生成 autotools 文件（autoreconf）..."

    if [ -f "${dir}/configure.ac" ] || [ -f "${dir}/configure.in" ]; then
        pushd "$dir" > /dev/null

        # 清理旧文件，避免版本冲突
        rm -f aclocal.m4 configure config.h.in
        find . -name "Makefile.in" -delete 2>/dev/null || true

        autoreconf -fiv 2>/dev/null || {
            echo "   ⚠️ autoreconf 失败，尝试手动分步执行..."
            if [ -n "$extra_aclocal_dir" ]; then
                aclocal -I "$extra_aclocal_dir" || { echo "❌ aclocal 失败"; exit 1; }
            else
                aclocal || { echo "❌ aclocal 失败"; exit 1; }
            fi
            autoheader 2>/dev/null || echo "   ⚠️ autoheader 跳过"
            automake --add-missing --copy 2>/dev/null || echo "   ⚠️ automake 跳过"
            autoconf || { echo "❌ autoconf 失败"; exit 1; }
        }

        popd > /dev/null
    fi
}

# ============================================================
# Phase 1: 编译 osip/exosip 库
# ============================================================
build_libs() {
    local libs_dir="$1"
    local build_src="${SCRIPT_DIR}/osip-build/build_osip_src"

    echo ""
    echo "[库] 开始编译 libosip2 + libeXosip2..."
    mkdir -p "${libs_dir}"/{lib,include}
    mkdir -p "$build_src"

    pushd "$build_src" > /dev/null

    # 下载源码
    if [ ! -d "osip2" ]; then
        echo "[库] 下载 libosip2-${OSIP_VERSION}..."
        curl -# -L "https://www.antisip.com/download/exosip2/libosip2-${OSIP_VERSION}.tar.gz" | tar -xzf -
        mv "libosip2-${OSIP_VERSION}" osip2
    fi

    if [ ! -d "eXosip2" ]; then
        echo "[库] 下载 libeXosip2-${EXOSIP_VERSION}..."
        curl -# -L "https://www.antisip.com/download/exosip2/libexosip2-${EXOSIP_VERSION}.tar.gz" | tar -xzf -
        mv "libexosip2-${EXOSIP_VERSION}" eXosip2
    fi

    # 编译 libosip2
    echo "[库] 编译 libosip2..."
    run_autoreconf "$(pwd)/osip2"

    pushd osip2 > /dev/null
    make distclean 2>/dev/null || true
    ./configure \
        CC=gcc CFLAGS="-O2 -fPIC" \
        --prefix="${libs_dir}" \
        --disable-shared --enable-static \
        --disable-dependency-tracking \
        || { echo "❌ libosip2 configure 失败"; exit 1; }
    make clean
    make -j"${JOBS}" || make
    make install
    popd > /dev/null

    # 编译 libeXosip2
    echo "[库] 编译 libeXosip2..."
    run_autoreconf "$(pwd)/eXosip2" "${libs_dir}/share/aclocal"

    pushd eXosip2 > /dev/null
    make distclean 2>/dev/null || true
    export PKG_CONFIG_PATH="${libs_dir}/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    ./configure \
        CC=gcc \
        CFLAGS="-O2 -fPIC -I${libs_dir}/include -DENABLE_MAIN_SOCKET" \
        LDFLAGS="-L${libs_dir}/lib" \
        PKG_CONFIG_PATH="${libs_dir}/lib/pkgconfig" \
        --prefix="${libs_dir}" \
        --disable-shared --enable-static \
        --disable-openssl \
        --disable-dependency-tracking \
        || { echo "❌ libeXosip2 configure 失败"; exit 1; }
    make clean
    make -j"${JOBS}" || make
    make install
    popd > /dev/null

    popd > /dev/null

    echo "[库] ✅ 库编译完成"
    ls -lh "${libs_dir}/lib/"lib*.a
}

if [ ! -f "${EXOSIP_DIR}/lib/libeXosip2.a" ] || $REBUILD_LIBS; then
    if $REBUILD_LIBS && [ -f "${EXOSIP_DIR}/lib/libeXosip2.a" ]; then
        echo "[库] --rebuild-libs 强制重新编译..."
    fi
    build_libs "$EXOSIP_DIR"
else
    echo ""
    echo "[库] 使用已有库: $EXOSIP_DIR"
    ls -lh "${EXOSIP_DIR}/lib/"lib*.a 2>/dev/null || true
fi

# 验证库文件存在
for lib in libosipparser2.a libosip2.a libeXosip2.a; do
    [ -f "${EXOSIP_DIR}/lib/${lib}" ] || { echo "❌ 缺少库文件: ${EXOSIP_DIR}/lib/${lib}"; exit 1; }
done

# ============================================================
# Phase 2: 编译 PHP 扩展
# ============================================================
echo ""
echo "[扩展] 编译 PHP 扩展..."
cd "$SCRIPT_DIR"

"$PHPIZE_BIN" --clean
"$PHPIZE_BIN"

./configure \
    --enable-exosip="${EXOSIP_DIR}" \
    --with-php-config="${PHP_CONFIG}"

make clean || true

# make clean 可能清理掉 osip 静态库，重新编译
if [ ! -f "${EXOSIP_DIR}/lib/libeXosip2.a" ]; then
    echo "📦 静态库被清理，重新编译..."
    build_libs "$EXOSIP_DIR"
fi

make -j"${JOBS}" || make

# ============================================================
# Phase 3: 检查 .o 文件
# ============================================================
echo ""
echo "[验证] 检查编译产物..."

if [ ! -f ".libs/php_exosip.o" ] || [ ! -f ".libs/exosip_wrapper.o" ]; then
    echo "❌ 未找到编译后的 .o 文件"
    exit 1
fi

# ============================================================
# Phase 4: 绕过 libtool 手动链接（CentOS 必须步骤）
#
# 原因：CentOS 的 libtool 有时不会将静态库中所有符号完整
# 链入 .so，导致 osip_* 符号在运行时找不到。
# 使用 --whole-archive 强制包含全部符号。
# ============================================================
echo ""
echo "[链接] 绕过 libtool，手动链接 .so..."
gcc -shared -o .libs/exosip.so \
    .libs/php_exosip.o .libs/exosip_wrapper.o \
    -Wl,--whole-archive \
    -Wl,--start-group \
    "${EXOSIP_DIR}/lib/libeXosip2.a" \
    "${EXOSIP_DIR}/lib/libosip2.a" \
    "${EXOSIP_DIR}/lib/libosipparser2.a" \
    -Wl,--end-group \
    -Wl,--no-whole-archive \
    -lresolv -lpthread -lrt -ldl

# 验证符号
MISSING=$(nm .libs/exosip.so 2>/dev/null | grep -c " U osip_" || true)
if [ "$MISSING" -gt 0 ]; then
    echo "   ⚠️ 仍有 ${MISSING} 个未定义 osip_ 符号："
    nm .libs/exosip.so | grep " U osip_" | head -10
    echo "   继续安装，但运行时可能出现符号找不到错误"
else
    echo "   ✅ 所有 osip_ 符号已正确链接"
fi

# ============================================================
# Phase 5: 安装
# ============================================================
echo ""
echo "[安装] 安装到 ${PHP_EXT_DIR}..."
sudo mkdir -p "${PHP_EXT_DIR}"
sudo cp .libs/exosip.so "${PHP_EXT_DIR}/"
sudo chmod 755 "${PHP_EXT_DIR}/exosip.so"

echo ""
echo "========================================="
echo "✅ 编译安装完成！PHP ${DETECTED_VERSION}"
echo "========================================="
echo ""

# 验证加载
PHP_BIN="$(dirname "$PHP_CONFIG")/php"
if [ -x "$PHP_BIN" ]; then
    if "$PHP_BIN" -m 2>/dev/null | grep -q "^exosip$"; then
        echo "  ✅ exosip 扩展已加载"
    else
        echo "  ⚠️ 扩展已安装，请在 php.ini 中添加："
        echo "     extension=exosip.so"
        PHP_INI_DIR="$("$PHP_CONFIG" --ini-path 2>/dev/null || echo '未知')"
        echo "  php.ini 目录: ${PHP_INI_DIR}"
    fi
fi

echo ""
echo "  运行测试: php examples/gb28181_server.php"
echo ""
echo "  编译环境信息:"
echo "    GCC:      $(gcc --version | head -n1)"
command -v automake &>/dev/null && echo "    automake: $(automake --version | head -n1)" || true
