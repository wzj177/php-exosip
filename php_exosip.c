#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_exosip.h"
#include "exosip_wrapper.h"
#include "zend_exceptions.h"
#include "ext/standard/php_var.h"
#include "zend_smart_str.h"
#include <signal.h>
#include <eXosip2/eXosip.h>

/* Event extension detection */
static zend_bool has_event_extension = 0;

/* Global SIP context for session close operations */
SipContext *global_sip_ctx = NULL;

/* Class entries */
zend_class_entry *exosip_ce;
zend_class_entry *sip_event_ce;
zend_class_entry *sip_session_ce;

/* ===== ArgInfo Definitions ===== */

/* ExoSip class arginfo */
ZEND_BEGIN_ARG_INFO_EX(arginfo_exosip_construct, 0, 0, 0)
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, config, IS_ARRAY, 1, "null")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosip_init, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, config, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosip_quit, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosip_processevents, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosip_sendmessage, 0, 0, 2)
    ZEND_ARG_TYPE_INFO(0, to, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, message, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, contentType, IS_STRING, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosip_sendresponse, 0, 0, 2)
    ZEND_ARG_TYPE_INFO(0, tid, IS_LONG, 0)
    ZEND_ARG_TYPE_INFO(0, code, IS_LONG, 0)
    ZEND_ARG_TYPE_INFO(0, reason, IS_STRING, 1)
    ZEND_ARG_TYPE_INFO(0, headers, IS_ARRAY, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosip_getfd, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosip_run, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosip_stop, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosip_isrunning, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosip_setconfig, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, config, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosip_getconfig, 0, 0, 0)
    ZEND_ARG_TYPE_INFO(0, key, IS_STRING, 1)
ZEND_END_ARG_INFO()



ZEND_BEGIN_ARG_INFO_EX(arginfo_exosip_getstats, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosip_addtask, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, data, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosip_sendtoworker, 0, 0, 1)
    ZEND_ARG_INFO(0, data)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosip_getprocessstatus, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosip_getrunstatus, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, pid_file, IS_STRING, 0)
ZEND_END_ARG_INFO()

/* SipEvent class arginfo */
ZEND_BEGIN_ARG_INFO_EX(arginfo_sipevent_gettype, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sipevent_getcode, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sipevent_getfromuri, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sipevent_gettouri, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sipevent_getrequesturi, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sipevent_getbody, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sipevent_getcontenttype, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sipevent_gettid, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sipevent_getexpires, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sipevent_getsession, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sipevent_getconnection, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sipevent_getheader, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
ZEND_END_ARG_INFO()

/* SipSession class arginfo */
ZEND_BEGIN_ARG_INFO_EX(arginfo_sipsession_getid, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sipsession_getcallid, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sipsession_getfromuri, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sipsession_gettouri, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sipsession_getstate, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sipsession_getrawbody, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sipsession_close, 0, 0, 0)
ZEND_END_ARG_INFO()

/* ExoSipClient class arginfo */
ZEND_BEGIN_ARG_INFO_EX(arginfo_exosipclient_construct, 0, 0, 0)
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, config, IS_ARRAY, 1, "null")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosipclient_start, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosipclient_stop, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosipclient_sendregister, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosipclient_sendunregister, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosipclient_sendmessage, 0, 0, 2)
    ZEND_ARG_TYPE_INFO(0, to_uri, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, body, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, content_type, IS_STRING, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosipclient_sendinvite, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, to_uri, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, sdp, IS_STRING, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosipclient_sendbye, 0, 0, 2)
    ZEND_ARG_TYPE_INFO(0, did, IS_LONG, 0)
    ZEND_ARG_TYPE_INFO(0, cid, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosipclient_sendoptions, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, to_uri, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosipclient_isregistered, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosipclient_getstats, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosipclient_processevents, 0, 0, 0)
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, timeout_ms, IS_LONG, 0, "0")
ZEND_END_ARG_INFO()

/* 全局函数已移除 - 只保留 OOP API */

/* ===== SipSession Class ===== */
typedef struct _php_sip_session_obj {
    SessionInfo *session_info;
    zend_object std;
} php_sip_session_obj;

static inline php_sip_session_obj *php_sip_session_from_obj(zend_object *obj) {
    return (php_sip_session_obj*)((char*)(obj) - XtOffsetOf(php_sip_session_obj, std));
}

static void php_sip_session_free_obj(zend_object *object) {
    php_sip_session_obj *obj = php_sip_session_from_obj(object);
    if (obj->session_info) {
        // 这里可以添加session清理逻辑
        efree(obj->session_info);
    }
    zend_object_std_dtor(&obj->std);
}

static zend_object_handlers sip_session_object_handlers;

static zend_object *php_sip_session_create_object(zend_class_entry *ce) {
    php_sip_session_obj *obj = ecalloc(1, sizeof(php_sip_session_obj));
    
    zend_object_std_init(&obj->std, ce);
    object_properties_init(&obj->std, ce);
    obj->std.handlers = &sip_session_object_handlers;
    
    obj->session_info = NULL;
    
    return &obj->std;
}

/* SipSession methods */
PHP_METHOD(SipSession, getId) {
    php_sip_session_obj *obj = php_sip_session_from_obj(Z_OBJ_P(getThis()));
    if (obj->session_info) {
        RETURN_LONG(obj->session_info->id);  // 修复：使用id而不是session_id
    }
    RETURN_NULL();
}

PHP_METHOD(SipSession, getCallId) {
    php_sip_session_obj *obj = php_sip_session_from_obj(Z_OBJ_P(getThis()));
    if (obj->session_info) {
        RETURN_LONG(obj->session_info->call_id);  // 修复：call_id是int类型，不是字符串
    }
    RETURN_NULL();
}

PHP_METHOD(SipSession, getFromUri) {
    php_sip_session_obj *obj = php_sip_session_from_obj(Z_OBJ_P(getThis()));
    if (obj->session_info && strlen(obj->session_info->from_uri) > 0) {  // 修复：检查字符串长度而不是地址
        RETURN_STRING(obj->session_info->from_uri);
    }
    RETURN_NULL();
}

PHP_METHOD(SipSession, getToUri) {
    php_sip_session_obj *obj = php_sip_session_from_obj(Z_OBJ_P(getThis()));
    if (obj->session_info && strlen(obj->session_info->to_uri) > 0) {  // 修复：检查字符串长度而不是地址
        RETURN_STRING(obj->session_info->to_uri);
    }
    RETURN_NULL();
}

PHP_METHOD(SipSession, getState) {
    php_sip_session_obj *obj = php_sip_session_from_obj(Z_OBJ_P(getThis()));
    if (obj->session_info) {
        RETURN_LONG(obj->session_info->type);  // 使用type作为state
    }
    RETURN_NULL();
}

PHP_METHOD(SipSession, getRawBody) {
    php_sip_session_obj *obj = php_sip_session_from_obj(Z_OBJ_P(getThis()));
    if (obj->session_info && strlen(obj->session_info->raw_body) > 0) {
        RETURN_STRING(obj->session_info->raw_body);
    }
    RETURN_NULL();
}

PHP_METHOD(SipSession, close) {
    php_sip_session_obj *obj = php_sip_session_from_obj(Z_OBJ_P(getThis()));
    
    if (!obj->session_info) {
        php_error_docref(NULL, E_WARNING, "Session not initialized");
        RETURN_FALSE;
    }
    
    // 获取全局 SipContext
    extern SipContext *global_sip_ctx;
    if (!global_sip_ctx || !global_sip_ctx->ctx) {
        php_error_docref(NULL, E_WARNING, "SIP context not available");
        RETURN_FALSE;
    }
    
    // 如果有 call_id 和 dialog_id，发送 BYE
    if (obj->session_info->call_id > 0 && obj->session_info->dialog_id > 0) {
        int ret = sip_send_bye(global_sip_ctx, 
                               obj->session_info->call_id, 
                               obj->session_info->dialog_id);
        if (ret != 0) {
            php_error_docref(NULL, E_WARNING, "Failed to send BYE: %d", ret);
        }
    }
    
    // 关闭会话
    sip_close_session(global_sip_ctx, obj->session_info->id);
    
    RETURN_TRUE;
}

