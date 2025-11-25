# 文档索引

本目录包含 php-exosip 扩展的所有技术文档。

## 📚 核心架构文档

### [MASTER_WORKER_TASK.md](MASTER_WORKER_TASK.md) ⭐️
Master-Worker-Task 多进程架构完整说明
- 架构设计原理
- 进程通信机制
- 使用指南和最佳实践
- 性能优化建议

### [MASTER_WORKER_TASK_IMPLEMENTATION.md](MASTER_WORKER_TASK_IMPLEMENTATION.md) ⭐️
Task→Worker 管道通信实现细节
- C 层实现(socketpair, 序列化)
- PHP API 层(sendToWorker, onPipeMessage)
- 使用示例和测试方法

### [TCP_MODE_SUPPORT.md](TCP_MODE_SUPPORT.md) ⭐️
TCP 传输模式支持文档
- TCP vs UDP 对比
- 设备连接管理(device_id ↔ fd 映射)
- DeviceManager API 说明
- GB28181Handler TCP 集成
- 完整测试示例

### [TASK_SERVER_OBJECT_SAFETY.md](TASK_SERVER_OBJECT_SAFETY.md) ⭐️
Task 进程中 $server 对象安全性分析
- fork() 内存模型详解
- 进程隔离机制
- 安全操作指南
- 禁止操作列表

## 🛠️ 开发指南

### [CALLBACK_ERROR_HANDLING.md](CALLBACK_ERROR_HANDLING.md)
PHP 回调错误处理机制
- zend_try/zend_catch 异常捕获
- CallbackWrapper 使用方法
- onError 回调设计

### [CLIENT_IMPLEMENTATION.md](CLIENT_IMPLEMENTATION.md)
SIP 客户端实现说明
- ExoSipClient 类设计
- 手动事件循环模式
- 避免后台线程问题

### [QUICKSTART.md](QUICKSTART.md)
快速开始指南
- 安装步骤
- 基础示例
- 常见问题

## 🔧 编译和部署

### [BUILD_CENTOS.md](BUILD_CENTOS.md) / [CENTOS_COMPILE.md](CENTOS_COMPILE.md)
CentOS 编译指南
- 依赖安装
- 编译步骤
- 符号冲突修复

### [PLATFORM_SUPPORT.md](PLATFORM_SUPPORT.md)
平台支持说明
- Linux/macOS/Windows 差异
- TCP 模式限制
- 推荐配置

## 🐛 问题修复

### [BUG_FIX_SUMMARY.md](BUG_FIX_SUMMARY.md)
Bug 修复总结
- SIP MESSAGE 目标 IP 错误修复
- Via received 参数使用
- Contact vs 实际源地址

### [CRITICAL_FIX_OSIP_LIST.md](CRITICAL_FIX_OSIP_LIST.md)
osip_list 双重释放修复
- 问题根源分析
- 修复方案
- 验证结果

### [FIX_VALIDATION.md](FIX_VALIDATION.md)
修复验证报告
- 测试场景
- 验证结果
- 回归测试

### [TEST_PATCH.md](TEST_PATCH.md)
补丁测试说明
- 测试方法
- 预期结果
- 回滚步骤

### [PATCH_EXOSIP_5.3.0.md](PATCH_EXOSIP_5.3.0.md)
eXosip 5.3.0 补丁说明
- 补丁内容
- 应用方法
- 兼容性说明

## 📖 业务文档(中文)

### [国标注册流程.md](国标注册流程.md)
GB28181 设备注册流程详解
- 注册请求和响应
- 认证机制(Digest MD5)
- 状态机转换

### [需要对接信令流程.md](需要对接信令流程.md)
GB28181 信令对接清单
- 设备管理信令
- 实时点播信令
- 云台控制信令
- 录像回放信令

### [抓包流程.md](抓包流程.md)
SIP 抓包和分析方法
- tcpdump 命令
- Wireshark 过滤器
- 常见问题排查

### [架构升级.md](架构升级.md)
架构演进历史
- v1.0: 单进程模式
- v2.0: 事件驱动
- v2.1: Master-Worker-Task
- v2.2: Task→Worker 管道

### [单线程+queue+event loop的新方案.md](单线程+queue+evevt loop的新方案.md)
早期架构设计方案(已废弃)
- 单线程事件循环
- 消息队列设计
- 最终采用多进程方案

## 📝 API 参考

### [exosip.stub.php](exosip.stub.php)
IDE 类型提示文件
- ExoSip 类完整 API
- SipEvent 类
- SipSession 类
- ExoSipClient 类
- 所有回调和方法的文档注释

---

## 文档分类说明

**⭐️ 核心文档** - 必读,理解系统架构和关键功能  
**🛠️ 开发指南** - 开发时参考,代码实现细节  
**🔧 编译部署** - 安装配置,环境搭建  
**🐛 问题修复** - 历史问题和解决方案  
**📖 业务文档** - GB28181 业务逻辑,中文说明  
**📝 API 参考** - API 完整文档

## 推荐阅读顺序

### 新手入门
1. [QUICKSTART.md](QUICKSTART.md) - 快速开始
2. [MASTER_WORKER_TASK.md](MASTER_WORKER_TASK.md) - 理解架构
3. [exosip.stub.php](exosip.stub.php) - API 参考

### 深入开发
1. [MASTER_WORKER_TASK_IMPLEMENTATION.md](MASTER_WORKER_TASK_IMPLEMENTATION.md) - 管道通信
2. [TASK_SERVER_OBJECT_SAFETY.md](TASK_SERVER_OBJECT_SAFETY.md) - 进程安全
3. [TCP_MODE_SUPPORT.md](TCP_MODE_SUPPORT.md) - TCP 模式
4. [CALLBACK_ERROR_HANDLING.md](CALLBACK_ERROR_HANDLING.md) - 错误处理

### GB28181 对接
1. [国标注册流程.md](国标注册流程.md) - 注册流程
2. [需要对接信令流程.md](需要对接信令流程.md) - 信令清单
3. [抓包流程.md](抓包流程.md) - 调试方法

### 运维部署
1. [PLATFORM_SUPPORT.md](PLATFORM_SUPPORT.md) - 平台选择
2. [BUILD_CENTOS.md](BUILD_CENTOS.md) - 编译安装
3. [README.md](../README.md) - 生产部署

## 维护说明

- 所有新功能应更新对应文档
- Bug 修复应记录在 [BUG_FIX_SUMMARY.md](BUG_FIX_SUMMARY.md)
- API 变更应同步更新 [exosip.stub.php](exosip.stub.php)
- 中文文档保留用于业务对接参考
- 过时文档标注 "(已废弃)" 但保留以供历史参考
