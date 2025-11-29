<?php

/**
 * GB28181 设备类
 * 模拟一个完整的 GB28181 设备，包括注册、心跳、命令响应、媒体会话等
 */
class Device
{
    // 设备基本信息
    public string $id;                    // 设备 ID
    public string $name;                  // 设备名称
    public string $manufacturer;          // 厂商
    public string $model;                 // 型号
    public string $ip;                    // 设备 IP
    public int $port;                     // 设备端口
    public string $transport = 'UDP';     // 传输协议
    
    // 服务器信息
    public string $serverIp;
    public int $serverPort;
    public string $serverId;
    public string $realm;
    public string $password = '';
    
    // 通道列表
    public array $channels = [];          // ChannelInfo[]
    
    // 配置参数
    public int $heartbeatInterval = 60;   // 心跳间隔（秒）
    public int $expires = 3600;           // 注册有效期
    public bool $closeAllWhenBye = false; // BYE 时是否关闭所有会话
    
    // 运行状态
    private ?ExoSipClient $client = null;
    private bool $running = false;
    private bool $registered = false;
    private int $lastHeartbeat = 0;
    private int $sn = 1;                  // 序列号
    
    // 会话管理
    private array $sessions = [];         // [did => Session]
    
    // 回调函数
    private $onRegistered = null;
    private $onUnregistered = null;
    private $onMessage = null;
    
    /**
     * 构造函数
     */
    public function __construct(array $config)
    {
        // 设备信息
        $this->id = $config['device_id'];
        $this->name = $config['name'] ?? 'PHP-Device';
        $this->manufacturer = $config['manufacturer'] ?? 'PHP-Manufacturer';
        $this->model = $config['model'] ?? 'PHP-Model';
        $this->ip = $config['ip'] ?? $this->getLocalIp();
        $this->port = $config['port'] ?? 0; // 0 = 自动分配
        $this->transport = strtoupper($config['transport'] ?? 'UDP');
        
        // 服务器信息
        $this->serverIp = $config['server_ip'];
        $this->serverPort = $config['server_port'];
        $this->serverId = $config['server_id'];
        $this->realm = $config['realm'];
        $this->password = $config['password'] ?? '';
        
        // 配置参数
        $this->heartbeatInterval = $config['heartbeat_interval'] ?? 60;
        $this->expires = $config['expires'] ?? 3600;
        $this->closeAllWhenBye = $config['close_all_when_bye'] ?? false;
        
        // 通道列表
        $this->channels = $config['channels'] ?? [];
    }
    
    /**
     * 启动设备
     */
    public function start(): bool
    {
        if ($this->running) {
            return true;
        }
        
        try {
            // 创建 SIP 客户端
            $this->client = new ExoSipClient([
                'server_ip' => $this->serverIp,
                'server_port' => $this->serverPort,
                'local_ip' => $this->ip,
                'local_port' => $this->port,
                'username' => $this->id,
                'password' => $this->password,
                'realm' => $this->realm,
                'mode' => $this->transport,
                'expires' => $this->expires,
                'debug' => false
            ]);
            
            $this->log("设备启动: {$this->id}");
            
            // 发送注册
            $rid = $this->client->sendRegister();
            if ($rid < 0) {
                $this->log("注册失败: rid={$rid}", 'ERROR');
                return false;
            }
            
            $this->running = true;
            $this->log("注册请求已发送 (rid={$rid})");
            
            return true;
            
        } catch (Exception $e) {
            $this->log("启动失败: " . $e->getMessage(), 'ERROR');
            return false;
        }
    }
    
    /**
     * 停止设备
     */
    public function stop(): void
    {
        if (!$this->running) {
            return;
        }
        
        $this->running = false;
        
        // 停止所有会话
        foreach ($this->sessions as $session) {
            $session->stop();
        }
        $this->sessions = [];
        
        // 发送注销
        if ($this->client && $this->registered) {
            $this->client->sendUnregister();
            $this->log("已发送注销请求");
        }
        
        $this->registered = false;
        $this->log("设备已停止");
    }
    
    /**
     * 主处理循环（需要定期调用）
     */
    public function process(): void
    {
        if (!$this->running || !$this->client) {
            return;
        }
        
        // 处理 SIP 事件
        $events = $this->client->processEvents(50);
        
        foreach ($events as $event) {
            $this->handleEvent($event);
        }
        
        // 发送心跳
        if ($this->registered) {
            $now = time();
            if ($now - $this->lastHeartbeat >= $this->heartbeatInterval) {
                $this->sendHeartbeat();
                $this->lastHeartbeat = $now;
            }
        }
    }
    
