#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <liburing.h>
#include <limits.h>
#include <pthread.h>

#include "../headers/request.h"
#include "../headers/submissions.h"
#include "../headers/thpool.h"



#define QUEUE_DEPTH 512



int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <path> <search_term>\n", argv[0]);
        exit(1);
    }
    const char* search_path = argv[1];
    const char* search_term = argv[2];
    printf("[KICKOFF] searching for '%s' in '%s'\n", search_term, search_path);

    // --- 1. Initialization ---
    const long NPROC = sysconf(_SC_NPROCESSORS_ONLN);
    struct io_uring ring;
    io_uring_queue_init(256, &ring, 0);

    int inflight_ops = 0;
    pthread_mutex_t ring_mutex;
    pthread_mutex_init(&ring_mutex, NULL);

    // --- 2. Create the Thread Pool ---
    threadpool pool = thpool_init(NPROC);

    // --- 3. Setup App Context ---
    AppContext ctx = { search_term, &ring, &inflight_ops, &ring_mutex, pool };

    // --- 4. Seed Initial Work ---
    pthread_mutex_lock(&ring_mutex);
    submit_open_request(search_path, &ring, &inflight_ops, 1); // force_flush = 1
    pthread_mutex_unlock(&ring_mutex);

    // --- 5. Main I/O Event Loop ---
    while (inflight_ops > 0) {
        struct io_uring_cqe *cqe;
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

    // --- 6. Graceful Shutdown ---
    printf("I/O operations complete. Waiting for workers...\n");
    thpool_wait(pool); // Wait for all queued readdir tasks to finish.
    thpool_destroy(pool);

    // --- 7. Final Cleanup ---
    pthread_mutex_destroy(&ring_mutex);
    io_uring_queue_exit(&ring);

    printf("Search complete.\n");
    return 0;
}