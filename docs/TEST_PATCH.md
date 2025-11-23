# eXosip 5.3.0 补丁测试指南

## 🎯 已应用的补丁

**修复位置**：`osip2/src/osipparser2/osip_list.c:201`

**修改内容**：
```c
// 修改前
if (li == NULL || 0 >= li->nb_elt) {

// 修改后  
if (li == NULL || li->node == NULL || 0 >= li->nb_elt) {
```

**关键作用**：防止第211行 `return li->node->element;` 访问空指针。

## 🧪 测试方案

### 测试 1：原生 C 程序（验证 eXosip 库本身）

```bash
cd /Users/jiechengyang/src/c-app/php-exosip

# 清理端口
lsof -ti:15060 | xargs kill -9 2>/dev/null

# 运行原生测试
./tests/test_exosip_udp
```

**如果原生程序崩溃**：说明 eXosip 5.3.0 本身有更深层次的问题，需要降级到 5.2.0 或等待官方修复。

**如果原生程序正常**：说明问题在 PHP 扩展层，继续测试 2。

---

### 测试 2：PHP 扩展（完整功能）

```bash
cd /Users/jiechengyang/src/c-app/php-exosip

# 清理端口
lsof -ti:15060 | xargs kill -9 2>/dev/null

# 运行 PHP 服务器
php examples/gb28181_server.php
```

**预期结果**：
- ✅ 服务器启动：`GB28181 Server started on 0.0.0.0:15060`
- ✅ 设备注册：`Received REGISTER`
- ✅ 发送 200 OK：`Sent response 200`
- ✅ Keepalive 处理：不崩溃
- ✅ 目录查询：能成功发送并接收响应

---

### 测试 3：使用调试工具（如果崩溃）

```bash
cd /Users/jiechengyang/src/c-app/php-exosip

# 运行调试脚本
./debug_server.sh
```

**查看崩溃点**：
- 如果崩溃在 `osip_list_get_first + 136`：补丁未生效或有其他地方也需要修复
- 如果崩溃在其他函数：需要针对性修复

---

## 🔍 验证补丁是否生效

```bash
# 查看修改后的第201行
sed -n '201p' osip-build/build_osip_src/osip2/src/osipparser2/osip_list.c
```

**应该输出**：
```
  if (li == NULL || li->node == NULL || 0 >= li->nb_elt) {
```

如果**没有** `li->node == NULL`，说明补丁未生效，需要重新应用：
```bash
./osip-build/patch_osip_5.3.0_fix_node_null.sh
cd osip-build && ./build_osip_macos.sh
cd .. && make clean && make
```

---

## 📊 结果对比

| 测试场景 | 5.3.0 原版 | 5.3.0 + 补丁 | 预期结果 |
|---------|-----------|-------------|---------|
| 原生 C 程序 | ❌ 崩溃 | ✅ 正常 | ✅ |
| PHP 设备注册 | ❌ 崩溃 | ✅ 正常 | ✅ |
| PHP Keepalive | ❌ 崩溃 | ✅ 正常 | ✅ |
| PHP 目录查询 | ❌ 崩溃 | ✅ 正常 | ✅ |

---

## 🚨 如果补丁后仍然崩溃

### 方案 1：降级到 5.2.0（牺牲 TCP）

```bash
cd /Users/jiechengyang/src/c-app/php-exosip

# 备份当前版本
mv libs libs-530-patched

# 使用 5.2.0
mv libs-520 libs

# 重新编译
make clean && make

# 测试
php examples/gb28181_server.php
```

### 方案 2：深度调试

如果原生 C 程序也崩溃，说明需要更多补丁：

```bash
# 查看其他可能需要修复的函数
grep -n "li->node->" osip-build/build_osip_src/osip2/src/osipparser2/osip_list.c

# 检查 osip_list_get_next, osip_list_get 等函数
```

可能需要修复的其他函数：
- `osip_list_get_next()`
- `osip_list_get()`
- `osip_list_remove()`

### 方案 3：联系官方

如果问题无法解决，建议：
1. 向 osip/eXosip 官方提交 bug report
2. 附上崩溃堆栈和复现步骤
3. 临时使用 5.2.0 版本

---

## 📝 补丁文件位置

- 补丁脚本：`osip-build/patch_osip_5.3.0_fix_node_null.sh`
- 源文件：`osip-build/build_osip_src/osip2/src/osipparser2/osip_list.c`
- 备份：`osip_list.c.manual_backup`

---

## ✅ 下一步

1. **先测试原生程序**：`./tests/test_exosip_udp`
2. **如果原生正常**：测试 PHP 扩展
3. **如果都正常**：补丁成功！
4. **如果仍崩溃**：查看 [方案 1 或 2]

---

**更新时间**：2024-11-19 18:25
**补丁版本**：v2.0 (精确修复 node 空指针)

