<?php

require_once __DIR__ . '/Device.php';

/**
 * 设备管理器 - 管理 GB28181 服务器端设备
 *
 * 功能：
 * - 设备注册/注销监控
 * - 心跳超时检测
 * - 设备状态统计
 * - 设备信息管理
 */
class DeviceManager
{
    // 设备对象列表 (服务器端 - 管理连接进来的设备)
    // [deviceId => Device]
    private array $devices = [];

    // TCP 连接管理 (仅 TCP 模式使用)
    // [fd => deviceId] 和 [deviceId => fd] 双向映射
    private array $fdToDevice = [];
    private array $deviceToFd = [];

    private int $heartbeatTimeout = 180;  // 3分钟心跳超时（GB28181建议值）
    private int $checkInterval = 30;      // 30秒检查一次
    private int $lastCheckTime = 0;

    /**
     * 构造函数
     *
     * @param int $heartbeatTimeout 心跳超时时间（秒）
     * @param int $checkInterval 检查间隔（秒）
     */
    public function __construct(int $heartbeatTimeout = 180, int $checkInterval = 30)
    {
        $this->heartbeatTimeout = $heartbeatTimeout;
        $this->checkInterval = $checkInterval;
        $this->lastCheckTime = time();

        $this->log("DeviceManager 已初始化");
    }

    /**
     * 添加新设备（从 REGISTER 事件创建）
     *
     * @param string $deviceId 设备ID
     * @param array $info 设备初始信息 ['uri', 'ip', 'port', 'registered_at', 'expires']
     * @return Device
     */
    public function addDevice(string $deviceId, array $info): Device
    {
        if (isset($this->devices[$deviceId])) {
            throw new \Exception("设备已存在: {$deviceId}");
        }

        $device = new Device($deviceId, $info);
        $device->markRegistered();
        $this->devices[$deviceId] = $device;

        $this->log("设备已添加: {$deviceId}");

        return $device;
    }

    /**
     * 移除设备
     */
    public function removeDevice(string $deviceId): void
    {
        unset($this->devices[$deviceId]);
        $this->log("设备已移除: {$deviceId}");
    }

    /**
     * 更新设备信息（只更新已存在的设备）
     *
     * @param string $deviceId 设备ID
     * @param array $info 要更新的信息 ['uri', 'ip', 'port', 'registered_at', 'expires', 'info']
     * @return bool 是否更新成功
     */
    public function updateDeviceInfo(string $deviceId, array $info): bool
    {
        $device = $this->devices[$deviceId] ?? null;
        if (!$device) {
            $this->log("设备不存在，无法更新: {$deviceId}", 'WARNING');
            return false;
        }

        // 更新设备属性
        if (isset($info['uri'])) $device->uri = $info['uri'];
        if (isset($info['ip'])) $device->ip = $info['ip'];
        if (isset($info['received_ip'])) $device->received_ip = $info['received_ip'];
        if (isset($info['received_port'])) $device->received_port = $info['received_port'];
        if (isset($info['port'])) $device->port = $info['port'];
        if (isset($info['registered_at'])) $device->registeredAt = $info['registered_at'];
        if (isset($info['expires'])) $device->expires = $info['expires'];
        if (isset($info['info'])) $device->updateInfo($info['info']);

        return true;
    }

    /**
     * 获取设备信息（服务器端使用）
     * @return array|null 返回设备信息数组，兼容旧代码
     */
    public function getDevice(string $deviceId): ?array
    {
        $device = $this->devices[$deviceId] ?? null;
        return $device ? $device->toArray() : null;
    }

    /**
     * 获取设备对象
     * @return Device|null
     */
    public function getDeviceObject(string $deviceId): ?Device
    {
        return $this->devices[$deviceId] ?? null;
    }

