<?php
/**
 * PHP ExoSip Extension Stub File
 * 
 * This file provides IDE autocomplete and type hints for the ExoSip extension.
 * DO NOT include this file in your code - it's only for IDE support.
 * 
 * @package ExoSip
 * @version 2.0.0
 * @link https://github.com/your-repo/php-exosip
 */

/**
 * Universal SIP Server Class
 * 
 * Pure OOP interface for building SIP-based applications.
 * Supports event-driven architecture with full RFC 3261 compliance.
 * 
 * @example
 * ```php
 * $sip = new ExoSip([
 *     'host' => '0.0.0.0',
 *     'port' => 5060,
 *     'mode' => 'TCP'  // TCP|UDP|ALL (case-insensitive)
 * ]);
 * 
 * $sip->onRegister = fn($event) => handleRegister($event);
 * $sip->onMessage = fn($event) => handleMessage($event);
 * $sip->run();
 * ```
 */
class ExoSip {
    
    /* ========== Core SIP Event Handlers (RFC 3261) ========== */
    
    /**
     * REGISTER event handler - Device/user registration
     * @var callable(SipEvent): void|bool|null
     */
    public $onRegister;
    
    /**
     * INVITE event handler - Session invitation
     * @var callable(SipEvent): void|bool|null
     */
    public $onInvite;
    
    /**
     * ACK event handler - Final response acknowledgment
     * @var callable(SipEvent): void|bool|null
     */
    public $onAck;
    
    /**
     * BYE event handler - Session termination
     * @var callable(SipEvent): void|bool|null
     */
    public $onBye;
    
    /**
     * CANCEL event handler - Request cancellation
     * @var callable(SipEvent): void|bool|null
     */
    public $onCancel;
    
    /**
     * OPTIONS event handler - Capability query
     * @var callable(SipEvent): void|bool|null
     */
    public $onOptions;
    
    /* ========== SIP Extension Methods ========== */
    
    /**
     * MESSAGE event handler - SIP instant messaging (RFC 3428)
     * Used in GB28181 for keepalive, device info, etc.
     * @var callable(SipEvent): void|bool|null
     */
    public $onMessage;
    
    /**
     * INFO event handler - Mid-session information (RFC 6086)
     * @var callable(SipEvent): void|bool|null
     */
    public $onInfo;
    
    /**
     * UPDATE event handler - Session modification (RFC 3311)
     * @var callable(SipEvent): void|bool|null
     */
    public $onUpdate;
    
    /**
     * PRACK event handler - Provisional response acknowledgment (RFC 3262)
     * @var callable(SipEvent): void|bool|null
     */
    public $onPrack;
    
    /**
     * REFER event handler - Call transfer (RFC 3515)
     * @var callable(SipEvent): void|bool|null
     */
    public $onRefer;
    
    /* ========== Publish-Subscribe Events (RFC 3903, RFC 3856) ========== */
    
    /**
     * SUBSCRIBE event handler - Event subscription
     * @var callable(SipEvent): void|bool|null
     */
    public $onSubscribe;
    
    /**
     * NOTIFY event handler - Event notification
     * @var callable(SipEvent): void|bool|null
     */
    public $onNotify;
    
    /**
     * PUBLISH event handler - Presence/state publication
     * @var callable(SipEvent): void|bool|null
     */
    public $onPublish;
    
    /* ========== Response and Error Handling ========== */
    
    /**
     * Response event handler - SIP responses (1xx-6xx)
     * @var callable(SipEvent): void|bool|null
     */
    public $onResponse;
    
    /**
     * Timeout event handler - Request timeout
     * @var callable(SipEvent): void|bool|null
     */
    public $onTimeout;
    
    /**
     * Error event handler - Protocol errors
     * @var callable(SipEvent): void|bool|null
     */
    public $onError;
    
    /* ========== Connection Management ========== */
    
    /**
     * Connection established handler (TCP/TLS)
     * @var callable(SipEvent): void|bool|null
     */
    public $onConnect;
    
    /**
     * Connection closed handler
     * @var callable(SipEvent): void|bool|null
     */
    public $onClose;
    
    /* ========== Master-Worker-Task Callbacks ========== */
    
