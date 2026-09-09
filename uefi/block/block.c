#include "block/block.h"

#define BLOCK_MAX_DEVICES 8

static block_device_t *devices[BLOCK_MAX_DEVICES];
static uint32_t device_count;

static int string_equals(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        if (*left != *right) {
            return 0;
        }
        left++;
        right++;
    }
    return *left == *right;
}

void block_initialize(void)
{
    uint32_t index;
    device_count = 0;
    for (index = 0; index < BLOCK_MAX_DEVICES; index++) {
        devices[index] = NULL;
    }
}

int block_register(block_device_t *device)
{
    if (device == NULL ||
        device_count >= BLOCK_MAX_DEVICES ||
        device->name == NULL ||
        device->block_size == 0 ||
        device->block_count == 0 ||
        device->read == NULL ||
        device->write == NULL) {
        return 0;
    }
    devices[device_count++] = device;
    return 1;
}

block_device_t *block_get(const char *name)
{
    uint32_t index;
    if (name == NULL) {
        return NULL;
    }
    for (index = 0; index < device_count; index++) {
        if (devices[index] != NULL && string_equals(devices[index]->name, name)) {
            return devices[index];
        }
    }
    return NULL;
}

block_device_t *block_get_first(void)
{
    if (device_count == 0) {
        return NULL;
    }
    return devices[0];
}

block_device_t *block_pick_boot(void)
{
    static const char *boot_names[] = {"hd0", "sata0", "nvme0", NULL};
    uint32_t index;
    block_device_t *device;

    for (index = 0; boot_names[index] != NULL; index++) {
        device = block_get(boot_names[index]);
        if (device != NULL) {
            return device;
        }
    }
    return block_get_first();
}

int block_read(block_device_t *device, uint32_t lba, uint32_t count, void *buffer)
{
    if (device == NULL || buffer == NULL || count == 0 ||
        lba >= device->block_count || count > device->block_count - lba) {
        return 0;
    }
    return device->read(device, lba, count, buffer);
}

int block_write(block_device_t *device, uint32_t lba, uint32_t count, const void *buffer)
{
    if (device == NULL || buffer == NULL || count == 0 ||
        lba >= device->block_count || count > device->block_count - lba) {
        return 0;
    }
    return device->write(device, lba, count, buffer);
}
