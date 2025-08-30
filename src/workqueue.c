#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <limits.h>

typedef struct DirTask{
    char path[PATH_MAX];
    int dir_fd;
}DirTask;

typedef struct WorkItem{
    DirTask task;
    struct WorkItem *next;

}WorkItem;


typedef struct WorkQueue{
    WorkItem *head;
    WorkItem *tail;
    int active_worker;
    int is_shutdown;
    pthread_mutex_t mutex;
    pthread_cond_t cond;

}WorkQueue;


void init_work_queue(WorkQueue *q){

    // initialize the pointers to null
    q->head = NULL;
    q->tail = NULL;

    // set the queue's finished condition to not finished
    q->is_shutdown=0;
    q->active_worker=0;

    // init the pthreads
    pthread_mutex_init(&q->mutex,NULL);
    pthread_cond_init(&q->cond,NULL);
}

void destroy_work_queue(WorkQueue *q){

    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->cond);

}


void work_queue_enqueue(WorkQueue *q,DirTask task){
    pthread_mutex_lock(&q->mutex);

    WorkItem *newWork = malloc(sizeof(struct WorkItem));

    newWork->task = task;
    newWork->next = NULL;

    if (q->tail)
    {
        q->tail->next = newWork;
        q->tail = newWork;
    }
    else
    {
        q->head = q->tail = newWork;  
    }

    pthread_cond_signal(&q->cond);    
    pthread_mutex_unlock(&q->mutex);
}


// dequeue_work_queue: returns a dynamically allocated string
// Caller must free() the returned string after use to avoid memory leaks
int dequeue_work_queue(WorkQueue *q, DirTask *task){
    pthread_mutex_lock(&q->mutex);

    while (q->head == NULL && !q->is_shutdown) {
        pthread_cond_wait(&q->cond, &q->mutex);
    }

    if (q->is_shutdown && q->head==NULL)
    {
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }

    WorkItem *item = q->head;
    q->head = q->head->next;
    if (q->head==NULL)
    {
        q->tail = NULL;
    }

    pthread_mutex_unlock(&q->mutex);

    *task = item->task;
    free(item);
    return 0;
    
}