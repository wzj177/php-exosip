#include "exosip_wrapper.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <dirent.h>
#include "ext/standard/php_var.h"
#include "zend_smart_str.h"

// 内存管理函数由osip库提供，不需要重新定义

// ==================== 核心功能 ====================

SipContext* sip_init(ServerInfo *info) {
    if (!info) return NULL;
    
    // 初始化内存分配函数（osip库已提供默认实现）
    // osip_set_allocators(malloc, realloc, free);
    
    SipContext *ctx = (SipContext*)calloc(1, sizeof(SipContext));
    if (!ctx) return NULL;
    
    // 初始化eXosip
    ctx->ctx = eXosip_malloc();
    if (!ctx->ctx) {
        free(ctx);
        return NULL;
    }
    
    if (eXosip_init(ctx->ctx) != 0) {
        eXosip_quit(ctx->ctx);
        free(ctx);
        return NULL;
    }
    
    // 复制服务器配置
    memcpy(&ctx->server_info, info, sizeof(ServerInfo));
    
    // 初始化互斥锁
    if (pthread_mutex_init(&ctx->lock, NULL) != 0) {
        eXosip_quit(ctx->ctx);
        free(ctx);
        return NULL;
    }
    
    memset(ctx->connections, 0, sizeof(ctx->connections));
    memset(ctx->sessions, 0, sizeof(ctx->sessions));
    ctx->connection_count = 0;
    ctx->session_count = 0;
    ctx->running = 0;
    ctx->callbacks_valid = 0;
    ctx->server_info = *info;
    ctx->stats.start_time = time(NULL);
    
    // 初始化定时器字段
    ctx->timer_interval_ms = 0;
    ctx->last_timer_tick = 0;
    ctx->last_timer_tick_us = 0;
    
    // 初始化进程管理字段
    ctx->master_pid = 0;
    ctx->worker_pid = 0;
    ctx->task_pids = NULL;
    ctx->task_sockfds = NULL;
    ctx->task_count = 0;
    ctx->is_master = 0;
    ctx->is_worker = 0;
    ctx->is_task = 0;
    ctx->task_worker_id = -1;
    ctx->tasks_posted = 0;
    ctx->tasks_failed = 0;
    ctx->worker_start_time = 0;
    ctx->worker_restart_count = 0;
    ZVAL_UNDEF(&ctx->task_callback);
    ZVAL_UNDEF(&ctx->task_finish_callback);
    
    int debug = info->debug;
    
    if (info->ua && strlen(info->ua) > 0) {
        eXosip_set_user_agent(ctx->ctx, info->ua);
    }
    
    // 启用TCP端口复用（关键：允许TCP/UDP同时监听同一端口）
    int enable_reuse = 1;
    eXosip_set_option(ctx->ctx, EXOSIP_OPT_ENABLE_REUSE_TCP_PORT, (void*)&enable_reuse);
    
    const char *mode = info->mode ? info->mode : "udp";
    int listen_result = 0;
    const char *use_ip = (info->ip && strlen(info->ip) > 0) ? info->ip : NULL;
    
    if (debug) fprintf(stderr, "[DEBUG] Init: mode=%s, ip=%s, port=%d\n", 
            mode, use_ip ? use_ip : "0.0.0.0", info->port);
    
    if (strcasecmp(mode, "udp") == 0) {
        listen_result = eXosip_listen_addr(ctx->ctx, IPPROTO_UDP, use_ip, info->port, AF_INET, 0);
        if (debug) fprintf(stderr, "[DEBUG] UDP listen result: %d\n", listen_result);
        if (listen_result != 0) {
            fprintf(stderr, "[ERROR] UDP listen failed on %s:%d (error: %d)\n", 
                    use_ip ? use_ip : "0.0.0.0", info->port, listen_result);
            pthread_mutex_destroy(&ctx->lock);
            eXosip_quit(ctx->ctx);
            free(ctx);
            return NULL;
        }
    } else if (strcasecmp(mode, "tcp") == 0) {
        // TCP需要设置更长的超时
        int tcp_timeout = 60;
        eXosip_set_option(ctx->ctx, EXOSIP_OPT_UDP_KEEP_ALIVE, (void*)&tcp_timeout);
        
        listen_result = eXosip_listen_addr(ctx->ctx, IPPROTO_TCP, use_ip, info->port, AF_INET, 0);
        if (debug) {
            fprintf(stderr, "[DEBUG] TCP listen result: %d\n", listen_result);
            fprintf(stderr, "[DEBUG] TCP server ready on %s:%d\n", use_ip ? use_ip : "0.0.0.0", info->port);
        }
        if (listen_result != 0) {
            fprintf(stderr, "[ERROR] TCP listen failed on %s:%d (error: %d)\n", 
                    use_ip ? use_ip : "0.0.0.0", info->port, listen_result);
            pthread_mutex_destroy(&ctx->lock);
            eXosip_quit(ctx->ctx);
            free(ctx);
            return NULL;
        }
    } else if (strcasecmp(mode, "all") == 0) {
        // 同时监听UDP和TCP，设置TCP超时
        int tcp_timeout = 60;
        eXosip_set_option(ctx->ctx, EXOSIP_OPT_UDP_KEEP_ALIVE, (void*)&tcp_timeout);
        
        int udp_result = eXosip_listen_addr(ctx->ctx, IPPROTO_UDP, use_ip, info->port, AF_INET, 0);
        int tcp_result = eXosip_listen_addr(ctx->ctx, IPPROTO_TCP, use_ip, info->port, AF_INET, 0);
        
        if (debug) {
            fprintf(stderr, "[DEBUG] UDP result: %d, TCP result: %d\n", udp_result, tcp_result);
            if (udp_result == 0) fprintf(stderr, "[DEBUG] UDP server ready on %s:%d\n", use_ip ? use_ip : "0.0.0.0", info->port);
            if (tcp_result == 0) fprintf(stderr, "[DEBUG] TCP server ready on %s:%d\n", use_ip ? use_ip : "0.0.0.0", info->port);
        }
        
        if (udp_result != 0 && tcp_result != 0) {
            fprintf(stderr, "[ERROR] Both UDP and TCP listen failed\n");
            pthread_mutex_destroy(&ctx->lock);
            eXosip_quit(ctx->ctx);
            free(ctx);
            return NULL;
        }
    } else {
        fprintf(stderr, "[WARNING] Unknown mode '%s', using UDP\n", mode);
        listen_result = eXosip_listen_addr(ctx->ctx, IPPROTO_UDP, use_ip, info->port, AF_INET, 0);
        if (listen_result != 0) {
            fprintf(stderr, "[ERROR] UDP listen failed on %s:%d\n", 
                    use_ip ? use_ip : "0.0.0.0", info->port);
            pthread_mutex_destroy(&ctx->lock);
            eXosip_quit(ctx->ctx);
            free(ctx);
            return NULL;
        }
    }
    
    if (info->sipId && info->sipPass && info->sipRealm) {
        eXosip_add_authentication_info(ctx->ctx, info->sipId, info->sipId, 
                                      info->sipPass, NULL, info->sipRealm);
    }
    
    if (debug) fprintf(stderr, "[OK] Listen successful on %s\n", mode);
    
    ZVAL_UNDEF(&ctx->event_callback);
    ZVAL_UNDEF(&ctx->connection_callback);
    ZVAL_UNDEF(&ctx->message_callback);
    ZVAL_UNDEF(&ctx->error_callback);
    
    return ctx;
}

int sip_start(SipContext *ctx) {
    if (!ctx || ctx->running) return -1;
    
    ctx->running = 1;
    
    // 注意：不再启动独立事件线程
    // 现在使用 PHP 的 run() 方法的单线程事件循环模式
    // 或使用 processEvents() 手动处理事件
    
    return 0;
}

int sip_stop(SipContext *ctx) {
    if (!ctx || !ctx->running) return -1;
    
    ctx->running = 0;
    
    // 等待事件线程退出
    if (ctx->event_thread) {
        pthread_join(ctx->event_thread, NULL);
        ctx->event_thread = 0;
    }
    
    return 0;
}

void sip_destroy(SipContext *ctx) {
    if (!ctx) return;
    
    sip_stop(ctx);
    
    pthread_mutex_lock(&ctx->lock);
    if (ctx->callbacks_valid) {
        if (!Z_ISUNDEF(ctx->event_callback)) zval_ptr_dtor(&ctx->event_callback);
        if (!Z_ISUNDEF(ctx->connection_callback)) zval_ptr_dtor(&ctx->connection_callback);
        if (!Z_ISUNDEF(ctx->message_callback)) zval_ptr_dtor(&ctx->message_callback);
        if (!Z_ISUNDEF(ctx->error_callback)) zval_ptr_dtor(&ctx->error_callback);
    }
    pthread_mutex_unlock(&ctx->lock);
    
    if (ctx->raw_data_buffer) {
        for (int i = 0; i < ctx->raw_data_size; i++) {
            if (ctx->raw_data_buffer[i].data) {
                free(ctx->raw_data_buffer[i].data);
            }
        }
        free(ctx->raw_data_buffer);
    }
    
    if (ctx->ctx) {
        eXosip_quit(ctx->ctx);
    }
    
    pthread_mutex_destroy(&ctx->lock);
    free(ctx);
}

// ==================== 连接管理 ====================

int sip_get_connection_count(SipContext *ctx) {
    if (!ctx) return 0;
    
    pthread_mutex_lock(&ctx->lock);
    int count = 0;
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (ctx->connections[i].id > 0 && 
            ctx->connections[i].state != CONN_STATE_DISCONNECTED) {
            count++;
        }
    }
    pthread_mutex_unlock(&ctx->lock);
    
    return count;
}

ConnectionInfo* sip_get_connection(SipContext *ctx, int conn_id) {
    if (!ctx || conn_id <= 0) return NULL;
    
    pthread_mutex_lock(&ctx->lock);
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (ctx->connections[i].id == conn_id) {
            pthread_mutex_unlock(&ctx->lock);
            return &ctx->connections[i];
        }
    }
    pthread_mutex_unlock(&ctx->lock);
    
    return NULL;
}

ConnectionInfo* sip_find_connection_by_device(SipContext *ctx, const char *device_id) {
    if (!ctx || !device_id) return NULL;
    
    pthread_mutex_lock(&ctx->lock);
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (ctx->connections[i].id > 0 && 
            strcmp(ctx->connections[i].device_id, device_id) == 0) {
            pthread_mutex_unlock(&ctx->lock);
            return &ctx->connections[i];
        }
    }
    pthread_mutex_unlock(&ctx->lock);
    
    return NULL;
}

void sip_close_connection(SipContext *ctx, int conn_id) {
    if (!ctx || conn_id <= 0) return;
    
    pthread_mutex_lock(&ctx->lock);
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (ctx->connections[i].id == conn_id) {
            ctx->connections[i].state = CONN_STATE_DISCONNECTED;
            // 清理关联的会话
            for (int j = 0; j < MAX_SESSIONS; j++) {
                if (ctx->sessions[j].connection_id == conn_id) {
                    memset(&ctx->sessions[j], 0, sizeof(SessionInfo));
                }
            }
            break;
        }
    }
    pthread_mutex_unlock(&ctx->lock);
}

void sip_cleanup_expired_connections(SipContext *ctx, int timeout_seconds) {
    if (!ctx) return;
    
    time_t now = time(NULL);
    
    pthread_mutex_lock(&ctx->lock);
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (ctx->connections[i].id > 0) {
            if (now - ctx->connections[i].last_seen > timeout_seconds) {
                // 标记为断开连接
                ctx->connections[i].state = CONN_STATE_DISCONNECTED;
                
                // 清理关联的会话
                for (int j = 0; j < MAX_SESSIONS; j++) {
                    if (ctx->sessions[j].connection_id == ctx->connections[i].id) {
                        memset(&ctx->sessions[j], 0, sizeof(SessionInfo));
                    }
                }
            }
        }
    }
    pthread_mutex_unlock(&ctx->lock);
}

// ==================== 会话管理 ====================

int sip_create_session(SipContext *ctx, int conn_id, SessionType type) {
    if (!ctx || conn_id <= 0) return -1;
    
    pthread_mutex_lock(&ctx->lock);
    
    // 查找空闲会话槽
    int session_id = -1;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (ctx->sessions[i].id == 0) {
            session_id = i + 1; // 会话ID从1开始
            break;
        }
    }
    
    if (session_id == -1) {
        pthread_mutex_unlock(&ctx->lock);
        return -1; // 会话已满
    }
    
    // 初始化会话
    SessionInfo *session = &ctx->sessions[session_id - 1];
    memset(session, 0, sizeof(SessionInfo));
    
    session->id = session_id;
    session->connection_id = conn_id;
    session->type = type;
    session->created_at = time(NULL);
    session->updated_at = session->created_at;
    
    ctx->session_count++;
    pthread_mutex_unlock(&ctx->lock);
    
    return session_id;
}

SessionInfo* sip_get_session(SipContext *ctx, int session_id) {
    if (!ctx || session_id <= 0 || session_id > MAX_SESSIONS) return NULL;
    
    pthread_mutex_lock(&ctx->lock);
    SessionInfo *session = &ctx->sessions[session_id - 1];
    if (session->id == session_id) {
        pthread_mutex_unlock(&ctx->lock);
        return session;
    }
    pthread_mutex_unlock(&ctx->lock);
    
    return NULL;
}

void sip_close_session(SipContext *ctx, int session_id) {
    if (!ctx || session_id <= 0 || session_id > MAX_SESSIONS) return;
    
    pthread_mutex_lock(&ctx->lock);
    SessionInfo *session = &ctx->sessions[session_id - 1];
    if (session->id == session_id) {
        memset(session, 0, sizeof(SessionInfo));
        ctx->session_count--;
    }
    pthread_mutex_unlock(&ctx->lock);
}

