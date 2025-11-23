#!/bin/bash
# 精确补丁：修复 osip_list.c 的 osip_list_get_first 空指针问题

set -e

OSIP_LIST_FILE="osip-build/build_osip_src/osip2/src/osipparser2/osip_list.c"

cd /Users/jiechengyang/src/c-app/php-exosip

if [ ! -f "$OSIP_LIST_FILE" ]; then
    echo "❌ 找不到 $OSIP_LIST_FILE"
    exit 1
fi

echo "🔧 修复 osip_list_get_first() 空指针检查..."
echo ""

# 备份原文件
cp "$OSIP_LIST_FILE" "$OSIP_LIST_FILE.manual_backup"

# 使用 sed 精确修改第201行
# 原始：if (li == NULL || 0 >= li->nb_elt) {
# 修改为：if (li == NULL || li->node == NULL || 0 >= li->nb_elt) {
sed -i.bak '201s/if (li == NULL || 0 >= li->nb_elt)/if (li == NULL || li->node == NULL || 0 >= li->nb_elt)/' "$OSIP_LIST_FILE"

# 验证修改
echo "📝 修改后的第201行："
sed -n '201p' "$OSIP_LIST_FILE"
echo ""

# 显示上下文
echo "📋 函数上下文（200-212行）："
sed -n '200,212p' "$OSIP_LIST_FILE"
echo ""

echo "✅ 补丁应用成功！"
echo ""
echo "备份文件："
echo "  - $OSIP_LIST_FILE.manual_backup (完整备份)"
echo "  - $OSIP_LIST_FILE.bak (sed备份)"
echo ""
echo "下一步："
echo "  cd osip-build && ./build_osip_macos.sh"
echo "  cd .. && make clean && make"

