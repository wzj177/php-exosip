# GB28181 项目文档索引

> 本项目包含 GB28181 SIP 网关的完整文档，涵盖参考资料、架构设计、功能实现、待开发任务、部署运维和问题修复等方面。

---

## 📂 文档结构

```
docs/
├── 01-参考资料/          # GB28181 协议学习资料和参考项目分析
├── 02-架构设计/          # 系统架构设计文档
├── 03-功能实现/          # 已实现功能的详细说明
├── 04-待开发功能/        # 待开发任务清单和2022版本支持
├── 05-部署运维/          # 编译、部署和运维文档
├── 06-问题修复/          # Bug修复和问题排查记录
├── 07-开发指南/          # 开发规范和入门指南
├── 开发进度/             # 开发进度记录
├── ak-stream/            # AKStream 参考代码
├── GB28181-Service/      # GB28181-Service 参考代码
└── zlm/                  # ZLMediaKit 相关资料
```

---

## 🎯 快速导航

### 新手入门
- [快速开始](05-部署运维/QUICKSTART.md) - 项目快速部署和测试
- [开发指南](07-开发指南/README.md) - 开发环境配置和编码规范
- [CentOS 编译](05-部署运维/CENTOS_COMPILE.md) - Linux 环境下的编译步骤

### 核心功能
- [GB28181 命令指南](03-功能实现/GB28181_COMMAND_GUIDE.md) - 设备控制命令详解
- [ZLMediaKit 集成](03-功能实现/GB28181_ZLM_INTEGRATION.md) - 媒体服务器对接
- [TCP 模式支持](03-功能实现/TCP_MODE_SUPPORT.md) - TCP 主被动模式实现
- [长任务支持](03-功能实现/LONG_TASK_SUPPORT.md) - 异步任务处理机制

### 待开发任务
- ⚡ [C 扩展待开发](04-待开发功能/C_EXTENSION_TODO.md) - C 扩展层开发任务（约 20-30 工作日）
- ⚡ [PHP 网关待开发](04-待开发功能/PHP_GATEWAY_TODO.md) - PHP 层开发任务（约 30-45 工作日）
- [2022 版本支持](04-待开发功能/扩展2022版本国标协议方案.md) - GB28181-2022 新特性
- [订阅功能状态](03-功能实现/SUBSCRIBE_FEATURE_STATUS.md) - 订阅功能支持情况

---

## 📋 01-参考资料

学习 GB28181 协议和参考其他项目实现：

| 文档 | 说明 |
|------|------|
| [参考项目-akStream运作原理.md](01-参考资料/参考项目-akStream运作原理.md) | AKStream 项目架构分析 |
| [国标注册流程.md](01-参考资料/国标注册流程.md) | GB28181 设备注册详细流程 |
| [需要对接信令流程.md](01-参考资料/需要对接信令流程.md) | 信令对接要点说明 |
| [抓包流程.md](01-参考资料/抓包流程.md) | SIP 信令抓包分析方法 |

---

## 🏗️ 02-架构设计

系统整体架构和核心机制设计：

| 文档 | 说明 |
|------|------|
| [架构升级.md](02-架构设计/架构升级.md) | 系统架构演进历史 |
| [单线程+queue+event loop的新方案.md](02-架构设计/单线程+queue+evevt%20loop的新方案.md) | 基于 Workerman 的事件循环架构 |
| [SOCKET_FORK_ARCHITECTURE.md](02-架构设计/SOCKET_FORK_ARCHITECTURE.md) | Socket 复制和进程 fork 机制 |
| [FORK_MEMORY_COPY_MECHANISM.md](02-架构设计/FORK_MEMORY_COPY_MECHANISM.md) | 进程间内存复制机制 |
| [MASTER_WORKER_TASK.md](02-架构设计/MASTER_WORKER_TASK.md) | Master-Worker 任务模型 |
| [MASTER_WORKER_TASK_IMPLEMENTATION.md](02-架构设计/MASTER_WORKER_TASK_IMPLEMENTATION.md) | 任务模型实现细节 |

---

## ✅ 03-功能实现

已实现功能的详细技术文档：

