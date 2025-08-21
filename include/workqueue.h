#ifndef WORK_QUEUE_H
#define WORK_QUEUE_H

#include <pthread.h>

// Structure for a single work item
typedef struct WorkItem {
    char *path;
    struct WorkItem *next;
} WorkItem;

// Structure for the work queue
typedef struct WorkQueue {
    WorkItem *head;
    WorkItem *tail;
    int is_finished;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} WorkQueue;

// Initialize the work queue
void init_work_queue(WorkQueue *q);

// Destroy the work queue and free all resources
void destroy_work_queue(WorkQueue *q);

// Add a new path to the work queue
void work_queue_enque(WorkQueue *q, const char *path);

// Remove a path from the work queue
// Returns a dynamically allocated string that must be freed by the caller
char *dequeue_work_queue(WorkQueue *q);

#endif // WORK_QUEUE_H