const zend_function_entry sip_session_methods[] = {
    PHP_ME(SipSession, getId, arginfo_sipsession_getid, ZEND_ACC_PUBLIC)
    PHP_ME(SipSession, getCallId, arginfo_sipsession_getcallid, ZEND_ACC_PUBLIC)
    PHP_ME(SipSession, getFromUri, arginfo_sipsession_getfromuri, ZEND_ACC_PUBLIC)
    PHP_ME(SipSession, getToUri, arginfo_sipsession_gettouri, ZEND_ACC_PUBLIC)
    PHP_ME(SipSession, getState, arginfo_sipsession_getstate, ZEND_ACC_PUBLIC)
    PHP_ME(SipSession, getRawBody, arginfo_sipsession_getrawbody, ZEND_ACC_PUBLIC)
    PHP_ME(SipSession, close, arginfo_sipsession_close, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

/* ===== SipEvent Class ===== */
typedef struct _php_sip_event_obj {
    int event_type;
    int event_code;      // SIP 事件代码
    int response_code;   // SIP 响应代码 (1xx-6xx)
    int tid;             // 事务ID
    int expires;         // Expires 头值（秒）
    char *from_uri;
    char *to_uri;
    char *request_uri;
    char *body;
    char *content_type;  // Content-Type
    php_sip_session_obj *session;
    zval connection;     // 连接信息数组
    zval headers;        // 所有 SIP 头字段数组
    zend_object std;
} php_sip_event_obj;

static inline php_sip_event_obj *php_sip_event_from_obj(zend_object *obj) {
    return (php_sip_event_obj*)((char*)(obj) - XtOffsetOf(php_sip_event_obj, std));
}

static void php_sip_event_free_obj(zend_object *object) {
    php_sip_event_obj *obj = php_sip_event_from_obj(object);
    if (obj->from_uri) efree(obj->from_uri);
    if (obj->to_uri) efree(obj->to_uri);
    if (obj->request_uri) efree(obj->request_uri);
    if (obj->body) efree(obj->body);
    if (obj->content_type) efree(obj->content_type);
    if (obj->session) {
        zend_object_release(&obj->session->std);
    }
    zval_ptr_dtor(&obj->connection);
    zval_ptr_dtor(&obj->headers);
    zend_object_std_dtor(&obj->std);
}

static zend_object_handlers sip_event_object_handlers;

static zend_object *php_sip_event_create_object(zend_class_entry *ce) {
    php_sip_event_obj *obj = ecalloc(1, sizeof(php_sip_event_obj));
    
    zend_object_std_init(&obj->std, ce);
    object_properties_init(&obj->std, ce);
    obj->std.handlers = &sip_event_object_handlers;
    
    obj->event_type = 0;
    obj->event_code = 0;
    obj->tid = 0;
    obj->expires = -1;
    obj->from_uri = NULL;
    obj->to_uri = NULL;
    obj->request_uri = NULL;
    obj->body = NULL;
    obj->content_type = NULL;
    obj->session = NULL;
    ZVAL_NULL(&obj->connection);
    ZVAL_NULL(&obj->headers);
    
    return &obj->std;
}

/* SipEvent methods */
PHP_METHOD(SipEvent, getType) {
    php_sip_event_obj *obj = php_sip_event_from_obj(Z_OBJ_P(getThis()));
    RETURN_LONG(obj->event_type);
}

PHP_METHOD(SipEvent, getCode) {
    php_sip_event_obj *obj = php_sip_event_from_obj(Z_OBJ_P(getThis()));
    RETURN_LONG(obj->event_code);
}

PHP_METHOD(SipEvent, getFromUri) {
    php_sip_event_obj *obj = php_sip_event_from_obj(Z_OBJ_P(getThis()));
    if (obj->from_uri) {
        RETURN_STRING(obj->from_uri);
    }
    RETURN_NULL();
}

PHP_METHOD(SipEvent, getToUri) {
    php_sip_event_obj *obj = php_sip_event_from_obj(Z_OBJ_P(getThis()));
    if (obj->to_uri) {
        RETURN_STRING(obj->to_uri);
    }
    RETURN_NULL();
}

PHP_METHOD(SipEvent, getRequestUri) {
    php_sip_event_obj *obj = php_sip_event_from_obj(Z_OBJ_P(getThis()));
    if (obj->request_uri) {
        RETURN_STRING(obj->request_uri);
    }
    RETURN_NULL();
}

PHP_METHOD(SipEvent, getBody) {
    php_sip_event_obj *obj = php_sip_event_from_obj(Z_OBJ_P(getThis()));
    if (obj->body) {
        RETURN_STRING(obj->body);
    }
    RETURN_NULL();
}

PHP_METHOD(SipEvent, getContentType) {
    php_sip_event_obj *obj = php_sip_event_from_obj(Z_OBJ_P(getThis()));
    if (obj->content_type) {
        RETURN_STRING(obj->content_type);
    }
    RETURN_NULL();
}

PHP_METHOD(SipEvent, getTid) {
    php_sip_event_obj *obj = php_sip_event_from_obj(Z_OBJ_P(getThis()));
    RETURN_LONG(obj->tid);
}

PHP_METHOD(SipEvent, getExpires) {
    php_sip_event_obj *obj = php_sip_event_from_obj(Z_OBJ_P(getThis()));
    RETURN_LONG(obj->expires);
}

PHP_METHOD(SipEvent, getSession) {
    php_sip_event_obj *obj = php_sip_event_from_obj(Z_OBJ_P(getThis()));
    if (obj->session) {
        // 增加引用计数并返回对象
        GC_ADDREF(&obj->session->std);
        RETURN_OBJ(&obj->session->std);
    }
    RETURN_NULL();
}

PHP_METHOD(SipEvent, getConnection) {
    php_sip_event_obj *obj = php_sip_event_from_obj(Z_OBJ_P(getThis()));
    if (Z_TYPE(obj->connection) == IS_ARRAY) {
        RETURN_ZVAL(&obj->connection, 1, 0);
    }
    RETURN_NULL();
}

PHP_METHOD(SipEvent, getHeader) {
    char *name;
    size_t name_len;
    
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(name, name_len)
    ZEND_PARSE_PARAMETERS_END();
    
    php_sip_event_obj *obj = php_sip_event_from_obj(Z_OBJ_P(getThis()));
    
    if (Z_TYPE(obj->headers) == IS_ARRAY) {
        zval *val = zend_hash_str_find(Z_ARRVAL(obj->headers), name, name_len);
        if (val && Z_TYPE_P(val) == IS_STRING) {
            RETURN_STR(zval_get_string(val));
        }
    }
    
    RETURN_NULL();
}

const zend_function_entry sip_event_methods[] = {
    PHP_ME(SipEvent, getType, arginfo_sipevent_gettype, ZEND_ACC_PUBLIC)
    PHP_ME(SipEvent, getCode, arginfo_sipevent_getcode, ZEND_ACC_PUBLIC)
    PHP_ME(SipEvent, getFromUri, arginfo_sipevent_getfromuri, ZEND_ACC_PUBLIC)
    PHP_ME(SipEvent, getToUri, arginfo_sipevent_gettouri, ZEND_ACC_PUBLIC)
    PHP_ME(SipEvent, getRequestUri, arginfo_sipevent_getrequesturi, ZEND_ACC_PUBLIC)
    PHP_ME(SipEvent, getBody, arginfo_sipevent_getbody, ZEND_ACC_PUBLIC)
    PHP_ME(SipEvent, getContentType, arginfo_sipevent_getcontenttype, ZEND_ACC_PUBLIC)
    PHP_ME(SipEvent, getTid, arginfo_sipevent_gettid, ZEND_ACC_PUBLIC)
    PHP_ME(SipEvent, getExpires, arginfo_sipevent_getexpires, ZEND_ACC_PUBLIC)
    PHP_ME(SipEvent, getSession, arginfo_sipevent_getsession, ZEND_ACC_PUBLIC)
    PHP_ME(SipEvent, getConnection, arginfo_sipevent_getconnection, ZEND_ACC_PUBLIC)
    PHP_ME(SipEvent, getHeader, arginfo_sipevent_getheader, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

/* ===== Universal SIP Server Class ===== */
typedef struct _php_exosip_obj {
    SipContext *ctx;
    
    /* Core SIP Event Callbacks (RFC 3261 + Extensions) */
    zval onRegister;     // REGISTER - 用户/设备注册
    zval onInvite;       // INVITE - 会话邀请 
    zval onAck;          // ACK - 最终响应确认
    zval onBye;          // BYE - 会话终止
    zval onCancel;       // CANCEL - 请求取消
    zval onOptions;      // OPTIONS - 能力查询
    
    /* SIP Extension Methods */
    zval onMessage;      // MESSAGE - SIP 即时消息 (RFC 3428)
    zval onInfo;         // INFO - 会话内信息 (RFC 6086)
    zval onUpdate;       // UPDATE - 会话更新 (RFC 3311) 
    zval onPrack;        // PRACK - 临时响应确认 (RFC 3262)
    zval onRefer;        // REFER - 呼叫转移 (RFC 3515)
    
    /* Publish-Subscribe Events (RFC 3903, RFC 3856) */
    zval onSubscribe;    // SUBSCRIBE - 事件订阅
    zval onNotify;       // NOTIFY - 事件通知
    zval onPublish;      // PUBLISH - 状态发布
    
    /* Response and Error Handling */
    zval onResponse;     // SIP 响应处理 (1xx-6xx)
    zval onTimeout;      // 请求超时事件
    zval onError;        // 协议错误事件
    
    /* Connection Management */  
    zval onConnect;      // 连接建立 (TCP/TLS)
    zval onClose;        // 连接关闭
    
    /* Timer Support (Single-threaded Event Loop) */
    zval onTimer;        // 定时器回调
    
    /* Master-Worker-Task Support */
    zval onTask;         // Task 进程回调
    zval onTaskFinish;   // Task 完成回调（Worker 进程）
    zval onPipeMessage;  // Pipe 消息回调（Task→Worker主动推送）
    
    /* Universal SIP Configuration */
    HashTable *config;   // 服务器配置参数
    
    /* Runtime State */
    int is_running;      // 事件循环运行状态
    zend_object std;
} php_exosip_obj;

/* Forward declarations for universal SIP event processing */
static int php_exosip_parse_event_data(php_sip_event_obj *event_obj, zval *event_data);
static const char* php_exosip_get_event_type_name(int event_type, osip_message_t *request);
static zval* php_exosip_get_event_callback(php_exosip_obj *obj, const char *event_type);
static zval php_exosip_call_event_handler(zval *callback, zval *event_obj);
static void php_exosip_call_error_handler(php_exosip_obj *obj, const char *error_msg);
static void php_exosip_signal_handler(int sig);
static void php_exosip_parse_session_data(php_sip_event_obj *event_obj, zval *session_data);

/* Instance management for multi-instance support */
typedef struct _php_exosip_instance_node {
    php_exosip_obj *instance;
    struct _php_exosip_instance_node *next;
} php_exosip_instance_node;

static php_exosip_instance_node *g_instance_list = NULL;
static int g_instance_count = 0;

static void php_exosip_register_instance(php_exosip_obj *obj) {
    php_exosip_instance_node *node = (php_exosip_instance_node*)malloc(sizeof(php_exosip_instance_node));
    if (node) {
        node->instance = obj;
        node->next = g_instance_list;
        g_instance_list = node;
        g_instance_count++;
    }
}

static void php_exosip_unregister_instance(php_exosip_obj *obj) {
    php_exosip_instance_node **curr = &g_instance_list;
    while (*curr) {
        if ((*curr)->instance == obj) {
            php_exosip_instance_node *to_free = *curr;
            *curr = (*curr)->next;
            free(to_free);
            g_instance_count--;
            break;
        }
        curr = &((*curr)->next);
    }
}

static inline php_exosip_obj *php_exosip_from_obj(zend_object *obj) {
    return (php_exosip_obj*)((char*)(obj) - XtOffsetOf(php_exosip_obj, std));
}

static void php_exosip_free_obj(zend_object *object) {
    php_exosip_obj *obj = php_exosip_from_obj(object);
    
    // 停止运行
    obj->is_running = 0;
    
    // 从实例链表中移除
    php_exosip_unregister_instance(obj);
    
    // 清理eXosip上下文
    if (obj->ctx) {
        // 确保停止运行
        if (obj->ctx->running) {
            obj->ctx->running = 0;
            // 给一点时间让事件循环退出
            usleep(50000); // 50ms
        }
        
        // 安全地清理
        exosip_quit_wrapper(obj->ctx);
        obj->ctx = NULL;
    }
    
    /* Clean up all event callbacks only if config was initialized (meaning the object was actually used) */
    if (obj->config) {
        #define SAFE_ZVAL_DTOR(zv) if (Z_TYPE(zv) != IS_UNDEF) { zval_ptr_dtor(&zv); }
        
        /* Core SIP methods */
        SAFE_ZVAL_DTOR(obj->onRegister);
        SAFE_ZVAL_DTOR(obj->onInvite);
        SAFE_ZVAL_DTOR(obj->onAck);
        SAFE_ZVAL_DTOR(obj->onBye);
        SAFE_ZVAL_DTOR(obj->onCancel);
        SAFE_ZVAL_DTOR(obj->onOptions);
        
        /* SIP extensions */
        SAFE_ZVAL_DTOR(obj->onMessage);
        SAFE_ZVAL_DTOR(obj->onInfo);
        SAFE_ZVAL_DTOR(obj->onUpdate);
        SAFE_ZVAL_DTOR(obj->onPrack);
        SAFE_ZVAL_DTOR(obj->onRefer);
        
        /* Publish-Subscribe */
        SAFE_ZVAL_DTOR(obj->onSubscribe);
        SAFE_ZVAL_DTOR(obj->onNotify);
        SAFE_ZVAL_DTOR(obj->onPublish);
        
        /* Response and error handling */
        SAFE_ZVAL_DTOR(obj->onResponse);
        SAFE_ZVAL_DTOR(obj->onTimeout);
        SAFE_ZVAL_DTOR(obj->onError);
        
        /* Connection management */
        SAFE_ZVAL_DTOR(obj->onConnect);
        SAFE_ZVAL_DTOR(obj->onClose);
        SAFE_ZVAL_DTOR(obj->onTimer);
        
        /* Master-Worker-Task */
        SAFE_ZVAL_DTOR(obj->onTask);
        SAFE_ZVAL_DTOR(obj->onTaskFinish);
        SAFE_ZVAL_DTOR(obj->onPipeMessage);
        
        #undef SAFE_ZVAL_DTOR
    }
    
    /* Clean up configuration */
    if (obj->config) {
        zend_hash_destroy(obj->config);
        FREE_HASHTABLE(obj->config);
    }

    
    zend_object_std_dtor(&obj->std);
}

static zend_object_handlers exosip_object_handlers;

/* Universal SIP Event callback property handlers */
static zval *exosip_read_property(zend_object *object, zend_string *member, int type, void **cache_slot, zval *rv) {
    php_exosip_obj *obj = php_exosip_from_obj(object);
    const char *prop_name = ZSTR_VAL(member);
    
    /* Core SIP methods */
    if (strcmp(prop_name, "onRegister") == 0) return &obj->onRegister;
    if (strcmp(prop_name, "onInvite") == 0) return &obj->onInvite;
    if (strcmp(prop_name, "onAck") == 0) return &obj->onAck;
    if (strcmp(prop_name, "onBye") == 0) return &obj->onBye;
    if (strcmp(prop_name, "onCancel") == 0) return &obj->onCancel;
    if (strcmp(prop_name, "onOptions") == 0) return &obj->onOptions;
    
    /* SIP extensions */
    if (strcmp(prop_name, "onMessage") == 0) return &obj->onMessage;
    if (strcmp(prop_name, "onInfo") == 0) return &obj->onInfo;
    if (strcmp(prop_name, "onUpdate") == 0) return &obj->onUpdate;
    if (strcmp(prop_name, "onPrack") == 0) return &obj->onPrack;
    if (strcmp(prop_name, "onRefer") == 0) return &obj->onRefer;
    
    /* Publish-Subscribe */
    if (strcmp(prop_name, "onSubscribe") == 0) return &obj->onSubscribe;
    if (strcmp(prop_name, "onNotify") == 0) return &obj->onNotify;
    if (strcmp(prop_name, "onPublish") == 0) return &obj->onPublish;
    
    /* Response and error handling */
    if (strcmp(prop_name, "onResponse") == 0) return &obj->onResponse;
    if (strcmp(prop_name, "onTimeout") == 0) return &obj->onTimeout;
    if (strcmp(prop_name, "onError") == 0) return &obj->onError;
    
    /* Connection management */
    if (strcmp(prop_name, "onConnect") == 0) return &obj->onConnect;
    if (strcmp(prop_name, "onClose") == 0) return &obj->onClose;
    
    /* Timer support */
    if (strcmp(prop_name, "onTimer") == 0) return &obj->onTimer;
    
    /* Master-Worker-Task */
    if (strcmp(prop_name, "onTask") == 0) return &obj->onTask;
    if (strcmp(prop_name, "onTaskFinish") == 0) return &obj->onTaskFinish;
    if (strcmp(prop_name, "onPipeMessage") == 0) return &obj->onPipeMessage;
    
    return zend_std_read_property(object, member, type, cache_slot, rv);
}

static zval *exosip_write_property(zend_object *object, zend_string *member, zval *value, void **cache_slot) {
    php_exosip_obj *obj = php_exosip_from_obj(object);
    const char *prop_name = ZSTR_VAL(member);
    
    /* Core SIP methods */
    if (strcmp(prop_name, "onRegister") == 0) { ZVAL_COPY(&obj->onRegister, value); return &obj->onRegister; }
    if (strcmp(prop_name, "onInvite") == 0) { ZVAL_COPY(&obj->onInvite, value); return &obj->onInvite; }
    if (strcmp(prop_name, "onAck") == 0) { ZVAL_COPY(&obj->onAck, value); return &obj->onAck; }
    if (strcmp(prop_name, "onBye") == 0) { ZVAL_COPY(&obj->onBye, value); return &obj->onBye; }
    if (strcmp(prop_name, "onCancel") == 0) { ZVAL_COPY(&obj->onCancel, value); return &obj->onCancel; }
    if (strcmp(prop_name, "onOptions") == 0) { ZVAL_COPY(&obj->onOptions, value); return &obj->onOptions; }
    
    /* SIP extensions */
    if (strcmp(prop_name, "onMessage") == 0) { ZVAL_COPY(&obj->onMessage, value); return &obj->onMessage; }
    if (strcmp(prop_name, "onInfo") == 0) { ZVAL_COPY(&obj->onInfo, value); return &obj->onInfo; }
    if (strcmp(prop_name, "onUpdate") == 0) { ZVAL_COPY(&obj->onUpdate, value); return &obj->onUpdate; }
    if (strcmp(prop_name, "onPrack") == 0) { ZVAL_COPY(&obj->onPrack, value); return &obj->onPrack; }
    if (strcmp(prop_name, "onRefer") == 0) { ZVAL_COPY(&obj->onRefer, value); return &obj->onRefer; }
    
    /* Publish-Subscribe */
    if (strcmp(prop_name, "onSubscribe") == 0) { ZVAL_COPY(&obj->onSubscribe, value); return &obj->onSubscribe; }
    if (strcmp(prop_name, "onNotify") == 0) { ZVAL_COPY(&obj->onNotify, value); return &obj->onNotify; }
    if (strcmp(prop_name, "onPublish") == 0) { ZVAL_COPY(&obj->onPublish, value); return &obj->onPublish; }
    
    /* Response and error handling */
    if (strcmp(prop_name, "onResponse") == 0) { ZVAL_COPY(&obj->onResponse, value); return &obj->onResponse; }
    if (strcmp(prop_name, "onTimeout") == 0) { ZVAL_COPY(&obj->onTimeout, value); return &obj->onTimeout; }
    if (strcmp(prop_name, "onError") == 0) { ZVAL_COPY(&obj->onError, value); return &obj->onError; }
    
    /* Connection management */
    if (strcmp(prop_name, "onConnect") == 0) { ZVAL_COPY(&obj->onConnect, value); return &obj->onConnect; }
    if (strcmp(prop_name, "onClose") == 0) { ZVAL_COPY(&obj->onClose, value); return &obj->onClose; }
    if (strcmp(prop_name, "onTimer") == 0) { 
        ZVAL_COPY(&obj->onTimer, value); 
        // 设置到 C 层定时器
        if (obj->ctx) {
            // 从 config 中获取 timer_interval，默认 1000ms
            zval *interval_val = zend_hash_str_find(obj->config, "timer_interval", 14);
            int interval_ms = (interval_val && Z_TYPE_P(interval_val) == IS_LONG) ? Z_LVAL_P(interval_val) : 1000;
            sip_set_timer_callback(obj->ctx, value, interval_ms);
        }
        return &obj->onTimer; 
    }
    
    /* Master-Worker-Task */
    if (strcmp(prop_name, "onTask") == 0) { ZVAL_COPY(&obj->onTask, value); return &obj->onTask; }
    if (strcmp(prop_name, "onTaskFinish") == 0) { ZVAL_COPY(&obj->onTaskFinish, value); return &obj->onTaskFinish; }
    if (strcmp(prop_name, "onPipeMessage") == 0) { ZVAL_COPY(&obj->onPipeMessage, value); return &obj->onPipeMessage; }
    
    return zend_std_write_property(object, member, value, cache_slot);
}

static zend_object *php_exosip_create_object(zend_class_entry *ce) {
    php_exosip_obj *obj = ecalloc(1, sizeof(php_exosip_obj));
    
    zend_object_std_init(&obj->std, ce);
    object_properties_init(&obj->std, ce);
    obj->std.handlers = &exosip_object_handlers;
    
    obj->ctx = NULL;
    obj->config = NULL;
    obj->is_running = 0;
    
    // 初始化所有回调 zval 为 UNDEF（关键：避免野指针）
    ZVAL_UNDEF(&obj->onRegister);
    ZVAL_UNDEF(&obj->onInvite);
    ZVAL_UNDEF(&obj->onAck);
    ZVAL_UNDEF(&obj->onBye);
    ZVAL_UNDEF(&obj->onCancel);
    ZVAL_UNDEF(&obj->onOptions);
    ZVAL_UNDEF(&obj->onMessage);
    ZVAL_UNDEF(&obj->onInfo);
    ZVAL_UNDEF(&obj->onUpdate);
    ZVAL_UNDEF(&obj->onPrack);
    ZVAL_UNDEF(&obj->onRefer);
    ZVAL_UNDEF(&obj->onSubscribe);
    ZVAL_UNDEF(&obj->onNotify);
    ZVAL_UNDEF(&obj->onPublish);
    ZVAL_UNDEF(&obj->onResponse);
    ZVAL_UNDEF(&obj->onTimeout);
    ZVAL_UNDEF(&obj->onError);
    ZVAL_UNDEF(&obj->onConnect);
    ZVAL_UNDEF(&obj->onClose);
    ZVAL_UNDEF(&obj->onTimer);  // 定时器回调
    ZVAL_UNDEF(&obj->onTask);
    ZVAL_UNDEF(&obj->onTaskFinish);
    ZVAL_UNDEF(&obj->onPipeMessage);
    
    return &obj->std;
}

/* ========== ExoSip::init(array $config) ========== */
/* ========== ExoSip::__construct() ========== */
PHP_METHOD(ExoSip, __construct) {
    zval *configArr = NULL;
    ZEND_PARSE_PARAMETERS_START(0,1)
        Z_PARAM_OPTIONAL
        Z_PARAM_ARRAY_OR_NULL(configArr)
    ZEND_PARSE_PARAMETERS_END();

    php_exosip_obj *obj = php_exosip_from_obj(Z_OBJ_P(getThis()));
    
    // 初始化对象
    obj->ctx = NULL;
    obj->is_running = 0;
    
    // 初始化配置HashTable (只初始化一次)
    if (obj->config == NULL) {
        ALLOC_HASHTABLE(obj->config);
        zend_hash_init(obj->config, 0, NULL, ZVAL_PTR_DTOR, 0);
    }
    
    // 如果传入了配置，则自动初始化
    if (configArr != NULL) {
        // 直接调用初始化逻辑，而不是通过方法调用
        ServerInfo info;
        memset(&info, 0, sizeof(ServerInfo)); // 初始化结构体
        zval *val;

        val = zend_hash_str_find(Z_ARRVAL_P(configArr), "ua", 2);
        info.ua = (val && Z_TYPE_P(val) == IS_STRING) ? Z_STRVAL_P(val) : "PHP-GB28181";
        val = zend_hash_str_find(Z_ARRVAL_P(configArr), "ip", 2);
        info.ip = (val && Z_TYPE_P(val) == IS_STRING) ? Z_STRVAL_P(val) : "0.0.0.0";
        val = zend_hash_str_find(Z_ARRVAL_P(configArr), "port", 4);
        info.port = (val && Z_TYPE_P(val) == IS_LONG) ? Z_LVAL_P(val) : 5060;
        val = zend_hash_str_find(Z_ARRVAL_P(configArr), "mode", 4);
        if (val && Z_TYPE_P(val) == IS_STRING) {
            info.mode = Z_STRVAL_P(val);
        } else {
            // 平台自动检测
#ifdef __APPLE__
            info.mode = "udp";  // macOS强制UDP
            if (info.debug) fprintf(stderr, "[INFO] macOS detected, forcing UDP mode\n");
#else
            info.mode = "udp";  // 默认UDP
#endif
        }
        val = zend_hash_str_find(Z_ARRVAL_P(configArr), "sipId", 5);
        info.sipId = (val && Z_TYPE_P(val) == IS_STRING) ? Z_STRVAL_P(val) : "";
        val = zend_hash_str_find(Z_ARRVAL_P(configArr), "sipRealm", 8);
        info.sipRealm = (val && Z_TYPE_P(val) == IS_STRING) ? Z_STRVAL_P(val) : "";
        val = zend_hash_str_find(Z_ARRVAL_P(configArr), "sipPass", 7);
        info.sipPass = (val && Z_TYPE_P(val) == IS_STRING) ? Z_STRVAL_P(val) : "";
        val = zend_hash_str_find(Z_ARRVAL_P(configArr), "sipTimeout", 10);
        info.sipTimeout = (val && Z_TYPE_P(val) == IS_LONG) ? Z_LVAL_P(val) : 30;
        val = zend_hash_str_find(Z_ARRVAL_P(configArr), "sipExpiry", 9);
        info.sipExpiry = (val && Z_TYPE_P(val) == IS_LONG) ? Z_LVAL_P(val) : 3600;
        val = zend_hash_str_find(Z_ARRVAL_P(configArr), "debug", 5);
        info.debug = (val && (Z_TYPE_P(val) == IS_TRUE || (Z_TYPE_P(val) == IS_LONG && Z_LVAL_P(val)))) ? 1 : 0;

        // Read task_worker_num config (before init)
        val = zend_hash_str_find(Z_ARRVAL_P(configArr), "task_worker_num", 15);
        int task_worker_num = (val && Z_TYPE_P(val) == IS_LONG) ? Z_LVAL_P(val) : 0;
        
        // ✅ 如果是多进程模式，延迟初始化（在 Worker 进程中初始化）
        if (task_worker_num > 0) {
            // 只创建空的 SipContext，不绑定端口
            obj->ctx = (SipContext*)calloc(1, sizeof(SipContext));
            if (!obj->ctx) {
                php_error_docref(NULL, E_ERROR, "Failed to allocate SipContext");
                return;
            }
            
            // 保存配置，稍后在 Worker 中初始化
            obj->ctx->server_info = info;
            obj->ctx->task_count = task_worker_num;
            obj->ctx->running = 0; // 标记未初始化
            
            // 读取 pid_file 配置
            val = zend_hash_str_find(Z_ARRVAL_P(configArr), "pid_file", 8);
            if (val && Z_TYPE_P(val) == IS_STRING) {
                strncpy(obj->ctx->pid_file, Z_STRVAL_P(val), sizeof(obj->ctx->pid_file) - 1);
            } else {
                snprintf(obj->ctx->pid_file, sizeof(obj->ctx->pid_file), "/tmp/php_exosip_%d.pid", info.port);
            }
            
            php_printf("[INFO] Multi-process mode enabled, eXosip will be initialized in Worker process\n");
            php_printf("[INFO] PID file: %s\n", obj->ctx->pid_file);
        } else {
            // 单进程模式：立即初始化
            obj->ctx = exosip_init_wrapper(&info);
            if (!obj->ctx) {
                php_error_docref(NULL, E_WARNING, "Failed to init eXosip in constructor - port %d may be in use", info.port);
            }
        }
    }
}

/* ========== ExoSip::init() ========== */
PHP_METHOD(ExoSip, init) {
    zval *configArr;
    ZEND_PARSE_PARAMETERS_START(1,1)
        Z_PARAM_ARRAY(configArr)
    ZEND_PARSE_PARAMETERS_END();

    ServerInfo info;
    memset(&info, 0, sizeof(ServerInfo));
    
    zval *val;

    // 使用默认值或从配置中读取
    val = zend_hash_str_find(Z_ARRVAL_P(configArr), "ua", 2);
    info.ua = (val && Z_TYPE_P(val) == IS_STRING) ? Z_STRVAL_P(val) : "PHP-GB28181";
    
    val = zend_hash_str_find(Z_ARRVAL_P(configArr), "ip", 2);
    info.ip = (val && Z_TYPE_P(val) == IS_STRING) ? Z_STRVAL_P(val) : "0.0.0.0";
    
    val = zend_hash_str_find(Z_ARRVAL_P(configArr), "port", 4);
    info.port = (val && Z_TYPE_P(val) == IS_LONG) ? (int)Z_LVAL_P(val) : 5060;
    
    val = zend_hash_str_find(Z_ARRVAL_P(configArr), "mode", 4);
    if (val && Z_TYPE_P(val) == IS_STRING) {
        info.mode = Z_STRVAL_P(val);
    } else {
#ifdef __APPLE__
        info.mode = "udp";
        if (info.debug) fprintf(stderr, "[INFO] macOS detected, forcing UDP mode\n");
#else
        info.mode = "udp";
#endif
    }
    
    val = zend_hash_str_find(Z_ARRVAL_P(configArr), "sipId", 5);
    info.sipId = (val && Z_TYPE_P(val) == IS_STRING) ? Z_STRVAL_P(val) : "";
    
    val = zend_hash_str_find(Z_ARRVAL_P(configArr), "sipRealm", 8);
    info.sipRealm = (val && Z_TYPE_P(val) == IS_STRING) ? Z_STRVAL_P(val) : "";
    
    val = zend_hash_str_find(Z_ARRVAL_P(configArr), "sipPass", 7);
    info.sipPass = (val && Z_TYPE_P(val) == IS_STRING) ? Z_STRVAL_P(val) : "";
    
    val = zend_hash_str_find(Z_ARRVAL_P(configArr), "sipTimeout", 10);
    info.sipTimeout = (val && Z_TYPE_P(val) == IS_LONG) ? (int)Z_LVAL_P(val) : 30;
    
    val = zend_hash_str_find(Z_ARRVAL_P(configArr), "sipExpiry", 9);
    info.sipExpiry = (val && Z_TYPE_P(val) == IS_LONG) ? (int)Z_LVAL_P(val) : 3600;
    
    val = zend_hash_str_find(Z_ARRVAL_P(configArr), "debug", 5);
    info.debug = (val && (Z_TYPE_P(val) == IS_TRUE || (Z_TYPE_P(val) == IS_LONG && Z_LVAL_P(val)))) ? 1 : 0;

    php_exosip_obj *obj = php_exosip_from_obj(Z_OBJ_P(getThis()));
    obj->ctx = exosip_init_wrapper(&info);
    if (!obj->ctx) {
        php_error_docref(NULL, E_ERROR, "Failed to init eXosip (check mode: %s, port: %d, ip: %s)", 
                         info.mode, info.port, info.ip);
        RETURN_FALSE;
    }
    
    // Set global context for session close operations
    global_sip_ctx = obj->ctx;
    
    RETURN_TRUE;
}

/* ========== ExoSip::quit() ========== */
PHP_METHOD(ExoSip, quit) {
    php_exosip_obj *obj = php_exosip_from_obj(Z_OBJ_P(getThis()));
    
    // 停止运行
    obj->is_running = 0;
    if (obj->ctx) {
        obj->ctx->running = 0;
    }
    
    // 清理全局上下文
    if (global_sip_ctx == obj->ctx) {
        global_sip_ctx = NULL;
    }
    
    // 清理eXosip
    if (obj->ctx) {
        exosip_quit_wrapper(obj->ctx);
        obj->ctx = NULL;
    }
    
    RETURN_TRUE;
}

/* ========== ExoSip::processEvents() - 非阻塞获取事件 ========== */
PHP_METHOD(ExoSip, processEvents) {
    zend_long timeout_ms = 0;  // 修复：使用zend_long而不是long
    
    ZEND_PARSE_PARAMETERS_START(0,1)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(timeout_ms)
    ZEND_PARSE_PARAMETERS_END();

    php_exosip_obj *obj = php_exosip_from_obj(Z_OBJ_P(getThis()));
    if (!obj->ctx) {
        php_error_docref(NULL, E_WARNING, "eXosip not initialized");
        RETURN_FALSE;
    }

    // 使用新的非阻塞API获取事件
    zval events_array;
    int event_count = exosip_get_events_nonblocking(obj->ctx, &events_array, (int)timeout_ms);
    
    if (event_count < 0) {
        php_error_docref(NULL, E_WARNING, "Failed to get SIP events");
        RETURN_FALSE;
    }
    
    // 处理每个事件，创建SipEvent对象
    array_init(return_value);
    
    if (event_count > 0) {
        HashTable *events_ht = Z_ARRVAL(events_array);
        zval *event_data;
        
        ZEND_HASH_FOREACH_VAL(events_ht, event_data) {
            // 创建SipEvent对象
            zval sip_event_obj;
            object_init_ex(&sip_event_obj, sip_event_ce);
            php_sip_event_obj *event_obj = php_sip_event_from_obj(Z_OBJ(sip_event_obj));
            
            // 从数组中提取事件数据
            zval *val;
            
            if ((val = zend_hash_str_find(Z_ARRVAL_P(event_data), "type", 4)) != NULL) {
                event_obj->event_type = Z_LVAL_P(val);
            }
            
            if ((val = zend_hash_str_find(Z_ARRVAL_P(event_data), "ss_status", 9)) != NULL) {
                event_obj->event_code = Z_LVAL_P(val);
            }
            
            if ((val = zend_hash_str_find(Z_ARRVAL_P(event_data), "from_uri", 8)) != NULL) {
                if (Z_TYPE_P(val) == IS_STRING) {
                    event_obj->from_uri = estrndup(Z_STRVAL_P(val), Z_STRLEN_P(val));
                }
            }
            
            if ((val = zend_hash_str_find(Z_ARRVAL_P(event_data), "to_uri", 6)) != NULL) {
                if (Z_TYPE_P(val) == IS_STRING) {
                    event_obj->to_uri = estrndup(Z_STRVAL_P(val), Z_STRLEN_P(val));
                }
            }
            
            if ((val = zend_hash_str_find(Z_ARRVAL_P(event_data), "request_uri", 11)) != NULL) {
                if (Z_TYPE_P(val) == IS_STRING) {
                    event_obj->request_uri = estrndup(Z_STRVAL_P(val), Z_STRLEN_P(val));
                }
            }
            
            if ((val = zend_hash_str_find(Z_ARRVAL_P(event_data), "body", 4)) != NULL) {
                if (Z_TYPE_P(val) == IS_STRING) {
                    event_obj->body = estrndup(Z_STRVAL_P(val), Z_STRLEN_P(val));
                }
            }
            
            // 处理session信息
            if ((val = zend_hash_str_find(Z_ARRVAL_P(event_data), "session", 7)) != NULL) {
                if (Z_TYPE_P(val) == IS_ARRAY) {
                    // 创建SipSession对象
                    zend_object *session_zobj = php_sip_session_create_object(sip_session_ce);
                    php_sip_session_obj *session_obj = php_sip_session_from_obj(session_zobj);
                    
                    // 分配session_info内存
                    session_obj->session_info = emalloc(sizeof(SessionInfo));
                    memset(session_obj->session_info, 0, sizeof(SessionInfo));
                    
                    // 从数组填充session数据
                    zval *session_val;
                    if ((session_val = zend_hash_str_find(Z_ARRVAL_P(val), "id", 2)) != NULL) {
                        session_obj->session_info->id = Z_LVAL_P(session_val);
                    }
                    if ((session_val = zend_hash_str_find(Z_ARRVAL_P(val), "call_id", 7)) != NULL) {
                        session_obj->session_info->call_id = Z_LVAL_P(session_val);
                    }
                    if ((session_val = zend_hash_str_find(Z_ARRVAL_P(val), "from_uri", 8)) != NULL && Z_TYPE_P(session_val) == IS_STRING) {
                        strncpy(session_obj->session_info->from_uri, Z_STRVAL_P(session_val), sizeof(session_obj->session_info->from_uri) - 1);
                    }
                    if ((session_val = zend_hash_str_find(Z_ARRVAL_P(val), "to_uri", 6)) != NULL && Z_TYPE_P(session_val) == IS_STRING) {
                        strncpy(session_obj->session_info->to_uri, Z_STRVAL_P(session_val), sizeof(session_obj->session_info->to_uri) - 1);
                    }
                    if ((session_val = zend_hash_str_find(Z_ARRVAL_P(val), "raw_body", 8)) != NULL && Z_TYPE_P(session_val) == IS_STRING) {
                        strncpy(session_obj->session_info->raw_body, Z_STRVAL_P(session_val), sizeof(session_obj->session_info->raw_body) - 1);
                    }
                    if ((session_val = zend_hash_str_find(Z_ARRVAL_P(val), "dialog_id", 9)) != NULL) {
                        session_obj->session_info->dialog_id = Z_LVAL_P(session_val);
                    }
                    
                    // 设置session对象引用（直接持有对象，引用计数已经是1）
                    event_obj->session = session_obj;
                }
            }
            
            // 添加到返回数组（这会增加对象引用计数）
            add_next_index_zval(return_value, &sip_event_obj);
            
            // 重要：释放栈上zval的引用，但不销毁对象（因为已被数组持有）
            zval_ptr_dtor(&sip_event_obj);
            
        } ZEND_HASH_FOREACH_END();
    }
    
    // 清理临时数组
    zval_dtor(&events_array);
}

PHP_METHOD(ExoSip, run) {
    ZEND_PARSE_PARAMETERS_NONE();
    
    php_exosip_obj *obj = php_exosip_from_obj(Z_OBJ_P(getThis()));
    
    if (!obj->ctx) {
        php_error_docref(NULL, E_WARNING, "eXosip not initialized");
        RETURN_FALSE;
    }

    // Check if Master-Worker-Task mode is enabled
    if (obj->ctx->task_count > 0) {
        // 绑定 Task 回调到 SipContext（在 fork 前）
        if (!Z_ISUNDEF(obj->onTask)) {
            ZVAL_COPY(&obj->ctx->task_callback, &obj->onTask);
            php_printf("[DEBUG] onTask callback set before fork\n");
        } else {
            php_printf("[DEBUG] onTask callback is not set\n");
        }
        if (!Z_ISUNDEF(obj->onTaskFinish)) {
            ZVAL_COPY(&obj->ctx->task_finish_callback, &obj->onTaskFinish);
            php_printf("[DEBUG] onTaskFinish callback set before fork\n");
        } else {
            php_printf("[DEBUG] onTaskFinish callback is not set\n");
        }
        if (!Z_ISUNDEF(obj->onPipeMessage)) {
            ZVAL_COPY(&obj->ctx->pipe_message_callback, &obj->onPipeMessage);
            php_printf("[DEBUG] onPipeMessage callback set before fork\n");
        } else {
            php_printf("[DEBUG] onPipeMessage callback is not set\n");
        }
        
        if (sip_start_master_process(obj->ctx) < 0) {
            php_error_docref(NULL, E_WARNING, "Failed to start Master process");
            RETURN_FALSE;
        }
        
        if (obj->ctx->is_master) {
            php_printf("[Master] SIP Server started with %d Task workers\n", obj->ctx->task_count);
            sip_master_loop(obj->ctx);
            RETURN_TRUE;
        }
        
        // Task processes: they are already in sip_task_loop()
        if (obj->ctx->is_task) {
            // This code path should not be reached as Task processes call _exit() in sip_task_loop
            RETURN_TRUE;
        }
        
        // Worker process: initialize eXosip and use dedicated event loop
        if (obj->ctx->is_worker) {
            php_printf("[Worker] Initializing eXosip and entering SIP event loop (PID=%d)\n", getpid());
            
            // ✅ Worker 进程：初始化 eXosip（绑定端口）
            struct eXosip_t *exosip_ctx = eXosip_malloc();
            if (!exosip_ctx) {
                php_error_docref(NULL, E_ERROR, "[Worker] eXosip_malloc() failed");
                RETURN_FALSE;
            }
            
            if (eXosip_init(exosip_ctx) != 0) {
                php_error_docref(NULL, E_ERROR, "[Worker] eXosip_init() failed");
                RETURN_FALSE;
            }
            
            // 设置 User-Agent
            eXosip_set_user_agent(exosip_ctx, obj->ctx->server_info.ua);
            
            // 监听端口
            const char *mode = obj->ctx->server_info.mode;
            int transport = IPPROTO_UDP;
            if (strcmp(mode, "tcp") == 0) transport = IPPROTO_TCP;
            else if (strcmp(mode, "tls") == 0) transport = IPPROTO_TCP; // TLS 需要额外配置
            
            int ret = eXosip_listen_addr(exosip_ctx, transport, 
                                          obj->ctx->server_info.ip, 
                                          obj->ctx->server_info.port, 
                                          AF_INET, 0);
            if (ret != 0) {
                php_error_docref(NULL, E_ERROR, "[Worker] eXosip_listen_addr() failed on %s:%d",
                    obj->ctx->server_info.ip, obj->ctx->server_info.port);
                RETURN_FALSE;
            }
            
            obj->ctx->ctx = exosip_ctx;
            obj->ctx->running = 1;
            
            php_printf("[Worker] eXosip listening on %s:%d (%s)\n",
                obj->ctx->server_info.ip, obj->ctx->server_info.port, mode);
            
            // Save original signal handlers
            struct sigaction old_sigint, old_sigterm;
            struct sigaction sa;
            memset(&sa, 0, sizeof(sa));
            sa.sa_handler = php_exosip_signal_handler;
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = 0;
            
            sigaction(SIGINT, &sa, &old_sigint);
            sigaction(SIGTERM, &sa, &old_sigterm);
            
            // Worker process: process SIP events
            obj->ctx->running = 1;
            obj->is_running = 1;
            
            php_exosip_register_instance(obj);
            
            while (obj->ctx->running && obj->is_running) {
                // Get SIP events (non-blocking, 100ms timeout)
                zval events_array;
                int event_count = exosip_get_events_nonblocking(obj->ctx, &events_array, 100);
                
                if (event_count < 0) {
                    php_error_docref(NULL, E_WARNING, "[Worker] Failed to get SIP events");
                    break;
                }
                
                if (event_count > 0) {
                    HashTable *events_ht = Z_ARRVAL(events_array);
                    zval *event_data;
                    
                    ZEND_HASH_FOREACH_VAL(events_ht, event_data) {
                        zval sip_event_obj;
                        object_init_ex(&sip_event_obj, sip_event_ce);
                        php_sip_event_obj *event_obj = php_sip_event_from_obj(Z_OBJ(sip_event_obj));
                        
                        if (!php_exosip_parse_event_data(event_obj, event_data)) {
                            zval_ptr_dtor(&sip_event_obj);
                            continue;
                        }
                        
                        zval *method_val = zend_hash_str_find(Z_ARRVAL_P(event_data), "method", 6);
                        osip_message_t *dummy_request = NULL;
                        const char* event_type = php_exosip_get_event_type_name(event_obj->event_type, dummy_request);
                        
                        if (event_obj->event_type == EXOSIP_MESSAGE_NEW && method_val && Z_TYPE_P(method_val) == IS_STRING) {
                            const char *method = Z_STRVAL_P(method_val);
                            if (strcmp(method, "REGISTER") == 0) {
                                event_type = "register";
                            } else if (strcmp(method, "MESSAGE") == 0) {
                                event_type = "message";
                            }
                        }
                        
                        zval *callback = php_exosip_get_event_callback(obj, event_type);
                        
                        if (callback) {
                            Z_TRY_ADDREF(sip_event_obj);
                            
                            zend_try {
                                // 清除之前可能残留的异常
                                if (EG(exception)) {
                                    zend_clear_exception();
                                }
                                
                                zval result = php_exosip_call_event_handler(callback, &sip_event_obj);
                                
                                // 检查是否有异常产生
                                if (EG(exception)) {
                                    // 获取异常信息
                                    zval *exception = EG(exception);
                                    char error_buffer[1024];
                                    const char *error_msg = "Unknown error";
                                    
                                    if (exception && Z_TYPE_P(exception) == IS_OBJECT) {
                                        zend_class_entry *ce = Z_OBJCE_P(exception);
                                        
                                        // 尝试获取异常消息
                                        zval rv;
                                        zval *message = zend_read_property(ce, Z_OBJ_P(exception), "message", sizeof("message")-1, 0, &rv);
                                        
                                        if (message && Z_TYPE_P(message) == IS_STRING && Z_STRLEN_P(message) > 0) {
                                            snprintf(error_buffer, sizeof(error_buffer), "%s: %s", 
                                                    ZSTR_VAL(ce->name), Z_STRVAL_P(message));
                                            error_msg = error_buffer;
                                        } else {
                                            snprintf(error_buffer, sizeof(error_buffer), "%s", ZSTR_VAL(ce->name));
                                            error_msg = error_buffer;
                                        }
                                        
                                        // 如果有文件和行号信息,也包含进来
                                        zval *file = zend_read_property(ce, Z_OBJ_P(exception), "file", sizeof("file")-1, 0, &rv);
                                        zval *line = zend_read_property(ce, Z_OBJ_P(exception), "line", sizeof("line")-1, 0, &rv);
                                        
                                        if (file && Z_TYPE_P(file) == IS_STRING && line && Z_TYPE_P(line) == IS_LONG) {
                                            char temp_buffer[1024];
                                            snprintf(temp_buffer, sizeof(temp_buffer), "%s in %s:%ld", 
                                                    error_buffer, Z_STRVAL_P(file), Z_LVAL_P(line));
                                            strncpy(error_buffer, temp_buffer, sizeof(error_buffer)-1);
                                            error_buffer[sizeof(error_buffer)-1] = '\0';
                                        }
                                    }
                                    
                                    php_printf("[Worker] Event callback error: %s\n", error_msg);
                                    
                                    // 调用 onError 回调
                                    php_exosip_call_error_handler(obj, error_msg);
                                    
                                    zend_clear_exception();
                                } else if (Z_TYPE(result) == IS_FALSE) {
                                    php_printf("[Worker] Server shutdown requested\n");
                                    obj->is_running = 0;
                                    obj->ctx->running = 0;
                                }
                                
                                if (!Z_ISUNDEF(result)) {
                                    zval_ptr_dtor(&result);
                                }
                            } zend_catch {
                                php_printf("[Worker] Event callback bailout caught, continuing...\n");
                                // 调用 onError 回调
                                php_exosip_call_error_handler(obj, "Fatal error in event callback");
                                // 清除异常状态
                                if (EG(exception)) {
                                    zend_clear_exception();
                                }
                            } zend_end_try();
                        }
                        
                        zval_ptr_dtor(&sip_event_obj);
                    } ZEND_HASH_FOREACH_END();
                    
                    zval_ptr_dtor(&events_array);
                }
                
                // Check timer (with exception protection)
                if (obj->ctx->timer_interval_ms > 0 && sip_check_and_fire_timer(obj->ctx)) {
                    if (Z_TYPE(obj->onTimer) == IS_OBJECT) {
                        zval result;
                        zval args[0];
                        
                        zend_try {
                            // 清除之前可能残留的异常
                            if (EG(exception)) {
                                zend_clear_exception();
                            }
                            
                            if (call_user_function(NULL, &obj->onTimer, &obj->onTimer, &result, 0, args) == SUCCESS) {
                                // 检查是否有异常产生
                                if (EG(exception)) {
                                    php_printf("[Worker] Timer callback generated exception, clearing and continuing...\\n");
                                    zend_clear_exception();
                                } else if (Z_TYPE(result) == IS_FALSE) {
                                    obj->is_running = 0;
                                    obj->ctx->running = 0;
                                }
                                
                                if (!Z_ISUNDEF(result)) {
                                    zval_ptr_dtor(&result);
                                }
                            }
                        } zend_catch {
                            php_printf("[Worker] Timer callback bailout caught, continuing...\\n");
                            // 清除异常状态
                            if (EG(exception)) {
                                zend_clear_exception();
                            }
                        } zend_end_try();
                    }
                }
                
                // Check Task results
                for (int i = 0; i < obj->ctx->task_count; i++) {
                    sip_handle_task_result(obj->ctx, obj->ctx->task_sockfds[i]);
                }
            }
            
            php_printf("[Worker] Exiting event loop\n");
            RETURN_TRUE;
        }
    }

    // Single-process mode: 启动通用SIP服务器事件循环
    obj->ctx->running = 1;
    obj->is_running = 1;
    
    // 注册实例用于信号处理（支持多实例）
    php_exosip_register_instance(obj);
    
    php_printf("Universal SIP Server started (Event-Driven Mode)\n");
    php_printf("Listening on %s:%d (Protocol: %s)\n", 
         obj->ctx->server_info.ip ? obj->ctx->server_info.ip : "0.0.0.0",
         obj->ctx->server_info.port,
         obj->ctx->server_info.mode ? obj->ctx->server_info.mode : "udp");
    php_printf("Supported Events: REGISTER, INVITE, MESSAGE, BYE, ACK, CANCEL, OPTIONS, INFO, UPDATE, PRACK, REFER, SUBSCRIBE, NOTIFY, PUBLISH\n");
    
    // 保存原始信号处理器并设置新的信号处理器
    {
        struct sigaction old_sigint, old_sigterm;
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = php_exosip_signal_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        
        // 设置信号处理器（使用sigaction更安全）
        sigaction(SIGINT, &sa, &old_sigint);
        sigaction(SIGTERM, &sa, &old_sigterm);
    }
    
    while (obj->ctx->running && obj->is_running) {
        // 使用非阻塞API获取事件
        zval events_array;
        int event_count = exosip_get_events_nonblocking(obj->ctx, &events_array, 100);
        
        if (event_count < 0) {
            php_error_docref(NULL, E_WARNING, "Failed to get SIP events");
            break;
        }
        
        if (event_count > 0) {
            HashTable *events_ht = Z_ARRVAL(events_array);
            zval *event_data;
            
            ZEND_HASH_FOREACH_VAL(events_ht, event_data) {
                // 创建通用 SipEvent 对象
                zval sip_event_obj;
                object_init_ex(&sip_event_obj, sip_event_ce);
                php_sip_event_obj *event_obj = php_sip_event_from_obj(Z_OBJ(sip_event_obj));
                
                // 解析事件数据
                if (!php_exosip_parse_event_data(event_obj, event_data)) {
                    zval_ptr_dtor(&sip_event_obj);
                    continue;
                }
                
                // 获取事件类型字符串
                // 从 event_data 获取 method 用于区分 REGISTER 和 MESSAGE
                zval *method_val = zend_hash_str_find(Z_ARRVAL_P(event_data), "method", 6);
                osip_message_t *dummy_request = NULL;  // 暂时传NULL，用method字符串判断
                const char* event_type = php_exosip_get_event_type_name(event_obj->event_type, dummy_request);
                
                // 特殊处理：EXOSIP_MESSAGE_NEW 根据method区分
                if (event_obj->event_type == EXOSIP_MESSAGE_NEW && method_val && Z_TYPE_P(method_val) == IS_STRING) {
                    const char *method = Z_STRVAL_P(method_val);
                    if (strcmp(method, "REGISTER") == 0) {
                        event_type = "register";
                    } else if (strcmp(method, "MESSAGE") == 0) {
                        event_type = "message";
                    }
                }
                
                // 根据事件类型分发到相应的回调
                zval *callback = php_exosip_get_event_callback(obj, event_type);
                
                if (callback) {
                    // 执行事件回调
                    // 注意：回调函数会接收对象的引用，需要增加引用计数
                    Z_TRY_ADDREF(sip_event_obj);
                    
                    zend_try {
                        zval result = php_exosip_call_event_handler(callback, &sip_event_obj);
                        
                        // 检查回调返回值，false表示停止服务器
                        if (Z_TYPE(result) == IS_FALSE) {
                            php_printf("Server shutdown requested by event handler\n");
                            obj->is_running = 0;
                            obj->ctx->running = 0;
                        }
                        zval_ptr_dtor(&result);
                    } zend_catch {
                        php_error_docref(NULL, E_WARNING, "Event callback threw an exception, continuing...");
                        // 继续运行,不崩溃
                    } zend_end_try();
                } else {
                    // 没有设置回调，输出调试信息（包含原始事件类型值便于调试）
                    php_printf(" Unhandled SIP event: %s (type=%d) from %s\n", 
                              event_type,
                              event_obj->event_type,
                              event_obj->from_uri ? event_obj->from_uri : "unknown");
                }
                
                // 释放栈上的引用（但对象可能仍被PHP回调持有）
                zval_ptr_dtor(&sip_event_obj);
                
                if (!obj->is_running) break;
                
            } ZEND_HASH_FOREACH_END();
        }
        
        zval_dtor(&events_array);
        
        // 检查并触发定时器
        if (sip_check_and_fire_timer(obj->ctx)) {
            // 触发定时器回调
            if (!Z_ISUNDEF(obj->onTimer) && Z_TYPE(obj->onTimer) == IS_OBJECT) {
                zval result;
                zval params[0];  // onTimer 不需要参数
                
                if (call_user_function(EG(function_table), NULL, &obj->onTimer, &result, 0, params) == SUCCESS) {
                    // 检查返回值，false 表示停止服务器
                    if (Z_TYPE(result) == IS_FALSE) {
                        php_printf("Server shutdown requested by timer callback\n");
                        obj->is_running = 0;
                        obj->ctx->running = 0;
                    }
                    zval_ptr_dtor(&result);
                }
            }
        }
        
        // 10ms 非阻塞延迟，避免CPU过度占用
        usleep(10000);
    }
    
    // 从实例链表中移除
    php_exosip_unregister_instance(obj);
    
    // 恢复默认信号处理
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = SIG_DFL;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGINT, &sa, NULL);
        sigaction(SIGTERM, &sa, NULL);
    }
    
    php_printf("Universal SIP Server stopped\n");
    RETURN_TRUE;
}

/* ========== Helper Functions for Event Processing ========== */

/* Parse event data into SipEvent object */
static int php_exosip_parse_event_data(php_sip_event_obj *event_obj, zval *event_data) {
    if (Z_TYPE_P(event_data) != IS_ARRAY) {
        return 0;
    }
    
    zval *val;
    HashTable *ht = Z_ARRVAL_P(event_data);
    
    // Parse event type
    if ((val = zend_hash_str_find(ht, "type", 4)) != NULL && Z_TYPE_P(val) == IS_LONG) {
        event_obj->event_type = Z_LVAL_P(val);
    }
    
    // Parse SIP method (for MESSAGE_NEW disambiguation)
    char *sip_method = NULL;
    if ((val = zend_hash_str_find(ht, "method", 6)) != NULL && Z_TYPE_P(val) == IS_STRING) {
        sip_method = Z_STRVAL_P(val);
    }
    
    // Parse transaction ID
    if ((val = zend_hash_str_find(ht, "tid", 3)) != NULL && Z_TYPE_P(val) == IS_LONG) {
        event_obj->tid = Z_LVAL_P(val);
    }
    
    // Parse Expires header
    if ((val = zend_hash_str_find(ht, "expires", 7)) != NULL && Z_TYPE_P(val) == IS_LONG) {
        event_obj->expires = Z_LVAL_P(val);
    }
    
    // Parse response code
    if ((val = zend_hash_str_find(ht, "code", 4)) != NULL && Z_TYPE_P(val) == IS_LONG) {
        event_obj->response_code = Z_LVAL_P(val);
    }
    
    // Parse URIs
    if ((val = zend_hash_str_find(ht, "from_uri", 8)) != NULL && Z_TYPE_P(val) == IS_STRING) {
        event_obj->from_uri = estrndup(Z_STRVAL_P(val), Z_STRLEN_P(val));
    }
    
    if ((val = zend_hash_str_find(ht, "to_uri", 6)) != NULL && Z_TYPE_P(val) == IS_STRING) {
        event_obj->to_uri = estrndup(Z_STRVAL_P(val), Z_STRLEN_P(val));
    }
    
    if ((val = zend_hash_str_find(ht, "request_uri", 11)) != NULL && Z_TYPE_P(val) == IS_STRING) {
        event_obj->request_uri = estrndup(Z_STRVAL_P(val), Z_STRLEN_P(val));
    }
    
    // Parse message body
    if ((val = zend_hash_str_find(ht, "body", 4)) != NULL && Z_TYPE_P(val) == IS_STRING) {
        event_obj->body = estrndup(Z_STRVAL_P(val), Z_STRLEN_P(val));
    }
    
    // Parse content type
    if ((val = zend_hash_str_find(ht, "content_type", 12)) != NULL && Z_TYPE_P(val) == IS_STRING) {
        event_obj->content_type = estrndup(Z_STRVAL_P(val), Z_STRLEN_P(val));
    }
    
    // Parse session information
    if ((val = zend_hash_str_find(ht, "session", 7)) != NULL && Z_TYPE_P(val) == IS_ARRAY) {
        php_exosip_parse_session_data(event_obj, val);
    }
    
    // Parse connection information
    if ((val = zend_hash_str_find(ht, "connection", 10)) != NULL && Z_TYPE_P(val) == IS_ARRAY) {
        ZVAL_COPY(&event_obj->connection, val);
    } else {
        ZVAL_NULL(&event_obj->connection);
    }
    
    // Parse headers
    if ((val = zend_hash_str_find(ht, "headers", 7)) != NULL && Z_TYPE_P(val) == IS_ARRAY) {
        ZVAL_COPY(&event_obj->headers, val);
    } else {
        ZVAL_NULL(&event_obj->headers);
    }
    
    return 1;
}

/* Get event type name from numeric value and request method */
static const char* php_exosip_get_event_type_name(int event_type, osip_message_t *request) {
    // EXOSIP_MESSAGE_NEW 需要根据 SIP Method 区分 REGISTER 和 MESSAGE
    if (event_type == EXOSIP_MESSAGE_NEW && request) {
        if (MSG_IS_REGISTER(request)) {
            return "register";
        } else if (MSG_IS_MESSAGE(request)) {
            return "message";
        }
    }
    
    // 使用 eXosip 库定义的实际事件类型常量
    switch (event_type) {
        // REGISTER 相关事件
        case EXOSIP_REGISTRATION_SUCCESS:
        case EXOSIP_REGISTRATION_FAILURE:
            return "register";
        
        // INVITE 相关事件
        case EXOSIP_CALL_INVITE:
        case EXOSIP_CALL_REINVITE:
            return "invite";
        
        case EXOSIP_CALL_ACK:
            return "ack";
        
        case EXOSIP_CALL_CLOSED:
            return "bye";
        
        case EXOSIP_CALL_CANCELLED:
            return "cancel";
        
        // MESSAGE 相关事件（默认）
        case EXOSIP_MESSAGE_NEW:
            return "message";
        
        case EXOSIP_CALL_MESSAGE_NEW:
            return "info";
        
        // SUBSCRIPTION 相关事件
        case EXOSIP_SUBSCRIPTION_NOTIFY:
            return "notify";
        
        case EXOSIP_IN_SUBSCRIPTION_NEW:
            return "subscribe";
        
        // 响应事件
        case EXOSIP_CALL_ANSWERED:
        case EXOSIP_CALL_PROCEEDING:
        case EXOSIP_CALL_RINGING:
        case EXOSIP_CALL_REDIRECTED:
        case EXOSIP_MESSAGE_ANSWERED:
        case EXOSIP_MESSAGE_PROCEEDING:
        case EXOSIP_SUBSCRIPTION_ANSWERED:
        case EXOSIP_SUBSCRIPTION_PROCEEDING:
            return "response";
        
        // 超时事件
        case EXOSIP_CALL_NOANSWER:
        case EXOSIP_SUBSCRIPTION_NOANSWER:
            return "timeout";
        
        // 错误事件
        case EXOSIP_CALL_REQUESTFAILURE:
        case EXOSIP_CALL_SERVERFAILURE:
        case EXOSIP_CALL_GLOBALFAILURE:
        case EXOSIP_MESSAGE_REQUESTFAILURE:
        case EXOSIP_MESSAGE_SERVERFAILURE:
        case EXOSIP_MESSAGE_GLOBALFAILURE:
        case EXOSIP_CALL_MESSAGE_REQUESTFAILURE:
        case EXOSIP_CALL_MESSAGE_SERVERFAILURE:
        case EXOSIP_CALL_MESSAGE_GLOBALFAILURE:
        case EXOSIP_SUBSCRIPTION_REQUESTFAILURE:
        case EXOSIP_SUBSCRIPTION_SERVERFAILURE:
        case EXOSIP_SUBSCRIPTION_GLOBALFAILURE:
            return "error";
        
        // 连接关闭
        case EXOSIP_CALL_RELEASED:
            return "close";
        
        default:
            return "unknown";
    }
}

/* Get callback for specific event type */
static zval* php_exosip_get_event_callback(php_exosip_obj *obj, const char *event_type) {
    if (strcmp(event_type, "register") == 0 && !Z_ISNULL(obj->onRegister)) return &obj->onRegister;
    if (strcmp(event_type, "invite") == 0 && !Z_ISNULL(obj->onInvite)) return &obj->onInvite;
    if (strcmp(event_type, "ack") == 0 && !Z_ISNULL(obj->onAck)) return &obj->onAck;
    if (strcmp(event_type, "bye") == 0 && !Z_ISNULL(obj->onBye)) return &obj->onBye;
    if (strcmp(event_type, "cancel") == 0 && !Z_ISNULL(obj->onCancel)) return &obj->onCancel;
    if (strcmp(event_type, "message") == 0 && !Z_ISNULL(obj->onMessage)) return &obj->onMessage;
    if (strcmp(event_type, "info") == 0 && !Z_ISNULL(obj->onInfo)) return &obj->onInfo;
    if (strcmp(event_type, "options") == 0 && !Z_ISNULL(obj->onOptions)) return &obj->onOptions;
    if (strcmp(event_type, "update") == 0 && !Z_ISNULL(obj->onUpdate)) return &obj->onUpdate;
    if (strcmp(event_type, "prack") == 0 && !Z_ISNULL(obj->onPrack)) return &obj->onPrack;
    if (strcmp(event_type, "refer") == 0 && !Z_ISNULL(obj->onRefer)) return &obj->onRefer;
    if (strcmp(event_type, "subscribe") == 0 && !Z_ISNULL(obj->onSubscribe)) return &obj->onSubscribe;
    if (strcmp(event_type, "notify") == 0 && !Z_ISNULL(obj->onNotify)) return &obj->onNotify;
    if (strcmp(event_type, "publish") == 0 && !Z_ISNULL(obj->onPublish)) return &obj->onPublish;
    if (strcmp(event_type, "response") == 0 && !Z_ISNULL(obj->onResponse)) return &obj->onResponse;
    if (strcmp(event_type, "timeout") == 0 && !Z_ISNULL(obj->onTimeout)) return &obj->onTimeout;
    if (strcmp(event_type, "error") == 0 && !Z_ISNULL(obj->onError)) return &obj->onError;
    if (strcmp(event_type, "connect") == 0 && !Z_ISNULL(obj->onConnect)) return &obj->onConnect;
    if (strcmp(event_type, "close") == 0 && !Z_ISNULL(obj->onClose)) return &obj->onClose;
    
    return NULL;
}

/* Execute event callback function */
static zval php_exosip_call_event_handler(zval *callback, zval *event_obj) {
    zval retval;
    ZVAL_NULL(&retval);
    
    if (!callback || !zend_is_callable(callback, 0, NULL)) {
        return retval;
    }
    
    zend_fcall_info fci;
    zend_fcall_info_cache fcc;
    
    if (zend_fcall_info_init(callback, 0, &fci, &fcc, NULL, NULL) == SUCCESS) {
        zval params[1];
        ZVAL_COPY_VALUE(&params[0], event_obj);
        
        fci.retval = &retval;
        fci.param_count = 1;
        fci.params = params;
        
        zend_call_function(&fci, &fcc);
    }
    
    return retval;
}

/* Call error handler callback */
static void php_exosip_call_error_handler(php_exosip_obj *obj, const char *error_msg) {
    if (Z_ISNULL(obj->onError)) {
        return;
    }
    
    if (!zend_is_callable(&obj->onError, 0, NULL)) {
        return;
    }
    
    zend_fcall_info fci;
    zend_fcall_info_cache fcc;
    
    if (zend_fcall_info_init(&obj->onError, 0, &fci, &fcc, NULL, NULL) == SUCCESS) {
        zval params[1];
        zval retval;
        
        ZVAL_STRING(&params[0], error_msg);
        ZVAL_NULL(&retval);
        
        fci.retval = &retval;
        fci.param_count = 1;
        fci.params = params;
        
        zend_call_function(&fci, &fcc);
        
        zval_ptr_dtor(&params[0]);
        if (!Z_ISUNDEF(retval)) {
            zval_ptr_dtor(&retval);
        }
    }
}

/* Signal handler for graceful shutdown (multi-instance support) */
static void php_exosip_signal_handler(int sig) {
    php_exosip_instance_node *node = g_instance_list;
    while (node) {
        if (node->instance) {
            node->instance->is_running = 0;
            if (node->instance->ctx) {
                node->instance->ctx->running = 0;
            }
        }
        node = node->next;
    }
}

/* Parse session data from event */
static void php_exosip_parse_session_data(php_sip_event_obj *event_obj, zval *session_data) {
    // 实现session数据解析逻辑
    if (Z_TYPE_P(session_data) != IS_ARRAY) {
        return;
    }
    
    // 创建 SipSession 对象（直接创建 zend_object）
    zend_object *session_zobj = php_sip_session_create_object(sip_session_ce);
    php_sip_session_obj *session_obj = php_sip_session_from_obj(session_zobj);
    
    session_obj->session_info = emalloc(sizeof(SessionInfo));
    memset(session_obj->session_info, 0, sizeof(SessionInfo));
    
    // 解析session字段
    zval *val;
    HashTable *ht = Z_ARRVAL_P(session_data);
    
    if ((val = zend_hash_str_find(ht, "id", 2)) != NULL && Z_TYPE_P(val) == IS_LONG) {
        session_obj->session_info->id = Z_LVAL_P(val);
    }
    
    if ((val = zend_hash_str_find(ht, "call_id", 7)) != NULL && Z_TYPE_P(val) == IS_LONG) {
        session_obj->session_info->call_id = Z_LVAL_P(val);
    }
    
    if ((val = zend_hash_str_find(ht, "from_uri", 8)) != NULL && Z_TYPE_P(val) == IS_STRING) {
        strncpy(session_obj->session_info->from_uri, Z_STRVAL_P(val), sizeof(session_obj->session_info->from_uri) - 1);
    }
    
    if ((val = zend_hash_str_find(ht, "to_uri", 6)) != NULL && Z_TYPE_P(val) == IS_STRING) {
        strncpy(session_obj->session_info->to_uri, Z_STRVAL_P(val), sizeof(session_obj->session_info->to_uri) - 1);
    }
    
    if ((val = zend_hash_str_find(ht, "raw_body", 8)) != NULL && Z_TYPE_P(val) == IS_STRING) {
        strncpy(session_obj->session_info->raw_body, Z_STRVAL_P(val), sizeof(session_obj->session_info->raw_body) - 1);
    }
    
    if ((val = zend_hash_str_find(ht, "dialog_id", 9)) != NULL && Z_TYPE_P(val) == IS_LONG) {
        session_obj->session_info->dialog_id = Z_LVAL_P(val);
    }
    
    // 设置session对象引用（直接持有对象，引用计数已经是1）
    event_obj->session = session_obj;
}

/* ========== ExoSip::sendMessage(string $to, string $message) ========== */
PHP_METHOD(ExoSip, sendMessage) {
    char *to, *message, *content_type = NULL;
    size_t to_len, message_len, content_type_len = 0;
    
    ZEND_PARSE_PARAMETERS_START(2,3)
        Z_PARAM_STRING(to, to_len)
        Z_PARAM_STRING(message, message_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_STRING(content_type, content_type_len)
    ZEND_PARSE_PARAMETERS_END();

    php_exosip_obj *obj = php_exosip_from_obj(Z_OBJ_P(getThis()));
    if (!obj->ctx) {
        php_error_docref(NULL, E_WARNING, "eXosip not initialized");
        RETURN_FALSE;
    }

    int result = exosip_send_message_with_content_type(obj->ctx, to, message, content_type);
    RETURN_BOOL(result == 0);
}

/* ========== ExoSip::sendResponse(int $tid, int $code, string $reason, array $headers) ========== */
PHP_METHOD(ExoSip, sendResponse) {
    zend_long tid, code;
    char *reason = NULL;
    size_t reason_len = 0;
    HashTable *headers = NULL;
    
    ZEND_PARSE_PARAMETERS_START(2,4)
        Z_PARAM_LONG(tid)
        Z_PARAM_LONG(code)
        Z_PARAM_OPTIONAL
        Z_PARAM_STRING(reason, reason_len)
        Z_PARAM_ARRAY_HT(headers)
    ZEND_PARSE_PARAMETERS_END();

    php_exosip_obj *obj = php_exosip_from_obj(Z_OBJ_P(getThis()));
    if (!obj->ctx) {
        php_error_docref(NULL, E_WARNING, "eXosip not initialized");
        RETURN_FALSE;
    }

    // 将headers数组转换为字符串（只取第一个header）
    char headers_str[512] = {0};
    if (headers && zend_hash_num_elements(headers) > 0) {
        zend_string *key;
        zval *val;
        ZEND_HASH_FOREACH_STR_KEY_VAL(headers, key, val) {
            if (key && Z_TYPE_P(val) == IS_STRING) {
                snprintf(headers_str, sizeof(headers_str), "%s: %s", 
                         ZSTR_VAL(key), Z_STRVAL_P(val));
                break; // 只取第一个
            }
        } ZEND_HASH_FOREACH_END();
    }

    int result = exosip_send_response_wrapper(obj->ctx, (int)tid, (int)code, reason, 
                                              headers_str[0] ? headers_str : NULL);
    RETURN_BOOL(result == 0);
}

/* ========== ExoSip::getFd() - 获取socket文件描述符用于外部事件循环 ========== */
PHP_METHOD(ExoSip, getFd) {
    php_exosip_obj *obj = php_exosip_from_obj(Z_OBJ_P(getThis()));
    if (!obj->ctx) {
        php_error_docref(NULL, E_WARNING, "eXosip not initialized");
        RETURN_FALSE;
    }

    int fd = exosip_get_socket_fd(obj->ctx);
    RETURN_LONG(fd);
}

/* ========== ExoSip::stop() - 停止SIP服务器 ========== */
PHP_METHOD(ExoSip, stop) {
    ZEND_PARSE_PARAMETERS_NONE();
    
    php_exosip_obj *obj = php_exosip_from_obj(Z_OBJ_P(getThis()));
    
    if (!obj->ctx) {
        php_error_docref(NULL, E_WARNING, "eXosip not initialized");
        RETURN_FALSE;
    }
    
    php_printf("Stopping Universal SIP Server...\n");
    
    obj->is_running = 0;
    obj->ctx->running = 0;
    
    RETURN_TRUE;
}

/* ========== ExoSip::isRunning() - 检查服务器运行状态 ========== */
PHP_METHOD(ExoSip, isRunning) {
    ZEND_PARSE_PARAMETERS_NONE();
    
    php_exosip_obj *obj = php_exosip_from_obj(Z_OBJ_P(getThis()));
    
    if (!obj->ctx) {
        RETURN_FALSE;
    }
    
    RETURN_BOOL(obj->is_running && obj->ctx->running);
}

/* ========== ExoSip::setConfig() - 设置服务器配置 ========== */
PHP_METHOD(ExoSip, setConfig) {
    zval *config_array;
    
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ARRAY(config_array)
    ZEND_PARSE_PARAMETERS_END();
    
    php_exosip_obj *obj = php_exosip_from_obj(Z_OBJ_P(getThis()));
    
    if (!obj->config) {
        ALLOC_HASHTABLE(obj->config);
        zend_hash_init(obj->config, 16, NULL, ZVAL_PTR_DTOR, 0);
    }
    
    // 清空现有配置
    zend_hash_clean(obj->config);
    
    // 复制新配置
    HashTable *src_ht = Z_ARRVAL_P(config_array);
    zend_string *key;
    zval *value, copy_value;
    
    ZEND_HASH_FOREACH_STR_KEY_VAL(src_ht, key, value) {
        ZVAL_COPY(&copy_value, value);
        zend_hash_add(obj->config, key, &copy_value);
    } ZEND_HASH_FOREACH_END();
    
    php_printf(" Universal SIP Server configuration updated (%d settings)\n", zend_hash_num_elements(obj->config));
    RETURN_TRUE;
}

/* ========== ExoSip::getConfig() - 获取服务器配置 ========== */
PHP_METHOD(ExoSip, getConfig) {
    zend_string *key = NULL;
    
    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_STR(key)
    ZEND_PARSE_PARAMETERS_END();
    
    php_exosip_obj *obj = php_exosip_from_obj(Z_OBJ_P(getThis()));
    
    if (!obj->config) {
        array_init(return_value);
        return;
    }
    
    if (key) {
        // 返回特定配置项
        zval *value = zend_hash_find(obj->config, key);
        if (value) {
            ZVAL_COPY(return_value, value);
        } else {
            RETURN_NULL();
        }
    } else {
        // 返回所有配置
        array_init(return_value);
        zend_string *config_key;
        zval *config_value, copy_value;
        
        ZEND_HASH_FOREACH_STR_KEY_VAL(obj->config, config_key, config_value) {
            ZVAL_COPY(&copy_value, config_value);
            zend_hash_add(Z_ARRVAL_P(return_value), config_key, &copy_value);
        } ZEND_HASH_FOREACH_END();
    }
}



/* ========== ExoSip::getStats() - 获取服务器统计信息 ========== */
PHP_METHOD(ExoSip, getStats) {
    ZEND_PARSE_PARAMETERS_NONE();
    
    php_exosip_obj *obj = php_exosip_from_obj(Z_OBJ_P(getThis()));
    
    array_init(return_value);
    
    // 基本状态信息
    add_assoc_bool(return_value, "running", obj->is_running);
    add_assoc_long(return_value, "uptime", obj->ctx ? time(NULL) - obj->ctx->stats.start_time : 0);
    
    // 服务器信息
    if (obj->ctx) {
        add_assoc_string(return_value, "listen_ip", obj->ctx->server_info.ip ? obj->ctx->server_info.ip : "0.0.0.0");
        add_assoc_long(return_value, "listen_port", obj->ctx->server_info.port);
        add_assoc_string(return_value, "transport", obj->ctx->server_info.mode ? obj->ctx->server_info.mode : "udp");
    }
    
    // 配置统计
    add_assoc_long(return_value, "config_items", obj->config ? zend_hash_num_elements(obj->config) : 0);

    
    // 事件处理器统计
    zval handlers;
    array_init(&handlers);
    
    add_assoc_bool(&handlers, "onRegister", !Z_ISNULL(obj->onRegister));
    add_assoc_bool(&handlers, "onInvite", !Z_ISNULL(obj->onInvite));
    add_assoc_bool(&handlers, "onMessage", !Z_ISNULL(obj->onMessage));
    add_assoc_bool(&handlers, "onBye", !Z_ISNULL(obj->onBye));
    add_assoc_bool(&handlers, "onAck", !Z_ISNULL(obj->onAck));
    add_assoc_bool(&handlers, "onCancel", !Z_ISNULL(obj->onCancel));
    add_assoc_bool(&handlers, "onOptions", !Z_ISNULL(obj->onOptions));
    add_assoc_bool(&handlers, "onInfo", !Z_ISNULL(obj->onInfo));
    add_assoc_bool(&handlers, "onUpdate", !Z_ISNULL(obj->onUpdate));
    add_assoc_bool(&handlers, "onPrack", !Z_ISNULL(obj->onPrack));
    add_assoc_bool(&handlers, "onRefer", !Z_ISNULL(obj->onRefer));
    add_assoc_bool(&handlers, "onSubscribe", !Z_ISNULL(obj->onSubscribe));
    add_assoc_bool(&handlers, "onNotify", !Z_ISNULL(obj->onNotify));
    add_assoc_bool(&handlers, "onPublish", !Z_ISNULL(obj->onPublish));
    add_assoc_bool(&handlers, "onResponse", !Z_ISNULL(obj->onResponse));
    add_assoc_bool(&handlers, "onTimeout", !Z_ISNULL(obj->onTimeout));
    add_assoc_bool(&handlers, "onError", !Z_ISNULL(obj->onError));
    add_assoc_bool(&handlers, "onConnect", !Z_ISNULL(obj->onConnect));
    add_assoc_bool(&handlers, "onClose", !Z_ISNULL(obj->onClose));
    
    add_assoc_zval(return_value, "event_handlers", &handlers);
}

PHP_METHOD(ExoSip, addTask) {
    zval *data;
    
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ARRAY(data)
    ZEND_PARSE_PARAMETERS_END();
    
    php_exosip_obj *obj = php_exosip_from_obj(Z_OBJ_P(getThis()));
    
    if (!obj->ctx || !obj->ctx->is_worker) {
        php_error_docref(NULL, E_WARNING, "addTask can only be called in Worker process");
        RETURN_FALSE;
    }
    
    smart_str buf = {0};
    php_serialize_data_t var_hash;
    PHP_VAR_SERIALIZE_INIT(var_hash);
    php_var_serialize(&buf, data, &var_hash);
    PHP_VAR_SERIALIZE_DESTROY(var_hash);
    
    if (!buf.s) {
        RETURN_FALSE;
    }
    
    unsigned long task_id = sip_add_task(obj->ctx, ZSTR_VAL(buf.s), ZSTR_LEN(buf.s));
    smart_str_free(&buf);
    
    if (task_id == 0) {
        RETURN_FALSE;
    }
    
    RETURN_LONG(task_id);
}

/* ========== ExoSip::sendToWorker($data) ========== */
PHP_METHOD(ExoSip, sendToWorker) {
    zval *data;
    
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ZVAL(data)
    ZEND_PARSE_PARAMETERS_END();
    
    php_exosip_obj *obj = php_exosip_from_obj(Z_OBJ_P(getThis()));
    
    if (!obj->ctx) {
        php_error_docref(NULL, E_WARNING, "ExoSip not initialized");
        RETURN_FALSE;
    }
    
    if (!obj->ctx->is_task) {
        php_error_docref(NULL, E_WARNING, "sendToWorker can only be called from Task process");
        RETURN_FALSE;
    }
    
    // Serialize data
    smart_str buf = {0};
    php_serialize_data_t var_hash;
    PHP_VAR_SERIALIZE_INIT(var_hash);
    php_var_serialize(&buf, data, &var_hash);
    PHP_VAR_SERIALIZE_DESTROY(var_hash);
    smart_str_0(&buf);
    
    if (!buf.s) {
        php_error_docref(NULL, E_WARNING, "Failed to serialize data");
        RETURN_FALSE;
    }
    
    int result = sip_task_send_to_worker(obj->ctx, ZSTR_VAL(buf.s), ZSTR_LEN(buf.s));
    smart_str_free(&buf);
    
    if (result < 0) {
        RETURN_FALSE;
    }
    
    RETURN_TRUE;
}

/* ========== ExoSip::getProcessStatus() ========== */
PHP_METHOD(ExoSip, getProcessStatus) {
    ZEND_PARSE_PARAMETERS_NONE();
    
    php_exosip_obj *obj = php_exosip_from_obj(Z_OBJ_P(getThis()));
    
    if (!obj->ctx) {
        RETURN_FALSE;
    }
    
    sip_get_process_status(obj->ctx, return_value);
}

/* ========== ExoSip::getRunStatus() 静态方法 ========== */
PHP_METHOD(ExoSip, getRunStatus) {
    char *pid_file;
    size_t pid_file_len;
    
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(pid_file, pid_file_len)
    ZEND_PARSE_PARAMETERS_END();
    
    if (sip_read_process_status_from_pid(pid_file, return_value) < 0) {
        RETURN_FALSE;
    }
}

/* 全局函数已移除 - 统一使用 ExoSip 类的 OOP API */

/* ===== ExoSip Class methods ===== */
const zend_function_entry exosip_methods[] = {
    /* Constructor */
    PHP_ME(ExoSip, __construct, arginfo_exosip_construct, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
    
    /* Core SIP server methods */
    PHP_ME(ExoSip, init, arginfo_exosip_init, ZEND_ACC_PUBLIC)
    PHP_ME(ExoSip, quit, arginfo_exosip_quit, ZEND_ACC_PUBLIC)
    PHP_ME(ExoSip, run, arginfo_exosip_run, ZEND_ACC_PUBLIC)
    PHP_ME(ExoSip, stop, arginfo_exosip_stop, ZEND_ACC_PUBLIC)
    PHP_ME(ExoSip, isRunning, arginfo_exosip_isrunning, ZEND_ACC_PUBLIC)
    
    /* Event processing */
    PHP_ME(ExoSip, processEvents, arginfo_exosip_processevents, ZEND_ACC_PUBLIC)
    PHP_ME(ExoSip, getFd, arginfo_exosip_getfd, ZEND_ACC_PUBLIC)
    
    /* Message handling */
    PHP_ME(ExoSip, sendMessage, arginfo_exosip_sendmessage, ZEND_ACC_PUBLIC)
    PHP_ME(ExoSip, sendResponse, arginfo_exosip_sendresponse, ZEND_ACC_PUBLIC)
    
    /* Configuration and statistics */
    PHP_ME(ExoSip, setConfig, arginfo_exosip_setconfig, ZEND_ACC_PUBLIC)
    PHP_ME(ExoSip, getConfig, arginfo_exosip_getconfig, ZEND_ACC_PUBLIC)
    PHP_ME(ExoSip, getStats, arginfo_exosip_getstats, ZEND_ACC_PUBLIC)
    
    /* Master-Worker-Task */
    PHP_ME(ExoSip, addTask, arginfo_exosip_addtask, ZEND_ACC_PUBLIC)
    PHP_ME(ExoSip, sendToWorker, arginfo_exosip_sendtoworker, ZEND_ACC_PUBLIC)
    PHP_ME(ExoSip, getProcessStatus, arginfo_exosip_getprocessstatus, ZEND_ACC_PUBLIC)
    PHP_ME(ExoSip, getRunStatus, arginfo_exosip_getrunstatus, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    
    PHP_FE_END
};

/* 全局函数表已移除 */

/* ===== Module init ===== */

// ==================== ExoSipClient 类 ====================

zend_class_entry *exosip_client_ce;
zend_object_handlers exosip_client_handlers;

typedef struct {
    ClientContext *client_ctx;
    zend_object std;
} exosip_client_object;

static inline exosip_client_object* exosip_client_fetch_object(zend_object *obj) {
    return (exosip_client_object*)((char*)(obj) - XtOffsetOf(exosip_client_object, std));
}

#define Z_EXOSIP_CLIENT_OBJ_P(zv) exosip_client_fetch_object(Z_OBJ_P(zv))

static zend_object* exosip_client_create_object(zend_class_entry *ce) {
    exosip_client_object *intern = ecalloc(1, sizeof(exosip_client_object) + zend_object_properties_size(ce));
    
    zend_object_std_init(&intern->std, ce);
    object_properties_init(&intern->std, ce);
    
    intern->std.handlers = &exosip_client_handlers;
    intern->client_ctx = NULL;
    
    return &intern->std;
}

static void exosip_client_free_object(zend_object *object) {
    exosip_client_object *intern = exosip_client_fetch_object(object);
    
    if (intern->client_ctx) {
        client_destroy(intern->client_ctx);
        intern->client_ctx = NULL;
    }
    
    zend_object_std_dtor(&intern->std);
}

// __construct
PHP_METHOD(ExoSipClient, __construct) {
    zval *configArr = NULL;
    ClientConfig config;
    memset(&config, 0, sizeof(ClientConfig));
    
    // 默认值
    strcpy(config.mode, "UDP");
    config.local_port = 0;
    config.server_port = 5060;
    config.expires = 3600;
    config.debug = 0;
    strcpy(config.user_agent, "PHP-eXosip-Client/2.0");
    
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "|a", &configArr) == FAILURE) {
        return;
    }
    
    if (configArr) {
        zval *val;
        
        // server_ip (必填)
        val = zend_hash_str_find(Z_ARRVAL_P(configArr), "server_ip", 9);
        if (val && Z_TYPE_P(val) == IS_STRING) {
            strncpy(config.server_ip, Z_STRVAL_P(val), sizeof(config.server_ip) - 1);
        } else {
            zend_throw_exception(NULL, "server_ip is required", 0);
            return;
        }
        
        // server_port
        val = zend_hash_str_find(Z_ARRVAL_P(configArr), "server_port", 11);
        if (val && Z_TYPE_P(val) == IS_LONG) {
            config.server_port = Z_LVAL_P(val);
        }
        
        // username (必填)
        val = zend_hash_str_find(Z_ARRVAL_P(configArr), "username", 8);
        if (val && Z_TYPE_P(val) == IS_STRING) {
            strncpy(config.username, Z_STRVAL_P(val), sizeof(config.username) - 1);
        } else {
            zend_throw_exception(NULL, "username is required", 0);
            return;
        }
        
        // password
        val = zend_hash_str_find(Z_ARRVAL_P(configArr), "password", 8);
        if (val && Z_TYPE_P(val) == IS_STRING) {
            strncpy(config.password, Z_STRVAL_P(val), sizeof(config.password) - 1);
        }
        
        // realm
        val = zend_hash_str_find(Z_ARRVAL_P(configArr), "realm", 5);
        if (val && Z_TYPE_P(val) == IS_STRING) {
            strncpy(config.realm, Z_STRVAL_P(val), sizeof(config.realm) - 1);
        }
        
        // mode
        val = zend_hash_str_find(Z_ARRVAL_P(configArr), "mode", 4);
        if (val && Z_TYPE_P(val) == IS_STRING) {
            strncpy(config.mode, Z_STRVAL_P(val), sizeof(config.mode) - 1);
        }
        
        // local_ip
        val = zend_hash_str_find(Z_ARRVAL_P(configArr), "local_ip", 8);
        if (val && Z_TYPE_P(val) == IS_STRING) {
            strncpy(config.local_ip, Z_STRVAL_P(val), sizeof(config.local_ip) - 1);
        }
        
        // local_port
        val = zend_hash_str_find(Z_ARRVAL_P(configArr), "local_port", 10);
        if (val && Z_TYPE_P(val) == IS_LONG) {
            config.local_port = Z_LVAL_P(val);
        }
        
        // from_uri
        val = zend_hash_str_find(Z_ARRVAL_P(configArr), "from_uri", 8);
        if (val && Z_TYPE_P(val) == IS_STRING) {
            strncpy(config.from_uri, Z_STRVAL_P(val), sizeof(config.from_uri) - 1);
        }
        
        // to_uri
        val = zend_hash_str_find(Z_ARRVAL_P(configArr), "to_uri", 6);
        if (val && Z_TYPE_P(val) == IS_STRING) {
            strncpy(config.to_uri, Z_STRVAL_P(val), sizeof(config.to_uri) - 1);
        }
        
        // expires
        val = zend_hash_str_find(Z_ARRVAL_P(configArr), "expires", 7);
        if (val && Z_TYPE_P(val) == IS_LONG) {
            config.expires = Z_LVAL_P(val);
        }
        
        // debug
        val = zend_hash_str_find(Z_ARRVAL_P(configArr), "debug", 5);
        if (val) {
            config.debug = (Z_TYPE_P(val) == IS_TRUE || (Z_TYPE_P(val) == IS_LONG && Z_LVAL_P(val))) ? 1 : 0;
        }
    }
    
    ClientContext *ctx = client_init(&config);
    if (!ctx) {
        zend_throw_exception(NULL, "Failed to initialize SIP client", 0);
        return;
    }
    
    exosip_client_object *intern = Z_EXOSIP_CLIENT_OBJ_P(getThis());
    intern->client_ctx = ctx;
}

// start
PHP_METHOD(ExoSipClient, start) {
    exosip_client_object *intern = Z_EXOSIP_CLIENT_OBJ_P(getThis());
    
    if (!intern->client_ctx) {
        RETURN_FALSE;
    }
    
    int ret = client_start(intern->client_ctx);
    RETURN_BOOL(ret == 0);
}

// stop
PHP_METHOD(ExoSipClient, stop) {
    exosip_client_object *intern = Z_EXOSIP_CLIENT_OBJ_P(getThis());
    
    if (!intern->client_ctx) {
        RETURN_FALSE;
    }
    
    int ret = client_stop(intern->client_ctx);
    RETURN_BOOL(ret == 0);
}

// sendRegister
PHP_METHOD(ExoSipClient, sendRegister) {
    exosip_client_object *intern = Z_EXOSIP_CLIENT_OBJ_P(getThis());
    
    if (!intern->client_ctx) {
        RETURN_FALSE;
    }
    
    int ret = client_send_register(intern->client_ctx);
    RETURN_LONG(ret);
}

// sendUnregister
PHP_METHOD(ExoSipClient, sendUnregister) {
    exosip_client_object *intern = Z_EXOSIP_CLIENT_OBJ_P(getThis());
    
    if (!intern->client_ctx) {
        RETURN_FALSE;
    }
    
    int ret = client_send_unregister(intern->client_ctx);
    RETURN_LONG(ret);
}

// sendMessage
PHP_METHOD(ExoSipClient, sendMessage) {
    char *to_uri, *body;
    size_t to_uri_len, body_len;
    char *content_type = NULL;
    size_t content_type_len = 0;
    
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "ss|s", &to_uri, &to_uri_len, &body, &body_len, &content_type, &content_type_len) == FAILURE) {
        return;
    }
    
    exosip_client_object *intern = Z_EXOSIP_CLIENT_OBJ_P(getThis());
    
    if (!intern->client_ctx) {
        RETURN_FALSE;
    }
    
    int ret = client_send_message(intern->client_ctx, to_uri, body, content_type);
    RETURN_LONG(ret);
}

// sendInvite
PHP_METHOD(ExoSipClient, sendInvite) {
    char *to_uri, *sdp = NULL;
    size_t to_uri_len, sdp_len = 0;
    
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "s|s", &to_uri, &to_uri_len, &sdp, &sdp_len) == FAILURE) {
        return;
    }
    
    exosip_client_object *intern = Z_EXOSIP_CLIENT_OBJ_P(getThis());
    
    if (!intern->client_ctx) {
        RETURN_FALSE;
    }
    
    int ret = client_send_invite(intern->client_ctx, to_uri, sdp);
    RETURN_LONG(ret);
}

