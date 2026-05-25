# JBOD Storage System

A networked JBOD storage system written in C. This project implements a linear storage layer over multiple JBOD disks. It supports mount/unmount operations, bounded reads and writes, write permission control, block caching, and network communication with a remote JBOD server.

This repository focuses on the mdadm storage layer, block cache, networking implementation, and custom trace files used for testing. Driver files, binaries, and build artifacts are intentionally excluded.

## Overview

The system exposes multiple JBOD disks as one continuous linear address space. Read and write requests are translated into JBOD operations by calculating the target disk, block, and offset for each request.

JBOD operations are encoded into a 32-bit operation code.
```text
[ Unused space ][ Command ][ Block ][ Disk ]
[     31-20    ][  19-12  ][  11-4 ][ 3-0  ]
```
Each JBOD block stores 256 bytes. Reads and writes may begin in the middle of a block, so the implementation handles partial block access, full block transfers, and requests that span multiple blocks or disks.

## Repository Structure

```text
.
├── include/
│   ├── cache.h                 Cache interface
│   ├── jbod.h                  JBOD interface
│   ├── mdadm.h                 Multiple device administration interface
│   └── net.h                   Network interface
├── outputs/
│   └── sample-output.out       Sample output
├── src/
│   ├── cache.c                 Cache implementation
│   ├── mdadm.c                 Multiple device administration implementation
│   └── net.c                   Network implementation
├── tests/
│   ├── tcp_benchmark.c         TCP performance test
│   └── tcp_smoke_test.c        Basic TCP functions test
├── traces/
│   ├── linear-long-input       Linear input trace
│   ├── random-long-input       Random input trace
│   └── simple-long-input       Simple input trace
├── README.md                   Project documentation
└── .gitignore                  Excluded files and build artifacts
```

## Multiple Device Administration
`mdadm.c` / `mdadm.h`

The purpose of this file is to allow multiple JBOD disks to behave like one continuous linear address space. It manages mount/unmount state, translates user read/write addresses into disk and block operations, enforces request limits, and manages write permissions.

| Function | Purpose |
|---|---|
| `make_op(uint32_t disk, uint32_t block, uint32_t cmd)` | Builds a JBOD operation code |
| `mdadm_mount(void)` | Mounts JBOD system |
| `mdadm_unmount(void)` | Unmounts JBOD system |
| `mdadm_write_permission(void)` | Request permission to write |
| `mdadm_revoke_write_permission(void)` | Revoke write permission |
| `mdadm_read(uint32_t start_addr, uint32_t read_len, uint8_t *read_buf)` | Reads from the JBOD system into `read_buf` |
| `mdadm_write(uint32_t start_addr, uint32_t write_len, const uint8_t *write_buf)` | Writes from the `write_buf` to the JBOD system |

## Cache
`cache.c` / `cache.h`

The purpose of this file is to store recently accessed JBOD blocks using each block’s disk number and block number as the lookup key. This allows reads and writes to avoid unnecessary JBOD operations when cached data is available. It manages cache creation, lookup, insertion, update, resizing, hit-rate tracking, and replacement when the cache is full. The cache uses a **Most Recently Used (MRU)** eviction policy, replacing the entry with the most recent access timestamp. The cache is used by `mdadm.c` through `mdadm_read` and `mdadm_write`.

| Function | Purpose |
|---|---|
| `valid_disk_block(int disk_num, int block_num)` | Check if a disk/block pair is valid |
| `cache_create(int num_entries)` | Allocates and initializes cache |
| `cache_destroy(void)` | Frees and resets cache |
| `cache_lookup(int disk_num, int block_num, uint8_t *buf)` | Looks up block in cache |
| `cache_insert(int disk_num, int block_num, const uint8_t *buf)` | Inserts a block into cache |
| `cache_update(int disk_num, int block_num, const uint8_t *buf)` | Update a cached block if present |
| `cache_enabled(void)` | Checks whether the cache has been created and has a positive size |
| `cache_print_hit_rate(void)` | Tracks cache hits and queries |
| `cache_resize(int new_size)` | Resizes the cache |

## Network
`net.c` / `net.h`

This file implements the networking layer that lets the mdadm storage system communicate with a remote JBOD server. It opens and closes the client socket connection, packages JBOD operations into packets, sends those packets across the network, receives server responses, and returns the JBOD result back to `mdadm.c`. 