    /**
     * Task handler - Execute async tasks in Task process
     * Runs in Task process, handles time-consuming operations (HTTP, DB, Redis)
     * @var callable(int $taskId, array $data): mixed
     * 
     * @example
     * ```php
     * $sip->onTask = function($taskId, $data) {
     *     $type = $data['type'] ?? 'unknown';
     *     $payload = $data['payload'] ?? [];
     *     
     *     switch ($type) {
     *         case 'webhook':
     *             $result = file_get_contents($payload['url'], false, stream_context_create([
     *                 'http' => ['method' => 'POST', 'timeout' => 5]
     *             ]));
     *             return ['success' => true, 'response' => $result];
     *             
     *         case 'save_catalog':
     *             $db = new PDO(...);
     *             $stmt = $db->prepare("INSERT INTO catalog ...");
     *             $stmt->execute($payload);
     *             return ['success' => true];
     *             
     *         default:
     *             return ['success' => false, 'error' => 'Unknown task'];
     *     }
     * };
     * ```
     */
    public $onTask;
    
    /**
     * Task finish handler - Receive task results in Worker process
     * Runs in Worker process, automatically triggered when onTask returns
     * @var callable(int $taskId, mixed $result): void
     * 
     * @example
     * ```php
     * $sip->onTaskFinish = function($taskId, $result) {
     *     if ($result['success']) {
     *         echo "Task #{$taskId} completed successfully\n";
     *     } else {
     *         echo "Task #{$taskId} failed: {$result['error']}\n";
     *     }
     * };
     * ```
     */
    public $onTaskFinish;
    
    /**
     * Timer handler - Periodic execution in Worker process
     * Runs in Worker process, triggered at configured interval
     * @var callable(): bool
     * 
     * @example
     * ```php
     * $sip->onTimer = function() use ($gb28181) {
     *     // Check device timeouts
     *     $timeoutDevices = $gb28181->processTimeouts();
     *     
     *     // Cleanup expired data
     *     $gb28181->cleanupExpiredData();
     *     
     *     return true;  // Continue timer, false to stop
     * };
     * ```
     */
    public $onTimer;
    
    /**
     * Pipe message handler - Receive messages from Task process (Task→Worker communication)
     * Runs in Worker process, triggered when Task calls sendToWorker()
     * @var callable(mixed $data): void
     * 
     * @example
     * ```php
     * $sip->onPipeMessage = function($server, $data) {
     *     $type = $data['type'] ?? 'unknown';
     *     
     *     switch ($type) {
     *         case 'device_info':
     *             // Handle device info pushed from Task
     *             echo "Device: {$data['device_id']}\n";
     *             break;
     *             
     *         case 'progress':
     *             // Handle progress updates
     *             echo "Progress: {$data['percentage']}%\n";
     *             break;
     *     }
     * };
     * ```
     */
    public $onPipeMessage;
    
    /**
     * Create and optionally initialize SIP server
     * 
     * @param array|null $config Optional configuration:
     *   - host: string - Listen IP address (default: '0.0.0.0')
     *   - port: int - Listen port (default: 5060)
     *   - mode: string - Transport protocol: 'UDP'|'TCP'|'ALL' (case-insensitive, default: 'UDP')
     *   - ua: string - User-Agent string (default: 'PHP-GB28181')
     *   - sipId: string - SIP server ID for authentication
     *   - sipRealm: string - SIP authentication realm/domain
     *   - sipPass: string - SIP authentication password
     *   - sipTimeout: int - Transaction timeout in seconds (default: 30)
     *   - sipExpiry: int - Registration expiry in seconds (default: 3600)
     * 
     * Master-Worker-Task Configuration (optional):
     *   - task_worker_num: int - Number of Task processes (default: 4)
     *   - timer_interval: int - Timer interval in milliseconds (default: 1000)
     *   - pid_file: string - PID file path (e.g., '/tmp/server.pid')
     * 
     * @example
     * ```php
     * // Single process mode
     * $sip = new ExoSip([
     *     'host' => '0.0.0.0',
     *     'port' => 5060,
     *     'mode' => 'TCP',
     *     'sipId' => '34020000002000000001'
     * ]);
     * 
     * // Master-Worker-Task mode
     * $sip = new ExoSip([
     *     'host' => '0.0.0.0',
     *     'port' => 5060,
     *     'mode' => 'UDP',
     *     'task_worker_num' => 4,
     *     'timer_interval' => 30000,  // 30 seconds
     *     'pid_file' => '/tmp/gb28181_server.pid',
     * ]);
     * ```
     */
    public function __construct(?array $config = null) {}
    
