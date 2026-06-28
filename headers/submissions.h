#ifndef SUBMISSION_H
#define SUBMISSION_H

#include <liburing.h>
#include <pthread.h>
#include <limits.h>

// Forward declaration of threadpool
struct thpool_;
typedef struct thpool_ *threadpool;

// ---- Function declarations ----

// Submit async openat SQE (returns 1 on success, 0 on failure).
// Does NOT call io_uring_submit — caller batches and submits after readdir pass.
int submit_open_request(const char *path, struct io_uring *ring,
                        int *inflight_ops);

// Handle completion of openat
void handle_completion(struct io_uring_cqe *cqe, AppContext *ctx);

// Worker: readdir + scheduling recursion
void readdir_worker_function(void *args);

#endif // SUBMISSION_H
