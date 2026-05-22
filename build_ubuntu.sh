#!/bin/bash

# php-exosip 统一编译脚本 - Ubuntu/Debian

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OSIP_VERSION="5.3.0"
EXOSIP_VERSION="5.3.0"

PHP_CONFIG=""
PHP_VERSION=""
EXOSIP_DIR="${SCRIPT_DIR}/osip-build/libs"
REBUILD_LIBS=false
INSTALL_DIR=""
JOBS=$(nproc 2>/dev/null || echo 4)
INSTALL_DEPS=true

usage() {
    cat <<EOF
用法: $(basename "$0") [选项]

  --php-version=X.Y
  --php-config=/path
  --exosip-dir=/path
  --rebuild-libs
  --install-dir=/path
  --jobs=N
  --no-install-deps
  --help
EOF
}

# ============================================================
# 参数解析
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
        *) echo "❌ 未知参数: $arg"; usage; exit 1 ;;
    esac
done

# ============================================================
# ✅ 修复：php-config 查找（增强版）
# ============================================================
find_php_config() {
    local version="$1"
    local major_minor="${version/./}"

    local candidates=(
        "php-config${version}"
        "php-config-${version}"
        "php-config${major_minor}"
        "/usr/bin/php-config${version}"
        "/usr/bin/php-config-${version}"
        "/usr/bin/php-config${major_minor}"
        "/usr/bin/php${version}-config"
        "/usr/local/bin/php-config${version}"
        "/usr/local/bin/php-config-${version}"
        "/www/server/php/${major_minor}/bin/php-config"
        "/opt/php-${version}/bin/php-config"
        "/opt/php${major_minor}/bin/php-config"
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

    # Ubuntu alternatives
    if command -v update-alternatives >/dev/null 2>&1; then
        local alt
        alt="$(update-alternatives --list php-config 2>/dev/null | grep "$version" | head -n1 || true)"
        if [ -n "$alt" ] && [ -x "$alt" ]; then
            echo "$alt"
            return 0
        fi
    fi

    # fallback
    if command -v php-config &>/dev/null; then
        local sys_ver
        sys_ver="$(php-config --version 2>/dev/null | cut -d. -f1,2)"
        if [ -z "$version" ] || [[ "$sys_ver" == "$version"* ]]; then
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
            exit 1
        fi
    else
        PHP_CONFIG="$(command -v php-config || true)"
    fi
fi

[ -x "$PHP_CONFIG" ] || { echo "❌ php-config 不可执行: $PHP_CONFIG"; exit 1; }

DETECTED_VERSION="$("$PHP_CONFIG" --version)"
PHP_EXT_DIR="${INSTALL_DIR:-$("$PHP_CONFIG" --extension-dir)}"
PHPIZE_BIN="$(dirname "$PHP_CONFIG")/phpize"
[ -x "$PHPIZE_BIN" ] || PHPIZE_BIN="$(command -v phpize)" || { echo "❌ 未找到 phpize"; exit 1; }

echo "========================================="
echo "PHP: $DETECTED_VERSION"
echo "php-config: $PHP_CONFIG"
echo "phpize: $PHPIZE_BIN"
echo "========================================="

# ============================================================
# 依赖安装（保持原逻辑）
# ============================================================
if $INSTALL_DEPS; then
    NEED_PKGS=()
    command -v gcc &>/dev/null || NEED_PKGS+=("gcc")
    command -v make &>/dev/null || NEED_PKGS+=("make")
    command -v autoconf &>/dev/null || NEED_PKGS+=("autoconf")
    command -v automake &>/dev/null || NEED_PKGS+=("automake")
    command -v libtool &>/dev/null || NEED_PKGS+=("libtool")
    command -v curl &>/dev/null || NEED_PKGS+=("curl")

    pkg-config --exists libcares 2>/dev/null || \
    [ -f /usr/include/ares.h ] || \
    NEED_PKGS+=("libc-ares-dev")

    if [ ${#NEED_PKGS[@]} -gt 0 ]; then
        sudo apt-get update -qq
        sudo apt-get install -y build-essential "${NEED_PKGS[@]}"
    fi
fi

# ============================================================
# 编译库（原样）
# ============================================================
build_libs() {
    local libs_dir="$1"
    local build_src="${SCRIPT_DIR}/osip-build/build_osip_src"

    mkdir -p "${libs_dir}"/{lib,include}
    mkdir -p "$build_src"
    pushd "$build_src" >/dev/null

    if [ ! -d "osip2" ]; then
        curl -L "https://www.antisip.com/download/exosip2/libosip2-${OSIP_VERSION}.tar.gz" | tar -xzf -
        mv "libosip2-${OSIP_VERSION}" osip2
    fi

    if [ ! -d "eXosip2" ]; then
        curl -L "https://www.antisip.com/download/exosip2/libexosip2-${EXOSIP_VERSION}.tar.gz" | tar -xzf -
        mv "libexosip2-${EXOSIP_VERSION}" eXosip2
    fi

    pushd osip2 >/dev/null
    ./configure \
        CFLAGS="-O2 -fPIC" \
        --prefix="${libs_dir}" --disable-shared --enable-static --disable-dependency-tracking
    make -j"${JOBS}" && make install
    popd >/dev/null

    pushd eXosip2 >/dev/null
    export PKG_CONFIG_PATH="${libs_dir}/lib/pkgconfig"
    ./configure \
        CFLAGS="-O2 -fPIC -I${libs_dir}/include" \
        LDFLAGS="-L${libs_dir}/lib" \
        PKG_CONFIG_PATH="${libs_dir}/lib/pkgconfig" \
        --prefix="${libs_dir}" --disable-shared --enable-static --disable-openssl \
        --disable-dependency-tracking
    make -j"${JOBS}" && make install
    popd >/dev/null

    popd >/dev/null
}

if [ ! -f "${EXOSIP_DIR}/lib/libeXosip2.a" ] || $REBUILD_LIBS; then
    build_libs "$EXOSIP_DIR"
fi

# ============================================================
# 编译 PHP 扩展
# ============================================================
cd "$SCRIPT_DIR"

"$PHPIZE_BIN"
./configure --enable-exosip="${EXOSIP_DIR}" --with-php-config="${PHP_CONFIG}"
make -j"${JOBS}"

# ============================================================
# 安装
# ============================================================
sudo cp .libs/exosip.so "${PHP_EXT_DIR}/"

echo "✅ 完成"