#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <liburing.h>
#include <limits.h>

#include "../headers/request.h"
#include "../headers/submissions.h"


#define DIRENT_BUF_SIZE  32768


int main(int argc,char *argv[]){
    if (argc!=3)
    {
        fprintf(stderr,"Usage: %s <path> <search_term>\n", argv[0]);
        exit(1);
    }
    const char* search_path = argv[1];
    const char* search_term = argv[2];
    printf("[SEARCH] Starting in '%s' for files containing '%s'\n",search_path,search_term);


    struct io_uring ring;

    int ret = io_uring_queue_init(256,&ring,0);
    if (ret < 0) { fprintf(stderr, "io_uring_queue_init: %s\n", strerror(-ret)); return 1; }
    int inflight_ops = 0;

    submit_open_request(search_path,&ring,&inflight_ops);
    while (1)
    {
        io_uring_submit_and_wait(&ring,1);
    
    
        struct io_uring_cqe *cqe;
        unsigned head;
        unsigned count=0;

        io_uring_for_each_cqe(&ring,head,cqe){
            handle_completion(cqe,search_term,&ring,&inflight_ops);

            // free(req);
            inflight_ops--;
            count++;
        }

        io_uring_cq_advance(&ring, count);

        if (inflight_ops==0)
        {
            break;
        }
    }

    io_uring_queue_exit(&ring);
    
    return 0;
}