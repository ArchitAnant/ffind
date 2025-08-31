#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <liburing.h>
#include <limits.h>
#include <fcntl.h>
#include <string.h>

#include "../include/worker.h"
#include "../include/workqueue.h"

void *worker_function(void *args);

typedef struct Request {
    char path[PATH_MAX];
} Request;

void handle_completion(struct io_uring_cqe *cqe, WorkQueue *q){
    Request *req = (Request *)io_uring_cqe_get_data(cqe);

    if (cqe->res<0)
    {
        free(req);
        return;
    }

    DirTask task;
    task.dir_fd = cqe->res;
    strncpy(task.path, req->path,PATH_MAX);

    work_queue_enqueue(q,task);
    free(req);
}

void submit_open_request(const char *path, struct io_uring *ring, int *inflight_ops) {
    Request *req = malloc(sizeof(Request));
    if (!req) {
        perror("malloc request");
        return;
    }
    strncpy(req->path, path, sizeof(req->path) - 1);
    req->path[sizeof(req->path) - 1] = '\0';

    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    if (!sqe) {
        // This is a rare condition, means the submission queue is full.
        // A more robust app might handle this by waiting. We'll just drop the task.
        fprintf(stderr, "Warning: Could not get SQE, dropping task for %s\n", path);
        free(req);
        return;
    }
    
    io_uring_prep_openat(sqe, AT_FDCWD, path, O_RDONLY | O_DIRECTORY, 0);
    io_uring_sqe_set_data(sqe, req);

    (*inflight_ops)++;
}


int main(int argc,char *argv[]){

    if (argc!=3)
    {
        fprintf(stderr,"Usage: %s <path> <search_term>\n", argv[0]);
        exit(1);
    }
    const char* search_path = argv[1];
    const char* search_term = argv[2];
    printf("[SEARCH] Starting in '%s' for files containing '%s'\n",search_path,search_term);


    // determining a safe thread count
    const long NPROC = 2*sysconf(_SC_NPROCESSORS_ONLN);

    // init the worker queue
    WorkQueue q;
    init_work_queue(&q);

    struct io_uring ring;
    io_uring_queue_init(256, &ring, 0);

    int inflight_ops = 0;
    pthread_mutex_t ring_mutex;
    pthread_mutex_init(&ring_mutex, NULL);


    // init an array for number of threads
    pthread_t threads[NPROC];

    // init the workerargs
    WorkerArgs wargs = {&q, search_term, &ring, &inflight_ops, &ring_mutex};

    for (long i = 0; i < NPROC; i++)
    {
       pthread_create(&threads[i], NULL, worker_function, &wargs);
    }
    
    pthread_mutex_lock(&ring_mutex);
    submit_open_request(search_path, &ring, &inflight_ops);
    pthread_mutex_unlock(&ring_mutex);


    while (1)
    {
        pthread_mutex_lock(&q.mutex);
        int is_queue_empty = (q.head == NULL);
        pthread_mutex_unlock(&q.mutex);

        unsigned unsubmitted_sqes = io_uring_sq_ready(&ring);
        if (unsubmitted_sqes>0)
        {
           inflight_ops+=unsubmitted_sqes;
        }
        

        if (inflight_ops == 0 && is_queue_empty)
        {
            io_uring_submit(&ring);
            break;
        }
        int submited_count = io_uring_submit_and_wait(&ring,1);
        if (submited_count<0 && -submited_count!=EAGAIN)
        {
            perror("io_uring_submit_and_wait");
            break;
        }
        

        struct io_uring_cqe *cqe;
        unsigned head;
        unsigned count = 0;

        io_uring_for_each_cqe(&ring, head, cqe){
            handle_completion(cqe, &q);
            inflight_ops--;
            count++;
        }

        io_uring_cq_advance(&ring, count);
    }

    pthread_mutex_lock(&q.mutex);
    q.is_shutdown=1;
    pthread_cond_broadcast(&q.cond);
    pthread_mutex_unlock(&q.mutex);


    for (long i = 0; i < NPROC; i++)
    {
        pthread_join(threads[i],NULL);
    }

    destroy_work_queue(&q);
    pthread_mutex_destroy(&ring_mutex);
    io_uring_queue_exit(&ring);
    
    
    return 0;
}