| Function | Purpose |
|---|---|
| `get_cmd(uint32_t op)` | Extracts command from a JBOD operation code |
| `nread(int fd, int len, uint8_t *buf)` | Reads exactly `len` bytes from file descriptor to buffer |
| `nwrite(int fd, int len, uint8_t *buf)` | Writes exactly `len` bytes from buffer to file descriptor |
| `recv_packet(int fd, uint32_t *op, uint8_t *ret, uint8_t *block)` | Receives and parses one JBOD protocol packet |
| `send_packet(int fd, uint32_t op, uint8_t *block)` | Builds and sends one JBOD protocol packet |
| `jbod_connect(const char *ip, uint16_t port)` | Connects the client to the JBOD server using a TCP socket |
| `jbod_disconnect(void)` | Disconnects from JBOD server |
| `jbod_client_operation(uint32_t op, uint8_t *block)` | Sends a JBOD operation to the remote server and returns the result |

## Testing

The JBOD system was tested with a cache size of 4096 entries using three self-generated traces: **simple**, **linear**, and **random**.

### Simple
Uses mostly 256-byte block-aligned operations to test basic block read/write behavior, cache hits/misses, and the replacement policy without much complexity.

### Linear
Uses contiguous byte ranges where each address continues from the previous operation. This tests `mdadm_read` and `mdadm_write` with arbitrary lengths, unaligned addresses, and crossings between block/disk boundaries.

### Random
Starts with a sequential initialization pass to initialize storage in block order, then switches to random reads/writes using arbitrary addresses and lengths. This tests realistic access scenarios, cache eviction behavior, unaligned reads/writes, and random disk/block/offset calculations.

| Trace | Hits | Queries | Hit Rate |
|---|---:|---:|---:|
| Simple | 1777 | 2441 | 72.8% |
| Linear | 3604 | 4817 | 74.8% |
| Random | 59960 | 64056 | 93.6% |

*Note: the traces are different lengths, so hit rates are more meaningful than raw hit counts.*

## TCP Testing

The TCP path was tested with a basic smoke test and a sequential workload benchmark over localhost. The smoke test verified that the client could connect to the JBOD server, mount the JBOD system over TCP, unmount it, and disconnect cleanly.

```text
TCP smoke test
PASS: connected to 127.0.0.1:3333, rc=1
PASS: mount over TCP
PASS: unmount over TCP
PASS: disconnected
```

The benchmark was run against the JBOD server on `127.0.0.1:3333` with a cache size of 4096 entries. Each workload executed 10000 operations through the TCP client path.

| Workload | Request Size | Ops | Time | Throughput | Ops/sec |
|---|---:|---:|---:|---:|---:|
| Sequential write | 256 B | 10000 | 11.844 s | 0.216 MB/s | 844.284 |
| Sequential read | 256 B | 10000 | 0.206 s | 12.407 MB/s | 48465.762 |
| Sequential write | 16 B | 10000 | 7.475 s | 0.021 MB/s | 1337.788 |
| Sequential read | 16 B | 10000 | 0.041 s | 3.888 MB/s | 243028.503 |
| Sequential write | 1024 B | 10000 | 32.684 s | 0.313 MB/s | 305.964 |
| Sequential read | 1024 B | 10000 | 0.870 s | 11.769 MB/s | 11492.951 |

| Cache Metric | Result |
|---|---:|
| Cache hits | 115905 |
| Cache queries | 120000 |
| Hit rate | 96.6% |

The 16-byte workloads measure small request overhead, the 256-byte workloads match the JBOD block size, and the 1024-byte workloads test requests that span multiple blocks.

These results show that the TCP path handles repeated storage operations successfully across small, block sized, and multi block requests. Write workloads are slower because each write must update data through the remote JBOD server. Larger writes improve total MB/s compared with 16-byte writes, but reduce operations per second because each request touches more JBOD blocks.

Read workloads are much faster because the cache absorbs most repeated block accesses after earlier operations populate it. The 96.6% hit rate means the read results primarily measure cached read performance through the mdadm layer, not uncached network round trip cost. For that reason, write throughput is the better indicator of TCP update cost, while read throughput shows the benefit of the local block cache.