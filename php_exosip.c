#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_exosip.h"
#include "exosip_wrapper.h"
#include "zend_exceptions.h"
#include "ext/standard/php_var.h"
#include "zend_smart_str.h"
#include <signal.h>
#include <fcntl.h>
#include <eXosip2/eXosip.h>
#include <osipparser2/sdp_message.h>
#include <osipparser2/osip_list.h>

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

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosip_sendinvite, 0, 0, 2)
    ZEND_ARG_TYPE_INFO(0, toUri, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, sdp, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, headers, IS_ARRAY, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosip_sendbye, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, callId, IS_LONG, 0)
    ZEND_ARG_TYPE_INFO(0, dialogId, IS_LONG, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosip_sendack, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, dialogId, IS_LONG, 0)
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

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosip_startlongtask, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, callback, IS_CALLABLE, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosip_getprocessstatus, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosip_getrunstatus, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, pid_file, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosip_parsesdp, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, sdp_body, IS_STRING, 0)
ZEND_END_ARG_INFO()

/* SUBSCRIBE/NOTIFY arginfo */
ZEND_BEGIN_ARG_INFO_EX(arginfo_exosip_subscribe, 0, 0, 2)
    ZEND_ARG_TYPE_INFO(0, toUri, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, eventType, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, expires, IS_LONG, 1)
    ZEND_ARG_TYPE_INFO(0, xmlBody, IS_STRING, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosip_refreshsubscribe, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, subscriptionId, IS_LONG, 0)
    ZEND_ARG_TYPE_INFO(0, expires, IS_LONG, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosip_cancelsubscribe, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, subscriptionId, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_exosip_sendnotifyresponse, 0, 0, 2)
    ZEND_ARG_TYPE_INFO(0, tid, IS_LONG, 0)
    ZEND_ARG_TYPE_INFO(0, code, IS_LONG, 0)
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

ZEND_BEGIN_ARG_INFO_EX(arginfo_sipevent_getsdp, 0, 0, 0)
ZEND_END_ARG_INFO()

/* SipSession class arginfo */
ZEND_BEGIN_ARG_INFO_EX(arginfo_sipsession_getid, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sipsession_getcallid, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sipsession_getdialogid, 0, 0, 0)
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