### 核心功能
| 文档 | 说明 |
|------|------|
| [GB28181_COMMAND_GUIDE.md](03-功能实现/GB28181_COMMAND_GUIDE.md) | GB28181 所有命令的使用指南 |
| [GB28181_HANDLER_ZLM_USAGE.md](03-功能实现/GB28181_HANDLER_ZLM_USAGE.md) | GB28181Handler 和 ZLM 对接 |
| [GB28181_ZLM_INTEGRATION.md](03-功能实现/GB28181_ZLM_INTEGRATION.md) | ZLMediaKit 完整集成方案 |
| [TCP_MODE_SUPPORT.md](03-功能实现/TCP_MODE_SUPPORT.md) | TCP 主动/被动模式支持 |

### 客户端和回调
| 文档 | 说明 |
|------|------|
| [CLIENT_IMPLEMENTATION.md](03-功能实现/CLIENT_IMPLEMENTATION.md) | Gb28181Client PHP SDK 实现 |
| [CLIENT_USAGE.md](03-功能实现/CLIENT_USAGE.md) | 客户端使用示例 |
| [CALLBACK_ERROR_HANDLING.md](03-功能实现/CALLBACK_ERROR_HANDLING.md) | 回调错误处理机制 |

### 任务和消息
| 文档 | 说明 |
|------|------|
| [LONG_TASK_SUPPORT.md](03-功能实现/LONG_TASK_SUPPORT.md) | 长任务支持（录像查询等） |
| [LONG_TASK_REDIS_EXAMPLE.md](03-功能实现/LONG_TASK_REDIS_EXAMPLE.md) | Redis 任务队列示例 |
| [TASK_SERVER_OBJECT_SAFETY.md](03-功能实现/TASK_SERVER_OBJECT_SAFETY.md) | TaskServer 对象安全性 |

### SDP 和媒体
| 文档 | 说明 |
|------|------|
| [SDP_PARSER_NATIVE.md](03-功能实现/SDP_PARSER_NATIVE.md) | C 扩展原生 SDP 解析 |
| [SDP_PARSER_MIGRATION.md](03-功能实现/SDP_PARSER_MIGRATION.md) | SDP 解析迁移指南 |
| [SDP_PARSER_DELIVERY.md](03-功能实现/SDP_PARSER_DELIVERY.md) | SDP 解析交付说明 |

### 扩展功能
| 文档 | 说明 |
|------|------|
| [语音对讲.md](03-功能实现/语音对讲.md) | 语音对讲功能设计 |
| [DEVICE_CONFIG_EXTEND.md](03-功能实现/DEVICE_CONFIG_EXTEND.md) | 设备配置扩展方案 |
| [SUBSCRIBE_FEATURE_STATUS.md](03-功能实现/SUBSCRIBE_FEATURE_STATUS.md) | SUBSCRIBE 订阅功能状态 |

---

## 🚧 04-待开发功能

待开发任务清单和新功能规划：

### 🎯 核心待办
| 文档 | 说明 | 工作量 |
|------|------|--------|
| ⚡ [C_EXTENSION_TODO.md](04-待开发功能/C_EXTENSION_TODO.md) | **C 扩展层待开发任务** | 20-30 工作日 |
| ⚡ [PHP_GATEWAY_TODO.md](04-待开发功能/PHP_GATEWAY_TODO.md) | **PHP 网关层待开发任务** | 30-45 工作日 |

### GB28181-2022 支持
| 文档 | 说明 |
|------|------|
| [扩展2022版本国标协议方案.md](04-待开发功能/扩展2022版本国标协议方案.md) | 2022 版本协议扩展方案 |
| [GB28181_PRESET_AND_2022_GUIDE.md](04-待开发功能/GB28181_PRESET_AND_2022_GUIDE.md) | 预置位和 2022 版本指南 |
| [PRESET_2022_SUMMARY.md](04-待开发功能/PRESET_2022_SUMMARY.md) | 2022 版本新特性总结 |

### 功能扩展
| 文档 | 说明 |
|------|------|
| [国标设备扩展功能-订阅.md](04-待开发功能/国标设备扩展功能-订阅.md) | 订阅功能（Catalog/Alarm/Position） |
| [国标设备扩展功能-混合.md](04-待开发功能/国标设备扩展功能-混合.md) | 设备配置混合模式 |

---

## 🚀 05-部署运维

编译、部署和运维相关文档：

