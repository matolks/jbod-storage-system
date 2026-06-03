# JBOD Storage System

A networked JBOD storage system written in C. The project implements a linear storage layer across multiple JBOD disks, with support for mount and unmount operations, bounded reads and writes, write permission control, block caching, and TCP communication with a remote JBOD server.

This repository focuses on the mdadm storage layer, block cache, networking implementation, custom trace files, and TCP tests. Driver files, binaries, and build artifacts are intentionally excluded.

## Overview

The system exposes multiple JBOD disks as one continuous linear address space. Read and write requests are translated into JBOD operations by calculating the target disk, block, and byte offset for each request.

JBOD operations are encoded into a 32-bit operation code:

```text
[ Unused space ][ Command ][ Block ][ Disk ]
[     31-20    ][  19-12  ][  11-4 ][ 3-0  ]
```

Each JBOD block stores 256 bytes. Because requests may begin in the middle of a block, the implementation handles partial block access, full block transfers, and requests that span multiple blocks or disks.

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
│   └── tcp_smoke_test.c        Basic TCP connection test
├── traces/
│   ├── linear-long-input       Linear input trace
│   ├── random-long-input       Random input trace
│   └── simple-long-input       Simple input trace
├── README.md                   Project documentation
└── .gitignore                  Excluded files and build artifacts
```

## Multiple Device Administration

`mdadm.c` / `mdadm.h`

The mdadm layer allows multiple JBOD disks to behave like one continuous storage device. It manages mount and unmount state, translates user read and write addresses into disk and block operations, enforces request limits, and controls write permissions.

| Function                                                                         | Purpose                                         |
| -------------------------------------------------------------------------------- | ----------------------------------------------- |
| `make_op(uint32_t disk, uint32_t block, uint32_t cmd)`                           | Builds a JBOD operation code                    |
| `mdadm_mount(void)`                                                              | Mounts the JBOD system                          |
| `mdadm_unmount(void)`                                                            | Unmounts the JBOD system                        |
| `mdadm_write_permission(void)`                                                   | Requests write permission                       |
| `mdadm_revoke_write_permission(void)`                                            | Revokes write permission                        |
| `mdadm_read(uint32_t start_addr, uint32_t read_len, uint8_t *read_buf)`          | Reads from the JBOD system into `read_buf`      |
| `mdadm_write(uint32_t start_addr, uint32_t write_len, const uint8_t *write_buf)` | Writes data from `write_buf` to the JBOD system |

## Cache

`cache.c` / `cache.h`

The cache stores recently accessed JBOD blocks using each block’s disk number and block number as the lookup key. This reduces unnecessary JBOD operations when data is already available in memory.

The cache manages creation, lookup, insertion, update, resizing, hit-rate tracking, and replacement when full. It uses a **Most Recently Used (MRU)** eviction policy, replacing the entry with the most recent access timestamp. The mdadm layer uses the cache through `mdadm_read` and `mdadm_write`.

| Function                                                        | Purpose                                                           |
| --------------------------------------------------------------- | ----------------------------------------------------------------- |
| `valid_disk_block(int disk_num, int block_num)`                 | Checks whether a disk and block pair is valid                     |
| `cache_create(int num_entries)`                                 | Allocates and initializes the cache                               |
| `cache_destroy(void)`                                           | Frees and resets the cache                                        |
| `cache_lookup(int disk_num, int block_num, uint8_t *buf)`       | Looks up a block in the cache                                     |
| `cache_insert(int disk_num, int block_num, const uint8_t *buf)` | Inserts a block into the cache                                    |
| `cache_update(int disk_num, int block_num, const uint8_t *buf)` | Updates a cached block if present                                 |
| `cache_enabled(void)`                                           | Checks whether the cache has been created and has a positive size |
| `cache_print_hit_rate(void)`                                    | Prints cache hits, queries, and hit rate                          |
| `cache_resize(int new_size)`                                    | Resizes the cache                                                 |

## Network Layer

`net.c` / `net.h`

The network layer allows the mdadm storage system to communicate with a remote JBOD server over TCP. It opens and closes the client socket connection, packages JBOD operations into protocol packets, sends packets to the server, receives responses, and returns the JBOD result to `mdadm.c`.

| Function                                                          | Purpose                                                            |
| ----------------------------------------------------------------- | ------------------------------------------------------------------ |
| `get_cmd(uint32_t op)`                                            | Extracts the command field from a JBOD operation code              |
| `nread(int fd, int len, uint8_t *buf)`                            | Reads exactly `len` bytes from a file descriptor into a buffer     |
| `nwrite(int fd, int len, uint8_t *buf)`                           | Writes exactly `len` bytes from a buffer to a file descriptor      |
| `recv_packet(int fd, uint32_t *op, uint8_t *ret, uint8_t *block)` | Receives and parses one JBOD protocol packet                       |
| `send_packet(int fd, uint32_t op, uint8_t *block)`                | Builds and sends one JBOD protocol packet                          |
| `jbod_connect(const char *ip, uint16_t port)`                     | Connects the client to the JBOD server over TCP                    |
| `jbod_disconnect(void)`                                           | Disconnects from the JBOD server                                   |
| `jbod_client_operation(uint32_t op, uint8_t *block)`              | Sends a JBOD operation to the remote server and returns the result |

## Trace Testing

The JBOD system was tested with a cache size of 4096 entries using three self-generated traces: **simple**, **linear**, and **random**.

### Simple Trace

The simple trace uses mostly 256-byte block-aligned operations to test basic block read and write behavior, cache hits and misses, and the replacement policy.

### Linear Trace

The linear trace uses contiguous byte ranges where each address continues from the previous operation. This tests `mdadm_read` and `mdadm_write` with arbitrary lengths, unaligned addresses, and crossings between block and disk boundaries.

### Random Trace

The random trace starts with a sequential initialization pass to initialize storage in block order, then switches to random reads and writes using arbitrary addresses and lengths. This tests realistic access behavior, cache eviction, unaligned reads and writes, and random disk, block, and offset calculations.

| Trace  |   Hits | Queries | Hit Rate |
| ------ | -----: | ------: | -------: |
| Simple |  1,777 |   2,441 |    72.8% |
| Linear |  3,604 |   4,817 |    74.8% |
| Random | 59,960 |  64,056 |    93.6% |

The traces have different lengths, so hit rates are more meaningful than raw hit counts.

## TCP Testing

The TCP path was tested with a smoke test and a sequential workload benchmark over localhost. The smoke test verified that the client could connect to the JBOD server, mount the JBOD system over TCP, unmount it, and disconnect cleanly.

```text
TCP smoke test
PASS: connected to 127.0.0.1:3333, rc=1
PASS: mount over TCP
PASS: unmount over TCP
PASS: disconnected
```

The benchmark was run against the JBOD server on `127.0.0.1:3333` with a cache size of 4096 entries. Each workload executed 10,000 operations through the TCP client path.

| Workload         | Request Size |    Ops |     Time |  Throughput | Ops/sec |
| ---------------- | -----------: | -----: | -------: | ----------: | ------: |
| Sequential write |        256 B | 10,000 | 11.844 s |  0.216 MB/s |     844 |
| Sequential read  |        256 B | 10,000 |  0.206 s | 12.407 MB/s |  48,466 |
| Sequential write |         16 B | 10,000 |  7.475 s |  0.021 MB/s |   1,338 |
| Sequential read  |         16 B | 10,000 |  0.041 s |  3.888 MB/s | 243,029 |
| Sequential write |       1024 B | 10,000 | 32.684 s |  0.313 MB/s |     306 |
| Sequential read  |       1024 B | 10,000 |  0.870 s | 11.769 MB/s |  11,493 |

| Cache Metric  |  Result |
| ------------- | ------: |
| Cache hits    | 115,905 |
| Cache queries | 120,000 |
| Hit rate      |   96.6% |

The 16-byte workloads measure small request overhead, the 256-byte workloads match the JBOD block size, and the 1024-byte workloads test requests that span multiple blocks.

The results show that the TCP path handles repeated storage operations across small, block-size, and multi-block requests. Write workloads are slower because each write must update data through the remote JBOD server. Larger writes improve total MB/s compared with 16-byte writes, but reduce operations per second because each request touches more JBOD blocks.

Read workloads are much faster because the cache absorbs most repeated block accesses after earlier operations populate it. The 96.6% hit rate means the read results primarily measure cached read performance through the mdadm layer, not uncached network round-trip cost. For that reason, write throughput is the better indicator of TCP update cost, while read throughput shows the benefit of the local block cache.