    /**
     * Initialize or re-initialize SIP server
     * 
     * Used for manual initialization or hot-restart scenarios.
     * 
     * @param array $config Configuration array (same as constructor)
     * @return bool True on success, false on failure
     */
    public function init(array $config): bool {}
    
    /**
     * Shutdown and cleanup SIP context
     * 
     * Use this for graceful shutdown or before re-initialization.
     * The event loop (run()) will stop automatically.
     * 
     * @return bool Always returns true
     */
    public function quit(): bool {}
    
    /**
     * Process pending SIP events (non-blocking mode)
     * 
     * For custom event loops or integration with frameworks.
     * 
     * @param int $timeout_ms Timeout in milliseconds (0 = immediate return)
     * @return SipEvent[] Array of SipEvent objects
     * 
     * @example
     * ```php
     * while (true) {
     *     $events = $sip->processEvents(100);
     *     foreach ($events as $event) {
     *         handleEvent($event);
     *     }
     * }
     * ```
     */
    public function processEvents(int $timeout_ms = 0): array {}
    
    /**
     * Run SIP server with automatic event dispatching (blocking)
     * 
     * Starts the event loop and dispatches events to registered handlers.
     * Blocks until stop() is called or SIGINT/SIGTERM is received.
     * 
     * @return bool True when stopped gracefully
     * 
     * @example
     * ```php
     * $sip->onRegister = fn($e) => handleRegister($e);
     * $sip->onMessage = fn($e) => handleMessage($e);
     * $sip->run();  // Blocks here
     * ```
     */
    public function run(): bool {}
    
    /**
     * Send SIP MESSAGE request
     * 
     * Used for out-of-dialog messages (GB28181 commands, instant messaging, etc.)
     * 
     * @param string $to Target SIP URI (e.g., 'sip:34020000001320000001@3402000000')
     * @param string $message Message body content
     * @param string|null $contentType Content-Type header (default: 'Application/MANSCDP+xml')
     * @return bool True on success, false on failure
     * 
     * @example
     * ```php
     * // GB28181 catalog query
     * $xml = "<?xml version=\"1.0\"?><Query><CmdType>Catalog</CmdType>...</Query>";
     * $sip->sendMessage('sip:34020000001320000001@3402000000', $xml, 'Application/MANSCDP+xml');
     * ```
     */
    public function sendMessage(string $to, string $message, ?string $contentType = null): bool {}
    
    /**
     * Send SIP response to a request
     * 
     * Used in event handlers to respond to incoming requests.
     * 
     * @param int $tid Transaction ID from SipEvent::getTid()
     * @param int $code SIP response code:
     *   - 1xx: Provisional (100 Trying, 180 Ringing)
     *   - 2xx: Success (200 OK)
     *   - 3xx: Redirection (301 Moved Permanently)
     *   - 4xx: Client Error (400 Bad Request, 404 Not Found)
     *   - 5xx: Server Error (500 Internal Server Error)
     *   - 6xx: Global Failure (603 Decline)
     * @param string|null $reason Reason phrase (default: standard phrase for code)
     * @param array|null $headers Additional headers as key-value pairs (e.g., ['Expires' => 3600])
     * @return bool True on success, false on failure
     * 
     * @example
     * ```php
     * $sip->onRegister = function($event) use ($sip) {
     *     // Accept registration
     *     $sip->sendResponse($event->getTid(), 200, 'OK', ['Expires' => 3600]);
     * };
     * ```
     */
    public function sendResponse(int $tid, int $code, ?string $reason = null, ?array $headers = null): bool {}
    
    /**
     * Get socket file descriptor for external event loops
     * 
     * Advanced usage: integrate with select()/poll()/epoll().
     * 
     * @return int Socket file descriptor, or -1 if not available
     */
    public function getFd(): int {}
    
    /**
     * Stop the SIP server gracefully
     * 
     * Signals the run() loop to exit. Non-blocking.
     * 
     * @return bool Always returns true
     */
    public function stop(): bool {}
    
