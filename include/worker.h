#ifndef WORKER_H
#define WORKER_H

#include "workqueue.h"

// Arguments passed to each worker thread
typedef struct WorkerArgs {
    WorkQueue *queue;
    const char *search_term;
} WorkerArgs;

// Recursively process a directory, enqueueing subdirectories
// and printing files that match the search term
void process_dir(const char *path, const char *search_term, WorkQueue *q);

// Worker thread function
// Continuously dequeues directories from the work queue and processes them
void *worker_function(void *args);

#endif // WORKER_H
