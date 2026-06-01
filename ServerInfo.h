#ifndef SERVERINFO_H
#define SERVERINFO_H

typedef struct {
    const char *ua;        // User-Agent
    const char *nonce;     
    const char *ip;        // 监听IP (可以是 0.0.0.0)
    int port;              
    int rtpPort;           
    const char *sipId;     
    const char *sipRealm;  
    const char *sipPass;   
    int sipTimeout;         
    int sipExpiry;          
    const char *mode;      // 传输协议模式: "udp", "tcp"
    int debug;             // 调试模式: 0=关闭, 1=开启
    const char *public_ip; // 公网IP (用于NAT穿透,设置Contact/Via头)
} ServerInfo;

#endif // SERVERINFO_H
