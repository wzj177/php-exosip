#!/bin/bash

# 检查是否已编译
if [ ! -f "./test_exosip_udp" ]; then
    echo "编译测试程序 (macOS)..."
    
    # macOS 编译参数：移除 -lrt，不使用 --start-group/--end-group
    gcc -g -O0 -Wall \
        -I./libs/include \
        -I../libs/include/eXosip2 \
        -I../libs/include/osip2 \
        -I../libs/include/osipparser2 \
        -o test_exosip_udp test_exosip_udp.c \
        -L../libs/lib \
        -leXosip2 -losip2 -losipparser2 \
        -lpthread -lresolv -ldl

    if [ $? -ne 0 ]; then
        echo "❌ UDP 测试程序编译失败"
        exit 1
    fi
    
    echo "✅ 编译成功"
    echo ""
fi

# 可选：后续可添加运行或调试逻辑
# ./test_exosip_udp