    /**
     * Check if server event loop is running
     * 
     * @return bool True if run() is active, false otherwise
     */
    public function isRunning(): bool {}
    
    /**
     * Set server configuration (batch update)
     * 
     * @param array $config Configuration key-value pairs
     * @return bool True on success
     * 
     * @example
     * ```php
     * $sip->setConfig([
     *     'max_sessions' => 1000,
     *     'timeout' => 60
     * ]);
     * ```
     */
    public function setConfig(array $config): bool {}
    
    /**
     * Get configuration value(s)
     * 
     * @param string|null $key Configuration key (null = return all)
     * @return mixed|array|null Single value, all config, or null if key not found
     * 
     * @example
     * ```php
     * $port = $sip->getConfig('port');  // Get single value
     * $all = $sip->getConfig();         // Get all config
     * ```
     */
    public function getConfig(?string $key = null) {}
    
    /**
     * Get server runtime statistics
     * 
     * @return array{
     *   running: bool,
     *   uptime: int,
     *   listen_ip: string,
     *   listen_port: int,
     *   transport: string,
     *   config_items: int,
     *   event_handlers: array<string, bool>
     * } Statistics array
     * 
     * @example
     * ```php
     * $stats = $sip->getStats();
     * echo "Server running for {$stats['uptime']} seconds\n";
     * echo "Listening on {$stats['listen_ip']}:{$stats['listen_port']}\n";
     * ```
     */
    public function getStats(): array {}
    
    /* ========== Master-Worker-Task Methods ========== */
    
    /**
     * Add async task to Task process pool (Worker process only)
     * 
     * @param array $data Task data (must be array, will be passed to onTask callback)
     * @return int Task ID (auto-increment integer), or -1 on failure
     * 
     * @throws Exception If called in Master or Task process
     * 
     * @example
     * ```php
     * // In SIP event handler (Worker process)
     * $sip->onRegister = function($event) use ($sip) {
     *     $deviceId = extractDeviceId($event->getFromUri());
     *     
     *     // Post webhook task
     *     $taskId = $sip->addTask([
     *         'type' => 'webhook',
     *         'payload' => [
     *             'url' => 'http://api.example.com/device/register',
     *             'data' => [
     *                 'device_id' => $deviceId,
     *                 'timestamp' => time(),
     *             ]
     *         ]
     *     ]);
     *     
     *     echo "Task #{$taskId} posted\n";
     *     
     *     // Continue processing (non-blocking)
     *     $sip->sendResponse($event->getTid(), 200, 'OK');
     * };
     * ```
     */
    public function addTask(array $data): int {}
    
    /**
     * Send data to Worker process (Task process only)
     * 
     * Allows Task processes to proactively push messages to Worker.
     * Used for real-time notifications, progress updates, etc.
     * 
     * @param mixed $data Data to send (will be serialized)
     * @return bool True on success, false on failure
     * @throws Exception If called from non-Task process
     * 
     * @example
     * ```php
     * // In Task process (onTask callback)
     * $sip->onTask = function($server, $taskId, $data) {
     *     // Do some work
     *     $result = queryDatabase($data['id']);
     *     
     *     // Push result to Worker immediately (don't wait for return)
     *     $server->sendToWorker([
     *         'type' => 'db_result',
     *         'data' => $result,
     *         'timestamp' => time()
     *     ]);
     *     
     *     // Continue processing...
     *     $moreData = callExternalAPI();
     *     
     *     // Push progress update
     *     $server->sendToWorker([
     *         'type' => 'progress',
     *         'percentage' => 50
     *     ]);
     *     
     *     return ['status' => 'success'];
     * };
     * 
     * // In Worker process (onPipeMessage callback)
     * $sip->onPipeMessage = function($server, $data) {
     *     if ($data['type'] === 'db_result') {
     *         // Handle database result
     *         processResult($data['data']);
     *     } else if ($data['type'] === 'progress') {
     *         echo "Progress: {$data['percentage']}%\n";
     *     }
     * };
     * ```
     */
    public function sendToWorker($data): bool {}
    
