#ifndef TESTOS_UEFI_USER_EXEC_H
#define TESTOS_UEFI_USER_EXEC_H

#include "types.h"

int process_exec(const char *path, int argc, const char **argv);
int spawn_hello_user(void);
int seed_calc_from_blob(void);

#endif
