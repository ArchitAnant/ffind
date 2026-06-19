#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <liburing.h>
#include <limits.h>
#include <pthread.h>
#include <unistd.h>

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

    /*
     * Build the expression tree from argv[2..argc-1].
     *
     * This mirrors findutils' build_expression_tree() call from
     * ftsfind.c main().  The tree is built once and shared read-only
     * across all worker threads.
     *
     * If no expression is given (argc == 2), build_expression_tree
     * returns NULL, which evaluate() treats as "match everything"
     * — same behavior as `find <path>` with no predicates.
     */
    pred_node_t *filter_tree = build_expression_tree(argc, argv, 2);

    /* If the user provided expression args but parsing failed,
     * build_expression_tree returns NULL and has already printed
     * an error.  In that case, we still get "match everything"
     * behavior.  A more strict approach would be to exit(1) here,
     * but for now we match the permissive GNU find behavior. */

  
    const long NPROC = sysconf(_SC_NPROCESSORS_ONLN);
    struct io_uring ring;
    io_uring_queue_init(256, &ring, 0);

    int inflight_ops = 0;
    int active_tasks = 0;
    pthread_mutex_t ring_mutex;
    pthread_mutex_t task_couter_mutex;
    pthread_mutex_init(&ring_mutex, NULL);
    pthread_mutex_init(&task_couter_mutex, NULL);


    threadpool pool = thpool_init(NPROC);

    AppContext ctx = { filter_tree, &ring, &inflight_ops, &ring_mutex, pool ,&active_tasks,&task_couter_mutex};

    pthread_mutex_lock(&ring_mutex);
    submit_open_request(search_path, &ring, &inflight_ops, 1); // force_flush = 1
    pthread_mutex_unlock(&ring_mutex);

    while (inflight_ops > 0 || active_tasks>0) {
        struct io_uring_cqe *cqe;

        if (inflight_ops>0)
        {
            int ret = io_uring_wait_cqe(&ring, &cqe);
            if (ret < 0) {
                if (-ret == EINTR) continue;
                perror("io_uring_wait_cqe");
                break;
            }

            unsigned head;
            unsigned count = 0;
            io_uring_for_each_cqe(&ring, head, cqe) {
                handle_completion(cqe, &ctx);
                count++;
            }
            io_uring_cq_advance(&ring, count);
        }
        else{
            usleep(1000);
        }
    }
    thpool_wait(pool); // Wait for all queued readdir tasks to finish.
    thpool_destroy(pool);

    free_expression_tree(filter_tree);
    pthread_mutex_destroy(&ring_mutex);
    io_uring_queue_exit(&ring);

    return 0;
}