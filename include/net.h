#ifndef NET_H_
#define NET_H_

#include <stdint.h>
#include <stdbool.h> 

#define HEADER_LEN (sizeof(uint32_t) + sizeof(uint8_t)) // JBOD protocol header
// JBOD server address and port used for testing
#define JBOD_SERVER "127.0.0.1" 
#define JBOD_PORT 3333

// Socket read/write ops
bool nread(int fd, int len, uint8_t *buf);
bool nwrite(int fd, int len, uint8_t *buf);

// JBOD packet send/receive ops
bool recv_packet(int fd, uint32_t *op, uint8_t *ret, uint8_t *block);
bool send_packet(int fd, uint32_t op, uint8_t *block);

// JBOD network ops
int jbod_client_operation(uint32_t op, uint8_t *block);

// JBOD server lifecycle ops
bool jbod_connect(const char *ip, uint16_t port);
void jbod_disconnect(void);

#endif 