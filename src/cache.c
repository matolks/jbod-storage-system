#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "cache.h"
 
// Global cache state
static cache_entry_t *cache = NULL; // Cache entries
static int cache_size = 0; // # of entries currently allocated
static int clock = 0; // Track each cache entry access (for LRU policy)
static int num_queries = 0; // Cache lookup attempts
static int num_hits = 0; // Successful cache lookups


// Checks whether a disk number or block number are valid
static int valid_disk_block(int disk_num, int block_num){
    if(disk_num < 0 || disk_num >= JBOD_NUM_DISKS){ // Disk number below 0 or past last JBOD disk
        return 0;
    }
    if(block_num < 0 || block_num >= JBOD_NUM_BLOCKS_PER_DISK){ // Block number below 0 or past last block
        return 0;
    }
    return 1;
}

// Allocated and intializes cache
int cache_create(int num_entries){
    if(cache != NULL){ // Cache already exsists
        return -1;
    }
    if(num_entries < 2 || num_entries > 4096){ // Cache range
        return -1;
    }
    cache = malloc(num_entries * sizeof(cache_entry_t));
    if(cache == NULL){
        return -1;
    }
    cache_size = num_entries;
    clock = 0;
    num_queries = 0;
    num_hits = 0;
    for(int i = 0; i < cache_size; i++){ // Initialize cache
        cache[i].valid = false;
        cache[i].disk_num = -1;
        cache[i].block_num = -1;
        cache[i].clock_accesses = 0;
        memset(cache[i].block, 0, JBOD_BLOCK_SIZE); // Clear buffer
    }
    return 1;
}

// Frees and resets cache
int cache_destroy(void){
    if(cache == NULL){
        return -1;
    }
    free(cache);
    cache = NULL;
    cache_size = 0;
    clock = 0;
    return 1;
}

// Looks up block in cache
int cache_lookup(int disk_num, int block_num, uint8_t *buf){
    if(!cache_enabled() || buf == NULL){ // Cache not enabled or buffer is null
        return -1;
    }
    if(!valid_disk_block(disk_num, block_num)){ // Invalid disk/block
        return -1;
    }
    num_queries++;
    for(int i = 0; i < cache_size; i++){
        if(cache[i].valid &&
            cache[i].disk_num == disk_num &&
            cache[i].block_num == block_num){
            memcpy(buf, cache[i].block, JBOD_BLOCK_SIZE); // Copy cache into buffer
            cache[i].clock_accesses = ++clock; // Update Access
            num_hits++; // Hit
            return 1;
        }
    }
    return -1;
}

// Insert block into cache
int cache_insert(int disk_num, int block_num, const uint8_t *buf){
    if(!cache_enabled() || buf == NULL){ 
        return -1;
    }
    if(!valid_disk_block(disk_num, block_num)){ 
        return -1;
    }
    for(int i = 0; i < cache_size; i++){ // Check if already cached
        if(cache[i].valid &&
            cache[i].disk_num == disk_num &&
            cache[i].block_num == block_num){
            memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE); // Copy buffer into cache
            cache[i].clock_accesses = ++clock;
            return 1;
        }
    }
    for(int i = 0; i < cache_size; i++){ // Use first unused cache slot
        if(!cache[i].valid){
            cache[i].valid = true;
            cache[i].disk_num = disk_num;
            cache[i].block_num = block_num;
            memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE); // Copy buffer into cache
            cache[i].clock_accesses = ++clock;
            return 1;
        }
    }
    int victim = 0;
    for(int i = 1; i < cache_size; i++){ // Cache full - MRU Policy
        if(cache[i].clock_accesses > cache[victim].clock_accesses){
            victim = i;
        }
    }
    cache[victim].valid = true;
    cache[victim].disk_num = disk_num;
    cache[victim].block_num = block_num;
    memcpy(cache[victim].block, buf, JBOD_BLOCK_SIZE);  // Copy buffer into victim
    cache[victim].clock_accesses = ++clock;
    return 1;
}

// Update a cache entry if present
void cache_update(int disk_num, int block_num, const uint8_t *buf){
    if(!cache_enabled() || buf == NULL){ 
        return;
    }
    if(!valid_disk_block(disk_num, block_num)){
        return;
    }
    for(int i = 0; i < cache_size; i++){
        if(cache[i].valid &&
            cache[i].disk_num == disk_num &&
            cache[i].block_num == block_num){
            memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE); 
            cache[i].clock_accesses = ++clock;
            return;
        }
    }
}

// Check if cache has been created and has a positive size
bool cache_enabled(void){
    return cache != NULL && cache_size > 0;
}

// For testing
void cache_print_hit_rate(void){
    fprintf(stderr, "num_hits: %d, num_queries: %d\n", num_hits, num_queries);
    if(num_queries == 0){
        fprintf(stderr, "Hit rate: -nan%%\n");
    }
    else {
        fprintf(stderr, "Hit rate: %5.1f%%\n",
                100.0f * (float)num_hits / (float)num_queries);
    }
}

// Resizes the cache
int cache_resize(int new_size){
    if(cache == NULL){
        return -1;
    }
    if(new_size < 2 || new_size > 4096){ // Checks for valid range
        return -1;
    }
    cache_entry_t *new_cache = malloc(new_size * sizeof(cache_entry_t));
    if(new_cache == NULL){
        return -1;
    }
    for(int i = 0; i < new_size; i++){ // Initalize new cache
        new_cache[i].valid = false;
        new_cache[i].disk_num = -1;
        new_cache[i].block_num = -1;
        new_cache[i].clock_accesses = 0;
        memset(new_cache[i].block, 0, JBOD_BLOCK_SIZE);
    }
    int copied = 0;
    while(copied < new_size){
        int candidate = -1;
        for(int i = 0; i < cache_size; i++){
            if(!cache[i].valid){ // Ignore invalid old entries
                continue;
            }
            int already_copied = 0;
            for(int j = 0; j < copied; j++){
                if(new_cache[j].valid &&
                    new_cache[j].disk_num == cache[i].disk_num &&
                    new_cache[j].block_num == cache[i].block_num){
                    already_copied = 1;
                    break; // If block is already copied skip
                }
            }
            if(already_copied){ // Skip dupes
                continue; 
            }
            if(candidate == -1 ||
                cache[i].clock_accesses < cache[candidate].clock_accesses){
                candidate = i;
            }
        }
        if(candidate == -1){ // No more old entries
            break;
        }
        new_cache[copied] = cache[candidate];
        copied++;
    }
    free(cache);
    cache = new_cache;
    cache_size = new_size;
    return 1;
}