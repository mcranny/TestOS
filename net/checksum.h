#ifndef CHECKSUM_H
#define CHECKSUM_H

#include "types.h"

/* RFC 1071 Internet checksum (ones' complement). */
uint16_t internet_checksum(const void *data, uint16_t length);

#endif
