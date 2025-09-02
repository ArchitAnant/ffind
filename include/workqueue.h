#ifndef WORKQUEUE_H
#define WORKQUEUE_H

#include <pthread.h>
#include <limits.h>

// Represents a directory task with path and descriptor
typedef struct DirTask {
    char path[PATH_MAX];
    int dir_fd;
} DirTask;

// Node in the work queue
typedef struct WorkItem {
    DirTask task;
    struct WorkItem *next;
} WorkItem;

// Thread-safe work queue
typedef struct WorkQueue {
    WorkItem *head;
    WorkItem *tail;
    int is_shutdown;
    int active_worker;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} WorkQueue;

// Initializes a work queue
void init_work_queue(WorkQueue *q);

// Destroys a work queue (does not free remaining tasks)
void destroy_work_queue(WorkQueue *q);

// Enqueues a new task into the queue
void work_queue_enqueue(WorkQueue *q, DirTask task);

// Dequeues a task from the queue
// Returns 0 on success, -1 if shutdown and empty
int dequeue_work_queue(WorkQueue *q, DirTask *task);

// Marks a task as done (used by workers)
void work_queue_task_done(WorkQueue *q);

#endif // WORKQUEUE_H
