#!/bin/bash
# 补丁脚本：修复 eXosip 5.3.0 的 osip_list_get_first segfault 问题
# 添加空指针检查以避免崩溃

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OSIP_SRC="$SCRIPT_DIR/build_osip_src/osip2"
EXOSIP_SRC="$SCRIPT_DIR/build_osip_src/eXosip2"

echo "========================================="
echo "  eXosip 5.3.0 Segfault 补丁"
echo "========================================="
echo ""
echo "此补丁将修复 osip_list_get_first 的空指针崩溃问题"
echo ""

# 检查源码目录
if [ ! -d "$OSIP_SRC" ]; then
    echo "❌ 错误：找不到 osip2 源码目录: $OSIP_SRC"
    exit 1
fi

if [ ! -d "$EXOSIP_SRC" ]; then
    echo "❌ 错误：找不到 eXosip2 源码目录: $EXOSIP_SRC"
    exit 1
fi

echo "✓ 源码目录检测成功"
echo ""

# ============================================================
# 补丁 1: osip_list.c - osip_list_get_first 添加空指针检查
# ============================================================

OSIP_LIST_FILE="$OSIP_SRC/src/osipparser2/osip_list.c"

if [ ! -f "$OSIP_LIST_FILE" ]; then
    echo "⚠️  警告：找不到 osip_list.c，跳过"
else
    echo "[1/4] 补丁 osip_list.c - osip_list_get_first()..."
    
    # 备份原文件
    cp "$OSIP_LIST_FILE" "$OSIP_LIST_FILE.backup"
    
    # 查找并修改 osip_list_get_first 函数
    # 在函数开头添加空指针检查
    cat > /tmp/osip_list_patch.awk << 'EOF'
BEGIN { in_function = 0; patched = 0 }

# 找到函数定义
/^osip_list_get_first.*\(/ {
    in_function = 1
    print
    next
}

# 在函数体的第一个 { 后插入检查
in_function && /{/ && !patched {
    print
    print "  /* [PATCH] 添加空指针检查以避免 segfault */"
    print "  if (li == NULL || li->node == NULL) {"
    print "    if (iterator) iterator->pos = NULL;"
    print "    return NULL;"
    print "  }"
    patched = 1
    next
}

# 函数结束
in_function && /^}/ {
    in_function = 0
    patched = 0
}

{ print }
EOF

    awk -f /tmp/osip_list_patch.awk "$OSIP_LIST_FILE.backup" > "$OSIP_LIST_FILE"
    rm /tmp/osip_list_patch.awk
    
    echo "  ✓ 已添加空指针检查到 osip_list_get_first()"
fi

# ============================================================
# 补丁 2: ist.c - osip_ist_execute 添加上下文检查
# ============================================================

IST_FILE="$OSIP_SRC/src/osip2/ist.c"

if [ ! -f "$IST_FILE" ]; then
    echo "⚠️  警告：找不到 ist.c，跳过"
else
    echo "[2/4] 补丁 ist.c - osip_ist_execute()..."
    
    # 备份原文件
    cp "$IST_FILE" "$IST_FILE.backup"
    
    # 在 osip_ist_execute 开头添加检查
    cat > /tmp/ist_patch.awk << 'EOF'
BEGIN { in_function = 0; patched = 0 }

# 找到函数定义
/^osip_ist_execute.*\(/ {
    in_function = 1
    print
    next
}

# 在函数体的第一个 { 后插入检查
in_function && /{/ && !patched {
    print
    print "  /* [PATCH] 添加上下文检查以避免 segfault */"
    print "  if (ist == NULL || ist->state == IST_TERMINATED) {"
    print "    return;"
    print "  }"
    patched = 1
    next
}

# 函数结束
in_function && /^}/ {
    in_function = 0
    patched = 0
}

{ print }
EOF

    awk -f /tmp/ist_patch.awk "$IST_FILE.backup" > "$IST_FILE"
    rm /tmp/ist_patch.awk
    
    echo "  ✓ 已添加上下文检查到 osip_ist_execute()"
fi

# ============================================================
# 补丁 3: ict.c - osip_ict_execute 添加上下文检查
# ============================================================

ICT_FILE="$OSIP_SRC/src/osip2/ict.c"

if [ ! -f "$ICT_FILE" ]; then
    echo "⚠️  警告：找不到 ict.c，跳过"
else
    echo "[3/4] 补丁 ict.c - osip_ict_execute()..."
    
    # 备份原文件
    cp "$ICT_FILE" "$ICT_FILE.backup"
    
    # 在 osip_ict_execute 开头添加检查
    cat > /tmp/ict_patch.awk << 'EOF'
BEGIN { in_function = 0; patched = 0 }

# 找到函数定义
/^osip_ict_execute.*\(/ {
    in_function = 1
    print
    next
}

# 在函数体的第一个 { 后插入检查
in_function && /{/ && !patched {
    print
    print "  /* [PATCH] 添加上下文检查以避免 segfault */"
    print "  if (ict == NULL || ict->state == ICT_TERMINATED) {"
    print "    return;"
    print "  }"
    patched = 1
    next
}

# 函数结束
in_function && /^}/ {
    in_function = 0
    patched = 0
}

{ print }
EOF

    awk -f /tmp/ict_patch.awk "$ICT_FILE.backup" > "$ICT_FILE"
    rm /tmp/ict_patch.awk
    
    echo "  ✓ 已添加上下文检查到 osip_ict_execute()"
fi

# ============================================================
# 补丁 4: eXosip.c - 事件处理增强检查
# ============================================================

EXOSIP_FILE="$EXOSIP_SRC/src/eXosip.c"

if [ ! -f "$EXOSIP_FILE" ]; then
    echo "⚠️  警告：找不到 eXosip.c，跳过"
else
    echo "[4/4] 补丁 eXosip.c - eXosip_execute()..."
    
    # 备份原文件
    cp "$EXOSIP_FILE" "$EXOSIP_FILE.backup"
    
    echo "  ℹ️  eXosip.c 通常不需要额外补丁，已备份"
fi

echo ""
echo "========================================="
echo "  补丁应用完成！"
echo "========================================="
echo ""
echo "已修补的文件："
echo "  1. $OSIP_LIST_FILE"
[ -f "$IST_FILE.backup" ] && echo "  2. $IST_FILE"
[ -f "$ICT_FILE.backup" ] && echo "  3. $ICT_FILE"
echo ""
echo "备份文件（*.backup）已保存，如需回滚："
echo "  cd $OSIP_SRC"
echo "  find . -name '*.backup' -exec bash -c 'mv \"\$1\" \"\${1%.backup}\"' _ {} \\;"
echo ""
echo "下一步："
echo "  1. 运行: ./build_osip_macos.sh"
echo "  2. 编译扩展: cd .. && make clean && make"
echo "  3. 测试服务器: php examples/gb28181_server.php"
echo ""