// sendBye
PHP_METHOD(ExoSipClient, sendBye) {
    zend_long did, cid;
    
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "ll", &did, &cid) == FAILURE) {
        return;
    }
    
    exosip_client_object *intern = Z_EXOSIP_CLIENT_OBJ_P(getThis());
    
    if (!intern->client_ctx) {
        RETURN_FALSE;
    }
    
    int ret = client_send_bye(intern->client_ctx, (int)did, (int)cid);
    RETURN_LONG(ret);
}

// sendOptions
PHP_METHOD(ExoSipClient, sendOptions) {
    char *to_uri;
    size_t to_uri_len;
    
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "s", &to_uri, &to_uri_len) == FAILURE) {
        return;
    }
    
    exosip_client_object *intern = Z_EXOSIP_CLIENT_OBJ_P(getThis());
    
    if (!intern->client_ctx) {
        RETURN_FALSE;
    }
    
    int ret = client_send_options(intern->client_ctx, to_uri);
    RETURN_LONG(ret);
}

// isRegistered
PHP_METHOD(ExoSipClient, isRegistered) {
    exosip_client_object *intern = Z_EXOSIP_CLIENT_OBJ_P(getThis());
    
    if (!intern->client_ctx) {
        RETURN_FALSE;
    }
    
    int registered = client_is_registered(intern->client_ctx);
    RETURN_BOOL(registered);
}

