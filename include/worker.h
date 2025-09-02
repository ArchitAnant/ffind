#ifndef WORKER_H
#define WORKER_H

#include <pthread.h>
#include <liburing.h>
#include "workqueue.h"

// Arguments passed to each worker thread
typedef struct WorkerArgs {
    WorkQueue *queue;             // Shared work queue
    const char *search_term;      // Term to search for in file names
    struct io_uring *ring;        // io_uring instance
    pthread_mutex_t *ring_mutex;  // Mutex to synchronize ring usage
} WorkerArgs;

// Submits an open request for a given path to io_uring
void submit_open_request(const char *path, struct io_uring *ring);

// Worker thread function
// Each worker dequeues tasks and processes directories/files
void *worker_function(void *args);

#endif // WORKER_H