| 文档 | 说明 |
|------|------|
| [QUICKSTART.md](05-部署运维/QUICKSTART.md) | 快速开始指南 |
| [BUILD_CENTOS.md](05-部署运维/BUILD_CENTOS.md) | CentOS 构建说明 |
| [CENTOS_COMPILE.md](05-部署运维/CENTOS_COMPILE.md) | CentOS 详细编译步骤 |
| [PATCH_EXOSIP_5.3.0.md](05-部署运维/PATCH_EXOSIP_5.3.0.md) | eXosip2 5.3.0 补丁 |
| [PLATFORM_SUPPORT.md](05-部署运维/PLATFORM_SUPPORT.md) | 平台支持情况 |

---

## 🔧 06-问题修复

Bug 修复记录和问题排查文档：

| 文档 | 说明 |
|------|------|
| [BUG_FIX_SUMMARY.md](06-问题修复/BUG_FIX_SUMMARY.md) | Bug 修复总结 |
| [API_FIX_SUMMARY.md](06-问题修复/API_FIX_SUMMARY.md) | API 修复总结 |
| [FIX_VALIDATION.md](06-问题修复/FIX_VALIDATION.md) | 修复验证记录 |
| [CRITICAL_FIX_OSIP_LIST.md](06-问题修复/CRITICAL_FIX_OSIP_LIST.md) | osip_list 关键修复 |
| [ACTIVE_COMMAND_FLOW.md](06-问题修复/ACTIVE_COMMAND_FLOW.md) | 主动命令流程问题修复 |

---

## 📖 07-开发指南

开发规范和入门指南：

| 文档 | 说明 |
|------|------|
| [README.md](07-开发指南/README.md) | 开发者入门指南 |

---

## 📊 开发进度

项目开发进度跟踪：

| 文档 | 说明 |
|------|------|
| [信令网关核心功能清单.md](开发进度/信令网关核心功能清单.md) | 核心功能完成情况 |
| [GB28181生产就绪度分析.md](开发进度/GB28181生产就绪度分析.md) | 生产环境就绪度评估 |
| [TCP_MODE_FIX.md](开发进度/TCP_MODE_FIX.md) | TCP 模式修复进展 |
| [11-30最近进展.md](开发进度/11-30最近进展.md) | 11月30日进展 |
| [12-01进展.md](开发进度/12-01进展.md) | 12月1日进展 |
| [12-10进展.md](开发进度/12-10进展.md) | 12月10日进展 |

---

## 🔍 参考代码

外部项目参考代码：

- **ak-stream/**: AKStream 项目相关代码片段（C#）
- **GB28181-Service/**: GB28181-Service 项目参考（C++）
- **zlm/**: ZLMediaKit 相关配置和文档

---

## 📌 重要提示

### ⚡ 当前优先级最高的任务

1. **设备配置扩展完整实现** (P0)
   - 字符集编码处理
   - 通道类型过滤
   - media_host 在 SDP 中使用
   - API 配置同步

2. **SUBSCRIBE/NOTIFY 完整实现** (P0)
   - Catalog 订阅（目录变更）
   - Alarm 订阅（报警事件）
   - MobilePosition 订阅（位置上报）
   - 订阅自动刷新机制

3. **C 扩展 SUBSCRIBE 支持** (P0)
   - SUBSCRIBE 请求发送
   - NOTIFY 请求接收和解析
   - 订阅状态管理

### 📅 预计工作量

- **C 扩展层**: 20-30 工作日
- **PHP 网关层**: 30-45 工作日
- **总计**: 约 2-3 个月（单人开发）

详见：
- [C_EXTENSION_TODO.md](04-待开发功能/C_EXTENSION_TODO.md)
- [PHP_GATEWAY_TODO.md](04-待开发功能/PHP_GATEWAY_TODO.md)

---

## 📚 相关资源

- **GB28181 标准**: GB/T 28181-2016 和 GB/T 28181-2022
- **eXosip2**: http://savannah.nongnu.org/projects/exosip
- **osip2**: http://www.gnu.org/software/osip/
- **ZLMediaKit**: https://github.com/ZLMediaKit/ZLMediaKit
- **Workerman**: https://www.workerman.net/
- **Webman**: https://www.workerman.net/doc/webman/

---

## 🤝 贡献

欢迎提交 Issue 和 Pull Request！

文档更新日期: 2026-01-07
