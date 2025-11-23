<?php
/**
 * Timer - Swoole Timer Wrapper
 * 
 * Simple wrapper for Swoole\Timer.
 * REQUIRES Swoole extension.
 * 
 * Usage: Only use within SIP event callbacks when $sip->run() provides the event loop.
 * 
 * @example
 * ```php
 * $sip = new ExoSip();
 * $sip->onInvite = function($event) {
 *     $session = $event->getSession();
 *     
 *     // Add heartbeat timer for this session
 *     $timerId = Timer::add(30, function() use ($session) {
 *         echo "Checking session...\n";
 *     });
 *     
 *     // Cancel timer later
 *     Timer::del($timerId);
 * };
 * $sip->run();
 * ```
 */
class Timer
{
    /**
     * Add a timer
     *
     * @param float $seconds Interval in seconds
     * @param callable $callback Callback function
     * @param array $args Arguments for callback
     * @param bool $persistent true = repeat, false = once
     * @return int Timer ID
     */
    public static function add($seconds, $callback, $args = [], $persistent = true)
    {
        if (!class_exists('Swoole\Timer')) {
            trigger_error("Timer requires Swoole extension", E_USER_ERROR);
            return false;
        }

        $milliseconds = (int)($seconds * 1000);

        if ($persistent) {
            return \Swoole\Timer::tick($milliseconds, function() use ($callback, $args) {
                call_user_func_array($callback, $args);
            });
        } else {
            return \Swoole\Timer::after($milliseconds, function() use ($callback, $args) {
                call_user_func_array($callback, $args);
            });
        }
    }

    /**
     * Delete a timer
     *
     * @param int $timerId Timer ID
     * @return bool
     */
    public static function del($timerId)
    {
        if (!class_exists('Swoole\Timer')) {
            return false;
        }

        return \Swoole\Timer::clear($timerId);
    }

    /**
     * Clear all timers
     *
     * @return void
     */
    public static function delAll()
    {
        if (!class_exists('Swoole\Timer')) {
            return;
        }

        \Swoole\Timer::clearAll();
    }

    /**
     * Get timer statistics
     *
     * @return array
     */
    public static function info()
    {
        if (!class_exists('Swoole\Timer')) {
            return ['count' => 0];
        }

        return \Swoole\Timer::stats();
    }
}