    /**
     * Get process status (internal call, from running process)
     * 
     * @return array Process status information:
     *   - master: ['pid' => int, 'status' => string]
     *   - worker: ['pid' => int, 'status' => string, 'uptime' => int]
     *   - tasks: [['id' => int, 'pid' => int, 'status' => string], ...]
     *   - current_process: string - 'master'|'worker'|'task-N'
     *   - tasks_posted: int - Total tasks posted
     *   - tasks_failed: int - Failed tasks count
     * 
     * @example
     * ```php
     * $status = $sip->getProcessStatus();
     * 
     * echo "Current process: {$status['current_process']}\n";
     * echo "Master PID: {$status['master']['pid']}\n";
     * echo "Worker PID: {$status['worker']['pid']}\n";
     * echo "Tasks posted: {$status['tasks_posted']}\n";
     * 
     * foreach ($status['tasks'] as $task) {
     *     echo "Task-{$task['id']}: PID {$task['pid']}, {$task['status']}\n";
     * }
     * ```
     */
    public function getProcessStatus(): array {}
    
    /**
     * Get run status from PID file (external call, static method)
     * 
     * @param string $pidFile Path to PID file (e.g., '/tmp/gb28181_server.pid')
     * @return array Process status information:
     *   - master: ['pid' => int, 'status' => string, 'memory_rss_kb' => int, 'memory_vsz_kb' => int, 'fd_count' => int]
     *   - worker: ['pid' => int, 'status' => string, 'memory_rss_kb' => int, 'memory_vsz_kb' => int, 'fd_count' => int, 'uptime' => int, 'restart_count' => int]
     *   - tasks: [['id' => int, 'pid' => int, 'status' => string, 'memory_rss_kb' => int, 'memory_vsz_kb' => int, 'fd_count' => int], ...]
     *   - error: string (if failed)
     * 
     * @example
     * ```php
     * // From external script (e.g., monitoring tool)
     * $status = ExoSip::getRunStatus('/tmp/gb28181_server.pid');
     * 
     * if (isset($status['error'])) {
     *     echo "Error: {$status['error']}\n";
     *     exit(1);
     * }
     * 
     * echo "Master PID: {$status['master']['pid']}\n";
     * echo "Master Memory: " . round($status['master']['memory_rss_kb'] / 1024, 2) . " MB\n";
     * echo "Worker PID: {$status['worker']['pid']}\n";
     * echo "Worker Memory: " . round($status['worker']['memory_rss_kb'] / 1024, 2) . " MB\n";
     * echo "Worker FD Count: {$status['worker']['fd_count']}\n";
     * echo "Worker Uptime: {$status['worker']['uptime']} seconds\n";
     * 
     * foreach ($status['tasks'] as $task) {
     *     $mem = round($task['memory_rss_kb'] / 1024, 2);
     *     echo "Task-{$task['id']}: PID {$task['pid']}, Memory {$mem} MB\n";
     * }
     * ```
     */
    public static function getRunStatus(string $pidFile): array {}
}

/**
 * SIP Event Object
 * 
 * Represents a SIP event (REGISTER, MESSAGE, INVITE, etc.)
 */
class SipEvent {
    
    /**
     * Get event type (internal numeric type)
     * 
     * @return int Event type constant
     */
    public function getType(): int {}
    
    /**
     * Get response code
     * 
     * @return int Response code (0 for requests, 200-699 for responses)
     */
    public function getCode(): int {}
    
    /**
     * Get From URI
     * 
     * @return string|null From header URI (e.g., 'sip:34020000001320000001@3402000000')
     */
    public function getFromUri(): ?string {}
    
    /**
     * Get To URI
     * 
     * @return string|null To header URI
     */
    public function getToUri(): ?string {}
    
    /**
     * Get Request-URI
     * 
     * @return string|null Request-URI
     */
    public function getRequestUri(): ?string {}
    
    /**
     * Get message body
     * 
     * @return string|null Message body (e.g., XML content for GB28181)
     */
    public function getBody(): ?string {}
    
    /**
     * Get Content-Type header
     * 
     * @return string|null Content-Type (e.g., 'Application/MANSCDP+xml')
     */
    public function getContentType(): ?string {}
    
    /**
     * Get transaction ID
     * 
     * @return int Transaction ID (use for sendResponse)
     */
    public function getTid(): int {}
    
