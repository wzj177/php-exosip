#ifndef SERVERINFO_H
#define SERVERINFO_H

typedef struct {
    const char *ua;        // User-Agent
    const char *nonce;     
    const char *ip;        
    int port;              
    int rtpPort;           
    const char *sipId;     
    const char *sipRealm;  
    const char *sipPass;   
    int sipTimeout;         
    int sipExpiry;          
    const char *mode;      // 传输协议模式: "udp", "tcp", "all"
    int debug;             // 调试模式: 0=关闭, 1=开启
} ServerInfo;

#endif // SERVERINFO_H
