#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <dirent.h>
#include <sys/stat.h>
#include <linux/stat.h>

typedef enum{
    OP_OPEN,
    OP_READ_DIRENTS,
    OP_STAT
}OpType;

typedef struct Request{
    OpType type;
    char path[PATH_MAX];
    int dir_fd;
    struct linux_dirent64 *dirents;
    struct statx statx_buf;
}