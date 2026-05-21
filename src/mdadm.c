#include <stdint.h>
#include <string.h>

#include "mdadm.h"
#include "cache.h"
#include "net.h"

int is_mounted = 0; 
int write_permission = 0;

/* Builds JBOD op code
*  [ unused space ][ command ][ block ][ disk ]
*  [    31-20     ][  19-12  ][ 11-4  ][ 3-0  ]
*/
static uint32_t make_op(uint32_t disk, uint32_t block, uint32_t cmd){
    return (disk & 0xF) | ((block & 0xFF) << 4) | ((cmd & 0xFF) << 12);
} 

// Mounts JBOD system
int mdadm_mount(void){
    if(is_mounted){ 
        return -1;
    }
    uint32_t op = make_op(0, 0, JBOD_MOUNT); // 0s because not targeting anything
    if(jbod_client_operation(op, NULL) == 0){ // Send command to server
        is_mounted = 1; 
        return 1;
    }
    return -1;
}

// Unmounts JBOD system
int mdadm_unmount(void){
    if(!is_mounted){
        return -1;
    }
    uint32_t op = make_op(0, 0, JBOD_UNMOUNT); 
    if(jbod_client_operation(op, NULL) == 0){  
        is_mounted = 0;
        write_permission = 0;
        return 1;
    }
    return -1;
}

// Request permission to write
int mdadm_write_permission(void){
    if(write_permission){
        return -1;
    }
    uint32_t op = make_op(0, 0, JBOD_WRITE_PERMISSION); 
    if(jbod_client_operation(op, NULL) == 0){ 
        write_permission = 1;
        return 1;
    }
    return -1;
}

// Revoke write permission
int mdadm_revoke_write_permission(void){
    if(!write_permission){
        return -1;
    }
    uint32_t op = make_op(0, 0, JBOD_REVOKE_WRITE_PERMISSION);
    if(jbod_client_operation(op, NULL) == 0){ 
        write_permission = 0;
        return 1;
    }
    return -1;
}

// Reads from JBOD system into read_buff
int mdadm_read(uint32_t start_addr, uint32_t read_len, uint8_t *read_buf){
    uint32_t total_size = JBOD_NUM_DISKS * JBOD_DISK_SIZE;
    // Validate mount state, read size, buffer, and range
    if(!is_mounted){ 
        return -1;
    }
    if(read_len > 1024){ 
        return -1;
    }
    if(read_len > 0 && read_buf == NULL){ 
        return -1;
    }
    if(start_addr > total_size || read_len > total_size - start_addr){ 
        return -1;
    }
    uint32_t bytes_read = 0;
    uint32_t current_addr = start_addr;
    while(bytes_read < read_len){ // Read until requested number is reached
        uint32_t disk = current_addr / JBOD_DISK_SIZE;
        uint32_t block = (current_addr % JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE;
        uint32_t offset = current_addr % JBOD_BLOCK_SIZE;
        uint32_t bytes_left_in_block = JBOD_BLOCK_SIZE - offset;
        uint32_t bytes_left_to_read = read_len - bytes_read;
        uint32_t bytes_to_copy = bytes_left_in_block;
        if(bytes_to_copy > bytes_left_to_read){
            bytes_to_copy = bytes_left_to_read;
        }
        uint8_t temp_block[JBOD_BLOCK_SIZE];
        if(cache_enabled() && cache_lookup(disk, block, temp_block) == 1){
            // Cache hit. temp_block contains the requested block.
        } 
        else {
            uint32_t op = make_op(disk, 0, JBOD_SEEK_TO_DISK);
            if(jbod_client_operation(op, NULL) == -1){ // Disk seeking fail
                return -1;
            }
            op = make_op(0, block, JBOD_SEEK_TO_BLOCK);
            if(jbod_client_operation(op, NULL) == -1){ // Block seeking fail
                return -1;
            }
            op = make_op(0, 0, JBOD_READ_BLOCK);
            if(jbod_client_operation(op, temp_block) == -1){ // Block reading fail
                return -1;
            }
            if(cache_enabled()){ // Insert into cache
                cache_insert(disk, block, temp_block);
            }
        }
        memcpy(read_buf + bytes_read, temp_block + offset, bytes_to_copy);
        bytes_read += bytes_to_copy;
        current_addr += bytes_to_copy;
    }
    return bytes_read;
}

// Writes from the write_buff to the JBOD system
int mdadm_write(uint32_t start_addr, uint32_t write_len, const uint8_t *write_buf){
    uint32_t total_size = JBOD_NUM_DISKS * JBOD_DISK_SIZE;
    // Validate mount state, write size, buffer, permission and range
    if(!is_mounted){ 
        return -1;
    }
    if(write_len > 2048){ 
        return -1;
    }
    if(write_len > 0 && write_buf == NULL){ 
        return -1;
    }
    if(!write_permission){ 
        return -1;
    }
    if(start_addr > total_size || write_len > total_size - start_addr){ 
        return -1;
    }
    uint32_t bytes_written = 0;
    uint32_t current_addr = start_addr;
    while(bytes_written < write_len){ // Write until requested amount is reached
        uint32_t disk = current_addr / JBOD_DISK_SIZE;
        uint32_t block = (current_addr % JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE;
        uint32_t offset = current_addr % JBOD_BLOCK_SIZE;
        uint32_t bytes_left_in_block = JBOD_BLOCK_SIZE - offset;
        uint32_t bytes_left_to_write = write_len - bytes_written;
        uint32_t bytes_to_write = bytes_left_in_block;
        if(bytes_to_write > bytes_left_to_write){
            bytes_to_write = bytes_left_to_write;
        }
        uint8_t temp_block[JBOD_BLOCK_SIZE];
        if(cache_enabled() && cache_lookup(disk, block, temp_block) == 1){
            // Cache hit. temp_block contains the requested block.
        } 
        else {
            uint32_t op = make_op(disk, 0, JBOD_SEEK_TO_DISK); 
            if(jbod_client_operation(op, NULL) == -1){ // Seek to disk fail
                return -1;
            }
            op = make_op(0, block, JBOD_SEEK_TO_BLOCK);
            if(jbod_client_operation(op, NULL) == -1){ // Seek to block fail
                return -1;
            }
            op = make_op(0, 0, JBOD_READ_BLOCK);
            if(jbod_client_operation(op, temp_block) == -1){ // Read block fail
                return -1;
            }
        }
        memcpy(temp_block + offset, write_buf + bytes_written, bytes_to_write);
        if(cache_enabled()){
            cache_update(disk, block, temp_block); // Update block if present
            cache_insert(disk, block, temp_block); // Insert block if not present
        }
        uint32_t op = make_op(disk, 0, JBOD_SEEK_TO_DISK); // Seek to disk fail
        if(jbod_client_operation(op, NULL) == -1){
            return -1;
        }
        op = make_op(0, block, JBOD_SEEK_TO_BLOCK);
        if(jbod_client_operation(op, NULL) == -1){ // Seek to block fail
            return -1;
        }
        op = make_op(0, 0, JBOD_WRITE_BLOCK);
        if(jbod_client_operation(op, temp_block) == -1){ // Write to block fail
            return -1;
        }
        bytes_written += bytes_to_write;
        current_addr += bytes_to_write;
    }
    return bytes_written;
}