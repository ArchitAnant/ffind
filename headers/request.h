#ifndef REQUEST_H
#define REQUEST_H

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <dirent.h>
#include <sys/stat.h>
#include <linux/stat.h>
#include <linux/limits.h>
#include <linux/types.h>
#include <dirent.h>

// Operation types
typedef enum {
    OP_OPEN,
    OP_READ_DIRENTS,
    OP_STAT
} OpType;

// Request structure
typedef struct Request {
    OpType type;
    char path[PATH_MAX];
    int dir_fd;
    struct linux_dirent64 *dirents; // Buffer for directory entries
    struct statx statx_buf;         // Buffer for statx results
} Request;

#endif // REQUEST_H
