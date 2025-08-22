#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include "../include/worker.h"
#include "../include/workqueue.h"


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


    // init an array for number of threads
    pthread_t threads[NPROC];

    // init the workerargs
    WorkerArgs wags;
    wags.queue = &q;
    wags.search_term = search_term;

    // starting the worker threads
    for (int i = 0; i < NPROC; i++) {
        if (pthread_create(&threads[i], NULL, worker_function, &wags) != 0) {
            perror("Failed to create thread");
            return 1;
        }
    }

    // seeding and enqueuing the initial path and queue
    work_queue_enque(&q,search_path);


    // WAIT AND SHUT DOWN PROCESS
    // wait till the queue is empty
    pthread_mutex_lock(&q.mutex);
    while (q.head != NULL || q.active_worker > 0) {
        pthread_cond_wait(&q.cond, &q.mutex);
    }
    pthread_mutex_unlock(&q.mutex);

    pthread_mutex_lock(&q.mutex);
    q.is_finished = 1;
    pthread_cond_broadcast(&q.cond);
    pthread_mutex_unlock(&q.mutex);

    // join all the threads
    for (int i = 0; i < NPROC; i++)
    {
        pthread_join(threads[i],NULL);
    }

    // work queue clean up
    destroy_work_queue(&q);
    
    return 0;
}