    /**
     * 更新设备心跳（由 Device 对象内部调用）
     * 保留此方法用于外部监控
     */
    public function recordHeartbeat(string $deviceId): void
    {
        $device = $this->devices[$deviceId] ?? null;
        if ($device) {
            $wasTimeout = $device->status === 'timeout';
            $device->recordHeartbeat();

            if ($wasTimeout) {
                $this->log("设备恢复在线: {$deviceId}");
            }
        }
    }

    /**
     * 更新设备心跳（兼容旧接口）
     *
     * @param string $deviceId 设备ID
     */
    public function updateHeartbeat(string $deviceId): void
    {
        $this->recordHeartbeat($deviceId);
    }

    /**
     * 检查超时设备（定期调用）
     *
     * @return array 超时的设备列表
     */
    public function checkTimeout(): array
    {
        $now = time();

        // 控制检查频率
        if ($now - $this->lastCheckTime < $this->checkInterval) {
            return [];
        }

        $this->lastCheckTime = $now;
        $timeoutDevices = [];

        foreach ($this->devices as $deviceId => $device) {
            if ($device->isTimeout($this->heartbeatTimeout)) {
                $device->markTimeout();
                $timeSinceHeartbeat = $now - $device->lastHeartbeat;

                $this->log("设备心跳超时: {$deviceId}, 距离上次心跳: {$timeSinceHeartbeat}秒", 'ERROR');
                $timeoutDevices[] = $device->toArray();
            } // 预警（超过一半时间没心跳）
            else if ($device->isOnline()) {
                $timeSinceHeartbeat = $now - $device->lastHeartbeat;
                if ($timeSinceHeartbeat > $this->heartbeatTimeout / 2) {
                    $this->log("设备心跳预警: {$deviceId}, 距离上次心跳: {$timeSinceHeartbeat}秒", 'WARNING');
                }
            }
        }

        return $timeoutDevices;
    }

    /**
     * 获取所有在线设备
     *
     * @return array
     */
    public function getOnlineDevices(): array
    {
        return array_map(
            fn($device) => $device->toArray(),
            array_filter($this->devices, fn($device) => $device->isOnline())
        );
    }

    /**
     * 获取设备状态信息
     *
     * @param string $deviceId
     * @return array|null
     */
    public function getDeviceStatus(string $deviceId): ?array
    {
        $device = $this->devices[$deviceId] ?? null;
        return $device ? $device->toArray() : null;
    }

    /**
     * 获取所有设备列表
     *
     * @return array
     */
    public function getAllDevices(): array
    {
        return array_map(fn($device) => $device->toArray(), $this->devices);
    }

    /**
     * 获取设备统计
     *
     * @return array
     */
    public function getStats(): array
    {
        $created = 0;
        $starting = 0;
        $online = 0;
        $offline = 0;
        $timeout = 0;
        $stopped = 0;

        foreach ($this->devices as $device) {
            switch ($device->status) {
                case 'created':
                    $created++;
                    break;
                case 'starting':
                    $starting++;
                    break;
                case 'online':
                    $online++;
                    break;
                case 'offline':
                    $offline++;
                    break;
                case 'timeout':
                    $timeout++;
                    break;
                case 'stopped':
                    $stopped++;
                    break;
            }
        }

        return [
            'total' => count($this->devices),
            'created' => $created,
            'starting' => $starting,
            'online' => $online,
            'offline' => $offline,
            'timeout' => $timeout,
            'stopped' => $stopped,
        ];
    }

    /**
     * 清理长时间离线的设备
     *
     * @param int $days 离线多少天后清理
     * @return int 清理的数量
     */
    public function cleanupOfflineDevices(int $days = 7): int
    {
        $threshold = time() - ($days * 86400);
        $count = 0;

        foreach ($this->devices as $deviceId => $device) {
            if ($device->status === 'stopped' || $device->status === 'offline') {
                $lastTime = $device->registerTime;

                if ($lastTime > 0 && $lastTime < $threshold) {
                    $this->removeDevice($deviceId);
                    $count++;
                }
            }
        }

        if ($count > 0) {
            $this->log("清理 {$count} 个长期离线设备");
        }

        return $count;
    }

