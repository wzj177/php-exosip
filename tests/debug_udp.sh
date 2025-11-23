#!/bin/bash
# 调试 C 编译生成的 test_exosip_udp 程序的崩溃堆栈

echo "正在启动 lldb 调试 test_exosip_udp..."
echo ""

# 可选：清理可能冲突的端口（根据你的 test_exosip_udp 实际使用的端口调整）
# 例如假设 test_exosip_udp 使用 5060 UDP 端口
lsof -ti:15060 | xargs kill -9 2>/dev/null
sleep 1

# 创建 lldb 命令文件
cat > ./lldb_crash.txt <<'EOF'
# 捕获段错误（SIGSEGV）并停止
process handle SIGSEGV --stop true --pass false --notify true
# 如果你的程序可能收到 SIGBUS（如 ARM 上非法地址访问），也建议加上：
process handle SIGBUS --stop true --pass false --notify true
# 运行程序
run
# 崩溃后打印所有线程的完整调用栈
thread backtrace all
# 退出 lldb
quit
EOF

# 启动 lldb 调试 test_exosip_udp（确保 test_exosip_udp 在当前目录或 PATH 中）
# 如果 test_exosip_udp 有命令行参数，可以加在后面，例如：./test_exosip_udp -v
if [ ! -x "./test_exosip_udp" ]; then
    echo "错误：未找到可执行文件 ./test_exosip_udp"
    echo "请确保已编译生成 test_exosip_udp（建议带 -g 选项）"
    exit 1
fi

lldb -b -s ./lldb_crash.txt -- ./test_exosip_udp 2>&1 | tee crash_log.txt

# 清理临时文件
rm -f ./lldb_crash.txt

echo ""
echo "========================================="
echo "崩溃日志已保存到: crash_log.txt"
echo "建议检查是否使用 'gcc -g' 编译以获得完整符号信息"
echo "========================================="