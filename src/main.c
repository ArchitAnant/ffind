#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <liburing.h>
#include <limits.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>

#include "../headers/request.h"
#include "../headers/submissions.h"
#include "../headers/thpool.h"
#include "../headers/expr.h"

#define QUEUE_DEPTH 512

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <path> [expression...]\n", argv[0]);
        fprintf(stderr, "Example: %s /home -name '*.c' -type f\n", argv[0]);
        exit(1);
    }
    const char* search_path = argv[1];

    /* Build the expression tree from argv[2..argc-1].
     * NULL means "match everything" (no expression given). */
    pred_node_t *filter_tree = build_expression_tree(argc, argv, 2);

    const long NPROC = sysconf(_SC_NPROCESSORS_ONLN);
    struct io_uring ring;
    int ret = io_uring_queue_init(QUEUE_DEPTH, &ring, 0);
    if (ret < 0) {
        fprintf(stderr, "Fatal: io_uring_queue_init failed: %s\n", strerror(-ret));
        fprintf(stderr, "Tip: inside Docker, run with --privileged or a permissive seccomp profile.\n");
        exit(1);
    }

    int inflight_ops = 0;
    int active_tasks = 0;
    pthread_mutex_t ring_mutex        = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t task_counter_mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t  task_done_cond    = PTHREAD_COND_INITIALIZER;

    threadpool pool = thpool_init(NPROC);

    AppContext ctx = {
        .filter_tree        = filter_tree,
        .ring               = &ring,
        .inflight_ops       = &inflight_ops,
        .ring_mutex         = &ring_mutex,
        .pool               = pool,
        .active_task        = &active_tasks,
        .task_counter_mutex = &task_counter_mutex,
        .task_done_cond     = &task_done_cond,
    };

    /* Submit the initial openat for the root directory. */
    pthread_mutex_lock(&ring_mutex);
    submit_open_request(search_path, &ring, &inflight_ops);
    io_uring_submit(&ring);   /* first submit — no batch to form yet */
    pthread_mutex_unlock(&ring_mutex);

    /*
     * Main event loop.
     *
     * We have two sources of pending work:
     *   - inflight_ops: io_uring openat requests in flight
     *   - active_tasks: readdir worker tasks running in the thread pool
     *
     * Strategy:
     *   1. While io_uring has in-flight ops, wait for CQEs (io_uring_wait_cqe
     *      blocks efficiently in the kernel — no spinning, no usleep).
     *   2. When io_uring is drained but workers are still running, wait on a
     *      condition variable that each worker signals when it finishes.
     *      This replaces the old usleep(1000) busy-poll which added ~1ms
     *      of artificial latency per iteration.
     */
    while (1) {
        pthread_mutex_lock(&task_counter_mutex);
        int tasks_left = active_tasks;
        pthread_mutex_unlock(&task_counter_mutex);

        pthread_mutex_lock(&ring_mutex);
        int ops_left = inflight_ops;
        pthread_mutex_unlock(&ring_mutex);

        if (ops_left == 0 && tasks_left == 0)
            break;

        if (ops_left > 0) {
            struct io_uring_cqe *cqe;
            ret = io_uring_wait_cqe(&ring, &cqe);
            if (ret < 0) {
                if (-ret == EINTR) continue;
                perror("io_uring_wait_cqe");
                break;
            }
            unsigned head, count = 0;
            io_uring_for_each_cqe(&ring, head, cqe) {
                handle_completion(cqe, &ctx);
                count++;
            }
            io_uring_cq_advance(&ring, count);
        } else {
            /* No io_uring work — wait for a worker to signal completion.
             * pthread_cond_wait releases the mutex while sleeping, so
             * workers can still acquire it to signal. */
            pthread_mutex_lock(&task_counter_mutex);
            while (active_tasks > 0) {
                /* Re-check inflight_ops inside the lock to avoid a race
                 * where a worker just submitted new io_uring requests. */
                pthread_mutex_lock(&ring_mutex);
                int new_ops = inflight_ops;
                pthread_mutex_unlock(&ring_mutex);
                if (new_ops > 0) break;  /* back to io_uring loop */

                pthread_cond_wait(&task_done_cond, &task_counter_mutex);
            }
            pthread_mutex_unlock(&task_counter_mutex);
        }
    }

    thpool_wait(pool);
    thpool_destroy(pool);

    free_expression_tree(filter_tree);
    pthread_mutex_destroy(&ring_mutex);
    pthread_mutex_destroy(&task_counter_mutex);
    pthread_cond_destroy(&task_done_cond);
    io_uring_queue_exit(&ring);

    return 0;
}