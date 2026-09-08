#ifndef SOCKET_H
#define SOCKET_H

#include "types.h"
#include "ipv4.h"

/* Kernel-only, polling socket API. Handles include a generation counter. */
#define SOCKET_MAX 8U
#define SOCKET_RX_MAX 2048U
#define SOCKET_WOULD_BLOCK 0
#define SOCKET_EOF (-1)
#define SOCKET_ERROR (-2)

void socket_init(void);
int socket_create(void);
int socket_bind(int handle, uint16_t port);
int socket_listen(int handle, uint8_t backlog);
int socket_accept(int handle);
int socket_connect(int handle, ipv4_addr_t address, uint16_t port, uint16_t local_port);
int socket_send(int handle, const void *data, uint16_t length);
int socket_recv(int handle, void *data, uint16_t capacity);
int socket_close(int handle);

#endif
