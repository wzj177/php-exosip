#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <eXosip2/eXosip.h>

int main(int argc, char *argv[]) {
    struct eXosip_t *ctx = NULL;  // eXosip 5.3.0 需要 malloc
    const char *server_host = "127.0.0.1";
    int server_port = 15060;
    int use_tcp = 0; // 默认使用 UDP

    if (argc > 1 && strcmp(argv[1], "tcp") == 0) {
        use_tcp = 1;
    }

    printf("=== eXosip2 SIP Client Test ===\n");
    printf("Target: %s:%d (%s)\n", server_host, server_port, use_tcp ? "TCP" : "UDP");
    printf("\n");

    // 分配 eXosip 上下文
    ctx = eXosip_malloc();
    if (!ctx) {
        fprintf(stderr, "[ERROR] eXosip_malloc failed\n");
        return -1;
    }

    // 初始化 eXosip
    if (eXosip_init(ctx) != OSIP_SUCCESS) {
        fprintf(stderr, "[ERROR] eXosip_init failed\n");
        free(ctx);
        return -1;
    }

    // 设置客户端监听（自动分配端口）
    int protocol = use_tcp ? IPPROTO_TCP : IPPROTO_UDP;
    if (eXosip_listen_addr(ctx, protocol, NULL, 0, AF_INET, 0) != OSIP_SUCCESS) {
        fprintf(stderr, "[ERROR] eXosip_listen_addr failed\n");
        eXosip_quit(ctx);
        free(ctx);
        return -1;
    }

    eXosip_set_user_agent(ctx, "SIP-Test-Client/1.0");

    // 构建 From URI (必须是 sip:user@host:port)
    char from_uri[256];
    snprintf(from_uri, sizeof(from_uri), "sip:34020000001320000001@127.0.0.1:5070");

    // 构建 Proxy URI (必须是 sip:host:port)
    char proxy_uri[256];
    snprintf(proxy_uri, sizeof(proxy_uri), "sip:%s:%d", server_host, server_port);

    printf("[REGISTER] Building REGISTER request...\n");
    printf("  From: %s\n", from_uri);
    printf("  Proxy: %s\n", proxy_uri);

    osip_message_t *reg = NULL;
    eXosip_lock(ctx);
    int rid = eXosip_register_build_initial_register(ctx, from_uri, proxy_uri, NULL, 3600, &reg);
    // if (reg == NULL) {
    //     eXosip_unlock(ctx);
    //     fprintf(stderr,"[REGISTER] Failed to build REGISTER request.\n");
    //     eXosip_register_remove(ctx, rid);
    //     eXosip_quit(ctx);
    //     free(ctx);
    //     return -1;
    // }

    eXosip_unlock(ctx);
    if (rid < 0 || !reg) {
        fprintf(stderr, "[ERROR] Failed to build REGISTER (rid=%d)\n", rid);
        eXosip_quit(ctx);
        free(ctx);
        return -1;
    }

    printf("[REGISTER] Built successfully (rid=%d)\n", rid);

    // 发送 REGISTER
    eXosip_lock(ctx);
    int send_ret = eXosip_register_send_register(ctx, rid, reg);
    eXosip_unlock(ctx);
    
    if (send_ret != OSIP_SUCCESS) {
        fprintf(stderr, "[ERROR] Failed to send REGISTER\n");
    } else {
        printf("[REGISTER] Sent, rid=%d\n", rid);
    }

    // 构建并发送 MESSAGE
    char to_uri[256];
    snprintf(to_uri, sizeof(to_uri), "sip:server@%s:%d", server_host, server_port);

    printf("\n[MESSAGE] Preparing SIP MESSAGE...\n");
    printf("  To: %s\n", to_uri);

    osip_message_t *message = NULL;
    eXosip_lock(ctx);
    int ret = eXosip_message_build_request(ctx, &message, "MESSAGE", to_uri, from_uri, NULL);
    eXosip_unlock(ctx);
    
    if (ret != OSIP_SUCCESS || !message) {
        fprintf(stderr, "[ERROR] eXosip_message_build_request failed\n");
        eXosip_quit(ctx);
        free(ctx);
        return -1;
    }

    // 设置消息体（osip API，不需要 eXosip_lock）
    const char *msg_body = "Hello from eXosip2 test client!";
    osip_message_set_content_type(message, "text/plain");
    osip_message_set_body(message, msg_body, strlen(msg_body));

    // 设置 Contact
    char contact[256];
    snprintf(contact, sizeof(contact), "<sip:client@127.0.0.1:5070>");
    osip_message_set_contact(message, contact);

    // 发送 MESSAGE
    eXosip_lock(ctx);
    int tid = eXosip_message_send_request(ctx, message);
    eXosip_unlock(ctx);
    
    if (tid < 0) {
        fprintf(stderr, "[ERROR] eXosip_message_send_request failed\n");
        eXosip_quit(ctx);
        free(ctx);
        return -1;
    }
    printf("[MESSAGE] Sent, tid=%d\n", tid);

    printf("[WAIT] Waiting for responses ...\n");

    // 事件循环
    int received_response = 0;
    for (int i = 0; i < 100; i++) {
        eXosip_event_t *evt = eXosip_event_wait(ctx, 0, 100); // 100ms timeout
        
        eXosip_lock(ctx);
        eXosip_automatic_action(ctx);
        eXosip_unlock(ctx);
        
        if (!evt) continue;

        printf("--- Received Event ---\n");
        printf("Type: %d, TID: %d\n", evt->type, evt->tid);

        if (evt->response) {
            printf("Response: %d %s\n",
                   evt->response->status_code,
                   evt->response->reason_phrase ? evt->response->reason_phrase : "");
            received_response = 1;
        }

        if (evt->request) {
            printf("Request Method: %s\n",
                   evt->request->sip_method ? evt->request->sip_method : "NULL");
        }

        eXosip_event_free(evt);

        if (received_response) break;
    }

    if (!received_response) {
        printf("[INFO] No response received (server may be down or silent)\n");
    } else {
        printf("[SUCCESS] Response received!\n");
    }

    // 清理
    eXosip_quit(ctx);
    free(ctx);

    return 0;
}



