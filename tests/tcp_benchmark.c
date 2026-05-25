// Measure how the TCP backed storage performs under repeated read/write requests

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#include "net.h"
#include "mdadm.h"
#include "cache.h"
#include "jbod.h"

#define CACHE_SIZE 4096

static uint64_t now_ns(void){
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

static void fill_pattern(uint8_t *buf, size_t len, uint8_t seed){
  for(size_t i = 0; i < len; i++){
    buf[i] = (uint8_t)(seed + i);
  }
}

static uint32_t safe_addr_for_op(int i, int request_size){
  uint32_t total_size = JBOD_NUM_DISKS * JBOD_DISK_SIZE;
  uint32_t max_start = total_size - (uint32_t)request_size;
  return (uint32_t)(((uint64_t)i * (uint64_t)request_size) % max_start);
}

static int run_sequential_write(int request_size, int ops){
  uint8_t *buf = malloc((size_t)request_size);
  if (buf == NULL){
    return 1;
  }
  uint64_t start = now_ns();
  for(int i = 0; i < ops; i++){
    uint32_t addr = safe_addr_for_op(i, request_size);
    fill_pattern(buf, (size_t)request_size, (uint8_t)i);
    int rc = mdadm_write(addr, (uint32_t)request_size, buf);
    if (rc != request_size){
      fprintf(stderr, "sequential_write failed at op %d, addr=%u, rc=%d\n", i, addr, rc);
      free(buf);
      return 1;
    }
  }
  uint64_t end = now_ns();
  double seconds = (double)(end - start) / 1000000000.0;
  double total_mb = ((double)request_size * (double)ops) / 1000000.0;
  printf("tcp,sequential_write,%d,%d,%.6f,%.3f,%.3f\n",
         request_size, ops, seconds, total_mb / seconds, (double)ops / seconds);
  free(buf);
  return 0;
}

static int run_sequential_read(int request_size, int ops){
  uint8_t *buf = malloc((size_t)request_size);
  if (buf == NULL){
    return 1;
  }
  uint64_t start = now_ns();
  for(int i = 0; i < ops; i++){
    uint32_t addr = safe_addr_for_op(i, request_size);
    int rc = mdadm_read(addr, (uint32_t)request_size, buf);
    if (rc != request_size){
      fprintf(stderr, "sequential_read failed at op %d, addr=%u, rc=%d\n", i, addr, rc);
      free(buf);
      return 1;
    }
  }
  uint64_t end = now_ns();
  double seconds = (double)(end - start) / 1000000000.0;
  double total_mb = ((double)request_size * (double)ops) / 1000000.0;
  printf("tcp,sequential_read,%d,%d,%.6f,%.3f,%.3f\n",
         request_size, ops, seconds, total_mb / seconds, (double)ops / seconds);
  free(buf);
  return 0;
}

int main(int argc, char **argv){
  const char *ip = "127.0.0.1";
  uint16_t port = 3333;
  int ops = 10000;
  if (argc >= 2){
    ip = argv[1];
  }
  if (argc >= 3){
    port = (uint16_t)atoi(argv[2]);
  }
  if (argc >= 4){
    ops = atoi(argv[3]);
  }
  if (ops <= 0){
    fprintf(stderr, "ops must be positive\n");
    return 1;
  }
  int connect_rc = jbod_connect(ip, port);
  if (connect_rc < 0){
    fprintf(stderr, "could not connect to %s:%u, rc=%d\n", ip, port, connect_rc);
    return 1;
  }
  cache_create(CACHE_SIZE);
  if (mdadm_mount() != 1){
    fprintf(stderr, "mount failed\n");
    jbod_disconnect();
    return 1;
  }
  if (mdadm_write_permission() != 1){
    fprintf(stderr, "write permission failed\n");
    mdadm_unmount();
    jbod_disconnect();
    return 1;
  }
  printf("mode,workload,request_size,ops,time_sec,mbps,ops_sec\n");
  if (run_sequential_write(256, ops) != 0) return 1;
  if (run_sequential_read(256, ops) != 0) return 1;
  if (run_sequential_write(16, ops) != 0) return 1;
  if (run_sequential_read(16, ops) != 0) return 1;
  if (run_sequential_write(1024, ops) != 0) return 1;
  if (run_sequential_read(1024, ops) != 0) return 1;
  cache_print_hit_rate();
  mdadm_revoke_write_permission();
  mdadm_unmount();
  cache_destroy();
  jbod_disconnect();
  return 0;
}