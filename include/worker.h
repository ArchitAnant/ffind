#ifndef WORKER_H
#define WORKER_H

#include "workqueue.h"   // Include the work queue definitions

// Structure to pass arguments to worker threads
typedef struct WorkerArgs {
    WorkQueue *queue;        // Pointer to the work queue
    const char *search_term; // Term to search for in file names
} WorkerArgs;

// Function to process a directory: enqueue subdirectories and print matching files
void process_dir(const char *path, const char *search_term, WorkQueue *q);

// Worker thread function: dequeues directories and processes them
void *worker_function(void *args);

#endif // WORKER_H
