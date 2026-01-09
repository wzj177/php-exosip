<?php
/**
 * GB28181-2022 新功能和预置位操作示例
 * 
 * 这些方法应该添加到您的 Gb28181Service 类中
 */

class Gb28181ServiceExtension
{
    private $sipClient;  // SIP 客户端实例
    
    /**
     * 构建预置位命令
     * 
     * GB28181 预置位命令格式: A5 0F 01 [指令码] 00 [预置位编号] 00 [校验码]
     * - 0x81: 设置预置位
     * - 0x82: 调用预置位
     * - 0x83: 删除预置位
     */
    private function buildPresetCommand(string $action, int $presetId): string
    {
        $cmdCode = match($action) {
            'set' => 0x81,     // 设置预置位
            'call' => 0x82,    // 调用预置位
            'delete' => 0x83,  // 删除预置位
            default => 0x00
        };

        // 预置位ID范围: 1-255
        $presetId = max(1, min(255, $presetId));

        // 计算校验码
        $checksum = (0xA5 + 0x0F + 0x01 + $cmdCode + 0x00 + $presetId + 0x00) % 0x100;

        // 构建命令: A5 0F 01 [cmdCode] 00 [presetId] 00 [checksum]
        $presetCmd = sprintf("A50F01%02X00%02X00%02X", $cmdCode, $presetId, $checksum);

        return $presetCmd;
    }

    /**
     * 设置预置位
     * 
     * @param string $deviceId 设备ID (20位)
     * @param string $channelId 通道ID (20位)
     * @param int $presetId 预置位编号 (1-255)
     * @return array 操作结果
     */
    public function presetSet(string $deviceId, string $channelId, int $presetId): array
    {
        $ptzCmd = $this->buildPresetCommand('set', $presetId);
        
        // 构建 PTZ XML
        $sn = rand(1, 99999999);
        $xml = "<?xml version=\"1.0\" encoding=\"GB2312\"?>\r\n";
        $xml .= "<Control>\r\n";
        $xml .= "<CmdType>DeviceControl</CmdType>\r\n";
        $xml .= "<SN>{$sn}</SN>\r\n";
        $xml .= "<DeviceID>{$channelId}</DeviceID>\r\n";
        $xml .= "<PTZCmd>{$ptzCmd}</PTZCmd>\r\n";
        $xml .= "</Control>";

        // 获取设备信息
        $device = $this->getDeviceInfo($deviceId);
        if (!$device) {
            return ['success' => false, 'message' => 'Device not found'];
        }

        $targetUri = "sip:{$channelId}@{$device['ip']}:{$device['port']}";
        $result = $this->sipClient->sendMessage($targetUri, $xml, 'Application/MANSCDP+xml');

        return [
            'success' => $result,
            'message' => $result ? "Preset {$presetId} set successfully" : 'Failed to send command',
            'preset_id' => $presetId,
            'sn' => $sn
        ];
    }

    /**
     * 调用预置位
     */
    public function presetCall(string $deviceId, string $channelId, int $presetId): array
    {
        $ptzCmd = $this->buildPresetCommand('call', $presetId);
        
        $sn = rand(1, 99999999);
        $xml = "<?xml version=\"1.0\" encoding=\"GB2312\"?>\r\n";
        $xml .= "<Control>\r\n";
        $xml .= "<CmdType>DeviceControl</CmdType>\r\n";
        $xml .= "<SN>{$sn}</SN>\r\n";
        $xml .= "<DeviceID>{$channelId}</DeviceID>\r\n";
        $xml .= "<PTZCmd>{$ptzCmd}</PTZCmd>\r\n";
        $xml .= "</Control>";

        $device = $this->getDeviceInfo($deviceId);
        if (!$device) {
            return ['success' => false, 'message' => 'Device not found'];
        }

        $targetUri = "sip:{$channelId}@{$device['ip']}:{$device['port']}";
        $result = $this->sipClient->sendMessage($targetUri, $xml, 'Application/MANSCDP+xml');

        return [
            'success' => $result,
            'message' => $result ? "Preset {$presetId} called successfully" : 'Failed to send command',
            'preset_id' => $presetId,
            'sn' => $sn
        ];
    }