    /**
     * Get Expires header value
     * 
     * @return int Expires value in seconds, -1 if not present, 0 for unregister
     */
    public function getExpires(): int {}
    
    /**
     * Get associated SIP session
     * 
     * @return SipSession|null Session object if exists
     */
    public function getSession(): ?SipSession {}
    
    /**
     * Get connection information
     * 
     * @return array|null Connection info array with keys:
     *   - id: int - Connection ID
     *   - device_id: string - Device ID
     *   - ip: string - IP address
     *   - port: int - Port number
     *   - state: int - Connection state
     *   - state_name: string - State name (IDLE, REGISTERED, etc.)
     *   - contact_uri: string - Contact URI
     *   - user_agent: string - User-Agent header
     *   - created_at: int - Creation timestamp
     *   - last_seen: int - Last activity timestamp
     *   - register_count: int - Registration count
     *   - message_count: int - Message count
     */
    public function getConnection(): ?array {}
    
    /**
     * Get SIP header value
     * 
     * @param string $name Header name (e.g., 'Authorization', 'WWW-Authenticate', 'Via')
     * @return string|null Header value, or null if not found
     * 
     * @example
     * ```php
     * $auth = $event->getHeader('Authorization');
     * $wwwAuth = $event->getHeader('WWW-Authenticate');
     * $via = $event->getHeader('Via');  // For received parameter
     * ```
     */
    public function getHeader(string $name): ?string {}
    
    /**
     * Get file descriptor (TCP mode only)
     * 
     * Returns the TCP connection file descriptor for this event.
     * Used in TCP mode for connection binding and management.
     * 
     * @return int File descriptor (>0 for TCP), 0 for UDP
     * 
     * @example
     * ```php
     * $sip->onRegister = function($event) use ($deviceManager) {
     *     $mode = $this->getConfig()['mode'] ?? 'udp';
     *     
     *     if ($mode === 'tcp' || $mode === 'tls') {
     *         $fd = $event->getFd();
     *         if ($fd > 0) {
     *             $deviceId = extractDeviceId($event);
     *             $deviceManager->bindConnection($deviceId, $fd);
     *         }
     *     }
     * };
     * ```
     */
    public function getFd(): int {}
}

/**
 * SIP Session Object
 * 
 * Represents a SIP dialog/session (call, subscription, etc.)
 * Similar to Workerman's TcpConnection, provides session lifecycle management.
 * 
 * @example
 * ```php
 * $sip->onInvite = function($event) use ($sip) {
 *     $session = $event->getSession();
 *     
 *     // Store session for later use
 *     $sessions[$session->getId()] = $session;
 *     
 *     // Close session when needed (like Workerman's $connection->close())
 *     Timer::add(30, function() use ($session) {
 *         $session->close();  // Send BYE and cleanup
 *     }, null, false);
 * };
 * ```
 */
class SipSession {
    
    /**
     * Get session ID
     * 
     * @return int Session ID
     */
    public function getId(): int {}
    
    /**
     * Get connection ID
     * 
     * @return int Connection ID that this session belongs to
     */
    public function getConnectionId(): int {}
    
    /**
     * Get call ID
     * 
     * @return int Call ID
     */
    public function getCallId(): int {}
    
    /**
     * Get From URI
     * 
     * @return string|null From URI
     */
    public function getFromUri(): ?string {}
    
    /**
     * Get To URI
     * 
     * @return string|null To URI
     */
    public function getToUri(): ?string {}
    
    /**
     * Get session state
     * 
     * @return string Session state
     */
    public function getState(): string {}
    
    /**
     * Get raw body
     * 
     * @return string|null Raw body content
     */
    public function getRawBody(): ?string {}
    
    /**
     * Close the SIP session
     * 
     * Similar to Workerman's TcpConnection::close(), gracefully terminates the session.
     * Sends BYE request if it's an active call and cleans up session resources.
     * 
     * @return bool True on success, false on failure
     * 
     * @example
     * ```php
     * // Close session after timeout (like Workerman heartbeat example)
     * $sip->onInvite = function($event) {
     *     $session = $event->getSession();
     *     
     *     // Auto-close after 30 seconds
     *     Timer::add(30, function() use ($session) {
     *         if ($session) {
     *             $session->close();
     *         }
     *     }, null, false);
     * };
     * 
     * // Manual close
     * if ($session->getState() === 'INCALL') {
     *     $session->close();  // Send BYE and cleanup
     * }
     * ```
     */
    public function close(): bool {}
}

