// Test basic TCP Connect, Disconnect, Mount, Unmount

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "net.h"
#include "mdadm.h"

int main(int argc, char **argv){
  const char *ip = "127.0.0.1";
  uint16_t port = 3333;
  if (argc >= 2){
    ip = argv[1];
  }
  if (argc >= 3){
    port = (uint16_t)atoi(argv[2]);
  }
  printf("TCP smoke test\n");
  int connect_rc = jbod_connect(ip, port);
  if (connect_rc < 0){
    printf("FAIL: could not connect to %s:%u, rc=%d\n", ip, port, connect_rc);
    return 1;
  }
  printf("PASS: connected to %s:%u, rc=%d\n", ip, port, connect_rc);
  if (mdadm_mount() != 1){
    printf("FAIL: mdadm_mount over TCP failed\n");
    jbod_disconnect();
    return 1;
  }
  printf("PASS: mount over TCP\n");
  if (mdadm_unmount() != 1){
    printf("FAIL: mdadm_unmount over TCP failed\n");
    jbod_disconnect();
    return 1;
  }
  printf("PASS: unmount over TCP\n");
  jbod_disconnect();
  printf("PASS: disconnected\n");
  return 0;
}