PHP_METHOD(SipSession, getDialogId) {
    php_sip_session_obj *obj = php_sip_session_from_obj(Z_OBJ_P(getThis()));
    if (obj->session_info) {
        RETURN_LONG(obj->session_info->dialog_id);
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
    PHP_ME(SipSession, getDialogId, arginfo_sipsession_getdialogid, ZEND_ACC_PUBLIC)
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
    int call_id;         // Call ID (cid)
    int dialog_id;       // Dialog ID (did)
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
    obj->call_id = 0;
    obj->dialog_id = 0;
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
    // fprintf(stderr, "[PHP-DEBUG] getBody called: obj=%p, body=%p\n", obj, obj->body);
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

PHP_METHOD(SipEvent, getCallId) {
    php_sip_event_obj *obj = php_sip_event_from_obj(Z_OBJ_P(getThis()));
    RETURN_LONG(obj->call_id);
}

PHP_METHOD(SipEvent, getDialogId) {
    php_sip_event_obj *obj = php_sip_event_from_obj(Z_OBJ_P(getThis()));
    RETURN_LONG(obj->dialog_id);
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

PHP_METHOD(SipEvent, getSdp) {
    php_sip_event_obj *obj = php_sip_event_from_obj(Z_OBJ_P(getThis()));
    
    // 如果 body 存在且 content_type 是 application/sdp，则解析
    if (!obj->body || !obj->content_type) {
        RETURN_NULL();
    }
    
    // 检查 Content-Type 是否是 SDP
    if (strstr(obj->content_type, "application/sdp") == NULL) {
        RETURN_NULL();
    }
    
    size_t body_len = strlen(obj->body);
    
    // GB28181 扩展字段存储
    char gb28181_y[128] = {0};  // y= SSRC
    char gb28181_f[256] = {0};  // f= f参数
    
    // GB28181 兼容性处理：提取并移除非标准字段 (y=, f=)
    // osip2 是严格的 RFC 4566 解析器，不支持私有扩展
    // 策略：逐行扫描，检测 y= 和 f= 行并跳过
    char *cleaned_sdp = (char*)emalloc(body_len + 1);
    const char *read_pos = obj->body;
    char *write_pos = cleaned_sdp;
    
    while (*read_pos) {
        const char *line_start = read_pos;
        
        // 找到行尾
        while (*read_pos && *read_pos != '\r' && *read_pos != '\n') {
            read_pos++;
        }
        
        size_t line_len = read_pos - line_start;
        int skip_line = 0;
        
        // 检查是否是 GB28181 私有字段（至少2字符：x=）
        if (line_len >= 2 && line_start[1] == '=') {
            if (line_start[0] == 'y') {
                // y=0100000001
                skip_line = 1;
                size_t value_len = line_len - 2;
                if (value_len > 0 && value_len < sizeof(gb28181_y)) {
                    memcpy(gb28181_y, line_start + 2, value_len);
                    gb28181_y[value_len] = '\0';
                }
            } else if (line_start[0] == 'f') {
                // f= 或 f=v/////a/1/2/3
                skip_line = 1;
                size_t value_len = line_len - 2;
                if (value_len > 0 && value_len < sizeof(gb28181_f)) {
                    memcpy(gb28181_f, line_start + 2, value_len);
                    gb28181_f[value_len] = '\0';
                }
            }
        }
        
        // 如果不跳过，复制整行到输出
        if (!skip_line && line_len > 0) {
            memcpy(write_pos, line_start, line_len);
            write_pos += line_len;
        }
        
        // 处理换行符（如果不跳过此行，也复制换行符）
        if (*read_pos == '\r') {
            if (!skip_line) {
                *write_pos++ = '\r';
            }
            read_pos++;
            if (*read_pos == '\n') {
                if (!skip_line) {
                    *write_pos++ = '\n';
                }
                read_pos++;
            }
        } else if (*read_pos == '\n') {
            if (!skip_line) {
                *write_pos++ = '\n';
            }
            read_pos++;
        }
    }
    
    *write_pos = '\0';
    
    // 使用 osip2 原生 SDP 解析器
    sdp_message_t *sdp = NULL;
    int ret = sdp_message_init(&sdp);
    
    if (ret != 0 || sdp == NULL) {
        efree(cleaned_sdp);
        RETURN_NULL();
    }
    
    // 解析清理后的 SDP 字符串
    ret = sdp_message_parse(sdp, cleaned_sdp);
    efree(cleaned_sdp);
    
    if (ret != 0) {
        sdp_message_free(sdp);
        RETURN_NULL();
    }
    
    // 创建返回数组（复用 parseSdp 的逻辑）
    array_init(return_value);
    
    // 提取 v= (version)
    char *version = sdp_message_v_version_get(sdp);
    if (version) {
        add_assoc_string(return_value, "version", version);
    }
    
    // 提取 o= (origin)
    char *o_username = sdp_message_o_username_get(sdp);
    char *o_sess_id = sdp_message_o_sess_id_get(sdp);
    char *o_sess_version = sdp_message_o_sess_version_get(sdp);
    char *o_nettype = sdp_message_o_nettype_get(sdp);
    char *o_addrtype = sdp_message_o_addrtype_get(sdp);
    char *o_addr = sdp_message_o_addr_get(sdp);
    
    if (o_username || o_sess_id || o_addr) {
        zval origin;
        array_init(&origin);
        if (o_username) add_assoc_string(&origin, "username", o_username);
        if (o_sess_id) add_assoc_string(&origin, "session_id", o_sess_id);
        if (o_sess_version) add_assoc_string(&origin, "session_version", o_sess_version);
        if (o_nettype) add_assoc_string(&origin, "nettype", o_nettype);
        if (o_addrtype) add_assoc_string(&origin, "addrtype", o_addrtype);
        if (o_addr) add_assoc_string(&origin, "addr", o_addr);
        add_assoc_zval(return_value, "origin", &origin);
    }
    
    // 提取 s= (session name)
    char *s_name = sdp_message_s_name_get(sdp);
    if (s_name) {
        add_assoc_string(return_value, "session_name", s_name);
    }
    
    // 提取 c= (connection)
    sdp_connection_t *conn = sdp_message_connection_get(sdp, 0, 0);
    if (conn && conn->c_addr) {
        zval connection;
        array_init(&connection);
        if (conn->c_nettype) add_assoc_string(&connection, "nettype", conn->c_nettype);
        if (conn->c_addrtype) add_assoc_string(&connection, "addrtype", conn->c_addrtype);
        if (conn->c_addr) add_assoc_string(&connection, "addr", conn->c_addr);
        add_assoc_zval(return_value, "connection", &connection);
    }
    
    // 提取 m= (medias)
    zval medias;
    array_init(&medias);
    
    int media_pos = 0;
    sdp_media_t *media = NULL;
    
    while ((media = (sdp_media_t*)osip_list_get(&sdp->m_medias, media_pos)) != NULL) {
        zval media_arr;
        array_init(&media_arr);
        
        if (media->m_media) add_assoc_string(&media_arr, "media", media->m_media);
        if (media->m_port) add_assoc_string(&media_arr, "port", media->m_port);
        if (media->m_proto) add_assoc_string(&media_arr, "proto", media->m_proto);
        
        // Payloads
        zval payloads;
        array_init(&payloads);
        int payload_pos = 0;
        char *payload = NULL;
        while ((payload = (char*)osip_list_get(&media->m_payloads, payload_pos)) != NULL) {
            add_next_index_string(&payloads, payload);
            payload_pos++;
        }
        add_assoc_zval(&media_arr, "payloads", &payloads);
        
        // Media connection
        sdp_connection_t *media_conn = (sdp_connection_t*)osip_list_get(&media->c_connections, 0);
        if (media_conn && media_conn->c_addr) {
            zval m_conn;
            array_init(&m_conn);
            if (media_conn->c_nettype) add_assoc_string(&m_conn, "nettype", media_conn->c_nettype);
            if (media_conn->c_addrtype) add_assoc_string(&m_conn, "addrtype", media_conn->c_addrtype);
            if (media_conn->c_addr) add_assoc_string(&m_conn, "addr", media_conn->c_addr);
            add_assoc_zval(&media_arr, "connection", &m_conn);
        }
        
        // Attributes
        zval attributes;
        array_init(&attributes);
        int attr_pos = 0;
        sdp_attribute_t *attr = NULL;
        while ((attr = (sdp_attribute_t*)osip_list_get(&media->a_attributes, attr_pos)) != NULL) {
            if (attr->a_att_field) {
                if (attr->a_att_value) {
                    add_assoc_string(&attributes, attr->a_att_field, attr->a_att_value);
                } else {
                    add_assoc_null(&attributes, attr->a_att_field);
                }
            }
            attr_pos++;
        }
        add_assoc_zval(&media_arr, "attributes", &attributes);
        
        add_next_index_zval(&medias, &media_arr);
        media_pos++;
    }
    
    add_assoc_zval(return_value, "medias", &medias);
    
    // 添加 GB28181 扩展字段
    if (gb28181_y[0] != '\0' || gb28181_f[0] != '\0') {
        zval gb28181;
        array_init(&gb28181);
        if (gb28181_y[0] != '\0') {
            add_assoc_string(&gb28181, "ssrc", gb28181_y);
        }
        if (gb28181_f[0] != '\0') {
            add_assoc_string(&gb28181, "f", gb28181_f);
        }
        add_assoc_zval(return_value, "gb28181", &gb28181);
    }
    
    // 清理
    sdp_message_free(sdp);
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
    PHP_ME(SipEvent, getCallId, arginfo_sipevent_gettid, ZEND_ACC_PUBLIC)
    PHP_ME(SipEvent, getDialogId, arginfo_sipevent_gettid, ZEND_ACC_PUBLIC)
    PHP_ME(SipEvent, getExpires, arginfo_sipevent_getexpires, ZEND_ACC_PUBLIC)
    PHP_ME(SipEvent, getSession, arginfo_sipevent_getsession, ZEND_ACC_PUBLIC)
    PHP_ME(SipEvent, getConnection, arginfo_sipevent_getconnection, ZEND_ACC_PUBLIC)
    PHP_ME(SipEvent, getHeader, arginfo_sipevent_getheader, ZEND_ACC_PUBLIC)
    PHP_ME(SipEvent, getSdp, arginfo_sipevent_getsdp, ZEND_ACC_PUBLIC)
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
    zval onWorkerStart;  // Worker 进程启动回调
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
static int php_exosip_parse_event_data(php_sip_event_obj *event_obj, zval *event_data, int debug);
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
        SAFE_ZVAL_DTOR(obj->onWorkerStart);
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
    if (strcmp(prop_name, "onWorkerStart") == 0) return &obj->onWorkerStart;
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
    if (strcmp(prop_name, "onWorkerStart") == 0) { ZVAL_COPY(&obj->onWorkerStart, value); return &obj->onWorkerStart; }
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
    ZVAL_UNDEF(&obj->onWorkerStart);  // Worker启动回调
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
        
        // 读取 long_task_worker_num 配置（默认1个）
        val = zend_hash_str_find(Z_ARRVAL_P(configArr), "long_task_worker_num", 20);
        int long_task_worker_num = (val && Z_TYPE_P(val) == IS_LONG) ? Z_LVAL_P(val) : 1;
        
        // 如果是多进程模式，延迟初始化（在 Worker 进程中初始化）
        if (task_worker_num > 0 || long_task_worker_num > 0) {
            // 只创建空的 SipContext，不绑定端口
            obj->ctx = (SipContext*)calloc(1, sizeof(SipContext));
            if (!obj->ctx) {
                php_error_docref(NULL, E_ERROR, "Failed to allocate SipContext");
                return;
            }
            
            // 保存配置，稍后在 Worker 中初始化
            obj->ctx->server_info = info;
            obj->ctx->task_count = task_worker_num;
            obj->ctx->long_task_count = long_task_worker_num;
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

    // 🔥 读取公网IP配置 (用于NAT穿透)
    val = zend_hash_str_find(Z_ARRVAL_P(configArr), "public_ip", 9);
    info.public_ip = (val && Z_TYPE_P(val) == IS_STRING) ? Z_STRVAL_P(val) : "";

    php_exosip_obj *obj = php_exosip_from_obj(Z_OBJ_P(getThis()));
    obj->ctx = exosip_init_wrapper(&info);
    if (!obj->ctx) {
        php_error_docref(NULL, E_ERROR, "Failed to init eXosip (check mode: %s, port: %d, ip: %s)", 
                         info.mode, info.port, info.ip);
        RETURN_FALSE;
    }
    
    // 读取 Worker/Task 配置
    val = zend_hash_str_find(Z_ARRVAL_P(configArr), "task_worker_num", 15);
    obj->ctx->task_count = (val && Z_TYPE_P(val) == IS_LONG) ? (int)Z_LVAL_P(val) : 4;
    
    val = zend_hash_str_find(Z_ARRVAL_P(configArr), "long_task_worker_num", 20);
    obj->ctx->long_task_count = (val && Z_TYPE_P(val) == IS_LONG) ? (int)Z_LVAL_P(val) : 1;  // 默认 1
    
    fprintf(stderr, "[DEBUG] init(): Set task_count=%d, long_task_count=%d\n", obj->ctx->task_count, obj->ctx->long_task_count);
    
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
            
            // 修复: 从 status_code 读取响应码,而不是 ss_status
            if ((val = zend_hash_str_find(Z_ARRVAL_P(event_data), "status_code", 11)) != NULL) {
                event_obj->event_code = Z_LVAL_P(val);
            }
            
            // 添加: 读取 call_id 和 dialog_id
            if ((val = zend_hash_str_find(Z_ARRVAL_P(event_data), "cid", 3)) != NULL) {
                event_obj->call_id = Z_LVAL_P(val);
            }
            
            if ((val = zend_hash_str_find(Z_ARRVAL_P(event_data), "did", 3)) != NULL) {
                event_obj->dialog_id = Z_LVAL_P(val);
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
            
            // 读取 content_type
            if ((val = zend_hash_str_find(Z_ARRVAL_P(event_data), "content_type", 12)) != NULL) {
                if (Z_TYPE_P(val) == IS_STRING) {
                    event_obj->content_type = estrndup(Z_STRVAL_P(val), Z_STRLEN_P(val));
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
    if (obj->ctx->task_count > 0 || obj->ctx->long_task_count > 0) {
        // 绑定 Task 回调到 SipContext（在 fork 前）
        if (!Z_ISUNDEF(obj->onTask)) {
            ZVAL_COPY(&obj->ctx->task_callback, &obj->onTask);
            if (obj->ctx->server_info.debug) {
                php_printf("[DEBUG] onTask callback set before fork\n");
            }
        } else if (obj->ctx->server_info.debug) {
            php_printf("[DEBUG] onTask callback is not set\n");
        }
        if (!Z_ISUNDEF(obj->onTaskFinish)) {
            ZVAL_COPY(&obj->ctx->task_finish_callback, &obj->onTaskFinish);
            if (obj->ctx->server_info.debug) {
                php_printf("[DEBUG] onTaskFinish callback set before fork\n");
            }
        } else if (obj->ctx->server_info.debug) {
            php_printf("[DEBUG] onTaskFinish callback is not set\n");
        }
        if (!Z_ISUNDEF(obj->onPipeMessage)) {
            ZVAL_COPY(&obj->ctx->pipe_message_callback, &obj->onPipeMessage);
            if (obj->ctx->server_info.debug) {
                php_printf("[DEBUG] onPipeMessage callback set before fork\n");
            }
        } else if (obj->ctx->server_info.debug) {
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
            
            // Worker 进程：初始化 eXosip（绑定端口）
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
            
            // 关键修复: 设置公网IP用于 Contact 和 Via 头
            // 优先级: public_ip 配置 > 自动检测 > 监听IP
            const char *masquerade_ip = NULL;
            
            if (obj->ctx->server_info.public_ip && strlen(obj->ctx->server_info.public_ip) > 0) {
                // 使用配置的公网IP
                masquerade_ip = obj->ctx->server_info.public_ip;
                php_printf("[Worker] Using configured public IP for Contact/Via headers: %s:%d\n",
                    masquerade_ip, obj->ctx->server_info.port);
            } else if (strcmp(obj->ctx->server_info.ip, "0.0.0.0") == 0) {
                // 自动检测本地IP
                static char local_ip[256] = {0};
                FILE *fp = popen("ifconfig | grep 'inet ' | grep -v '127.0.0.1' | awk '{print $2}' | head -1", "r");
                if (fp) {
                    if (fgets(local_ip, sizeof(local_ip), fp) != NULL) {
                        local_ip[strcspn(local_ip, "\n")] = 0; // 移除换行符
                        if (strlen(local_ip) > 0) {
                            masquerade_ip = local_ip;
                            php_printf("[Worker] Auto-detected local IP for Contact/Via headers: %s:%d\n",
                                masquerade_ip, obj->ctx->server_info.port);
                        }
                    }
                    pclose(fp);
                }
            } else {
                // 使用监听IP
                masquerade_ip = obj->ctx->server_info.ip;
                php_printf("[Worker] Using listen IP for Contact/Via headers: %s:%d\n",
                    masquerade_ip, obj->ctx->server_info.port);
            }
            
            // 设置 masquerade contact
            if (masquerade_ip != NULL) {
                eXosip_masquerade_contact(exosip_ctx, masquerade_ip, obj->ctx->server_info.port);
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
            
            // 触发 onWorkerStart 回调
            if (!Z_ISUNDEF(obj->onWorkerStart) && Z_TYPE(obj->onWorkerStart) == IS_OBJECT) {
                php_printf("[Worker] Calling onWorkerStart callback\n");
                
                zend_try {
                    zval result;
                    zval params[1];
                    ZVAL_OBJ(&params[0], Z_OBJ_P(getThis()));
                    Z_ADDREF(params[0]);
                    
                    if (call_user_function(EG(function_table), NULL, &obj->onWorkerStart, &result, 1, params) == SUCCESS) {
                        zval_ptr_dtor(&result);
                    }
                    
                    zval_ptr_dtor(&params[0]);
                } zend_catch {
                    php_error_docref(NULL, E_WARNING, "[Worker] onWorkerStart callback threw exception");
                } zend_end_try();
            }
            
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
                        
                        if (!php_exosip_parse_event_data(event_obj, event_data, obj->ctx->server_info.debug)) {
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
                            // 特殊处理 "error" 事件类型：传递字符串而不是 SipEvent
                            // 这是为了与 php_exosip_call_error_handler 保持一致
                            if (strcmp(event_type, "error") == 0) {
                                // 从事件中提取错误信息
                                char error_msg_buf[512];
                                const char *error_msg = "SIP Error";
                                
                                // 尝试从响应中获取状态码和原因短语
                                zval *status_code_val = zend_hash_str_find(Z_ARRVAL_P(event_data), "status_code", 11);
                                zval *reason_val = zend_hash_str_find(Z_ARRVAL_P(event_data), "reason_phrase", 13);
                                
                                if (status_code_val && Z_TYPE_P(status_code_val) == IS_LONG) {
                                    if (reason_val && Z_TYPE_P(reason_val) == IS_STRING) {
                                        snprintf(error_msg_buf, sizeof(error_msg_buf), 
                                                 "SIP Error %ld: %s", Z_LVAL_P(status_code_val), Z_STRVAL_P(reason_val));
                                    } else {
                                        snprintf(error_msg_buf, sizeof(error_msg_buf), 
                                                 "SIP Error %ld", Z_LVAL_P(status_code_val));
                                    }
                                    error_msg = error_msg_buf;
                                }
                                
                                // 调用错误处理器，传递字符串
                                php_exosip_call_error_handler(obj, error_msg);
                                
                                zval_ptr_dtor(&sip_event_obj);
                                continue;  // 跳过后续处理
                            }
                            
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
                                    zend_object *exception_obj = EG(exception);
                                    char error_buffer[1024];
                                    const char *error_msg = "Unknown error";
                                    
                                    if (exception_obj) {
                                        zend_class_entry *ce = exception_obj->ce;
                                        
                                        // 尝试获取异常消息
                                        zval rv;
                                        zval *message = zend_read_property(ce, exception_obj, "message", sizeof("message")-1, 0, &rv);
                                        
                                        if (message && Z_TYPE_P(message) == IS_STRING && Z_STRLEN_P(message) > 0) {
                                            snprintf(error_buffer, sizeof(error_buffer), "%s: %s", 
                                                    ZSTR_VAL(ce->name), Z_STRVAL_P(message));
                                            error_msg = error_buffer;
                                        } else {
                                            snprintf(error_buffer, sizeof(error_buffer), "%s", ZSTR_VAL(ce->name));
                                            error_msg = error_buffer;
                                        }
                                        
                                        // 如果有文件和行号信息,也包含进来
                                        zval *file = zend_read_property(ce, exception_obj, "file", sizeof("file")-1, 0, &rv);
                                        zval *line = zend_read_property(ce, exception_obj, "line", sizeof("line")-1, 0, &rv);
                                        
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
                
                // 每次循环都检查 Task/Long Task 消息（不依赖 SIP 事件）
                // Check Task results
                for (int i = 0; i < obj->ctx->task_count; i++) {
                    sip_handle_task_result(obj->ctx, obj->ctx->task_sockfds[i]);
                }
                
                // Check Long Task results (sendToWorker from Long Task)
                for (int i = 0; i < obj->ctx->long_task_count; i++) {
                    if (obj->ctx->long_task_sockfds[i] >= 0) {
                        sip_handle_task_result(obj->ctx, obj->ctx->long_task_sockfds[i]);
                    }
                }
            }
            
            php_printf("[Worker] Exiting event loop\n");
            
            // Worker 退出前从实例列表中移除（防止信号处理器访问野指针）
            php_exosip_unregister_instance(obj);
            
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
                if (!php_exosip_parse_event_data(event_obj, event_data, obj->ctx->server_info.debug)) {
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
                    // 特殊处理 "error" 事件类型：传递字符串而不是 SipEvent
                    // 这是为了与 php_exosip_call_error_handler 保持一致
                    if (strcmp(event_type, "error") == 0) {
                        // 从事件中提取错误信息
                        char error_msg_buf[512];
                        const char *error_msg = "SIP Error";
                        
                        // 尝试从响应中获取状态码和原因短语
                        zval *status_code_val = zend_hash_str_find(Z_ARRVAL_P(event_data), "status_code", 11);
                        zval *reason_val = zend_hash_str_find(Z_ARRVAL_P(event_data), "reason_phrase", 13);
                        
                        if (status_code_val && Z_TYPE_P(status_code_val) == IS_LONG) {
                            if (reason_val && Z_TYPE_P(reason_val) == IS_STRING) {
                                snprintf(error_msg_buf, sizeof(error_msg_buf), 
                                         "SIP Error %ld: %s", Z_LVAL_P(status_code_val), Z_STRVAL_P(reason_val));
                            } else {
                                snprintf(error_msg_buf, sizeof(error_msg_buf), 
                                         "SIP Error %ld", Z_LVAL_P(status_code_val));
                            }
                            error_msg = error_msg_buf;
                        }
                        
                        // 调用错误处理器，传递字符串
                        php_exosip_call_error_handler(obj, error_msg);
                        
                        zval_ptr_dtor(&sip_event_obj);
                        continue;  // 跳过后续处理
                    }
                    
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
static int php_exosip_parse_event_data(php_sip_event_obj *event_obj, zval *event_data, int debug) {
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
    
    // Parse call ID
    if ((val = zend_hash_str_find(ht, "cid", 3)) != NULL && Z_TYPE_P(val) == IS_LONG) {
        event_obj->call_id = Z_LVAL_P(val);
    }
    
    // Parse dialog ID
    if ((val = zend_hash_str_find(ht, "did", 3)) != NULL && Z_TYPE_P(val) == IS_LONG) {
        event_obj->dialog_id = Z_LVAL_P(val);
    }
    
    // Parse Expires header
    if ((val = zend_hash_str_find(ht, "expires", 7)) != NULL && Z_TYPE_P(val) == IS_LONG) {
        event_obj->expires = Z_LVAL_P(val);
    }
    
    // Parse response code
    if ((val = zend_hash_str_find(ht, "code", 4)) != NULL && Z_TYPE_P(val) == IS_LONG) {
        event_obj->response_code = Z_LVAL_P(val);
        event_obj->event_code = Z_LVAL_P(val);
    } else if ((val = zend_hash_str_find(ht, "status_code", 11)) != NULL && Z_TYPE_P(val) == IS_LONG) {
        event_obj->response_code = Z_LVAL_P(val);
        event_obj->event_code = Z_LVAL_P(val);
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
        if (debug) fprintf(stderr, "[PHP-DEBUG] body hydrated: %s\n", event_obj->body);
    } else {
        if (debug) fprintf(stderr, "[PHP-DEBUG] body NOT found in array (val=%p, type=%d)\n", val, val ? Z_TYPE_P(val) : -1);
    }
    
    // Parse content type
    if ((val = zend_hash_str_find(ht, "content_type", 12)) != NULL && Z_TYPE_P(val) == IS_STRING) {
        event_obj->content_type = estrndup(Z_STRVAL_P(val), Z_STRLEN_P(val));
        if (debug) fprintf(stderr, "[PHP-DEBUG] content_type hydrated: %s\n", event_obj->content_type);
    } else {
        if (debug) fprintf(stderr, "[PHP-DEBUG] content_type NOT found in array (val=%p, type=%d)\n", val, val ? Z_TYPE_P(val) : -1);
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
static volatile sig_atomic_t g_signal_received = 0;

static void php_exosip_signal_handler(int sig) {
    // 防止重复处理
    if (g_signal_received) {
        return;
    }
    g_signal_received = 1;
    
    // 安全遍历实例列表（可能在遍历时被修改）
    php_exosip_instance_node *node = g_instance_list;
    while (node) {
        // 保存 next 指针（防止 node 被释放）
        php_exosip_instance_node *next_node = node->next;
        
        if (!node->instance) {
            node = next_node;
            continue;
        }
        
        // 检查实例是否有效（防止野指针）
        php_exosip_obj *inst = node->instance;
        if (!inst) {
            node = next_node;
            continue;
        }
        
        inst->is_running = 0;
        
        if (!inst->ctx) {
            node = next_node;
            continue;
        }
        
        inst->ctx->running = 0;
        
        // 清理 Long Task 进程（仅 Worker 进程）
        if (inst->ctx->is_worker && 
            inst->ctx->long_task_count > 0 &&
            inst->ctx->long_task_pids != NULL) {
            
            if (inst->ctx->server_info.debug) {
                fprintf(stderr, "[Worker] Cleaning up %d Long Task(s) on signal %d\n", 
                           inst->ctx->long_task_count, sig);
            }
            
            for (int i = 0; i < inst->ctx->long_task_count; i++) {
                pid_t pid = inst->ctx->long_task_pids[i];
                if (pid > 0) {
                    if (inst->ctx->server_info.debug) {
                        fprintf(stderr, "[Worker] Sending SIGTERM to Long Task PID=%d\n", pid);
                    }
                    kill(pid, SIGTERM);
                }
            }
            
            // 等待所有 Long Task 子进程退出（避免僵尸进程）
            int wait_count = 0;
            while (wait_count < inst->ctx->long_task_count) {
                int status;
                pid_t pid = waitpid(-1, &status, WNOHANG);
                if (pid > 0) {
                    wait_count++;
                    if (inst->ctx->server_info.debug) {
                        fprintf(stderr, "[Worker] Long Task PID=%d exited\n", pid);
                    }
                } else {
                    break;  // 没有更多子进程
                }
            }
            
            // 清空数组
            for (int i = 0; i < inst->ctx->long_task_count; i++) {
                inst->ctx->long_task_pids[i] = 0;
            }
        }
        
        node = next_node;
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
    
    // result >= 0: transaction_id (成功)
    // result < 0: 失败
    if (result < 0) {
        RETURN_FALSE;
    }
    RETURN_LONG(result);  // 返回 transaction_id
}

/* ========== ExoSip::sendInvite(string $toUri, string $sdp, array $headers = []) ========== */
PHP_METHOD(ExoSip, sendInvite) {
    char *to_uri, *sdp;
    size_t to_uri_len, sdp_len;
    HashTable *headers = NULL;
    
    ZEND_PARSE_PARAMETERS_START(2, 3)
        Z_PARAM_STRING(to_uri, to_uri_len)
        Z_PARAM_STRING(sdp, sdp_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_ARRAY_HT(headers)
    ZEND_PARSE_PARAMETERS_END();
    
    php_exosip_obj *obj = php_exosip_from_obj(Z_OBJ_P(getThis()));
    if (!obj->ctx) {
        php_error_docref(NULL, E_WARNING, "eXosip not initialized");
        RETURN_FALSE;
    }
    
    // 从 headers 数组中提取 Subject
    char *subject = NULL;
    if (headers) {
        zval *subject_val = zend_hash_str_find(headers, "Subject", sizeof("Subject") - 1);
        if (subject_val && Z_TYPE_P(subject_val) == IS_STRING) {
            subject = Z_STRVAL_P(subject_val);
        }
    }
    
    // 调用底层实现
    int call_id = sip_send_invite(obj->ctx, to_uri, sdp, subject);
    
    if (call_id < 0) {
        RETURN_FALSE;
    }
    
    RETURN_LONG(call_id);
}

/* ========== ExoSip::sendBye(int $callId, int $dialogId = -1) ========== */
PHP_METHOD(ExoSip, sendBye) {
    zend_long call_id, dialog_id = -1;
    
    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_LONG(call_id)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(dialog_id)
    ZEND_PARSE_PARAMETERS_END();
    
    php_exosip_obj *obj = php_exosip_from_obj(Z_OBJ_P(getThis()));
    if (!obj->ctx) {
        php_error_docref(NULL, E_WARNING, "eXosip not initialized");
        RETURN_FALSE;
    }
    
    // 调用底层实现
    int result = sip_send_bye(obj->ctx, (int)call_id, (int)dialog_id);
    
    RETURN_BOOL(result == 0);
}

/* ========== ExoSip::sendAck(int $dialogId) ========== */
PHP_METHOD(ExoSip, sendAck) {
    zend_long dialog_id;
    
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_LONG(dialog_id)
    ZEND_PARSE_PARAMETERS_END();
    
    php_exosip_obj *obj = php_exosip_from_obj(Z_OBJ_P(getThis()));
    if (!obj->ctx) {
        php_error_docref(NULL, E_WARNING, "eXosip not initialized");
        RETURN_FALSE;
    }
    
    // 调用底层实现
    int result = sip_send_ack(obj->ctx, (int)dialog_id);
    
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

/* ========== ExoSip::subscribe(string $toUri, string $eventType, int $expires, string $xmlBody) ========== */
PHP_METHOD(ExoSip, subscribe) {
    char *to_uri, *event_type, *xml_body = NULL;
    size_t to_uri_len, event_type_len, xml_body_len = 0;
    zend_long expires = 3600;
    
    ZEND_PARSE_PARAMETERS_START(2, 4)
        Z_PARAM_STRING(to_uri, to_uri_len)
        Z_PARAM_STRING(event_type, event_type_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(expires)
        Z_PARAM_STRING(xml_body, xml_body_len)
    ZEND_PARSE_PARAMETERS_END();
    
    php_exosip_obj *obj = php_exosip_from_obj(Z_OBJ_P(getThis()));
    if (!obj->ctx) {
        php_error_docref(NULL, E_WARNING, "eXosip not initialized");
        RETURN_FALSE;
    }
    
    int subscription_id = sip_send_subscribe(obj->ctx, to_uri, event_type, (int)expires, xml_body);
    
    if (subscription_id < 0) {
        RETURN_FALSE;
    }
    
    RETURN_LONG(subscription_id);
}

/* ========== ExoSip::refreshSubscribe(int $subscriptionId, int $expires) ========== */
PHP_METHOD(ExoSip, refreshSubscribe) {
    zend_long subscription_id, expires = 3600;
    
    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_LONG(subscription_id)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(expires)
    ZEND_PARSE_PARAMETERS_END();
    
    php_exosip_obj *obj = php_exosip_from_obj(Z_OBJ_P(getThis()));
    if (!obj->ctx) {
        php_error_docref(NULL, E_WARNING, "eXosip not initialized");
        RETURN_FALSE;
    }
    
    int result = sip_refresh_subscribe(obj->ctx, (int)subscription_id, (int)expires);
    
    RETURN_BOOL(result == 0);
}

/* ========== ExoSip::cancelSubscribe(int $subscriptionId) ========== */
PHP_METHOD(ExoSip, cancelSubscribe) {
    zend_long subscription_id;
    
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_LONG(subscription_id)
    ZEND_PARSE_PARAMETERS_END();
    
    php_exosip_obj *obj = php_exosip_from_obj(Z_OBJ_P(getThis()));
    if (!obj->ctx) {
        php_error_docref(NULL, E_WARNING, "eXosip not initialized");
        RETURN_FALSE;
    }
    
    int result = sip_cancel_subscribe(obj->ctx, (int)subscription_id);
    
    RETURN_BOOL(result == 0);
}

/* ========== ExoSip::sendNotifyResponse(int $tid, int $code) ========== */
PHP_METHOD(ExoSip, sendNotifyResponse) {
    zend_long tid, code;
    
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_LONG(tid)
        Z_PARAM_LONG(code)
    ZEND_PARSE_PARAMETERS_END();
    
    php_exosip_obj *obj = php_exosip_from_obj(Z_OBJ_P(getThis()));
    if (!obj->ctx) {
        php_error_docref(NULL, E_WARNING, "eXosip not initialized");
        RETURN_FALSE;
    }
    
    int result = sip_send_notify_response(obj->ctx, (int)tid, (int)code);
    
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

/* ========== ExoSip::parseSdp() - 原生 SDP 解析（静态方法） ========== */
/**
 * 使用 osip2 原生 API 解析 SDP 
 * 生产级别实现，支持 eXosip2 5.1.2 (macOS) 和 5.3.0 (Linux)
 * 
 * @param string $sdp_body SDP 文本内容
 * @return array|null 解析后的 SDP 数组，失败返回 null
 * 
 * 返回数组结构：
 * [
 *   'version' => '0',
 *   'origin' => ['username' => ..., 'session_id' => ..., 'addr' => ...],
 *   'session_name' => 'Play',
 *   'connection' => ['nettype' => 'IN', 'addrtype' => 'IP4', 'addr' => '192.168.1.100'],
 *   'medias' => [
 *     ['media' => 'video', 'port' => '6000', 'proto' => 'RTP/AVP', 
 *      'payloads' => ['96', '98'], 'attributes' => [...]],
 *     ['media' => 'audio', 'port' => '6002', 'proto' => 'RTP/AVP', ...]
 *   ]
 * ]
 */
PHP_METHOD(ExoSip, parseSdp) {
    char *sdp_str;
    size_t sdp_len;
    
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(sdp_str, sdp_len)
    ZEND_PARSE_PARAMETERS_END();
    
    // 参数验证
    if (!sdp_str || sdp_len == 0) {
        RETURN_NULL();
    }
    
    // GB28181 扩展字段存储
    char gb28181_y[128] = {0};  // y= SSRC
    char gb28181_f[256] = {0};  // f= f参数
    
    // GB28181 兼容性处理：提取并移除非标准字段 (y=, f=)
    // osip2 是严格的 RFC 4566 解析器，不支持私有扩展
    // 
    // 策略：逐行扫描，检测 y= 和 f= 行并跳过
    char *cleaned_sdp = (char*)emalloc(sdp_len + 1);
    const char *read_pos = sdp_str;
    char *write_pos = cleaned_sdp;
    
    while (*read_pos) {
        const char *line_start = read_pos;
        
        // 找到行尾
        while (*read_pos && *read_pos != '\r' && *read_pos != '\n') {
            read_pos++;
        }
        
        size_t line_len = read_pos - line_start;
        int skip_line = 0;
        
        // 检查是否是 GB28181 私有字段（至少2字符：x=）
        if (line_len >= 2 && line_start[1] == '=') {
            if (line_start[0] == 'y') {
                // y=0100000001
                skip_line = 1;
                size_t value_len = line_len - 2;
                if (value_len > 0 && value_len < sizeof(gb28181_y)) {
                    memcpy(gb28181_y, line_start + 2, value_len);
                    gb28181_y[value_len] = '\0';
                }
            } else if (line_start[0] == 'f') {
                // f= 或 f=v/////a/1/2/3
                skip_line = 1;
                size_t value_len = line_len - 2;
                if (value_len > 0 && value_len < sizeof(gb28181_f)) {
                    memcpy(gb28181_f, line_start + 2, value_len);
                    gb28181_f[value_len] = '\0';
                }
            }
        }
        
        // 如果不跳过，复制整行到输出
        if (!skip_line && line_len > 0) {
            memcpy(write_pos, line_start, line_len);
            write_pos += line_len;
        }
        
        // 处理换行符（如果不跳过此行，也复制换行符）
        if (*read_pos == '\r') {
            if (!skip_line) {
                *write_pos++ = '\r';
            }
            read_pos++;
            if (*read_pos == '\n') {
                if (!skip_line) {
                    *write_pos++ = '\n';
                }
                read_pos++;
            }
        } else if (*read_pos == '\n') {
            if (!skip_line) {
                *write_pos++ = '\n';
            }
            read_pos++;
        }
    }
    
    *write_pos = '\0';
    
    // 使用 osip2 原生 SDP 解析器
    sdp_message_t *sdp = NULL;
    int ret = sdp_message_init(&sdp);
    
    if (ret != 0 || sdp == NULL) {
        efree(cleaned_sdp);
        php_error_docref(NULL, E_WARNING, "Failed to initialize SDP message structure");
        RETURN_NULL();
    }
    
    // 解析清理后的 SDP 字符串
    ret = sdp_message_parse(sdp, cleaned_sdp);
    efree(cleaned_sdp);
    
    if (ret != 0) {
        sdp_message_free(sdp);
        php_error_docref(NULL, E_WARNING, "Failed to parse SDP body (invalid format)");
        RETURN_NULL();
    }
    
    // 创建返回数组
    array_init(return_value);
    
    // 1. 提取 v= (version)
    char *version = sdp_message_v_version_get(sdp);
    if (version) {
        add_assoc_string(return_value, "version", version);
    }
    
    // 2. 提取 o= (origin)
    char *o_username = sdp_message_o_username_get(sdp);
    char *o_sess_id = sdp_message_o_sess_id_get(sdp);
    char *o_sess_version = sdp_message_o_sess_version_get(sdp);
    char *o_nettype = sdp_message_o_nettype_get(sdp);
    char *o_addrtype = sdp_message_o_addrtype_get(sdp);
    char *o_addr = sdp_message_o_addr_get(sdp);
    
    if (o_username || o_sess_id || o_addr) {
        zval origin;
        array_init(&origin);
        if (o_username) add_assoc_string(&origin, "username", o_username);
        if (o_sess_id) add_assoc_string(&origin, "session_id", o_sess_id);
        if (o_sess_version) add_assoc_string(&origin, "session_version", o_sess_version);
        if (o_nettype) add_assoc_string(&origin, "nettype", o_nettype);
        if (o_addrtype) add_assoc_string(&origin, "addrtype", o_addrtype);
        if (o_addr) add_assoc_string(&origin, "addr", o_addr);
        add_assoc_zval(return_value, "origin", &origin);
    }
    
    // 3. 提取 s= (session name)
    char *s_name = sdp_message_s_name_get(sdp);
    if (s_name) {
        add_assoc_string(return_value, "session_name", s_name);
    }
    
    // 4. 提取 c= (connection) - 会话级别
    sdp_connection_t *conn = sdp_message_connection_get(sdp, 0, 0);
    if (conn && conn->c_addr) {
        zval connection;
        array_init(&connection);
        if (conn->c_nettype) add_assoc_string(&connection, "nettype", conn->c_nettype);
        if (conn->c_addrtype) add_assoc_string(&connection, "addrtype", conn->c_addrtype);
        if (conn->c_addr) add_assoc_string(&connection, "addr", conn->c_addr);
        add_assoc_zval(return_value, "connection", &connection);
    }
    
    // 5. 提取 m= (medias) - 支持多个媒体流
    zval medias;
    array_init(&medias);
    
    int media_pos = 0;
    sdp_media_t *media = NULL;
    
    while ((media = (sdp_media_t*)osip_list_get(&sdp->m_medias, media_pos)) != NULL) {
        zval media_arr;
        array_init(&media_arr);
        
        // 媒体类型 (audio/video/application)
        if (media->m_media) {
            add_assoc_string(&media_arr, "media", media->m_media);
        }
        
        // 端口号
        if (media->m_port) {
            add_assoc_string(&media_arr, "port", media->m_port);
        }
        
        // 传输协议 (RTP/AVP, TCP/RTP/AVP, UDP/TLS/RTP/SAVP等)
        if (media->m_proto) {
            add_assoc_string(&media_arr, "proto", media->m_proto);
        }
        
        // Payload 类型列表 (96, 98, 97, 0, 8 等)
        zval payloads;
        array_init(&payloads);
        int payload_pos = 0;
        char *payload = NULL;
        while ((payload = (char*)osip_list_get(&media->m_payloads, payload_pos)) != NULL) {
            add_next_index_string(&payloads, payload);
            payload_pos++;
        }
        add_assoc_zval(&media_arr, "payloads", &payloads);
        
        // 媒体级别的连接信息
        sdp_connection_t *media_conn = (sdp_connection_t*)osip_list_get(&media->c_connections, 0);
        if (media_conn && media_conn->c_addr) {
            zval m_conn;
            array_init(&m_conn);
            if (media_conn->c_nettype) add_assoc_string(&m_conn, "nettype", media_conn->c_nettype);
            if (media_conn->c_addrtype) add_assoc_string(&m_conn, "addrtype", media_conn->c_addrtype);
            if (media_conn->c_addr) add_assoc_string(&m_conn, "addr", media_conn->c_addr);
            add_assoc_zval(&media_arr, "connection", &m_conn);
        }
        
        // a= 属性列表 (rtpmap, fmtp, sendonly, recvonly, setup等)
        zval attributes;
        array_init(&attributes);
        int attr_pos = 0;
        sdp_attribute_t *attr = NULL;
        while ((attr = (sdp_attribute_t*)osip_list_get(&media->a_attributes, attr_pos)) != NULL) {
            if (attr->a_att_field) {
                if (attr->a_att_value) {
                    // 有值的属性: a=rtpmap:96 PS/90000
                    add_assoc_string(&attributes, attr->a_att_field, attr->a_att_value);
                } else {
                    // 无值的属性: a=sendonly
                    add_assoc_null(&attributes, attr->a_att_field);
                }
            }
            attr_pos++;
        }
        add_assoc_zval(&media_arr, "attributes", &attributes);
        
        add_next_index_zval(&medias, &media_arr);
        media_pos++;
    }
    
    add_assoc_zval(return_value, "medias", &medias);
    
    // 6. 添加 GB28181 扩展字段 (如果存在)
    if (strlen(gb28181_y) > 0 || strlen(gb28181_f) > 0) {
        zval gb28181;
        array_init(&gb28181);
        
        if (strlen(gb28181_y) > 0) {
            add_assoc_string(&gb28181, "ssrc", gb28181_y);  // y= SSRC字段
        }
        if (strlen(gb28181_f) > 0) {
            add_assoc_string(&gb28181, "f", gb28181_f);     // f= f参数
        }
        
        add_assoc_zval(return_value, "gb28181", &gb28181);
    }
    
    // 清理
    sdp_message_free(sdp);
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
    
    // 允许 Task 和 Long Task 进程调用 sendToWorker
    if (!obj->ctx->is_task && !obj->ctx->is_long_task) {
        php_error_docref(NULL, E_WARNING, "sendToWorker can only be called from Task or Long Task process");
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

/* ========== ExoSip::startLongTask(callable $callback) ========== */
/**
 * 启动一个长期运行的Task进程
 * 将回调发送给预分配的 Long Task 进程（由 Master fork）
 * 只能在Worker进程的onWorkerStart回调中调用
 * 
 * @param callable $callback 回调函数,在Long Task进程中执行
 * @return bool 成功返回true,失败返回false
 */
PHP_METHOD(ExoSip, startLongTask) {
    zval *callback;
    
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ZVAL(callback)
    ZEND_PARSE_PARAMETERS_END();
    
    php_exosip_obj *obj = php_exosip_from_obj(Z_OBJ_P(getThis()));
    
    if (!obj->ctx) {
        php_error_docref(NULL, E_WARNING, "ExoSip not initialized");
        RETURN_FALSE;
    }
    
    if (!obj->ctx->is_worker) {
        php_error_docref(NULL, E_WARNING, "startLongTask can only be called from Worker process");
        RETURN_FALSE;
    }
    
    if (obj->ctx->server_info.debug) {
        fprintf(stderr, "[DEBUG] startLongTask: long_task_count=%d, long_task_pids=%p, long_task_sockfds=%p\n",
            obj->ctx->long_task_count, 
            (void*)obj->ctx->long_task_pids,
            (void*)obj->ctx->long_task_sockfds);
    }
    
    if (obj->ctx->long_task_count <= 0) {
        php_error_docref(NULL, E_WARNING, "No Long Task workers configured. Set 'long_task_worker_num' in init()");
        RETURN_FALSE;
    }
    
    if (!zend_is_callable(callback, 0, NULL)) {
        php_error_docref(NULL, E_WARNING, "Parameter must be a valid callback");
        RETURN_FALSE;
    }
    
    // 直接在此处 fork Long Task 子进程（利用 fork 的内存副本特性）
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1) {
        php_error_docref(NULL, E_WARNING, "Failed to create socketpair: %s", strerror(errno));
        RETURN_FALSE;
    }
    
    pid_t pid = fork();
    
    if (pid < 0) {
        close(sv[0]);
        close(sv[1]);
        php_error_docref(NULL, E_WARNING, "Failed to fork Long Task: %s", strerror(errno));
        RETURN_FALSE;
    }
    
    if (pid == 0) {
        // Long Task 子进程
        close(sv[0]);
        
        // 设置 Long Task 标志和 Worker 通信 socket
        obj->ctx->is_long_task = 1;
        obj->ctx->is_worker = 0;
        obj->ctx->is_task = 0;
        obj->ctx->worker_sockfd = sv[1];  // 保存通向 Worker 的 socket
        
        // 关闭所有继承的 fd（避免泄漏）
        if (obj->ctx->task_sockfds) {
            for (int j = 0; j < obj->ctx->task_count; j++) {
                if (obj->ctx->task_sockfds[j] >= 0) {
                    close(obj->ctx->task_sockfds[j]);
                }
            }
        }
        
        if (obj->ctx->long_task_sockfds) {
            for (int j = 0; j < obj->ctx->long_task_count; j++) {
                if (obj->ctx->long_task_sockfds[j] >= 0) {
                    close(obj->ctx->long_task_sockfds[j]);
                }
            }
        }
        
        // 不调用 eXosip_quit()，因为：
        // 1. Long Task 在 Worker 初始化 eXosip 之后 fork，继承了损坏的线程状态
        // 2. eXosip_quit() 会尝试清理不存在的线程，导致错误
        // 3. Long Task 不使用 SIP 功能，不需要 eXosip
        // 4. Long Task 是永久运行的进程，不依赖进程退出清理
        if (obj->ctx->ctx) {
            obj->ctx->ctx = NULL;  // 只需置空指针，防止意外使用
        }
        
        if (obj->ctx->server_info.debug) {
            fprintf(stderr, "[LongTask] Started PID=%d, can use sendToWorker()\n", getpid());
        }
        
        // 设置信号处理器（优雅退出）
        signal(SIGTERM, SIG_DFL);  // 默认处理（允许被 kill）
        signal(SIGINT, SIG_DFL);
        
        // 直接调用回调（fork 后 zval 是有效副本）
        zval retval;
        zval args[0];
        
        // 使用 zend_try 捕获异常
        zend_try {
            if (call_user_function(NULL, NULL, callback, &retval, 0, args) == SUCCESS) {
                fprintf(stderr, "[LongTask] Callback completed normally\n");
            } else {
                fprintf(stderr, "[LongTask] Callback execution failed\n");
            }
            zval_ptr_dtor(&retval);
        } zend_catch {
            fprintf(stderr, "[LongTask] Callback terminated by signal or exception\n");
        } zend_end_try();
        
        close(sv[1]);
        fprintf(stderr, "[LongTask] Exiting (PID=%d)\n", getpid());
        _exit(0);
    }
    
    // Worker 父进程
    close(sv[1]);
    
    // 设置 sv[0] 为非阻塞模式（重要！）
    int flags = fcntl(sv[0], F_GETFL, 0);
    if (flags != -1) {
        fcntl(sv[0], F_SETFL, flags | O_NONBLOCK);
    }
    
    // 找到空闲槽位并记录 PID
    int slot_id = -1;
    for (int i = 0; i < obj->ctx->long_task_count; i++) {
        if (obj->ctx->long_task_pids[i] == 0) {
            obj->ctx->long_task_pids[i] = pid;
            obj->ctx->long_task_sockfds[i] = sv[0];
            slot_id = i;
            break;
        }
    }
    
    if (slot_id == -1) {
        // 没有空槽位，关闭 socket
        close(sv[0]);
        php_error_docref(NULL, E_WARNING, "No free slot to track Long Task PID=%d", pid);
    }
    
    if (obj->ctx->server_info.debug) {
        fprintf(stderr, "[Worker] Started Long Task PID=%d (slot=%d)\n", pid, slot_id);
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
    PHP_ME(ExoSip, sendInvite, arginfo_exosip_sendinvite, ZEND_ACC_PUBLIC)
    PHP_ME(ExoSip, sendBye, arginfo_exosip_sendbye, ZEND_ACC_PUBLIC)
    PHP_ME(ExoSip, sendAck, arginfo_exosip_sendack, ZEND_ACC_PUBLIC)
    PHP_ME(ExoSip, sendResponse, arginfo_exosip_sendresponse, ZEND_ACC_PUBLIC)
    
    /* SUBSCRIBE/NOTIFY support */
    PHP_ME(ExoSip, subscribe, arginfo_exosip_subscribe, ZEND_ACC_PUBLIC)
    PHP_ME(ExoSip, refreshSubscribe, arginfo_exosip_refreshsubscribe, ZEND_ACC_PUBLIC)
    PHP_ME(ExoSip, cancelSubscribe, arginfo_exosip_cancelsubscribe, ZEND_ACC_PUBLIC)
    PHP_ME(ExoSip, sendNotifyResponse, arginfo_exosip_sendnotifyresponse, ZEND_ACC_PUBLIC)
    
    /* Configuration and statistics */
    PHP_ME(ExoSip, setConfig, arginfo_exosip_setconfig, ZEND_ACC_PUBLIC)
    PHP_ME(ExoSip, getConfig, arginfo_exosip_getconfig, ZEND_ACC_PUBLIC)
    PHP_ME(ExoSip, getStats, arginfo_exosip_getstats, ZEND_ACC_PUBLIC)
    
    /* SDP Parsing */
    PHP_ME(ExoSip, parseSdp, arginfo_exosip_parsesdp, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    
    /* Master-Worker-Task */
    PHP_ME(ExoSip, addTask, arginfo_exosip_addtask, ZEND_ACC_PUBLIC)
    PHP_ME(ExoSip, sendToWorker, arginfo_exosip_sendtoworker, ZEND_ACC_PUBLIC)
    PHP_ME(ExoSip, startLongTask, arginfo_exosip_startlongtask, ZEND_ACC_PUBLIC)
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