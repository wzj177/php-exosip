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
    
    printf("=== eXosip2 TCP Test ===\n");
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
    
    eXosip_set_user_agent(ctx, "Test-TCP-Server/1.0");
    
    // 启用TCP端口复用（参考GB28181-Service）
    int enable_reuse = 1;
    eXosip_set_option(ctx, EXOSIP_OPT_ENABLE_REUSE_TCP_PORT, (void*)&enable_reuse);
    
    // 监听 TCP
    printf("[INIT] Listening on TCP port %d...\n", port);
    if (eXosip_listen_addr(ctx, IPPROTO_TCP, NULL, port, AF_INET, 0) != 0) {
        fprintf(stderr, "[ERROR] eXosip_listen_addr failed\n");
        eXosip_quit(ctx);
        return -1;
    }
    
    printf("[OK] TCP server started on 0.0.0.0:%d\n", port);
    printf("[INFO] Press Ctrl+C to stop\n\n");
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 事件循环 - 增强调试
    int loop_count = 0;
    while (running) {
        loop_count++;
        
        // 先执行automatic_action（TCP连接维护）
        eXosip_lock(ctx);
        eXosip_automatic_action(ctx);
        eXosip_unlock(ctx);
        
        // 再获取事件
        eXosip_event_t *evt = eXosip_event_wait(ctx, 0, 20);
        
        if (loop_count % 500 == 0) {
            printf("[ALIVE] Loop: %d, Running: %d\n", loop_count, running);
        }
        
        if (evt == NULL) {
            continue;
        }
        
        // 事件已获取，再次处理
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
