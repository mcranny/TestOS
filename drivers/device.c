#include "device.h"

static device_t *device_list_head;

void device_initialize(void)
{
    device_list_head = NULL;
}

int device_register(device_t *device)
{
    if (device == NULL)
    {
        return 0;
    }

    device->next = device_list_head;
    device_list_head = device;
    return 1;
}

device_t *device_get_list(void)
{
    return device_list_head;
}