// getStats
PHP_METHOD(ExoSipClient, getStats) {
    exosip_client_object *intern = Z_EXOSIP_CLIENT_OBJ_P(getThis());
    
    if (!intern->client_ctx) {
        RETURN_FALSE;
    }
    
    array_init(return_value);
    client_get_stats(intern->client_ctx, return_value);
}

// processEvents
PHP_METHOD(ExoSipClient, processEvents) {
    zend_long timeout_ms = 0;
    
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "|l", &timeout_ms) == FAILURE) {
        return;
    }
    
    exosip_client_object *intern = Z_EXOSIP_CLIENT_OBJ_P(getThis());
    
    if (!intern->client_ctx) {
        array_init(return_value);
        return;
    }
    
    client_process_events(intern->client_ctx, (int)timeout_ms, return_value);
}

// 方法定义
static const zend_function_entry exosip_client_methods[] = {
    PHP_ME(ExoSipClient, __construct, arginfo_exosipclient_construct, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
    PHP_ME(ExoSipClient, start, arginfo_exosipclient_start, ZEND_ACC_PUBLIC)
    PHP_ME(ExoSipClient, stop, arginfo_exosipclient_stop, ZEND_ACC_PUBLIC)
    PHP_ME(ExoSipClient, sendRegister, arginfo_exosipclient_sendregister, ZEND_ACC_PUBLIC)
    PHP_ME(ExoSipClient, sendUnregister, arginfo_exosipclient_sendunregister, ZEND_ACC_PUBLIC)
    PHP_ME(ExoSipClient, sendMessage, arginfo_exosipclient_sendmessage, ZEND_ACC_PUBLIC)
    PHP_ME(ExoSipClient, sendInvite, arginfo_exosipclient_sendinvite, ZEND_ACC_PUBLIC)
    PHP_ME(ExoSipClient, sendBye, arginfo_exosipclient_sendbye, ZEND_ACC_PUBLIC)
    PHP_ME(ExoSipClient, sendOptions, arginfo_exosipclient_sendoptions, ZEND_ACC_PUBLIC)
    PHP_ME(ExoSipClient, isRegistered, arginfo_exosipclient_isregistered, ZEND_ACC_PUBLIC)
    PHP_ME(ExoSipClient, getStats, arginfo_exosipclient_getstats, ZEND_ACC_PUBLIC)
    PHP_ME(ExoSipClient, processEvents, arginfo_exosipclient_processevents, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

PHP_MINIT_FUNCTION(exosip) {
    zend_class_entry ce;
    
    // 初始化实例管理链表
    g_instance_list = NULL;
    g_instance_count = 0;
    
    // 检测 event 扩展是否已加载
    has_event_extension = zend_hash_str_exists(&module_registry, "event", sizeof("event")-1);
    
    /* Initialize ExoSip class */
    memcpy(&exosip_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    exosip_object_handlers.free_obj = php_exosip_free_obj;
    exosip_object_handlers.read_property = exosip_read_property;
    exosip_object_handlers.write_property = exosip_write_property;
    exosip_object_handlers.offset = XtOffsetOf(php_exosip_obj, std);
    
    INIT_CLASS_ENTRY(ce, "ExoSip", exosip_methods);
    exosip_ce = zend_register_internal_class(&ce);
    exosip_ce->create_object = php_exosip_create_object;
    
    /* Initialize SipEvent class */
    memcpy(&sip_event_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    sip_event_object_handlers.free_obj = php_sip_event_free_obj;
    sip_event_object_handlers.offset = XtOffsetOf(php_sip_event_obj, std);
    
    INIT_CLASS_ENTRY(ce, "SipEvent", sip_event_methods);
    sip_event_ce = zend_register_internal_class(&ce);
    sip_event_ce->create_object = php_sip_event_create_object;
    
    /* Initialize SipSession class */
    memcpy(&sip_session_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    sip_session_object_handlers.free_obj = php_sip_session_free_obj;
    sip_session_object_handlers.offset = XtOffsetOf(php_sip_session_obj, std);
    
    INIT_CLASS_ENTRY(ce, "SipSession", sip_session_methods);
    sip_session_ce = zend_register_internal_class(&ce);
    sip_session_ce->create_object = php_sip_session_create_object;
    
    /* Register ExoSipClient class */
    memcpy(&exosip_client_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    exosip_client_handlers.offset = XtOffsetOf(exosip_client_object, std);
    exosip_client_handlers.free_obj = exosip_client_free_object;
    
    INIT_CLASS_ENTRY(ce, "ExoSipClient", exosip_client_methods);
    exosip_client_ce = zend_register_internal_class(&ce);
    exosip_client_ce->create_object = exosip_client_create_object;
    
    /* Register eXosip event type constants */
    /* These match the values from eXosip2/eXosip.h */
    
    /* Registration events */
    REGISTER_LONG_CONSTANT("EXOSIP_REGISTRATION_SUCCESS", EXOSIP_REGISTRATION_SUCCESS, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_REGISTRATION_FAILURE", EXOSIP_REGISTRATION_FAILURE, CONST_CS | CONST_PERSISTENT);
    
    /* Call events */
    REGISTER_LONG_CONSTANT("EXOSIP_CALL_INVITE", EXOSIP_CALL_INVITE, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_CALL_REINVITE", EXOSIP_CALL_REINVITE, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_CALL_NOANSWER", EXOSIP_CALL_NOANSWER, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_CALL_PROCEEDING", EXOSIP_CALL_PROCEEDING, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_CALL_RINGING", EXOSIP_CALL_RINGING, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_CALL_ANSWERED", EXOSIP_CALL_ANSWERED, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_CALL_REDIRECTED", EXOSIP_CALL_REDIRECTED, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_CALL_REQUESTFAILURE", EXOSIP_CALL_REQUESTFAILURE, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_CALL_SERVERFAILURE", EXOSIP_CALL_SERVERFAILURE, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_CALL_GLOBALFAILURE", EXOSIP_CALL_GLOBALFAILURE, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_CALL_ACK", EXOSIP_CALL_ACK, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_CALL_CANCELLED", EXOSIP_CALL_CANCELLED, CONST_CS | CONST_PERSISTENT);
    
    /* Call message events */
    REGISTER_LONG_CONSTANT("EXOSIP_CALL_MESSAGE_NEW", EXOSIP_CALL_MESSAGE_NEW, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_CALL_MESSAGE_PROCEEDING", EXOSIP_CALL_MESSAGE_PROCEEDING, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_CALL_MESSAGE_ANSWERED", EXOSIP_CALL_MESSAGE_ANSWERED, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_CALL_MESSAGE_REDIRECTED", EXOSIP_CALL_MESSAGE_REDIRECTED, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_CALL_MESSAGE_REQUESTFAILURE", EXOSIP_CALL_MESSAGE_REQUESTFAILURE, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_CALL_MESSAGE_SERVERFAILURE", EXOSIP_CALL_MESSAGE_SERVERFAILURE, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_CALL_MESSAGE_GLOBALFAILURE", EXOSIP_CALL_MESSAGE_GLOBALFAILURE, CONST_CS | CONST_PERSISTENT);
    
    /* Call closed/released */
    REGISTER_LONG_CONSTANT("EXOSIP_CALL_CLOSED", EXOSIP_CALL_CLOSED, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_CALL_RELEASED", EXOSIP_CALL_RELEASED, CONST_CS | CONST_PERSISTENT);
    
    /* Message events */
    REGISTER_LONG_CONSTANT("EXOSIP_MESSAGE_NEW", EXOSIP_MESSAGE_NEW, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_MESSAGE_PROCEEDING", EXOSIP_MESSAGE_PROCEEDING, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_MESSAGE_ANSWERED", EXOSIP_MESSAGE_ANSWERED, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_MESSAGE_REDIRECTED", EXOSIP_MESSAGE_REDIRECTED, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_MESSAGE_REQUESTFAILURE", EXOSIP_MESSAGE_REQUESTFAILURE, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_MESSAGE_SERVERFAILURE", EXOSIP_MESSAGE_SERVERFAILURE, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_MESSAGE_GLOBALFAILURE", EXOSIP_MESSAGE_GLOBALFAILURE, CONST_CS | CONST_PERSISTENT);
    
    /* Subscription events */
    REGISTER_LONG_CONSTANT("EXOSIP_SUBSCRIPTION_NOANSWER", EXOSIP_SUBSCRIPTION_NOANSWER, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_SUBSCRIPTION_PROCEEDING", EXOSIP_SUBSCRIPTION_PROCEEDING, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_SUBSCRIPTION_ANSWERED", EXOSIP_SUBSCRIPTION_ANSWERED, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_SUBSCRIPTION_REDIRECTED", EXOSIP_SUBSCRIPTION_REDIRECTED, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_SUBSCRIPTION_REQUESTFAILURE", EXOSIP_SUBSCRIPTION_REQUESTFAILURE, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_SUBSCRIPTION_SERVERFAILURE", EXOSIP_SUBSCRIPTION_SERVERFAILURE, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_SUBSCRIPTION_GLOBALFAILURE", EXOSIP_SUBSCRIPTION_GLOBALFAILURE, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_SUBSCRIPTION_NOTIFY", EXOSIP_SUBSCRIPTION_NOTIFY, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_IN_SUBSCRIPTION_NEW", EXOSIP_IN_SUBSCRIPTION_NEW, CONST_CS | CONST_PERSISTENT);
    
    /* Notification events */
    REGISTER_LONG_CONSTANT("EXOSIP_NOTIFICATION_NOANSWER", EXOSIP_NOTIFICATION_NOANSWER, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_NOTIFICATION_PROCEEDING", EXOSIP_NOTIFICATION_PROCEEDING, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_NOTIFICATION_ANSWERED", EXOSIP_NOTIFICATION_ANSWERED, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_NOTIFICATION_REDIRECTED", EXOSIP_NOTIFICATION_REDIRECTED, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_NOTIFICATION_REQUESTFAILURE", EXOSIP_NOTIFICATION_REQUESTFAILURE, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_NOTIFICATION_SERVERFAILURE", EXOSIP_NOTIFICATION_SERVERFAILURE, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("EXOSIP_NOTIFICATION_GLOBALFAILURE", EXOSIP_NOTIFICATION_GLOBALFAILURE, CONST_CS | CONST_PERSISTENT);
    
    /* Legacy constants for backward compatibility */
    REGISTER_LONG_CONSTANT("SIP_EVENT_REGISTER", 1, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("SIP_EVENT_INVITE", 2, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("SIP_EVENT_ACK", 3, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("SIP_EVENT_BYE", 4, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("SIP_EVENT_CANCEL", 5, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("SIP_EVENT_MESSAGE", 6, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("SIP_EVENT_INFO", 7, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("SIP_EVENT_OPTIONS", 8, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("SIP_EVENT_SUBSCRIBE", 9, CONST_CS | CONST_PERSISTENT);
    REGISTER_LONG_CONSTANT("SIP_EVENT_NOTIFY", 10, CONST_CS | CONST_PERSISTENT);
    
    return SUCCESS;
}

/* ===== Module shutdown ===== */
PHP_MSHUTDOWN_FUNCTION(exosip) {
    // 清理所有实例
    while (g_instance_list) {
        php_exosip_instance_node *next = g_instance_list->next;
        free(g_instance_list);
        g_instance_list = next;
    }
    g_instance_count = 0;
    
    // 恢复默认信号处理
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = SIG_DFL;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGINT, &sa, NULL);
        sigaction(SIGTERM, &sa, NULL);
    }
    
    return SUCCESS;
}

zend_module_entry exosip_module_entry = {
    STANDARD_MODULE_HEADER,
    "exosip",
    NULL,  // 不注册全局函数，只提供 OOP API
    PHP_MINIT(exosip),
    PHP_MSHUTDOWN(exosip),
    NULL,
    NULL,
    NULL,
    NO_VERSION_YET,
    STANDARD_MODULE_PROPERTIES
};

ZEND_GET_MODULE(exosip)