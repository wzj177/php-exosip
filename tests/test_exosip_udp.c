#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <eXosip2/eXosip.h>

static int running = 1;

void signal_handler(int sig) {
    printf("\n[SIGNAL] Caught signal %d, stopping...\n", sig);
    running = 0;
}

int main(int argc, char *argv[]) {
    struct eXosip_t *ctx = NULL;
    int port = 15060;
    int event_count = 0;
    
    printf("=== eXosip2 UDP Test ===\n");
    printf("Based on GB28181-Service pattern\n\n");
    
    // 初始化
    ctx = eXosip_malloc();
    if (!ctx) {
        fprintf(stderr, "[ERROR] eXosip_malloc failed\n");
        return -1;
    }
    
    if (eXosip_init(ctx) != 0) {
        fprintf(stderr, "[ERROR] eXosip_init failed\n");
        eXosip_quit(ctx);
        return -1;
    }
    
    eXosip_set_user_agent(ctx, "Test-UDP-Server/1.0");
    
    // 监听 UDP
    printf("[INIT] Listening on UDP port %d...\n", port);
    if (eXosip_listen_addr(ctx, IPPROTO_UDP, NULL, port, AF_INET, 0) != 0) {
        fprintf(stderr, "[ERROR] eXosip_listen_addr failed\n");
        eXosip_quit(ctx);
        return -1;
    }
    
    printf("[OK] UDP server started on 0.0.0.0:%d\n", port);
    printf("[INFO] Press Ctrl+C to stop\n\n");
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 事件循环 - GB28181-Service 模式
    while (running) {
        eXosip_event_t *evt = eXosip_event_wait(ctx, 0, 20);
        
        if (evt == NULL) {
            // 无事件时继续
            continue;
        }
        
        // 处理自动操作（401/407认证、重定向等）
        eXosip_lock(ctx);
        eXosip_automatic_action(ctx);
        eXosip_unlock(ctx);
        
        // 验证事件类型
        if (evt->type < EXOSIP_REGISTRATION_SUCCESS || 
            evt->type > EXOSIP_NOTIFICATION_GLOBALFAILURE) {
            eXosip_event_free(evt);
            continue;
        }
        
        event_count++;
        printf("\n=== Event #%d ===\n", event_count);
        printf("Type: %d\n", evt->type);
        
        if (evt->request) {
            printf("Method: %s\n", evt->request->sip_method ? evt->request->sip_method : "NULL");
            
            // 打印 From
            if (evt->request->from && evt->request->from->url) {
                printf("From: ");
                if (evt->request->from->url->username)
                    printf("%s", evt->request->from->url->username);
                if (evt->request->from->url->host)
                    printf("@%s", evt->request->from->url->host);
                printf("\n");
            }
        }
        
        // 处理 MESSAGE 请求
        if (evt->type == EXOSIP_MESSAGE_NEW) {
            printf("[ACTION] Handling MESSAGE\n");
            
            eXosip_lock(ctx);
            osip_message_t *response = NULL;
            if (eXosip_message_build_answer(ctx, evt->tid, 200, &response) == 0 && response) {
                eXosip_message_send_answer(ctx, evt->tid, 200, response);
                printf("[RESPONSE] Sent 200 OK\n");
            }
            eXosip_unlock(ctx);
        }
        
        eXosip_event_free(evt);
    }
    
    printf("\n[CLEANUP] Shutting down...\n");
    printf("[STATS] Total events: %d\n", event_count);
    eXosip_quit(ctx);
    printf("[OK] Done\n");
    
    return 0;
}
