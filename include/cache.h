#ifndef CACHE_H_
#define CACHE_H_
 
#include <stdbool.h>
#include <stdint.h>

#include "jbod.h"

// Cached JBOD block
typedef struct{
    bool valid;
    int disk_num;
    int block_num;
    uint8_t block[JBOD_BLOCK_SIZE];
    int clock_accesses;
} cache_entry_t;

// Lifecycle ops
int cache_create(int num_entries);
int cache_destroy(void);
int cache_resize(int new_size);

// Cache data ops
int cache_lookup(int disk_num, int block_num, uint8_t *buf);
int cache_insert(int disk_num, int block_num, const uint8_t *buf);
void cache_update(int disk_num, int block_num, const uint8_t *buf);

// Status/statistics ops
bool cache_enabled(void);
void cache_print_hit_rate(void);

#endif