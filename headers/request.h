#ifndef REQUEST_H
#define REQUEST_H

#include <limits.h>
#include <pthread.h>
#include <liburing.h>

#include "expr.h"

// Forward declare threadpool type
typedef struct thpool_* threadpool;

// ---- Request structure ----
typedef struct Request {
    char path[PATH_MAX];
} Request;

// ---- Worker task arguments ----
typedef struct WorkerTaskArgs{
    int dir_fd;
    char path[PATH_MAX];
    pred_node_t *filter_tree;   /* expression tree (shared, read-only) */
    struct io_uring *ring;
    int *inflight_ops;
    pthread_mutex_t *ring_mutex;
    int *active_task;
    pthread_mutex_t *task_counter_mutex;
    pthread_cond_t  *task_done_cond;  /* signalled when active_task decrements */
}WorkerTaskArgs;

// ---- Application context ----
typedef struct AppContext{
    pred_node_t *filter_tree;   /* expression tree (shared, read-only) */
    struct io_uring *ring;
    int *inflight_ops;
    pthread_mutex_t *ring_mutex;
    threadpool pool;
    int *active_task;
    pthread_mutex_t *task_counter_mutex;
    pthread_cond_t  *task_done_cond;  /* signalled when a worker finishes */
}AppContext;

#endif // REQUEST_H
