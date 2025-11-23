#ifndef CLIENT_H
#define CLIENT_H

typedef struct {
    const char *device;   // 设备ID
    const char *ip;       
    int port;             
    int rtpPort;          
    int isRegistered;     
} ClientInfo;

#endif // CLIENT_H
