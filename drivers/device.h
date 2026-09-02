#ifndef DEVICE_H
#define DEVICE_H

#include "types.h"

typedef struct device
{
    const char *name;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    struct device *next;
} device_t;

void device_initialize(void);
int device_register(device_t *device);
device_t *device_get_list(void);

#endif