    // ========================================
    // TCP 连接管理 (Task 3)
    // ========================================

    /**
     * 绑定设备到 TCP 连接
     *
     * @param string $deviceId 设备ID
     * @param int $fd 文件描述符
     */
    public function bindConnection(string $deviceId, int $fd): void
    {
        // 如果该 fd 已经绑定了其他设备,先解绑
        if (isset($this->fdToDevice[$fd])) {
            $oldDeviceId = $this->fdToDevice[$fd];
            if ($oldDeviceId !== $deviceId) {
                $this->log("TCP连接 fd={$fd} 从设备 {$oldDeviceId} 重新绑定到 {$deviceId}", 'WARNING');
                unset($this->deviceToFd[$oldDeviceId]);
            }
        }

        // 如果该设备已经绑定了其他 fd,先解绑
        if (isset($this->deviceToFd[$deviceId])) {
            $oldFd = $this->deviceToFd[$deviceId];
            if ($oldFd !== $fd) {
                $this->log("设备 {$deviceId} 从 fd={$oldFd} 重新绑定到 fd={$fd}", 'WARNING');
                unset($this->fdToDevice[$oldFd]);
            }
        }

        $this->fdToDevice[$fd] = $deviceId;
        $this->deviceToFd[$deviceId] = $fd;

        $this->log("TCP连接已绑定: device={$deviceId}, fd={$fd}");
    }

    /**
     * 解绑设备的 TCP 连接
     *
     * @param string $deviceId 设备ID
     */
    public function unbindConnectionByDevice(string $deviceId): void
    {
        if (isset($this->deviceToFd[$deviceId])) {
            $fd = $this->deviceToFd[$deviceId];
            unset($this->fdToDevice[$fd]);
            unset($this->deviceToFd[$deviceId]);

            $this->log("TCP连接已解绑: device={$deviceId}, fd={$fd}");
        }
    }

    /**
     * 解绑 TCP 连接(通过 fd)
     *
     * @param int $fd 文件描述符
     */
    public function unbindConnectionByFd(int $fd): void
    {
        if (isset($this->fdToDevice[$fd])) {
            $deviceId = $this->fdToDevice[$fd];
            unset($this->deviceToFd[$deviceId]);
            unset($this->fdToDevice[$fd]);

            $this->log("TCP连接已断开: device={$deviceId}, fd={$fd}");

            // 标记设备离线
            $device = $this->getDeviceObject($deviceId);
            if ($device) {
                $device->markOffline();
                $this->log("设备因TCP断开而离线: {$deviceId}");
            }
        }
    }

    /**
     * 通过设备ID获取文件描述符
     *
     * @param string $deviceId 设备ID
     * @return int|null 文件描述符,如果未绑定返回 null
     */
    public function getFdByDevice(string $deviceId): ?int
    {
        return $this->deviceToFd[$deviceId] ?? null;
    }

    /**
     * 通过文件描述符获取设备ID
     *
     * @param int $fd 文件描述符
     * @return string|null 设备ID,如果未绑定返回 null
     */
    public function getDeviceByFd(int $fd): ?string
    {
        return $this->fdToDevice[$fd] ?? null;
    }

    /**
     * 获取所有 TCP 连接信息
     *
     * @return array ['device_id' => fd, ...]
     */
    public function getAllConnections(): array
    {
        return $this->deviceToFd;
    }

    /**
     * 检查设备是否有活跃的 TCP 连接
     *
     * @param string $deviceId 设备ID
     * @return bool
     */
    public function hasConnection(string $deviceId): bool
    {
        return isset($this->deviceToFd[$deviceId]);
    }

    /**
     * 日志输出
     */
    private function log(string $message, string $level = 'INFO')
    {
        $time = date('Y-m-d H:i:s');
        $prefix = match ($level) {
            'ERROR' => '[ERROR]',
            'WARNING' => '[WARNING]',
            default => '[INFO]'
        };

        echo "[{$time}]  [{$level}] {$message}\n";
    }
}