    /**
     * 删除预置位
     */
    public function presetDelete(string $deviceId, string $channelId, int $presetId): array
    {
        $ptzCmd = $this->buildPresetCommand('delete', $presetId);
        
        $sn = rand(1, 99999999);
        $xml = "<?xml version=\"1.0\" encoding=\"GB2312\"?>\r\n";
        $xml .= "<Control>\r\n";
        $xml .= "<CmdType>DeviceControl</CmdType>\r\n";
        $xml .= "<SN>{$sn}</SN>\r\n";
        $xml .= "<DeviceID>{$channelId}</DeviceID>\r\n";
        $xml .= "<PTZCmd>{$ptzCmd}</PTZCmd>\r\n";
        $xml .= "</Control>";

        $device = $this->getDeviceInfo($deviceId);
        if (!$device) {
            return ['success' => false, 'message' => 'Device not found'];
        }

        $targetUri = "sip:{$channelId}@{$device['ip']}:{$device['port']}";
        $result = $this->sipClient->sendMessage($targetUri, $xml, 'Application/MANSCDP+xml');

        return [
            'success' => $result,
            'message' => $result ? "Preset {$presetId} deleted successfully" : 'Failed to send command',
            'preset_id' => $presetId,
            'sn' => $sn
        ];
    }

    /**
     * GB28181-2022: 设备升级
     * 
     * @param string $deviceId 设备ID
     * @param string $manufacturer 制造商
     * @param string $firmware 固件版本
     * @return array 操作结果
     */
    public function deviceUpgrade(string $deviceId, string $manufacturer, string $firmware): array
    {
        $sn = rand(1, 99999999);
        $xml = "<?xml version=\"1.0\" encoding=\"GB2312\"?>\r\n";
        $xml .= "<Control>\r\n";
        $xml .= "<CmdType>DeviceControl</CmdType>\r\n";
        $xml .= "<SN>{$sn}</SN>\r\n";
        $xml .= "<DeviceID>{$deviceId}</DeviceID>\r\n";
        $xml .= "<DeviceUpgrade>\r\n";
        $xml .= "<Manufacturer>{$manufacturer}</Manufacturer>\r\n";
        $xml .= "<Firmware>{$firmware}</Firmware>\r\n";
        $xml .= "</DeviceUpgrade>\r\n";
        $xml .= "</Control>";

        $device = $this->getDeviceInfo($deviceId);
        if (!$device) {
            return ['success' => false, 'message' => 'Device not found'];
        }

        $targetUri = "sip:{$deviceId}@{$device['ip']}:{$device['port']}";
        $result = $this->sipClient->sendMessage($targetUri, $xml, 'Application/MANSCDP+xml');

        return [
            'success' => $result,
            'message' => $result ? 'Upgrade command sent successfully' : 'Failed to send command',
            'manufacturer' => $manufacturer,
            'firmware' => $firmware,
            'sn' => $sn,
            'note' => '设备将注销、升级并重新注册'
        ];
    }

    /**
     * GB28181-2022: 图像抓拍
     * 
     * @param string $deviceId 设备ID
     * @param string $channelId 通道ID
     * @param string $imageFormat 图片格式 (JPEG/PNG/BMP)
     * @return array 操作结果
     */
    public function snapshot(string $deviceId, string $channelId, string $imageFormat = 'JPEG'): array
    {
        $sessionId = strtoupper(md5(uniqid() . microtime(true)));
        $sn = rand(1, 99999999);
        
        $xml = "<?xml version=\"1.0\" encoding=\"GB2312\"?>\r\n";
        $xml .= "<Control>\r\n";
        $xml .= "<CmdType>DeviceControl</CmdType>\r\n";
        $xml .= "<SN>{$sn}</SN>\r\n";
        $xml .= "<DeviceID>{$channelId}</DeviceID>\r\n";
        $xml .= "<Snapshot>\r\n";
        $xml .= "<SessionID>{$sessionId}</SessionID>\r\n";
        $xml .= "<ImageFormat>{$imageFormat}</ImageFormat>\r\n";
        $xml .= "</Snapshot>\r\n";
        $xml .= "</Control>";

        $device = $this->getDeviceInfo($deviceId);
        if (!$device) {
            return ['success' => false, 'message' => 'Device not found'];
        }

        $targetUri = "sip:{$channelId}@{$device['ip']}:{$device['port']}";
        $result = $this->sipClient->sendMessage($targetUri, $xml, 'Application/MANSCDP+xml');

        return [
            'success' => $result,
            'message' => $result ? 'Snapshot command sent successfully' : 'Failed to send command',
            'session_id' => $sessionId,
            'image_format' => $imageFormat,
            'sn' => $sn,
            'note' => '设备将发送 MediaStatus 通知包含图片URL'
        ];
    }

