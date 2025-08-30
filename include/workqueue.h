#ifndef WORKQUEUE_H
#define WORKQUEUE_H

#include <pthread.h>
#include <limits.h>

/*
 * DirTask: Represents a single unit of work for a worker thread.
 * It contains an open file descriptor for a directory and its full path.
 */
typedef struct DirTask {
    char path[PATH_MAX];
    int dir_fd;
} DirTask;

/*
 * WorkItem: A node in the linked list that makes up the queue.
 * This is an internal implementation detail and is not needed by other files,
 * but it must be declared here because WorkQueue references it.
 */
typedef struct WorkItem {
    DirTask task;
    struct WorkItem *next;
} WorkItem;

/*
 * WorkQueue: A thread-safe queue for distributing directory scanning tasks.
 */
typedef struct WorkQueue {
    WorkItem *head;
    WorkItem *tail;
    int is_shutdown;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} WorkQueue;


/* --- PUBLIC API --- */

/*
 * Initializes a WorkQueue's synchronization primitives.
 * Must be called before the queue is used.
 *
 * @param q A pointer to the WorkQueue to initialize.
 */
void init_work_queue(WorkQueue *q);

/*
 * Destroys a WorkQueue's synchronization primitives.
 * The queue should be empty when this is called.
 *
 * @param q A pointer to the WorkQueue to destroy.
 */
void destroy_work_queue(WorkQueue *q);

/*
 * Enqueues a new directory task for a worker to process.
 * This function is thread-safe.
 *
 * @param q A pointer to the WorkQueue.
 * @param task The DirTask to be added to the queue.
 */
void work_queue_enqueue(WorkQueue *q, DirTask task);

/*
 * Dequeues a directory task for a worker to process.
 * Blocks until a task is available or the queue is shut down.
 * This function is thread-safe.
 *
 * @param q A pointer to the WorkQueue.
 * @param task A pointer to a DirTask struct where the dequeued task will be stored.
 * @return 0 on success, -1 if the queue has been shut down and is empty.
 */
int work_queue_dequeue(WorkQueue *q, DirTask *task);


#endif // WORKQUEUE_H