void sip_cleanup_expired_sessions(SipContext *ctx, int timeout_seconds) {
    if (!ctx) return;
    
    time_t now = time(NULL);
    
    pthread_mutex_lock(&ctx->lock);
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (ctx->sessions[i].id > 0) {
            if (now - ctx->sessions[i].updated_at > timeout_seconds) {
                memset(&ctx->sessions[i], 0, sizeof(SessionInfo));
                ctx->session_count--;
            }
        }
    }
    pthread_mutex_unlock(&ctx->lock);
}

// ==================== PHP回调设置 ====================

void sip_set_event_callback(SipContext *ctx, zval *callback) {
    if (!ctx || !callback) return;
    
    pthread_mutex_lock(&ctx->lock);
    if (!Z_ISUNDEF(ctx->event_callback)) {
        zval_ptr_dtor(&ctx->event_callback);
    }
    ZVAL_COPY(&ctx->event_callback, callback);
    ctx->callbacks_valid = 1;
    pthread_mutex_unlock(&ctx->lock);
}

void sip_set_connection_callback(SipContext *ctx, zval *callback) {
    if (!ctx || !callback) return;
    
    pthread_mutex_lock(&ctx->lock);
    if (!Z_ISUNDEF(ctx->connection_callback)) {
        zval_ptr_dtor(&ctx->connection_callback);
    }
    ZVAL_COPY(&ctx->connection_callback, callback);
    ctx->callbacks_valid = 1;
    pthread_mutex_unlock(&ctx->lock);
}

void sip_set_message_callback(SipContext *ctx, zval *callback) {
    if (!ctx || !callback) return;
    
    pthread_mutex_lock(&ctx->lock);
    if (!Z_ISUNDEF(ctx->message_callback)) {
        zval_ptr_dtor(&ctx->message_callback);
    }
    ZVAL_COPY(&ctx->message_callback, callback);
    ctx->callbacks_valid = 1;
    pthread_mutex_unlock(&ctx->lock);
}

void sip_set_error_callback(SipContext *ctx, zval *callback) {
    if (!ctx || !callback) return;
    
    pthread_mutex_lock(&ctx->lock);
    if (!Z_ISUNDEF(ctx->error_callback)) {
        zval_ptr_dtor(&ctx->error_callback);
    }
    ZVAL_COPY(&ctx->error_callback, callback);
    ctx->callbacks_valid = 1;
    pthread_mutex_unlock(&ctx->lock);
}

// ==================== 工具函数 ====================

