#ifndef EXOSIP_WRAPPER_H
#define EXOSIP_WRAPPER_H

#include "ServerInfo.h"
#include "Client.h"
#include <osip2/osip_mt.h>
#include <eXosip2/eXosip.h>
#include <osipparser2/osip_port.h>
#include <osipparser2/osip_message.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "php.h"

#ifdef __cplusplus
extern "C" {
#endif

// 内存管理函数指针
extern osip_malloc_func_t *osip_malloc_func;
extern osip_realloc_func_t *osip_realloc_func;
extern osip_free_func_t *osip_free_func;

// 最大限制
#define MAX_CONNECTIONS 10000      // 支持1万个设备连接
#define MAX_SESSIONS 1000          // 支持1000个并发会话
#define MAX_RAW_DATA_SIZE 8192     // 单个消息最大8KB
#define MAX_CATALOG_ITEMS 1000     // 设备目录最大1000项

// 设备目录项结构
typedef struct _catalog_item {
    char device_id[64];
    char name[256];
    char manufacturer[128];
    char model[128];
    char owner[128];
    char civil_code[32];
    char address[256];
    char parental[8];
    char parent_id[64];
    char safety_way[8];
    char register_way[8];
    char secrecy[8];
    char status[16];
} CatalogItem;

// 设备信息结构
typedef struct _device_info {
    char device_name[256];
    char manufacturer[128];
    char model[128];
    char firmware[64];
    int channel;
} DeviceInfo;

// SIP事件类型增强
typedef enum {
    SIP_EVENT_REGISTER = 1,
    SIP_EVENT_MESSAGE,
    SIP_EVENT_INVITE,
    SIP_EVENT_BYE,
    SIP_EVENT_ACK,
    SIP_EVENT_RESPONSE,
    SIP_EVENT_TIMEOUT,
    SIP_EVENT_NETWORK_ERROR
} SipEventType;

// 连接状态
typedef enum {
    CONN_STATE_IDLE = 0,
    CONN_STATE_REGISTERING,
    CONN_STATE_REGISTERED,
    CONN_STATE_CALLING,
    CONN_STATE_INCALL,
    CONN_STATE_DISCONNECTED,
    CONN_STATE_ERROR
} ConnectionState;

// 会话类型
typedef enum {
    SESSION_TYPE_UNKNOWN = 0,
    SESSION_TYPE_REGISTER,
    SESSION_TYPE_VIDEO_PREVIEW,
    SESSION_TYPE_VIDEO_PLAYBACK,
    SESSION_TYPE_AUDIO_TALK,
    SESSION_TYPE_PTZ_CONTROL,
    SESSION_TYPE_MESSAGE
} SessionType;

// GB28181命令类型
typedef enum {
    GB_CMD_UNKNOWN = 0,
    GB_CMD_CATALOG,
    GB_CMD_DEVICEINFO,
    GB_CMD_DEVICESTATUS,
    GB_CMD_KEEPALIVE,
    GB_CMD_PTZ_CONTROL,
    GB_CMD_RECORD_INFO,
    GB_CMD_ALARM,
    GB_CMD_CONFIG_DOWNLOAD,
    GB_CMD_PRESET_QUERY
} GB28181CmdType;

// 连接信息结构
typedef struct _connection_info {
    int id;                    // 连接ID
    char device_id[64];        // 设备ID
    char ip[64];              // IP地址
    int port;                 // 端口
    ConnectionState state;     // 连接状态
    time_t last_seen;         // 最后活跃时间
    time_t created_at;        // 创建时间
    int register_count;       // 注册次数
    int message_count;        // 消息计数
    char user_agent[256];     // User-Agent
    char contact_uri[256];    // Contact URI
    void *user_data;          // 用户自定义数据
} ConnectionInfo;

// 会话信息结构
typedef struct _session_info {
    int id;                   // 会话ID
    int connection_id;        // 关联的连接ID
    SessionType type;         // 会话类型
    int call_id;             // SIP Call-ID
    int dialog_id;           // SIP Dialog-ID
    char from_uri[256];      // From URI
    char to_uri[256];        // To URI
    char sdp_local[2048];    // 本地SDP
    char sdp_remote[2048];   // 远端SDP
    char raw_body[4096];     // 完整的原始XML/SIP消息体
    time_t created_at;       // 创建时间
    time_t updated_at;       // 更新时间
    void *user_data;         // 用户自定义数据
} SessionInfo;

// GB28181消息结构
typedef struct _gb28181_message {
    GB28181CmdType cmd_type_enum;    // 命令类型枚举
    char cmd_type[64];               // 命令类型字符串
    char device_id[64];
    char sn[64];
    int sum_num;                     // 目录数量
    char raw_xml[4096];              // 原始XML
    
    // 设备目录项（用于Catalog消息）
    CatalogItem catalog_items[MAX_CATALOG_ITEMS];
    
    // 设备信息（用于DeviceInfo消息）
    DeviceInfo device_info;
    
    // 解析后的数据
    union {
        struct {
            char manufacturer[128];
            char model[128];
            char firmware[64];
        } device_info_data;
        
        struct {
            int online;
            char status[32];
            char encode[32];
            char record[32];
        } device_status;
        
        struct {
            int ptz_cmd;
            int speed;
            int zoom;
        } ptz_control;
    } data;
} GB28181Message;

// 原始数据结构
typedef struct _raw_data {
    char *data;              // 原始数据
    size_t length;           // 数据长度
    char content_type[64];   // 内容类型
    time_t timestamp;        // 时间戳
} RawData;

// 统计信息结构
typedef struct _sip_statistics {
    int total_connections;
    int active_connections;
    int active_sessions;
    int total_messages;
    int register_success;
    int register_failed;
    time_t start_time;
} SipStatistics;

// TaskQueue 已移除：改用同步加锁发送方式
// 原因：单线程事件循环 + eXosip_lock/unlock 足够高效
// 性能：即使1000设备并发，每次加锁仅1ms，总延迟<1秒

// SIP上下文结构（完整版）
typedef struct _sip_context {
    struct eXosip_t *ctx;
    pthread_t event_thread;
    pthread_mutex_t lock;
    
    // 服务器配置
    ServerInfo server_info;
    int running;
    
    // 连接管理
    ConnectionInfo connections[MAX_CONNECTIONS];
    int connection_count;
    
    // 会话管理
    SessionInfo sessions[MAX_SESSIONS];
    int session_count;
    
    // 统计信息
    SipStatistics stats;
    
    // PHP回调
    zval event_callback;
    zval connection_callback;
    zval message_callback;
    zval error_callback;
    int callbacks_valid;
    
    // 定时器支持（单线程事件循环）
    zval timer_callback;        // onTimer 回调
    int timer_interval_ms;      // 定时器间隔（毫秒）
    time_t last_timer_tick;     // 上次触发时间（秒）
    long last_timer_tick_us;    // 上次触发时间（微秒）
    
    // 原始数据缓存（可选）
    int save_raw_data;
    RawData *raw_data_buffer;
    int raw_data_size;
    
} SipContext;

// 客户端配置信息
typedef struct _client_config {
    char local_ip[64];          // 本地IP（可选，NULL则自动）
    int local_port;             // 本地端口（0则自动分配）
    char server_ip[64];         // 服务器IP
    int server_port;            // 服务器端口
    char mode[8];               // 传输模式：UDP/TCP
    char user_agent[128];       // User-Agent
    char from_uri[256];         // From URI
    char to_uri[256];           // To URI
    char username[64];          // 用户名
    char password[64];          // 密码
    char realm[64];             // 认证域
    int expires;                // 注册过期时间（秒）
    int debug;                  // 调试模式
} ClientConfig;

// 客户端上下文
typedef struct _client_context {
    struct eXosip_t *ctx;
    pthread_t event_thread;
    pthread_mutex_t lock;
    
    // 配置
    ClientConfig config;
    int running;
    int registered;
    
    // 注册信息
    int register_id;
    int register_tid;
    time_t register_time;
    
    // 统计
    int request_count;
    int response_count;
    int timeout_count;
    
    // PHP回调
    zval response_callback;
    zval timeout_callback;
    zval error_callback;
    int callbacks_valid;
    
} ClientContext;

// 核心功能
SipContext* sip_init(ServerInfo *info);
int sip_start(SipContext *ctx);
int sip_stop(SipContext *ctx);
void sip_destroy(SipContext *ctx);

// 连接管理
int sip_get_connection_count(SipContext *ctx);
ConnectionInfo* sip_get_connection(SipContext *ctx, int conn_id);
ConnectionInfo* sip_find_connection_by_device(SipContext *ctx, const char *device_id);
void sip_close_connection(SipContext *ctx, int conn_id);
void sip_cleanup_expired_connections(SipContext *ctx, int timeout_seconds);

// 会话管理
int sip_create_session(SipContext *ctx, int conn_id, SessionType type);
SessionInfo* sip_get_session(SipContext *ctx, int session_id);
void sip_close_session(SipContext *ctx, int session_id);
void sip_cleanup_expired_sessions(SipContext *ctx, int timeout_seconds);

// 消息发送
int sip_send_register_response(SipContext *ctx, int tid, int code, const char *auth_header);
int sip_send_message_response(SipContext *ctx, int tid, int code, const char *body);
int sip_send_invite(SipContext *ctx, const char *target_uri, const char *sdp_body);
int sip_send_bye(SipContext *ctx, int call_id, int dialog_id);
int sip_send_ack(SipContext *ctx, int dialog_id);
int sip_send_message(SipContext *ctx, const char *target_uri, const char *content_type, const char *body);

// GB28181专用功能
int sip_send_catalog_query(SipContext *ctx, const char *device_id);
int sip_send_device_info_query(SipContext *ctx, const char *device_id);
int sip_send_ptz_control(SipContext *ctx, const char *device_id, const char *channel_id, int cmd, int speed);
int sip_send_keepalive_response(SipContext *ctx, int tid);

// 消息解析
int parse_gb28181_message(const char *xml, GB28181Message *msg);
int parse_sip_register(osip_message_t *sip_msg, char *device_id, char *contact_uri, char *user_agent);
int validate_sip_auth(osip_message_t *sip_msg, const char *password, const char *realm, const char *nonce);

// PHP回调设置
void sip_set_event_callback(SipContext *ctx, zval *callback);
void sip_set_connection_callback(SipContext *ctx, zval *callback);
void sip_set_message_callback(SipContext *ctx, zval *callback);
void sip_set_error_callback(SipContext *ctx, zval *callback);

// 定时器设置（单线程事件循环）
void sip_set_timer_callback(SipContext *ctx, zval *callback, int interval_ms);
int sip_check_and_fire_timer(SipContext *ctx);  // 检查并触发定时器，返回是否触发

// 数据转换（给PHP用）
void connection_to_php_array(ConnectionInfo *conn, zval *arr);
void session_to_php_array(SessionInfo *session, zval *arr);
void gb28181_message_to_php_array(GB28181Message *msg, zval *arr);
void sip_event_to_php_array(eXosip_event_t *evt, ConnectionInfo *conn, SessionInfo *session, zval *arr);

// 原始数据访问
int sip_enable_raw_data_capture(SipContext *ctx, int enable);
char* sip_get_raw_request(SipContext *ctx, int tid);
char* sip_get_raw_response(SipContext *ctx, int tid);
RawData* sip_get_raw_data(SipContext *ctx, int index);
int sip_get_raw_data_count(SipContext *ctx);
void sip_clear_raw_data(SipContext *ctx);

// 统计和诊断
void sip_get_statistics(SipContext *ctx, SipStatistics *stats);
void sip_reset_statistics(SipContext *ctx);
void sip_get_stats(SipContext *ctx, zval *stats_array);
void sip_get_connections_info(SipContext *ctx, zval *connections_array);
void sip_get_sessions_info(SipContext *ctx, zval *sessions_array);

// 工具函数
char* generate_nonce(void);
char* generate_call_id(void);
char* generate_sn(void);
int parse_xml_tag(const char *xml, const char *tag, char *value, int max_len);
char* build_gb28181_xml(GB28181CmdType cmd_type, const char *device_id, const char *sn, const void *data);

// 内部事件处理
void* sip_event_thread(void *arg);
void handle_sip_event(SipContext *ctx, eXosip_event_t *evt);
void handle_register_new(SipContext *ctx, eXosip_event_t *evt);
void handle_message_new(SipContext *ctx, eXosip_event_t *evt);
void handle_invite_new(SipContext *ctx, eXosip_event_t *evt);
void handle_call_answered(SipContext *ctx, eXosip_event_t *evt);
void handle_call_closed(SipContext *ctx, eXosip_event_t *evt);

// ==================== PHP兼容性接口 ====================
SipContext* exosip_init_wrapper(ServerInfo *info);
void exosip_quit_wrapper(SipContext *ctx);
void exosip_event_loop_php(SipContext *ctx, zval *callback);
int exosip_listen_wrapper(SipContext *ctx);
int exosip_stop_wrapper(SipContext *ctx);
void exosip_set_callbacks_wrapper(SipContext *ctx, zval *event_cb, zval *conn_cb, zval *msg_cb, zval *err_cb);

// ==================== 新的非阻塞API ====================
int exosip_get_events_nonblocking(SipContext *ctx, zval *events_array, int timeout_ms);
int exosip_get_socket_fd(SipContext *ctx);
int exosip_send_message_wrapper(SipContext *ctx, const char *to, const char *message);
int exosip_send_message_with_content_type(SipContext *ctx, const char *to, const char *message, const char *content_type);
int exosip_send_response_wrapper(SipContext *ctx, int tid, int code, const char *reason, const char *headers);
zval* exosip_create_event_object(eXosip_event_t *evt, ConnectionInfo *conn, SessionInfo *session);
zval* exosip_create_session_object(SessionInfo *session);
void exosip_create_event_object_array(eXosip_event_t *evt, ConnectionInfo *conn, SessionInfo *session, zval *event_array);
void exosip_create_session_object_array(SessionInfo *session, zval *session_array);

// ============ 客户端API ============
ClientContext* client_init(ClientConfig *config);
int client_start(ClientContext *ctx);
int client_stop(ClientContext *ctx);
void client_destroy(ClientContext *ctx);

// 客户端请求发送
int client_send_register(ClientContext *ctx);
int client_send_unregister(ClientContext *ctx);
int client_send_message(ClientContext *ctx, const char *to_uri, const char *body, const char *content_type);
int client_send_invite(ClientContext *ctx, const char *to_uri, const char *sdp);
int client_send_bye(ClientContext *ctx, int did, int cid);
int client_send_ack(ClientContext *ctx, int did, int cid);
int client_send_options(ClientContext *ctx, const char *to_uri);
int client_send_info(ClientContext *ctx, int did, int cid, const char *body);

// 客户端响应发送
int client_send_answer(ClientContext *ctx, int tid, int status);

// 客户端状态查询
int client_is_registered(ClientContext *ctx);
int client_get_stats(ClientContext *ctx, zval *arr);

// 客户端事件处理（非阻塞）
int client_process_events(ClientContext *ctx, int timeout_ms, zval *events_array);

#ifdef __cplusplus
}
#endif

#endif /* EXOSIP_WRAPPER_H */