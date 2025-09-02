#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <liburing.h>
#include <limits.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include "../include/worker.h"
#include "../include/workqueue.h"

// Forward declaration for the worker function defined in worker.c
void *worker_function(void *args);

// The Request struct is private to the io_uring dispatcher (this file).
typedef struct Request {
    char path[PATH_MAX];
} Request;

/*
 * submit_open_request: Prepares an async 'openat' request.
 * Called by both the bootstrap thread and worker threads.
 */
void submit_open_request(const char *path, struct io_uring *ring) {
    Request *req = malloc(sizeof(Request));
    if (!req) {
        perror("malloc request");
        return;
    }
    strncpy(req->path, path, sizeof(req->path) - 1);
    req->path[sizeof(req->path) - 1] = '\0';

    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    if (!sqe) {
        fprintf(stderr, "Warning: Could not get SQE, dropping task for %s\n", path);
        free(req);
        return;
    }

    io_uring_prep_openat(sqe, AT_FDCWD, path, O_RDONLY | O_DIRECTORY, 0);
    io_uring_sqe_set_data(sqe, req);
}

/*
 * handle_completion: Called by the main I/O loop when an operation completes.
 * Enqueues a task for a worker thread.
 */
void handle_completion(struct io_uring_cqe *cqe, WorkQueue *q) {
    Request *req = (Request *)io_uring_cqe_get_data(cqe);
    if (cqe->res < 0) {
        free(req);
        return;
    }

    DirTask task;
    task.dir_fd = cqe->res;
    strncpy(task.path, req->path, PATH_MAX - 1);
    task.path[PATH_MAX - 1] = '\0';

    work_queue_enqueue(q, task);
    free(req);
}

/*
 * NEW: Bootstrap thread to submit the very first task.
 * This keeps the main thread's logic pure.
 */
typedef struct BootstrapArgs {
    const char* path;
    struct io_uring *ring;
    pthread_mutex_t *ring_mutex;
} BootstrapArgs;

void *bootstrap_function(void *args) {
    BootstrapArgs *b_args = (BootstrapArgs*)args;
    
    pthread_mutex_lock(b_args->ring_mutex);
    submit_open_request(b_args->path, b_args->ring);
    pthread_mutex_unlock(b_args->ring_mutex);
    
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <path> <search_term>\n", argv[0]);
        exit(1);
    }
    const char* search_path = argv[1];
    const char* search_term = argv[2];
    printf("[SEARCH] Starting in '%s' for files containing '%s'\n", search_path, search_term);

    // --- 1. Initialization ---
    const long NPROC = sysconf(_SC_NPROCESSORS_ONLN);
    WorkQueue q;
    init_work_queue(&q);

    struct io_uring ring;
    io_uring_queue_init(256, &ring, 0);

    int inflight_ops = 0;
    pthread_mutex_t ring_mutex;
    pthread_mutex_init(&ring_mutex, NULL);

    // --- 2. Create worker threads ---
    pthread_t worker_threads[NPROC];
    WorkerArgs wargs = {&q, search_term, &ring, &ring_mutex};
    for (long i = 0; i < NPROC; i++) {
        pthread_create(&worker_threads[i], NULL, worker_function, &wargs);
    }

    // --- 3. Seed initial work via a bootstrap thread ---
    pthread_t bootstrap_thread;
    BootstrapArgs b_args = {search_path, &ring, &ring_mutex};
    pthread_create(&bootstrap_thread, NULL, bootstrap_function, &b_args);
    pthread_join(bootstrap_thread, NULL); // Wait for the first task to be queued.

    // --- 4. Main I/O Event Loop ---
     while (1) {
        // --- Phase 1: Atomically check system state and decide what to do next ---
        int should_break = 0;
        int has_new_submissions = 0;

        // Lock both mutexes to get a consistent, atomic snapshot of the entire system state.
        // A consistent lock order (e.g., ring then queue) prevents deadlocks.
        pthread_mutex_lock(&ring_mutex);
        pthread_mutex_lock(&q.mutex);

        // Check for new I/O work queued by the worker threads.
        unsigned unsubmitted_sqes = io_uring_sq_ready(&ring);
        if (unsubmitted_sqes > 0) {
            has_new_submissions = 1;
            inflight_ops += unsubmitted_sqes;
        }
        
        // Now, check the definitive shutdown condition.
        if (inflight_ops == 0 && q.head == NULL && q.active_worker == 0) {
            should_break = 1;
        }

        pthread_mutex_unlock(&q.mutex);
        pthread_mutex_unlock(&ring_mutex);

        if (should_break) {
            break;
        }

        // --- Phase 2: Wait for an event ---
        // This is the core logic: we wait on I/O if we expect I/O, otherwise
        // we wait for workers to make progress.

        if (inflight_ops > 0) {
            // State A: I/O is in flight or ready to be submitted.
            // Block until at least one I/O operation completes.
            int ret = io_uring_submit_and_wait(&ring, 1);
            if (ret < 0 && -ret != EINTR && -ret != EAGAIN) {
                perror("io_uring_submit_and_wait");
                break;
            }
        } else {
            // State B: No I/O is in flight. Workers must be busy.
            // Wait efficiently for a worker to finish a task, with a timeout.
            pthread_mutex_lock(&q.mutex);
            struct timespec wait_time;
            clock_gettime(CLOCK_REALTIME, &wait_time);
            wait_time.tv_sec += 1; // Wait for up to 1 second

            // This timed wait is woken up by work_queue_task_done() or the timeout.
            pthread_cond_timedwait(&q.cond, &q.mutex, &wait_time);
            pthread_mutex_unlock(&q.mutex);
            continue; // Go back to the top of the loop to re-evaluate state.
        }

        // --- Phase 3: Process I/O completions ---
        struct io_uring_cqe *cqe;
        unsigned head;
        unsigned completed_count = 0;
        io_uring_for_each_cqe(&ring, head, cqe) {
            handle_completion(cqe, &q);
            completed_count++;
        }
        
        if (completed_count > 0) {
            // Atomically decrement the inflight counter
            pthread_mutex_lock(&ring_mutex);
            inflight_ops -= completed_count;
            pthread_mutex_unlock(&ring_mutex);
            
            io_uring_cq_advance(&ring, completed_count);
        }
    }

    // --- 5. Graceful Shutdown ---
    pthread_mutex_lock(&q.mutex);
    q.is_shutdown = 1;
    pthread_cond_broadcast(&q.cond);
    pthread_mutex_unlock(&q.mutex);

    for (long i = 0; i < NPROC; i++) {
        pthread_join(worker_threads[i], NULL);
    }

    // --- 6. Final Cleanup ---
    destroy_work_queue(&q);
    pthread_mutex_destroy(&ring_mutex);
    io_uring_queue_exit(&ring);

    return 0;
}