/**
 * SIP Client Class
 * 
 * Provides SIP client functionality for connecting to SIP servers.
 * Supports REGISTER, MESSAGE, INVITE, and other SIP methods.
 * 
 * @example
 * ```php
 * $client = new ExoSipClient([
 *     'server_ip' => '127.0.0.1',
 *     'server_port' => 5060,
 *     'username' => 'device001',
 *     'password' => '123456',
 *     'realm' => '3402000000',
 *     'mode' => 'UDP'
 * ]);
 * 
 * $client->start();
 * $client->sendRegister();
 * 
 * // Send MESSAGE
 * $client->sendMessage('sip:server@domain', 'Hello!');
 * 
 * // Process events
 * $events = $client->processEvents(100);
 * 
 * $client->stop();
 * ```
 */
class ExoSipClient {
    
    /**
     * Create SIP client instance
     * 
     * @param array $config Client configuration
     * - server_ip (string, required): Server IP address
     * - server_port (int): Server port, default 5060
     * - username (string, required): SIP username/device ID
     * - password (string): SIP password for authentication
     * - realm (string): SIP realm/domain
     * - mode (string): Transport mode (UDP|TCP), default UDP
     * - local_ip (string): Local IP to bind, optional
     * - local_port (int): Local port, 0 = auto, default 0
     * - from_uri (string): Custom From URI, optional
     * - to_uri (string): Custom To URI, optional
     * - expires (int): Registration expiry seconds, default 3600
     * - debug (bool): Enable debug output, default false
     * 
     * @throws Exception If required parameters missing or init fails
     */
    public function __construct(?array $config = null) {}
    
    /**
     * Start client (bind port and event thread)
     * 
     * @return bool True on success
     */
    public function start(): bool {}
    
    /**
     * Stop client
     * 
     * @return bool True on success
     */
    public function stop(): bool {}
    
    /**
     * Send REGISTER request
     * 
     * @return int Registration ID (>= 0), or -1 on failure
     */
    public function sendRegister(): int {}
    
    /**
     * Send UNREGISTER request (expires=0)
     * 
     * @return int 0 on success, -1 on failure
     */
    public function sendUnregister(): int {}
    
    /**
     * Send MESSAGE request
     * 
     * @param string $to_uri Target URI (e.g., 'sip:user@domain')
     * @param string $body Message body
     * @param string|null $content_type Content-Type header, default 'text/plain'
     * @return int Transaction ID (>= 0), or -1 on failure
     */
    public function sendMessage(string $to_uri, string $body, ?string $content_type = null): int {}
    
    /**
     * Send INVITE request (initiate call/session)
     * 
     * @param string $to_uri Target URI
     * @param string|null $sdp SDP body (optional)
     * @return int Call ID (> 0), or -1 on failure
     */
    public function sendInvite(string $to_uri, ?string $sdp = null): int {}
    
    /**
     * Send BYE request (terminate call/session)
     * 
     * @param int $did Dialog ID
     * @param int $cid Call ID
     * @return int 0 on success, -1 on failure
     */
    public function sendBye(int $did, int $cid): int {}
    
    /**
     * Send OPTIONS request
     * 
     * @param string $to_uri Target URI
     * @return int Transaction ID (>= 0), or -1 on failure
     */
    public function sendOptions(string $to_uri): int {}
    
    /**
     * Check if client is registered
     * 
     * @return bool True if registered
     */
    public function isRegistered(): bool {}
    
    /**
     * Get client statistics
     * 
     * @return array Statistics array
     * - registered (int): 1 if registered, 0 otherwise
     * - request_count (int): Total requests sent
     * - response_count (int): Total responses received
     * - timeout_count (int): Total timeouts
     * - register_time (int): Unix timestamp of last successful registration
     */
    public function getStats(): array {}
    
    /**
     * Process events (non-blocking)
     * 
     * @param int $timeout_ms Timeout in milliseconds, default 0 (immediate return)
     * @return array Array of events
     * Each event contains:
     * - type (int): Event type constant
     * - tid (int): Transaction ID
     * - did (int): Dialog ID
     * - cid (int): Call ID
     * - rid (int): Registration ID
     * - status_code (int): Response status code (if response)
     * - reason (string): Response reason phrase (if response)
     * - method (string): Request method (if request)
     */
    public function processEvents(int $timeout_ms = 0): array {}
}