    /**
     * GB28181-2022: 处理 MediaStatus 通知
     * 
     * 设备抓拍完成后会发送此通知
     */
    public function handleMediaStatusNotify(string $xmlBody): array
    {
        $xml = simplexml_load_string($xmlBody);
        
        $result = [
            'cmd_type' => (string)$xml->CmdType,
            'sn' => (string)$xml->SN,
            'device_id' => (string)$xml->DeviceID,
            'notify_type' => (string)$xml->NotifyType,
        ];

        // SnapshotComplete 通知
        if (isset($xml->NotifyType) && (string)$xml->NotifyType === 'SnapshotComplete') {
            $result['session_id'] = (string)$xml->SessionID;
            $result['file_url'] = (string)$xml->FileURL;
        }

        // 媒体流状态通知
        if (isset($xml->SSRC)) {
            $result['ssrc'] = (string)$xml->SSRC;
            $result['bit_rate'] = (string)$xml->BitRate ?? null;
            $result['frame_rate'] = (string)$xml->FrameRate ?? null;
            $result['packet_loss'] = (string)$xml->PacketLoss ?? null;
        }

        return $result;
    }

    /**
     * GB28181-2022: 增强的设备信息查询 (包含新字段)
     */
    public function queryDeviceInfoEnhanced(string $deviceId): array
    {
        $sn = rand(1, 99999999);
        $xml = "<?xml version=\"1.0\" encoding=\"GB2312\"?>\r\n";
        $xml .= "<Query>\r\n";
        $xml .= "<CmdType>DeviceInfo</CmdType>\r\n";
        $xml .= "<SN>{$sn}</SN>\r\n";
        $xml .= "<DeviceID>{$deviceId}</DeviceID>\r\n";
        $xml .= "</Query>";

        $device = $this->getDeviceInfo($deviceId);
        if (!$device) {
            return ['success' => false, 'message' => 'Device not found'];
        }

        $targetUri = "sip:{$deviceId}@{$device['ip']}:{$device['port']}";
        $result = $this->sipClient->sendMessage($targetUri, $xml, 'Application/MANSCDP+xml');

        return [
            'success' => $result,
            'message' => $result ? 'Device info query sent' : 'Failed to send command',
            'sn' => $sn,
            'note' => '响应包含 Manufacturer, Model, Firmware 字段'
        ];
    }

    /**
     * GB28181-2022: 解析增强的设备信息响应
     */
    public function parseDeviceInfoResponse(string $xmlBody): array
    {
        $xml = simplexml_load_string($xmlBody);
        
        return [
            'cmd_type' => (string)$xml->CmdType,
            'sn' => (string)$xml->SN,
            'device_id' => (string)$xml->DeviceID,
            'result' => (string)$xml->Result,
            'device_name' => (string)$xml->DeviceName,
            'manufacturer' => (string)$xml->Manufacturer ?? '',  // 2022新增
            'model' => (string)$xml->Model ?? '',                // 2022新增
            'firmware' => (string)$xml->Firmware ?? '',          // 2022新增
            'channel' => (string)$xml->Channel ?? '',
            'status' => (string)$xml->Status ?? '',
            'info' => [
                'ptz_type' => (string)$xml->Info->PTZType ?? '',
                'position_x' => (string)$xml->Info->PositionX ?? '',
                'position_y' => (string)$xml->Info->PositionY ?? '',
            ]
        ];
    }

    /**
     * 获取设备信息的辅助方法 (需要根据实际数据库实现)
     */
    private function getDeviceInfo(string $deviceId): ?array
    {
        // TODO: 从数据库查询设备信息
        // return $this->deviceRepository->findByDeviceId($deviceId);
        return null;
    }
}

/**
 * 使用示例
 */
/*
// 1. 设置预置位
$gb28181 = new Gb28181ServiceExtension();
$result = $gb28181->presetSet('34020000001320948622', '34020000001320000001', 5);
// 命令: A50F018100050088

// 2. 调用预置位
$result = $gb28181->presetCall('34020000001320948622', '34020000001320000001', 5);
// 命令: A50F018200050089

// 3. 删除预置位
$result = $gb28181->presetDelete('34020000001320948622', '34020000001320000001', 5);
// 命令: A50F0183000500A

// 4. GB28181-2022: 设备升级
$result = $gb28181->deviceUpgrade(
    '34020000001320948622',
    'Hikvision',
    'V5.7.12_build230801'
);

// 5. GB28181-2022: 图像抓拍
$result = $gb28181->snapshot(
    '34020000001320948622',
    '34020000001320000001',
    'JPEG'
);

// 6. GB28181-2022: 查询设备信息 (包含 Manufacturer/Model/Firmware)
$result = $gb28181->queryDeviceInfoEnhanced('34020000001320948622');
*/
