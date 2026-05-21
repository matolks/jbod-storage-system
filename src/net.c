#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "net.h"
#include "jbod.h" 

// Stores TCP socket
int cli_sd = -1;

#define INFO_RET_FAIL  0x01 // JBOD operation status
#define INFO_HAS_BLOCK 0x02 // Block payload indicator

/* Extracts code
*  [ unused space ][ command ][ block ][ disk ]
*  [    31-20     ][  19-12  ][ 11-4  ][ 3-0  ]
*/
static jbod_cmd_t get_cmd(uint32_t op){
  return (jbod_cmd_t)((op >> 12) & 0xFF); // Shift right 12
}

// Reads exactly len bytes from file descriptor to buffer
bool nread(int fd, int len, uint8_t *buf){
  int total = 0; // Bytes read so far
  while(total < len){
    ssize_t n = read(fd, buf + total, len - total);
    if(n < 0){
      if(errno == EINTR){ // Interupted by signal, Not socket failure so retry
        continue;
      }
      return false;
    }
    if(n == 0){
      return false;
    }
    total += n;
  }
  return true;
}

// Writes exactly len bytyes from buffer to file descriptor
bool nwrite(int fd, int len, uint8_t *buf){
  int total = 0; // Bytes written so far
  while(total < len){
    ssize_t n = write(fd, buf + total, len - total);
    if(n < 0){
      if(errno == EINTR){ 
        continue;
      }
      return false;
    }
    if(n == 0){
      return false;
    }
    total += n;
  }
  return true;
}

// Recieves and parses one JBOD protocol packet
bool recv_packet(int fd, uint32_t *op, uint8_t *ret, uint8_t *block){
  uint8_t header[HEADER_LEN]; // Local buffer
  if(op == NULL || ret == NULL){
    return false;
  }
  if(!nread(fd, HEADER_LEN, header)){ // Read packet header from socket
    return false;
  }
  uint32_t net_op;
  memcpy(&net_op, header, sizeof(uint32_t));
  *op = ntohl(net_op); // Convert to host byte order from network byte order
  uint8_t info = header[4];
  *ret = (info & INFO_RET_FAIL) ? 1 : 0; // Store JBOD status
  if(info & INFO_HAS_BLOCK){
    if(block == NULL){ // Did not provide block buffer
      return false;
    }
    if(!nread(fd, JBOD_BLOCK_SIZE, block)){ // Read one block from socket
      return false;
    }
  }
  return true;
}

// Builds and sends on JBOD protocol packet
bool send_packet(int fd, uint32_t op, uint8_t *block){
  uint8_t packet[HEADER_LEN + JBOD_BLOCK_SIZE]; // Allocate enough local space
  memset(packet, 0, sizeof(packet)); // Clear packet
  uint32_t net_op = htonl(op); // Convert opcode to network byte order
  memcpy(packet, &net_op, sizeof(uint32_t));
  uint8_t info = 0; 
  int packet_len = HEADER_LEN;
  if(get_cmd(op) == JBOD_WRITE_BLOCK){ 
    if(block == NULL){
      return false;
    }
    info |= INFO_HAS_BLOCK; // Set payload indicator
    memcpy(packet + HEADER_LEN, block, JBOD_BLOCK_SIZE);
    packet_len += JBOD_BLOCK_SIZE;
  }
  packet[4] = info;
  return nwrite(fd, packet_len, packet); // Send bytes over the socket
}

// Connects the client to the JBOD server (TCP socket)
bool jbod_connect(const char *ip, uint16_t port){
  if(cli_sd >= 0){ // Mulitple connections
    return false;
  }
  cli_sd = socket(AF_INET, SOCK_STREAM, 0); // Create TCP IPv4 socket
  if(cli_sd < 0){
    return false;
  }
  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET; // IPv4
  server_addr.sin_port = htons(port); // Convert host byte order to network byte order
  if(inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0){ // Fail
    close(cli_sd); 
    cli_sd = -1;
    return false;
  }
  // Connect to JBOD server address
  if(connect(cli_sd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
    close(cli_sd);
    cli_sd = -1;
    return false;
  }
  return true;
}

// Disconnect from JBOD server
void jbod_disconnect(void){
  if(cli_sd >= 0){
    close(cli_sd);
    cli_sd = -1;
  }
}

// Sends one JBOD operation to the server
int jbod_client_operation(uint32_t op, uint8_t *block){
  if(cli_sd < 0){ // Not active
    return -1;
  }
  if(!send_packet(cli_sd, op, block)){ // Send packet
    return -1;
  }
  uint32_t response_op;
  uint8_t ret_code;
  if(!recv_packet(cli_sd, &response_op, &ret_code, block)){ // Recieve packet
    return -1;
  }
  return ret_code == 0 ? 0 : -1;
}