    /**
     * 处理 SIP 事件
     */
    private function handleEvent(array $event): void
    {
        $type = $event['type'] ?? 0;
        
        switch ($type) {
            case EXOSIP_REGISTRATION_SUCCESS:
                $this->onRegistrationSuccess($event);
                break;
                
            case EXOSIP_REGISTRATION_FAILURE:
                $this->onRegistrationFailure($event);
                break;
                
            case EXOSIP_MESSAGE_NEW:
                $this->onMessageNew($event);
                break;
                
            case EXOSIP_CALL_INVITE:
                $this->onCallInvite($event);
                break;
                
            case EXOSIP_CALL_ACK:
                $this->onCallAck($event);
                break;
                
            case EXOSIP_CALL_CLOSED:
                $this->onCallClosed($event);
                break;
                
            case EXOSIP_CALL_MESSAGE_NEW:
                $this->onCallMessageNew($event);
                break;
                
            case EXOSIP_IN_SUBSCRIPTION_NEW:
                $this->onInSubscriptionNew($event);
                break;
        }
    }
    
    /**
     * 注册成功
     */
    private function onRegistrationSuccess(array $event): void
    {
        if (!$this->registered) {
            $this->registered = true;
            $this->lastHeartbeat = time();
            $this->log("注册成功");
            
            if ($this->onRegistered) {
                call_user_func($this->onRegistered, $this);
            }
        }
    }
    
    /**
     * 注册失败
     */
    private function onRegistrationFailure(array $event): void
    {
        $code = $event['status_code'] ?? 0;
        $reason = $event['reason'] ?? 'Unknown';
        $this->registered = false;
        $this->log("注册失败: {$code} {$reason}", 'ERROR');
        
        if ($this->onUnregistered) {
            call_user_func($this->onUnregistered, $this, $code, $reason);
        }
    }
    
    /**
     * 收到 MESSAGE 请求（命令）
     */
    private function onMessageNew(array $event): void
    {
        // 解析 XML 消息体（假设在事件中有 body 字段）
        // 实际需要从 event 中提取消息体
        $this->log("收到 MESSAGE 请求");
        
        // TODO: 解析 XML 并处理不同的命令
        // - Catalog: 目录查询
        // - DeviceInfo: 设备信息查询
        // - RecordInfo: 录像查询
        // - DeviceControl: 设备控制
        
        if ($this->onMessage) {
            call_user_func($this->onMessage, $this, $event);
        }
    }
    
    /**
     * 收到 INVITE 请求（媒体会话）
     */
    private function onCallInvite(array $event): void
    {
        $did = $event['did'] ?? 0;
        $tid = $event['tid'] ?? 0;
        
        $this->log("收到 INVITE (did={$did}, tid={$tid})");
        
        // 检查是否已存在会话
        if (isset($this->sessions[$did])) {
            $this->log("会话已存在 (did={$did})", 'WARNING');
            return;
        }
        
        // TODO: 解析 SDP，创建 Session
        // 这里需要从 event 中提取 SDP 信息
        
        $session = new Session([
            'device' => $this,
            'did' => $did,
            'tid' => $tid,
        ]);
        
        $this->sessions[$did] = $session;
        
        // TODO: 回复 200 OK with SDP
    }
    
    /**
     * 收到 ACK（开始推流）
     */
    private function onCallAck(array $event): void
    {
        $did = $event['did'] ?? 0;
        $this->log("收到 ACK (did={$did})，开始推流");
        
        if (isset($this->sessions[$did])) {
            $this->sessions[$did]->start();
        }
    }
    
    /**
     * 收到 BYE（结束推流）
     */
    private function onCallClosed(array $event): void
    {
        $did = $event['did'] ?? 0;
        $this->log("收到 BYE (did={$did})，结束推流");
        
        if ($this->closeAllWhenBye) {
            // 关闭所有会话
            foreach ($this->sessions as $session) {
                $session->stop();
            }
            $this->sessions = [];
        } else {
            // 只关闭指定会话
            if (isset($this->sessions[$did])) {
                $this->sessions[$did]->stop();
                unset($this->sessions[$did]);
            }
        }
    }
    
    /**
     * 收到 INFO 消息（播放控制）
     */
    private function onCallMessageNew(array $event): void
    {
        $did = $event['did'] ?? 0;
        $this->log("收到 INFO (did={$did})");
        
        // TODO: 解析 INFO 消息体（PAUSE、PLAY、SCALE 等）
        if (isset($this->sessions[$did])) {
            // 处理播放控制
        }
    }
    
    /**
     * 收到订阅请求（如移动位置订阅）
     */
    private function onInSubscriptionNew(array $event): void
    {
        $this->log("收到订阅请求");
        // TODO: 处理订阅
    }
    
    /**
     * 发送心跳
     */
    private function sendHeartbeat(): void
    {
        $xml = $this->generateKeepaliveXml();
        $to = "sip:{$this->serverId}@{$this->realm}";
        
        $result = $this->client->sendMessage($to, $xml, 'Application/MANSCDP+xml');
        
        if ($result >= 0) {
            $this->log("心跳已发送", 'DEBUG');
        } else {
            $this->log("心跳发送失败", 'WARNING');
        }
    }
    
