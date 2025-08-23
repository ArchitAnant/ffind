#ifndef REQUEST_H
#define REQUEST_H

#include <limits.h>
#include <linux/limits.h>

typedef struct Request {
    char path[PATH_MAX];
} Request;

#endif // REQUEST_H
