#ifndef TESTOS_UEFI_USER_ULIB_H
#define TESTOS_UEFI_USER_ULIB_H

#include "types.h"

void uwrite(const char *string);
void uexit(int status);
void uyield(void);
void usleep(unsigned long ticks);

int usocket(void);
int ubind(int sock, unsigned short port);
int ulisten(int sock, unsigned char backlog);
int uaccept(int sock);
int uconnect(int sock, unsigned int ip, unsigned short port);
int usend(int sock, const void *buf, unsigned short len);
int urecv(int sock, void *buf, unsigned short len);
int uclose(int sock);
int uresolve(const char *host, unsigned int *ip_out);
int ufswrite(const char *path, const void *buf, unsigned int len);

void uwrite_u32(unsigned int value);
void uwrite_ip(unsigned int ip);

#endif