    /**
     * 生成心跳 XML
     */
    private function generateKeepaliveXml(): string
    {
        return <<<XML
<?xml version="1.0" encoding="GB2312"?>
<Notify>
<CmdType>Keepalive</CmdType>
<SN>{$this->getNextSn()}</SN>
<DeviceID>{$this->id}</DeviceID>
<Status>OK</Status>
</Notify>
XML;
    }
    
    /**
     * 生成目录 XML
     */
    public function generateCatalogXml(int $sn): string
    {
        $channelCount = count($this->channels);
        
        $xml = new SimpleXMLElement('<?xml version="1.0" encoding="GB2312"?><Response></Response>');
        $xml->addChild('CmdType', 'Catalog');
        $xml->addChild('SN', $sn);
        $xml->addChild('DeviceID', $this->id);
        $xml->addChild('SumNum', $channelCount);
        
        $deviceList = $xml->addChild('DeviceList');
        $deviceList->addAttribute('Num', $channelCount);
        
        foreach ($this->channels as $channel) {
            $item = $deviceList->addChild('Item');
            $item->addChild('DeviceID', $channel['id']);
            $item->addChild('Name', $channel['name']);
            $item->addChild('Manufacturer', $this->manufacturer);
            $item->addChild('Model', $this->model);
            $item->addChild('Status', 'ON');
        }
        
        return $xml->asXML();
    }
    
    /**
     * 生成设备信息 XML
     */
    public function generateDeviceInfoXml(int $sn): string
    {
        return <<<XML
<?xml version="1.0" encoding="GB2312"?>
<Response>
<CmdType>DeviceInfo</CmdType>
<SN>{$sn}</SN>
<DeviceID>{$this->id}</DeviceID>
<Result>OK</Result>
<DeviceName>{$this->name}</DeviceName>
<Manufacturer>{$this->manufacturer}</Manufacturer>
<Model>{$this->model}</Model>
<Firmware>v1.0.0</Firmware>
<Channel>" . count($this->channels) . "</Channel>
</Response>
XML;
    }
    
    /**
     * 设置注册成功回调
     */
    public function onRegistered(callable $callback): void
    {
        $this->onRegistered = $callback;
    }
    
    /**
     * 设置注销回调
     */
    public function onUnregistered(callable $callback): void
    {
        $this->onUnregistered = $callback;
    }
    
    /**
     * 设置消息回调
     */
    public function onMessage(callable $callback): void
    {
        $this->onMessage = $callback;
    }
    
    /**
     * 获取下一个序列号
     */
    private function getNextSn(): int
    {
        $this->sn++;
        if ($this->sn > 99999999) {
            $this->sn = 1;
        }
        return $this->sn;
    }
    
    /**
     * 获取本地 IP
     */
    private function getLocalIp(): string
    {
        // 简单实现，实际应该更智能
        return gethostbyname(gethostname());
    }
    
    /**
     * 日志
     */
    private function log(string $message, string $level = 'INFO'): void
    {
        $time = date('Y-m-d H:i:s');
        echo "[{$time}] [{$level}] [Device:{$this->id}] {$message}\n";
    }
    
    /**
     * 获取设备状态
     */
    public function getStatus(): array
    {
        return [
            'id' => $this->id,
            'name' => $this->name,
            'running' => $this->running,
            'registered' => $this->registered,
            'sessions' => count($this->sessions),
            'last_heartbeat' => $this->lastHeartbeat,
        ];
    }
}

/**
 * 会话类（媒体会话）
 */
class Session
{
    public Device $device;
    public int $did;           // Dialog ID
    public int $tid;           // Transaction ID
    public string $targetIp = '';
    public int $targetPort = 0;
    public int $localPort = 0;
    public string $ssrc = '';
    public string $channelId = '';
    public string $playMode = 'realtime'; // realtime, playback, download
    public bool $used = false;
    public bool $paused = false;
    
    public function __construct(array $config)
    {
        $this->device = $config['device'];
        $this->did = $config['did'];
        $this->tid = $config['tid'];
    }
    
    /**
     * 开始推流
     */
    public function start(): void
    {
        if ($this->used) {
            return;
        }
        
        $this->used = true;
        $this->device->log("开始推流 (did={$this->did})", 'INFO');
        
        // TODO: 实际的推流逻辑
        // 根据 playMode 决定是实时流还是回放
    }
    
    /**
     * 停止推流
     */
    public function stop(): void
    {
        if (!$this->used) {
            return;
        }
        
        $this->used = false;
        $this->device->log("停止推流 (did={$this->did})", 'INFO');
        
        // TODO: 停止推流
    }
    
    /**
     * 暂停/继续
     */
    public function pause(bool $pause): void
    {
        $this->paused = $pause;
        $this->device->log(($pause ? "暂停" : "继续") . "推流 (did={$this->did})", 'INFO');
        
        // TODO: 暂停/继续推流
    }
    
    private function log(string $message, string $level = 'INFO'): void
    {
        $this->device->log("[Session:{$this->did}] {$message}", $level);
    }
}