char* generate_nonce(void) {
    static const char charset[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char *nonce = (char*)malloc(33);
    if (!nonce) return NULL;
    
    struct timeval tv;
    gettimeofday(&tv, NULL);
    srand(tv.tv_sec + tv.tv_usec);
    
    for (int i = 0; i < 32; i++) {
        nonce[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    nonce[32] = '\0';
    
    return nonce;
}

char* generate_call_id(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    char *call_id = (char*)malloc(64);
    if (!call_id) return NULL;
    
    snprintf(call_id, 64, "%ld%06d@sipserver", tv.tv_sec, tv.tv_usec);
    return call_id;
}

char* generate_sn(void) {
    char *sn = (char*)malloc(32);
    if (!sn) return NULL;
    
    snprintf(sn, 32, "%ld", time(NULL));
    return sn;
}

int parse_xml_tag(const char *xml, const char *tag, char *value, int max_len) {
    if (!xml || !tag || !value) return -1;
    
    char start_tag[128], end_tag[128];
    snprintf(start_tag, sizeof(start_tag), "<%s>", tag);
    snprintf(end_tag, sizeof(end_tag), "</%s>", tag);
    
    const char *start = strstr(xml, start_tag);
    if (!start) return -1;
    
    start += strlen(start_tag);
    const char *end = strstr(start, end_tag);
    if (!end) return -1;
    
    int len = end - start;
    if (len >= max_len) len = max_len - 1;
    
    strncpy(value, start, len);
    value[len] = '\0';
    
    return 0;
}

// ==================== GB28181消息解析 ====================

int parse_gb28181_message(const char *xml, GB28181Message *msg) {
    if (!xml || !msg) return -1;
    
    memset(msg, 0, sizeof(GB28181Message));
    
    // 保存原始XML
    strncpy(msg->raw_xml, xml, sizeof(msg->raw_xml) - 1);
    
    // 解析基本字段
    char cmd_type_str[64];
    if (parse_xml_tag(xml, "CmdType", cmd_type_str, sizeof(cmd_type_str)) == 0) {
        strncpy(msg->cmd_type, cmd_type_str, sizeof(msg->cmd_type) - 1);
        
        // 确定命令类型
        if (strcmp(cmd_type_str, "Catalog") == 0) {
            msg->cmd_type_enum = GB_CMD_CATALOG;
        } else if (strcmp(cmd_type_str, "DeviceInfo") == 0) {
            msg->cmd_type_enum = GB_CMD_DEVICEINFO;
        } else if (strcmp(cmd_type_str, "DeviceStatus") == 0) {
            msg->cmd_type_enum = GB_CMD_DEVICESTATUS;
        } else if (strcmp(cmd_type_str, "Keepalive") == 0) {
            msg->cmd_type_enum = GB_CMD_KEEPALIVE;
        } else if (strcmp(cmd_type_str, "DeviceControl") == 0) {
            msg->cmd_type_enum = GB_CMD_PTZ_CONTROL;
        } else if (strcmp(cmd_type_str, "RecordInfo") == 0) {
            msg->cmd_type_enum = GB_CMD_RECORD_INFO;
        } else if (strcmp(cmd_type_str, "Alarm") == 0) {
            msg->cmd_type_enum = GB_CMD_ALARM;
        } else {
            msg->cmd_type_enum = GB_CMD_UNKNOWN;
        }
    }
    
    parse_xml_tag(xml, "DeviceID", msg->device_id, sizeof(msg->device_id));
    parse_xml_tag(xml, "SN", msg->sn, sizeof(msg->sn));
    
    // 根据命令类型解析具体数据
    switch (msg->cmd_type_enum) {
        case GB_CMD_DEVICEINFO:
            parse_xml_tag(xml, "Manufacturer", msg->device_info.manufacturer, 
                         sizeof(msg->device_info.manufacturer));
            parse_xml_tag(xml, "Model", msg->device_info.model, 
                         sizeof(msg->device_info.model));
            parse_xml_tag(xml, "Firmware", msg->device_info.firmware, 
                         sizeof(msg->device_info.firmware));
            break;
            
        case GB_CMD_DEVICESTATUS: {
            char online_str[16];
            if (parse_xml_tag(xml, "Online", online_str, sizeof(online_str)) == 0) {
                msg->data.device_status.online = (strcmp(online_str, "ONLINE") == 0) ? 1 : 0;
            }
            parse_xml_tag(xml, "Status", msg->data.device_status.status, 
                         sizeof(msg->data.device_status.status));
            parse_xml_tag(xml, "Encode", msg->data.device_status.encode, 
                         sizeof(msg->data.device_status.encode));
            parse_xml_tag(xml, "Record", msg->data.device_status.record, 
                         sizeof(msg->data.device_status.record));
            break;
        }
        
        case GB_CMD_CATALOG: {
            char sum_num_str[16];
            if (parse_xml_tag(xml, "SumNum", sum_num_str, sizeof(sum_num_str)) == 0) {
                msg->sum_num = atoi(sum_num_str);
            }
            // 这里可以进一步解析设备列表
            break;
        }
        
        case GB_CMD_PTZ_CONTROL: {
            char ptz_cmd_str[32], speed_str[16];
            if (parse_xml_tag(xml, "PTZCmd", ptz_cmd_str, sizeof(ptz_cmd_str)) == 0) {
                // 解析PTZ命令（16进制字符串）
                msg->data.ptz_control.ptz_cmd = (int)strtol(ptz_cmd_str, NULL, 16);
            }
            if (parse_xml_tag(xml, "Speed", speed_str, sizeof(speed_str)) == 0) {
                msg->data.ptz_control.speed = atoi(speed_str);
            }
            break;
        }
        
        default:
            break;
    }
    
    return 0;
}

// ==================== SIP消息解析 ====================

int parse_sip_register(osip_message_t *sip_msg, char *device_id, char *contact_uri, char *user_agent) {
    if (!sip_msg) return -1;
    
    // 从From头提取设备ID
    if (sip_msg->from && sip_msg->from->url && sip_msg->from->url->username) {
        if (device_id) {
            strncpy(device_id, sip_msg->from->url->username, 63);
            device_id[63] = '\0';
        }
    }
    
    // 从Contact头提取联系地址
    osip_contact_t *contact = NULL;
    osip_message_get_contact(sip_msg, 0, &contact);
    if (contact && contact->url) {
        if (contact_uri) {
            char *uri_str = NULL;
            osip_uri_to_str(contact->url, &uri_str);
            if (uri_str) {
                strncpy(contact_uri, uri_str, 255);
                contact_uri[255] = '\0';
                osip_free(uri_str);
            }
        }
    }
    
    // 提取User-Agent
    osip_header_t *ua_header = NULL;
    osip_message_get_user_agent(sip_msg, 0, &ua_header);
    if (ua_header && ua_header->hvalue) {
        if (user_agent) {
            strncpy(user_agent, ua_header->hvalue, 255);
            user_agent[255] = '\0';
        }
    }
    
    return 0;
}

// ==================== 消息发送 ====================

// ==================== 消息发送（已废弃，保留用于兼容性） ====================
// 注意：以下函数基于旧的队列机制，已被新的同步发送函数替代
// 新代码请使用：exosip_send_response_wrapper() 和 exosip_send_message_with_content_type()

int sip_send_register_response(SipContext *ctx, int tid, int code, const char *auth_header) {
    // 废弃：请使用 exosip_send_response_wrapper()
    return exosip_send_response_wrapper(ctx, tid, code, NULL, auth_header);
}

int sip_send_message_response(SipContext *ctx, int tid, int code, const char *body) {
    // 废弃：请使用 exosip_send_response_wrapper()
    return exosip_send_response_wrapper(ctx, tid, code, NULL, NULL);
}

int sip_send_invite(SipContext *ctx, const char *target_uri, const char *sdp_body) {
    // TODO: 实现同步发送INVITE
    fprintf(stderr, "[WARN] sip_send_invite not yet implemented with sync send\n");
    return -1;
}

int sip_send_bye(SipContext *ctx, int call_id, int dialog_id) {
    // TODO: 实现同步发送BYE
    fprintf(stderr, "[WARN] sip_send_bye not yet implemented with sync send\n");
    return -1;
}

int sip_send_ack(SipContext *ctx, int dialog_id) {
    // TODO: 实现同步发送ACK
    fprintf(stderr, "[WARN] sip_send_ack not yet implemented with sync send\n");
    return -1;
}

int sip_send_message(SipContext *ctx, const char *target_uri, const char *content_type, const char *body) {
    // 废弃：请使用 exosip_send_message_with_content_type()
    return exosip_send_message_with_content_type(ctx, target_uri, body, content_type);
}

// ==================== GB28181专用功能 ====================

int sip_send_catalog_query(SipContext *ctx, const char *device_id) {
    if (!ctx || !device_id) return -1;
    
    ConnectionInfo *conn = sip_find_connection_by_device(ctx, device_id);
    if (!conn) return -1;
    
    char *sn = generate_sn();
    if (!sn) return -1;
    
    char xml_body[1024];
    snprintf(xml_body, sizeof(xml_body),
        "<?xml version=\"1.0\"?>\r\n"
        "<Query>\r\n"
        "<CmdType>Catalog</CmdType>\r\n"
        "<SN>%s</SN>\r\n"
        "<DeviceID>%s</DeviceID>\r\n"
        "</Query>\r\n",
        sn, device_id);
    
    int ret = sip_send_message(ctx, conn->contact_uri, "Application/MANSCDP+xml", xml_body);
    
    free(sn);
    return ret;
}

int sip_send_device_info_query(SipContext *ctx, const char *device_id) {
    if (!ctx || !device_id) return -1;
    
    ConnectionInfo *conn = sip_find_connection_by_device(ctx, device_id);
    if (!conn) return -1;
    
    char *sn = generate_sn();
    if (!sn) return -1;
    
    char xml_body[1024];
    snprintf(xml_body, sizeof(xml_body),
        "<?xml version=\"1.0\"?>\r\n"
        "<Query>\r\n"
        "<CmdType>DeviceInfo</CmdType>\r\n"
        "<SN>%s</SN>\r\n"
        "<DeviceID>%s</DeviceID>\r\n"
        "</Query>\r\n",
        sn, device_id);
    
    int ret = sip_send_message(ctx, conn->contact_uri, "Application/MANSCDP+xml", xml_body);
    
    free(sn);
    return ret;
}

int sip_send_ptz_control(SipContext *ctx, const char *device_id, const char *channel_id, int cmd, int speed) {
    if (!ctx || !device_id || !channel_id) return -1;
    
    ConnectionInfo *conn = sip_find_connection_by_device(ctx, device_id);
    if (!conn) return -1;
    
    char *sn = generate_sn();
    if (!sn) return -1;
    
    // 构建PTZ命令（简化版）
    char ptz_cmd_hex[32];
    snprintf(ptz_cmd_hex, sizeof(ptz_cmd_hex), "A50F%02X%02X00%02X0000", cmd, speed, speed);
    
    char xml_body[1024];
    snprintf(xml_body, sizeof(xml_body),
        "<?xml version=\"1.0\"?>\r\n"
        "<Control>\r\n"
        "<CmdType>DeviceControl</CmdType>\r\n"
        "<SN>%s</SN>\r\n"
        "<DeviceID>%s</DeviceID>\r\n"
        "<PTZCmd>%s</PTZCmd>\r\n"
        "</Control>\r\n",
        sn, channel_id, ptz_cmd_hex);
    
    int ret = sip_send_message(ctx, conn->contact_uri, "Application/MANSCDP+xml", xml_body);
    
    free(sn);
    return ret;
}

int sip_send_keepalive_response(SipContext *ctx, int tid) {
    return sip_send_message_response(ctx, tid, 200, NULL);
}

// ==================== 事件处理 ====================

// ==================== 旧的事件线程实现（已废弃） ====================
// 注意：此函数基于独立事件线程模式，已被单线程事件循环替代
// 现在使用 PHP run() 方法中的事件循环，不再需要独立线程

#if 0  // 废弃代码，保留用于参考
void* sip_event_thread(void *arg) {
    SipContext *ctx = (SipContext*)arg;
    int debug = ctx->server_info.debug;
    
    if (debug) fprintf(stderr, "[DEBUG] Event thread started\n");
    
    while (ctx->running) {
        eXosip_event_t *evt = eXosip_event_wait(ctx->ctx, 0, 20);
        
        eXosip_lock(ctx->ctx);
        eXosip_automatic_action(ctx->ctx);
        eXosip_unlock(ctx->ctx);
        
        if (evt) {
            if (debug) fprintf(stderr, "[DEBUG] Event: type=%d, tid=%d\n", evt->type, evt->tid);
            handle_sip_event(ctx, evt);
            eXosip_event_free(evt);
        }
        
        Task *task;
        while ((task = task_queue_try_pop(&ctx->task_queue)) != NULL) {
            switch (task->type) {
                case TASK_SEND_MESSAGE: {
                    osip_message_t *msg = NULL;
                    eXosip_lock(ctx->ctx);
                    int ret = eXosip_message_build_request(ctx->ctx, &msg, "MESSAGE", 
                                                          task->target_uri, task->from_uri, NULL);
                    if (ret == 0 && msg) {
                        osip_message_set_body(msg, task->body, strlen(task->body));
                        osip_message_set_content_type(msg, task->content_type);
                        eXosip_message_send_request(ctx->ctx, msg);
                    }
                    eXosip_unlock(ctx->ctx);
                    break;
                }
                case TASK_SEND_MESSAGE_RESPONSE: {
                    osip_message_t *resp = NULL;
                    eXosip_lock(ctx->ctx);
                    int ret = eXosip_message_build_answer(ctx->ctx, task->tid, task->code, &resp);
                    if (ret == 0 && resp) {
                        if (task->body[0] != '\0') {
                            osip_message_set_body(resp, task->body, strlen(task->body));
                            osip_message_set_content_type(resp, "Application/MANSCDP+xml");
                        }
                        // 添加自定义headers（如WWW-Authenticate）
                        if (task->headers[0] != '\0') {
                            // 格式："WWW-Authenticate: Digest realm=..."
                            if (strstr(task->headers, "WWW-Authenticate:") != NULL) {
                                const char *value = strchr(task->headers, ':');
                                if (value) {
                                    value++; // 跳过':'
                                    while (*value == ' ') value++; // 跳过空格
                                    osip_message_set_www_authenticate(resp, value);
                                }
                            } else if (strstr(task->headers, "Expires:") != NULL) {
                                const char *value = strchr(task->headers, ':');
                                if (value) {
                                    value++;
                                    while (*value == ' ') value++;
                                    osip_message_set_expires(resp, value);
                                }
                            }
                        }
                        eXosip_message_send_answer(ctx->ctx, task->tid, task->code, resp);
                    }
                    eXosip_unlock(ctx->ctx);
                    break;
                }
                case TASK_SEND_REGISTER_RESPONSE: {
                    osip_message_t *resp = NULL;
                    eXosip_lock(ctx->ctx);
                    eXosip_message_build_answer(ctx->ctx, task->tid, task->code, &resp);
                    if (resp) {
                        if (task->code == 401 && task->body[0] != '\0') {
                            osip_message_set_www_authenticate(resp, task->body);
                        }
                        eXosip_message_send_answer(ctx->ctx, task->tid, task->code, resp);
                    }
                    eXosip_unlock(ctx->ctx);
                    break;
                }
                case TASK_SEND_INVITE: {
                    osip_message_t *inv = NULL;
                    eXosip_lock(ctx->ctx);
                    int cid = eXosip_call_build_initial_invite(ctx->ctx, &inv, 
                                                              task->target_uri, task->from_uri, NULL, NULL);
                    if (cid >= 0 && inv) {
                        if (task->body[0] != '\0') {
                            osip_message_set_body(inv, task->body, strlen(task->body));
                            osip_message_set_content_type(inv, "application/sdp");
                        }
                        eXosip_call_send_initial_invite(ctx->ctx, inv);
                    }
                    eXosip_unlock(ctx->ctx);
                    break;
                }
                case TASK_SEND_BYE: {
                    eXosip_lock(ctx->ctx);
                    eXosip_call_terminate(ctx->ctx, task->call_id, task->dialog_id);
                    eXosip_unlock(ctx->ctx);
                    break;
                }
                case TASK_SEND_ACK: {
                    osip_message_t *ack = NULL;
                    eXosip_lock(ctx->ctx);
                    int ret = eXosip_call_build_ack(ctx->ctx, task->dialog_id, &ack);
                    if (ret == 0 && ack) {
                        eXosip_call_send_ack(ctx->ctx, task->dialog_id, ack);
                    }
                    eXosip_unlock(ctx->ctx);
                    break;
                }
                default:
                    break;
            }
            free(task);
        }
        
        if (!evt) usleep(1000);
    }
    
    if (debug) fprintf(stderr, "[DEBUG] Event thread stopped\n");
    return NULL;
}
#endif  // 废弃代码结束

void handle_sip_event(SipContext *ctx, eXosip_event_t *evt) {
    if (!ctx || !evt) return;
    
    ctx->stats.total_messages++;
    
    // ✅ C层不再处理具体业务逻辑
    // 所有事件都通过通用事件回调转发给PHP层
    // PHP层根据事件类型和method字段自行处理
    
    if (ctx->callbacks_valid && !Z_ISUNDEF(ctx->event_callback)) {
        pthread_mutex_lock(&ctx->lock);
        
        zval event_arr;
        sip_event_to_php_array(evt, NULL, NULL, &event_arr);
        
        zval retval;
        zval params[1];
        ZVAL_COPY(&params[0], &event_arr);
        
        zend_fcall_info fci;
        zend_fcall_info_cache fcc;
        
        if (zend_fcall_info_init(&ctx->event_callback, 0, &fci, &fcc, NULL, NULL) == SUCCESS) {
            fci.retval = &retval;
            fci.param_count = 1;
            fci.params = params;
            
            if (zend_call_function(&fci, &fcc) == SUCCESS) {
                zval_ptr_dtor(&retval);
            }
        }
        
        zval_ptr_dtor(&params[0]);
        zval_ptr_dtor(&event_arr);
        
        pthread_mutex_unlock(&ctx->lock);
    }
}

// ==================== PHP数据转换函数 ====================

void sip_event_to_php_array(eXosip_event_t *evt, ConnectionInfo *conn, SessionInfo *session, zval *arr) {
    array_init(arr);
    
    // 事件基本信息
    add_assoc_long(arr, "type", evt->type);
    add_assoc_long(arr, "tid", evt->tid);
    add_assoc_long(arr, "cid", evt->cid);
    add_assoc_long(arr, "did", evt->did);
    add_assoc_long(arr, "rid", evt->rid);
    
    // 事件类型字符串
    const char *type_name = "UNKNOWN";
    switch (evt->type) {
        case EXOSIP_MESSAGE_NEW: type_name = "MESSAGE_NEW"; break;
        case EXOSIP_CALL_INVITE: type_name = "CALL_INVITE"; break;
        case EXOSIP_CALL_ANSWERED: type_name = "CALL_ANSWERED"; break;
        case EXOSIP_CALL_CLOSED: type_name = "CALL_CLOSED"; break;
    }
    add_assoc_string(arr, "type_name", (char*)type_name);
    
    // 原始消息
    if (evt->request) {
        char *msg_str = NULL;
        size_t msg_len = 0;
        osip_message_to_str(evt->request, &msg_str, &msg_len);
        if (msg_str) {
            add_assoc_string(arr, "request", msg_str);
            osip_free(msg_str);
        }
        
        // 请求URI
        if (evt->request->req_uri) {
            char *uri_str = NULL;
            osip_uri_to_str(evt->request->req_uri, &uri_str);
            if (uri_str) {
                add_assoc_string(arr, "request_uri", uri_str);
                osip_free(uri_str);
            }
        }
        
        // 方法
        if (evt->request->sip_method) {
            add_assoc_string(arr, "method", evt->request->sip_method);
        }
        
        // From
        if (evt->request->from && evt->request->from->url) {
            char *from_str = NULL;
            osip_uri_to_str(evt->request->from->url, &from_str);
            if (from_str) {
                add_assoc_string(arr, "from_uri", from_str);
                osip_free(from_str);
            }
            
            if (evt->request->from->url->username) {
                add_assoc_string(arr, "from_username", evt->request->from->url->username);
            }
            if (evt->request->from->url->host) {
                add_assoc_string(arr, "from_host", evt->request->from->url->host);
            }
        }
        
        // To
        if (evt->request->to && evt->request->to->url) {
            char *to_str = NULL;
            osip_uri_to_str(evt->request->to->url, &to_str);
            if (to_str) {
                add_assoc_string(arr, "to_uri", to_str);
                osip_free(to_str);
            }
            
            if (evt->request->to->url->username) {
                add_assoc_string(arr, "to_username", evt->request->to->url->username);
            }
        }
        
        // 消息体
        osip_body_t *body = NULL;
        osip_message_get_body(evt->request, 0, &body);
        if (body && body->body) {
            add_assoc_string(arr, "body", body->body);
        }
    }
    
    if (evt->response) {
        char *msg_str = NULL;
        size_t msg_len = 0;
        osip_message_to_str(evt->response, &msg_str, &msg_len);
        if (msg_str) {
            add_assoc_string(arr, "response", msg_str);
            osip_free(msg_str);
        }
        
        add_assoc_long(arr, "status_code", evt->response->status_code);
        if (evt->response->reason_phrase) {
            add_assoc_string(arr, "reason_phrase", evt->response->reason_phrase);
        }
    }
    
    // 连接信息
    if (conn) {
        zval conn_arr;
        connection_to_php_array(conn, &conn_arr);
        add_assoc_zval(arr, "connection", &conn_arr);
    }
    
    // 会话信息
    if (session) {
        zval session_arr;
        session_to_php_array(session, &session_arr);
        add_assoc_zval(arr, "session", &session_arr);
    }
    
    // 时间戳
    add_assoc_long(arr, "timestamp", time(NULL));
}

void connection_to_php_array(ConnectionInfo *conn, zval *arr) {
    array_init(arr);
    
    add_assoc_long(arr, "id", conn->id);
    add_assoc_string(arr, "device_id", conn->device_id);
    add_assoc_string(arr, "ip", conn->ip);
    add_assoc_long(arr, "port", conn->port);
    add_assoc_string(arr, "contact_uri", conn->contact_uri);
    add_assoc_string(arr, "user_agent", conn->user_agent);
    add_assoc_long(arr, "state", conn->state);
    add_assoc_long(arr, "created_at", conn->created_at);
    add_assoc_long(arr, "last_seen", conn->last_seen);
    add_assoc_long(arr, "register_count", conn->register_count);
    add_assoc_long(arr, "message_count", conn->message_count);
    
    // 状态字符串
    const char *state_name = "UNKNOWN";
    switch (conn->state) {
        case CONN_STATE_IDLE: state_name = "IDLE"; break;
        case CONN_STATE_DISCONNECTED: state_name = "DISCONNECTED"; break;
        case CONN_STATE_REGISTERING: state_name = "REGISTERING"; break;
        case CONN_STATE_REGISTERED: state_name = "REGISTERED"; break;
        case CONN_STATE_CALLING: state_name = "CALLING"; break;
        case CONN_STATE_INCALL: state_name = "INCALL"; break;
        case CONN_STATE_ERROR: state_name = "ERROR"; break;
    }
    add_assoc_string(arr, "state_name", (char*)state_name);
}

void session_to_php_array(SessionInfo *session, zval *arr) {
    array_init(arr);
    
    add_assoc_long(arr, "id", session->id);
    add_assoc_long(arr, "connection_id", session->connection_id);
    add_assoc_long(arr, "type", session->type);
    add_assoc_long(arr, "call_id", session->call_id);
    add_assoc_long(arr, "dialog_id", session->dialog_id);
    add_assoc_string(arr, "from_uri", session->from_uri);
    add_assoc_string(arr, "to_uri", session->to_uri);
    add_assoc_string(arr, "sdp_local", session->sdp_local);
    add_assoc_string(arr, "sdp_remote", session->sdp_remote);
    add_assoc_string(arr, "raw_body", session->raw_body);  // 添加raw_body字段
    add_assoc_long(arr, "created_at", session->created_at);
    add_assoc_long(arr, "updated_at", session->updated_at);
    
    // 会话类型字符串
    const char *type_name = "UNKNOWN";
    switch (session->type) {
        case SESSION_TYPE_UNKNOWN: type_name = "UNKNOWN"; break;
        case SESSION_TYPE_REGISTER: type_name = "REGISTER"; break;
        case SESSION_TYPE_VIDEO_PREVIEW: type_name = "VIDEO_PREVIEW"; break;
        case SESSION_TYPE_VIDEO_PLAYBACK: type_name = "VIDEO_PLAYBACK"; break;
        case SESSION_TYPE_AUDIO_TALK: type_name = "AUDIO_TALK"; break;
        case SESSION_TYPE_PTZ_CONTROL: type_name = "PTZ_CONTROL"; break;
        case SESSION_TYPE_MESSAGE: type_name = "MESSAGE"; break;
    }
    add_assoc_string(arr, "type_name", (char*)type_name);
}

void gb28181_message_to_php_array(GB28181Message *msg, zval *arr) {
    array_init(arr);
    
    add_assoc_string(arr, "cmd_type", msg->cmd_type);
    add_assoc_string(arr, "sn", msg->sn);
    add_assoc_string(arr, "device_id", msg->device_id);
    add_assoc_long(arr, "sum_num", msg->sum_num);
    
    // 根据命令类型添加特定信息
    if (strcmp(msg->cmd_type, "Keepalive") == 0) {
        add_assoc_string(arr, "status", "OK");
    } else if (strcmp(msg->cmd_type, "Catalog") == 0) {
        zval item_arr;
        array_init(&item_arr);
        
        for (int i = 0; i < msg->sum_num && i < MAX_CATALOG_ITEMS; i++) {
            CatalogItem *item = &msg->catalog_items[i];
            
            zval item_info;
            array_init(&item_info);
            
            add_assoc_string(&item_info, "device_id", item->device_id);
            add_assoc_string(&item_info, "name", item->name);
            add_assoc_string(&item_info, "manufacturer", item->manufacturer);
            add_assoc_string(&item_info, "model", item->model);
            add_assoc_string(&item_info, "owner", item->owner);
            add_assoc_string(&item_info, "civil_code", item->civil_code);
            add_assoc_string(&item_info, "address", item->address);
            add_assoc_string(&item_info, "parental", item->parental);
            add_assoc_string(&item_info, "parent_id", item->parent_id);
            add_assoc_string(&item_info, "safety_way", item->safety_way);
            add_assoc_string(&item_info, "register_way", item->register_way);
            add_assoc_string(&item_info, "secrecy", item->secrecy);
            add_assoc_string(&item_info, "status", item->status);
            
            add_index_zval(&item_arr, i, &item_info);
        }
        
        add_assoc_zval(arr, "items", &item_arr);
    } else if (strcmp(msg->cmd_type, "DeviceInfo") == 0) {
        DeviceInfo *info = &msg->device_info;
        zval info_arr;
        array_init(&info_arr);
        
        add_assoc_string(&info_arr, "device_name", info->device_name);
        add_assoc_string(&info_arr, "manufacturer", info->manufacturer);
        add_assoc_string(&info_arr, "model", info->model);
        add_assoc_string(&info_arr, "firmware", info->firmware);
        add_assoc_long(&info_arr, "channel", info->channel);
        
        add_assoc_zval(arr, "device_info", &info_arr);
    }
    
    // 添加原始XML
    add_assoc_string(arr, "raw_xml", msg->raw_xml);
}

// ==================== PHP兼容性接口 ====================
// 这些函数提供与PHP扩展期望的函数名兼容

SipContext* exosip_init_wrapper(ServerInfo *info) {
    return sip_init(info);
}

void exosip_quit_wrapper(SipContext *ctx) {
    sip_destroy(ctx);
}

void exosip_event_loop_php(SipContext *ctx, zval *callback) {
    if (!ctx) return;
    
    // 设置事件回调
    if (callback) {
        sip_set_event_callback(ctx, callback);
    }
    
    // 启动事件循环
    sip_start(ctx);
}

// 为了兼容可能需要的其他函数名
int exosip_listen_wrapper(SipContext *ctx) {
    return sip_start(ctx);
}

int exosip_stop_wrapper(SipContext *ctx) {
    return sip_stop(ctx);
}

void exosip_set_callbacks_wrapper(SipContext *ctx, zval *event_cb, zval *conn_cb, zval *msg_cb, zval *err_cb) {
    if (event_cb) sip_set_event_callback(ctx, event_cb);
    if (conn_cb) sip_set_connection_callback(ctx, conn_cb);
    if (msg_cb) sip_set_message_callback(ctx, msg_cb);
    if (err_cb) sip_set_error_callback(ctx, err_cb);
}

// ==================== 新的非阻塞API实现 ====================

/**
 * 非阻塞获取SIP事件
 * @param ctx SIP上下文
 * @param events_array PHP数组，用于存储事件对象
 * @param timeout_ms 超时毫秒数
 * @return 获取到的事件数量，-1表示错误
 */
int exosip_get_events_nonblocking(SipContext *ctx, zval *events_array, int timeout_ms) {
    if (!ctx || !ctx->ctx || !events_array) {
        return -1;
    }

    int debug = ctx->server_info.debug;
    eXosip_event_t *evt;
    int event_count = 0;
    
    array_init(events_array);

    eXosip_lock(ctx->ctx);
    eXosip_automatic_action(ctx->ctx);
    eXosip_unlock(ctx->ctx);

    int tv_sec = timeout_ms / 1000;
    int tv_ms = timeout_ms % 1000;

    while ((evt = eXosip_event_wait(ctx->ctx, tv_sec, tv_ms)) != NULL) {
        if (debug) fprintf(stderr, "[DEBUG] Event: type=%d, tid=%d\n", evt->type, evt->tid);
        
        // 处理标准SIP自动响应（401/407/3xx等）
        eXosip_lock(ctx->ctx);
        eXosip_automatic_action(ctx->ctx);
        eXosip_unlock(ctx->ctx);
        
        // 查找对应的连接和会话
        ConnectionInfo *conn = NULL;
        SessionInfo *session = NULL;
        
        // 对于 REGISTER 事件，尝试创建或查找 Connection
        if (evt->type == EXOSIP_MESSAGE_NEW && evt->request && MSG_IS_REGISTER(evt->request)) {
            char device_id[64] = {0};
            char contact_uri[256] = {0};
            char user_agent[256] = {0};
            parse_sip_register(evt->request, device_id, contact_uri, user_agent);
            
            if (strlen(device_id) > 0) {
                conn = sip_find_connection_by_device(ctx, device_id);
                if (!conn) {
                    pthread_mutex_lock(&ctx->lock);
                    for (int i = 0; i < MAX_CONNECTIONS; i++) {
                        if (ctx->connections[i].id == 0) {
                            conn = &ctx->connections[i];
                            conn->id = i + 1;
                            strncpy(conn->device_id, device_id, sizeof(conn->device_id) - 1);
                            conn->created_at = time(NULL);
                            ctx->connection_count++;
                            break;
                        }
                    }
                    pthread_mutex_unlock(&ctx->lock);
                }
                
                if (conn) {
                    // 更新连接信息
                    osip_via_t *via = NULL;
                    osip_message_get_via(evt->request, 0, &via);
                    if (via && via->host) {
                        strncpy(conn->ip, via->host, sizeof(conn->ip) - 1);
                        conn->port = via->port ? atoi(via->port) : 5060;
                    }
                    strncpy(conn->contact_uri, contact_uri, sizeof(conn->contact_uri) - 1);
                    strncpy(conn->user_agent, user_agent, sizeof(conn->user_agent) - 1);
                    conn->last_seen = time(NULL);
                    conn->register_count++;
                    conn->state = CONN_STATE_REGISTERED;
                }
            }
        }
        
        if (evt->rid > 0 && !conn) {
            // 根据rid查找已存储的连接
            for (int i = 0; i < ctx->connection_count; i++) {
                if (ctx->connections[i].id == evt->rid) {
                    conn = &ctx->connections[i];
                    break;
                }
            }
        }
        
        if (evt->cid > 0 || evt->did > 0) {
            // 根据call_id或dialog_id查找会话
            // 重要：检查session.id是否有效，防止访问已清零的会话
            for (int i = 0; i < MAX_SESSIONS; i++) {
                if (ctx->sessions[i].id > 0 &&  // 检查会话是否有效
                    (ctx->sessions[i].call_id == evt->cid || 
                     ctx->sessions[i].dialog_id == evt->did)) {
                    session = &ctx->sessions[i];
                    break;
                }
            }
        }
        
        // 如果有消息体，保存到session的raw_body中
        // 使用 osip_message_get_body API 而不是直接访问 bodies 链表
        if (session && evt->request) {
            osip_body_t *body = NULL;
            // 使用官方 API 获取消息体，这会处理所有的边界情况
            if (osip_message_get_body(evt->request, 0, &body) == 0 && body && body->body) {
                strncpy(session->raw_body, body->body, sizeof(session->raw_body) - 1);
                session->raw_body[sizeof(session->raw_body) - 1] = '\0';
                session->updated_at = time(NULL);
            }
        } else if (session && evt->response) {
            osip_body_t *body = NULL;
            // 使用官方 API 获取消息体
            if (osip_message_get_body(evt->response, 0, &body) == 0 && body && body->body) {
                strncpy(session->raw_body, body->body, sizeof(session->raw_body) - 1);
                session->raw_body[sizeof(session->raw_body) - 1] = '\0';
                session->updated_at = time(NULL);
            }
        }

        // 创建事件对象
        zval event_obj;
        exosip_create_event_object_array(evt, conn, session, &event_obj);
        
        // 添加到事件数组
        add_next_index_zval(events_array, &event_obj);
        event_count++;

        eXosip_event_free(evt);
        
        tv_sec = 0;
        tv_ms = 0;
    }

    return event_count;
}

/**
 * 获取eXosip的socket文件描述符
 * @param ctx SIP上下文
 * @return 文件描述符，-1表示不支持或错误
 */
int exosip_get_socket_fd(SipContext *ctx) {
    if (!ctx || !ctx->ctx) {
        return -1;
    }

    // eXosip2可能没有直接提供FD访问，这里返回-1表示不支持
    // 实际使用中，建议使用定时器轮询模式
    return -1;
}

/**
 * 发送SIP消息
 * @param ctx SIP上下文
 * @param to 目标URI
 * @param message 消息内容
 * @return 0成功，-1失败
 */
int exosip_send_message_wrapper(SipContext *ctx, const char *to, const char *message) {
    return exosip_send_message_with_content_type(ctx, to, message, "application/MANSCDP+xml");
}

int exosip_send_message_with_content_type(SipContext *ctx, const char *to, const char *message, const char *content_type) {
    if (!ctx || !to || !message) {
        return -1;
    }

    int debug = ctx->server_info.debug;
    char from_uri[256];
    snprintf(from_uri, sizeof(from_uri), "sip:%s@%s:%d", 
             ctx->server_info.sipId, ctx->server_info.ip, ctx->server_info.port);
    
    const char *ct = content_type ? content_type : "application/MANSCDP+xml";
    
    osip_message_t *msg = NULL;
    eXosip_lock(ctx->ctx);
    
    int ret = eXosip_message_build_request(ctx->ctx, &msg, "MESSAGE", to, from_uri, NULL);
    if (ret == 0 && msg) {
        osip_message_set_body(msg, message, strlen(message));
        osip_message_set_content_type(msg, ct);
        
        int send_ret = eXosip_message_send_request(ctx->ctx, msg);
        if (debug) fprintf(stderr, "[DEBUG] Send MESSAGE result: %d\n", send_ret);
        
        eXosip_unlock(ctx->ctx);
        return send_ret > 0 ? 0 : -1;
    }
    
    eXosip_unlock(ctx->ctx);
    if (debug) fprintf(stderr, "[DEBUG] Failed to build MESSAGE: %d\n", ret);
    return -1;
}

/**
 * 发送SIP响应（同步发送）
 * @param ctx SIP上下文
 * @param tid 事务ID
 * @param code 响应码
 * @param reason 响应原因
 * @param headers 自定义头（如WWW-Authenticate）
 * @return 0成功，-1失败
 * 
 * 注意：此函数在单线程事件循环中调用是安全的
 * 因为 eXosip_event_wait() 返回后锁已释放
 */
int exosip_send_response_wrapper(SipContext *ctx, int tid, int code, const char *reason, const char *headers) {
    if (!ctx || tid <= 0) {
        return -1;
    }

    int debug = ctx->server_info.debug;
    osip_message_t *resp = NULL;
    
    eXosip_lock(ctx->ctx);
    
    int ret = eXosip_message_build_answer(ctx->ctx, tid, code, &resp);
    if (ret == 0 && resp) {
        // 添加自定义头（如 WWW-Authenticate）
        if (headers && strlen(headers) > 0) {
            char *header_copy = strdup(headers);
            char *line = strtok(header_copy, "\r\n");
            
            while (line) {
                char *colon = strchr(line, ':');
                if (colon) {
                    *colon = '\0';
                    char *name = line;
                    char *value = colon + 1;
                    while (*value == ' ') value++;
                    
                    osip_message_set_header(resp, name, value);
                    
                    // 特殊处理 Expires 头
                    if (strcasecmp(name, "Expires") == 0) {
                        osip_message_set_expires(resp, value);
                    }
                }
                line = strtok(NULL, "\r\n");
            }
            free(header_copy);
        }
        
        // 调试：打印完整的响应消息
        if (debug) {
            char *msg_str = NULL;
            size_t msg_len = 0;
            if (osip_message_to_str(resp, &msg_str, &msg_len) == 0 && msg_str) {
                fprintf(stderr, "[DEBUG] Sending Response:\n%s\n", msg_str);
                osip_free(msg_str);
            }
        }
        
        int send_ret = eXosip_message_send_answer(ctx->ctx, tid, code, resp);
        if (debug) fprintf(stderr, "[DEBUG] Send response %d result: %d\n", code, send_ret);
        
        eXosip_unlock(ctx->ctx);
        return send_ret == 0 ? 0 : -1;
    }
    
    eXosip_unlock(ctx->ctx);
    if (debug) fprintf(stderr, "[DEBUG] Failed to build response: %d\n", ret);
    return -1;
}

/**
 * 创建SipEvent对象的PHP数组表示
 * @param evt eXosip事件
 * @param conn 连接信息
 * @param session 会话信息
 * @param event_array PHP数组
 */
void exosip_create_event_object_array(eXosip_event_t *evt, ConnectionInfo *conn, SessionInfo *session, zval *event_array) {
    array_init(event_array);
    
    // 基本事件信息
    // 注意：EXOSIP_MESSAGE_NEW(23) 包含 REGISTER 和 MESSAGE，需要添加method字段区分
    add_assoc_long(event_array, "type", evt->type);
    
    // 添加SIP方法字段用于区分REGISTER和MESSAGE
    if (evt->request && evt->request->sip_method) {
        add_assoc_string(event_array, "method", evt->request->sip_method);
    } else {
        add_assoc_null(event_array, "method");
    }
    add_assoc_long(event_array, "tid", evt->tid);
    add_assoc_long(event_array, "cid", evt->cid);
    add_assoc_long(event_array, "did", evt->did);
    add_assoc_long(event_array, "rid", evt->rid);
    add_assoc_long(event_array, "ss_status", evt->ss_status);
    add_assoc_long(event_array, "ss_reason", evt->ss_reason);
    
    // 提取Expires头（用于区分注册和注销）
    int expires = -1;
    osip_message_t *msg = evt->request ? evt->request : evt->response;
    if (msg) {
        osip_header_t *expires_header = NULL;
        osip_message_get_expires(msg, 0, &expires_header);
        if (expires_header && expires_header->hvalue) {
            expires = atoi(expires_header->hvalue);
        }
    }
    add_assoc_long(event_array, "expires", expires);

    // URI信息
    if (evt->request) {
        if (evt->request->from && evt->request->from->url) {
            char *from_uri = NULL;
            osip_uri_to_str(evt->request->from->url, &from_uri);
            if (from_uri) {
                add_assoc_string(event_array, "from_uri", from_uri);
                osip_free(from_uri);
            }
        }
        
        if (evt->request->to && evt->request->to->url) {
            char *to_uri = NULL;
            osip_uri_to_str(evt->request->to->url, &to_uri);
            if (to_uri) {
                add_assoc_string(event_array, "to_uri", to_uri);
                osip_free(to_uri);
            }
        }
        
        if (evt->request->req_uri) {
            char *req_uri = NULL;
            osip_uri_to_str(evt->request->req_uri, &req_uri);
            if (req_uri) {
                add_assoc_string(event_array, "request_uri", req_uri);
                osip_free(req_uri);
            }
        }
    }

    // 消息体和Content-Type（参考标准GB28181实现）
    const char *body = NULL;
    const char *content_type = NULL;
    
    // msg 已在上面定义，复用
    if (msg) {
        // 使用标准osip API获取body
        osip_body_t *osip_body = NULL;
        if (osip_message_get_body(msg, 0, &osip_body) == 0 && osip_body && osip_body->body) {
            body = osip_body->body;
        }
        
        // 获取Content-Type
        osip_content_type_t *ct = osip_message_get_content_type(msg);
        if (ct && ct->type && ct->subtype) {
            static char ct_buffer[256];
            snprintf(ct_buffer, sizeof(ct_buffer), "%s/%s", ct->type, ct->subtype);
            content_type = ct_buffer;
        }
    }
    
    if (body) {
        add_assoc_string(event_array, "body", body);
    } else {
        add_assoc_null(event_array, "body");
    }
    
    if (content_type) {
        add_assoc_string(event_array, "content_type", content_type);
    } else {
        add_assoc_null(event_array, "content_type");
    }
    
    // 提取所有 SIP 头字段
    zval headers_array;
    array_init(&headers_array);
    
    if (msg) {
        // 1. 添加标准 SIP 头（这些有专门的字段）
        
        // CSeq
        if (msg->cseq && msg->cseq->number) {
            char cseq_str[128];
            snprintf(cseq_str, sizeof(cseq_str), "%s %s", 
                    msg->cseq->number, msg->cseq->method ? msg->cseq->method : "");
            add_assoc_string(&headers_array, "CSeq", cseq_str);
        }
        
        // Call-ID
        if (msg->call_id && msg->call_id->number) {
            add_assoc_string(&headers_array, "Call-ID", msg->call_id->number);
        }
        
        // Expires
        osip_header_t *expires_h = NULL;
        osip_message_get_expires(msg, 0, &expires_h);
        if (expires_h && expires_h->hvalue) {
            add_assoc_string(&headers_array, "Expires", expires_h->hvalue);
        }
        
        // Authorization
        osip_authorization_t *auth = NULL;
        osip_message_get_authorization(msg, 0, &auth);
        if (auth) {
            char auth_str[1024] = {0};
            int offset = 0;
            
            if (auth->auth_type) {
                offset += snprintf(auth_str + offset, sizeof(auth_str) - offset, "%s ", auth->auth_type);
            }
            if (auth->username) {
                offset += snprintf(auth_str + offset, sizeof(auth_str) - offset, "username=\"%s\"", auth->username);
            }
            if (auth->realm) {
                offset += snprintf(auth_str + offset, sizeof(auth_str) - offset, ",realm=\"%s\"", auth->realm);
            }
            if (auth->nonce) {
                offset += snprintf(auth_str + offset, sizeof(auth_str) - offset, ",nonce=\"%s\"", auth->nonce);
            }
            if (auth->uri) {
                offset += snprintf(auth_str + offset, sizeof(auth_str) - offset, ",uri=\"%s\"", auth->uri);
            }
            if (auth->response) {
                offset += snprintf(auth_str + offset, sizeof(auth_str) - offset, ",response=\"%s\"", auth->response);
            }
            if (auth->algorithm) {
                offset += snprintf(auth_str + offset, sizeof(auth_str) - offset, ",algorithm=%s", auth->algorithm);
            }
            
            if (offset > 0) {
                add_assoc_string(&headers_array, "Authorization", auth_str);
            }
        }
        
        // User-Agent
        osip_header_t *ua = NULL;
        osip_message_get_user_agent(msg, 0, &ua);
        if (ua && ua->hvalue) {
            add_assoc_string(&headers_array, "User-Agent", ua->hvalue);
        }
        
        // Contact
        if (!osip_list_eol(&msg->contacts, 0)) {
            osip_contact_t *contact = (osip_contact_t *)osip_list_get(&msg->contacts, 0);
            if (contact && contact->url) {
                char *contact_str = NULL;
                osip_uri_to_str(contact->url, &contact_str);
                if (contact_str) {
                    add_assoc_string(&headers_array, "Contact", contact_str);
                    osip_free(contact_str);
                }
            }
        }
        
        // 2. 添加自定义头（非标准头在 headers 列表中）
        osip_list_t *custom_headers = &msg->headers;
        int pos = 0;
        osip_header_t *header;
        
        while (!osip_list_eol(custom_headers, pos)) {
            header = (osip_header_t *)osip_list_get(custom_headers, pos);
            if (header && header->hname && header->hvalue) {
                add_assoc_string(&headers_array, header->hname, header->hvalue);
            }
            pos++;
        }
    }
    
    add_assoc_zval(event_array, "headers", &headers_array);

    // 会话信息
    if (session) {
        zval session_array;
        exosip_create_session_object_array(session, &session_array);
        add_assoc_zval(event_array, "session", &session_array);
    } else {
        add_assoc_null(event_array, "session");
    }

    // 连接信息
    if (conn) {
        zval conn_array;
        connection_to_php_array(conn, &conn_array);
        add_assoc_zval(event_array, "connection", &conn_array);
    } else {
        add_assoc_null(event_array, "connection");
    }
}

/**
 * 创建SipSession对象的PHP数组表示
 * @param session 会话信息
 * @param session_array PHP数组
 */
void exosip_create_session_object_array(SessionInfo *session, zval *session_array) {
    array_init(session_array);
    
    add_assoc_long(session_array, "id", session->id);
    add_assoc_long(session_array, "connection_id", session->connection_id);
    add_assoc_long(session_array, "type", session->type);
    add_assoc_long(session_array, "call_id", session->call_id);
    add_assoc_long(session_array, "dialog_id", session->dialog_id);
    add_assoc_string(session_array, "from_uri", session->from_uri);
    add_assoc_string(session_array, "to_uri", session->to_uri);
    add_assoc_string(session_array, "sdp_local", session->sdp_local);
    add_assoc_string(session_array, "sdp_remote", session->sdp_remote);
    add_assoc_string(session_array, "raw_body", session->raw_body);  // 关键：原始XML数据
    add_assoc_long(session_array, "created_at", session->created_at);
    add_assoc_long(session_array, "updated_at", session->updated_at);
}
// ==================== 客户端实现 ====================

// 分配一个可用的随机端口（范围 5060-15060）
static int allocate_random_port(int protocol) {
    int min_port = 5060;
    int max_port = 15060;
    int attempts = 100; // 最多尝试 100 次
    
    srand((unsigned int)time(NULL) + getpid());
    
    for (int i = 0; i < attempts; i++) {
        int port = min_port + (rand() % (max_port - min_port + 1));
        
        // 尝试绑定这个端口验证可用性
        int sock = socket(AF_INET, protocol == IPPROTO_TCP ? SOCK_STREAM : SOCK_DGRAM, 0);
        if (sock < 0) continue;
        
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        
        if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            close(sock);
            return port; // 找到可用端口
        }
        
        close(sock);
    }
    
    return 5060; // 失败回退到默认端口
}

static void* client_event_thread(void *arg) {
    ClientContext *ctx = (ClientContext*)arg;
    
    while (ctx->running) {
        eXosip_event_t *evt = eXosip_event_wait(ctx->ctx, 0, 20);
        
        eXosip_lock(ctx->ctx);
        eXosip_automatic_action(ctx->ctx);
        eXosip_unlock(ctx->ctx);
        
        if (evt) {
            pthread_mutex_lock(&ctx->lock);
            
            if (ctx->config.debug) {
                printf("[CLIENT] Event: %d, TID: %d\n", evt->type, evt->tid);
            }
            
            // 统计
            if (evt->response) {
                ctx->response_count++;
            }
            
            // 处理注册响应
            if (evt->type == EXOSIP_REGISTRATION_SUCCESS) {
                ctx->registered = 1;
                ctx->register_time = time(NULL);
                if (ctx->config.debug) {
                    printf("[CLIENT] Registration successful\n");
                }
            } else if (evt->type == EXOSIP_REGISTRATION_FAILURE) {
                ctx->registered = 0;
                if (ctx->config.debug) {
                    printf("[CLIENT] Registration failed\n");
                }
            }
            
            pthread_mutex_unlock(&ctx->lock);
            eXosip_event_free(evt);
        }
    }
    
    return NULL;
}

ClientContext* client_init(ClientConfig *config) {
    if (!config) return NULL;
    
    ClientContext *ctx = (ClientContext*)calloc(1, sizeof(ClientContext));
    if (!ctx) return NULL;
    
    ctx->ctx = eXosip_malloc();
    if (!ctx->ctx) {
        free(ctx);
        return NULL;
    }
    
    if (eXosip_init(ctx->ctx) != 0) {
        eXosip_quit(ctx->ctx);
        free(ctx);
        return NULL;
    }
    
    // 复制配置
    memcpy(&ctx->config, config, sizeof(ClientConfig));
    
    // 初始化互斥锁
    if (pthread_mutex_init(&ctx->lock, NULL) != 0) {
        eXosip_quit(ctx->ctx);
        free(ctx);
        return NULL;
    }
    
    // 设置 User-Agent
    if (config->user_agent[0]) {
        eXosip_set_user_agent(ctx->ctx, config->user_agent);
    } else {
        eXosip_set_user_agent(ctx->ctx, "PHP-eXosip-Client/2.0");
    }
    
    // 立即监听本地端口（必须在发送任何消息之前）
    int protocol = IPPROTO_UDP;
    if (config->mode[0] && strcasecmp(config->mode, "TCP") == 0) {
        protocol = IPPROTO_TCP;
    }
    
    // 如果 local_port 为 0，自动分配一个随机可用端口
    int actual_port = config->local_port;
    if (actual_port == 0) {
        actual_port = allocate_random_port(protocol);
        ctx->config.local_port = actual_port; // 更新配置中的端口
        if (config->debug) {
            printf("[CLIENT] Auto-allocated port: %d\n", actual_port);
        }
    }
    
    const char *local_ip = config->local_ip[0] ? config->local_ip : NULL;
    int ret = eXosip_listen_addr(ctx->ctx, protocol, local_ip, actual_port, AF_INET, 0);
    if (ret != 0) {
        if (config->debug) {
            fprintf(stderr, "[CLIENT] Listen failed on %s:%d, error=%d\n", 
                    local_ip ? local_ip : "0.0.0.0", actual_port, ret);
        }
        pthread_mutex_destroy(&ctx->lock);
        eXosip_quit(ctx->ctx);
        free(ctx);
        return NULL;
    }
    
    if (config->debug) {
        printf("[CLIENT] Listening on %s:%d (%s)\n", 
               local_ip ? local_ip : "0.0.0.0", 
               actual_port,
               protocol == IPPROTO_TCP ? "TCP" : "UDP");
    }
    
    ctx->running = 0;
    ctx->registered = 0;
    ctx->register_id = -1;
    ctx->callbacks_valid = 0;
    
    if (config->debug) {
        printf("[CLIENT] Initialized\n");
    }
    
    return ctx;
}

int client_start(ClientContext *ctx) {
    if (!ctx || ctx->running) return -1;
    
    // listen 已经在 client_init 中完成，这里只需要启动事件线程
    ctx->running = 1;
    
    // 创建事件处理线程
    if (pthread_create(&ctx->event_thread, NULL, client_event_thread, ctx) != 0) {
        if (ctx->config.debug) {
            fprintf(stderr, "[CLIENT] Failed to create event thread\n");
        }
        ctx->running = 0;
        return -1;
    }
    
    if (ctx->config.debug) {
        printf("[CLIENT] Event thread started\n");
    }
    
    return 0;
}

int client_stop(ClientContext *ctx) {
    if (!ctx || !ctx->running) return -1;
    
    ctx->running = 0;
    
    if (pthread_join(ctx->event_thread, NULL) != 0) {
        return -1;
    }
    
    if (ctx->config.debug) {
        printf("[CLIENT] Stopped\n");
    }
    
    return 0;
}

void client_destroy(ClientContext *ctx) {
    if (!ctx) return;
    
    if (ctx->running) {
        client_stop(ctx);
    }
    
    if (ctx->ctx) {
        eXosip_quit(ctx->ctx);
    }
    
    pthread_mutex_destroy(&ctx->lock);
    free(ctx);
}

int client_send_register(ClientContext *ctx) {
    if (!ctx) return -1;
    
    osip_message_t *reg = NULL;
    char from[512];
    char contact[512];
    char proxy[512];
    int local_port = ctx->config.local_port;
    
    // 确定本地IP：优先使用用户指定的，否则尝试自动获取
    const char *use_local_ip = "127.0.0.1";  // 默认回退值
    if (ctx->config.local_ip[0]) {
        use_local_ip = ctx->config.local_ip;
    } else {
        if (strcmp(ctx->config.server_ip, "127.0.0.1") == 0 || 
            strcmp(ctx->config.server_ip, "localhost") == 0) {
            use_local_ip = "127.0.0.1";
        } else {
            use_local_ip = ctx->config.server_ip;  // 占位，应该用实际本地IP
        }
    }
    
    if (ctx->config.from_uri[0]) {
        // 如果用户指定了 from_uri，直接使用
        strncpy(from, ctx->config.from_uri, sizeof(from) - 1);
    } else if (ctx->config.realm[0]) {
        // GB28181 标准：sip:设备ID@域（行政区划码）
        snprintf(from, sizeof(from), "sip:%s@%s", 
                 ctx->config.username, ctx->config.realm);
    } else {
        // 回退：sip:设备ID@本地IP:本地端口
        snprintf(from, sizeof(from), "sip:%s@%s:%d", 
                 ctx->config.username, use_local_ip, local_port);
    }
    
    // Contact: sip:设备ID@本地IP:本地端口
    // 标准格式：sip:34020000001320000001@127.0.0.1:5070
    snprintf(contact, sizeof(contact), "sip:%s@%s:%d",
             ctx->config.username, use_local_ip, local_port);
    
    // snprintf(proxy, sizeof(proxy), "%s:%d",
    //          ctx->config.server_ip, ctx->config.server_port);
    snprintf(proxy, sizeof(proxy), "sip:%s:%d",
             ctx->config.server_ip, ctx->config.server_port);
    
    if (ctx->config.debug) {
        printf("[CLIENT] Building REGISTER:\n");
        printf("  From: %s\n", from);
        printf("  Contact: %s\n", contact);
        printf("  Proxy: %s\n", proxy);
    }
    
    eXosip_lock(ctx->ctx);
    
    // 构建 REGISTER，明确指定 Contact
    int rid = eXosip_register_build_initial_register(ctx->ctx, from, proxy, contact, ctx->config.expires, &reg);
    if (reg == NULL) {
        eXosip_unlock(ctx->ctx);
        if (ctx->config.debug)
        {
            fprintf(stderr, "[ERROR] eXosip_register_build_initial_register returned NULL message\n");
        }

        return -1;
    }

    if (ctx->config.debug)
    {
        // fprintf reg->status_code and reason_phrase
        fprintf(stderr, "[DEBUG] REGISTER message built: status_code=%d, reason_phrase=%s\n",
                reg->status_code, reg->reason_phrase ? reg->reason_phrase : "N/A");
    }
    
    if (rid < 0) {
        eXosip_unlock(ctx->ctx);
        if (ctx->config.debug) {
            fprintf(stderr, "[ERROR] Failed to build REGISTER: from=%s, proxy=%s, contact=%s\n", 
                    from, proxy, contact);
        }
        return -1;
    }
    
    // 添加 GB28181 必需的头部
    osip_message_set_header(reg, "X-GB-Ver", "2.0");
    
    // 添加认证信息
    if (ctx->config.password[0] && ctx->config.realm[0]) {
        eXosip_add_authentication_info(ctx->ctx, ctx->config.username, 
                                       ctx->config.username, ctx->config.password,
                                       NULL, ctx->config.realm);
    }
    
    // 打印完整的 REGISTER 报文（调试用）
    if (ctx->config.debug) {
        char *msg_str = NULL;
        size_t msg_len = 0;
        osip_message_to_str(reg, &msg_str, &msg_len);
        if (msg_str) {
            printf("[CLIENT] Complete REGISTER message:\n%s\n", msg_str);
            osip_free(msg_str);
        }
    }
    
    int ret = eXosip_register_send_register(ctx->ctx, rid, reg);
    eXosip_unlock(ctx->ctx);
    
    if (ret == 0) {
        pthread_mutex_lock(&ctx->lock);
        ctx->register_id = rid;
        ctx->request_count++;
        pthread_mutex_unlock(&ctx->lock);
        
        if (ctx->config.debug) {
            printf("[CLIENT] REGISTER sent (rid=%d)\n", rid);
        }
    }
    
    return ret;
}

int client_send_unregister(ClientContext *ctx) {
    if (!ctx || ctx->register_id < 0) return -1;
    
    osip_message_t *reg = NULL;
    
    eXosip_lock(ctx->ctx);
    int ret = eXosip_register_build_register(ctx->ctx, ctx->register_id, 0, &reg);
    if (ret == 0 && reg) {
        ret = eXosip_register_send_register(ctx->ctx, ctx->register_id, reg);
    }
    eXosip_unlock(ctx->ctx);
    
    if (ret == 0) {
        pthread_mutex_lock(&ctx->lock);
        ctx->registered = 0;
        ctx->request_count++;
        pthread_mutex_unlock(&ctx->lock);
        
        if (ctx->config.debug) {
            printf("[CLIENT] UNREGISTER sent\n");
        }
    }
    
    return ret;
}

int client_send_message(ClientContext *ctx, const char *to_uri, const char *body, const char *content_type) {
    if (!ctx || !to_uri || !body) return -1;
    
    osip_message_t *msg = NULL;
    char from[512];
    
    if (ctx->config.from_uri[0]) {
        snprintf(from, sizeof(from), "%s", ctx->config.from_uri);
    } else {
        snprintf(from, sizeof(from), "sip:%s@%s:%d",
                 ctx->config.username, ctx->config.server_ip, ctx->config.server_port);
    }
    
    eXosip_lock(ctx->ctx);
    
    int ret = eXosip_message_build_request(ctx->ctx, &msg, "MESSAGE", to_uri, from, NULL);
    if (ret != 0 || !msg) {
        eXosip_unlock(ctx->ctx);
        return -1;
    }
    
    // 设置消息体
    const char *ct = content_type ? content_type : "text/plain";
    osip_message_set_content_type(msg, ct);
    osip_message_set_body(msg, body, strlen(body));
    
    ret = eXosip_message_send_request(ctx->ctx, msg);
    eXosip_unlock(ctx->ctx);
    
    if (ret >= 0) {
        pthread_mutex_lock(&ctx->lock);
        ctx->request_count++;
        pthread_mutex_unlock(&ctx->lock);
        
        if (ctx->config.debug) {
            printf("[CLIENT] MESSAGE sent to %s (tid=%d)\n", to_uri, ret);
        }
    }
    
    return ret;
}

int client_send_invite(ClientContext *ctx, const char *to_uri, const char *sdp) {
    if (!ctx || !to_uri) return -1;
    
    osip_message_t *invite = NULL;
    char from[512];
    
    if (ctx->config.from_uri[0]) {
        snprintf(from, sizeof(from), "%s", ctx->config.from_uri);
    } else {
        snprintf(from, sizeof(from), "sip:%s@%s:%d",
                 ctx->config.username, ctx->config.server_ip, ctx->config.server_port);
    }
    
    eXosip_lock(ctx->ctx);
    
    int ret = eXosip_call_build_initial_invite(ctx->ctx, &invite, to_uri, from, NULL, NULL);
    if (ret < 0 || !invite) {
        eXosip_unlock(ctx->ctx);
        return -1;
    }
    
    // 添加 SDP
    if (sdp) {
        osip_message_set_content_type(invite, "application/sdp");
        osip_message_set_body(invite, sdp, strlen(sdp));
    }
    
    int cid = eXosip_call_send_initial_invite(ctx->ctx, invite);
    eXosip_unlock(ctx->ctx);
    
    if (cid > 0) {
        pthread_mutex_lock(&ctx->lock);
        ctx->request_count++;
        pthread_mutex_unlock(&ctx->lock);
        
        if (ctx->config.debug) {
            printf("[CLIENT] INVITE sent to %s (cid=%d)\n", to_uri, cid);
        }
    }
    
    return cid;
}

int client_send_bye(ClientContext *ctx, int did, int cid) {
    if (!ctx || did <= 0 || cid <= 0) return -1;
    
    eXosip_lock(ctx->ctx);
    int ret = eXosip_call_terminate(ctx->ctx, cid, did);
    eXosip_unlock(ctx->ctx);
    
    if (ret == 0) {
        pthread_mutex_lock(&ctx->lock);
        ctx->request_count++;
        pthread_mutex_unlock(&ctx->lock);
        
        if (ctx->config.debug) {
            printf("[CLIENT] BYE sent (did=%d, cid=%d)\n", did, cid);
        }
    }
    
    return ret;
}

int client_send_ack(ClientContext *ctx, int did, int cid) {
    if (!ctx || did <= 0 || cid <= 0) return -1;
    
    eXosip_lock(ctx->ctx);
    int ret = eXosip_call_send_ack(ctx->ctx, did, NULL);
    eXosip_unlock(ctx->ctx);
    
    if (ret == 0) {
        pthread_mutex_lock(&ctx->lock);
        ctx->request_count++;
        pthread_mutex_unlock(&ctx->lock);
        
        if (ctx->config.debug) {
            printf("[CLIENT] ACK sent (did=%d)\n", did);
        }
    }
    
    return ret;
}

int client_send_options(ClientContext *ctx, const char *to_uri) {
    if (!ctx || !to_uri) return -1;
    
    osip_message_t *options = NULL;
    char from[512];
    
    if (ctx->config.from_uri[0]) {
        snprintf(from, sizeof(from), "%s", ctx->config.from_uri);
    } else {
        snprintf(from, sizeof(from), "sip:%s@%s:%d",
                 ctx->config.username, ctx->config.server_ip, ctx->config.server_port);
    }
    
    eXosip_lock(ctx->ctx);
    
    int ret = eXosip_options_build_request(ctx->ctx, &options, to_uri, from, NULL);
    if (ret != 0 || !options) {
        eXosip_unlock(ctx->ctx);
        return -1;
    }
    
    ret = eXosip_options_send_request(ctx->ctx, options);
    eXosip_unlock(ctx->ctx);
    
    if (ret >= 0) {
        pthread_mutex_lock(&ctx->lock);
        ctx->request_count++;
        pthread_mutex_unlock(&ctx->lock);
        
        if (ctx->config.debug) {
            printf("[CLIENT] OPTIONS sent to %s\n", to_uri);
        }
    }
    
    return ret;
}

int client_send_info(ClientContext *ctx, int did, int cid, const char *body) {
    if (!ctx || did <= 0 || cid <= 0) return -1;
    
    osip_message_t *info = NULL;
    
    eXosip_lock(ctx->ctx);
    
    int ret = eXosip_call_build_info(ctx->ctx, did, &info);
    if (ret != 0 || !info) {
        eXosip_unlock(ctx->ctx);
        return -1;
    }
    
    if (body) {
        osip_message_set_content_type(info, "application/MANSCDP+xml");
        osip_message_set_body(info, body, strlen(body));
    }
    
    ret = eXosip_call_send_request(ctx->ctx, did, info);
    eXosip_unlock(ctx->ctx);
    
    if (ret == 0) {
        pthread_mutex_lock(&ctx->lock);
        ctx->request_count++;
        pthread_mutex_unlock(&ctx->lock);
        
        if (ctx->config.debug) {
            printf("[CLIENT] INFO sent (did=%d)\n", did);
        }
    }
    
    return ret;
}

int client_send_answer(ClientContext *ctx, int tid, int status) {
    if (!ctx || tid <= 0) return -1;
    
    eXosip_lock(ctx->ctx);
    int ret = eXosip_call_send_answer(ctx->ctx, tid, status, NULL);
    eXosip_unlock(ctx->ctx);
    
    if (ret == 0 && ctx->config.debug) {
        printf("[CLIENT] Answer %d sent (tid=%d)\n", status, tid);
    }
    
    return ret;
}

int client_is_registered(ClientContext *ctx) {
    if (!ctx) return 0;
    
    pthread_mutex_lock(&ctx->lock);
    int registered = ctx->registered;
    pthread_mutex_unlock(&ctx->lock);
    
    return registered;
}

int client_get_stats(ClientContext *ctx, zval *arr) {
    if (!ctx || !arr) return -1;
    
    pthread_mutex_lock(&ctx->lock);
    
    add_assoc_long(arr, "registered", ctx->registered);
    add_assoc_long(arr, "request_count", ctx->request_count);
    add_assoc_long(arr, "response_count", ctx->response_count);
    add_assoc_long(arr, "timeout_count", ctx->timeout_count);
    add_assoc_long(arr, "register_time", ctx->register_time);
    
    pthread_mutex_unlock(&ctx->lock);
    
    return 0;
}

int client_process_events(ClientContext *ctx, int timeout_ms, zval *events_array) {
    if (!ctx || !events_array) return -1;
    
    array_init(events_array);
    
    int tv_sec = timeout_ms / 1000;
    int tv_ms = timeout_ms % 1000;
    int count = 0;
    
    eXosip_lock(ctx->ctx);
    eXosip_automatic_action(ctx->ctx);
    eXosip_unlock(ctx->ctx);
    
    eXosip_event_t *evt;
    while ((evt = eXosip_event_wait(ctx->ctx, tv_sec, tv_ms)) != NULL) {
        zval event_item;
        array_init(&event_item);
        
        add_assoc_long(&event_item, "type", evt->type);
        add_assoc_long(&event_item, "tid", evt->tid);
        add_assoc_long(&event_item, "did", evt->did);
        add_assoc_long(&event_item, "cid", evt->cid);
        add_assoc_long(&event_item, "rid", evt->rid);
        
        if (evt->response) {
            add_assoc_long(&event_item, "status_code", evt->response->status_code);
            if (evt->response->reason_phrase) {
                add_assoc_string(&event_item, "reason", evt->response->reason_phrase);
            }
        }
        
        if (evt->request && evt->request->sip_method) {
            add_assoc_string(&event_item, "method", evt->request->sip_method);
        }
        
        add_next_index_zval(events_array, &event_item);
        eXosip_event_free(evt);
        count++;
        
        tv_sec = 0;
        tv_ms = 0;
    }
    
    return count;
}

// ==================== 定时器功能实现 ====================

/**
 * 设置定时器回调和间隔
 * @param ctx SIP上下文
 * @param callback PHP回调函数
 * @param interval_ms 定时器间隔（毫秒）
 */
void sip_set_timer_callback(SipContext *ctx, zval *callback, int interval_ms) {
    if (!ctx) return;
    
    pthread_mutex_lock(&ctx->lock);
    
    // 如果已有回调，先释放
    if (!Z_ISUNDEF(ctx->timer_callback)) {
        zval_ptr_dtor(&ctx->timer_callback);
        ZVAL_UNDEF(&ctx->timer_callback);
    }
    
    // 设置新回调
    if (callback && Z_TYPE_P(callback) != IS_NULL) {
        ZVAL_COPY(&ctx->timer_callback, callback);
        ctx->timer_interval_ms = interval_ms > 0 ? interval_ms : 1000;  // 最小1秒
        
        // 初始化计时器起点
        struct timeval tv;
        gettimeofday(&tv, NULL);
        ctx->last_timer_tick = tv.tv_sec;
        ctx->last_timer_tick_us = tv.tv_usec;
    } else {
        // 清除定时器
        ZVAL_UNDEF(&ctx->timer_callback);
        ctx->timer_interval_ms = 0;
    }
    
    pthread_mutex_unlock(&ctx->lock);
}

/**
 * 检查并触发定时器（在事件循环中调用）
 * @param ctx SIP上下文
 * @return 1=触发了定时器, 0=未触发
 */
int sip_check_and_fire_timer(SipContext *ctx) {
    if (!ctx || ctx->timer_interval_ms <= 0) return 0;
    if (Z_ISUNDEF(ctx->timer_callback)) return 0;
    
    // 获取当前时间
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    // 计算距离上次触发的毫秒数
    long elapsed_ms = (tv.tv_sec - ctx->last_timer_tick) * 1000 + 
                     (tv.tv_usec - ctx->last_timer_tick_us) / 1000;
    
    if (elapsed_ms >= ctx->timer_interval_ms) {
        // 更新上次触发时间
        ctx->last_timer_tick = tv.tv_sec;
        ctx->last_timer_tick_us = tv.tv_usec;
        
        // 返回1表示需要触发（调用者负责执行PHP回调）
        return 1;
    }
    
    return 0;
}

// ==================== Master-Worker-Task 进程管理 ====================

static volatile sig_atomic_t g_shutdown_flag = 0;
static volatile sig_atomic_t g_worker_died = 0;
static SipContext *g_master_ctx = NULL;

static void sigchld_handler(int signo) {
    int status;
    pid_t pid;
    (void)signo;
    
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (g_master_ctx && g_master_ctx->is_master) {
            if (pid == g_master_ctx->worker_pid) {
                fprintf(stderr, "[Master] Worker %d exited\n", pid);
                g_worker_died = 1;
            }
        }
    }
}

static void sigterm_handler(int signo) {
    (void)signo;
    g_shutdown_flag = 1;
}

int sip_start_master_process(SipContext *ctx) {
    if (!ctx) return -1;
    
    ctx->master_pid = getpid();
    ctx->is_master = 1;
    ctx->is_worker = 0;
    ctx->is_task = 0;
    ctx->worker_restart_count = 0;
    
    g_master_ctx = ctx;
    
    signal(SIGCHLD, sigchld_handler);
    signal(SIGTERM, sigterm_handler);
    signal(SIGINT, sigterm_handler);
    signal(SIGPIPE, SIG_IGN);
    
    fprintf(stderr, "[Master] Started PID=%d\n", ctx->master_pid);
    
    // Fork Worker 和 Task
    if (sip_fork_worker(ctx) < 0) {
        return -1;
    }
    
    if (sip_fork_task_workers(ctx) < 0) {
        return -1;
    }
    
    // ✅ 只保存 Master PID（标准做法）
    if (strlen(ctx->pid_file) > 0) {
        FILE *fp = fopen(ctx->pid_file, "w");
        if (fp) {
            fprintf(fp, "%d\n", ctx->master_pid);
            fclose(fp);
            fprintf(stderr, "[Master] PID file saved: %s (PID=%d)\n", ctx->pid_file, ctx->master_pid);
        } else {
            fprintf(stderr, "[Master] Failed to write PID file: %s\n", ctx->pid_file);
        }
    }
    
    return 0;
}

int sip_fork_worker(SipContext *ctx) {
    pid_t pid = fork();
    
    if (pid < 0) {
        perror("[Master] fork worker failed");
        return -1;
    }
    
    if (pid == 0) {
        // Worker child process
        ctx->is_master = 0;
        ctx->is_worker = 1;
        ctx->is_task = 0;
        ctx->worker_start_time = time(NULL);
        
        fprintf(stderr, "[Worker] Started PID=%d\n", getpid());
        
        // Worker will use PHP event loop in run(), so just return
        // Do NOT call _exit() here
        return 0;
    }
    
    // Master process
    ctx->worker_pid = pid;
    return 0;
}

int sip_fork_task_workers(SipContext *ctx) {
    if (ctx->task_count <= 0) ctx->task_count = 4;
    
    ctx->task_pids = (pid_t*)calloc(ctx->task_count, sizeof(pid_t));
    ctx->task_sockfds = (int*)calloc(ctx->task_count, sizeof(int));
    
    if (!ctx->task_pids || !ctx->task_sockfds) {
        return -1;
    }
    
    for (int i = 0; i < ctx->task_count; i++) {
        int sv[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1) {
            perror("[Master] socketpair failed");
            return -1;
        }
        
        pid_t pid = fork();
        
        if (pid < 0) {
            perror("[Master] fork task failed");
            close(sv[0]);
            close(sv[1]);
            return -1;
        }
        
        if (pid == 0) {
            close(sv[0]);
            ctx->is_master = 0;
            ctx->is_worker = 0;
            ctx->is_task = 1;
            ctx->task_worker_id = i;
            
            fprintf(stderr, "[Task-%d] Started PID=%d\n", i, getpid());
            sip_task_loop(ctx, sv[1]);
            close(sv[1]);
            _exit(0);
        }
        
        close(sv[1]);
        ctx->task_pids[i] = pid;
        ctx->task_sockfds[i] = sv[0];
        
        int flags = fcntl(sv[0], F_GETFL, 0);
        fcntl(sv[0], F_SETFL, flags | O_NONBLOCK);
    }
    
    return 0;
}

void sip_master_loop(SipContext *ctx) {
    fprintf(stderr, "[Master] Entering monitor loop\n");
    
    while (!g_shutdown_flag) {
        if (g_worker_died) {
            fprintf(stderr, "[Master] Restarting worker\n");
            ctx->worker_restart_count++;
            
            if (sip_fork_worker(ctx) < 0) {
                fprintf(stderr, "[Master] Failed to restart worker\n");
                break;
            }
            
            g_worker_died = 0;
        }
        
        sleep(1);
    }
    
    fprintf(stderr, "[Master] Shutting down\n");
    
    if (ctx->worker_pid > 0) {
        kill(ctx->worker_pid, SIGTERM);
    }
    
    for (int i = 0; i < ctx->task_count; i++) {
        if (ctx->task_pids[i] > 0) {
            kill(ctx->task_pids[i], SIGTERM);
        }
    }
    
    sleep(2);
    
    if (ctx->worker_pid > 0) kill(ctx->worker_pid, SIGKILL);
    for (int i = 0; i < ctx->task_count; i++) {
        if (ctx->task_pids[i] > 0) kill(ctx->task_pids[i], SIGKILL);
    }
    
    // ✅ 删除 PID 文件
    if (strlen(ctx->pid_file) > 0) {
        if (unlink(ctx->pid_file) == 0) {
            fprintf(stderr, "[Master] PID file deleted: %s\n", ctx->pid_file);
        }
    }
    
    fprintf(stderr, "[Master] Shutdown complete\n");
}

void sip_worker_loop(SipContext *ctx) {
    signal(SIGTERM, sigterm_handler);
    signal(SIGINT, sigterm_handler);
    
    ctx->running = 1;
    
    while (!g_shutdown_flag && ctx->running) {
        eXosip_event_t *evt = eXosip_event_wait(ctx->ctx, 0, 100);
        
        if (evt) {
            handle_sip_event(ctx, evt);
            eXosip_event_free(evt);
        }
        
        if (ctx->timer_interval_ms > 0) {
            sip_check_and_fire_timer(ctx);
        }
        
        for (int i = 0; i < ctx->task_count; i++) {
            sip_handle_task_result(ctx, ctx->task_sockfds[i]);
        }
    }
    
    fprintf(stderr, "[Worker] Exiting\n");
}

void sip_task_loop(SipContext *ctx, int sockfd) {
    signal(SIGTERM, sigterm_handler);
    
    while (!g_shutdown_flag) {
        task_msg_t msg_hdr;
        ssize_t n = read(sockfd, &msg_hdr, sizeof(msg_hdr));
        
        if (n == sizeof(msg_hdr)) {
            char *data = NULL;
            if (msg_hdr.data_len > 0) {
                data = (char*)malloc(msg_hdr.data_len);
                if (data && read(sockfd, data, msg_hdr.data_len) == (ssize_t)msg_hdr.data_len) {
                    // 反序列化 PHP array
                    zval task_data;
                    php_unserialize_data_t var_hash;
                    PHP_VAR_UNSERIALIZE_INIT(var_hash);
                    
                    const unsigned char *p = (const unsigned char *)data;
                    if (php_var_unserialize(&task_data, &p, p + msg_hdr.data_len, &var_hash)) {
                        // 调用 PHP onTask 回调
                        if (Z_TYPE(ctx->task_callback) == IS_OBJECT) {
                            zval retval;
                            zval args[2];
                            ZVAL_LONG(&args[0], msg_hdr.task_id);
                            ZVAL_COPY(&args[1], &task_data);
                            
                            if (call_user_function(NULL, &ctx->task_callback, &ctx->task_callback, &retval, 2, args) == SUCCESS) {
                                // 序列化返回结果
                                smart_str buf = {0};
                                php_serialize_data_t serialize_data;
                                PHP_VAR_SERIALIZE_INIT(serialize_data);
                                php_var_serialize(&buf, &retval, &serialize_data);
                                smart_str_0(&buf);
                                PHP_VAR_SERIALIZE_DESTROY(serialize_data);
                                
                                // 发送结果回 Worker
                                task_result_t result_hdr;
                                result_hdr.task_id = msg_hdr.task_id;
                                result_hdr.success = 1;
                                result_hdr.result_len = ZSTR_LEN(buf.s);
                                
                                write(sockfd, &result_hdr, sizeof(result_hdr));
                                if (result_hdr.result_len > 0) {
                                    write(sockfd, ZSTR_VAL(buf.s), result_hdr.result_len);
                                }
                                
                                smart_str_free(&buf);
                                zval_ptr_dtor(&retval);
                            }
                            
                            zval_ptr_dtor(&args[0]);
                            zval_ptr_dtor(&args[1]);
                        }
                        
                        zval_ptr_dtor(&task_data);
                    }
                    
                    PHP_VAR_UNSERIALIZE_DESTROY(var_hash);
                    free(data);
                } else {
                    free(data);
                }
            }
        } else if (n == 0) {
            break;
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            break;
        }
        
        usleep(10000);
    }
    
    fprintf(stderr, "[Task-%d] Exiting\n", ctx->task_worker_id);
}

unsigned long sip_add_task(SipContext *ctx, const char *serialized_data, size_t data_len) {
    if (!ctx || !ctx->is_worker || !serialized_data) {
        return 0;
    }
    
    unsigned long task_id = ++ctx->tasks_posted;
    
    int target_fd = ctx->task_sockfds[task_id % ctx->task_count];
    
    task_msg_t msg_hdr;
    msg_hdr.task_id = task_id;
    msg_hdr.data_len = data_len;
    
    pthread_mutex_lock(&ctx->lock);
    
    ssize_t n = write(target_fd, &msg_hdr, sizeof(msg_hdr));
    if (n == sizeof(msg_hdr) && data_len > 0) {
        n = write(target_fd, serialized_data, data_len);
    }
    
    pthread_mutex_unlock(&ctx->lock);
    
    if (n < 0) {
        ctx->tasks_failed++;
        return 0;
    }
    
    return task_id;
}

void sip_handle_task_result(SipContext *ctx, int sockfd) {
    task_result_t result_hdr;
    ssize_t n = read(sockfd, &result_hdr, sizeof(result_hdr));
    
    if (n == sizeof(result_hdr)) {
        char *result_data = NULL;
        if (result_hdr.result_len > 0) {
            result_data = (char*)malloc(result_hdr.result_len);
            if (result_data && read(sockfd, result_data, result_hdr.result_len) == (ssize_t)result_hdr.result_len) {
                // 反序列化 PHP 返回值
                zval result_zval;
                php_unserialize_data_t var_hash;
                PHP_VAR_UNSERIALIZE_INIT(var_hash);
                
                const unsigned char *p = (const unsigned char *)result_data;
                if (php_var_unserialize(&result_zval, &p, p + result_hdr.result_len, &var_hash)) {
                    // 调用 PHP onTaskFinish 回调
                    if (Z_TYPE(ctx->task_finish_callback) == IS_OBJECT) {
                        zval retval;
                        zval args[2];
                        ZVAL_LONG(&args[0], result_hdr.task_id);
                        ZVAL_COPY(&args[1], &result_zval);
                        
                        call_user_function(NULL, &ctx->task_finish_callback, &ctx->task_finish_callback, &retval, 2, args);
                        
                        zval_ptr_dtor(&retval);
                        zval_ptr_dtor(&args[0]);
                        zval_ptr_dtor(&args[1]);
                    }
                    
                    zval_ptr_dtor(&result_zval);
                }
                
                PHP_VAR_UNSERIALIZE_DESTROY(var_hash);
                free(result_data);
            } else {
                free(result_data);
            }
        }
    }
}

// ==================== 进程状态查询 ====================

/**
 * 获取进程状态信息
 * @param ctx SIP上下文
 * @param status_array PHP数组，用于存储进程状态
 * 
 * 返回格式：
 * [
 *   'master' => ['pid' => 12345, 'status' => 'running'],
 *   'worker' => ['pid' => 12346, 'status' => 'running', 'uptime' => 120, 'restart_count' => 0],
 *   'tasks' => [
 *     ['id' => 0, 'pid' => 12347, 'status' => 'running'],
 *     ['id' => 1, 'pid' => 12348, 'status' => 'running'],
 *     ...
 *   ]
 * ]
 */
void sip_get_process_status(SipContext *ctx, zval *status_array) {
    if (!ctx || !status_array) {
        return;
    }
    
    array_init(status_array);
    
    // Master 进程信息
    if (ctx->master_pid > 0) {
        zval master_info;
        array_init(&master_info);
        add_assoc_long(&master_info, "pid", ctx->master_pid);
        
        // 检查进程是否存在
        int master_alive = (kill(ctx->master_pid, 0) == 0);
        add_assoc_string(&master_info, "status", master_alive ? "running" : "dead");
        
        if (ctx->is_master) {
            add_assoc_bool(&master_info, "current", 1);
        }
        
        add_assoc_zval(status_array, "master", &master_info);
    }
    
    // Worker 进程信息
    if (ctx->worker_pid > 0) {
        zval worker_info;
        array_init(&worker_info);
        add_assoc_long(&worker_info, "pid", ctx->worker_pid);
        
        // 检查进程是否存在
        int worker_alive = (kill(ctx->worker_pid, 0) == 0);
        add_assoc_string(&worker_info, "status", worker_alive ? "running" : "dead");
        
        // 运行时长
        if (ctx->worker_start_time > 0) {
            time_t uptime = time(NULL) - ctx->worker_start_time;
            add_assoc_long(&worker_info, "uptime", uptime);
        }
        
        // 重启次数
        add_assoc_long(&worker_info, "restart_count", ctx->worker_restart_count);
        
        if (ctx->is_worker) {
            add_assoc_bool(&worker_info, "current", 1);
        }
        
        add_assoc_zval(status_array, "worker", &worker_info);
    }
    
    // Task 进程池信息
    if (ctx->task_count > 0 && ctx->task_pids) {
        zval tasks_array;
        array_init(&tasks_array);
        
        for (int i = 0; i < ctx->task_count; i++) {
            zval task_info;
            array_init(&task_info);
            
            add_assoc_long(&task_info, "id", i);
            add_assoc_long(&task_info, "pid", ctx->task_pids[i]);
            
            // 检查进程是否存在
            int task_alive = (kill(ctx->task_pids[i], 0) == 0);
            add_assoc_string(&task_info, "status", task_alive ? "running" : "dead");
            
            if (ctx->is_task && ctx->task_worker_id == i) {
                add_assoc_bool(&task_info, "current", 1);
            }
            
            add_next_index_zval(&tasks_array, &task_info);
        }
        
        add_assoc_zval(status_array, "tasks", &tasks_array);
    }
    
    // 进程类型标识
    if (ctx->is_master) {
        add_assoc_string(status_array, "current_process", "master");
    } else if (ctx->is_worker) {
        add_assoc_string(status_array, "current_process", "worker");
    } else if (ctx->is_task) {
        add_assoc_string(status_array, "current_process", "task");
        add_assoc_long(status_array, "current_task_id", ctx->task_worker_id);
    } else {
        add_assoc_string(status_array, "current_process", "single");
    }
    
    // 任务统计
    if (ctx->is_worker) {
        add_assoc_long(status_array, "tasks_posted", ctx->tasks_posted);
        add_assoc_long(status_array, "tasks_failed", ctx->tasks_failed);
    }
}

/**
 * 从 PID 文件读取进程状态（外部脚本调用）
 * @param pid_file PID 文件路径
 * @param status_array PHP 数组，用于存储进程状态
 * @return 0=成功, -1=失败
 * 
 * PID 文件格式：
 * Line 1: master_pid
 * Line 2: worker_pid
 * Line 3: task_count
 * Line 4+: task_pids[0..N-1]
 */
int sip_read_process_status_from_pid(const char *pid_file, zval *status_array) {
    if (!pid_file || !status_array) {
        return -1;
    }
    
    FILE *fp = fopen(pid_file, "r");
    if (!fp) {
        return -1;
    }
    
    array_init(status_array);
    
    pid_t master_pid = 0;
    char cmd[512];  // 声明 cmd 缓冲区
    
    // 只读取 Master PID
    if (fscanf(fp, "%d", &master_pid) != 1) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    
    // 检查 Master 进程是否存在
    if (kill(master_pid, 0) != 0) {
        add_assoc_long(status_array, "master_pid", master_pid);
        add_assoc_string(status_array, "status", "stopped");
        add_assoc_string(status_array, "message", "Master process not running");
        return 0;
    }
    
    // Master 进程信息
    zval master_info;
    array_init(&master_info);
    add_assoc_long(&master_info, "pid", master_pid);
    add_assoc_string(&master_info, "status", "running");
    
    // 获取 Master 进程内存和 FD 信息
#ifdef __APPLE__
    snprintf(cmd, sizeof(cmd), "ps -o rss=,vsz= -p %d", master_pid);
    FILE *ps_mem = popen(cmd, "r");
    if (ps_mem) {
        long rss_kb = 0, vsz_kb = 0;
        if (fscanf(ps_mem, "%ld %ld", &rss_kb, &vsz_kb) == 2) {
            add_assoc_long(&master_info, "memory_rss_kb", rss_kb);  // 物理内存
            add_assoc_long(&master_info, "memory_vsz_kb", vsz_kb);  // 虚拟内存
        }
        pclose(ps_mem);
    }
    
    // 获取文件描述符数量
    snprintf(cmd, sizeof(cmd), "lsof -p %d 2>/dev/null | wc -l", master_pid);
    FILE *lsof_cmd = popen(cmd, "r");
    if (lsof_cmd) {
        int fd_count = 0;
        if (fscanf(lsof_cmd, "%d", &fd_count) == 1) {
            // lsof 输出包含标题行，减 1
            add_assoc_long(&master_info, "fd_count", fd_count > 0 ? fd_count - 1 : 0);
        }
        pclose(lsof_cmd);
    }
#else
    // Linux: 从 /proc 读取
    snprintf(cmd, sizeof(cmd), "/proc/%d/status", master_pid);
    FILE *status_file = fopen(cmd, "r");
    if (status_file) {
        char line[256];
        while (fgets(line, sizeof(line), status_file)) {
            if (strncmp(line, "VmRSS:", 6) == 0) {
                long rss_kb;
                if (sscanf(line + 6, "%ld", &rss_kb) == 1) {
                    add_assoc_long(&master_info, "memory_rss_kb", rss_kb);
                }
            } else if (strncmp(line, "VmSize:", 7) == 0) {
                long vsz_kb;
                if (sscanf(line + 7, "%ld", &vsz_kb) == 1) {
                    add_assoc_long(&master_info, "memory_vsz_kb", vsz_kb);
                }
            }
        }
        fclose(status_file);
    }
    
    // 统计 FD 数量
    snprintf(cmd, sizeof(cmd), "/proc/%d/fd", master_pid);
    DIR *fd_dir = opendir(cmd);
    if (fd_dir) {
        int fd_count = 0;
        struct dirent *entry;
        while ((entry = readdir(fd_dir)) != NULL) {
            if (entry->d_name[0] != '.') {
                fd_count++;
            }
        }
        closedir(fd_dir);
        add_assoc_long(&master_info, "fd_count", fd_count);
    }
#endif
    
    add_assoc_zval(status_array, "master", &master_info);
    
    // 查找子进程（通过 ps 命令）
    
#ifdef __APPLE__
    // macOS: ps -o pid,ppid,command -p <master_pid> 然后查找子进程
    snprintf(cmd, sizeof(cmd), "ps -o pid=,ppid= -A | awk '$2 == %d {print $1}'", master_pid);
#else
    // Linux: ps --ppid <master_pid>
    snprintf(cmd, sizeof(cmd), "ps --ppid %d -o pid=", master_pid);
#endif
    
    FILE *ps = popen(cmd, "r");
    if (ps) {
        pid_t child_pids[32];
        int child_count = 0;
        
        while (child_count < 32 && fscanf(ps, "%d", &child_pids[child_count]) == 1) {
            child_count++;
        }
        pclose(ps);
        
        // 第一个子进程通常是 Worker
        if (child_count > 0) {
            zval worker_info;
            array_init(&worker_info);
            add_assoc_long(&worker_info, "pid", child_pids[0]);
            add_assoc_string(&worker_info, "status", "running");
            
            // 获取 Worker 进程内存和 FD
#ifdef __APPLE__
            snprintf(cmd, sizeof(cmd), "ps -o rss=,vsz= -p %d", child_pids[0]);
            FILE *ps_worker = popen(cmd, "r");
            if (ps_worker) {
                long rss_kb = 0, vsz_kb = 0;
                if (fscanf(ps_worker, "%ld %ld", &rss_kb, &vsz_kb) == 2) {
                    add_assoc_long(&worker_info, "memory_rss_kb", rss_kb);
                    add_assoc_long(&worker_info, "memory_vsz_kb", vsz_kb);
                }
                pclose(ps_worker);
            }
            
            snprintf(cmd, sizeof(cmd), "lsof -p %d 2>/dev/null | wc -l", child_pids[0]);
            FILE *lsof_worker = popen(cmd, "r");
            if (lsof_worker) {
                int fd_count = 0;
                if (fscanf(lsof_worker, "%d", &fd_count) == 1) {
                    add_assoc_long(&worker_info, "fd_count", fd_count > 0 ? fd_count - 1 : 0);
                }
                pclose(lsof_worker);
            }
#endif
            
            add_assoc_zval(status_array, "worker", &worker_info);
        }
        
        // 其余子进程是 Task
        if (child_count > 1) {
            zval tasks_array;
            array_init(&tasks_array);
            
            for (int i = 1; i < child_count; i++) {
                zval task_info;
                array_init(&task_info);
                add_assoc_long(&task_info, "id", i - 1);
                add_assoc_long(&task_info, "pid", child_pids[i]);
                add_assoc_string(&task_info, "status", "running");
                
                // 获取 Task 进程内存
#ifdef __APPLE__
                snprintf(cmd, sizeof(cmd), "ps -o rss= -p %d", child_pids[i]);
                FILE *ps_task = popen(cmd, "r");
                if (ps_task) {
                    long rss_kb = 0;
                    if (fscanf(ps_task, "%ld", &rss_kb) == 1) {
                        add_assoc_long(&task_info, "memory_rss_kb", rss_kb);
                    }
                    pclose(ps_task);
                }
#endif
                
                add_next_index_zval(&tasks_array, &task_info);
            }
            
            add_assoc_zval(status_array, "tasks", &tasks_array);
        }
        
        add_assoc_long(status_array, "total_processes", child_count + 1);
    }
    
    add_assoc_string(status_array, "pid_file", (char*)pid_file);
    
    return 0;
}



