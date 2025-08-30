#ifndef WORKER_H
#define WORKER_H

#include <pthread.h>
#include <liburing.h>
#include "workqueue.h" // The worker needs to know about the WorkQueue type

/*
 * WorkerArgs: A struct to pass all shared resources and arguments to each
 * worker thread upon creation.
 */
typedef struct WorkerArgs {
    WorkQueue *queue;            // Pointer to the shared task queue
    const char *search_term;     // The substring to search for in filenames
    struct io_uring *ring;       // Pointer to the shared io_uring instance
    int *inflight_ops;           // Pointer to the counter for pending async operations
    pthread_mutex_t *ring_mutex; // Pointer to the mutex protecting the io_uring submission queue
} WorkerArgs;


/* --- PUBLIC API --- */

/*
 * The main entry point for each worker thread.
 * This function contains the primary worker loop: it dequeues directory tasks,
 * scans them using readdir, and submits new asynchronous 'open' requests for
 * any subdirectories it finds.
 *
 * @param args A void pointer to a WorkerArgs struct containing all necessary
 *             shared state and arguments.
 * @return Always returns NULL.
 */
void *worker_function(void *args);

/*
 * NOTE: The function 'submit_open_request' is *called* by the worker, but its
 * definition belongs in the file that manages the io_uring dispatcher (e.g., main.c).
 * A forward declaration for it should be included in any file that calls it.
 * Example (to be placed in worker.c):
 *
 * void submit_open_request(const char *path, struct io_uring *ring, int *inflight_ops);
 *
 */

#endif // WORKER_H