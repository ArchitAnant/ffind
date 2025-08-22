#ifndef WORK_QUEUE_H
#define WORK_QUEUE_H

#include <pthread.h>

// Work item structure
typedef struct WorkItem {
    char *path;
    struct WorkItem *next;
} WorkItem;

// Work queue structure
typedef struct WorkQueue {
    WorkItem *head;
    WorkItem *tail;
    int active_worker;   // number of workers currently processing items
    int is_finished;     // flag to indicate queue is finished
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} WorkQueue;

// Initialize the work queue
void init_work_queue(WorkQueue *q);

// Destroy the work queue and free resources
void destroy_work_queue(WorkQueue *q);

// Enqueue a new path into the work queue
void work_queue_enque(WorkQueue *q, const char *path);

// Dequeue a path from the work queue
// Returns a dynamically allocated string; caller must free it
char *dequeue_work_queue(WorkQueue *q);

// Mark that a worker has finished processing an item
void work_done(WorkQueue *q);

#endif // WORK_QUEUE_H