// SIP Method Event Constants (PHP Extension)
define('SIP_EVENT_REGISTER', 1);
define('SIP_EVENT_INVITE', 2);
define('SIP_EVENT_ACK', 3);
define('SIP_EVENT_BYE', 4);
define('SIP_EVENT_CANCEL', 5);
define('SIP_EVENT_MESSAGE', 6);
define('SIP_EVENT_INFO', 7);
define('SIP_EVENT_OPTIONS', 8);
define('SIP_EVENT_SUBSCRIBE', 9);
define('SIP_EVENT_NOTIFY', 10);

// eXosip2 event type constants (from eXosip2 library)
define('EXOSIP_REGISTRATION_SUCCESS', 1);
define('EXOSIP_REGISTRATION_FAILURE', 2);
define('EXOSIP_CALL_INVITE', 3);
define('EXOSIP_CALL_REINVITE', 4);
define('EXOSIP_CALL_NOANSWER', 5);
define('EXOSIP_CALL_PROCEEDING', 6);
define('EXOSIP_CALL_RINGING', 7);
define('EXOSIP_CALL_ANSWERED', 8);
define('EXOSIP_CALL_REDIRECTED', 9);
define('EXOSIP_CALL_REQUESTFAILURE', 10);
define('EXOSIP_CALL_SERVERFAILURE', 11);
define('EXOSIP_CALL_GLOBALFAILURE', 12);
define('EXOSIP_CALL_ACK', 13);
define('EXOSIP_CALL_CANCELLED', 14);
define('EXOSIP_CALL_MESSAGE_NEW', 15);
define('EXOSIP_CALL_MESSAGE_PROCEEDING', 16);
define('EXOSIP_CALL_MESSAGE_ANSWERED', 17);
define('EXOSIP_CALL_MESSAGE_REDIRECTED', 18);
define('EXOSIP_CALL_MESSAGE_REQUESTFAILURE', 19);
define('EXOSIP_CALL_MESSAGE_SERVERFAILURE', 20);
define('EXOSIP_CALL_MESSAGE_GLOBALFAILURE', 21);
define('EXOSIP_CALL_CLOSED', 22);
define('EXOSIP_CALL_RELEASED', 23);
define('EXOSIP_MESSAGE_NEW', 24);
define('EXOSIP_MESSAGE_PROCEEDING', 25);
define('EXOSIP_MESSAGE_ANSWERED', 26);
define('EXOSIP_MESSAGE_REDIRECTED', 27);
define('EXOSIP_MESSAGE_REQUESTFAILURE', 28);
define('EXOSIP_MESSAGE_SERVERFAILURE', 29);
define('EXOSIP_MESSAGE_GLOBALFAILURE', 30);
define('EXOSIP_SUBSCRIPTION_NOANSWER', 31);
define('EXOSIP_SUBSCRIPTION_PROCEEDING', 32);
define('EXOSIP_SUBSCRIPTION_ANSWERED', 33);
define('EXOSIP_SUBSCRIPTION_REDIRECTED', 34);
define('EXOSIP_SUBSCRIPTION_REQUESTFAILURE', 35);
define('EXOSIP_SUBSCRIPTION_SERVERFAILURE', 36);
define('EXOSIP_SUBSCRIPTION_GLOBALFAILURE', 37);
define('EXOSIP_SUBSCRIPTION_NOTIFY', 38);
define('EXOSIP_IN_SUBSCRIPTION_NEW', 39);
define('EXOSIP_NOTIFICATION_NOANSWER', 40);
define('EXOSIP_NOTIFICATION_PROCEEDING', 41);
define('EXOSIP_NOTIFICATION_ANSWERED', 42);
define('EXOSIP_NOTIFICATION_REDIRECTED', 43);
define('EXOSIP_NOTIFICATION_REQUESTFAILURE', 44);
define('EXOSIP_NOTIFICATION_SERVERFAILURE', 45);
define('EXOSIP_NOTIFICATION_GLOBALFAILURE', 46);
