#ifndef MDADM_H_
#define MDADM_H_

#include <stdint.h>

// JBOD mount/unmount ops
int mdadm_mount(void);
int mdadm_unmount(void);

// JBOD permission ops
int mdadm_write_permission(void);
int mdadm_revoke_write_permission(void);

// JBOD read/write ops
int mdadm_read(uint32_t addr, uint32_t len, uint8_t *buf);
int mdadm_write(uint32_t addr, uint32_t len, const uint8_t *buf);

#endif  