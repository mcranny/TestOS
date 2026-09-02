#ifndef BLOCK_H
#define BLOCK_H

#include "types.h"

typedef struct block_device
{
    const char *name;
    uint32_t block_size;
    uint32_t block_count;
    int (*read)(
        struct block_device *device,
        uint32_t lba,
        uint32_t count,
        void *buffer
    );
    int (*write)(
        struct block_device *device,
        uint32_t lba,
        uint32_t count,
        const void *buffer
    );
    void *priv;
} block_device_t;

void block_initialize(void);
int block_register(block_device_t *device);
block_device_t *block_get(const char *name);
int block_read(
    block_device_t *device,
    uint32_t lba,
    uint32_t count,
    void *buffer
);
int block_write(
    block_device_t *device,
    uint32_t lba,
    uint32_t count,
    const void *buffer
);

#endif
