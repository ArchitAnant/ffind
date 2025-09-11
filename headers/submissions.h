#ifndef SUBMISSION_H
#define SUBMISSION_H

#include <liburing.h>
#include <pthread.h>
#include <limits.h>

// Forward declaration of threadpool
struct thpool_;
typedef struct thpool_ *threadpool;

// ---- Request structure ----
typedef struct {
    char path[PATH_MAX];
} Request;

// ---- Worker task args ----
typedef struct {
    int dir_fd;
    char path[PATH_MAX];
    const char *search_term;
    struct io_uring *ring;
    int *inflight_ops;
    pthread_mutex_t *ring_mutex;
} WorkerTaskArgs;

// ---- App context ----
typedef struct {
    const char *search_term;
    struct io_uring *ring;
    int *inflight_ops;
    pthread_mutex_t *ring_mutex;
    threadpool pool;
} AppContext;

// ---- Function declarations ----

// Flush batched SQEs
void flush_batch(struct io_uring *ring);

// Submit async openat request
void submit_open_request(const char *path, struct io_uring *ring,
                         int *inflight_ops, int force_flush);

// Handle completion of openat
void handle_completion(struct io_uring_cqe *cqe, AppContext *ctx);

// Worker: readdir + scheduling recursion
void readdir_worker_function(void *args);

#endif // SUBMISSION_H
