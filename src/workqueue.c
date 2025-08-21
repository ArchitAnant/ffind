#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>

typedef struct WorkItem{
    char *path;
    struct WorkItem *next;

}WorkItem;


typedef struct WorkQueue{
    WorkItem *head;
    WorkItem *tail;
    int active_worker;
    int is_finished;
    pthread_mutex_t mutex;
    pthread_cond_t cond;

}WorkQueue;


void init_work_queue(WorkQueue *q){

    // initialize the pointers to null
    q->head = NULL;
    q->tail = NULL;

    // set the queue's finished condition to not finished
    q->is_finished=0;
    q->active_worker=0;

    // init the pthreads
    pthread_mutex_init(&q->mutex,NULL);
    pthread_cond_init(&q->cond,NULL);
}

void destroy_work_queue(WorkQueue *q){
    // lock the queue
    pthread_mutex_lock(&q->mutex);
    // set the queue as complete
    q->is_finished=1;

    pthread_cond_broadcast(&q->cond);
    

    // free the items inside, if there are any
    WorkItem *curr = q->head;
    while (curr)
    {
        WorkItem *next = curr->next;
        free(curr->path);
        free(curr);
        curr = next;
    }
    pthread_mutex_unlock(&q->mutex);

    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->cond);
}


void work_queue_enque(WorkQueue *q,const char * path){
    pthread_mutex_lock(&q->mutex);

    WorkItem *newWork = (struct WorkItem*) malloc(sizeof(struct WorkItem));

    newWork->path = strdup(path);
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
char *dequeue_work_queue(WorkQueue *q){
    pthread_mutex_lock(&q->mutex);

    while (q->head == NULL && !q->is_finished) {
        pthread_cond_wait(&q->cond, &q->mutex);
    }

    if (q->head==NULL)
    {
        pthread_mutex_unlock(&q->mutex);
       return NULL;
    }
    
    WorkItem *item = q->head;

    q->head = q->head->next;
    if (q->head == NULL) q->tail = NULL;

    q->active_worker++;

    pthread_mutex_unlock(&q->mutex);

    char *return_path = item->path;
    free(item);
    return return_path;
}

void work_done(WorkQueue *q) {
    pthread_mutex_lock(&q->mutex);
    q->active_worker--;

    // If queue is empty and no active workers, signal main thread
    if (q->head == NULL && q->active_worker == 0) {
        pthread_cond_broadcast(&q->cond); 
    }

    pthread_mutex_unlock(&q->mutex);
}
