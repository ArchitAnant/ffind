#ifndef REQUEST_H
#define REQUEST_H

#include <sys/types.h>
#include <linux/stat.h>
#include <liburing.h>
#include <linux/limits.h>
#include <dirent.h>


// Operation types
// typedef enum {
//     OP_OPEN,
//     OP_READ_DIRENTS,
//     OP_STAT
// } OpType;

// struct linux_dirent64 {
//     uint64_t        d_ino;    /* 64-bit inode number */
//     int64_t        d_off;    /* 64-bit offset to next structure */
//     unsigned short d_reclen; /* Size of this dirent */
//     unsigned char  d_type;   /* File type */
//     char           d_name[]; /* Filename (null-terminated) */
// };
// Request structure
typedef struct Request {
    // OpType type;
    char path[PATH_MAX];
    // int dir_fd;
    // struct linux_dirent64 *dirents; // Buffer for directory entries
    // struct statx statx_buf;         // Buffer for statx results
} Request;

#endif // REQUEST_H
