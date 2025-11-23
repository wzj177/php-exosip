<?php
/**
 * SIP Session Management Example (Workerman Style)
 * 
 * This example demonstrates how to manage SIP sessions similar to Workerman's connection management.
 * The SipSession::close() method works like TcpConnection::close() for graceful session termination.
 */

require_once __DIR__ . '/Timer.php';

// Configuration
define('HEARTBEAT_TIME', 55);  // Session timeout in seconds
define('CHECK_INTERVAL', 10);  // Check interval for session cleanup

$sip = new ExoSip([
    'host' => '0.0.0.0',
    'port' => 5060,
    'mode' => 'TCP',
    'debug' => true
]);

// Store active sessions (like Workerman's $worker->connections)
$sessions = [];

/**
 * INVITE Handler - New session created
 */
$sip->onInvite = function($event) use ($sip, &$sessions) {
    echo "[INVITE] New session from: " . $event->getFromUri() . "\n";
    
    $session = $event->getSession();
    if (!$session) {
        echo "[ERROR] Failed to get session\n";
        return;
    }
    
    $sessionId = $session->getId();
    
    // Store session with metadata (like Workerman's connection->lastMessageTime)
    $sessions[$sessionId] = [
        'session' => $session,
        'last_seen' => time(),
        'from_uri' => $event->getFromUri(),
        'to_uri' => $event->getToUri(),
        'created_at' => time()
    ];
    
    echo "[SESSION] Created session $sessionId, total: " . count($sessions) . "\n";
    
    // Accept the call
    $sip->sendResponse($event->getTid(), 200, 'OK');
    
    // Start heartbeat timer for this specific session
    $timerId = Timer::add(CHECK_INTERVAL, function() use ($sessionId, &$sessions) {
        if (!isset($sessions[$sessionId])) {
            return; // Session already closed
        }
        
        $sessionData = $sessions[$sessionId];
        $idle_time = time() - $sessionData['last_seen'];
        
        if ($idle_time > HEARTBEAT_TIME) {
            echo "[TIMEOUT] Session $sessionId idle for {$idle_time}s, closing...\n";
            $sessionData['session']->close();
            unset($sessions[$sessionId]);
        }
    });
};

/**
 * MESSAGE Handler - Update session last_seen time (like Workerman's onMessage)
 */
$sip->onMessage = function($event) use (&$sessions) {
    $session = $event->getSession();
    if ($session) {
        $sessionId = $session->getId();
        
        if (isset($sessions[$sessionId])) {
            // Update last seen time (like Workerman's $connection->lastMessageTime = time())
            $sessions[$sessionId]['last_seen'] = time();
            echo "[MESSAGE] Session $sessionId alive, from: " . $event->getFromUri() . "\n";
        }
    }
};

/**
 * BYE Handler - Session closed by remote
 */
$sip->onBye = function($event) use ($sip, &$sessions) {
    echo "[BYE] Session closing from: " . $event->getFromUri() . "\n";
    
    $session = $event->getSession();
    if ($session) {
        $sessionId = $session->getId();
        
        // Remove from active sessions
        if (isset($sessions[$sessionId])) {
            unset($sessions[$sessionId]);
            echo "[SESSION] Removed session $sessionId, total: " . count($sessions) . "\n";
        }
        
        // Send 200 OK
        $sip->sendResponse($event->getTid(), 200, 'OK');
    }
};

/**
 * Statistics reporter - runs every 30 seconds
 */
Timer::add(30, function() use (&$sessions, $sip) {
    $stats = $sip->getStats();
    echo "\n=== SIP Server Statistics ===\n";
    echo "Active sessions: " . count($sessions) . "\n";
    echo "============================\n\n";
});

echo "SIP Server started on 0.0.0.0:5060 (TCP)\n";
echo "Session timeout: " . HEARTBEAT_TIME . " seconds\n";
echo "Cleanup interval: " . CHECK_INTERVAL . " seconds\n";
echo "Press Ctrl+C to stop\n\n";

// Run event loop (blocking)
$sip->run();
