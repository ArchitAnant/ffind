#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <liburing.h>
#include <limits.h>

#include "../headers/request.h"
#include "../headers/submissions.h"



#define QUEUE_DEPTH 256

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <path> <search_term>\n", argv[0]);
        return 1;
    }
    const char* search_path = argv[1];
    const char* search_term = argv[2];

    struct io_uring ring;
    int ret = io_uring_queue_init(QUEUE_DEPTH, &ring, 0);
    if (ret < 0) {
        fprintf(stderr, "io_uring_queue_init: %s\n", strerror(-ret));
        return 1;
    }
    printf("[KICKOFF] searching regex <%s> : : %s\n",search_term,search_path);
    int inflight_ops = 0;
    // Kick off the very first task.
    submit_open_request(search_path, &ring, &inflight_ops);

    // The main event loop. It runs as long as there are operations in flight.
    while (inflight_ops > 0) {
        io_uring_submit_and_wait(&ring, 1);

        struct io_uring_cqe *cqe;
        unsigned head;
        unsigned count = 0;

        // Process all completions that are ready.
        io_uring_for_each_cqe(&ring, head, cqe) {
            handle_completion(cqe, search_term, &ring, &inflight_ops);
            inflight_ops--;
            count++;
        }

        io_uring_cq_advance(&ring, count);
    }

    io_uring_queue_exit(&ring);
    return 0;
}