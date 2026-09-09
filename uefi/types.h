#ifndef TESTOS_UEFI_TYPES_H
#define TESTOS_UEFI_TYPES_H

typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef signed short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef long long int64_t;
typedef unsigned long size_t;
typedef unsigned long uintptr_t;

#define UINT64_MAX 0xffffffffffffffffULL
#define NULL ((void *)0)

#ifndef __bool_true_false_are_defined
typedef _Bool bool;
#define true 1
#define false 0
#endif

#endif
