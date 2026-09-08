#ifndef EXEC_H
#define EXEC_H

#include "types.h"

int process_exec(
    const char *path,
    uint32_t parent_pid,
    int argc,
    const char **argv
);
int process_kill(uint32_t pid);

#endif
